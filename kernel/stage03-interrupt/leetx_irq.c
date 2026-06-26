#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/kfifo.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/timer.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>

#define DEVICE_NAME "leetx_irq"
#define DEFAULT_FIFO_SIZE 4096

static int fifo_size = DEFAULT_FIFO_SIZE;
module_param(fifo_size, int, 0644);
MODULE_PARM_DESC(fifo_size, "kfifo缓冲区大小（字节），默认 4096");

static const struct of_device_id leetx_irq_of_match[] = {
    {.compatible = "leetx,irq-sensor"},
    {}
};

MODULE_DEVICE_TABLE(of, leetx_irq_of_match);

/* 前向声明 */
static const struct file_operations leetx_irq_fops;
static enum hrtimer_restart leetx_hrtimer_callback(struct hrtimer *timer);

/* 模块参数：必须在 probe 之前定义 */
static int use_timer = 1;
module_param(use_timer, int, 0644);
MODULE_PARM_DESC(use_timer, "设置为 1 用 hrtimer 模拟中断，0 等待真实 GPIO 中断");

static int timer_period_us = 250;
module_param(timer_period_us, int, 0644);
MODULE_PARM_DESC(timer_period_us, "定时器周期（微秒），默认 250μs，对齐 Leetx 控制周期");

struct leetx_irq_dev {
    dev_t               devno;
    struct cdev         cdev;
    struct class       *class;
    struct device      *device;

    DECLARE_KFIFO_PTR(fifo, u32);
    wait_queue_head_t   read_wq;
    struct mutex        write_lock;
    atomic_t            opened;

    unsigned int        sample_rate;
    unsigned int        instance_id;

    int                 irq_gpio;
    int                 irq;
    struct hrtimer      sim_timer;
    //和 irq_gpio 用杜邦线短接，hrtimer 回调里翻转这个引脚，产生真实的硬件中断供 irq_gpio 捕获。
    int                 trigger_gpio;
    /* 中断计数器：debug 用，看中断触发了多少次 */
    atomic_t            irq_count;
};

static irqreturn_t leetx_irq_handler(int irq, void *data)
{
    struct leetx_irq_dev *dev = data;

    //中断计数器递增
    atomic_inc(&dev->irq_count);

    return IRQ_WAKE_THREAD;
}

static irqreturn_t leetx_irq_thread(int irq, void *data)
{
    struct leetx_irq_dev *dev = data;
    u32 sample_val;

    /*
     * 模拟传感器数据：用中断计数器的值作为采样值。
     * 真实产品里这里会调 spi_read() 去读传感器的 ADC 寄存器。
     */
    sample_val = atomic_read(&dev->irq_count);

    mutex_lock(&dev->write_lock);
    kfifo_in(&dev->fifo, &sample_val, 1);
    mutex_unlock(&dev->write_lock);


    //唤醒等待队列
    wake_up_interruptible(&dev->read_wq);

    return IRQ_HANDLED;
}


static int leetx_irq_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    struct leetx_irq_dev *dev;
    int ret;

    pr_info("leetx_irq: probe starting...\n");

    /* 第一步：分配私有结构体 */
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        pr_err("leetx_irq: devm_kzalloc failed\n");
        return -ENOMEM;
    }

    /* 第二步：从设备树读取参数 */
    ret = of_property_read_u32(np, "sample-rate", &dev->sample_rate);
    if (ret < 0) {
        dev->sample_rate = 1000;
    }

    ret = of_property_read_u32(np, "instance-id", &dev->instance_id);
    if (ret < 0) {
        dev->instance_id = 0;
    }

    dev->irq_gpio = of_get_named_gpio(np, "irq-gpios", 0);
    if (gpio_is_valid(dev->irq_gpio)) {

        /* 向内核申请这个 GPIO 的使用权 */
        ret = devm_gpio_request(&pdev->dev, dev->irq_gpio, "leetx-irq");
        if (ret) {
            pr_err("leetx_irq: gpio_request failed for GPIO %d\n",
                   dev->irq_gpio);
            return ret;
        }

        /* 把 GPIO 设为输入模式——用来接收传感器信号 */
        ret = gpio_direction_input(dev->irq_gpio);
        if (ret) {
            pr_err("leetx_irq: gpio_direction_input failed\n");
            return ret;
        }
    
    dev->irq = gpio_to_irq(dev->irq_gpio);
        if (dev->irq < 0) {
            pr_err("leetx_irq: gpio_to_irq failed\n");
            return dev->irq;
        }


    pr_info("leetx_irq: GPIO %d → IRQ %d\n", dev->irq_gpio, dev->irq);

    ret = devm_request_threaded_irq(
            &pdev->dev,
            dev->irq,
            leetx_irq_handler,           /* 顶半部 */
            leetx_irq_thread,            /* 底半部 */
            IRQF_TRIGGER_RISING | IRQF_ONESHOT,
            "leetx_irq",
            dev
    );
    if (ret) {
            pr_err("leetx_irq: request_threaded_irq failed, ret=%d\n",
                   ret);
            return ret;
    }
    pr_info("leetx_irq: IRQ %d registered\n", dev->irq);
    }else{
        dev->irq = -1;
        pr_info("leetx_irq: no IRQ GPIO in DT, will use hrtimer\n");
    }

    /* 第四步：初始化 kfifo / 锁 / 等待队列（Stage 2 已有） */
    ret = kfifo_alloc(&dev->fifo, fifo_size, GFP_KERNEL);
    if (ret) {
        pr_err("leetx_irq: kfifo_alloc failed\n");
        return ret;
    }

    mutex_init(&dev->write_lock);
    init_waitqueue_head(&dev->read_wq);
    atomic_set(&dev->opened, 0);
    atomic_set(&dev->irq_count, 0);

    /* 初始化 hrtimer：只初始化 + 绑定，不启动（open 里启动） */
    if (use_timer) {
        hrtimer_init(&dev->sim_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
        dev->sim_timer.function = leetx_hrtimer_callback;
    }

    /* 第五步：注册字符设备（Stage 2 已有） */
    ret = alloc_chrdev_region(&dev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("leetx_irq: alloc_chrdev_region failed\n");
        goto err_free_fifo;
    }

    cdev_init(&dev->cdev, &leetx_irq_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devno, 1);
    if (ret < 0) {
        pr_err("leetx_irq: cdev_add failed\n");
        goto err_unregister_region;
    }

    dev->class = class_create(THIS_MODULE, "leetx_irq_class");
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, &pdev->dev,
                                dev->devno, NULL,
                                "leetx_irq%u", dev->instance_id);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        goto err_class_destroy;
    }

    /* 第六步：挂到 pdev 上 */
    platform_set_drvdata(pdev, dev);

    pr_info("leetx_irq: probe done — instance %u, IRQ %d, "
            "/dev/leetx_irq%u\n",
            dev->instance_id, dev->irq, dev->instance_id);
    return 0;

    /* 错误处理（Stage 2 已有） */
err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_region:
    unregister_chrdev_region(dev->devno, 1);
err_free_fifo:
    kfifo_free(&dev->fifo);
    return ret;
}


/* ================================================================
 * 模块 4a: hrtimer 备用触发方式
 *
 * 当设备树里没有 irq-gpios 属性时，用 hrtimer 模拟硬件中断。
 * hrtimer 回调里直接调 thread_fn，效果等同于硬中断触发.
 *
 * 使用方式（insmod 时传参）：
 *   insmod leetx_irq.ko use_timer=1 timer_period_us=250
 *
 * 核心认知：
 *   hrtimer 回调运行在硬中断上下文 → 和真正的中断一样的限制
 *   不能睡眠、不能调 mutex
 * ================================================================ */

static enum hrtimer_restart leetx_hrtimer_callback(struct hrtimer *timer)
{
    struct leetx_irq_dev *dev;

    /*
     * container_of：和 open 里一样，从成员地址反推结构体首地址。
     * timer 就是 dev->sim_timer 的地址，反推出 dev。
     */
    dev = container_of(timer, struct leetx_irq_dev, sim_timer);

    // 模拟中断触发
    u32 sample_val = atomic_inc_return(&dev->irq_count);
    kfifo_in(&dev->fifo, &sample_val, 1);

    // 每次中断触发时翻转 trigger_gpio 引脚的输出
    if (gpio_is_valid(dev->trigger_gpio)) {
        static int toggle = 0;
        gpio_set_value(dev->trigger_gpio, toggle);
        toggle = !toggle;
    }

    wake_up_interruptible(&dev->read_wq);

    /*
     * hrtimer_forward_now：从"现在"开始，再往后推一个周期。
     * 防止累积延迟（不是从上次到期时间往后推，而是从当前时间往后推）。
     * 返回 HRTIMER_RESTART 表示定时器再次启动。
     */
    hrtimer_forward_now(timer, ktime_set(0, timer_period_us * 1000));
    return HRTIMER_RESTART;
}

static int leetx_irq_remove(struct platform_device *pdev)
{
    struct leetx_irq_dev *dev = platform_get_drvdata(pdev);

    if (!dev)
        return 0;

    /* 先停掉高精度定时器 */
    hrtimer_cancel(&dev->sim_timer);
    pr_info("leetx_irq: hrtimer cancelled\n");

    /* 和 Stage 2 一样的清理 */
    device_destroy(dev->class, dev->devno);
    class_destroy(dev->class);
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devno, 1);
    kfifo_free(&dev->fifo);
    mutex_destroy(&dev->write_lock);

    pr_info("leetx_irq: instance %u removed, total irq count=%d\n",
            dev->instance_id, atomic_read(&dev->irq_count));
    return 0;
}

static int leetx_irq_open(struct inode *inode, struct file *filp)
{
    struct leetx_irq_dev *dev;
    int ret;

    dev = container_of(inode->i_cdev, struct leetx_irq_dev, cdev);
    filp->private_data = dev;

    ret = atomic_cmpxchg(&dev->opened, 0, 1);
    if (ret != 0)
        return -EBUSY;

    /*
     * 启动 hrtimer，开始周期性模拟传感器中断。
     * 如果后面连接了杜邦线，设备树有 irq-gpios 属性，
     * use_timer 设为 0，这里就不会启动 hrtimer。
     */
    if (use_timer) {
        hrtimer_start(&dev->sim_timer,
                      ktime_set(0, timer_period_us * 1000),
                      HRTIMER_MODE_REL);
    }

    pr_info("leetx_irq: instance %u opened\n", dev->instance_id);
    return 0;
}

static int leetx_irq_release(struct inode *inode, struct file *filp)
{
    struct leetx_irq_dev *dev = filp->private_data;

    /* 停掉定时器，停止产生模拟中断数据 */
    hrtimer_cancel(&dev->sim_timer);
    atomic_set(&dev->opened, 0);

    pr_info("leetx_irq: instance %u closed\n", dev->instance_id);
    return 0;
}

static ssize_t leetx_irq_write(struct file *filp, const char __user *buf,
                               size_t count, loff_t *f_pos)
{
    struct leetx_irq_dev *dev = filp->private_data;
    size_t max_count = count / sizeof(u32);
    u32 *kbuf;
    unsigned int written;

    if (max_count == 0)
        return -EINVAL;

    kbuf = kmalloc(max_count * sizeof(u32), GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    if (copy_from_user(kbuf, buf, max_count * sizeof(u32))) {
        kfree(kbuf);
        return -EFAULT;
    }

    mutex_lock(&dev->write_lock);
    written = kfifo_in(&dev->fifo, kbuf, max_count);
    mutex_unlock(&dev->write_lock);

    kfree(kbuf);
    wake_up_interruptible(&dev->read_wq);

    return written * sizeof(u32);
}

static ssize_t leetx_irq_read(struct file *filp, char __user *buf,
                              size_t count, loff_t *f_pos)
{
    struct leetx_irq_dev *dev = filp->private_data;
    unsigned int read_count = count / sizeof(u32);
    unsigned int actual;
    u32 *kbuf;
    int ret;

    if (read_count == 0)
        return -EINVAL;

    if (filp->f_flags & O_NONBLOCK) {
        if (kfifo_is_empty(&dev->fifo))
            return -EAGAIN;
    } else {
        ret = wait_event_interruptible(dev->read_wq,
                                       !kfifo_is_empty(&dev->fifo));
        if (ret)
            return ret;
    }

    kbuf = kmalloc(read_count * sizeof(u32), GFP_KERNEL);
    if (!kbuf)
        return -ENOMEM;

    actual = kfifo_out(&dev->fifo, kbuf, read_count);

    if (copy_to_user(buf, kbuf, actual * sizeof(u32))) {
        kfree(kbuf);
        return -EFAULT;
    }

    kfree(kbuf);
    return actual * sizeof(u32);
}

static const struct file_operations leetx_irq_fops = {
    .owner    = THIS_MODULE,
    .open     = leetx_irq_open,
    .release  = leetx_irq_release,
    .write    = leetx_irq_write,
    .read     = leetx_irq_read,
};

static struct platform_driver leetx_irq_driver = {
    .probe  = leetx_irq_probe,
    .remove = leetx_irq_remove,
    .driver = {
        .name           = "leetx_irq",
        .of_match_table = leetx_irq_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(leetx_irq_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang");
MODULE_DESCRIPTION("Interrupt handling driver — Leetx Stage 3");