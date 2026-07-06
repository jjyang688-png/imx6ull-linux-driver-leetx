/*
 * test_spi_adc.c — Stage 5 SPI ADC 用户态测试程序
 *
 * 用法：
 *   arm-linux-gnueabihf-gcc -o test_spi_adc test_spi_adc.c -Wall -O2
 *   ./test_spi_adc                    # 默认 /dev/leetx_spi0, 1MB, 打印 10 个采样
 *   ./test_spi_adc -d /dev/leetx_spi0 -n 100 -t 5000
 *
 * 工作方式：
 *   open → mmap → poll 等待数据 → 直接从 mmap 内存读 → 打印
 *
 * 零拷贝体现在哪？
 *   没有 read() 调用。数据从内核 DMA 缓冲区直接到用户态指针，
 *   CPU 不参与拷贝，只执行一次页表映射。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/poll.h>
#include <signal.h>
#include <errno.h>

static volatile int keep_running = 1;

void sigint_handler(int sig) { keep_running = 0; }

int main(int argc, char *argv[])
{
    const char *dev_path = "/dev/leetx_spi0";
    int buf_size = 1024 * 1024;    /* 1MB，和驱动默认值一致 */
    int max_samples = 10;
    int timeout_ms = 5000;
    int fd;
    u_int32_t *dma_map;
    u_int32_t read_pos = 0;
    int count = 0;
    int ret;

    /* 解析命令行 */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-d") && i + 1 < argc)
            dev_path = argv[++i];
        else if (!strcmp(argv[i], "-s") && i + 1 < argc)
            buf_size = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-n") && i + 1 < argc)
            max_samples = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-t") && i + 1 < argc)
            timeout_ms = atoi(argv[++i]);
        else {
            fprintf(stderr, "用法: %s [-d <dev>] [-s <size>] [-n <samples>] [-t <timeout_ms>]\n",
                    argv[0]);
            return 1;
        }
    }

    /* ===== 第 1 步：打开设备 ===== */
    fd = open(dev_path, O_RDWR);
    if (fd < 0) {
        perror("open");
        fprintf(stderr, "请先 insmod leetx_spi_adc.ko use_sim=1\n");
        return 1;
    }
    printf("[open] %s 打开成功, fd=%d\n", dev_path, fd);

    /* ===== 第 2 步：mmap 映射 DMA 缓冲区 =====
     *
     * 这一步完成后，dma_map[i] 直接访问内核 DMA 缓冲区第 i 个元素。
     * 不需要 read()，不需要 copy_to_user，零拷贝。
     */
    dma_map = mmap(NULL, buf_size, PROT_READ, MAP_SHARED, fd, 0);
    if (dma_map == MAP_FAILED) {
        perror("mmap");
        close(fd);
        return 1;
    }
    printf("[mmap] DMA 缓冲区映射完成, 虚拟地址=%p, 大小=%d 字节\n",
           (void *)dma_map, buf_size);

    /*
     * 计算 DMA 缓冲区最多能存多少个 u32 采样。
     * 和驱动里 pos = (pos + 1) % (dma_buf_size / sizeof(u32)) 对应。
     */
    u_int32_t max_pos = buf_size / sizeof(u_int32_t);
    printf("[信息] 缓冲区容量: %u 个采样 (u32)\n", max_pos);

    /* ===== 第 3 步：poll + mmap 循环读数据 ===== */
    signal(SIGINT, sigint_handler);
    printf("\n开始接收数据 (按 Ctrl+C 停止)...\n\n");

    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;          /* 关心"有数据可读" */

    while (keep_running && count < max_samples) {
        /*
         * poll：阻塞等待，直到内核 wake_up_interruptible 叫醒。
         *
         * 内核路径：
         *   poll() → leetx_spi_poll → poll_wait(挂号) → 睡觉
         *   hrtimer 每 250μs → dma_buf[pos]=val → wake_up → poll 醒来
         *   → leetx_spi_poll 重新检查 total_samples > 0 → 返回 POLLIN
         */
        ret = poll(&pfd, 1, timeout_ms);
        if (ret < 0) {
            if (errno == EINTR)
                break;
            perror("poll");
            break;
        }
        if (ret == 0) {
            printf("[poll] 超时 (%d ms)，无数据\n", timeout_ms);
            break;
        }

        if (!(pfd.revents & POLLIN))
            continue;

        /*
         * poll 返回了 → 驱动说有新数据
         *
         * 但具体有多少新数据？驱动不告诉我们。
         * 我们通过 write_pos 和 read_pos 自己判断。
         *
         * 注意：这里简化处理——每次 poll 返回只读一个采样。
         * 实际产品里可以批量读（read_pos 到 write_pos 之间的全部）。
         */
        u_int32_t val = dma_map[read_pos];       /* 直接读！零拷贝！ */
        read_pos = (read_pos + 1) % max_pos;     /* 和驱动一样的回绕逻辑 */

        printf("[采样 %4d] write_pos=%-6u 值=%u\n",
               count + 1, read_pos, val);
        count++;
    }

    /* ===== 第 4 步：统计 ===== */
    printf("\n--- 统计 ---\n");
    printf("共接收 %d 个采样\n", count);
    printf("最后一个采样: dma_map[%u] = %u\n",
           read_pos > 0 ? read_pos - 1 : max_pos - 1,
           dma_map[read_pos > 0 ? read_pos - 1 : max_pos - 1]);

    /* ===== 第 5 步：清理 ===== */
    munmap(dma_map, buf_size);
    close(fd);
    printf("[清理] mmap 解除映射, 设备已关闭\n");

    return 0;
}
