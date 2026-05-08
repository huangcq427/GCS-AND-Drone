#include "udp.h"
#include "config.h"
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

// 改为全局变量，供main.c访问
int video_sock = -1;
struct sockaddr_in pc_addr;

// 初始化视频发送UDP
void udp_init_video(void) {
    if (video_sock >= 0) {
        close(video_sock);
    }

    video_sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (video_sock < 0) {
        perror("❌ udp_init_video: socket 创建失败");
        return;
    }

    memset(&pc_addr, 0, sizeof(pc_addr));
    pc_addr.sin_family = AF_INET;
    pc_addr.sin_port = htons(VIDEO_UDP_PORT);

    if (inet_pton(AF_INET, PC_IP, &pc_addr.sin_addr) <= 0) {
        fprintf(stderr, "❌ udp_init_video: 无效 PC_IP: %s\n", PC_IP);
        close(video_sock);
        video_sock = -1;
        return;
    }

    printf("✅ 视频UDP初始化完成 → %s:%d\n", PC_IP, VIDEO_UDP_PORT);
}

// 分片发送H264（保留原有函数，但main.c使用新的带序列号版本）
void udp_send(const void *data, int len) {
    if (video_sock < 0) {
        return;
    }
    if (!data || len <= 0) {
        return;
    }

    const int MAX_UDP_PAYLOAD = 1400;
    int offset = 0;
    const unsigned char *buf = (const unsigned char *)data;

    while (offset < len) {
        int send_len = len - offset;
        if (send_len > MAX_UDP_PAYLOAD) {
            send_len = MAX_UDP_PAYLOAD;
        }

        ssize_t ret = sendto(video_sock,
                             buf + offset,
                             send_len,
                             0,
                             (struct sockaddr *)&pc_addr,
                             sizeof(pc_addr));

        if (ret < 0) {
            break;
        }

        offset += send_len;
    }
}

// 创建命令监听Socket（Qt端发RTF指令）
int udp_create_cmd_socket(void) {
    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("❌ 命令 socket 创建失败");
        return -1;
    }

    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(CMD_UDP_PORT);
    addr.sin_addr.s_addr = htonl(INADDR_ANY);

    if (bind(sock, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        perror("❌ 命令端口绑定失败");
        close(sock);
        return -1;
    }

    printf("✅ 命令UDP监听已启动 → 端口 %d\n", CMD_UDP_PORT);
    return sock;
}