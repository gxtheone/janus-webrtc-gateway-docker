#ifndef JANUS_RTMP_H
#define JANUS_RTMP_H

#include <glib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavformat/rtpdec.h>
#include <srs_librtmp.h>
#include "../mutex.h"
#include <opus/opus.h>
#include <EasyAACEncoderAPI.h>
#include <faac.h>
#include <faaccfg.h>
// #include <fdk-aac/aacenc_lib.h>

typedef enum {
    Media_Audio = 0,
    Media_Video
} MediaType;

// 用于组装音视频完整帧
typedef struct AVData {
    uint8_t* vbuf;
    int vlen;
    uint32_t vpts;
    uint8_t* abuf;
    int abegin;
    int aend;
    uint32_t apts;
} AVData;

// 变量声明
AVFormatContext* ofmt_ctx;
AVStream* pStream;
RTPDemuxContext* rtp_demux_ctx;
gboolean codec_prepared_flag;
// janus_mutex ff_mutex = JANUS_MUTEX_INITIALIZER;
srs_rtmp_t rtmp;
AVData avdata;

// opus解码
OpusDecoder* opus_dec;

// aac编码
faacEncHandle aac_enc;
ulong inputSamples; // 一个采样深度(16bit)算一个样本
ulong maxOutputBytes;
// Easy_Handle aac_enc;
// AVCodec* aCodec;
// AVCodecContext* aCtx;

// 函数声明
void rtmp_prepare(char* room_id, int width, int height);
int rtmp_push_stream(char *buf, int len, MediaType av);
void rtmp_end(void);
// end ffmpeg

#endif