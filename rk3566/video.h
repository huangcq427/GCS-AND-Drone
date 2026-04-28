#pragma once

int video_init(void);
int video_get_h264_frame(unsigned char **out_buf, int *out_len);
void video_deinit(void);