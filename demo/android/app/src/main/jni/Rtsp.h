//
// Created by sjh on 2021/12/28.
//

#ifndef FFMPEG_ANDROID_RTSP_H
#define INBUF_SIZE 4096

#include "ALog.h"
#include "AvcFileReader.h"
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
#include <libavutil/timestamp.h>
#include <libavutil/imgutils.h>
#include <libavfilter/buffersrc.h>
#include <libavutil/time.h>
}
#include "MNNInterpreter.h"

#define FFMPEG_ANDROID_RTSP_H


class Rtsp {
private:
    bool recording = false;

    Rtsp();

    ~Rtsp();

public:
    bool play(const char *rtspUrl, const char *string);
    bool playImage(const char *rtspUrl, const char *string);
    bool swsScale(const char *rtspUrl, const char *string);
    static int save_jpeg(AVFrame *pFrame, char *out_name);
    static void fill_yuv_image(uint8_t *data[4], const int linesize[4],
                               int width, int height, int frame_index);

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
