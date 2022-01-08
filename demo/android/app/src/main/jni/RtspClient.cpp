#include "RtspClient.h"

RtspClient::RtspClient() = default;

RtspClient::~RtspClient() = default;

bool RtspClient::play(const char *rtspUrl, const char *pathName) {
    recording = true;

//    SwsContext *img_convert_ctx;
    AVFormatContext *context = avformat_alloc_context();
    AVCodecContext *pContext = avcodec_alloc_context3(NULL);
    int video_stream_index = -1;

    av_register_all();
    avformat_network_init();

    AVDictionary *option = NULL;
    av_dict_set(&option, "rtsp_transport", "tcp", 0);

    // Open RTSP
    if (int err = avformat_open_input(&context, rtspUrl, NULL, &option) != 0) {
        LOGE("Cannot open input %s, error code: %d", rtspUrl, err);
        return false;
    }

    av_dict_free(&option);

    if (avformat_find_stream_info(context, NULL) < 0) {
        LOGE("Cannot find stream info");
        return false;
    }

    // Search video stream
    for (int i = 0; i < context->nb_streams; i++) {
        if (context->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO)
            video_stream_index = i;
    }

    if (video_stream_index == -1) {
        __android_log_print(ANDROID_LOG_ERROR, TAG, "Video stream not found");
        return false;
    }

    AVPacket packet;
    av_init_packet(&packet);

    // Open output file
    AVFormatContext *oc = avformat_alloc_context();
    AVStream *stream = NULL;

    // Start reading packets from stream and write them to file
    av_read_play(context);

    AVCodec *codec = NULL;
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOGE("Cannot find decoder H264");
        return false;
    }

    avcodec_get_context_defaults3(pContext, codec);
    avcodec_copy_context(pContext, context->streams[video_stream_index]->codec);

    if (avcodec_open2(pContext, codec, NULL) < 0) {
        LOGE("Cannot open codec");
        return false;
    }

//    img_convert_ctx = sws_getContext(pContext->width, pContext->height, pContext->pix_fmt,
//                                     pContext->width, pContext->height,
//                                     AV_PIX_FMT_RGB24, SWS_BICUBIC, NULL, NULL, NULL);

    auto size = (size_t) avpicture_get_size(AV_PIX_FMT_YUV420P, pContext->width,
                                            pContext->height);
    auto *picture_buf = (uint8_t *) (av_malloc(size));
    AVFrame *pic = av_frame_alloc();
    AVFrame *picrgb = av_frame_alloc();
    auto size2 = (size_t) avpicture_get_size(AV_PIX_FMT_RGB24, pContext->width, pContext->height);
    auto *picture_buf2 = (uint8_t *) (av_malloc(size2));
    avpicture_fill((AVPicture *) pic, picture_buf, AV_PIX_FMT_YUV420P, pContext->width,
                   pContext->height);
    avpicture_fill((AVPicture *) picrgb, picture_buf2, AV_PIX_FMT_RGB24, pContext->width,
                   pContext->height);

    auto f = fopen(pathName, "wb+");

    timeval timeout{};
    const int millisecond = 10;
    timeout.tv_sec = millisecond / 1000;
    timeout.tv_usec = millisecond % 1000 * 1000;

    while (recording && av_read_frame(context, &packet) >= 0) {
        if (packet.stream_index == video_stream_index) { // Packet is video
            if (stream == NULL) {
                stream = avformat_new_stream(oc,
                                             context->streams[video_stream_index]->codec->codec);
                avcodec_copy_context(stream->codec, context->streams[video_stream_index]->codec);
                stream->sample_aspect_ratio = context->streams[video_stream_index]->codec->sample_aspect_ratio;
            }

//            int check = 0;
//            packet.stream_index = stream->id;
//            LOGD("packet:%d", packet.size);

            fwrite(packet.data, 1, packet.size, f);


//            avcodec_decode_video2(pContext, pic, &check, &packet);

//            sws_scale(img_convert_ctx, (const uint8_t *const *) pic->data, pic->linesize, 0,
//                      pContext->height, picrgb->data, picrgb->linesize);
//
//            LOGD("width:%d  height:%d", pContext->width, pContext->height);
//            callback(env, picture_buf2, 3, pContext->width, pContext->height);
        }

//        select(0, NULL, NULL, NULL, &timeout);

    }
    av_free_packet(&packet);
//    av_init_packet(&packet);
    fflush(f);
    fclose(f);

    av_free(pic);
    av_free(picrgb);
    av_free(picture_buf);
    av_free(picture_buf2);

    av_read_pause(context);
    avio_close(oc->pb);
    avformat_free_context(oc);
    avformat_close_input(&context);
    return true;
}

void RtspClient::stop() {
    recording = false;
}
