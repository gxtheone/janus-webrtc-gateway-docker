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

    opus_dec = NULL;
    JANUS_LOG(LOG_INFO, "rtmp prepare\n");

    // ffmpeg
    avcodec_register_all();
    avformat_network_init();

    char output_path[512] = {0};
    snprintf(output_path, sizeof(output_path)-1, "rtmp://wxs.cisco.com:1935/hls/%s", room_id);
    // g_snprintf(output_path, sizeof(output_path)-1, "rtmp://10.140.212.107:1935/home/%s", room_id);
    int ret = avformat_alloc_output_context2(&ofmt_ctx, NULL, "flv", output_path);
    if (ret < 0) {
        JANUS_LOG(LOG_ERR, "avformat alloc output context2 fail, err:%s\n", av_err2str(ret));
        return;
    }
    JANUS_LOG(LOG_INFO, "ffmpeg alloc output context2 success\n");

    // video
    AVCodec* pCodec = avcodec_find_encoder(AV_CODEC_ID_H264);
    if (!pCodec) {
        JANUS_LOG(LOG_ERR, "avcodec h264 not found\n");
        avformat_free_context(ofmt_ctx);
        return;
    }
    pStream = avformat_new_stream(ofmt_ctx, pCodec);
    if (!pStream) {
        JANUS_LOG(LOG_ERR, "avformat new stream video fail\n");
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
    JANUS_LOG(LOG_INFO, "ffmpeg new stream video success\n");
    ret = avcodec_open2(pStream->codec, pCodec, NULL);
    if (ret < 0) {
        JANUS_LOG(LOG_ERR, "avcodec open fail\n");
        avformat_free_context(ofmt_ctx);
        return;
    }
    JANUS_LOG(LOG_INFO, "ffmpeg video avcodec open2 success\n");

    // audio
    // opus_dec
    int err = 0;
    opus_dec = opus_decoder_create(48000, 1, &err);
    if (err != OPUS_OK) {
        JANUS_LOG(LOG_ERR, "opus decoder create fail, err=%d\n", err);
        return;
    }
    JANUS_LOG(LOG_INFO, "opus decoder create success\n");

    // faac
    aac_enc = faacEncOpen(48000, 1, &inputSamples, &maxOutputBytes);
    if (aac_enc == NULL) {
        JANUS_LOG(LOG_ERR, "faac enc open fail\n");
        return;
    }
	// 设置编码配置信息
    faacEncConfigurationPtr pConfiguration = NULL;
	pConfiguration = faacEncGetCurrentConfiguration(aac_enc);
	pConfiguration->inputFormat = FAAC_INPUT_16BIT;
	pConfiguration->outputFormat = ADTS_STREAM;
	pConfiguration->aacObjectType = LOW;
	pConfiguration->allowMidside = 0;
	pConfiguration->useLfe = 0;
    pConfiguration->useTns = 1;
    pConfiguration->shortctl = SHORTCTL_NORMAL;
    pConfiguration->quantqual = 100;
	pConfiguration->bitRate = 0;
	pConfiguration->bandWidth = 0;
	// 重置编码器的配置信息
	faacEncSetConfiguration(aac_enc, pConfiguration);
    printf("faac enc open success, inputSamples=%lu, maxOutputBytes=%lu\n", inputSamples, maxOutputBytes);
    // 分配缓存
    avdata.abuf = malloc(inputSamples * 2 * 2);
    avdata.abegin = 0;
    avdata.aend = 0;
    avdata.apts = janus_get_monotonic_time() / 1000;
    
    // srs-librtmp
    rtmp = srs_rtmp_create(output_path);
    if (rtmp == NULL) {
        JANUS_LOG(LOG_ERR, "srs rtmp create fail\n");
        return;
    }
    JANUS_LOG(LOG_INFO, "librtmp create success\n");
    ret = srs_rtmp_handshake(rtmp);
    if (ret != 0) {
        JANUS_LOG(LOG_ERR, "srs librtmp handshake fail, err:%d\n", ret);
        srs_rtmp_destroy(rtmp);
        return;
    }
    JANUS_LOG(LOG_INFO, "librtmp handshake success\n");
    ret = srs_rtmp_connect_app(rtmp);
    if (ret != 0) {
        JANUS_LOG(LOG_ERR, "srs rtmp connect app fail, err:%d\n", ret);
        srs_rtmp_destroy(rtmp);
        return;
    }
    JANUS_LOG(LOG_INFO, "librtmp connect app success\n");
    ret = srs_rtmp_publish_stream(rtmp);
    if (ret != 0) {
        JANUS_LOG(LOG_ERR, "srs rtmp publish stream fail, err:%d\n", ret);
        srs_rtmp_destroy(rtmp);
        return;
    }
    JANUS_LOG(LOG_INFO, "librtmp publish success\n");
    codec_prepared_flag = TRUE;
}

int rtmp_push_stream(char *buf, int len, MediaType av) {
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
    // rtp解包
    int rv = 0;
    if (av == Media_Video) {
        if (rtp_demux_ctx == NULL) {
            rtp_demux_ctx = ff_rtp_parse_open(ofmt_ctx, pStream, rtp_header->type, 5*1024*1024);
            if (!rtp_demux_ctx) {
                JANUS_LOG(LOG_ERR, "video ff_rtp_parse_open fail\n");
                return -1;
            }
            RTPDynamicProtocolHandler* dpHandler = ff_rtp_handler_find_by_name("H264", AVMEDIA_TYPE_VIDEO);
            if (!dpHandler) {
                JANUS_LOG(LOG_ERR, "ff_rtp_handler_find_by_id h264 fail\n");
                return -1;
            }
            ff_rtp_parse_set_dynamic_protocol(rtp_demux_ctx, NULL, dpHandler);
            JANUS_LOG(LOG_INFO, "video rtp parse prepared\n");
        }

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
    } else if (av == Media_Audio) {
        int plen = 0;
        char *payload = janus_rtp_payload(buf, len, &plen);
        if(payload == NULL)
            return -1;
        // 解码
        int frame_size = 48000 / 50 * 1;  // (1 == channels)
        opus_uint16 pcmbuf[1024 * 1024] = {0};
        int pcmlen = frame_size * 1 * sizeof(opus_int16);
        int ret = opus_decode(opus_dec, payload, plen, pcmbuf, frame_size, 0);
        if (ret <= 0) {
            JANUS_LOG(LOG_ERR, "opus decode fail\n");
            return -1;
        }
        // aac编码及推送
        memcpy(avdata.abuf + avdata.aend, pcmbuf, pcmlen);
        avdata.aend += pcmlen;
        if (avdata.aend - avdata.abegin >= inputSamples * 2) {
            // 编码
            uint8_t aacbuf[8192] = {0};
            uint aaclen = faacEncEncode(aac_enc, (int32_t*)avdata.abuf, inputSamples, aacbuf, sizeof(aacbuf));
            avdata.abegin += inputSamples * 2;
            // 数据移位
            memcpy(avdata.abuf, avdata.abuf + avdata.abegin, avdata.aend - avdata.abegin);
            avdata.abegin = 0;
            avdata.aend -= inputSamples * 2;
            // rtmp推送
            if (aaclen > 0) {
                avdata.apts = janus_get_monotonic_time() / 1000;
                ret = srs_audio_write_raw_frame(rtmp, 10, 3, 1, 0, aacbuf, aaclen, avdata.apts);
                if (ret != 0) {
                    JANUS_LOG(LOG_ERR, "rtmp send audio frame fail:%d\n", ret);
                    return -1;
                }
            }
        }

    }

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
    if (opus_dec) {
        opus_decoder_destroy(opus_dec);
        opus_dec = NULL;
    }
    if (aac_enc) {
        aacEncClose(aac_enc);
        aac_enc = NULL;
    }
}