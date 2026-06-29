# i.MX6ULL Linux 驱动开发学习项目

> **目标岗位**：砺星工业科技（Leetx）— Linux 驱动开发工程师 / 嵌入式软件工程师  
> **学习平台**：正点原子 i.MX6ULL Alpha 开发板（NXP Cortex-A7）  
> **内核版本**：Linux 4.1.15  
> **交叉编译器**：arm-linux-gnueabihf-gcc 5.4.0 (Linaro)

---

## 项目背景

[砺星工业科技（上海）有限公司](http://www.leetx.com) 是国内高端智能装配领域的领先企业，专注于伺服拧紧系统、伺服压装系统、自动送钉系统、涂胶系统等工业装配设备的自主研发。其产品广泛应用于新能源汽车、航空航天、轨道交通等领域。

本项目的目标是通过循序渐进的 10 个实战阶段，掌握该领域 Linux 驱动开发所需的全部核心技术栈。

---

## 技术栈全景

### 内核框架

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **字符设备驱动** | cdev、file_operations、设备号注册 | Stage 1 |
| **平台设备驱动模型** | platform_driver、probe/remove、device tree 匹配 | Stage 2-10 |
| **设备树 (Device Tree)** | compatible 匹配、of_property_read_*、pinctrl | Stage 2-10 |
| **devm 资源管理** | devm_kzalloc、devm_request_irq 等自动生命周期管理 | Stage 2-10 |

### 中断与实时性

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **中断子系统** | request_irq、request_threaded_irq、硬中断/底半部 | Stage 3 |
| **threaded IRQ** | hard_handler + thread_fn 拆分式中断处理 | Stage 3 |
| **hrtimer 高精度定时器** | 纳秒级精度、周期性触发、HRTIMER_RESTART 自动续期 | Stage 3 |
| **PREEMPT_RT 实时内核** | Linux RT 补丁、cyclictest 延迟测量、CPU 隔离 | Stage 8 |
| **tasklet / workqueue** | 软中断下半部、进程上下文延迟任务 | Stage 3 |

### 内存与 DMA

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **kfifo 环形缓冲区** | DECLARE_KFIFO_PTR、kfifo_alloc、kfifo_in/kfifo_out | Stage 1-10 |
| **DMA 一致性内存** | dma_alloc_coherent、dma_mmap_coherent、物理连续内存 | Stage 4 |
| **mmap 零拷贝** | 用户态直接映射内核 DMA 缓冲区，省去 copy_to_user | Stage 4 |
| **CMA 连续内存分配器** | reserved-memory 设备树配置 | Stage 4 |
| **内核内存管理** | kmalloc/vmalloc、GFP_KERNEL/GFP_ATOMIC、页表映射 | Stage 4 |

### 同步与并发

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **mutex 互斥锁** | 进程上下文保护、可睡眠 | Stage 1-10 |
| **spinlock 自旋锁** | 中断上下文保护、不可睡眠 | Stage 3 |
| **wait_queue 等待队列** | 阻塞式 I/O、wait_event_interruptible | Stage 1-10 |
| **atomic 原子操作** | atomic_cmpxchg、atomic_inc_return | Stage 1-10 |
| **completion 完成量** | 等待异步事件完成 | Stage 3 |

### 工业总线

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **SPI 总线驱动** | spi_device、spi_driver、spi_write_then_read | Stage 5 |
| **I2C 总线驱动** | i2c_client、i2c_driver、SMBus 协议 | Stage 5 |
| **SocketCAN** | PF_CAN、can_frame、flexcan 控制器驱动分析 | Stage 6 |
| **CANopen** | CANopenNode 协议栈移植、PDO/SDO、对象字典 | Stage 6 |
| **Modbus TCP/RTU** | libmodbus 移植、工业现场总线 | Stage 6 |

### 传感器框架

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **IIO 子系统** | iio_dev、iio_chan_spec、标准 sysfs 接口 | Stage 7 |
| **IIO 缓冲区** | iio_buffer、scan_elements、连续采集 | Stage 7 |
| **IIO Trigger** | hrtimer trigger、GPIO trigger、周期性触发 | Stage 7 |

### 网络

| 技术 | 说明 | 涉及阶段 |
|------|------|----------|
| **以太网驱动结构** | fec 驱动分析、NAPI 收包、net_device | Stage 9 |
| **TCP 服务器** | 上位机通信、数据上报 | Stage 10 |

### 开发工具链

| 工具 | 说明 |
|------|------|
| **ARM 交叉编译** | arm-linux-gnueabihf-gcc、内核模块编译 |
| **设备树编译** | dtc、DTS/DTSI 语法 |
| **TFTP 部署** | tftpd-hpa、网络启动 |
| **逻辑分析仪** | SPI/I2C/CAN 总线波形抓取与分析 |
| **示波器** | 中断延迟测量、PWM 波形验证 |
| **cyclictest** | RT 内核实时性量化测试 |
| **checkpatch.pl** | 内核代码风格检查 |
| **sparse** | 静态代码分析 |

---

## 学习路线

```
Stage 1  ████████░░░░░░░░░░  基础字符驱动（kfifo + 阻塞读写）
Stage 2  ████████████░░░░░░  平台设备驱动模型（platform_driver + 设备树）
Stage 3  ████████████████░░  中断子系统（threaded IRQ + hrtimer）
Stage 4  ██████████████████  DMA + mmap 零拷贝
Stage 5  ██████████████████  SPI/I2C 总线驱动框架
Stage 6  ██████████████████  SocketCAN 工业总线
Stage 7  ██████████████████  IIO 工业 I/O 子系统
Stage 8  ██████████████████  PREEMPT_RT 实时内核
Stage 9  ██████████████████  以太网驱动结构分析
Stage 10 ██████████████████  综合实战项目（精密数据采集与实时控制系统）
```

---

## 已完成阶段

### Stage 1：基础字符设备驱动（`kernel/stage01-char/`）

**文件**：`leetx_queue.c`、`userspace/test_queue.c`

**学习内容**：

- `cdev` + `file_operations` 注册字符设备
- `kfifo` 环形缓冲区（`DECLARE_KFIFO_PTR`、`kfifo_alloc`、`kfifo_in`、`kfifo_out`）
- `wait_queue_head_t` + `wait_event_interruptible` 阻塞式读取
- `mutex` 保护多写者并发
- `atomic_cmpxchg` 单实例保护
- `module_param` 模块参数（`fifo_size`）
- `class_create` + `device_create` 自动创建设备节点

**验证方式**：

```bash
insmod leetx_queue.ko
./test_queue -c 10 -d 1000    # 写 10 个采样，间隔 1ms
```

### Stage 2：平台设备驱动模型（`kernel/stage02-platform/`）

**文件**：`leetx_sensor.c`

**学习内容**：

- `platform_driver` + `platform_device` 框架
- `of_device_id` 匹配表 + `MODULE_DEVICE_TABLE(of, ...)`
- `of_property_read_u32` 从设备树读取硬件参数
- `devm_kzalloc` 自动生命周期管理
- `platform_set_drvdata` / `platform_get_drvdata` 替代全局变量
- `container_of` 从 cdev 反推设备结构体
- `module_platform_driver` 一行注册 init/exit

### Stage 3：中断子系统（`kernel/stage03-interrupt/`）

**文件**：`leetx_irq.c`

**学习内容**：

- GPIO 中断注册全流程：`of_get_named_gpio` → `gpio_request` → `gpio_direction_input` → `gpio_to_irq` → `request_threaded_irq`
- threaded IRQ：`hard_handler`（硬中断，返回 `IRQ_WAKE_THREAD`）+ `thread_fn`（进程上下文）
- `IRQF_TRIGGER_RISING | IRQF_ONESHOT` 标志位
- `hrtimer` 模拟中断：init 在 probe、start 在 open、cancel 在 release
- `atomic_inc_return` / `atomic_read` 中断计数器

**当前进行中**：Stage 4（DMA + mmap 零拷贝）

---

## 开发环境

| 项目 | 详情 |
|------|------|
| **虚拟机** | Ubuntu 16.04 @ 192.168.80.106 |
| **用户名** | `yang` |
| **交叉编译器** | `arm-linux-gnueabihf-gcc` 5.4.0 (Linaro) |
| **内核源码** | `/home/yang/linux/alentek_uboot/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek` |
| **项目路径（Linux）** | `/home/yang/linux/Industrial_Driver/` |
| **项目路径（Windows）** | `Z:\Industrial_Driver\`（Samba 映射） |
| **TFTP 目录** | `/home/yang/linux/tftpboot/` |
| **NFS 目录** | `/home/yang/linux/nfs/` |

---

## 项目结构

```
Industrial_Driver/
├── README.md                          ← 本文件
├── CLAUDE.md                          ← 项目上下文与决策记录
├── Linux驱动开发学习计划.md             ← 完整 10 阶段学习计划
├── Makefile                           ← 顶层 Makefile
├── .gitignore
│
├── kernel/                            ← 内核模块源码
│   ├── stage01-char/                  ← 字符设备驱动
│   │   ├── Makefile
│   │   └── leetx_queue.c
│   ├── stage02-platform/              ← 平台设备驱动
│   │   ├── Makefile
│   │   └── leetx_sensor.c
│   ├── stage03-interrupt/             ← 中断子系统
│   │   ├── Makefile
│   │   └── leetx_irq.c
│   ├── stage04-dma/                   ← DMA + mmap（进行中）
│   ├── stage05-bus/                   ← SPI/I2C（待开始）
│   ├── stage06-can/                   ← SocketCAN（待开始）
│   ├── stage07-iio/                   ← IIO 子系统（待开始）
│   ├── stage08-rt/                    ← PREEMPT_RT（待开始）
│   └── stage10-final/                 ← 综合项目（待开始）
│
├── userspace/                         ← 用户态测试程序
│   └── test_queue.c
│
├── scripts/                           ← 编译/部署/验证脚本
│   ├── build.sh
│   └── deploy.sh
│
├── dts/                               ← 设备树片段
│
└── notes/                             ← 学习笔记
```

---

## 编译与部署

```bash
# 在 VM 上编译（Stage 1 示例）
cd /home/yang/linux/Industrial_Driver
make -C <内核源码路径> M=kernel/stage01-char ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules
arm-linux-gnueabihf-gcc -o userspace/test_queue userspace/test_queue.c -Wall -O2 -pthread

# 部署到开发板
cp kernel/stage01-char/leetx_queue.ko /home/yang/linux/tftpboot/
cp userspace/test_queue /home/yang/linux/tftpboot/

# 在开发板上
tftp -g -r leetx_queue.ko 192.168.80.106
tftp -g -r test_queue 192.168.80.106
insmod leetx_queue.ko
./test_queue -c 10 -d 1000
```

---

## 硬件准备

| 设备 | 用途 | 涉及阶段 |
|------|------|----------|
| i.MX6ULL 开发板 | 主平台 | Stage 1-10 |
| USB-TTL 串口模块 | 调试串口 | Stage 1-10 |
| 杜邦线 | GPIO 中断回环测试 | Stage 3 |
| 逻辑分析仪（推荐） | SPI/I2C/CAN 波形分析 | Stage 5-6 |
| SPI ADC 模块（MCP3008 等） | SPI 驱动实战 | Stage 5 |
| CAN 收发器模块（SN65HVD230） | CAN 总线通信 | Stage 6 |
| 小直流电机 + L298N 驱动 | PID 闭环控制 | Stage 10 |

---

## License

GPL-2.0 — 与 Linux 内核许可证保持一致
