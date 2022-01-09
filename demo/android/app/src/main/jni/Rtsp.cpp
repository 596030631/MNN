#include "Rtsp.h"

Rtsp::Rtsp() = default;

Rtsp::~Rtsp() = default;


void Rtsp::log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag) {
    AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
    LOGD("%s: pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
         tag,
         av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
         av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
         av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
         pkt->stream_index);
}


bool Rtsp::play(const char *rtspUrl, const char *out_filename) {
    recording = true;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = nullptr;
    int stream_mapping_size;
    AVPacket pkt;
    AVFormatContext *ifmt_ctx = nullptr;
    AVFormatContext *ofmt_ctx = nullptr;
    AVOutputFormat *ofmt;

    AVDictionary *option = nullptr;
    av_dict_set(&option, "rtsp_transport", "tcp", 0);
    av_dict_set(&option, "max_delay", "5000000", 0);

    avformat_open_input(&ifmt_ctx, rtspUrl, nullptr, &option);
    if (!ifmt_ctx) {
        LOGD("avformat_open_input:打开错误");
        return false;
    } else {
        LOGW("RTSP流已连接");
    }

    av_dict_free(&option);

    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
        LOGE("Cannot find stream info");
        return false;
    }

    int video_stream_id = -1;
    for (i = 0; i < ifmt_ctx->nb_streams; ++i) {
        LOGW("NB_STREAM:%d", ifmt_ctx->streams[i]->codecpar->codec_type);
        if (AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[i]->codecpar->codec_type) {
            video_stream_id = i;
        }
    }

    if (video_stream_id == -1) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Video stream not found");
        return false;
    }

    av_dump_format(ifmt_ctx, 0, rtspUrl, 0);

    avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, out_filename);

    if (!ofmt_ctx) {
        LOGE("avformat_alloc_output_context2 failed");
        return false;
    }

    stream_mapping_size = (int) ifmt_ctx->nb_streams;
    stream_mapping = static_cast<int *>(av_mallocz_array(stream_mapping_size,
                                                         sizeof(*stream_mapping)));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }


    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        LOGD("RTSP:[%d %dx%d]", in_codecpar->codec_type, in_codecpar->width, in_codecpar->height);

        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            LOGD("Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
        LOGD("H264:[%d %dx%d]", out_stream->codecpar->codec_type, out_stream->codecpar->width,
             out_stream->codecpar->height);
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGD("Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    LOGE("O_STREAM:%d", ofmt_ctx->nb_streams);

    ret = avformat_write_header(ofmt_ctx, nullptr);

    if (ret < 0) {
        LOGD("Error occurred when opening output file:%d", ret);
        goto end;
    }

    AVFrame *frame;
    frame = av_frame_alloc();

    while (recording) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];
        out_stream = ofmt_ctx->streams[pkt.stream_index];
        log_packet(ifmt_ctx, &pkt, "in");

        /* copy packet */
        pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base,
                                   static_cast<AVRounding>(AV_ROUND_NEAR_INF |
                                                           AV_ROUND_PASS_MINMAX));
        pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
        pkt.pos = -1;
        log_packet(ofmt_ctx, &pkt, "out");

        ret = av_interleaved_write_frame(ofmt_ctx, &pkt);

        if (ret < 0) {
            LOGD("Error muxing packet\n");
            break;
        }
        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);

    end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE("Error occurred: %s\n", av_err2str(ret));
        return false;
    }

    LOGE("AAAAAAAAAAA");
    // AvcFileReader::start_decode("/sdcard/Android/Data/com.taobao.android.mnndemo/cache/1641732312589.mp4", out_filename);
    LOGE("BBBBBBBBBB");

    return true;
}


bool Rtsp::playImage(const char *rtspUrl, const char *out_filename) {
    recording = true;
    int ret, i;
    int stream_index = 0;
    int *stream_mapping = nullptr;
    int stream_mapping_size;
    AVPacket pkt;
    AVFormatContext *ifmt_ctx = nullptr;
    AVFormatContext *ofmt_ctx = nullptr;
    AVOutputFormat *ofmt;

    AVDictionary *option = nullptr;
    av_dict_set(&option, "rtsp_transport", "tcp", 0);
    av_dict_set(&option, "max_delay", "5000000", 0);

    avformat_open_input(&ifmt_ctx, rtspUrl, nullptr, &option);
    if (!ifmt_ctx) {
        LOGD("avformat_open_input:打开错误");
        return false;
    } else {
        LOGW("RTSP流已连接");
    }

    av_dict_free(&option);

    if (avformat_find_stream_info(ifmt_ctx, nullptr) < 0) {
        LOGE("Cannot find stream info");
        return false;
    }

    int video_stream_id = -1;
    for (i = 0; i < ifmt_ctx->nb_streams; ++i) {
        LOGW("NB_STREAM:%d", ifmt_ctx->streams[i]->codecpar->codec_type);
        if (AVMEDIA_TYPE_VIDEO == ifmt_ctx->streams[i]->codecpar->codec_type) {
            video_stream_id = i;
        }
    }

    if (video_stream_id == -1) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Video stream not found");
        return false;
    }

    av_dump_format(ifmt_ctx, 0, rtspUrl, 0);

    avformat_alloc_output_context2(&ofmt_ctx, nullptr, nullptr, out_filename);

    if (!ofmt_ctx) {
        LOGE("avformat_alloc_output_context2 failed");
        return false;
    }

    stream_mapping_size = (int) ifmt_ctx->nb_streams;
    stream_mapping = static_cast<int *>(av_mallocz_array(stream_mapping_size,
                                                         sizeof(*stream_mapping)));
    if (!stream_mapping) {
        ret = AVERROR(ENOMEM);
        goto end;
    }


    ofmt = ofmt_ctx->oformat;

    for (i = 0; i < ifmt_ctx->nb_streams; i++) {
        AVStream *out_stream;
        AVStream *in_stream = ifmt_ctx->streams[i];
        AVCodecParameters *in_codecpar = in_stream->codecpar;

        if (in_codecpar->codec_type != AVMEDIA_TYPE_VIDEO &&
            in_codecpar->codec_type != AVMEDIA_TYPE_SUBTITLE) {
            stream_mapping[i] = -1;
            continue;
        }

        LOGD("RTSP:[%d %dx%d]", in_codecpar->codec_type, in_codecpar->width, in_codecpar->height);

        stream_mapping[i] = stream_index++;

        out_stream = avformat_new_stream(ofmt_ctx, nullptr);
        if (!out_stream) {
            fprintf(stderr, "Failed allocating output stream\n");
            ret = AVERROR_UNKNOWN;
            goto end;
        }

        ret = avcodec_parameters_copy(out_stream->codecpar, in_codecpar);
        if (ret < 0) {
            LOGD("Failed to copy codec parameters\n");
            goto end;
        }
        out_stream->codecpar->codec_tag = 0;
        LOGD("H264:[%d %dx%d]", out_stream->codecpar->codec_type, out_stream->codecpar->width,
             out_stream->codecpar->height);
    }

    av_dump_format(ofmt_ctx, 0, out_filename, 1);

    if (!(ofmt->flags & AVFMT_NOFILE)) {
        ret = avio_open(&ofmt_ctx->pb, out_filename, AVIO_FLAG_WRITE);
        if (ret < 0) {
            LOGD("Could not open output file '%s'", out_filename);
            goto end;
        }
    }

    LOGE("O_STREAM:%d", ofmt_ctx->nb_streams);

    ret = avformat_write_header(ofmt_ctx, nullptr);

    if (ret < 0) {
        LOGD("Error occurred when opening output file:%d", ret);
        goto end;
    }

    while (recording) {
        AVStream *in_stream, *out_stream;

        ret = av_read_frame(ifmt_ctx, &pkt);
        if (ret < 0)
            break;

        in_stream = ifmt_ctx->streams[pkt.stream_index];
        if (pkt.stream_index >= stream_mapping_size ||
            stream_mapping[pkt.stream_index] < 0) {
            av_packet_unref(&pkt);
            continue;
        }

        pkt.stream_index = stream_mapping[pkt.stream_index];

        av_packet_unref(&pkt);
    }

    av_write_trailer(ofmt_ctx);

    end:

    avformat_close_input(&ifmt_ctx);

    /* close output */
    if (ofmt_ctx && !(ofmt->flags & AVFMT_NOFILE))
        avio_closep(&ofmt_ctx->pb);
    avformat_free_context(ofmt_ctx);

    av_freep(&stream_mapping);

    if (ret < 0 && ret != AVERROR_EOF) {
        LOGE("Error occurred: %s\n", av_err2str(ret));
        return false;
    }

    return true;
}


int Rtsp::save_jpeg(AVFrame *pFrame, char *out_name) {

    int width = pFrame->width;
    int height = pFrame->height;
    AVCodecContext *pCodeCtx = NULL;


    AVFormatContext *pFormatCtx = avformat_alloc_context();
    // 设置输出文件格式
    pFormatCtx->oformat = av_guess_format("mjpeg", NULL, NULL);

    // 创建并初始化输出AVIOContext
    if (avio_open(&pFormatCtx->pb, out_name, AVIO_FLAG_READ_WRITE) < 0) {
        printf("Couldn't open output file.");
        return -1;
    }

    // 构建一个新stream
    AVStream *pAVStream = avformat_new_stream(pFormatCtx, 0);
    if (pAVStream == NULL) {
        return -1;
    }

    AVCodecParameters *parameters = pAVStream->codecpar;
    parameters->codec_id = pFormatCtx->oformat->video_codec;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->format = AV_PIX_FMT_YUVJ420P;
    parameters->width = pFrame->width;
    parameters->height = pFrame->height;

    AVCodec *pCodec = avcodec_find_encoder(pAVStream->codecpar->codec_id);

    if (!pCodec) {
        printf("Could not find encoder\n");
        return -1;
    }

    pCodeCtx = avcodec_alloc_context3(pCodec);
    if (!pCodeCtx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    if ((avcodec_parameters_to_context(pCodeCtx, pAVStream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return -1;
    }

    pCodeCtx->time_base = (AVRational) {1, 25};

    if (avcodec_open2(pCodeCtx, pCodec, NULL) < 0) {
        printf("Could not open codec.");
        return -1;
    }

    int ret = avformat_write_header(pFormatCtx, NULL);
    if (ret < 0) {
        printf("write_header fail\n");
        return -1;
    }

    int y_size = width * height;

    //Encode
    // 给AVPacket分配足够大的空间
    AVPacket pkt;
    av_new_packet(&pkt, y_size * 3);

    // 编码数据
    ret = avcodec_send_frame(pCodeCtx, pFrame);
    if (ret < 0) {
        printf("Could not avcodec_send_frame.");
        return -1;
    }

    // 得到编码后数据
    ret = avcodec_receive_packet(pCodeCtx, &pkt);
    if (ret < 0) {
        printf("Could not avcodec_receive_packet");
        return -1;
    }

    ret = av_write_frame(pFormatCtx, &pkt);

    if (ret < 0) {
        printf("Could not av_write_frame");
        return -1;
    }

    av_packet_unref(&pkt);

    //Write Trailer
    av_write_trailer(pFormatCtx);


    avcodec_close(pCodeCtx);
    avio_close(pFormatCtx->pb);
    avformat_free_context(pFormatCtx);

    return 0;
}



void Rtsp::stop() {
    recording = false;
}

