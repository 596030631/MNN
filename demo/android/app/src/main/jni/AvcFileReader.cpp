//
// Created by Administrator on 2022/1/9 0009.
//
#include "AvcFileReader.h"

AvcFileReader::AvcFileReader() = default;

AvcFileReader::~AvcFileReader() = default;

void AvcFileReader::pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                             char *filename) {
    FILE *f;
    int i;

    LOGW("%s", filename);
    f = fopen(filename, "w");
    LOGD("P5 %d %d %d", xsize, ysize, 255);
    for (i = 0; i < ysize; i++) {
        fwrite(buf + i * wrap, 1, xsize, f);
    }
    fclose(f);
}

void AvcFileReader::decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                           const char *filename) {
    char buf[1024];
    int ret;
    LOGW("............");
    ret = avcodec_send_packet(dec_ctx, pkt);
    if (ret < 0) {
        LOGE("Error sending a packet for decoding");
        return;
    }

    while (ret >= 0) {
        ret = avcodec_receive_frame(dec_ctx, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            LOGW("avcodec_receive_frame error %d", ret);
            return;
        }

        else if (ret < 0) {
            LOGE("Error during decoding\n");
            return;
        }

        LOGE("saving frame %3d", dec_ctx->frame_number);
        fflush(stdout);

        /* the picture is allocated by the decoder. no need to
           free it */
        snprintf(buf, sizeof(buf), "%s-%d", filename, dec_ctx->frame_number);
        pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);
    }
}


bool AvcFileReader::start_decode(const char *filename, const char *outfilename) {
    const AVCodec *codec;
    AVCodecParserContext *parser;
    AVCodecContext *c = nullptr;
    FILE *f;
    AVFrame *frame;
    uint8_t inbuf[INBUF_SIZE + AV_INPUT_BUFFER_PADDING_SIZE];
    uint8_t *data;
    size_t data_size;
    int ret;
    AVPacket *pkt;

    pkt = av_packet_alloc();
    if (!pkt) {
        LOGE("av_packet_alloc failed");
        return false;
    }
    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    memset(inbuf
           + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);

    /* find the MPEG-1 video decoder */
    codec = avcodec_find_decoder(AV_CODEC_ID_H264);
    if (!codec) {
        LOGE("Codec not found");
        return false;
    }

    parser = av_parser_init(codec->id);
    if (!parser) {
        LOGD("parser not found\n");
        return false;
    }

    c = avcodec_alloc_context3(codec);
    if (!c) {
        LOGE("Could not allocate video codec context");
        return false;
    }

    /* For some codecs, such as msmpeg4 and mpeg4, width and height
     * MUST be initialized there because this information is not
     * available in the bitstream.
     */

    /* open it */
    if (avcodec_open2(c, codec, nullptr) < 0) {
        LOGD("Could not open codec");
        return false;
    }

    f = fopen(filename, "rb");
    if (!f) {
        LOGE("Could not open %s", filename);
        return false;
    }

    frame = av_frame_alloc();
    if (!frame) {
        LOGE("Could not allocate video frame");
        return false;
    }

    while (!feof(f)) {
        /* read raw data from the input file */
        data_size = fread(inbuf, 1, INBUF_SIZE, f);
        LOGE("IIIIIIII:%d", data_size);
        if (!data_size) {
            LOGE("IIIIIIII BREAK :%d", data_size);
            break;
        }

        /* use the parser to split the data into frames */
        data = inbuf;
        while (data_size > 0) {
            ret = av_parser_parse2(parser, c, &pkt->data, &pkt->size,
                                   data, data_size, AV_NOPTS_VALUE, AV_NOPTS_VALUE, 0);
            LOGE("Result :%d", ret);

            if (ret < 0) {
                LOGE("Error while parsing");
                return false;
            }
            data += ret;
            data_size -= ret;

            LOGW("SSSSSSSSSSSSSSSSSSSS：%d", pkt->size);

            if (pkt->size) {
                LOGW("Decode Frame");
                decode(c, frame, pkt, outfilename);
            }
        }
    }

    /* flush the decoder */
    LOGW("Decode Frame End");

    decode(c, frame, nullptr, outfilename);

    fclose(f);

    av_parser_close(parser);
    avcodec_free_context(&c);
    av_frame_free(&frame);
    av_packet_free(&pkt);
    return true;
}
