#pragma once

// 全局变量声明（用于main.c访问）
extern int video_sock;
extern struct sockaddr_in pc_addr;

void udp_init_video(void);
void udp_send(const void *data, int len);
int udp_create_cmd_socket(void);