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
    int stream_index = -1;
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
        av_usleep(pkt.duration);
        LOGE("SLEEP:%lld", pkt.duration);

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
    // AvcFileReader::start_decode("/sdcard/Android/Data/com.taobao.android.mnndemo/cache/1641732312589.mp4", out_filename);
    return true;
}


bool Rtsp::playImage(const char *rtspUrl, const char *out_filename) {
    recording = true;
    int video_index = -1;
    int ret_av;
    AVPacket pkt;
    AVFormatContext *ifmt_ctx = nullptr;
    AVFrame *frame;
    AVCodecContext *codec_ctx = nullptr;
    AVStream *in_stream;
    int frame_count;
    AVCodec *codec;
    int ret_read;


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

    av_dump_format(ifmt_ctx, 0, rtspUrl, 0);

    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
        in_stream = ifmt_ctx->streams[i];
        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            video_index = i;
            break;
        }
    }

    codec = avcodec_find_decoder(in_stream->codecpar->codec_id);

    if (!codec) {
        LOGD("Could not open output file2 '%s'", out_filename);
        goto end;
    }
    codec_ctx = avcodec_alloc_context3(codec);

    ret_av = avcodec_parameters_to_context(codec_ctx, in_stream->codecpar);
    if (ret_av < 0) {
        LOGE("Failed to copy codec parameters to decoder context:%s:%d",
             av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret_av);
        return -1;
    }

    avcodec_open2(codec_ctx, codec, nullptr);

    frame = av_frame_alloc();

    if (!frame) {
        LOGE("Could not allocate video frame");
        goto end;
    }

    frame_count = 0;
    char buf[1024];
    while (recording) {
        ret_read = av_read_frame(ifmt_ctx, &pkt);
        if (ret_read < 0) {
            LOGE("av_read_frame failed");
            break;
        }

        if (pkt.stream_index == video_index) {
            int re = avcodec_send_packet(codec_ctx, &pkt);
            if (re < 0) {
                continue;
            }
            while (avcodec_receive_frame(codec_ctx, frame) == 0) {
                snprintf(buf, sizeof(buf), "%s/%05d.jpg", out_filename, frame_count);
                save_jpeg(frame, buf);
            }
            frame_count++;
        }
        av_packet_unref(&pkt);
    }

    end:
    avformat_close_input(&ifmt_ctx);
    return true;
}


int Rtsp::save_jpeg(AVFrame *pFrame, char *out_name) {

    int width = pFrame->width;
    int height = pFrame->height;
    AVCodecContext *pCodeCtx;
    int ret;

    AVFormatContext *pfmt_ctx = avformat_alloc_context();
    pfmt_ctx->oformat = av_guess_format("mjpeg", nullptr, nullptr);

    if (avio_open(&pfmt_ctx->pb, out_name, AVIO_FLAG_READ_WRITE) < 0) {
        printf("Couldn't open output file.");
        return -1;
    }

    AVStream *pAVStream = avformat_new_stream(pfmt_ctx, nullptr);
    if (pAVStream == nullptr) {
        return -1;
    }

    AVCodecParameters *parameters = pAVStream->codecpar;
    parameters->codec_id = pfmt_ctx->oformat->video_codec;
    parameters->codec_type = AVMEDIA_TYPE_VIDEO;
    parameters->format = AV_PIX_FMT_YUVJ420P;
//    parameters->format = AV_PIX_FMT_NV21;
    parameters->width = pFrame->width;
    parameters->height = pFrame->height;

    AVCodec *pCodec = avcodec_find_encoder(pAVStream->codecpar->codec_id);

    if (!pCodec) {
        LOGE("Could not find encoder");
        return -1;
    }

    pCodeCtx = avcodec_alloc_context3(pCodec);
    if (!pCodeCtx) {
        LOGE("Could not allocate video codec context");
        return -2;
    }

    if ((avcodec_parameters_to_context(pCodeCtx, pAVStream->codecpar)) < 0) {
        LOGE("Failed to copy %s codec parameters to decoder context",
             av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return -1;
    }

    pCodeCtx->time_base = (AVRational) {1, 25};

    ret = avcodec_open2(pCodeCtx, pCodec, nullptr);
    if (ret < 0) {
        LOGE("Could not open codec:%d", ret);
        return -2;
    }

    ret = avformat_write_header(pfmt_ctx, nullptr);
    if (ret < 0) {
        LOGE("write_header fail");
        return -3;
    }

    int y_size = width * height;

    AVPacket pkt;
    av_new_packet(&pkt, y_size * 3);

    ret = avcodec_send_frame(pCodeCtx, pFrame);
    if (ret < 0) {
        LOGE("Could not avcodec_send_frame.");
        return -1;
    }

    // 得到编码后数据
    ret = avcodec_receive_packet(pCodeCtx, &pkt);
    if (ret < 0) {
        LOGE("Could not avcodec_receive_packet");
        return -1;
    }

//    LOGW("pkt.size:%d", pkt.size);
    LOGW("pkt.size:%d", pkt.buf->size);




//    MNNInterpreter::getInstance().testRunSession();

    ret = av_write_frame(pfmt_ctx, &pkt);

    if (ret < 0) {
        LOGE("Could not av_write_frame");
        return -1;
    }

    av_packet_unref(&pkt);

    //Write Trailer
    av_write_trailer(pfmt_ctx);


    avcodec_close(pCodeCtx);
    avio_close(pfmt_ctx->pb);
    avformat_free_context(pfmt_ctx);

    return 0;
}


bool Rtsp::swsScale(const char *rtspUrl, const char *out_filename) {
    recording = true;
    int video_index = -1;
    int ret_av;
    AVPacket dec_pkt;
    AVFormatContext *ifmt_ctx = nullptr;
    AVFrame *dec_frame;
    AVCodecContext *dec_context;
//    AVStream *in_stream;
    int frame_count;
    AVCodec *dec_codec = nullptr;
    int ret_read;
    int ret;
    AVPixelFormat opt_pxfmt = AV_PIX_FMT_RGB24;


    uint8_t *src_data[4], *dst_data[4];
    int src_linesize[4], dst_linesize[4];
    int src_w, src_h, dst_w, dst_h;
    enum AVPixelFormat src_pix_fmt = AV_PIX_FMT_YUV420P, dst_pix_fmt = AV_PIX_FMT_RGB24;
    SwsContext *conversionContext;

    /* scale variables */
    AVFrame *scaled_frame;
    SwsContext *scaleContext;

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

    /* select the video stream */
    ret = av_find_best_stream(ifmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, &dec_codec, 0);
    if (ret < 0) {
        av_log(nullptr, AV_LOG_ERROR, "Cannot find a video stream in the input file");
        return ret;
    }
    video_index = ret;
    dec_codec = avcodec_find_decoder(ifmt_ctx->streams[video_index]->codecpar->codec_id);


//    for (int i = 0; i < ifmt_ctx->nb_streams; i++) {
//        AVStream *in_stream = ifmt_ctx->streams[i];
//        if (in_stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
//            video_index = i;
//            dec_codec = avcodec_find_decoder(in_stream->codecpar->codec_id);
//            break;
//        }
//    }


    if (!dec_codec) {
        LOGD("Could not open output file2 '%s'", out_filename);
        goto end;
    }

    dec_context = avcodec_alloc_context3(dec_codec);

    ret_av = avcodec_parameters_to_context(dec_context, ifmt_ctx->streams[video_index]->codecpar);

    if (ret_av < 0) {
        LOGE("Failed to copy codec parameters to decoder context:%s:%d",
             av_get_media_type_string(AVMEDIA_TYPE_VIDEO), ret_av);
        return -1;
    }


//    avcodec_get_context_defaults3(dec_context, dec_codec);
    /*
  avcodec_get_context_defaults3 :
  Set the fields of the given AVCodecContext to default values corresponding to
  the given codec (defaults may be codec-dependent).
  */

//    dec_context->width = unscaled_width;
//    dec_context->height = unscaled_height;
    dec_context->pix_fmt = opt_pxfmt;


    ret = avcodec_open2(dec_context, dec_codec, nullptr);
    if (ret < 0) {
        LOGE("Could not avcodec_open2");
        goto end;
    }

    dec_frame = av_frame_alloc();

    if (!dec_frame) {
        LOGE("Could not allocate video frame");
        goto end;
    }

    src_w = dst_w = dec_context->width;
    src_h = dst_h = dec_context->height;

    LOGW("IMAGE[%dx%d]", src_w, src_h);

    /* create scaling context */
    conversionContext = sws_getContext(src_w, src_h, src_pix_fmt,
                                       dst_w, dst_h, dst_pix_fmt,
                                       SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!conversionContext) {
        LOGE("Impossible to create scale context for the conversion "
             "fmt:%s s:%dx%d -> fmt:%s s:%dx%d",
             av_get_pix_fmt_name(src_pix_fmt), src_w, src_h,
             av_get_pix_fmt_name(dst_pix_fmt), dst_w, dst_h);
        goto end;
    }

    /* allocate source and destination image buffers */
    if (av_image_alloc(src_data, src_linesize,
                       src_w, src_h, src_pix_fmt, 16) < 0) {
        LOGE("Could not allocate source image");
        goto end;
    }

    /* buffer is going to be written to rawvideo file, no alignment */
    if (av_image_alloc(dst_data, dst_linesize, dst_w, dst_h, dst_pix_fmt, 1) < 0) {
        LOGE("Could not allocate destination image");
        goto end;
    }

    /* read all packets */
    while (recording) {
        if (av_read_frame(ifmt_ctx, &dec_pkt) < 0) break;

        if (dec_pkt.stream_index == video_index) {
            ret = avcodec_send_packet(dec_context, &dec_pkt);
            if (ret < 0) {
                av_log(nullptr, AV_LOG_ERROR, "Error while sending a packet to the decoder\n");
                break;
            }

            while (ret >= 0) {
                ret = avcodec_receive_frame(dec_context, dec_frame);
                if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
                    break;
                } else if (ret < 0) {
                    av_log(nullptr, AV_LOG_ERROR, "Error while receiving a frame from the decoder");
                    goto end;
                }

                dec_frame->pts = dec_frame->best_effort_timestamp;

                LOGW("frame:%d %d %d %d %d", dec_frame->width, dec_frame->height,
                     dec_frame->key_frame,
                     dec_frame->format, dec_frame->data[0][100]);

                if (!MNNInterpreter::getInstance().isRunSession()) {
                    MNN::CV::ImageProcess::Config config;
                    config.filterType = MNN::CV::NEAREST;
                    float mean[3] = {103.94f, 116.78f, 123.68f};
                    float normals[3] = {0.017f, 0.017f, 0.017f};
                    ::memcpy(config.mean, mean, sizeof(mean));
                    ::memcpy(config.normal, normals, sizeof(normals));
                    config.sourceFormat = MNN::CV::RGB;
                    config.destFormat = MNN::CV::BGR;
                    std::shared_ptr<MNN::CV::ImageProcess> pretreat(
                            MNN::CV::ImageProcess::create(config));
                    MNN::Tensor *input = MNNInterpreter::getInstance().getSessionInput(nullptr);
                    if (input) {
//                        auto dims = input->shape();
//                        int size_h = dims[2];
//                        int size_w = dims[3];
//                        MNN::CV::Matrix trans;
//                        //Dst -> [0, 1]
//                        trans.postScale(1.0 / size_w, 1.0 / size_h);
//                        //Flip Y  （因为 FreeImage 解出来的图像排列是Y方向相反的）
//                        trans.postScale(1.0, -1.0, 0.0, 0.5);
//                        //[0, 1] -> Src
//                        trans.postScale(dec_frame->width, dec_frame->height);
//                        pretreat->setMatrix(trans);
//                        pretreat->convert(dec_frame->data[0], input->width(), input->height(), 0,
//                                          input);


                        LOGD("ZZ");
                        for (int i = 0; i < input->width() * input->height(); ++i) {
                            input->host<int>()[i] = 11;
                        }
                        LOGD("CC");

                        MNNInterpreter::getInstance().testRunSession();
                    } else {
                        LOGD("输入未就绪");
                    }
                }

                /* push the decoded frame into the filtergraph */
//                if (av_buffersrc_add_frame_flags(buffersrc_ctx, frame, AV_BUFFERSRC_FLAG_KEEP_REF) < 0) {
//                    av_log(NULL, AV_LOG_ERROR, "Error while feeding the filtergraph\n");
//                    break;
//                }
//
//                /* pull filtered frames from the filtergraph */
//                while (1) {
//                    ret = av_buffersink_get_frame(buffersink_ctx, filt_frame);
//                    if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
//                        break;
//                    if (ret < 0)
//                        goto end;
//                    display_frame(filt_frame, buffersink_ctx->inputs[0]->time_base);
//                    av_frame_unref(filt_frame);
//                }
                av_frame_unref(dec_frame);
//                av_usleep(10 * 1000L);
                LOGD("****************************************");
            }
        }
        av_packet_unref(&dec_pkt);
//        av_usleep(10 * 1000L);
    }
    end:
    avformat_close_input(&ifmt_ctx);
    return true;
}


void Rtsp::fill_yuv_image(uint8_t *data[4], const int linesize[4],
                          int width, int height, int frame_index) {
    int x, y;

    /* Y */
    for (y = 0; y < height; y++)
        for (x = 0; x < width; x++)
            data[0][y * linesize[0] + x] = x + y + frame_index * 3;

    /* Cb and Cr */
    for (y = 0; y < height / 2; y++) {
        for (x = 0; x < width / 2; x++) {
            data[1][y * linesize[1] + x] = 128 + y + frame_index * 2;
            data[2][y * linesize[2] + x] = 64 + x + frame_index * 5;
        }
    }
}


void Rtsp::stop() {
    recording = false;
}

