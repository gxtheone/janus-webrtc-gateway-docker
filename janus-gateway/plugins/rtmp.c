#include "rtmp.h"
#include "debug.h"
#include "../rtp.h"
#include <libavutil/error.h>


void rtmp_prepare(char* room_id, int width, int height) {
    // 变量初始化
    ofmt_ctx = NULL;
    pStream = NULL;
    codec_prepared_flag = FALSE;
    // janus_mutex ff_mutex = JANUS_MUTEX_INITIALIZER;
    rtmp = NULL;
    rtp_demux_ctx = NULL;
    printf("### rtmp prepare\n");

    // ffmpeg
    avcodec_register_all();
    avformat_network_init();

    char output_path[512] = {0};
    // snprintf(output_path, sizeof(output_path)-1, "rtmp://wxs.cisco.com:1935/hls/%s", room_id);
    g_snprintf(output_path, sizeof(output_path)-1, "rtmp://10.140.212.107:1935/home/%s", room_id);
    int ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", output_path);
    if (ret < 0) {
        JANUS_LOG(LOG_ERR, "avformat alloc output context2 fail, err:%s\n", av_err2str(ret));
        return;
    }
    printf("### ffmpeg alloc output context2 success\n");

    AVCodec* pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!pCodec) {
        JANUS_LOG(LOG_ERR, "avcodec h264 not found\n");
        avformat_free_context(ofmt_ctx);
        return;
    }
    pStream = avformat_new_stream(ofmt_ctx, pCodec);
    if (!pStream) {
        JANUS_LOG(LOG_ERR, "avformat new stream fail\n");
        avformat_free_context(ofmt_ctx);
        return;
    }
    pStream->time_base = (AVRational){1, 1000};
    pStream->codec->codec_id = pCodec->id;
    pStream->codec->codec_type = AVMEDIA_TYPE_VIDEO;
    pStream->codec->pix_fmt = AV_PIX_FMT_YUV420P;
    pStream->codec->width = width;
    pStream->codec->height = height;
    pStream->codec->time_base = (AVRational){1, 1000};
    if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER) {
        pStream->codec->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }
    printf("### ffmpeg new stream success\n");

    ret = avcodec_open2(pStream->codec, pCodec, NULL);
    if (ret < 0) {
        JANUS_LOG(LOG_ERR, "avcodec open fail\n");
        avformat_free_context(ofmt_ctx);
        return;
    }
    printf("### ffmpeg avcodec open2 success\n");


    // srs-librtmp
    rtmp = srs_rtmp_create(output_path);
    if (rtmp == NULL) {
        JANUS_LOG(LOG_ERR, "srs rtmp create fail\n");
        return;
    }
    printf("### librtmp create success\n");

    ret = srs_rtmp_handshake(rtmp);
    if (ret != 0) {
        JANUS_LOG(LOG_ERR, "srs rtmp handshake fail, err:%d\n", ret);
        srs_rtmp_destroy(rtmp);
        return;
    }
    printf("### librtmp handshake success\n");

    ret = srs_rtmp_connect_app(rtmp);
    if (ret != 0) {
        JANUS_LOG(LOG_ERR, "srs rtmp connect app fail, err:%d\n", ret);
        srs_rtmp_destroy(rtmp);
        return;
    }
    printf("### librtmp connect app success\n");

    ret = srs_rtmp_publish_stream(rtmp);
    if (ret != 0) {
        JANUS_LOG(LOG_ERR, "srs rtmp publish stream fail, err:%d\n", ret);
        srs_rtmp_destroy(rtmp);
        return;
    }
    printf("### librtmp publish success\n");

    codec_prepared_flag = TRUE;
    JANUS_LOG(LOG_INFO, "ffmepg prepared\n");
}

int rtmp_push_stream(char *buf, int len) {
    if (ofmt_ctx == NULL) {
        JANUS_LOG(LOG_ERR, "ofmt_ctx == NULL\n");
        return -1;
    }
    if (!codec_prepared_flag) {
        JANUS_LOG(LOG_WARN, "wait for ffmpeg prepared\n");
        return -1;
    }

    // 准备rtp解析工作
    janus_rtp_header *rtp_header = (janus_rtp_header *)buf;
    if (rtp_demux_ctx == NULL) {
        rtp_demux_ctx = ff_rtp_parse_open(ofmt_ctx, pStream, rtp_header->type, 5*1024*1024);
        if (!rtp_demux_ctx) {
            JANUS_LOG(LOG_ERR, "ff_rtp_parse_open fail\n");
            return -1;
        }
        RTPDynamicProtocolHandler* dpHandler = ff_rtp_handler_find_by_name("H264", AVMEDIA_TYPE_VIDEO);
        if (!dpHandler) {
            JANUS_LOG(LOG_ERR, "ff_rtp_handler_find_by_id fail\n");
            return -1;
        }
        ff_rtp_parse_set_dynamic_protocol(rtp_demux_ctx, NULL, dpHandler);
        JANUS_LOG(LOG_INFO, "rtp parse prepared\n");
    }

    // rtp解包
    int rv = 0;
    // janus_mutex_lock(&ff_mutex);
    do {
        // 解rtp包
        AVPacket pkt;
        av_init_packet(&pkt);
        rv = ff_rtp_parse_packet(rtp_demux_ctx, &pkt, rv==1 ? NULL : &buf, len);
        if (rv == -1 || pkt.size == 0) {
            JANUS_LOG(LOG_ERR, "rtp parse fail\n");
            av_packet_unref(&pkt);
            break;
        }
        // TODO: audio需要区分处理
        if (pkt.data[0] == 0x00 && pkt.data[1] == 0x00 && ((pkt.data[2] == 0x00 && pkt.data[3] == 0x01) || pkt.data[2] == 0x01)) {
            if (avdata.vbuf != NULL) {
                // 使用srslibrtmp推流，不考虑b帧情况
                int ret = srs_h264_write_raw_frames(rtmp, avdata.vbuf, avdata.vlen, avdata.vpts, avdata.vpts);
                if (ret != 0) {
                    JANUS_LOG(LOG_ERR, "h264 frame push to rtmp server fail: %d\n", ret);
                    JANUS_LOG(LOG_INFO, "raw frames, size=%d, pts=%lld, dts=%lld\n", avdata.vlen, avdata.vpts, avdata.vpts);
                }
                // 无论发送成功失败都要释放缓存
                free(avdata.vbuf);
                avdata.vbuf = NULL;
                avdata.vlen = 0;
                // pts必须放在此处计算
                avdata.vpts = janus_get_monotonic_time() / 1000;
            }
            avdata.vbuf = malloc(pkt.size);
            memcpy(avdata.vbuf, pkt.data, pkt.size);
            avdata.vlen += pkt.size;
        } else {
            if (avdata.vbuf == NULL) {
                av_packet_unref(&pkt);
                break;
            }
            // 组装完整的nal单元
            avdata.vbuf = realloc(avdata.vbuf, avdata.vlen + pkt.size);
            memcpy(avdata.vbuf + avdata.vlen, pkt.data, pkt.size);
            avdata.vlen += pkt.size;
        }
        av_packet_unref(&pkt);
    } while(rv == 1);
    // janus_mutex_unlock(&ff_mutex);

    return rv;
}

void rtmp_end() {
    JANUS_LOG(LOG_INFO, "stream push over, release resources\n");
    // TODO:释放资源
    if (rtmp) {
        srs_rtmp_destroy(rtmp);
        rtmp = NULL;
    }
    if (ofmt_ctx) {
        avformat_free_context(ofmt_ctx);
        ofmt_ctx = NULL;
    }
}