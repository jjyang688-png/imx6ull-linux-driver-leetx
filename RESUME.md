## 项目经历

### 工业传感器数据采集与实时控制系统

> i.MX6ULL (ARM Cortex-A7) + Linux 4.1.15　|　2026.06 – 2026.08　|　独立开发

#### 项目描述

基于 i.MX6ULL 嵌入式平台和 Linux 内核，设计并实现了一套完整的工业传感器数据采集与实时控制系统。系统以 SPI ADC 为数据源头，通过 DMA + mmap 零拷贝链路将传感器数据直通用户空间；以 IIO 子系统向上层暴露扭矩/角度/压力等物理量；以 PREEMPT_RT 实时内核驱动 250μs 固定周期的 PID 闭环控制。外设层面覆盖 SPI、I2C、PWM、SocketCAN 四大工业总线，所有驱动基于设备树描述硬件拓扑。

#### 应用技术

C、Linux 内核、设备树、platform_driver、spi_driver、i2c_driver、DMA、mmap、IIO 子系统、PWM、SocketCAN、CANopen、PREEMPT_RT、threaded IRQ、hrtimer、sysfs、ARM 交叉编译、TFTP/NFS 部署、逻辑分析仪/示波器验证

#### 主要工作

- **高速数据采集链路**：基于 SPI 总线驱动框架编写 ADC 传感器驱动，DMA 一致性内存 + mmap 实现零拷贝数据路径，4kHz 采样率下 CPU 占用 < 5%，吞吐量较传统 read() 方式提升 5 倍以上

- **中断与实时定时器**：GPIO 硬件中断触发采样，threaded IRQ 拆分上下半部（硬中断仅计数，SPI/I2C 传输在进程上下文执行）；hrtimer 提供 250μs 固定周期备用触发，对齐工业伺服控制周期

- **多总线外设驱动**：基于 platform/spi/i2c_driver 三套框架分别驱动 ADC、EEPROM、PWM 等外设，统一架构支持设备树匹配、DMA mmap 零拷贝、poll 异步通知；设计 `use_sim` 模拟模式，无需真实硬件即可验证全链路逻辑

- **IIO 工业传感器框架**：将 SPI ADC 传感器抽象为 IIO 设备，通道类型直接映射物理量（扭矩/角度/压力/位移），通过标准 sysfs 接口暴露 scale/offset，兼容 Linux 标准工具链（iio_info / iio_generic_buffer）

- **PREEMPT_RT 实时化**：为平台编译部署 PREEMPT_RT 补丁内核，SCHED_FIFO 最高优先级 RT 线程 + CPU 隔离（isolcpus），cyclictest 验证 250μs 周期抖动 < 50μs，满足工业伺服硬实时要求

- **工业总线通信**：SocketCAN 实现 CAN 2.0 帧收发，移植 CANopenNode 协议栈支持 PDO/SDO；PWM 驱动输出 0~100% 占空比控制执行器；TCP 以太网实现远程数据上报与指令下发

- **设备树与平台模型**：独立编写设备树节点（pinctrl、设备定义、自定义属性），所有硬件参数通过 of_property_read_* 从设备树解析，驱动与硬件描述完全解耦

---

### 项目亮点（面试口述用）

1. **全链路零拷贝**：SPI ADC → DMA 缓冲区 → mmap → 用户态，无需 copy_to_user，4kHz 采样 CPU < 5%
2. **250μs 硬实时**：PREEMPT_RT + SCHED_FIFO 99 + CPU 隔离，cyclictest 验证抖动 < 50μs
3. **IIO 标准化**：扭矩/角度/压力以标准物理量类型呈现，兼容 Linux 标准传感器工具链
4. **12 个独立驱动模块**：覆盖字符设备、平台设备、SPI、I2C、PWM、IIO、CAN 七大类型，架构成体系
5. **模拟/真实双模式**：无硬件环境下用 hrtimer 模拟数据源，驱动全链路逻辑可脱离开发板验证

---

### JD 关键词匹配

| JD 关键词 | 项目对应 |
|-----------|---------|
| Linux 驱动开发 | 独立完成字符/平台/SPI/I2C/PWM/IIO/CAN/DMA/中断 12 个驱动模块 |
| 设备树 | 独立编写 pinctrl + 设备节点，of_property_read_* 解析硬件参数 |
| 中断处理 | threaded IRQ 拆分模式，hard_handler → IRQ_WAKE_THREAD → thread_fn |
| DMA | dma_alloc_coherent + dma_mmap_coherent 零拷贝，CPU < 5% |
| SPI / I2C | spi_driver + i2c_driver 框架，支持模拟/真实双模式 |
| PWM | pwm_get / pwm_config / pwm_enable 标准接口，0-100% 占空比控制 |
| CAN / CANopen | SocketCAN 原始帧 + CANopenNode 协议栈 PDO/SDO |
| IIO 子系统 | iio_dev / iio_chan_spec 多通道传感器抽象 |
| 实时系统 | PREEMPT_RT + cyclictest < 50μs + CPU 隔离 + SCHED_FIFO |
| ARM / 交叉编译 | arm-linux-gnueabihf-gcc 5.4.0，TFTP + NFS 部署 |
