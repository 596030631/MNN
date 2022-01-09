//
// Created by sjh on 2021/12/28.
//

#ifndef FFMPEG_ANDROID_RTSP_H
#define INBUF_SIZE 4096

#include "ALog.h"

extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include <libavutil/timestamp.h>
}

#define FFMPEG_ANDROID_RTSP_H


class Rtsp {
private:
    bool recording = false;

    Rtsp();

    ~Rtsp();

public:
    bool play(const char *rtspUrl, const char *string);

    void stop();

    static Rtsp &getInstance() {
        static Rtsp instance;
        return instance;
    }

    static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag);

    Rtsp(Rtsp const &) = delete;

    void operator=(Rtsp const &) = delete;
};


#endif //FFMPEG_ANDROID_RTSP_H
