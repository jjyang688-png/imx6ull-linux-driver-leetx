#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/poll.h>

#include <linux/i2c.h>

#define DEVICE_NAME     "leetx_i2c_eeprom"
#define DEFAULT_BUF_SIZE (256 * 4096)

static int buf_size = DEFAULT_BUF_SIZE;
module_param(buf_size, int, 0644);
MODULE_PARM_DESC(buf_size, "DMA 缓冲区大小（字节），默认 1MB");

static int use_sim = 1;
module_param(use_sim, int, 0644);
MODULE_PARM_DESC(use_sim, "1=模拟（不需硬件），0=真实 I2C EEPROM");

static int timer_period_us = 250;
module_param(timer_period_us, int, 0644);
MODULE_PARM_DESC(timer_period_us, "定时器周期（微秒），默认 250μs");

static const struct file_operations leetx_i2c_fops;
static enum hrtimer_restart leetx_i2c_hrtimer_callback(struct hrtimer *timer);
static irqreturn_t leetx_i2c_irq_handler(int irq, void *data);
static irqreturn_t leetx_i2c_irq_thread(int irq, void *data);

static const struct of_device_id leetx_i2c_of_match[] = {
    { .compatible = "leetx,i2c-eeprom" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, leetx_i2c_of_match);



struct leetx_i2c_dev {
    /* ========== 字符设备相关（Stage 1-4 相同的部分）========== */
    dev_t               devno;
    struct cdev         cdev;
    struct class       *class;
    struct device      *device;
    wait_queue_head_t   read_wq;
    atomic_t            opened;
     /* ========== 硬件参数 ========== */
    unsigned int        sample_rate;
    unsigned int        instance_id;
    /* ========== DMA 缓冲区（Stage 4 完整保留）========== */
    u32                *dma_buf;    //虚拟地址
    dma_addr_t          dma_handle; //物理地址
    size_t              dma_buf_size;//字节数
    struct device      *parent_dev;      /* &client->dev */
    atomic_t            write_pos;
    atomic_t            total_samples;

    struct i2c_client  *client;
     /* ========== 中断 + hrtimer（Stage 3-4 保留）========== */
    int                 irq_gpio;
    int                 irq;
    struct hrtimer      sim_timer;
    int                 trigger_gpio;
    atomic_t            irq_count;

    /* ========== 互斥锁 ========== */
    struct mutex        write_lock;
};

static int leetx_i2c_probe(struct i2c_client *client,
                           const struct i2c_device_id *id)
{
    struct device_node *np = client->dev.of_node;
    struct device *dev = &client->dev;
    struct leetx_i2c_dev *ddev;
    int ret;

    pr_info("leetx_i2c: probe starting, addr=0x%02x\n", client->addr);
    /* 第一步：分配私有结构体 */
    ddev = devm_kzalloc(dev, sizeof(*ddev), GFP_KERNEL);
    if (!ddev) {
        pr_err("leetx_i2c: devm_kzalloc failed\n");
        return -ENOMEM;
    }
    /* 第二步：从设备树读取参数 */
    ret = of_property_read_u32(np, "sample-rate", &ddev->sample_rate);
    if (ret < 0)
        ddev->sample_rate = 1000;

    ret = of_property_read_u32(np, "instance-id", &ddev->instance_id);
    if (ret < 0)
        ddev->instance_id = 0;

        ddev->parent_dev    = dev;
    ddev->dma_buf_size  = buf_size;

    ddev->dma_buf = dma_alloc_coherent(dev,
                                       ddev->dma_buf_size,
                                       &ddev->dma_handle,
                                       GFP_KERNEL);
    if (!ddev->dma_buf) {
        pr_err("leetx_i2c: dma_alloc_coherent failed (%zu bytes)\n",
               ddev->dma_buf_size);
        return -ENOMEM;
    }
    pr_info("leetx_i2c: DMA buffer — virt=%p, bus=0x%pad, size=%zu\n",
            ddev->dma_buf, &ddev->dma_handle, ddev->dma_buf_size);
    /* 第五步：保存 I2C 设备指针 */
    ddev->client = client;

    /* 第六步：初始化计数器和锁 */
    atomic_set(&ddev->write_pos, 0);
    atomic_set(&ddev->total_samples, 0);
    atomic_set(&ddev->opened, 0);
    atomic_set(&ddev->irq_count, 0);

    init_waitqueue_head(&ddev->read_wq);
    mutex_init(&ddev->write_lock);

    /* 第七步：GPIO 中断（和 Stage 3-5 一样）*/
    ddev->irq_gpio = of_get_named_gpio(np, "irq-gpios", 0);
    if (gpio_is_valid(ddev->irq_gpio)) {
        ret = devm_gpio_request(dev, ddev->irq_gpio, "leetx-i2c-irq");
        if (ret) {
            pr_err("leetx_i2c: gpio_request failed for GPIO %d\n",
                   ddev->irq_gpio);
            goto err_free_dma;
        }

        ret = gpio_direction_input(ddev->irq_gpio);
        if (ret) {
            pr_err("leetx_i2c: gpio_direction_input failed\n");
            goto err_free_dma;
        }

        ddev->irq = gpio_to_irq(ddev->irq_gpio);
        if (ddev->irq < 0) {
            ret = ddev->irq;
            goto err_free_dma;
        }

        ret = devm_request_threaded_irq(
                dev, ddev->irq,
                leetx_i2c_irq_handler,
                leetx_i2c_irq_thread,
                IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                "leetx_i2c", ddev);
        if (ret) {
            pr_err("leetx_i2c: request_threaded_irq failed, ret=%d\n", ret);
            goto err_free_dma;
        }
        pr_info("leetx_i2c: IRQ %d registered\n", ddev->irq);
    } else {
        ddev->irq = -1;
        pr_info("leetx_i2c: no IRQ GPIO in DT, will use hrtimer\n");
    }

    /* 第八步：初始化 hrtimer */
    hrtimer_init(&ddev->sim_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ddev->sim_timer.function = leetx_i2c_hrtimer_callback;

    /* 第九步：注册字符设备 */
    ret = alloc_chrdev_region(&ddev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("leetx_i2c: alloc_chrdev_region failed\n");
        goto err_free_dma;
    }

    cdev_init(&ddev->cdev, &leetx_i2c_fops);
    ddev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&ddev->cdev, ddev->devno, 1);
    if (ret < 0) {
        pr_err("leetx_i2c: cdev_add failed\n");
        goto err_unregister_region;
    }

    ddev->class = class_create(THIS_MODULE, "leetx_i2c_class");
    if (IS_ERR(ddev->class)) {
        ret = PTR_ERR(ddev->class);
        goto err_cdev_del;
    }

    ddev->device = device_create(ddev->class, dev,
                                 ddev->devno, NULL,
                                 "leetx_i2c%u", ddev->instance_id);
    if (IS_ERR(ddev->device)) {
        ret = PTR_ERR(ddev->device);
        goto err_class_destroy;
    }
    /* 第十步：挂到 i2c_client 上 */
    i2c_set_clientdata(client, ddev);

    pr_info("leetx_i2c: probe done — addr=0x%02x, /dev/leetx_i2c%u\n",
            client->addr, ddev->instance_id);
    return 0;

err_class_destroy:
    class_destroy(ddev->class);
err_cdev_del:
    cdev_del(&ddev->cdev);
err_unregister_region:
    unregister_chrdev_region(ddev->devno, 1);
err_free_dma:
    dma_free_coherent(dev, ddev->dma_buf_size,
                      ddev->dma_buf, ddev->dma_handle);
    return ret;
}

/* --- 读取一次 I2C 设备数据 ---
 *
 * 模拟 AT24C02 EEPROM：256 字节存储空间，地址 0x00~0xFF。
 * 每次读一个字节（循环读取，读完 0xFF 回到 0x00）。
 */
static u32 leetx_i2c_read_sample(struct leetx_i2c_dev *ddev)
{
    u32 val;

    if (use_sim) {
        val = (u32)atomic_inc_return(&ddev->irq_count) & 0xFF;
    } else {
        /*
         * 真实 AT24C02 EEPROM 读取：
         *
         * 先写 1 字节（EEPROM 内部地址），再读 1 字节（该地址的数据）。
         * 2 条 I2C 消息组成一个 i2c_transfer，中间自动插入 REPEATED START。
         */
        int ret;
        u8 reg = 0x00;
        u8 data = 0;
        struct i2c_msg msgs[2] = {
            {
                .addr  = ddev->client->addr,
                .flags = 0,
                .buf   = &reg,
                .len   = 1,
            },
            {
                .addr  = ddev->client->addr,
                .flags = I2C_M_RD,
                .buf   = &data,
                .len   = 1,
            },
        };

        ret = i2c_transfer(ddev->client->adapter, msgs, 2);
        if (ret < 0) {
            pr_err("leetx_i2c: i2c_transfer failed, ret=%d\n", ret);
            return 0;
        }
        val = data;
    }

    return val;
}

/* --- 写 DMA 缓冲区（和 Stage 4-5 100% 一样）--- */
static void leetx_i2c_write_dma(struct leetx_i2c_dev *ddev, u32 val)
{
    int pos;

    pos = atomic_read(&ddev->write_pos);
    ddev->dma_buf[pos] = val;

    pos = (pos + 1) % (ddev->dma_buf_size / sizeof(u32));
    atomic_set(&ddev->write_pos, pos);

    atomic_inc(&ddev->total_samples);

    wake_up_interruptible(&ddev->read_wq);
}

/* --- 硬中断（顶半部）--- */
static irqreturn_t leetx_i2c_irq_handler(int irq, void *data)
{
    struct leetx_i2c_dev *ddev = data;
    atomic_inc(&ddev->irq_count);
    return IRQ_WAKE_THREAD;
}

/* --- 底半部（进程上下文，可以调 I2C 函数）--- */
static irqreturn_t leetx_i2c_irq_thread(int irq, void *data)
{
    struct leetx_i2c_dev *ddev = data;
    u32 val;

    val = leetx_i2c_read_sample(ddev);
    leetx_i2c_write_dma(ddev, val);

    return IRQ_HANDLED;
}

/* --- hrtimer 回调 --- */
static enum hrtimer_restart leetx_i2c_hrtimer_callback(struct hrtimer *timer)
{
    struct leetx_i2c_dev *ddev;
    u32 val;

    ddev = container_of(timer, struct leetx_i2c_dev, sim_timer);

    val = leetx_i2c_read_sample(ddev);
    leetx_i2c_write_dma(ddev, val);

    if (gpio_is_valid(ddev->trigger_gpio)) {
        static int toggle;
        gpio_set_value(ddev->trigger_gpio, toggle);
        toggle = !toggle;
    }

    hrtimer_forward_now(timer, ktime_set(0, timer_period_us * 1000));
    return HRTIMER_RESTART;
}

static int leetx_i2c_remove(struct i2c_client *client)
{
    struct leetx_i2c_dev *ddev = i2c_get_clientdata(client);

    if (!ddev)
        return 0;

    hrtimer_cancel(&ddev->sim_timer);

    device_destroy(ddev->class, ddev->devno);
    class_destroy(ddev->class);
    cdev_del(&ddev->cdev);
    unregister_chrdev_region(ddev->devno, 1);

    dma_free_coherent(&client->dev, ddev->dma_buf_size,
                      ddev->dma_buf, ddev->dma_handle);

    mutex_destroy(&ddev->write_lock);

    pr_info("leetx_i2c: addr=0x%02x removed, total samples=%d\n",
            client->addr, atomic_read(&ddev->total_samples));
    return 0;
}

static int leetx_i2c_open(struct inode *inode, struct file *filp)
{
    struct leetx_i2c_dev *ddev;

    ddev = container_of(inode->i_cdev, struct leetx_i2c_dev, cdev);

    if (atomic_cmpxchg(&ddev->opened, 0, 1) != 0)
        return -EBUSY;

    filp->private_data = ddev;

    if (use_sim) {
        hrtimer_start(&ddev->sim_timer,
                      ktime_set(0, timer_period_us * 1000),
                      HRTIMER_MODE_REL);
    }

    pr_info("leetx_i2c: addr=0x%02x opened\n", ddev->client->addr);
    return 0;
}

static int leetx_i2c_release(struct inode *inode, struct file *filp)
{
    struct leetx_i2c_dev *ddev = filp->private_data;

    hrtimer_cancel(&ddev->sim_timer);
    atomic_set(&ddev->opened, 0);

    pr_info("leetx_i2c: addr=0x%02x closed, total samples=%d\n",
            ddev->client->addr, atomic_read(&ddev->total_samples));
    return 0;
}

static int leetx_i2c_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct leetx_i2c_dev *ddev = filp->private_data;

    return dma_mmap_coherent(ddev->parent_dev, vma,
                             ddev->dma_buf, ddev->dma_handle,
                             ddev->dma_buf_size);
}

static unsigned int leetx_i2c_poll(struct file *filp,
                                    struct poll_table_struct *pts)
{
    struct leetx_i2c_dev *ddev = filp->private_data;
    unsigned int mask = 0;

    poll_wait(filp, &ddev->read_wq, pts);

    if (atomic_read(&ddev->total_samples) > 0)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static const struct file_operations leetx_i2c_fops = {
    .owner    = THIS_MODULE,
    .open     = leetx_i2c_open,
    .release  = leetx_i2c_release,
    .mmap     = leetx_i2c_mmap,
    .poll     = leetx_i2c_poll,
};

static struct i2c_driver leetx_i2c_driver = {
    .probe  = leetx_i2c_probe,
    .remove = leetx_i2c_remove,
    .driver = {
        .name           = "leetx_i2c",
        .of_match_table = leetx_i2c_of_match,
        .owner          = THIS_MODULE,
    },
};

module_i2c_driver(leetx_i2c_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang");
MODULE_DESCRIPTION("I2C EEPROM driver with DMA + mmap — Leetx Stage 6");