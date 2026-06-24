#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/poll.h>
#include <linux/device.h>

#define DEVICE_NAME "leetx_queue"
#define CLASS_NAME "leetx"
#define DEFAULT_FIFO_SIZE 4096  //等价于4kb

static int fifo_size = DEFAULT_FIFO_SIZE;
module_param(fifo_size, int, 0644);
MODULE_PARM_DESC(fifo_size, "环形缓冲区大小（字节），默认 4096");


struct leetx_queue_dev {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;
    wait_queue_head_t read_wq;
    DECLARE_KFIFO_PTR(fifo, u32);
    struct mutex write_lock;
    atomic_t opened;
};

static struct leetx_queue_dev *g_dev;

static int leetx_open(struct inode *inode, struct file *filp)
{
    filp->private_data = g_dev;
    if (atomic_cmpxchg(&g_dev->opened, 0, 1) != 0)
        return -EBUSY;
    pr_info("leetx_queue: device opened\n");
        return 0;
}

static ssize_t leetx_write(struct file *filp, const char __user *buf,
                           size_t count, loff_t *f_pos)
{
    struct leetx_queue_dev *dev = filp->private_data;
    //计算要写入多少个u32元素
    size_t max_count = count / sizeof(u32);  //count为字节数
    if(max_count == 0) {
        return -EINVAL; //如果没有完整的u32元素，返回错误
    }
    //从用户空间拷贝到内核临时缓冲区
    u32 *kbuf = kmalloc(max_count * sizeof(u32), GFP_KERNEL);
    if (!kbuf) {
        return -ENOMEM; //内存分配失败
    }
    if (copy_from_user(kbuf, buf, max_count * sizeof(u32))) {
        kfree(kbuf);
        return -EFAULT; //拷贝失败  
    }

    //加锁并写入kfifo，mutex_lock 保护 kfifo_in，防止多个 writer 同时改 in 指针
    unsigned int written;
    mutex_lock(&dev->write_lock);
    written = kfifo_in(&dev->fifo, kbuf, max_count);
    mutex_unlock(&dev->write_lock);
    //释放缓冲区
    kfree(kbuf);

    //唤醒等待队列上休眠的reader
    wake_up_interruptible(&dev->read_wq);

    //返回实际写入的字节数
    return written * sizeof(u32);

}

static ssize_t leetx_read(struct file *filp, char __user *buf,
                          size_t count, loff_t *f_pos)
{
    struct leetx_queue_dev *dev = filp->private_data;
    unsigned int read_count;//本次要读取元素个数
    unsigned int actual;//实际读取元素个数
    u32 *kbuf;
    int ret;

    //计算要读取多少个u32元素
    read_count = count / sizeof(u32);
    if (read_count == 0) {
        return -EINVAL; //如果没有完整的u32元素，返回错误
    }

    //处理非阻塞模式
    if (filp->f_flags & O_NONBLOCK) {
        if (kfifo_is_empty(&dev->fifo)) {
            return -EAGAIN; //如果kfifo为空，返回EAGAIN
        }
    } //阻塞等待数据
    else {
        ret = wait_event_interruptible(dev->read_wq, !kfifo_is_empty(&dev->fifo));
        if (ret) {
            return ret; //如果被信号中断，返回错误码
        }
    }

    kbuf = kmalloc(read_count * sizeof(u32), GFP_KERNEL);
    if (!kbuf) {
        return -ENOMEM; //内存分配失败
    }

    /*
     * kfifo_out：从 fifo 取出数据
     * 返回实际取出的元素个数（不是字节数）
     * 如果 fifo 里的数据不够 read_count 个，只取出实际有的
     *
     * 这里暂不做互斥是因为我们只论单读者，如果考虑多读者的逻辑
     * 我们还需要读者锁,后续可以让我们考虑这一步
     */
    actual = kfifo_out(&dev->fifo, kbuf, read_count);

    //将数据从内核缓冲区拷贝到用户空间
    if (copy_to_user(buf, kbuf, actual * sizeof(u32))) {
        kfree(kbuf);
        return -EFAULT; //拷贝失败  
    }
    kfree(kbuf);

    //返回实际读取的字节数
    return actual * sizeof(u32);    

}

static int leetx_release(struct inode *inode, struct file *filp)
{
    atomic_set(&g_dev->opened, 0);
    pr_info("leetx_queue: device closed\n");
    return 0;
}

static const struct file_operations leetx_fops = {
    .owner   = THIS_MODULE,      /* 所属模块，防止模块卸载时还在使用 */
    .open    = leetx_open,       /* 用户 open()  → 调用 leetx_open() */
    .release = leetx_release,    /* 用户 close() → 调用 leetx_release() */
    .write   = leetx_write,      /* 用户 write() → 调用 leetx_write() */
    .read    = leetx_read,       /* 用户 read()  → 调用 leetx_read() */
};

static int __init leetx_queue_init(void)
{
    int ret;
    struct leetx_queue_dev *dev;

    pr_info("leetx_queue: initializing...\n");
    dev = kzalloc(sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        pr_err("leetx_queue: kzalloc failed\n");
        return -ENOMEM;
    }

    //初始化kfifo
    ret = kfifo_alloc(&dev->fifo, fifo_size, GFP_KERNEL);
    if (ret) {
        pr_err("leetx_queue: kfifo_alloc failed\n");
        goto err_free_dev;
    }

    mutex_init(&dev->write_lock);           /* 互斥锁 → 未锁定状态 */
    init_waitqueue_head(&dev->read_wq);     /* 等待队列 → 空队列 */
    atomic_set(&dev->opened, 0);            /* 打开标记 → 未打开 */


    //注册字符设备号
    ret = alloc_chrdev_region(&dev->devno,0,1,DEVICE_NAME);
    if (ret) {
        pr_err("leetx_queue: alloc_chrdev_region failed\n");
        goto err_free_fifo;
    }
    cdev_init(&dev->cdev, &leetx_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devno, 1);
    if (ret) {
        pr_err("leetx_queue: cdev_add failed\n");
        goto err_unregister_region;
    } 

     /*
     * 第 5 步：创建设备节点 /dev/leetx_queue
     *
     * class_create：创建 /sys/class/leetx/ 目录
     * device_create：在 /sys/class/leetx/ 下创建设备，
     *                内核会自动在 /dev/ 下生成结点文件
     *
     * 这两个函数帮我们省掉了手动 mknod 的步骤
     */
    //创建设备节点 这两个函数帮我们省掉了手动 mknod 的步骤
    dev->class = class_create(THIS_MODULE, CLASS_NAME);
    if (IS_ERR(dev->class)) {
        pr_err("leetx_queue: class_create failed\n");
        ret = PTR_ERR(dev->class);
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, NULL, dev->devno,
                                NULL, DEVICE_NAME);
    if (IS_ERR(dev->device)) {
        pr_err("leetx_queue: device_create failed\n");
        ret = PTR_ERR(dev->device);
        goto err_class_destroy;
    }
     /*
     * 全部成功！把指针存到全局变量
     * open 函数通过 g_dev 找到设备
     */
    g_dev = dev;
    pr_info("leetx_queue: initialized, fifo_size=%d bytes, "
            "capacity=%u samples\n",
            fifo_size, fifo_size / (unsigned int)sizeof(u32));
    return 0;

err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_region:
    unregister_chrdev_region(dev->devno, 1);
err_free_fifo:
    kfifo_free(&dev->fifo);
err_free_dev:
    kfree(dev);
    return ret;
}

static void __exit leetx_queue_exit(void)
{
    struct leetx_queue_dev *dev = g_dev;

    if (!dev)       /* 防止 g_dev 为空时崩溃 */
        return;

    /* 顺序和 init 里的正好相反 */
    device_destroy(dev->class, dev->devno);     /* 删 /dev/leetx_queue */
    class_destroy(dev->class);                   /* 删 /sys/class/leetx */
    cdev_del(&dev->cdev);                       /* 注销 cdev */
    unregister_chrdev_region(dev->devno, 1);    /* 归还设备号 */
    kfifo_free(&dev->fifo);                     /* 释放 kfifo */
    mutex_destroy(&dev->write_lock);            /* 销毁互斥锁 */
    kfree(dev);                                  /* 释放设备结构体 */
    g_dev = NULL;

    pr_info("leetx_queue: module removed\n");
}


module_init(leetx_queue_init);
module_exit(leetx_queue_exit);


MODULE_LICENSE("GPL");                                  /* 许可证，必须写 */
MODULE_AUTHOR("Yang");                             /* 你的名字 */
MODULE_DESCRIPTION("Industrial blocking data queue - Leetx Stage 1 Task 1");

