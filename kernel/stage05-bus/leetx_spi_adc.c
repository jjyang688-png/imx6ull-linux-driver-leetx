#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/poll.h>

/* ================================================================
 * Stage 5 新增头文件 — SPI 总线框架
 *
 * 和 Stage 1-4 最核心的区别就在这里：
 *   之前所有驱动都是 platform_driver，内核把设备当成"平台设备"
 *   Stage 5 开始用 spi_driver，内核知道这是一个 SPI 总线上的设备
 *
 * #include <linux/spi/spi.h> 提供了什么？
 *   - struct spi_device     — 代替 struct platform_device
 *   - struct spi_driver      — 代替 struct platform_driver
 *   - spi_write / spi_read   — 代替手写的 GPIO 操作
 *   - spi_write_then_read    — 最常用：先发命令再读数据
 *   - module_spi_driver      — 代替 module_platform_driver
 * ================================================================ */
#include <linux/spi/spi.h>

#define DEVICE_NAME     "leetx_spi_adc"
#define DEFAULT_BUF_SIZE (256 * 4096)

static int buf_size = DEFAULT_BUF_SIZE;
module_param(buf_size, int, 0644);
MODULE_PARM_DESC(buf_size, "DMA 缓冲区大小（字节），默认 1MB");

/*
 * 模拟模式：没有真实 SPI ADC 硬件时，用 hrtimer 产生假数据。
 * use_sim=1：hrtimer 模拟传感器数据（不调 SPI）
 * use_sim=0：真实 SPI ADC，通过 spi_write_then_read 读取
 */
static int use_sim = 1;
module_param(use_sim, int, 0644);
MODULE_PARM_DESC(use_sim, "1=模拟传感器（不需硬件），0=真实 SPI ADC");

static int timer_period_us = 250;
module_param(timer_period_us, int, 0644);
MODULE_PARM_DESC(timer_period_us, "定时器周期（微秒），默认 250μs");

static const struct file_operations leetx_spi_fops;
static enum hrtimer_restart leetx_spi_hrtimer_callback(struct hrtimer *timer);
static irqreturn_t leetx_spi_irq_handler(int irq, void *data);
static irqreturn_t leetx_spi_irq_thread(int irq, void *data);

static const struct of_device_id leetx_spi_of_match[] = {
    { .compatible = "leetx,spi-adc" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, leetx_spi_of_match);

struct leetx_spi_dev {
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
    u32                *dma_buf;         /* CPU 看到的虚拟地址 */
    dma_addr_t          dma_handle;      /* DMA 控制器看到的总线地址 */
    size_t              dma_buf_size;    /* 缓冲区字节大小 */
    struct device      *parent_dev;      /* &spi->dev，dma_alloc/mm/mmap 用 */
    atomic_t            write_pos;       /* 当前写位置（u32 下标） */
    atomic_t            total_samples;   /* 累计采样总数 */

    /* ========== SPI 设备指针（Stage 5 新增）========== */
    /*
     * spi：指向内核匹配后传入的 spi_device
     *
     * 为什么需要存下来？
     *   probe 后 probe 函数返回了，spi_device 指针就丢了。
     *   但 hrtimer 回调 / 中断线程里要调 spi_write_then_read(spi, ...)
     *   所以必须存到私有结构体里，跟 Stage 4 存 parent_dev 一样的原因。
     *
     * 和 parent_dev 的关系：
     *   parent_dev = &spi->dev  （spi_device 内嵌的通用设备）
     *   dma_alloc_coherent 需要的是 &spi->dev，不是 spi 本身
     */
    struct spi_device  *spi;

    /* ========== 中断 + hrtimer（Stage 3-4 保留）========== */
    int                 irq_gpio;
    int                 irq;
    struct hrtimer      sim_timer;
    int                 trigger_gpio;
    atomic_t            irq_count;

    /* ========== 互斥锁 ========== */
    struct mutex        write_lock;
};

static int leetx_spi_probe(struct spi_device *spi)
{
    struct device_node *np = spi->dev.of_node;
    struct device *dev = &spi->dev;
    struct leetx_spi_dev *ddev;
    int ret;

    pr_info("leetx_spi: probe starting...\n");
    ddev = devm_kzalloc(dev, sizeof(*ddev), GFP_KERNEL);
    if (!ddev) {
        pr_err("leetx_spi: devm_kzalloc failed\n");
        return -ENOMEM;
    }

    /* 第二步：从设备树读取参数（只读非 SPI 总线的自定义属性） */
    ret = of_property_read_u32(np, "sample-rate", &ddev->sample_rate);
    if (ret < 0)
        ddev->sample_rate = 1000;

    ret = of_property_read_u32(np, "instance-id", &ddev->instance_id);
    if (ret < 0)
        ddev->instance_id = 0;

    /*
     * 第三步：配置 SPI 总线参数（Stage 5 新增！Stage 4 没有）
     *
     * spi_setup 做的事：
     *   把你改过的 mode / bits_per_word / max_speed_hz 写回硬件控制器。
     *   如果设备树里已经有 spi-cpha / spi-cpol，内核已解析到 spi->mode。
     *   这里也可以手动改（比如 spi->mode = SPI_MODE_0）。
     *
     * 调用 spi_setup 后 SPI 控制器就按这个参数工作了。
     */
    spi->bits_per_word = 16;        /* ADC 通常是 16 位采样 */
    ret = spi_setup(spi);
    if (ret < 0) {
        pr_err("leetx_spi: spi_setup failed, ret=%d\n", ret);
        return ret;
    }
    pr_info("leetx_spi: SPI setup — %u Hz, mode=%u, %u bits/word\n",
            spi->max_speed_hz, spi->mode, spi->bits_per_word);

    /*
     * 第四步：分配 DMA 缓冲区（和 Stage 4 完全一样）
     *
     * 区别只在于第一个参数：
     *   Stage 4：&pdev->dev
     *   Stage 5：&spi->dev
     *
     * 两者都是 struct device *，dma_alloc_coherent 不关心外面那层是什么。
     */
    ddev->parent_dev    = dev;
    ddev->dma_buf_size  = buf_size;

    ddev->dma_buf = dma_alloc_coherent(dev,
                                       ddev->dma_buf_size,
                                       &ddev->dma_handle,
                                       GFP_KERNEL);
    if (!ddev->dma_buf) {
        pr_err("leetx_spi: dma_alloc_coherent failed (%zu bytes)\n",
               ddev->dma_buf_size);
        return -ENOMEM;
    }

    pr_info("leetx_spi: DMA buffer — virt=%p, bus=0x%pad, size=%zu\n",
            ddev->dma_buf, &ddev->dma_handle, ddev->dma_buf_size);

    /*
     * 第五步：保存 SPI 设备指针（关键！）
     *
     * 为什么必须存？
     *   probe 结束后 spi 指针没了，但 hrtimer 回调 / 中断线程里
     *   要调 spi_write_then_read(ddev->spi, ...) 读传感器数据。
     *   不存下来就找不到 SPI 设备了。
     */
    ddev->spi = spi;
    /* 第六步：初始化计数器和锁（和 Stage 4 完全一样） */
    atomic_set(&ddev->write_pos, 0);
    atomic_set(&ddev->total_samples, 0);
    atomic_set(&ddev->opened, 0);
    atomic_set(&ddev->irq_count, 0);

    init_waitqueue_head(&ddev->read_wq);
    mutex_init(&ddev->write_lock);

    /*
     * 第七步：GPIO 中断 + hrtimer（和 Stage 4 完全一样）
     *
     * 注意 irq_gpio 不是 SPI 总线本身的东西，是传感器的独立中断引脚。
     * SPI 总线只用 MOSI/MISO/SCLK/CS 四根线，传感器完成转换后通过
     * 额外的 GPIO 引脚通知 SoC"数据准备好了"。
     */
    ddev->irq_gpio = of_get_named_gpio(np, "irq-gpios", 0);
    if (gpio_is_valid(ddev->irq_gpio)) {
        ret = devm_gpio_request(dev, ddev->irq_gpio, "leetx-spi-irq");
        if (ret) {
            pr_err("leetx_spi: gpio_request failed for GPIO %d\n",
                   ddev->irq_gpio);
            goto err_free_dma;
        }

        ret = gpio_direction_input(ddev->irq_gpio);
        if (ret) {
            pr_err("leetx_spi: gpio_direction_input failed\n");
            goto err_free_dma;
        }

        ddev->irq = gpio_to_irq(ddev->irq_gpio);
        if (ddev->irq < 0) {
            ret = ddev->irq;
            goto err_free_dma;
        }

        pr_info("leetx_spi: GPIO %d → IRQ %d\n", ddev->irq_gpio, ddev->irq);

        ret = devm_request_threaded_irq(
                dev, ddev->irq,
                leetx_spi_irq_handler,
                leetx_spi_irq_thread,
                IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                "leetx_spi", ddev);
        if (ret) {
            pr_err("leetx_spi: request_threaded_irq failed, ret=%d\n", ret);
            goto err_free_dma;
        }
        pr_info("leetx_spi: IRQ %d registered\n", ddev->irq);
    } else {
        ddev->irq = -1;
        pr_info("leetx_spi: no IRQ GPIO in DT, will use hrtimer\n");
    }

    /* 第八步：初始化 hrtimer */
    hrtimer_init(&ddev->sim_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    ddev->sim_timer.function = leetx_spi_hrtimer_callback;

    /* 第九步：注册字符设备（和 Stage 2-4 完全一样） */
    ret = alloc_chrdev_region(&ddev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("leetx_spi: alloc_chrdev_region failed\n");
        goto err_free_dma;
    }

    cdev_init(&ddev->cdev, &leetx_spi_fops);
    ddev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&ddev->cdev, ddev->devno, 1);
    if (ret < 0) {
        pr_err("leetx_spi: cdev_add failed\n");
        goto err_unregister_region;
    }

    ddev->class = class_create(THIS_MODULE, "leetx_spi_class");
    if (IS_ERR(ddev->class)) {
        ret = PTR_ERR(ddev->class);
        goto err_cdev_del;
    }

    ddev->device = device_create(ddev->class, dev,
                                 ddev->devno, NULL,
                                 "leetx_spi%u", ddev->instance_id);
    if (IS_ERR(ddev->device)) {
        ret = PTR_ERR(ddev->device);
        goto err_class_destroy;
    }

    /*
     * 第十步：挂到 spi_device 上（不是 platform_set_drvdata！）
     *
     * spi_set_drvdata / spi_get_drvdata 本质和 platform 版本一样：
     *   内部都是 dev_set_drvdata(&spi->dev, data)
     *
     * 但用 spi_ 前缀的版本语义更清晰：一看就知道是 SPI 设备。
     */
    spi_set_drvdata(spi, ddev);

    pr_info("leetx_spi: probe done — instance %u, /dev/leetx_spi%u\n",
            ddev->instance_id, ddev->instance_id);
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

static u32 leetx_spi_read_sample(struct leetx_spi_dev *ddev)
{
    u32 val;

    if (use_sim) {
        /* 模拟：和 Stage 4 一样，用计数器值当采样值 */
        val = (u32)atomic_inc_return(&ddev->irq_count);
    } else {
        /*
         * 真实 SPI ADC 读取（以 MCP3008 为例）：
         *
         * u8 tx[3] = {0x01, 0x80, 0x00};  // 启动位 + 单端 CH0
         * u8 rx[3] = {0};
         * spi_write_then_read(ddev->spi, tx, 3, rx, 3);
         * val = ((rx[1] & 0x03) << 8) | rx[2];  // 10 位结果
         *
         * 不同 ADC 芯片的 SPI 命令不同，这里留接口，
         * 后续根据具体芯片填对应的 tx 命令和 rx 解析。
         */
        val = 0;  /* TODO: 替换为具体 SPI ADC 的读取逻辑 */
    }

    return val;
}

/* --- 写 DMA 缓冲区（三个函数共享这段逻辑）--- */
static void leetx_spi_write_dma(struct leetx_spi_dev *ddev, u32 val)
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
static irqreturn_t leetx_spi_irq_handler(int irq, void *data)
{
    struct leetx_spi_dev *ddev = data;
    atomic_inc(&ddev->irq_count);
    return IRQ_WAKE_THREAD;
}

/* --- 底半部（进程上下文，可以调 SPI 函数）---
 *
 * 关键：SPI 传输可能睡眠（等总线、等 DMA），必须在线程上下文调用。
 * 所以 spi_write_then_read 放在 thread_fn 里，不放 hard_handler。
 */
static irqreturn_t leetx_spi_irq_thread(int irq, void *data)
{
    struct leetx_spi_dev *ddev = data;
    u32 val;

    /*
     * 读传感器（模拟 or 真实 SPI）：
     *
     * 模拟模式：val = ++irq_count
     * 真实模式：val = SPI 读 ADC 寄存器
     *
     * 同一个接口，use_sim 决定走哪条路。
     */
    val = leetx_spi_read_sample(ddev);

    leetx_spi_write_dma(ddev, val);

    return IRQ_HANDLED;
}

static enum hrtimer_restart leetx_spi_hrtimer_callback(struct hrtimer *timer)
{
    struct leetx_spi_dev *ddev;
    u32 val;

    ddev = container_of(timer, struct leetx_spi_dev, sim_timer);

    val = leetx_spi_read_sample(ddev);

    leetx_spi_write_dma(ddev, val);

    if (gpio_is_valid(ddev->trigger_gpio)) {
        static int toggle;
        gpio_set_value(ddev->trigger_gpio, toggle);
        toggle = !toggle;
    }

    hrtimer_forward_now(timer, ktime_set(0, timer_period_us * 1000));
    return HRTIMER_RESTART;
}

static int leetx_spi_remove(struct spi_device *spi)
{
    struct leetx_spi_dev *ddev = spi_get_drvdata(spi);

    if (!ddev)
        return 0;

    hrtimer_cancel(&ddev->sim_timer);

    device_destroy(ddev->class, ddev->devno);
    class_destroy(ddev->class);
    cdev_del(&ddev->cdev);
    unregister_chrdev_region(ddev->devno, 1);

    dma_free_coherent(&spi->dev, ddev->dma_buf_size,
                      ddev->dma_buf, ddev->dma_handle);

    mutex_destroy(&ddev->write_lock);

    pr_info("leetx_spi: instance %u removed, total samples=%d\n",
            ddev->instance_id, atomic_read(&ddev->total_samples));
    return 0;
}

/* ================================================================
 * 模块 6: open / release
 *
 * 和 Stage 4 的区别：
 *   use_sim=0 时 hrtimer 不启动！因为 SPI 传输必须在进程上下文，
 *   而 hrtimer 回调是硬中断上下文，不能调 spi_write_then_read。
 *
 *   use_sim=1 时和 Stage 4 完全一样。
 * ================================================================ */

static int leetx_spi_open(struct inode *inode, struct file *filp)
{
    struct leetx_spi_dev *ddev;

    ddev = container_of(inode->i_cdev, struct leetx_spi_dev, cdev);

    if (atomic_cmpxchg(&ddev->opened, 0, 1) != 0)
        return -EBUSY;

    filp->private_data = ddev;

    /*
     * 只有模拟模式才启动 hrtimer。
     * 真实 SPI 模式：等 GPIO 中断触发，不走定时器。
     */
    if (use_sim) {
        hrtimer_start(&ddev->sim_timer,
                      ktime_set(0, timer_period_us * 1000),
                      HRTIMER_MODE_REL);
    }

    pr_info("leetx_spi: instance %u opened (use_sim=%d)\n",
            ddev->instance_id, use_sim);
    return 0;
}

static int leetx_spi_release(struct inode *inode, struct file *filp)
{
    struct leetx_spi_dev *ddev = filp->private_data;

    hrtimer_cancel(&ddev->sim_timer);
    atomic_set(&ddev->opened, 0);

    pr_info("leetx_spi: instance %u closed, total samples=%d\n",
            ddev->instance_id, atomic_read(&ddev->total_samples));
    return 0;
}

static int leetx_spi_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct leetx_spi_dev *ddev = filp->private_data;

    return dma_mmap_coherent(ddev->parent_dev, vma,
                             ddev->dma_buf, ddev->dma_handle,
                             ddev->dma_buf_size);
    /*
     * 一行代码！和 Stage 4 手工版 remap_pfn_range 做的事情完全一样，
     * 但内核帮你封装好了：算 PFN、设 noncached、边界检查、建页表。
     *
     * 注意第一个参数是 parent_dev（=&spi->dev，probe 里存的）。
     */
}

static unsigned int leetx_spi_poll(struct file *filp,
                                   struct poll_table_struct *pts)
{
    struct leetx_spi_dev *ddev = filp->private_data;
    unsigned int mask = 0;

    poll_wait(filp, &ddev->read_wq, pts);

    if (atomic_read(&ddev->total_samples) > 0)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static const struct file_operations leetx_spi_fops = {
    .owner    = THIS_MODULE,
    .open     = leetx_spi_open,
    .release  = leetx_spi_release,
    .mmap     = leetx_spi_mmap,
    .poll     = leetx_spi_poll,
};

static struct spi_driver leetx_spi_driver = {
    .probe  = leetx_spi_probe,
    .remove = leetx_spi_remove,
    .driver = {
        .name           = "leetx_spi",
        .of_match_table = leetx_spi_of_match,
        .owner          = THIS_MODULE,
    },
};

module_spi_driver(leetx_spi_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang");
MODULE_DESCRIPTION("SPI ADC sensor driver with DMA + mmap — Leetx Stage 5");
