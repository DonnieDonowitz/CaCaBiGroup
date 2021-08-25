#pragma once

#ifdef __cplusplus
extern "C" {
#endif
    struct AVFormatContext;
    struct AVCodecContext;
    struct AVCodec;
    struct AVFifoBuffer;
    struct AVAudioFifo;
    struct AVFrame;
    struct SwsContext;
    struct SwrContext;
    struct AVDictionary;
#ifdef __cplusplus
};
#endif
#include <iostream>
#include <condition_variable>
#include <mutex>
#include <atomic>

class ScreenRecord {

public:
    enum RecordState {
        NotStarted,
        Started,
        Paused,
        Stopped
    };

    ScreenRecord();

    void    Init();
    void    Start();
    void    Pause();
    void    Stop();
    bool    GetFinished();

private:
    void    ScreenRecordThreadProc();
    void    ScreenRecordAcquireThread();
    int     OpenVideo();
    int     OpenOutput();
    void    SetEncoderParams();
    void    FlushDecoder();
    void    FlushEncoder();
    void    InitBuffer();
    void    Release();
    void    ShowStartMenu();
    void    SetFinished();

private:
    std::string             filePath;
    int                     width, height, fps, vIndex, vOutIndex, vOutFrameSize;
    bool                    finished;
    uint8_t*                vOutFrameBuf;
    AVFormatContext*        vFmtCtx;
    AVFormatContext*        oFmtCtx;
    AVCodecContext*         vDecodeContext;
    AVCodecContext*         vEncodeContext;
    AVDictionary*           dict;
    SwsContext*             swsCtx;
    AVFifoBuffer*           vFifoBuf;
    AVFrame*                vOutFrame;
    RecordState             state;
    std::condition_variable cvNotFull;
    std::condition_variable cvNotEmpty;
    std::condition_variable cvNotPause;
    std::mutex              mtx;
    std::mutex              mtxPause;
};