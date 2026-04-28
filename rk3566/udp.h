#pragma once

void udp_init_video(void);
void udp_send(const void *data, int len);
int udp_create_cmd_socket(void);