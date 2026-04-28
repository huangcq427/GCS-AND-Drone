#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <linux/videodev2.h>
#include <sys/mman.h>
#include "config.h"

#include <libavcodec/avcodec.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>

#define CAPTURE_COUNT 4
#define VIDEO_WIDTH   640
#define VIDEO_HEIGHT  480
#define MAX_H264_BUF  (512 * 1024)

static int cap_fd = -1;
static void *cap_buf[CAPTURE_COUNT];
static int cap_len[CAPTURE_COUNT];

static AVCodec *codec = NULL;
static AVCodecContext *codec_ctx = NULL;
static AVFrame *frame = NULL;
static AVPacket *pkt = NULL;
static struct SwsContext *sws_ctx = NULL;

static int open_camera()
{
    cap_fd = open(VIDEO_DEVICE, O_RDWR | O_NONBLOCK);
    if (cap_fd < 0) {
        perror("camera open");
        return -1;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = VIDEO_WIDTH;
    fmt.fmt.pix.height = VIDEO_HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;

    if (ioctl(cap_fd, VIDIOC_S_FMT, &fmt) < 0) {
        perror("camera set format");
        close(cap_fd);
        return -1;
    }

    struct v4l2_requestbuffers req = {0};
    req.count = CAPTURE_COUNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;

    if (ioctl(cap_fd, VIDIOC_REQBUFS, &req) < 0) {
        perror("reqbufs");
        close(cap_fd);
        return -1;
    }

    for (int i = 0; i < CAPTURE_COUNT; i++) {
        struct v4l2_buffer buf = {0};
        buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        buf.memory = V4L2_MEMORY_MMAP;
        buf.index = i;

        if (ioctl(cap_fd, VIDIOC_QUERYBUF, &buf) < 0) {
            perror("querybuf");
            close(cap_fd);
            return -1;
        }

        cap_buf[i] = mmap(NULL, buf.length, PROT_READ, MAP_SHARED, cap_fd, buf.m.offset);
        if (cap_buf[i] == MAP_FAILED) {
            perror("mmap");
            close(cap_fd);
            return -1;
        }
        cap_len[i] = buf.length;

        if (ioctl(cap_fd, VIDIOC_QBUF, &buf) < 0) {
            perror("qbuf");
            close(cap_fd);
            return -1;
        }
    }

    int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    if (ioctl(cap_fd, VIDIOC_STREAMON, &type) < 0) {
        perror("streamon");
        close(cap_fd);
        return -1;
    }

    printf("✅ 摄像头初始化成功：%dx%d\n", VIDEO_WIDTH, VIDEO_HEIGHT);
    return 0;
}

static int init_ffmpeg_encoder()
{

    codec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!codec) {
        fprintf(stderr, "❌ 找不到H264编码器\n");
        return -1;
    }

    codec_ctx = avcodec_alloc_context3(codec);
    if (!codec_ctx) {
        fprintf(stderr, "❌ 编码器上下文分配失败\n");
        return -1;
    }

    // ===================== 关键：实时流编码参数 =====================
    codec_ctx->width = VIDEO_WIDTH;
    codec_ctx->height = VIDEO_HEIGHT;
    codec_ctx->bit_rate = 800000;         // 码率 800k 更清晰
    codec_ctx->time_base = (AVRational){1, 30};
    codec_ctx->framerate = (AVRational){30, 1};
    codec_ctx->gop_size = 30;             // 每秒1个I帧
    codec_ctx->max_b_frames = 0;          // 无B帧，实时流必须关
    codec_ctx->pix_fmt = AV_PIX_FMT_YUV420P;

    // 超低延迟 + 裸流标准配置（Qt 解码必须）
    av_opt_set(codec_ctx->priv_data, "profile", "baseline", 0);
    av_opt_set(codec_ctx->priv_data, "preset", "ultrafast", 0);
    av_opt_set(codec_ctx->priv_data, "tune", "zerolatency", 0);

    // 关键：每个I帧自动带 SPS/PPS，Qt 一上电就能解码
    av_opt_set(codec_ctx->priv_data, "x264-params", "repeat-headers=1", 0);

    if (avcodec_open2(codec_ctx, codec, NULL) < 0) {
        fprintf(stderr, "❌ 编码器打开失败\n");
        avcodec_free_context(&codec_ctx);
        return -1;
    }

    frame = av_frame_alloc();
    frame->format = AV_PIX_FMT_YUV420P;
    frame->width = VIDEO_WIDTH;
    frame->height = VIDEO_HEIGHT;
    av_frame_get_buffer(frame, 32);

    pkt = av_packet_alloc();

    // YUYV => YUV420P
    sws_ctx = sws_getContext(VIDEO_WIDTH, VIDEO_HEIGHT, AV_PIX_FMT_YUYV422,
                             VIDEO_WIDTH, VIDEO_HEIGHT, AV_PIX_FMT_YUV420P,
                             SWS_FAST_BILINEAR, NULL, NULL, NULL);

    printf("✅ H264 编码器初始化完成\n");
    return 0;
}

int video_init(void)
{
    if (open_camera() < 0)
        return -1;

    if (init_ffmpeg_encoder() < 0) {
        if (cap_fd >= 0) {
            int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
            ioctl(cap_fd, VIDIOC_STREAMOFF, &type);
            for (int i = 0; i < CAPTURE_COUNT; i++) {
                munmap(cap_buf[i], cap_len[i]);
            }
            close(cap_fd);
            cap_fd = -1;
        }
        return -1;
    }

    printf("✅ 视频模块全部启动成功\n");
    return 0;
}

// ===================== 核心修复：获取标准 H264 裸流 =====================
int video_get_h264_frame(unsigned char **out_buf, int *out_len)
{
    static unsigned char h264_buf[MAX_H264_BUF];
    static int64_t pts = 0;
    int total_len = 0;

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;

    // 无数据直接返回
    if (ioctl(cap_fd, VIDIOC_DQBUF, &buf) < 0) {
        *out_len = 0;
        return 0;
    }

    // YUYV => YUV420P
    const uint8_t *src_data[] = { (const uint8_t *)cap_buf[buf.index] };
    int src_stride[] = { VIDEO_WIDTH * 2 };

    sws_scale(sws_ctx,
              src_data, src_stride,
              0, VIDEO_HEIGHT,
              frame->data, frame->linesize);

    frame->pts = pts++;

    // 发送帧
    avcodec_send_frame(codec_ctx, frame);

    // 读取所有编码输出（标准 Annex B H264 裸流）
    while (avcodec_receive_packet(codec_ctx, pkt) == 0) {
        if (total_len + pkt->size < MAX_H264_BUF) {
            memcpy(h264_buf + total_len, pkt->data, pkt->size);
            total_len += pkt->size;
        }
        av_packet_unref(pkt);
    }

    // 归还缓冲区
    ioctl(cap_fd, VIDIOC_QBUF, &buf);

    *out_buf = h264_buf;
    *out_len = total_len;

    return total_len > 0 ? 0 : -1;
}

void video_deinit(void)
{
    if (cap_fd >= 0) {
        int type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        ioctl(cap_fd, VIDIOC_STREAMOFF, &type);

        for (int i = 0; i < CAPTURE_COUNT; i++) {
            munmap(cap_buf[i], cap_len[i]);
        }
        close(cap_fd);
        cap_fd = -1;
    }

    sws_freeContext(sws_ctx);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    avcodec_free_context(&codec_ctx);

    printf("✅ 视频资源已释放\n");
}