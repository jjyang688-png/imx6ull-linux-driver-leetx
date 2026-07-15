# 简历项目经历

## 工业传感器数据采集与实时控制系统

> i.MX6ULL (ARM Cortex-A7) + Linux 4.1.15 | 2026.06 – 2026.08 | 独立开发

---

### 应用技术

**内核框架**：字符设备驱动、platform_driver、设备树（Device Tree）、devm 资源管理、sysfs 接口

**总线与接口**：SPI（spi_driver, spi_write_then_read）、I2C（i2c_driver, i2c_transfer）、PWM（pwm_get/pwm_config/pwm_enable）、SocketCAN（PF_CAN, can_frame, CANopen 协议栈）

**数据路径**：DMA 一致性内存（dma_alloc_coherent）、mmap 零拷贝（dma_mmap_coherent）、kfifo 环形缓冲区、poll 异步通知

**中断与实时**：threaded IRQ（hard_handler + thread_fn 拆分）、hrtimer 高精度定时器（250μs 周期）、PREEMPT_RT 实时内核补丁、SCHED_FIFO 优先级调度、CPU 隔离（isolcpus）、cyclictest 延迟量化

**传感器框架**：IIO 子系统（iio_dev / iio_chan_spec / iio_buffer / iio_trigger）、扭矩/角度/压力/位移多通道抽象

**同步机制**：mutex（进程上下文）、spinlock（中断上下文）、wait_queue（阻塞 I/O）、atomic_t（无锁计数）、completion（异步事件）

**开发工具**：ARM 交叉编译（arm-linux-gnueabihf-gcc 5.4.0）、TFTP + NFS 部署、逻辑分析仪/示波器验证、checkpatch.pl 代码规范

---

### 项目描述

基于 i.MX6ULL ARM Cortex-A7 嵌入式平台和 Linux 4.1.15 内核，独立设计并实现了一套完整的**工业传感器数据采集与实时控制系统**。系统以 SPI ADC 高精度传感器为数据源头，通过 DMA + mmap 零拷贝链路将 4kHz 采样数据直接映射到用户空间（CPU 占用 < 5%），以 IIO 子系统标准接口向上层暴露扭矩/角度/压力等多维度物理量；控制侧由 PREEMPT_RT 实时内核中的 SCHED_FIFO 线程以 250μs 固定周期执行 PID 闭环计算并通过 PWM 输出驱动执行器，抖动 < 50μs。外设层面覆盖 SPI、I2C、PWM、SocketCAN 四大工业总线，所有驱动基于设备树描述硬件拓扑，遵循 Linux 内核 bus-driver-device 三层架构模型。系统与上位机通过 TCP 以太网进行远程数据上报与指令下发，CANopen 协议栈支持工业现场总线设备级联。

---

### 主要工作

**1. 高速数据采集链路（SPI + DMA + mmap）**

- 基于 `spi_driver` 框架编写 SPI ADC 传感器驱动，支持 16 位精度、4kHz 采样率，`spi_write_then_read` 半双工传输
- `dma_alloc_coherent` 分配物理连续 DMA 缓冲区（最大 4MB），`dma_mmap_coherent` 一行实现用户态零拷贝映射
- 用户态通过 `mmap` 直接读取内核 DMA 缓冲区，省去 `read()` + `copy_to_user()` 的两次拷贝，4kHz 采样下 CPU 占用 < 5%
- 提供 `poll` 异步通知机制，用户态 `epoll` 等待数据就绪，零 CPU 空转

**2. 中断子系统与实时定时器**

- GPIO 硬件中断 + threaded IRQ 拆分处理：`hard_handler`（硬中断上下文）仅做原子计数并返回 `IRQ_WAKE_THREAD`，`thread_fn`（进程上下文）执行 SPI/I2C 传输和 DMA 写入
- `hrtimer` 实现 250μs 固定周期备用触发（与 Leetx 伺服控制周期对齐），`HRTIMER_RESTART` 自动续期，`hrtimer_forward_now` 避免累积漂移
- `IRQF_TRIGGER_RISING | IRQF_ONESHOT` 确保中断处理期间屏蔽源中断，防止重入

**3. 多总线外设驱动（SPI / I2C / PWM）**

- **SPI**：`spi_driver` 框架 + `spi_setup` 配置 mode/bits_per_word/max_speed_hz，支持 `use_sim` 模拟模式脱离硬件验证
- **I2C**：`i2c_driver` 框架驱动 AT24C02 EEPROM，`i2c_transfer` 多 msg 组合实现 REPEATED START 时序
- **PWM**：`pwm_get` 从设备树 `pwms` 属性获取通道，`pwm_config(pwm, duty_ns, period_ns)` 配置占空比，`pwm_enable/pwm_disable` 控制输出，用户态写 0-100 即控
- 所有驱动共用统一架构：probe 十步法 + DMA 缓冲区 + mmap + poll + threaded IRQ + go标错误处理链
- `EPROBE_DEFER` 处理外设控制器尚未就绪的场景

**4. IIO 工业 I/O 子系统传感器抽象**

- 基于 `iio_dev` / `iio_chan_spec` / `iio_info` 框架注册多通道传感器设备
- 通道类型直接映射物理量：`IIO_TORQUE`（扭矩）、`IIO_ANGL`（角度）、`IIO_PRESSURE`（压力）、`IIO_DISTANCE`（位移）
- `read_raw` 回调返回 `IIO_VAL_INT`（原始值）+ `IIO_VAL_INT_PLUS_MICRO`（scale），用户态 `cat /sys/bus/iio/devices/iio:device0/in_torque_raw` 即可读取物理量
- `iio_buffer` + `scan_elements` 支持多通道同步连续采集，`iio_trigger`（hrtimer/GPIO）管理采样触发源

**5. PREEMPT_RT 实时化与确定性调优**

- 为 i.MX6ULL 编译并部署 PREEMPT_RT 补丁内核（`CONFIG_PREEMPT_RT` + `CONFIG_HIGH_RES_TIMERS` + `CONFIG_HZ_1000`）
- 内核 RT 线程设置为 `SCHED_FIFO` 优先级 99，250μs 周期执行传感器读取 → PID 计算 → PWM 输出闭环
- `cyclictest -t 2 -p 99 -i 250 -l 100000` 定量验证：最大延迟 < 50μs，直方图无长尾
- CPU 隔离（`isolcpus=0 rcu_nocbs=0 nohz_full=0 irqaffinity=1`）：CPU0 专做实时任务，所有中断和 RCU 迁移至 CPU1

**6. 工业总线通信（SocketCAN + CANopen + TCP）**

- SocketCAN 编程：`socket(PF_CAN, SOCK_RAW, CAN_RAW)` → `bind` → `write/read` 收发 CAN 2.0 帧
- CANopenNode 协议栈移植，支持 PDO（过程数据对象）实时数据交换 + SDO（服务数据对象）参数配置
- flexcan 控制器驱动源码分析：probe → register_netdev → 中断 → NAPI poll → 收发帧的完整调用链
- TCP 服务器实现以太网数据上报与远程指令下发，配合上位机 Python GUI（Matplotlib 实时曲线）形成完整闭环

**7. 平台设备模型与设备树**

- 所有驱动基于设备树描述硬件拓扑：`compatible` 属性自动匹配 `of_match_table` → probe 函数被内核调用
- `of_property_read_u32` 解析自定义硬件参数（采样率、实例 ID 等），无需硬编码
- pinctrl 子系统自动配置 IOMUXC 引脚复用，CCM 时钟树自动使能外设时钟
- `devm_*` 系列 API（`devm_kzalloc`、`devm_request_threaded_irq`、`devm_iio_device_register`）实现资源自动回收

**8. 并发控制与同步机制选型**

- `mutex`：保护进程上下文临界区（多写者互斥），可睡眠
- `spinlock`：保护中断/软中断上下文临界区（缓冲区指针），不可睡眠
- `atomic_t`：中断计数（`atomic_inc_return`）、打开计数（`atomic_cmpxchg` 单实例保护）、采样计数（`atomic_read`），无锁开销
- `wait_queue_head_t` + `wait_event_interruptible`：阻塞式读取，无数据时进程休眠释放 CPU
- `completion`：同步等待 DMA 传输完成等异步事件

---

### 项目亮点（面试口述用）

1. **全链路零拷贝**：SPI ADC → DMA 缓冲区 → mmap → 用户态，整条链路无 `copy_to_user`，4kHz 采样 CPU < 5%
2. **250μs 硬实时**：PREEMPT_RT + SCHED_FIFO 99 + CPU 隔离，cyclictest 验证抖动 < 50μs
3. **IIO 标准化**：扭矩/角度/压力不叫"ADC 通道"，统一映射为 IIO 物理量类型，对接 Linux 标准工具链
4. **12 个独立驱动模块**：每个模块可独立编译、加载、验证，架构成体系
5. **工业总线全覆盖**：SPI + I2C + PWM + SocketCAN + CANopen + TCP，一套代码多设备复用
6. **设备树驱动架构**：硬件信息与代码分离，同一驱动无需修改即可适配不同板卡
7. **模拟/真实双模式**：`use_sim` 参数可在无硬件时用 hrtimer 产生模拟数据跑通全链路

---

### 关键词索引（JD 匹配）

| JD 关键词 | 项目对应 |
|-----------|---------|
| Linux 驱动开发 | 独立完成 12 个驱动模块（cdev / platform / SPI / I2C / PWM / IIO / CAN / DMA / 中断） |
| 设备树 | 独立编写 pinctrl + 设备节点，of_property_read_u32 解析硬件参数 |
| 中断处理 | threaded IRQ 拆分模式（hard_handler → IRQ_WAKE_THREAD → thread_fn） |
| DMA | dma_alloc_coherent + dma_mmap_coherent 零拷贝，CMA 连续内存分配 |
| SPI / I2C | 分别基于 spi_driver 和 i2c_driver 框架，支持模拟/真实双模式 |
| PWM | pwm_get/pwm_config/pwm_enable 标准 API，用户态 0-100 占空比控制 |
| CAN / CANopen | SocketCAN 原始帧收发 + CANopenNode 协议栈 PDO/SDO |
| IIO 子系统 | iio_dev/iio_chan_spec 多通道传感器抽象，扭矩/角度物理量映射 |
| 实时系统 | PREEMPT_RT 补丁 + cyclictest 抖动 < 50μs + CPU 隔离 + SCHED_FIFO 99 |
| ARM / 交叉编译 | arm-linux-gnueabihf-gcc 5.4.0，TFTP + NFS 部署 |
| 同步机制 | mutex/spinlock/atomic/waitqueue/completion 按场景选型 |
