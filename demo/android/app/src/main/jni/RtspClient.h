//
// Created by sjh on 2021/12/28.
//

#ifndef FFMPEG_ANDROID_RTSPCLIENT_H
#include "ALog.h"
extern "C" {
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libswscale/swscale.h"
}
#define FFMPEG_ANDROID_RTSPCLIENT_H


class RtspClient {
private:
    bool recording = false;
    RtspClient();
    ~RtspClient();
public:
    bool play(const char *rtspUrl, const char *string);
    void stop();
    static RtspClient& getInstance()
    {
        static RtspClient instance;
        return instance;
    }
    RtspClient(RtspClient const&) = delete;
    void operator=(RtspClient const&) = delete;
};


#endif //FFMPEG_ANDROID_RTSPCLIENT_H
