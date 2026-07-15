# 🔧 Linux 驱动开发详细学习计划

> **目标公司**：砺星工业科技（上海）有限公司（[Leetx](http://www.leetx.com)）  
> **目标岗位**：Linux 驱动开发工程师 / 嵌入式软件工程师  
> **目标领域**：工业伺服系统（拧紧、压装、涂胶）、精密数据采集、实时控制  
> **硬件平台**：i.MX6ULL 开发板（NXP Cortex-A7）  
> **总周期**：约 18 – 20 周（全日制可压缩至 12 – 14 周）  

---

## 进度概览

> **更新时间**：2026-07-15  
> **当前阶段**：Stage 9（IIO 子系统，准备中）

```
Stage 1  ████████████████████  ✅ 字符驱动深度重构          kernel/stage01-char/
Stage 2  ████████████████████  ✅ 平台设备驱动模型           kernel/stage02-platform/
Stage 3  ████████████████████  ✅ 中断子系统                 kernel/stage03-interrupt/
Stage 4  ████████████████████  ✅ 内存与 DMA                kernel/stage04-dma/
Stage 5  ████████████████████  ✅ SPI 总线驱动框架           kernel/stage05-spi/
Stage 6  ████████████████████  ✅ I2C 总线驱动框架           kernel/stage06-i2c/
Stage 7  ████████████████████  ✅ PWM 输出驱动               kernel/stage07-pwm/
Stage 8  ████████░░░░░░░░░░░░  ⚠️ SocketCAN 工业总线         kernel/stage08-can/
Stage 9  ░░░░░░░░░░░░░░░░░░░░  ⬜ IIO 工业 I/O 子系统        kernel/stage07-iio/
Stage 10 ░░░░░░░░░░░░░░░░░░░░  ⬜ PREEMPT_RT 实时内核       —
Stage 11 ░░░░░░░░░░░░░░░░░░░░  ⬜ 以太网驱动结构            —
Stage 12 ░░░░░░░░░░░░░░░░░░░░  ⬜ 综合实战项目              —
```

| 阶段 | 目录 | 状态 | 说明 |
|------|------|------|------|
| 1 字符驱动 | `kernel/stage01-char/` | ✅ | cdev + kfifo + waitqueue + mutex |
| 2 平台设备 | `kernel/stage02-platform/` | ✅ | platform_driver + 设备树 + devm |
| 3 中断子系统 | `kernel/stage03-interrupt/` | ✅ | threaded IRQ (hard + thread) + hrtimer |
| 4 DMA | `kernel/stage04-dma/` | ✅ | dma_alloc_coherent + mmap + poll |
| 5 SPI 总线 | `kernel/stage05-spi/` | ✅ | spi_driver + 模拟/真实双模式 |
| 6 I2C 总线 | `kernel/stage06-i2c/` | ✅ | i2c_driver + EEPROM 读写 |
| 7 PWM 输出 | `kernel/stage07-pwm/` | ✅ | pwm_get/pwm_config/pwm_enable |
| 8 SocketCAN | `kernel/stage08-can/` | ⚠️ | 用户态 demo 已有，内核驱动待写 |
| 9 IIO 子系统 | `kernel/stage07-iio/` | ⬜ | **最关键的面试阶段** |
| 10 PREEMPT_RT | — | ⬜ | RT 内核编译 + cyclictest |
| 11 以太网 | — | ⬜ | fec 驱动源码分析 |
| 12 综合项目 | — | ⬜ | 数据采集 + 实时控制闭环 |

## 目录

- [进度概览](#进度概览)
- [前置要求](#前置要求)
- [第一阶段：字符驱动深度重构](#第一阶段字符驱动深度重构)
- [第二阶段：平台设备驱动模型](#第二阶段平台设备驱动模型)
- [第三阶段：中断子系统深度掌握](#第三阶段中断子系统深度掌握)
- [第四阶段：内存与-dma](#第四阶段内存与-dma)
- [第五阶段：spi--i2c-总线驱动框架](#第五阶段spi--i2c-总线驱动框架)
- [第六阶段：工业总线---socketcan](#第六阶段工业总线---socketcan)
- [第七阶段：iio-工业io子系统](#第七阶段iio-工业io子系统)
- [第八阶段：preempt_rt-实时内核](#第八阶段preempt_rt-实时内核)
- [第九阶段：以太网驱动结构](#第九阶段以太网驱动结构)
- [第十阶段：综合实战项目](#第十阶段综合实战项目)
- [时间分配总表](#时间分配总表)
- [每日学习节奏](#每日学习节奏)
- [学习建议](#学习建议)

---

## 前置要求

| 前置技能 | 熟练度 | 说明 |
|----------|--------|------|
| C 语言 | 精通 | 指针、内存管理、位运算、`volatile`/`const` |
| 基础字符设备驱动 | 熟练 | `open`/`read`/`write`/`ioctl` 结构 |
| 常见总线协议概念 | 了解 | I2C、SPI、UART 基本时序 |
| Linux 基本操作 | 熟练 | 交叉编译、模块加载、`printk` 调试 |

---

## 第一阶段：字符驱动深度重构

> **周期**：2 周  
> **目标**：把字符驱动从"能跑"升级到"工业级"——抗高频访问、处理并发、暴露标准接口

### 1.1 并发控制与同步

**任务**：写一个全局字符设备（多进程共享），正确处理竞争条件。

| 机制 | 适用场景 | 睡眠？ |
|------|----------|--------|
| `mutex` | 进程上下文保护临界区 | ✅ 可睡眠 |
| `spinlock` | 中断 / 软中断上下文 | ❌ 不可睡眠 |
| `atomic_t` | 计数统计、状态标志 | — |
| `wait_queue_head_t` | 阻塞式读写（无数据时休眠） | ✅ |
| `completion` | 等待异步事件（DMA 完成、ADC 就绪） | ✅ |

**代码练习**

```c
/**
 * 实现一个 /dev/leetx_queue 设备
 *
 * - write: 写入扭矩采样值（u32）
 * - read:  阻塞读取，无数据时睡眠（wait_event_interruptible）
 * - 内核中维护一个环形缓冲区（kfifo）
 * - 多进程可同时 open，mutex 保护写，spinlock 保护缓冲区指针
 */
```

**验收标准**

- 两个终端，一个持续写、一个持续读
- 数据不丢、不乱
- `cat /proc/lock_stat` 无异常锁竞争

### 1.2 内核定时器与高精度计时

**任务**：在驱动中加入周期性采样，对比三种定时器。

| 定时器 | 精度 | 运行上下文 | 适用场景 |
|--------|------|------------|----------|
| `timer_list` | ms 级 | 软中断 | 非关键周期任务 |
| `hrtimer` | μs 级 | 硬中断 | **250μs 控制周期（Leetx 伺服）** |
| `hrtimer` + kthread | ns 级 | 进程上下文 | 复杂计算 + 实时要求 |

**代码练习**

```c
/**
 * 驱动中实现 hrtimer，每 250μs 触发一次
 *
 * 每次触发 → 读 GPIO 模拟传感器 → 存 kfifo → 唤醒阻塞的 read
 * 用户态用 clock_gettime 打时间戳，测量实际 jitter
 */
```

**验收标准**

- 用户态程序打印每次读取的时间间隔
- 观察抖动范围，画出直方图

### 1.3 从 ioctl 升级到 sysfs

**任务**：把驱动参数暴露为标准 sysfs 属性，用户无需写代码即可配置。

```bash
# 最终效果
cat /sys/class/leetx/sensor0/sample_rate       # → 1000
echo 2000 > /sys/class/leetx/sensor0/sample_rate
cat /sys/class/leetx/sensor0/trigger_value     # 读取当前阈值
```

**核心宏和 API**

| API | 作用 |
|-----|------|
| `__ATTR()` / `DEVICE_ATTR_RW` | 定义设备属性 |
| `struct attribute_group` | 属性分组 |
| `sysfs_create_group()` | 创建属性组 |
| `container_of()` | 从 `kobj` 反推设备结构体 |
| `show` / `store` 函数 | 读写回调 |

**验收标准**

- `echo` / `cat` 能实时读写驱动参数
- 参数变化后驱动行为随之改变
- 删除 ioctl 接口，代码更简洁

---

## 第二阶段：平台设备驱动模型

> **周期**：2 周  
> **目标**：掌握 Linux 驱动体系的骨架——`bus-driver-device` 模型 + 设备树

### 2.1 platform_driver / platform_device

**两个必须吃透的概念**

1. **匹配机制**：设备树 `compatible` 字符串 → `of_match_table` → `probe` 函数被调用
2. **资源管理**：`devm_*` 系列 API — 驱动 remove 时自动释放

**设备树节点**

```dts
// i.MX6ULL 设备树
/ {
    my_sensor: leetx-sensor {
        compatible = "leetx,sensor-v1";
        sample-rate = <1000>;
        trigger-gpio = <&gpio1 18 0>;
    };
};
```

**驱动骨架**

```c
#include <linux/platform_device.h>
#include <linux/mod_devicetable.h>

static const struct of_device_id leetx_sensor_of_match[] = {
    { .compatible = "leetx,sensor-v1" },
    { /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, leetx_sensor_of_match);

static int leetx_sensor_probe(struct platform_device *pdev)
{
    struct device *dev = &pdev->dev;
    int rate;

    /* 从设备树提取资源 */
    if (of_property_read_u32(dev->of_node, "sample-rate", &rate))
        return -EINVAL;

    /* 获取 GPIO */
    struct gpio_desc *gpio = devm_gpiod_get(dev, "trigger", GPIOD_IN);
    if (IS_ERR(gpio))
        return PTR_ERR(gpio);

    /* 注册字符设备 / IIO 设备 / ... */
    pr_info("leetx_sensor probed, sample_rate=%d\n", rate);
    return 0;
}

static int leetx_sensor_remove(struct platform_device *pdev)
{
    pr_info("leetx_sensor removed\n");
    return 0;
}

static struct platform_driver leetx_sensor_driver = {
    .probe  = leetx_sensor_probe,
    .remove = leetx_sensor_remove,
    .driver = {
        .name           = "leetx_sensor",
        .of_match_table = leetx_sensor_of_match,
    },
};
module_platform_driver(leetx_sensor_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Your Name");
MODULE_DESCRIPTION("Leetx sensor platform driver");
```

**验收标准**

- `insmod` 后 probe 被自动调用
- `rmmod` 后 remove 被自动调用
- 能解释 module_platform_driver 宏的展开过程

### 2.2 pinctrl 与时钟子系统

**调试命令**

```bash
# 查看引脚复用状态
cat /sys/kernel/debug/pinctrl/20e0000.iomuxc/pinmux-pins

# 查看时钟树（确认外设时钟被正确使能）
cat /sys/kernel/debug/clk/clk_summary | grep -E "uart|spi|can|pwm"
```

**关键理解**

> 驱动不只是读写寄存器。你要确保：
> 1. 引脚被正确复用（IOMUXC 配置）
> 2. 时钟被正确使能（CCM 配置）
> 3. 这两项由设备树 + 内核框架自动完成——但要能 debug 异常情况

---

## 第三阶段：中断子系统深度掌握

> **周期**：2 周  
> **目标**：理解从硬件中断到处理函数完成的全链路，会用三种下半部机制

### 3.1 中断处理生命周期

```
硬件中断触发
    │
    ▼
GIC (Generic Interrupt Controller)      ← 中断控制器
    │
    ▼
中断向量表 → 汇编入口 → 保存现场
    │
    ▼
__irq_svc / do_IRQ()                    ← 架构相关
    │
    ▼
generic_handle_irq()                    ← 根据 irq number 找到 irq_desc
    │
    ▼
handle_level_irq() / handle_edge_irq()  ← flow handler
    │
    ▼
你的 irq_handler()                      ← 硬中断上下文 — 不能睡眠！
```

### 3.2 三种下半部机制对比

**代码练习**：同一个 GPIO 中断写出三个版本，用示波器测量延迟。

```c
/* 版本 A：tasklet */
/* - 同一个 tasklet 不会并发，简单但受限于软中断上下文 */
DECLARE_TASKLET(my_tasklet, tasklet_fn, (unsigned long)&dev);

/* 版本 B：workqueue */
/* - 进程上下文，可以睡眠，但调度延迟大（ms 级） */
schedule_work(&my_work);

/* 版本 C：threaded IRQ ← 工业驱动首选 */
/* - hard_handler：确认中断源、关中断（几 μs） */
/* - thread_fn：进程上下文，可以做重活 */
request_threaded_irq(
    irq,
    hard_handler,   /* 硬中断：快进快出 */
    thread_fn,      /* 线程上下文：读 SPI/I2C、数据处理 */
    IRQF_TRIGGER_RISING | IRQF_ONESHOT,
    "leetx_sensor",
    dev
);
```

**Leetx 场景对应**

```
场景：扭矩传感器通过 GPIO 触发 "新数据就绪" 中断

hard_handler:  记录时间戳（几 μs），确认中断
       ↓
thread_fn:     读 SPI 寄存器（ms 级，可睡眠）
               存入 kfifo
               唤醒阻塞的 read
```

### 3.3 IRQF_ONESHOT 与中断共享

```c
// 中断共享 + oneshot —— 保持中断屏蔽直到 thread_fn 完成
request_threaded_irq(
    irq, NULL, my_thread_fn,
    IRQF_SHARED | IRQF_ONESHOT,
    "leetx_shared", dev
);
```

**任务**：在 i.MX6ULL 上模拟两个设备共用一个中断号，验证 IRQF_SHARED 行为。

**验收标准**

- 画出 GPIO 中断从引脚电平变化到 `thread_fn` 执行的完整调用链
- 用示波器测量三种下半部机制的延迟差异
- 能解释为什么中断上下文不能调 `mutex_lock`

---

## 第四阶段：内存与 DMA

> **周期**：2 周  
> **目标**：掌握内核内存分配策略，实现 mmap + DMA 零拷贝数据路径

### 4.1 内核内存分配全景

| 分配 API | 物理连续？ | 虚拟连续？ | 适用场景 |
|----------|------------|------------|----------|
| `kmalloc` | ✅ | ✅ | 小对象（< 128KB） |
| `vmalloc` | ❌ | ✅ | 大缓冲区（几 MB） |
| `dma_alloc_coherent` | ✅ | ✅ | **DMA 传输（设备可访问）** |
| `get_free_pages` | ✅ | ✅ | 页级分配 |
| `kmap` / `ioremap` | — | — | 映射设备寄存器 / highmem |

**代码练习**：写一个模块，用 debugfs 暴露内存使用情况。

```bash
# 运行时验证
insmod memory_test.ko
cat /sys/kernel/debug/leetx_mem/alloc_summary
cat /proc/slabinfo | grep leetx
```

### 4.2 mmap 零拷贝实现

**驱动侧**

```c
#include <linux/dma-mapping.h>
#include <linux/mm.h>

static dma_addr_t dma_handle;
static void *dma_buf;

/* probe 中分配 */
dma_buf = dma_alloc_coherent(dev, PAGE_SIZE, &dma_handle, GFP_KERNEL);
if (!dma_buf)
    return -ENOMEM;

static int leetx_mmap(struct file *filp, struct vm_area_struct *vma)
{
    struct my_dev *dev = filp->private_data;
    unsigned long size = vma->vm_end - vma->vm_start;

    return dma_mmap_coherent(
        dev->parent, vma,
        dev->dma_buf, dev->dma_handle, size
    );
}

static const struct file_operations leetx_fops = {
    .owner = THIS_MODULE,
    .mmap  = leetx_mmap,
    /* 不再需要 .read！ */
};
```

**用户侧**

```c
void *buf = mmap(
    NULL, size,
    PROT_READ | PROT_WRITE,
    MAP_SHARED,
    fd, 0
);
/* 读写 buf 直接反映内核数据 —— 不需要 read() 系统调用！ */
```

**验收标准**

- 用户态通过 mmap 读取 1MB 数据，对比 `read()` 方式的吞吐量
- 用 `gettimeofday` 统计差异：mmap 版本至少快 3-5 倍
- 用 `cat /proc/vmallocinfo` 验证映射关系

### 4.3 CMA 与 reserved-memory

**i.MX6ULL 设备树中预留连续内存**

```dts
/ {
    reserved-memory {
        #address-cells = <1>;
        #size-cells = <1>;
        ranges;

        leetx_dma_buf: leetx-dma@88000000 {
            compatible = "shared-dma-pool";
            reg = <0x88000000 0x1000000>;    /* 16 MB */
            no-map;
        };
    };

    my_device {
        compatible = "leetx,my-device";
        memory-region = <&leetx_dma_buf>;
    };
};
```

**驱动中引用**

```c
/* probe 中获取 CMA */
np = dev->of_node;
ret = of_reserved_mem_device_init(dev);
buf = dma_alloc_coherent(dev, size, &handle, GFP_KERNEL);
```

**验收标准**

- 驱动能成功分配 4MB 的连续物理内存
- `cat /proc/dma/dma-ops` 显示 DMA 区域信息

---

## 第五阶段：SPI / I2C 总线驱动框架

> **周期**：2 周  
> **目标**：理解 Linux 的 bus-driver-device 三层模型，写出标准 SPI/I2C 设备驱动

### 5.1 Linux 总线驱动架构

```
三层协作模型：

┌──────────────────────────────────────┐
│  设备驱动层 (spi_driver / i2c_driver) │  ← 你写传感器驱动
│  关心：传感器寄存器、数据格式          │
├──────────────────────────────────────┤
│  核心层 (spi.c / i2c-core.c)          │  ← 内核提供，不可见
│  关心：统一的总线 API，设备-驱动匹配    │
├──────────────────────────────────────┤
│  控制器驱动 (spi-imx.c / i2c-imx.c)   │  ← 芯片厂商 NXP 提供
│  关心：i.MX6ULL 寄存器操作、DMA        │
└──────────────────────────────────────┘
```

### 5.2 SPI 设备驱动

**设备树**

```dts
&ecspi1 {
    status = "okay";
    cs-gpios = <&gpio4 26 GPIO_ACTIVE_LOW>;

    torque_sensor: ads8688@0 {
        compatible = "ti,ads8688";
        reg = <0>;                       /* CS0 */
        spi-max-frequency = <10000000>;
    };
};
```

**驱动骨架**

```c
#include <linux/spi/spi.h>

static const struct of_device_id adc_of_match[] = {
    { .compatible = "ti,ads8688" },
    { }
};
MODULE_DEVICE_TABLE(of, adc_of_match);

static int adc_probe(struct spi_device *spi)
{
    /* spi 自动指向匹配的设备 */
    spi->mode = SPI_MODE_0;
    spi->bits_per_word = 16;
    spi_setup(spi);

    /* 注册到 IIO 子系统（见第七阶段） */
    return 0;
}

static struct spi_driver adc_driver = {
    .probe  = adc_probe,
    .driver = {
        .name           = "ads8688",
        .of_match_table = adc_of_match,
    },
};
module_spi_driver(adc_driver);
```

**核心 API 速查**

| API | 用途 |
|-----|------|
| `spi_write(spi, buf, len)` | 仅发送 |
| `spi_read(spi, buf, len)` | 仅接收 |
| `spi_write_then_read(spi, tx, tx_len, rx, rx_len)` | 全双工 / 半双工 |
| `spi_sync(spi, &msg)` | 同步传输 |
| `spi_async(spi, &msg)` | 异步 + completion 通知 |

### 5.3 I2C 设备驱动

**设备树**

```dts
&i2c2 {
    pressure_sensor: bmp280@76 {
        compatible = "bosch,bmp280";
        reg = <0x76>;
    };
};
```

**SMBus 风格 API（推荐）**

```c
i2c_smbus_read_byte_data(client, reg);
i2c_smbus_write_byte_data(client, reg, val);
i2c_smbus_read_word_data(client, reg);      /* 16 位 */
```

**原始 I2C 传输（复杂场景）**

```c
struct i2c_msg msgs[2] = {
    { .addr = client->addr, .flags = 0,            .buf = wr_buf, .len = 1 },
    { .addr = client->addr, .flags = I2C_M_RD,      .buf = rd_buf, .len = 2 },
};
i2c_transfer(client->adapter, msgs, 2);   /* 支持 REPEATED START */
```

> ⚠️ **关键注意**：I2C 函数可能在进程上下文中睡眠，**绝不可以在中断 / 软中断中调用**。

### 5.4 新旧思维对比

| 旧思维（裸字符驱动） | 新思维（总线驱动框架） |
|----------------------|------------------------|
| 手动创建设备节点 | 设备树描述 + 内核匹配 probe |
| 写死寄存器地址 | 从设备树 `reg` 属性自动获取 |
| 手写 `read`/`write` | 复用 SPI/I2C 核心层传输 API |
| 单一模块 | 核心层 + 控制器驱动 + 设备驱动，三层协作 |

**验收标准**

- 能画出 `spi_device` / `spi_driver` / `spi_controller` / `spi_message` / `spi_transfer` 的关系图
- 在 i.MX6ULL 上接一个真实的 SPI ADC（如 MCP3008 / ADS8688）并跑通驱动
- 用示波器抓取 CS、CLK、MOSI、MISO 四路波形，验证 SPI 时序

### ✅ 实际进度

- **SPI 驱动**（`kernel/stage05-spi/leetx_spi_adc.c`）：spi_driver 框架，支持 `use_sim` 模拟/真实双模式，集成 DMA + mmap + threaded IRQ + hrtimer
- **I2C 驱动**（`kernel/stage06-i2c/leetx_i2c_eeprom.c`）：i2c_driver 框架，AT24C02 EEPROM 读写，同样集成完整的 DMA + mmap + 中断链路
- **PWM 驱动**（`kernel/stage07-pwm/leetx_pwm.c`）：platform_driver + PWM 子系统，用户态写 0-100 控制占空比，`pwm_get/pwm_config/pwm_enable` 标准 API

---

## 第六阶段：工业总线 — SocketCAN

> **周期**：1.5 周  
> **目标**：掌握 CAN 总线在 Linux 下的标准接口——SocketCAN

### 6.1 在 i.MX6ULL 上启用 CAN

i.MX6ULL 自带 **flexcan** 控制器。

**设备树**

```dts
&flexcan1 {
    status = "okay";
};

&flexcan2 {
    status = "okay";
};
```

**命令行测试**（需外接 CAN 收发器，如 SN65HVD230）

```bash
ip link set can0 type can bitrate 500000
ip link set up can0
cansend can0 123#1122334455667788
candump can0
```

### 6.2 SocketCAN 编程

```c
#include <linux/can.h>
#include <linux/can/raw.h>
#include <net/if.h>
#include <sys/socket.h>

/* 创建 CAN 套接字 */
int s = socket(PF_CAN, SOCK_RAW, CAN_RAW);

struct sockaddr_can addr;
struct ifreq ifr;

strcpy(ifr.ifr_name, "can0");
ioctl(s, SIOCGIFINDEX, &ifr);

addr.can_family = AF_CAN;
addr.can_ifindex = ifr.ifr_ifindex;
bind(s, (struct sockaddr *)&addr, sizeof(addr));

/* 发送 CAN 帧 */
struct can_frame frame;
frame.can_id = 0x123;
frame.can_dlc = 8;
memset(frame.data, 0xAA, 8);
write(s, &frame, sizeof(frame));

/* 接收 CAN 帧（配合 epoll/select） */
read(s, &frame, sizeof(frame));
```

### 6.3 读懂 flexcan 驱动（内核源码剖析）

```
SocketCAN 架构全景：

  用户态 (cansend / candump / 你的程序)
        │
   socket() → write() → read()
        │
  ┌────────────────────────┐
  │  AF_CAN 协议层         │
  │  net/can/af_can.c      │
  │  can_rx_register()     │
  └────────┬───────────────┘
           │
  ┌────────▼───────────────┐
  │  flexcan 控制器驱动     │
  │  drivers/net/can/      │
  │       flexcan.c        │
  └────────┬───────────────┘
           │
     CAN 总线 (物理层)
```

**任务**：阅读 `drivers/net/can/flexcan.c`，画出 `probe → 注册 net_device → 配置寄存器 → 中断处理 → NAPI poll → 收发帧` 的完整调用链。

**验收标准**

- 完成一次 `cansend` + `candump` 的端到端收发
- 用逻辑分析仪抓取 CAN 差分信号波形
- 能解释 NAPI 在 CAN 驱动中为什么比纯中断收包好

---

## 第七阶段：IIO（工业 I/O）子系统

> **周期**：1.5 周  
> **目标**：用 IIO 框架抽象物理传感器，这是与 Leetx 产品匹配度最高的一块  
> **重要性**：⭐ ⭐ ⭐ ⭐ ⭐

### 7.1 为什么用 IIO 而不是裸字符设备？

| 对比项 | 裸字符驱动 | IIO 框架 |
|--------|-----------|----------|
| 传感器数据 | 原始 ADC 值（无单位） | 带 scale/offset 的物理量 |
| 采样率配置 | 自己写 ioctl | 标准 sysfs 接口 |
| 连续采集 | 自己实现环形缓冲区 | `iio_buffer` 基础设施 |
| 触发源 | 自己管理 hrtimer | `iio_trigger` 框架 |
| 多通道同步 | 自己拼数据 | `scan_elements` 机制 |
| 用户态工具 | 自己写 | `iio_info` / `iio_generic_buffer` 标准工具 |

### 7.2 IIO sysfs 接口预览

```bash
# 单次读取
cat /sys/bus/iio/devices/iio:device0/in_voltage0_raw     # → 2048
cat /sys/bus/iio/devices/iio:device0/in_voltage0_scale   # → 0.305
# 实际电压 = 2048 × 0.305 mV = 624.6 mV

# 配置
cat /sys/bus/iio/devices/iio:device0/sampling_frequency  # → 1000
echo 4000 > /sys/bus/iio/devices/iio:device0/sampling_frequency

# 连续采集
echo 1 > /sys/bus/iio/devices/iio:device0/buffer/enable
cat /dev/iio:device0 | xxd                                 # 读连续数据
echo 0 > /sys/bus/iio/devices/iio:device0/buffer/enable
```

### 7.3 写一个 IIO 驱动

**任务**：把第五阶段的 SPI ADC 驱动用 IIO 框架重新实现。

```c
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>

struct leetx_sensor {
    struct spi_device *spi;
    struct iio_dev *indio_dev;
};

/* 单次读取回调 */
static int leetx_read_raw(struct iio_dev *indio_dev,
                          struct iio_chan_spec const *chan,
                          int *val, int *val2, long mask)
{
    struct leetx_sensor *sensor = iio_priv(indio_dev);

    switch (mask) {
    case IIO_CHAN_INFO_RAW:
        *val = spi_read_adc(sensor->spi);   /* 读 SPI ADC */
        return IIO_VAL_INT;
    case IIO_CHAN_INFO_SCALE:
        *val = 0;
        *val2 = 305000;                     /* 0.305 mV/LSB */
        return IIO_VAL_INT_PLUS_MICRO;
    default:
        return -EINVAL;
    }
}

/* 通道规格描述 —— 直接表达物理量 */
static const struct iio_chan_spec leetx_channels[] = {
    {
        .type           = IIO_TORQUE,        /* ← 扭矩 —— 不是 ADC 值！*/
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .address        = 0,
        .scan_index     = 0,
    },
    {
        .type           = IIO_ANGL,          /* ← 角度 */
        .info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
                              BIT(IIO_CHAN_INFO_SCALE),
        .address        = 1,
        .scan_index     = 1,
    },
};

static const struct iio_info leetx_iio_info = {
    .read_raw = leetx_read_raw,
};

/* probe 中注册 */
static int leetx_sensor_probe(struct spi_device *spi)
{
    struct leetx_sensor *sensor;
    struct iio_dev *indio_dev;

    indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*sensor));
    if (!indio_dev)
        return -ENOMEM;

    sensor = iio_priv(indio_dev);
    sensor->spi = spi;

    indio_dev->name         = "leetx-torque";
    indio_dev->info         = &leetx_iio_info;
    indio_dev->channels     = leetx_channels;
    indio_dev->num_channels = ARRAY_SIZE(leetx_channels);
    indio_dev->modes        = INDIO_DIRECT_MODE;

    spi_set_drvdata(spi, indio_dev);
    return devm_iio_device_register(&spi->dev, indio_dev);
}
```

**Leetx 物理量 → IIO 通道类型对照**

| Leetx 产品 | 物理量 | IIO 通道类型 |
|------------|--------|-------------|
| 拧紧工具 | 扭矩 | `IIO_TORQUE` |
| 拧紧工具 | 角度 | `IIO_ANGL` |
| 伺服压机 | 压力 | `IIO_PRESSURE` |
| 伺服压机 | 位移 | `IIO_DISTANCE` |
| 涂胶系统 | 流量 | `IIO_FLOW` |
| 通用 | 电压 / 电流 | `IIO_VOLTAGE` / `IIO_CURRENT` |

**验收标准**

- SPI ADC 驱动成功注册为 IIO 设备
- `iio_info` 命令能列出设备和通道信息
- 可以用 `cat` 标准 sysfs 路径读到物理量数据

---

## 第八阶段：PREEMPT_RT 实时内核

> **周期**：1.5 周  
> **目标**：给 i.MX6ULL 换上 RT 内核，测量并优化实时性

### 8.1 编译 RT 内核

```bash
# 下载内核 + RT 补丁
wget https://cdn.kernel.org/pub/linux/kernel/v6.x/linux-6.1.tar.xz
wget https://cdn.kernel.org/pub/linux/kernel/projects/rt/6.1/patch-6.1.x-rt.patch.xz

# 打补丁
cd linux-6.1
xzcat ../patch-6.1.x-rt.patch.xz | patch -p1

# 配置
make ARCH=arm imx_v6_v7_defconfig
make ARCH=arm menuconfig

# 必须启用的选项
#   → General setup
#     → Preemption Model: Fully Preemptible Kernel (RT)
#       [*] CONFIG_PREEMPT_RT
#   → Kernel Features
#     [*] High Resolution Timer Support
#       [*] CONFIG_HIGH_RES_TIMERS
#     → Timer frequency: 1000 Hz
#       [*] CONFIG_HZ_1000

# 编译
make ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- -j4 zImage dtbs modules
```

### 8.2 cyclictest 量化测量

```bash
# 标准实时性测试
cyclictest -t 2 -p 99 -i 250 -l 100000 -h 100 -q > rt_histogram.txt

# 参数说明
# -t 2:  2 个线程
# -p 99: SCHED_FIFO 优先级 99
# -i 250: 周期 250μs (Leetx 控制周期)
# -l 100000: 10 万次迭代
# -h 100: 100 格直方图
```

**必须能回答**

| 内核类型 | CONFIG 宏 | 典型最大延迟 |
|----------|-----------|-------------|
| 无抢占 | `PREEMPT_NONE` | ms 级 |
| 自愿抢占 | `PREEMPT` | 数百 μs |
| 完全抢占 | `PREEMPT` | 数十 μs |
| 实时 | `PREEMPT_RT` | **< 10 μs** |

### 8.3 内核 RT 线程

```c
#include <linux/kthread.h>
#include <linux/sched.h>
#include <linux/hrtimer.h>

static int rt_thread_fn(void *data)
{
    struct sched_param param = { .sched_priority = 99 };
    sched_setscheduler(current, SCHED_FIFO, &param);

    while (!kthread_should_stop()) {
        /* 每 250μs 执行一次 */
        do_periodic_work();

        set_current_state(TASK_INTERRUPTIBLE);
        schedule_hrtimeout(&next_period, HRTIMER_MODE_ABS);
    }

    return 0;
}
```

**验证方法**

- 线程入口处 GPIO 拉高，出口处拉低
- 示波器测量高电平持续时间 = 处理时间
- 两个上升沿间隔的抖动 = jitter

**验收标准**

- 内核 RT 线程 jitter < 50 μs（用示波器验证）
- `cyclictest` 直方图无长尾（最大延迟 < 100 μs）
- CPU 隔离后不相关任务不干扰 RT 核心

### 8.4 CPU 隔离调优

```bash
# 内核启动参数
console=ttymxc0,115200 isolcpus=0 rcu_nocbs=0 nohz_full=0 irqaffinity=1

# 效果：CPU0 专做实时任务，中断和 RCU 在 CPU1 处理
```

---

## 第九阶段：以太网驱动结构

> **周期**：1 周  
> **目标**：理解 Linux 网络驱动的基本结构，能读懂 i.MX6ULL 的 fec 驱动

### 9.1 读懂 fec 驱动

```bash
# 核心文件
drivers/net/ethernet/freescale/fec.c
drivers/net/ethernet/freescale/fec_main.c
```

**需要追踪的关键函数链**

```
probe:
  fec_probe()
    → alloc_etherdev()
    → fec_enet_init()
    → register_netdev()

中断 → 收包:
  fec_enet_interrupt()          ← 硬中断 handler
    → napi_schedule()           ← 调度 NAPI
    → fec_enet_rx_napi()       ← NAPI poll（软中断上下文）
      → netif_receive_skb()    ← 提交到协议栈

发包:
  fec_enet_start_xmit()        ← ndo_start_xmit
    → 填充 BD (Buffer Descriptor)
    → 触发 DMA 发送
```

### 9.2 核心结构体

```c
struct net_device            /* 内核代表一块网卡 */
  ├── ndo_open()             /* ifconfig eth0 up */
  ├── ndo_stop()             /* ifconfig eth0 down */
  ├── ndo_start_xmit()       /* 发包入口 */
  └── struct napi_struct     /* NAPI 收包 */
```

**验收标准**

- 画出 `ping` 一个包从用户态到硬件发送、再到接收返回的完整路径
- 能解释为什么需要 NAPI（对比纯中断模式的优缺点）
- 用 `ethtool -S eth0` 看网卡统计寄存器

---

## 第十阶段：综合实战项目

> **周期**：3 周  
> **目标**：将前九阶段所有技能串联成一个完整项目，直接对标 Leetx

### 🏆 项目：i.MX6ULL 精密数据采集与实时控制系统

```
┌─────────────────────────────────────────────────┐
│              上位机 (PC)                         │
│  ┌──────────────────────────────────┐           │
│  │ Python GUI (Matplotlib 实时曲线)  │           │
│  │ TCP Client 连接 i.MX6ULL          │           │
│  └──────────────┬───────────────────┘           │
└─────────────────┼───────────────────────────────┘
                  │ TCP / 以太网
┌─────────────────┼───────────────────────────────┐
│  i.MX6ULL (PREEMPT_RT 内核)                      │
│                 │                                │
│  ┌──────────────▼──────────────────┐            │
│  │  用户态控制程序 (RT 优先级)       │            │
│  │  - mmap 映射驱动缓冲区，零拷贝读取│            │
│  │  - TCP epoll + 事件循环          │            │
│  │  - 命令解析与数据分发             │            │
│  └─┬────────────┬────────────┬─────┘            │
│    │ mmap       │ sysfs      │                  │
│    ▼            ▼            ▼                  │
│  ┌─────────┐ ┌──────────┐ ┌──────────────────┐ │
│  │ IIO 驱动 │ │ sysfs 接 │ │ 内核 RT 线程      │ │
│  │ SPI ADC  │ │ 口配参数  │ │ (SCHED_FIFO, 99) │ │
│  │ 传感器   │ │          │ │ 250μs 周期:      │ │
│  │         │ │          │ │  读 SPI ADC →     │ │
│  │         │ │          │ │  PID 计算 →       │ │
│  │         │ │          │ │  PWM 输出 →       │ │
│  │         │ │          │ │  写入 mmap 缓冲区  │ │
│  └─────────┘ └──────────┘ └──────────────────┘ │
│                                                  │
│  ┌──────────────────────────────┐               │
│  │ 硬件层                        │               │
│  │  SPI ADC (MCP3008 或类似)    │               │
│  │  → 电位器模拟传感器信号       │               │
│  │  PWM → LED / 小电机           │               │
│  │  逻辑分析仪 / 示波器验证       │               │
│  └──────────────────────────────┘               │
└─────────────────────────────────────────────────┘
```

### 项目中覆盖的技能点

| 技术点 | 对应能力 | 来自阶段 |
|--------|---------|----------|
| **IIO 子系统驱动** | SPI ADC 传感器的标准 Linux 抽象 | 第五、七阶段 |
| **PREEMPT_RT 实时内核** | 内核 RT 线程 + 250μs 硬实时周期 | 第八阶段 |
| **mmap + DMA 零拷贝** | 高频数据从内核到用户态的零开销路径 | 第四阶段 |
| **sysfs 接口** | PID 参数、采样率、触发阈值的运行时配置 | 第一阶段 |
| **TCP 服务器** | 以太网数据上报 + 远程指令控制 | 第九阶段 |
| **threaded IRQ** | GPIO 触发 → 线程化处理的标准工业模式 | 第三阶段 |
| **cyclictest 实时性验证** | 定量证明 RT 内核抖动 | 第八阶段 |
| **platform_driver** | 标准平台设备驱动模型 | 第二阶段 |

### 简历呈现

> **精密数据采集与实时控制系统**  
> *i.MX6ULL + PREEMPT_RT | 独立开发*  
> — 基于 IIO 子系统实现 SPI ADC 传感器驱动，支持扭矩/角度双通道同步采集  
> — 使用 mmap + DMA 零拷贝技术，4kHz 采样率下 CPU 占用率 < 5%  
> — PREEMPT_RT 内核下 RT 线程 jitter < 50μs，250μs 固定控制周期  
> — sysfs 接口支持 PID 参数运行时动态调整  
> — TCP 服务器实现远程数据上报与指令控制

---

## 时间分配总表

| 阶段 | 内容 | 计划周期 | 实际状态 | 核心产出 |
|------|------|----------|----------|----------|
| 1 | 字符驱动深度（并发/定时器/sysfs） | 2 周 | ✅ 完成 | 工业级字符驱动 |
| 2 | 平台设备模型 + 设备树 | 2 周 | ✅ 完成 | platform_driver 骨架 |
| 3 | 中断子系统 | 2 周 | ✅ 完成 | threaded IRQ 驱动 |
| 4 | 内存 & DMA | 2 周 | ✅ 完成 | mmap + DMA 零拷贝 |
| 5 | SPI 总线驱动框架 | 2 周 | ✅ 完成 | SPI ADC 传感器驱动 |
| 6 | I2C 总线驱动框架 | — | ✅ 完成 | I2C EEPROM 驱动 |
| 7 | PWM 输出驱动 | — | ✅ 完成 | PWM 0-100% 占空比控制 |
| 8 | SocketCAN 工业总线 | 1.5 周 | ⚠️ 部分 | 用户态 demo 已有 |
| 9 | IIO 子系统 | 1.5 周 | ⬜ 待开始 | **最重要的面试阶段** |
| 10 | PREEMPT_RT 实时内核 | 1.5 周 | ⬜ 待开始 | RT 内核 + cyclictest |
| 11 | 以太网驱动结构 | 1 周 | ⬜ 待开始 | fec 驱动调用链分析 |
| 12 | 综合项目 | 3 周 | ⬜ 待开始 | 完整闭环系统 |
| **合计** | | **≈ 18.5 周** | **7/12 完成** | |

---

## 每日学习节奏

```
┌────────────────────────────────────────────────────┐
│              建议每日安排（2 - 3 小时）              │
├────────────────────────────────────────────────────┤
│                                                    │
│  Day 1     学概念 + 手写核心代码框架（不复制粘贴）    │
│  Day 2     在 i.MX6ULL 上跑通，拿到实际输出          │
│  Day 3     写笔记记录 "关键理解" + "踩过的坑"         │
│            （这将成为面试时能脱口而出的内容）          │
│                                                    │
│  周末      把当周代码重构一遍                        │
│            git commit message 写清楚 "为什么改"      │
│            面试时 git log 就是你的成长证据           │
│                                                    │
└────────────────────────────────────────────────────┘
```

---

## Git 版本管理建议

### 实际 commit 历史

```bash
768390a [stage05-06] 重命名目录 + 新增 I2C 总线驱动 + PWM 预备
6efbccb [stage04-06] DMA mmap零拷贝驱动 + SPI总线驱动 + SocketCAN框架
cca6132 Add comprehensive README with tech stack documentation
f7543a3 Restore 笔记.docx
37fe2f4 Stage 2+3: platform_driver + interrupt (threaded IRQ + hrtimer)
e189871 Add study notes docx and update gitignore
ccdddf1 Initial commit: Leetx preparation project - Stage 1 Task 1
```

> **注意**：实际开发中未严格按每个阶段一个分支，所有工作都在 `master` 分支上。每个 commit 可能包含多个阶段的代码。

### 推荐规范（供参考）

```bash
# 每个阶段一个分支
git checkout -b stage01-char-driver-deep
git checkout -b stage02-platform-model
git checkout -b stage03-interrupts
# ...

# commit 规范
[stage03] threaded_irq: 测量延迟，jitter < 30μs
[stage04] mmap: 实现零拷贝，对比 read 吞吐量提升 5x
[stage05] spi: MCP3008 驱动，通过 IIO 框架注册成功

# 最终合并到主分支
git checkout main
git merge stage10-final-project
```

---

## 学习建议

1. **不要急着走完**。前六个阶段的每个驱动，都在 i.MX6ULL 上跑出结果、拿示波器/逻辑分析仪验证、写一篇笔记——这个质量远好于十个阶段全部"看过但没跑过"。

2. **优先跑通**。每个阶段的目标是"在 i.MX6ULL 上出结果"，不是"看完所有理论"。

3. **面试时你只有一个武器**：你亲手跑通的真实项目。你能讲出中断延迟数据、DMA 吞吐对比、RT 内核 jitter 直方图——这比你说"我了解 Linux 驱动"有力一百倍。

4. **第一阶段到第七阶段是基础**，已全部完成；第七阶段（IIO）是与 Leetx 最直接的对口技术，重点投入。

5. **综合项目是核心**。做完之后把这个项目写在简历最显眼的位置，它直接对标 Leetx 的技术栈。

---

> 📌 **核心原则**：只学你在 i.MX6ULL 上能跑通的。驱动开发不是靠"看过"学会的，是靠"中断不来→查寄存器→改设备树→重编译→跑通"这个循环踩出来的。
