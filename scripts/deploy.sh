#!/bin/bash
# 部署到开发板 — 在 VM 上执行
# 用法: bash scripts/deploy.sh [board_ip]

PROJECT_DIR="/home/yang/linux/Industrial_Driver"
TFTP_DIR="/home/yang/linux/tftpboot"
BOARD_IP="${1:-192.168.80.200}"

MODULE="$PROJECT_DIR/kernel/stage01-char/leetx_queue.ko"
TEST="$PROJECT_DIR/userspace/test_queue"

echo "=== Copying to TFTP ==="
cp "$MODULE" "$TFTP_DIR/" 2>/dev/null && echo "  leetx_queue.ko → tftpboot" || echo "  Module not built yet"
cp "$TEST" "$TFTP_DIR/" 2>/dev/null && echo "  test_queue     → tftpboot" || echo "  Test not built yet"

echo ""
echo "=== Instructions ==="
echo "On the i.MX6ULL board, run:"
echo ""
echo "  # Download"
echo "  tftp -g -r leetx_queue.ko -l /root/leetx_queue.ko 192.168.80.106"
echo "  tftp -g -r test_queue -l /root/test_queue 192.168.80.106"
echo ""
echo "  # Load and test"
echo "  insmod /root/leetx_queue.ko"
echo "  chmod +x /root/test_queue && /root/test_queue"
echo ""
echo "  # Clean up"
echo "  rmmod leetx_queue"
echo "  dmesg | tail -20"
