/**
 * Stage 1, Task 1: 用户态测试程序
 *
 * 测试 /dev/leetx_queue 驱动：
 *   - 启动一个 writer 线程，定时写入模拟传感器数据
 *   - 主线程阻塞读取数据并打印
 *   - 验证数据完整性（无丢失、无乱序）
 *
 * 编译（在 VM 上）：
 *   arm-linux-gnueabihf-gcc -o test_queue test_queue.c -Wall -O2 -pthread
 *
 * 使用（在 i.MX6ULL 上）：
 *   ./test_queue                             # 默认 10 个采样
 *   ./test_queue -c 100 -d 250               # 100 个采样，间隔 250μs
 *   ./test_queue -n                          # 非阻塞模式测试
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <errno.h>
#include <sys/time.h>

#define DEVICE_PATH     "/dev/leetx_queue"
#define DEFAULT_COUNT   10
#define DEFAULT_DELAY   100000      /* 100ms */

/* 全局参数 */
static int g_count = DEFAULT_COUNT;
static int g_delay_us = DEFAULT_DELAY;
static int g_nonblock = 0;
static int g_fd = -1;

/* ================================================================
 * Writer 线程：定时写入模拟传感器数据
 * ================================================================ */

static void *writer_thread(void *arg)
{
    uint32_t val;
    int ret;
    int i;

    printf("[writer] starting: count=%d, interval=%d us\n",
           g_count, g_delay_us);

    for (i = 0; i < g_count; i++) {
        val = (uint32_t)i;              /* 采样值 = 序号 */
        ret = write(g_fd, &val, sizeof(val));
        if (ret != sizeof(val)) {
            fprintf(stderr, "[writer] write error at sample %d: ret=%d, errno=%d (%s)\n",
                    i, ret, errno, strerror(errno));
            break;
        }

        printf("[writer] wrote sample %d: value=%u\n", i, val);

        if (g_delay_us > 0)
            usleep(g_delay_us);
    }

    printf("[writer] finished: %d samples written\n", i);
    return NULL;
}

/* ================================================================
 * Reader 主线程：阻塞读取数据
 * ================================================================ */

static int reader_loop(void)
{
    uint32_t val;
    uint32_t expected = 0;
    int ret;
    int lost = 0;
    int received = 0;
    struct timeval tv;

    printf("[reader] starting (blocking mode%s)\n",
           g_nonblock ? ", O_NONBLOCK" : "");

    while (1) {
        ret = read(g_fd, &val, sizeof(val));
        if (ret == sizeof(val)) {
            gettimeofday(&tv, NULL);

            printf("[reader] #%-4d: value=%u @ %ld.%06ld",
                   received, val, tv.tv_sec, tv.tv_usec);

            /* 验证数据完整性 */
            if (val != expected) {
                printf(" *** MISMATCH (expected %u, got %u) ***",
                       expected, val);
                lost++;
            }
            printf("\n");

            expected = val + 1;
            received++;
        } else if (ret == 0) {
            printf("[reader] EOF, total received=%d, lost=%d\n",
                   received, lost);
            break;
        } else if (ret < 0) {
            if (errno == EAGAIN) {
                /* 非阻塞模式下没有数据可用 */
                printf("[reader] no data available (EAGAIN)\n");
                break;
            } else if (errno == EINTR) {
                /* 被信号中断，继续等待 */
                printf("[reader] interrupted, retrying...\n");
                continue;
            } else {
                fprintf(stderr, "[reader] read error: ret=%d, errno=%d (%s)\n",
                        ret, errno, strerror(errno));
                break;
            }
        }
    }

    printf("[reader] summary: received=%d, lost=%d, expected=%u\n",
           received, lost, expected);
    return (lost == 0) ? 0 : 1;
}

/* ================================================================
 * Main
 * ================================================================ */

static void usage(const char *prog)
{
    fprintf(stderr,
            "Usage: %s [-c count] [-d delay_us] [-n]\n"
            "  -c COUNT    number of samples (default: 10)\n"
            "  -d DELAY    interval in us (default: 100000, 0=fastest)\n"
            "  -n          non-blocking mode\n",
            prog);
}

int main(int argc, char *argv[])
{
    int opt;
    pthread_t writer_tid;
    int ret;

    /* 解析参数 */
    while ((opt = getopt(argc, argv, "c:d:nh")) != -1) {
        switch (opt) {
        case 'c': g_count = atoi(optarg); break;
        case 'd': g_delay_us = atoi(optarg); break;
        case 'n': g_nonblock = 1; break;
        case 'h':
        default:  usage(argv[0]); return (opt == 'h') ? 0 : 1;
        }
    }

    /* 打开设备 */
    int flags = O_RDWR;
    if (g_nonblock)
        flags |= O_NONBLOCK;

    g_fd = open(DEVICE_PATH, flags);
    if (g_fd < 0) {
        perror("open " DEVICE_PATH);
        fprintf(stderr, "Hint: insmod leetx_queue.ko first\n");
        return 1;
    }
    printf("=== leetx_queue Test ===\n");

    /* 启动 writer 线程 */
    ret = pthread_create(&writer_tid, NULL, writer_thread, NULL);
    if (ret != 0) {
        perror("pthread_create");
        close(g_fd);
        return 1;
    }

    /* 主线程做 reader */
    ret = reader_loop();

    /* 等待 writer 线程结束 */
    pthread_join(writer_tid, NULL);

    close(g_fd);
    printf("=== %s ===\n", (ret == 0) ? "PASS" : "FAIL");
    return ret;
}
