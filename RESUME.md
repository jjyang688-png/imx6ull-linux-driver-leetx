# 简历项目描述

## 版本 A：招聘网站版（~120 字）

> **工业传感器数据采集与实时控制系统**（2026.06 – 2026.08，独立开发）
>
> 基于 i.MX6ULL + Linux 4.1.15，独立完成 7 个内核驱动模块。SPI ADC 4kHz 采集 + DMA mmap 零拷贝（CPU < 5%），设备树驱动 SPI/I2C/PWM/CAN 全外设，PREEMPT_RT 实时化（250μs 周期 / 抖动 < 50μs）。
>
> **技术栈**：C、Linux 内核、设备树、DMA、SPI、I2C、PWM、SocketCAN、PREEMPT_RT、ARM 交叉编译

---

## 版本 B：简历项目栏版（适合"项目经历"板块）

**工业传感器数据采集与实时控制系统**　|　2026.06 – 2026.08　|　独立开发

基于 i.MX6ULL (ARM Cortex-A7) + Linux 4.1.15，独立完成 7 个内核驱动模块，覆盖数据采集、外设驱动、实时内核与工业总线。

- **高速数据采集链路**：SPI ADC 以 4kHz 采集传感器数据（扭矩/压力），DMA 缓冲区通过 mmap 直接映射到用户空间，整条链路零拷贝、CPU 占用 < 5%

- **中断与定时器子系统**：GPIO 硬件中断触发采样，threaded IRQ 拆分上下半部（硬中断只计数，SPI 传输在进程上下文执行）；hrtimer 提供 250μs 固定周期备用触发

- **多总线外设驱动**：设备树描述硬件拓扑，platform/spi/i2c_driver 三套框架分别驱动 ADC、EEPROM、PWM 等外设，sysfs 暴露采样率等运行时参数

- **PREEMPT_RT 实时内核**：部署 PREEMPT_RT 补丁（中断线程化 / spinlock 可抢占），SCHED_FIFO 最高优先级 + CPU 隔离，cyclictest 验证 250μs 周期抖动 < 50μs

- **工业总线通信**：SocketCAN 实现 CAN 2.0 帧收发，移植 CANopen 协议栈支持 PDO/SDO 过程数据交换；PWM 驱动输出 0~100% 占空比控制执行器

**技术栈**：C、Linux 内核、设备树、DMA、SPI、I2C、PWM、SocketCAN、PREEMPT_RT、platform/spi/i2c_driver、ARM 交叉编译、i.MX6ULL

---

## 版本 C：面试口述版（~3 分钟）

> 我在 i.MX6ULL 上独立做了 7 个内核驱动模块，覆盖数据采集、外设驱动、实时化和工业总线。
>
> **数据采集**是核心。SPI ADC 4kHz 采样，DMA + mmap 零拷贝到用户空间，CPU < 5%。GPIO 中断 + threaded IRQ 触发，hrtimer 做 250μs 备用定时。
>
> **外设驱动**，platform/spi/i2c_driver 三套框架分别驱动 ADC、EEPROM、PWM，设备树描述硬件，一套代码多设备复用。
>
> **实时性**，PREEMPT_RT + SCHED_FIFO + CPU 隔离，cyclictest 验证 250μs 抖动 < 50μs。同步用 mutex/spinlock/atomic/waitqueue 按场景选型。
>
> **通信**，SocketCAN + CANopen 协议栈，PWM 0~100% 占空比控制执行器。
>
> JD 关键词全覆盖：Linux 驱动、SPI、I2C、PWM、DMA、中断、CAN、PREEMPT_RT、设备树。

---

## 关键词索引（JD 匹配）

| JD 关键词 | 项目对应 |
|-----------|---------|
| Linux 驱动开发 | 独立完成 7 个完整驱动（cdev / platform / SPI / I2C / PWM / DMA / 中断） |
| 设备树 | 独立编写 pinctrl + 设备节点，of_property_read_u32 解析硬件参数 |
| 中断处理 | threaded IRQ 拆分模式，hard_handler → IRQ_WAKE_THREAD → thread_fn |
| DMA | dma_alloc_coherent + CMA + dma_mmap_coherent 零拷贝 |
| SPI / I2C | 分别基于 spi_driver 和 i2c_driver 框架，真实硬件调通 |
| CAN / CANopen | SocketCAN 原始帧收发 + CANopenNode 协议栈 |
| 实时系统 | PREEMPT_RT 补丁 + cyclictest 抖动 < 50μs + CPU 隔离 + SCHED_FIFO |
| ARM / 交叉编译 | arm-linux-gnueabihf-gcc 5.4.0，TFTP + NFS 部署 |
