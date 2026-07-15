#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/mod_devicetable.h>

#include <linux/pwm.h>
#include <linux/math64.h>

#define DEVICE_NAME  "leetx_pwm"

/* 默认 PWM 周期（纳秒）：1ms = 1000000ns → PWM 频率 = 1kHz */
static unsigned int pwm_period_ns = 1000000;
module_param(pwm_period_ns, uint, 0644);
MODULE_PARM_DESC(pwm_period_ns, "PWM 周期（纳秒），默认 1000000(1ms=1kHz)");

static const struct of_device_id leetx_pwm_of_match[] = {
    { .compatible = "leetx,pwm-ctrl" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, leetx_pwm_of_match);

/* 前向声明 */
static const struct file_operations leetx_pwm_fops;

struct leetx_pwm_dev {
    /* ========== 字符设备相关 ========== */
    dev_t               devno;
    struct cdev         cdev;
    struct class       *class;
    struct device      *device;
    atomic_t            opened;
    /* ========== PWM 相关 ========== */
    struct pwm_device *pwm;
    /* ========== 当前状态 ========== */
    unsigned int        period_ns;    /* PWM 周期（纳秒） */
    unsigned int        duty_ns;      /* 当前占空比（纳秒） */
};

static int leetx_pwm_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    struct leetx_pwm_dev *ddev;
    int ret;

    pr_info("leetx_pwm: probe starting...\n");

    /* 第一步：分配私有结构体 */
    ddev = devm_kzalloc(dev, sizeof(*ddev), GFP_KERNEL);
    if (!ddev) {
        pr_err("leetx_pwm: devm_kzalloc failed\n");
        return -ENOMEM;
    }

    /*
     * 第二步：获取 PWM 通道
     *
     * pwm_get(dev, NULL) → 从设备树的 pwms 属性自动解析
     *
     * 如果有多个 pwms（如 pwms = <&pwm1 0 1000000>, <&pwm2 0 1000000>;），
     * 用 pwm_get(dev, "motor") 通过 con_id 区分。
     * NULL 表示"给我第一个 pwms 就行"。
     *
     * 返回值：
     *  成功 → struct pwm_device * 指针
     *  失败 → ERR_PTR(-EINVAL) 等错误码
     */
    ddev->pwm = pwm_get(dev, NULL);
    if (IS_ERR(ddev->pwm)) {
        ret = PTR_ERR(ddev->pwm);
        if (ret == -EPROBE_DEFER)
            pr_info("leetx_pwm: PWM 通道尚未就绪，稍后重试\n");
        else
            pr_err("leetx_pwm: pwm_get failed, ret=%d "
                   "(设备树里有没有 pwms 属性？)\n", ret);
        return ret;
    }
    /*
     * 第三步：设置默认 PWM 周期
     *
     * pwm_config(pwm, duty_ns, period_ns)：
     *   把 PWM 硬件配置为：
     *     周期   = period_ns  纳秒
     *     占空比 = duty_ns    纳秒
     *
     *   初始占空比为 0（先不输出，等用户 write 才启动）
     *
     *   注意：pwm_config 只是计算和写寄存器，不开启输出。
     *   需要 pwm_enable() 之后 PWM 引脚上才有波形。
     */
    ddev->period_ns = pwm_period_ns;     /* 默认 1ms = 1kHz */
    ddev->duty_ns   = 0;                 /* 初始占空比 = 0 */
    pwm_config(ddev->pwm, 0, ddev->period_ns);

    pr_info("leetx_pwm: PWM channel acquired, period=%u ns\n",
            ddev->period_ns);

    /* 第四步：初始化字符设备 */
    atomic_set(&ddev->opened, 0);

    ret = alloc_chrdev_region(&ddev->devno, 0, 1, DEVICE_NAME);
    if (ret < 0) {
        pr_err("leetx_pwm: alloc_chrdev_region failed\n");
        goto err_pwm_put;
    }

    cdev_init(&ddev->cdev, &leetx_pwm_fops);
    ddev->cdev.owner = THIS_MODULE;
    ret = cdev_add(&ddev->cdev, ddev->devno, 1);
    if (ret < 0) {
        pr_err("leetx_pwm: cdev_add failed\n");
        goto err_unregister_region;
    }

    ddev->class = class_create(THIS_MODULE, "leetx_pwm_class");
    if (IS_ERR(ddev->class)) {
        ret = PTR_ERR(ddev->class);
        goto err_cdev_del;
    }

    ddev->device = device_create(ddev->class, dev,
                                 ddev->devno, NULL, DEVICE_NAME);
    if (IS_ERR(ddev->device)) {
        ret = PTR_ERR(ddev->device);
        goto err_class_destroy;
    }

    /* 第五步：挂到 platform_device */
    platform_set_drvdata(pdev, ddev);

    pr_info("leetx_pwm: probe done — /dev/%s\n", DEVICE_NAME);
    return 0;

err_class_destroy:
    class_destroy(ddev->class);
err_cdev_del:
    cdev_del(&ddev->cdev);
err_unregister_region:
    unregister_chrdev_region(ddev->devno, 1);
err_pwm_put:
    pwm_put(ddev->pwm);
    return ret;
}

static ssize_t leetx_pwm_write(struct file *filp,
                                const char __user *buf,
                                size_t count, loff_t *f_pos)
{
    struct leetx_pwm_dev *ddev = filp->private_data;
    unsigned int duty_pct;
    unsigned int duty_ns;
    char kbuf[16];
    int ret;

    /* 读用户态输入：最多 15 个字符 + '\0' */
    if (count > sizeof(kbuf) - 1)
        count = sizeof(kbuf) - 1;

    if (copy_from_user(kbuf, buf, count))
        return -EFAULT;

    kbuf[count] = '\0';


    //把字符串转换成 无符号整型
    ret = kstrtouint(kbuf, 10, &duty_pct);
    if (ret < 0) {
        pr_err("leetx_pwm: 输入格式错误，请写 0-100 的数字\n");
        return -EINVAL;
    }

    if (duty_pct > 100) {
        pr_err("leetx_pwm: 占空比范围 0-100，收到 %u\n", duty_pct);
        return -EINVAL;
    }

    duty_ns = (unsigned int)div_u64((u64)duty_pct * ddev->period_ns, 100);

    /*
     * pwm_config：把周期和占空比写进 PWM 硬件寄存器
     *
     * 参数：
     *   ddev->pwm        — PWM 通道
     *   duty_ns           — 高电平持续时间（纳秒）
     *   ddev->period_ns   — 完整周期（纳秒）
     *
     * 例：period=1000000ns, duty=300000ns → 30% 占空比
     *
     * pwm_config 内部做的事（你看不到的）：
     *   1. 根据 ipg_clk (66MHz) 和 period_ns 算分频系数
     *   2. 算 PWMPR 寄存器值（周期）
     *   3. 把 duty 值写入 FIFO（PWMSAR 寄存器）
     *   4. 如果 pwm_enable 已调，新配置立即生效
     */
    pwm_config(ddev->pwm, duty_ns, ddev->period_ns);

    /*
     * pwm_enable：开启 PWM 输出
     *
     * 只有在调了 pwm_enable 之后，引脚上才会有波形。
     * 第一次写且占空比 > 0 → 开启输出。
     * 第一次写且占空比 = 0 → 不开（没有意义）。
     */
    if (duty_pct > 0) {
        pwm_enable(ddev->pwm);
    } else {
        /* 占空比写 0 → 关闭 PWM 输出 */
        pwm_disable(ddev->pwm);
    }

    ddev->duty_ns = duty_ns;

    pr_info("leetx_pwm: duty=%u%% (%u ns), %s\n",
            duty_pct, duty_ns,
            duty_pct > 0 ? "ENABLED" : "DISABLED");

    return count;
}

static int leetx_pwm_remove(struct platform_device *pdev)
{
    struct leetx_pwm_dev *ddev = platform_get_drvdata(pdev);

    if (!ddev)
        return 0;

    /* 关闭 PWM 输出 */
    pwm_disable(ddev->pwm);
    /* 释放 PWM 通道 */
    pwm_put(ddev->pwm);

    device_destroy(ddev->class, ddev->devno);
    class_destroy(ddev->class);
    cdev_del(&ddev->cdev);
    unregister_chrdev_region(ddev->devno, 1);

    pr_info("leetx_pwm: removed\n");
    return 0;
}

static int leetx_pwm_open(struct inode *inode, struct file *filp)
{
    struct leetx_pwm_dev *ddev;

    ddev = container_of(inode->i_cdev, struct leetx_pwm_dev, cdev);

    if (atomic_cmpxchg(&ddev->opened, 0, 1) != 0)
        return -EBUSY;

    filp->private_data = ddev;

    pr_info("leetx_pwm: opened\n");
    return 0;
}

static int leetx_pwm_release(struct inode *inode, struct file *filp)
{
    struct leetx_pwm_dev *ddev = filp->private_data;

    atomic_set(&ddev->opened, 0);

    pr_info("leetx_pwm: closed\n");
    return 0;
}

static const struct file_operations leetx_pwm_fops = {
    .owner    = THIS_MODULE,
    .open     = leetx_pwm_open,
    .release  = leetx_pwm_release,
    .write    = leetx_pwm_write,
    /* 注意：没有 .read .mmap .poll — PWM 是输出设备，不需要这些 */
};

static struct platform_driver leetx_pwm_driver = {
    .probe  = leetx_pwm_probe,
    .remove = leetx_pwm_remove,
    .driver = {
        .name           = "leetx_pwm",
        .of_match_table = leetx_pwm_of_match,
        .owner          = THIS_MODULE,
    },
};

module_platform_driver(leetx_pwm_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Yang");
MODULE_DESCRIPTION("PWM output driver — Leetx Stage 7");