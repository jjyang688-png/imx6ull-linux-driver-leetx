/*
 * leetx_can_demo.c — Stage 6 SocketCAN 工业总线通信
 *
 * 用法：
 *   arm-linux-gnueabihf-gcc -o can_demo leetx_can_demo.c -Wall -O2
 *
 *   # 接收
 *   ./can_demo rx can0
 *
 *   # 发送
 *   ./can_demo tx can0 123#11223344
 *
 * 核心认知：
 *   SocketCAN 把 CAN 总线当成一种网络，
 *   用 socket() + bind() + write() + read() 操作。
 *   和 UDP socket 编程模式几乎一样。
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

/* ================================================================
 * SocketCAN 核心头文件
 * ================================================================ */
#include <sys/socket.h>          /* socket(), bind(), write(), read() */
#include <sys/ioctl.h>           /* ioctl(SIOCGIFINDEX) */
#include <net/if.h>              /* struct ifreq, if_nametoindex() */
#include <linux/can.h>           /* struct can_frame — CAN 帧定义 */
#include <linux/can/raw.h>       /* CAN_RAW 协议, PF_CAN, AF_CAN */

/*
 * struct can_frame（定义在 <linux/can.h>）：
 *
 *   struct can_frame {
 *       canid_t can_id;    // CAN ID (11 位或 29 位)
 *       uint8_t can_dlc;    // 数据长度 (0-8)
 *       uint8_t data[8];    // 数据字节
 *       uint8_t __pad;      // 对齐
 *       uint8_t __res0;
 *       uint8_t __res1;
 *   };
 *
 * 一帧 CAN 数据 = 一个 ID + 最多 8 字节数据
 * 例：ID=0x123, data={0x11, 0x22, 0x33, 0x44}
 * → cansend 命令：cansend can0 123#11223344
 */

/*
 * 接收示例
 */
static void can_receive(const char *ifname)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;

    /* 第 1 步：创建 CAN 套接字 — 和 UDP socket 几乎一样 */
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    //      │       │         │
    //      │       │         └── CAN_RAW: 原始 CAN 帧 (不用 CAN_BCM, CAN_ISOTP)
    //      │       └── SOCK_RAW: 原始套接字, 发什么收什么
    //      └── PF_CAN: CAN 协议族 (不是 AF_INET!)
    if (s < 0) { perror("socket"); exit(1); }

    /* 第 2 步：绑定到 can0 */
    strcpy(ifr.ifr_name, ifname);             /* "can0" */
    ioctl(s, SIOCGIFINDEX, &ifr);             /* 查"can0"对应的接口索引号 */
    //   上面两步等于: ifr.ifr_ifindex = if_nametoindex("can0");

    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;        /* 绑定到 can0 */
    bind(s, (struct sockaddr *)&addr, sizeof(addr));

    printf("[CAN] 接收模式: 在 %s 上监听...\n", ifname);

    /* 第 3 步：循环接收 */
    while (1) {
        int n = read(s, &frame, sizeof(frame));
        //       ↑ 和读文件/读 socket 一模一样
        //       阻塞等待：总线上有帧来了才返回

        if (n < 0) {
            perror("read");
            break;
        }

        /* 打印 CAN 帧：
         * ID (16 进制) + [数据长度] + 数据字节
         * 格式和 cansend/candump 命令一致
         */
        printf("%s  %03X   [%d]  ",
               ifname, frame.can_id, frame.can_dlc);
        for (int i = 0; i < frame.can_dlc; i++)
            printf("%02X ", frame.data[i]);
        printf("\n");
    }

    close(s);
}

/*
 * 发送示例
 */
static void can_send(const char *ifname, const char *send_str)
{
    int s;
    struct sockaddr_can addr;
    struct ifreq ifr;
    struct can_frame frame;
    unsigned int id;
    int len, i;

    /* 解析 "123#11223344" 格式的字符串 */
    sscanf(send_str, "%x#", &id);                /* 解析 ID: "123" → 0x123 */
    const char *data_str = strchr(send_str, '#'); /* 找到 # */
    if (!data_str) { fprintf(stderr, "格式: ID#DATA (例: 123#11223344)\n"); exit(1); }
    data_str++;
    len = strlen(data_str) / 2;                  /* 每两个字符一个字节 */
    if (len > 8) len = 8;                        /* CAN 最多 8 字节 */
    for (i = 0; i < len; i++)
        sscanf(data_str + i*2, "%2hhx", &frame.data[i]);

    frame.can_id  = id;
    frame.can_dlc = len;

    /* 创建 + 绑定 (和接收一样) */
    s = socket(PF_CAN, SOCK_RAW, CAN_RAW);
    if (s < 0) { perror("socket"); exit(1); }

    strcpy(ifr.ifr_name, ifname);
    ioctl(s, SIOCGIFINDEX, &ifr);
    addr.can_family  = AF_CAN;
    addr.can_ifindex = ifr.ifr_ifindex;
    bind(s, (struct sockaddr *)&addr, sizeof(addr));

    /* 发送 */
    int n = write(s, &frame, sizeof(frame));
    //       ↑ 和写文件/写 socket 一模一样
    if (n < 0)
        perror("write");
    else
        printf("[CAN] 已发送: %s  %03X  [%d]  ", ifname,
               frame.can_id, frame.can_dlc);
        for (i = 0; i < frame.can_dlc; i++)
            printf("%02X ", frame.data[i]);
        printf("\n");

    close(s);
}

int main(int argc, char *argv[])
{
    if (argc < 3) {
        printf("用法:\n");
        printf("  接收: %s rx <can接口>\n", argv[0]);
        printf("  发送: %s tx <can接口> <ID#DATA>\n", argv[0]);
        printf("\n示例:\n");
        printf("  %s rx can0\n", argv[0]);
        printf("  %s tx can0 123#11223344\n", argv[0]);
        printf("\n前提: ip link set up can0 type can bitrate 500000\n");
        return 1;
    }

    if (!strcmp(argv[1], "rx"))
        can_receive(argv[2]);
    else if (!strcmp(argv[1], "tx") && argc >= 4)
        can_send(argv[2], argv[3]);
    else
        printf("无效命令: %s\n", argv[1]);

    return 0;
}
