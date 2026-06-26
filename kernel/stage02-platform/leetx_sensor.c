#include <linux/module.h>       // module_init, MODULE_LICENSE
#include <linux/kernel.h>       // pr_info, pr_err
#include <linux/fs.h>           // struct file_operations
#include <linux/cdev.h>         // cdev_init, cdev_add
#include <linux/uaccess.h>      // copy_from_user, copy_to_user
#include <linux/slab.h>         // kmalloc, kfree
#include <linux/kfifo.h>        // DECLARE_KFIFO_PTR, kfifo_alloc, kfifo_in, kfifo_out
#include <linux/wait.h>         // wait_event_interruptible, wake_up
#include <linux/mutex.h>        // mutex_init, mutex_lock
#include <linux/device.h>       // class_create, device_create
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>

#define DEVICE_NAME "leetx_sensor"
#define DEFAULT_FIFO_SIZE 4096

static int fifo_size = DEFAULT_FIFO_SIZE;
module_param(fifo_size, int, 0644);
MODULE_PARM_DESC(fifo_size, "kfifo缓冲区大小（字节），默认 4096");

/* 前向声明：fops 在后面定义，但 probe 里就要用到 */
static const struct file_operations leetx_sensor_fops;



static const struct of_device_id leetx_sensor_of_match[] = {
    { .compatible = "leetx,torque-sensor" },
    { .compatible = "leetx,pressure-sensor" },
    { /* sentinel */ }
};

MODULE_DEVICE_TABLE(of, leetx_sensor_of_match);

//设备私有数据这个指针挂在 platform_device 上


struct leetx_sensor_dev {
    dev_t devno;
    struct cdev cdev;
    struct class *class;
    struct device *device;

    DECLARE_KFIFO_PTR(fifo, u32);
    wait_queue_head_t   read_wq;        /* 等待队列 */
    struct mutex        write_lock;     /* 写锁 */
    atomic_t            opened;         /* 单实例标记 */

    unsigned int        sample_rate;    /* 从设备树读出的采样率 */
    unsigned int        instance_id;    /* 第几个实例（调试用） */
};

static int leetx_sensor_probe(struct platform_device *pdev)
{
    int ret;
    struct leetx_sensor_dev *dev;//实例化一个新设备
    struct device_node *np = pdev->dev.of_node;// 拿到设备树节点的地址

    //分配设备私有结构数据
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
        if (!dev) {
        pr_err("leetx_sensor: devm_kzalloc failed\n");
        return -ENOMEM;
    }
    //从设备树读取硬件参数
    ret = of_property_read_u32(np, "sample-rate", &dev->sample_rate);
    if (ret) {
        pr_err("leetx_sensor: failed to read sample-rate from device tree\n");
        return ret;
    }
    ret = of_property_read_u32(np, "instance-id", &dev->instance_id);
    if (ret < 0) {
        dev->instance_id = 0;
    }
    //初始化kfifo
    ret = kfifo_alloc(&dev->fifo, fifo_size, GFP_KERNEL);
    if (ret) {
        pr_err("leetx_sensor: failed to allocate kfifo\n");
        return ret;
    }

    mutex_init(&dev->write_lock);
    init_waitqueue_head(&dev->read_wq);
    atomic_set(&dev->opened, 0);

    //注册字符设备，创建设备节点
    ret = alloc_chrdev_region(&dev->devno, 0, 1, DEVICE_NAME);
    if (ret) {
        pr_err("leetx_sensor: alloc_chrdev_region failed\n");
        goto err_free_fifo;
    }

    cdev_init(&dev->cdev, &leetx_sensor_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devno, 1);
    if (ret < 0) {
        pr_err("leetx_sensor: cdev_add failed\n");
        goto err_unregister_region;
    }

    dev->class = class_create(THIS_MODULE, "leetx_sensor_class");
    if (IS_ERR(dev->class)) {
        pr_err("leetx_sensor: class_create failed\n");
        ret = PTR_ERR(dev->class);
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, &pdev->dev,
                                dev->devno, NULL, "leetx_sensor%u",
                                dev->instance_id);//设备名与设备树中 instance-id 相关联

    if (IS_ERR(dev->device)) {
        pr_err("leetx_sensor: device_create failed\n");
        ret = PTR_ERR(dev->device);
        goto err_class_destroy;
    }

    platform_set_drvdata(pdev, dev);//将设备私有数据挂在 platform_device 上

    pr_info("leetx_sensor: probe instance %u done — /dev/leetx_sensor%u\n",
            dev->instance_id, dev->instance_id);
    return 0;

    /*
     * 错误处理：和 Stage 1 一样用 goto 链式清理
     * 区别：devm_kzalloc 的 dev 不用手动 kfree，内核自动回收
     */
err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_region:
    unregister_chrdev_region(dev->devno, 1);
err_free_fifo:
    kfifo_free(&dev->fifo);
    /*
     * 没有 err_free_dev，因为 dev 是 devm_kzalloc 分配的，
     * probe 返回错误码后内核自动释放
     */
    return ret;
}

static int leetx_sensor_remove(struct platform_device *pdev)
{
    /*
     * platform_get_drvdata：
     *   从 pdev 取回 probe 里通过 platform_set_drvdata 保存的私有数据指针。
     *   这就是我们在 probe 里创建的 struct leetx_sensor_dev。
     */
    struct leetx_sensor_dev *dev = platform_get_drvdata(pdev);

    if (!dev)
        return 0;

    /*
     * 销毁顺序：后创建的先销毁（和 probe 相反）
     */
    device_destroy(dev->class, dev->devno);       /* 删 /dev/leetx_sensorX */
    class_destroy(dev->class);                     /* 删 /sys/class/leetx_sensor_class */
    cdev_del(&dev->cdev);                         /* 注销字符设备 */
    unregister_chrdev_region(dev->devno, 1);      /* 归还设备号 */
    kfifo_free(&dev->fifo);                       /* 释放环形缓冲区 */
    mutex_destroy(&dev->write_lock);              /* 销毁互斥锁 */

    /*
     * 没有 kfree(dev)！
     * dev 是 devm_kzalloc(&pdev->dev, ...) 分配的，
     * remove 返回后内核自动释放所有登记在 pdev->dev 名下的内存。
     */

    pr_info("leetx_sensor: instance %u removed\n", dev->instance_id);
    return 0;
}

static int leetx_sensor_open(struct inode *inode, struct file *filp)
{
    struct leetx_sensor_dev *dev;
    int ret;

    dev = container_of(inode->i_cdev, struct leetx_sensor_dev, cdev);

    filp->private_data = dev;
    ret = atomic_cmpxchg(&dev->opened, 0, 1);
    if (ret != 0)
        return -EBUSY;

    pr_info("leetx_sensor: instance %u opened\n", dev->instance_id);
    return 0;
}

static int leetx_sensor_release(struct inode *inode, struct file *filp)
{
    struct leetx_sensor_dev *dev = filp->private_data;

    atomic_set(&dev->opened, 0);
    pr_info("leetx_sensor: instance %u closed\n", dev->instance_id);
    return 0;
}

static ssize_t leetx_sensor_write(struct file *filp, const char __user *buf,
                           size_t count, loff_t *f_pos)
{
    struct leetx_sensor_dev *dev = filp->private_data;
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

static ssize_t leetx_sensor_read(struct file *filp, char __user *buf,
                          size_t count, loff_t *f_pos)
{
    struct leetx_sensor_dev *dev = filp->private_data;
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

static const struct file_operations leetx_sensor_fops = {
    .owner    = THIS_MODULE,
    .open     = leetx_sensor_open,
    .release  = leetx_sensor_release,
    .write    = leetx_sensor_write,
    .read     = leetx_sensor_read,
};

static struct platform_driver leetx_sensor_driver = {
    .probe  = leetx_sensor_probe,     /* "找到设备了，初始化！" */
    .remove = leetx_sensor_remove,    /* "设备没了，清理！" */
    .driver = {
        .name           = "leetx_sensor",           /* 驱动名，lsmod 显示 */
        .of_match_table = leetx_sensor_of_match,    /* 匹配表 */
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(leetx_sensor_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang");
MODULE_DESCRIPTION("Platform device driver — Leetx Stage 2");