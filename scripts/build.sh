#!/bin/bash
# Stage 1 编译脚本 — 在 VM 上执行
# 用法: bash scripts/build.sh

set -e

PROJECT_DIR="/home/yang/linux/Industrial_Driver"
KERNEL_DIR="/home/yang/linux/alentek_uboot/linux/linux-imx-rel_imx_4.1.15_2.1.0_ga_alientek"

cd "$PROJECT_DIR"

echo "=== Cleaning ==="
make -C "$KERNEL_DIR" M="$PROJECT_DIR/kernel/stage01-char" clean 2>/dev/null || true

echo "=== Building kernel module ==="
make -C "$KERNEL_DIR" M="$PROJECT_DIR/kernel/stage01-char" ARCH=arm CROSS_COMPILE=arm-linux-gnueabihf- modules

echo "=== Building userspace test ==="
arm-linux-gnueabihf-gcc -o userspace/test_queue userspace/test_queue.c -Wall -O2 -pthread

echo ""
echo "=== Build complete ==="
echo "Module: kernel/stage01-char/leetx_queue.ko"
echo "Test:   userspace/test_queue"
ls -lh kernel/stage01-char/*.ko userspace/test_queue
