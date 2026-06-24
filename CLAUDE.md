# CLAUDE.md

## 项目概述

本项目是为准备 **砺星工业科技（Leetx）** Linux 驱动开发工程师岗位面试而建立的学习仓库。

- **目标公司**：砺星工业科技（上海）有限公司（[www.leetx.com](http://www.leetx.com)）
- **目标岗位**：嵌入式软件工程师 / Linux 驱动开发工程师
- **目标领域**：工业伺服系统（拧紧工具、伺服压机、涂胶系统、送钉系统）
- **学习平台**：i.MX6ULL 开发板（NXP Cortex-A7）
- **总学习周期**：约 18-20 周

## 目标公司背景

砺星（Leetx）是上海松江的高端智能装配设备公司，产品线覆盖：

- 传感器式拧紧系统（扭矩精度 ±5%/6σ，范围 0.3-500 N·m）
- 伺服压装 & 旋铆系统（控制周期 250μs，位移精度 < 0.01mm）
- 自动送钉系统、涂胶系统、智能分析系统

核心技术特征：**全栈自主研发**，从伺服电机、传感器、控制器硬件到嵌入式软件、上位机软件全部自研。

## 当前技能水平

- ✅ 基础字符设备驱动（open/read/write/ioctl）
- ✅ 常见总线协议概念（I2C、SPI、UART）
- ⬜ 平台设备驱动模型（platform_driver / 设备树）
- ⬜ 中断子系统（threaded IRQ / tasklet / workqueue）
- ⬜ PREEMPT_RT 实时内核
- ⬜ IIO 子系统
- ⬜ SocketCAN / CANopen
- ⬜ mmap + DMA 零拷贝

## 关键决策记录

1. **聚焦 Linux 驱动开发**：不学 STM32 裸机开发和 RTOS，全部精力放在 Linux 驱动方向
2. **不学 MCU 控制算法**：FOC/PID 等电机控制算法仅了解概念，不深入实现
3. **硬件知识策略**：掌握原理图阅读 + 示波器/逻辑分析仪使用，不需要硬件设计能力
4. **学习原则**：每个阶段必须在 i.MX6ULL 上跑出实际结果，不只看书不实践

## 核心文件

- `Linux驱动开发学习计划.md` — 完整的 10 阶段学习计划，包含每个阶段的代码骨架、验收标准、时间安排

## 项目结构约定

- 学习笔记和验证脚本放在各阶段子目录下
- 每个阶段用 Git 分支管理
- commit message 格式：`[stageXX] 简短描述`

## 开发环境

| 项目 | 详情 |
|------|------|
| 虚拟机 | Ubuntu 16.04 @ 192.168.80.106（用户名 `yang`，密码 `11ji`） |
| 工具链 | `arm-linux-gnueabihf-gcc` 5.4.0（Linaro） |
| 内核源码 | `/home/yang/linux/alentek_uboot/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek` |
| 内核版本 | Linux 4.1.15（正点原子官方适配） |
| 项目路径 | Linux: `/home/yang/linux/Industrial_Driver/` ⬄ Windows: `Z:\Industrial_Driver\`（Samba） |
| TFTP | `/home/yang/linux/tftpboot/`（`tftpd-hpa`） |
| NFS | `/home/yang/linux/nfs/` |
| 内核模块编译 | KERNEL_DIR 指向正点原子内核源码 |

## 与我交互的注意事项

- 所有技术讨论以 i.MX6ULL / Linux 4.1.15 平台为前提
- 驱动代码示例使用正点原子 i.MX6ULL Alpha 的设备树和寄存器
- 学习计划进展更新时，同步更新本文件中的技能水平状态
- 面试准备阶段需要重点练习：IIO 子系统、PREEMPT_RT 实时性论证、SocketCAN 编程
- 代码在 Windows VS Code 编辑，通过 Samba 同步到 VM，在 VM 编译，TFTP 传输到开发板
