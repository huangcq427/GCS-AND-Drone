#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>

#include "video.h"
#include "udp.h"
#include "config.h"

int main()
{
    // ===================== 初始化视频 =====================
    if (video_init() < 0) {
        fprintf(stderr, "❌ 视频初始化失败\n");
        return -1;
    }

    // ===================== 初始化UDP =====================
    udp_init_video();

    printf("=============================================\n");
    printf("✅ RK3566 视频推流已启动\n");
    printf("🎯 目标IP: %s\n", PC_IP);
    printf("📡 目标端口: %d\n", VIDEO_UDP_PORT);
    printf("=============================================\n");

    // ===================== 主循环 =====================
    unsigned char *h264_data = NULL;
    int h264_len = 0;

    while (1) {
        // 获取一帧 H.264
        int ret = video_get_h264_frame(&h264_data, &h264_len);

        // 有效帧才发送
        if (ret > 0 && h264_data && h264_len > 0) {
            udp_send(h264_data, h264_len);
            printf("📤 发送帧：%d 字节\n", h264_len);
        }

        // 稳定 25~30 帧
        usleep(33000);
    }

    // ===================== 不会走到这里 =====================
    video_deinit();
    return 0;
}