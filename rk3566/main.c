#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <poll.h>

#include "video.h"
#include "udp.h"
#include "uart.h"
#include "config.h"

// 视频帧序列号
static uint32_t frame_seq = 0;

// 分片协议定义
#define FRAME_SEQ_SIZE   4
#define SLICE_SEQ_SIZE   1
#define SLICE_TOTAL_SIZE 1
#define HEAD_SIZE (FRAME_SEQ_SIZE + SLICE_SEQ_SIZE + SLICE_TOTAL_SIZE)
#define UDP_MTU         1400
#define SLICE_DATA_SIZE (UDP_MTU - HEAD_SIZE)

// 带分片的视频发送
static void udp_send_video_with_seq(const void *data, int len) {
    if (!data || len <= 0) return;

    const uint8_t *h264 = (const uint8_t *)data;
    int offset = 0;
    int remain = len;
       int total_slice = (len + SLICE_DATA_SIZE - 1) / SLICE_DATA_SIZE;
    if (total_slice > 255) total_slice = 255;

    for (uint8_t slice_seq = 0; slice_seq < total_slice && remain > 0; slice_seq++) {
        uint8_t pkt[UDP_MTU];
        int pkt_len = 0;

        // 4字节帧序号 网络字节序
        uint32_t net_fseq = htonl(frame_seq);
        memcpy(pkt + pkt_len, &net_fseq, 4);
        pkt_len += 4;

        // 当前分片号、总分片数
        pkt[pkt_len++] = slice_seq;
        pkt[pkt_len++] = total_slice;

        // 分片数据
        int send_sz = remain > SLICE_DATA_SIZE ? SLICE_DATA_SIZE : remain;
        memcpy(pkt + pkt_len, h264 + offset, send_sz);
        pkt_len += send_sz;

        sendto(video_sock, pkt, pkt_len, 0,
               (struct sockaddr *)&pc_addr, sizeof(pc_addr));

        offset += send_sz;
        remain -= send_sz;
    }

    frame_seq++;
}

// 处理飞控指令透传
static void handle_flight_commands(int cmd_sock) {
    char cmd_buffer[256];
    struct sockaddr_in sender_addr;
    socklen_t addr_len = sizeof(sender_addr);
    
    ssize_t bytes_received = recvfrom(cmd_sock, 
                                      cmd_buffer, 
                                      sizeof(cmd_buffer) - 1, 
                                      MSG_DONTWAIT,
                                      (struct sockaddr*)&sender_addr, 
                                      &addr_len);
    
    if (bytes_received > 0) {
        cmd_buffer[bytes_received] = '\0';
        
        if (strstr(cmd_buffer, RTF_CMD) != NULL) {
            printf("🔄 接收到飞控指令: %s", cmd_buffer);
            uart_send_rtf_mode();
        } else {
            printf("🔄 透传指令到飞控: %s", cmd_buffer);
            write(uart_fd, cmd_buffer, bytes_received);
        }
    }
}

int main()
{
    if (video_init() < 0) {
        fprintf(stderr, "❌ 视频初始化失败\n");
        return -1;
    }

    udp_init_video();
    
    int cmd_sock = udp_create_cmd_socket();
    if (cmd_sock < 0) {
        fprintf(stderr, "⚠️  命令UDP初始化失败，仅支持单向视频流\n");
    } else {
        uart_init();
        if (uart_fd < 0) {
            fprintf(stderr, "⚠️  UART初始化失败，无法透传飞控指令\n");
            close(cmd_sock);
            cmd_sock = -1;
        }
    }

    printf("=============================================\n");
    printf("✅ RK3566 视频推流已启动\n");
    printf("🎯 目标IP: %s\n", PC_IP);
    printf("📡 视频端口: %d\n", VIDEO_UDP_PORT);
    printf("📡 命令端口: %d\n", CMD_UDP_PORT);
    printf("=============================================\n");

    unsigned char *h264_data = NULL;
    int h264_len = 0;

    while (1) {
        int ret = video_get_h264_frame(&h264_data, &h264_len);

        if (ret == 0 && h264_data && h264_len > 0) {
            udp_send_video_with_seq(h264_data, h264_len);
            printf("📤 发送帧 #%u：%d 字节\n", frame_seq - 1, h264_len);
        }
        
        if (cmd_sock >= 0) {
            handle_flight_commands(cmd_sock);
        }

        usleep(33000);
    }

    if (cmd_sock >= 0) {
        close(cmd_sock);
    }
    video_deinit();
    return 0;
}