//
// Created by Administrator on 2022/1/9 0009.
//

#ifndef ANDROID_AVCFILEREADER_H
#define ANDROID_AVCFILEREADER_H
#define INBUF_SIZE 4096
#include "ALog.h"
extern "C" {
#include "libavcodec/avcodec.h"
}

class AvcFileReader {
private:

public:
    AvcFileReader();
    ~AvcFileReader();
    static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                                 char *filename);
    static void decode(AVCodecContext *dec_ctx, AVFrame *frame, AVPacket *pkt,
                               const char *filename);
    static bool start_decode(const char *filename, const char *outfilename);
};
#endif //ANDROID_AVCFILEREADER_H
