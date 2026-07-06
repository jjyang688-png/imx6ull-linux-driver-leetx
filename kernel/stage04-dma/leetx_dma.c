#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/gpio.h>
#include <linux/of_gpio.h>
#include <linux/wait.h>
#include <linux/mutex.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>
#include <linux/interrupt.h>
#include <linux/hrtimer.h>

#include <linux/dma-mapping.h>
#include <linux/mm.h>
#include <linux/poll.h>

#define DEVICE_NAME     "leetx_dma"
#define DEFAULT_BUF_SIZE (256 * 4096)

static int buf_size = DEFAULT_BUF_SIZE;
module_param(buf_size, int, 0644);
MODULE_PARM_DESC(buf_size, "DMA 缓冲区大小（字节），默认 1MB");

static int use_timer = 1;
module_param(use_timer, int, 0644);
MODULE_PARM_DESC(use_timer, "设置为 1 用 hrtimer 模拟中断，0 等待真实 GPIO 中断");

static int timer_period_us = 250;
module_param(timer_period_us, int, 0644);
MODULE_PARM_DESC(timer_period_us, "定时器周期（微秒），默认 250μs，对齐 Leetx 控制周期");

/* ================================================================
 * 模块 2: 匹配表 + 私有结构体
 *
 * 和 Stage 3 的结构体最关键的区别：
 *   Stage 3：用 kfifo 存储数据，read() 通过 copy_to_user 拷贝
 *   Stage 4：用 DMA 缓冲区存储数据，用户态通过 mmap 直接读
 *
 * 所以 Stage 4 不需要 kfifo 了！但需要保留 wait_queue，
 * 让用户态知道"有新数据了"（通过 poll 通知，不通过拷贝）。
 * ================================================================ */

static const struct of_device_id leetx_dma_of_match[] = {
    { .compatible = "leetx,dma-sensor" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, leetx_dma_of_match);

/* 前向声明 */
static const struct file_operations leetx_dma_fops;
static enum hrtimer_restart leetx_dma_hrtimer_callback(struct hrtimer *timer);

struct leetx_dma_dev {
    /* 字符设备相关（Stage 1-3 相同的部分） */
    dev_t               devno;
    struct cdev         cdev;
    struct class       *class;
    struct device      *device;
    wait_queue_head_t   read_wq;
    atomic_t            opened;

    /* 硬件参数 */
    unsigned int        sample_rate;
    unsigned int        instance_id;
    //DMA缓冲区基地址
    u32                *dma_buf;
    //DMA 缓冲区的总线地址
    dma_addr_t          dma_handle;
    //DMA 缓冲区的字节大小
    size_t              dma_buf_size;
    struct device      *parent_dev;
     /* 和 kfifo 的区别：
     *   kfifo：有 in/out 指针，内核管理读取位置
     *   DMA buf：只有写指针，用户态自己管理读到哪了
     */
    atomic_t            write_pos;
    /*
     * total_samples：累计采样总数（用于通知用户态有数据可读）
    */
    atomic_t            total_samples;

    int                 irq_gpio;
    int                 irq;
    struct hrtimer      sim_timer;
    int                 trigger_gpio;
    atomic_t            irq_count;
    struct mutex        write_lock;
};

static irqreturn_t leetx_dma_irq_handler(int irq, void *data)
{
    struct leetx_dma_dev *dev = data;
    atomic_inc(&dev->irq_count);
    return IRQ_WAKE_THREAD;
}

static irqreturn_t leetx_dma_irq_thread(int irq, void *data)
{
    struct leetx_dma_dev *dev = data;
    u32 sample_val;
    int pos;

    sample_val = (u32)atomic_read(&dev->irq_count);
    pos = atomic_read(&dev->write_pos);
    dev->dma_buf[pos] = sample_val;

    pos = (pos + 1) % (dev->dma_buf_size / sizeof(u32));
    atomic_set(&dev->write_pos, pos);

    atomic_inc(&dev->total_samples);

    wake_up_interruptible(&dev->read_wq);

    return IRQ_HANDLED;
}

static irqreturn_t leetx_dma_irq_handler(int irq, void *data);

static int leetx_dma_probe(struct platform_device *pdev)
{
    struct device_node *np = pdev->dev.of_node;
    struct leetx_dma_dev *dev;
    int ret;

    pr_info("leetx_dma: probe starting...\n");

     /* 第一步：分配私有结构体 */
    dev = devm_kzalloc(&pdev->dev, sizeof(*dev), GFP_KERNEL);
    if (!dev) {
        pr_err("leetx_dma: devm_kzalloc failed\n");
        return -ENOMEM;
    }

    /* 第二步：从设备树读取参数 */
    ret = of_property_read_u32(np, "sample-rate", &dev->sample_rate);
    if (ret < 0)
        dev->sample_rate = 1000;  /* 默认 1000Hz */

    ret = of_property_read_u32(np, "instance-id", &dev->instance_id);
    if (ret < 0)
        dev->instance_id = 0;

    /*
     * 第三步：分配 DMA 缓冲区（替换 Stage 3 的 kfifo_alloc）
     *
     * 注意：4.1.15 内核不支持 of_reserved_mem_device_init_by_idx。
     * dma_alloc_coherent 直接从默认 CMA 池分配，设备树里的
     * memory-region 属性在 4.5+ 才自动绑定。
     */
    dev->parent_dev    = &pdev->dev;
    dev->dma_buf_size  = buf_size;

    dev->dma_buf = dma_alloc_coherent(&pdev->dev,
                                      dev->dma_buf_size,
                                      &dev->dma_handle,
                                      GFP_KERNEL);
    if (!dev->dma_buf) {
        pr_err("leetx_dma: dma_alloc_coherent failed (%zu bytes)\n",
               dev->dma_buf_size);
        return -ENOMEM;
    }

    pr_info("leetx_dma: DMA buffer allocated — "
            "virt=%p, bus=0x%pad, size=%zu bytes\n",
            dev->dma_buf, &dev->dma_handle, dev->dma_buf_size);
    /*
     * 注释：%pad 是内核专用的 dma_addr_t 打印格式，
     * 传参时传的是 &dev->dma_handle（指针），不是值。
     */

     /* 第五步：初始化写指针和计数器 */
    atomic_set(&dev->write_pos, 0);       /* 下次写到 dma_buf[0] */
    atomic_set(&dev->total_samples, 0);   /* 还没产生任何采样 */
    atomic_set(&dev->opened, 0);          /* 设备尚未被打开 */
    atomic_set(&dev->irq_count, 0);       /* 中断计数从 0 开始 */

    /* 第六步：初始化等待队列 / 锁 */
    init_waitqueue_head(&dev->read_wq);
    mutex_init(&dev->write_lock);

    /* 第七步：设置 GPIO 中断（和 Stage 3 一样） */
    dev->irq_gpio = of_get_named_gpio(np, "irq-gpios", 0);
    if (gpio_is_valid(dev->irq_gpio)) {
        ret = devm_gpio_request(&pdev->dev, dev->irq_gpio, "leetx-dma-irq");
        if (ret) {
            pr_err("leetx_dma: gpio_request failed for GPIO %d\n",
                   dev->irq_gpio);
            goto err_free_dma;
        }

        ret = gpio_direction_input(dev->irq_gpio);
        if (ret) {
            pr_err("leetx_dma: gpio_direction_input failed\n");
            goto err_free_dma;
        }

        dev->irq = gpio_to_irq(dev->irq_gpio);
        if (dev->irq < 0) {
            pr_err("leetx_dma: gpio_to_irq failed\n");
            ret = dev->irq;
            goto err_free_dma;
        }

        pr_info("leetx_dma: GPIO %d → IRQ %d\n",
                dev->irq_gpio, dev->irq);

        ret = devm_request_threaded_irq(
                &pdev->dev,
                dev->irq,
                leetx_dma_irq_handler,
                leetx_dma_irq_thread,
                IRQF_TRIGGER_RISING | IRQF_ONESHOT,
                "leetx_dma",
                dev);
        if (ret) {
            pr_err("leetx_dma: request_threaded_irq failed, ret=%d\n", ret);
            goto err_free_dma;
        }
        pr_info("leetx_dma: IRQ %d registered\n", dev->irq);
    } else {
        dev->irq = -1;
        pr_info("leetx_dma: no IRQ GPIO in DT, will use hrtimer\n");
    }

     /*
     * 第八步：初始化 hrtimer（只绑回调，不启动）
     *
     * 和 Stage 3 一样：probe 里 hrtimer_init + 绑定回调，
     * open 里 hrtimer_start 启动，release 里 hrtimer_cancel 停止。
     *
     * CLOCK_MONOTONIC：单调时钟，不受系统时间调整影响
     * HRTIMER_MODE_REL：相对时间模式（"从现在开始 N 微秒后触发"）
     */
    hrtimer_init(&dev->sim_timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
    dev->sim_timer.function = leetx_dma_hrtimer_callback;

    /* 第九步：注册字符设备（和 Stage 2-3 一样的流程） */
    ret = alloc_chrdev_region(&dev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("leetx_dma: alloc_chrdev_region failed\n");
        goto err_free_dma;
    }

    cdev_init(&dev->cdev, &leetx_dma_fops);
    dev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&dev->cdev, dev->devno, 1);
    if (ret < 0) {
        pr_err("leetx_dma: cdev_add failed\n");
        goto err_unregister_region;
    }

    dev->class = class_create(THIS_MODULE, "leetx_dma_class");
    if (IS_ERR(dev->class)) {
        ret = PTR_ERR(dev->class);
        pr_err("leetx_dma: class_create failed\n");
        goto err_cdev_del;
    }

    dev->device = device_create(dev->class, &pdev->dev,
                                dev->devno, NULL,
                                "leetx_dma%u", dev->instance_id);
    if (IS_ERR(dev->device)) {
        ret = PTR_ERR(dev->device);
        pr_err("leetx_dma: device_create failed\n");
        goto err_class_destroy;
    }

    /* 第十步：挂到 pdev 上（其他函数通过 platform_get_drvdata 取回） */
    platform_set_drvdata(pdev, dev);

    pr_info("leetx_dma: probe done — instance %u, /dev/leetx_dma%u\n",
            dev->instance_id, dev->instance_id);
    return 0;

    /*
     * 错误回滚：严格按创建顺序的逆序清理
     *
     * 创建顺序：        回滚顺序（goto 标签）：
     * ─────────        ─────────────────────
     * 1. kzalloc       (devm 自动回收，不用手动 kfree)
     * 2. of_reserved   err_release_mem_region ← 🆕 Stage 4 独有
     * 3. dma_alloc     err_free_dma           ← 🆕 Stage 4 独有
     * 4. gpio_request  (devm 自动回收)
     * 5. request_irq   (devm 自动回收)
     * 6. alloc_chrdev  err_unregister_region
     * 7. cdev_add      err_cdev_del
     * 8. class_create  err_class_destroy
     * 9. device_create (class_destroy 前 device_destroy，但 class_destroy
     *                   会自动清理，所以不单独列标签)
     *
     * devm 前缀的函数在 probe 返回负值后自动清理，不需要 goto。
     * 所以 goto 标签只需要管非 devm 的资源。
     */
err_class_destroy:
    class_destroy(dev->class);
err_cdev_del:
    cdev_del(&dev->cdev);
err_unregister_region:
    unregister_chrdev_region(dev->devno, 1);
err_free_dma:
    dma_free_coherent(&pdev->dev, dev->dma_buf_size,
                      dev->dma_buf, dev->dma_handle);
    return ret;
}


static enum hrtimer_restart leetx_dma_hrtimer_callback(struct hrtimer *timer)
{
    struct leetx_dma_dev *dev;
    u32 sample_val;
    int pos;

    dev = container_of(timer, struct leetx_dma_dev, sim_timer);

    sample_val = (u32)atomic_inc_return(&dev->irq_count);

    pos = atomic_read(&dev->write_pos);
    dev->dma_buf[pos] = sample_val;

    pos = (pos + 1) % (dev->dma_buf_size / sizeof(u32));
    atomic_set(&dev->write_pos, pos);

    atomic_inc(&dev->total_samples);

    if (gpio_is_valid(dev->trigger_gpio)) {
        static int toggle;
        gpio_set_value(dev->trigger_gpio, toggle);
        toggle = !toggle;
    }

    wake_up_interruptible(&dev->read_wq);

    hrtimer_forward_now(timer, ktime_set(0, timer_period_us * 1000));
    return HRTIMER_RESTART;
}

static int leetx_dma_remove(struct platform_device *pdev)
{
    struct leetx_dma_dev *dev = platform_get_drvdata(pdev);

    if (!dev)
        return 0;

    /*
     * 第一步：停掉 hrtimer
     *
     * 即使 release 里已经 cancel 过，这里再 cancel 一次确保安全。
     * hrtimer_cancel 对已停止的定时器是空操作（不会报错）。
     *
     * 必须在 dma_free_coherent 之前停！
     * 否则定时器回调可能正在写 dma_buf，释放后写就是 use-after-free。
     */
    hrtimer_cancel(&dev->sim_timer);
    pr_info("leetx_dma: hrtimer cancelled\n");

    /*
     * 第二步：销毁设备节点和类（逆序：先 device_destroy 再 class_destroy）
     *
     * device_destroy：删除 /dev/leetx_dma0（用户态 open 会失败）
     * class_destroy： 删除 /sys/class/leetx_dma_class
     */
    device_destroy(dev->class, dev->devno);
    class_destroy(dev->class);

    /*
     * 第三步：注销字符设备 + 归还设备号
     *
     * cdev_del：               从内核字符设备子系统注销
     * unregister_chrdev_region：归还动态分配的设备号
     */
    cdev_del(&dev->cdev);
    unregister_chrdev_region(dev->devno, 1);

    /*
     * 第四步：释放 DMA 缓冲区（替换 Stage 3 的 kfifo_free）
     *
     * 参数必须和 dma_alloc_coherent 完全一致（同一个 dev、
     * 同一个大小、同一个 buf、同一个 handle）。
     *
     * dma_free_coherent 做的事：
     *   1. 解除内核虚拟地址映射
     *   2. 把物理内存归还 CMA 池
     *   3. 清空相关页表项
     */
    dma_free_coherent(&pdev->dev, dev->dma_buf_size,
                      dev->dma_buf, dev->dma_handle);
    pr_info("leetx_dma: DMA buffer freed (%zu bytes)\n",
            dev->dma_buf_size);

    /* 第五步：销毁互斥锁 */
    mutex_destroy(&dev->write_lock);

    /*
     * 没有 kfree(dev)！
     * dev 是 devm_kzalloc 分配的，remove 返回后内核自动释放。
     * 也没有 gpio_free / free_irq — devm 版本自动处理。
     */

    pr_info("leetx_dma: instance %u removed, "
            "total samples=%d, total irq=%d\n",
            dev->instance_id,
            atomic_read(&dev->total_samples),
            atomic_read(&dev->irq_count));
    return 0;
}


static int leetx_dma_open(struct inode *inode, struct file *filp)
{
    struct leetx_dma_dev *dev;

    dev = container_of(inode->i_cdev, struct leetx_dma_dev, cdev);
    //同一时刻只能有一个进程打开设备
    if (atomic_cmpxchg(&dev->opened, 0, 1) != 0)
        return -EBUSY;

    filp->private_data = dev;

    if (use_timer) {
        hrtimer_start(&dev->sim_timer,
                      ktime_set(0, timer_period_us * 1000),
                      HRTIMER_MODE_REL);
    }

     pr_info("leetx_dma: instance %u opened (use_timer=%d, period=%dus)\n",
            dev->instance_id, use_timer, timer_period_us);
    return 0;
}

static int leetx_dma_release(struct inode *inode, struct file *filp)
{
    struct leetx_dma_dev *dev = filp->private_data;

    /*
     * 停掉 hrtimer：停止产生模拟中断数据。
     *
     * hrtimer_cancel 保证：
     *   1. 如果定时器还在等待队列里 → 取消掉
     *   2. 如果回调正在执行 → 等它执行完才返回
     *   3. 返回后保证不会再触发任何回调
     *
     * 对已停止的定时器调用 cancel 是空操作（不报错）。
     */
    hrtimer_cancel(&dev->sim_timer);

    /* 释放单实例锁：允许其他进程再次打开 */
    atomic_set(&dev->opened, 0);

    pr_info("leetx_dma: instance %u closed, "
            "total samples=%d, total irq=%d\n",
            dev->instance_id,
            atomic_read(&dev->total_samples),
            atomic_read(&dev->irq_count));
    return 0;
}


/* ================================================================
 * 模块 8: mmap 回调 —— 整个 Stage 4 最核心的函数
 *
 * 作用：把内核 DMA 缓冲区直接映射到用户态进程的地址空间。
 *       用户态拿到映射后，像读普通数组一样读传感器数据，
 *       不需要 read() / copy_to_user，实现零拷贝。
 *
 * 和 Stage 3 的本质区别：
 *   Stage 3：read() → kfifo_out → copy_to_user → 用户态拿到副本
 *            ↑ 数据从内核拷到用户空间，每次 read 都要拷
 *
 *   Stage 4：mmap() → 内核建页表映射 → 用户态直接访问 DMA 缓冲区
 *            ↑ 零拷贝！数据一直在 DMA 缓冲区里，用户态直接看
 *
 * mmap 的回调签名：
 *   struct file          *filp — 文件描述符（open 时创建的）
 *   struct vm_area_struct *vma — 虚拟内存区域（用户态要映射的地址范围）
 *
 * 调用链：
 *   用户态 mmap(NULL, size, PROT_READ, MAP_SHARED, fd, 0)
 *     → 内核 VFS 层：创建 vma 结构体，描述"用户态要映射哪段虚拟地址"
 *     → 内核调用你注册的 .mmap 回调（就是这个函数）
 *     → 你把 DMA 物理内存关联到 vma
 *     → 内核建立页表：用户态虚拟地址 → 物理页框
 *     → mmap() 返回用户态虚拟地址指针
 * ================================================================ */
static int leetx_dma_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct leetx_dma_dev *dev = filp->private_data;
    unsigned long pfn; //计算物理页框号
    int ret;

    pfn = dev->dma_handle >> PAGE_SHIFT;


    //检查大小是否超过缓冲区
    if ((vma->vm_end - vma->vm_start) > dev->dma_buf_size) {
        pr_err("leetx_dma: mmap size too large (req=%lu, max=%zu)\n",
               vma->vm_end - vma->vm_start, dev->dma_buf_size);
        return -EINVAL;
    }

    //禁用 CPU 缓存
    vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);

    //把物理内存映射到用户虚拟地址空间
     ret = remap_pfn_range(vma,
                          vma->vm_start,
                          pfn,
                          vma->vm_end - vma->vm_start,
                          vma->vm_page_prot);
    if (ret) {
        pr_err("leetx_dma: remap_pfn_range failed, ret=%d\n", ret);
        return -EAGAIN;
    }

    pr_info("leetx_dma: mmap — virt=0x%lx → phys=0x%pad, size=%lu\n",
            vma->vm_start, &dev->dma_handle,
            vma->vm_end - vma->vm_start);
    return 0;
}

static unsigned int leetx_dma_poll(struct file *filp,
                                   struct poll_table_struct *pts)
{
    struct leetx_dma_dev *dev = filp->private_data;
    unsigned int mask = 0;


    /*
     * poll_wait：把当前进程挂到 read_wq 等待队列上。
     *
     * 这一步不是阻塞！poll_wait 本身不睡觉，只是"挂号"。
     * 告诉内核："如果这个队列上的数据有变化，记得叫醒我。"
     *
     * 真正的睡眠发生在 poll_wait 返回后，VFS 层检查 mask == 0
     * → 进程状态改为 TASK_INTERRUPTIBLE → schedule() 睡觉。
     *
     * 参数：
     *   filp       — 文件描述符（内核用这个关联等待者）
     *   &dev->read_wq — 在哪个等待队列上挂号
     *   pts        — 内核 poll 子系统传进来的，原封不动传给 poll_wait
     */
    poll_wait(filp, &dev->read_wq, pts);

    if (atomic_read(&dev->total_samples) > 0)
        mask |= POLLIN | POLLRDNORM;

    return mask;
}

static const struct file_operations leetx_dma_fops = {
    .owner    = THIS_MODULE,
    .open     = leetx_dma_open,
    .release  = leetx_dma_release,
    .mmap     = leetx_dma_mmap,      /* 🆕 Stage 4 核心：零拷贝映射 */
    .poll     = leetx_dma_poll,      /* 🆕 Stage 4 通知机制：代替阻塞读 */
    /*
     * .read 暂不提供：鼓励用户态用 mmap + poll。
     * 如果需要向后兼容，按 Stage 3 的格式把 read 加回来即可。
     */
};

static struct platform_driver leetx_dma_driver = {
    .probe  = leetx_dma_probe,
    .remove = leetx_dma_remove,
    .driver = {
        .name           = "leetx_dma",
        .of_match_table = leetx_dma_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(leetx_dma_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang");
MODULE_DESCRIPTION("DMA + mmap zero-copy sensor driver — Leetx Stage 4");