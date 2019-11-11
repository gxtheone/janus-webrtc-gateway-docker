#ifndef JANUS_RTMP_H
#define JANUS_RTMP_H

#include <glib.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/error.h>
#include <libavformat/rtpdec.h>
#include <srs_librtmp.h>
#include "../mutex.h"

// 用于组装音视频完整帧
typedef struct AVData {
    uint8_t* vbuf;
    int vlen;
    uint32_t vpts;
    uint8_t* abuf;
    int alen;
} AVData;

// 变量声明
AVFormatContext* ofmt_ctx;
AVStream* pStream;
RTPDemuxContext* rtp_demux_ctx;
gboolean codec_prepared_flag;
// janus_mutex ff_mutex = JANUS_MUTEX_INITIALIZER;
srs_rtmp_t rtmp;
AVData avdata;

// 函数声明
void rtmp_prepare(char* room_id, int width, int height);
int rtmp_push_stream(char *buf, int len);
void rtmp_end(void);
// end ffmpeg

#endif