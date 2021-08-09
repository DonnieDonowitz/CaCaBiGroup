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
        Stopped,
        Unknown
    };

    ScreenRecord();

    void Init();
    void Start();
    void Pause();
    void Stop();
    bool GetFinito();

private:
    void ScreenRecordThreadProc();
    void ScreenRecordAcquireThread();
    int OpenVideo();
    int OpenOutput();
    void SetEncoderParams();
    void FlushDecoder();
    void FlushEncoder();
    void InitBuffer();
    void Release();
    void SetFinito();

private:
    std::string m_filePath;
    int m_width;
    int m_height;
    int m_fps;
    int m_vIndex;
    int m_vOutIndex;
    AVFormatContext* m_vFmtCtx;
    AVFormatContext* m_oFmtCtx;
    AVCodecContext* m_vDecodeContext;
    AVCodecContext* m_vEncodeContext;
    AVDictionary* m_dict;
    SwsContext* m_swsCtx;
    AVFifoBuffer* m_vFifoBuf;
    AVFrame* m_vOutFrame;
    uint8_t* m_vOutFrameBuf;
    int m_vOutFrameSize;
    RecordState m_state;
    std::condition_variable m_cvNotFull;
    std::condition_variable m_cvNotEmpty;
    std::mutex m_mtx;
    std::condition_variable m_cvNotPause;
    std::mutex m_mtxPause;
    bool finito = false;
};