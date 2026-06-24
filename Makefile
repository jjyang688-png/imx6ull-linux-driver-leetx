# Stage 1, Task 1: 工业级阻塞式数据队列
#
# 对标 Leetx 场景：扭矩传感器数据采集管道
# - kfifo 高效环形缓冲区
# - wait_queue 阻塞读取（无数据时休眠，不空转 CPU）
# - mutex 保护多进程并发写
#
# 用法：
#   make                    # 编译模块
#   make test               # 编译用户态测试程序
#   make deploy             # 部署到开发板 (TFTP)
#   make verify             # 运行验证脚本

# === 内核源码路径（正点原子 i.MX6ULL） ===
KERNEL_DIR ?= /home/yang/linux/alentek_uboot/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek

# === 交叉编译工具链 ===
ARCH ?= arm
CROSS_COMPILE ?= arm-linux-gnueabihf-

# === 目标平台 IP（开发板） ===
BOARD_IP ?= 192.168.80.200
TFTP_DIR ?= /home/yang/linux/tftpboot

# === 内核模块目标 ===
obj-m += leetx_queue.o

# 模块源码路径
MODULE_DIR := kernel/stage01-char

all: module test

module:
	@echo "=== Building leetx_queue.ko ==="
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD)/$(MODULE_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) modules

test: userspace/test_queue.c
	@echo "=== Building test_queue (userspace) ==="
	$(CROSS_COMPILE)gcc -o userspace/test_queue userspace/test_queue.c -Wall -O2 -pthread

deploy: module test
	@echo "=== Deploying to $(BOARD_IP) ==="
	cp $(MODULE_DIR)/leetx_queue.ko $(TFTP_DIR)/
	cp userspace/test_queue $(TFTP_DIR)/
	@echo "Run on board: tftp -g -r leetx_queue.ko -l /root/leetx_queue.ko $(shell hostname -I | awk '{print $$1}')"
	@echo "Then: insmod /root/leetx_queue.ko && /root/test_queue"

clean:
	$(MAKE) -C $(KERNEL_DIR) M=$(PWD)/$(MODULE_DIR) ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) clean
	rm -f userspace/test_queue

.PHONY: all module test deploy clean
