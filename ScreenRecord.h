#pragma once

#ifdef _WIN32
    #include <Windows.h>
#endif
#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef	__cplusplus
extern "C"
{
#endif
    struct AVFormatContext;
    struct AVCodecContext;
    struct AVCodec;
    struct AVFifoBuffer;
    struct AVAudioFifo;
    struct AVFrame;
    struct SwsContext;
    struct SwrContext;
#ifdef __cplusplus
};
#endif

class ScreenRecord
{
private:
    enum RecordState {
        NotStarted,
        Started,
        Paused,
        Stopped,
        Unknown,
    };
public:
    ScreenRecord();
    void Init(std::string path, int width, int height);
    bool isDone;
    void Start();
    void Pause();
    void Stop();

private:
    void MuxThreadProc();
    void ScreenRecordThreadProc();
    void SoundRecordThreadProc();
    int OpenVideo();
    int OpenAudio();
    int OpenOutput();
    std::string GetSpeakerDeviceName();
    std::string GetMicrophoneDeviceName();
    AVFrame* AllocAudioFrame(AVCodecContext* c, int nbSamples);
    void InitVideoBuffer();
    void InitAudioBuffer();
    void FlushVideoDecoder();
    void FlushAudioDecoder();
    void FlushEncoders();
    void Release();

private:
    std::string                 m_filePath;
    int                         m_width;
    int                         m_height;
    int                         m_fps;
    int                         m_audioBitrate;
    int                         m_vIndex;       
    int                         m_aIndex;       
    int                         m_vOutIndex;   
    int                         m_aOutIndex;    
    AVFormatContext*            m_vFmtCtx;
    AVFormatContext*            m_aFmtCtx;
    AVFormatContext*            m_oFmtCtx;
    AVCodecContext*             m_vDecodeCtx;
    AVCodecContext*             m_aDecodeCtx;
    AVCodecContext*             m_vEncodeCtx;
    AVCodecContext*             m_aEncodeCtx;
    SwsContext*                 m_swsCtx;
    SwrContext*                 m_swrCtx;
    AVFifoBuffer*               m_vFifoBuf;
    AVAudioFifo*                m_aFifoBuf;

    AVFrame*                    m_vOutFrame;
    uint8_t*                    m_vOutFrameBuf;
    int                         m_vOutFrameSize;

    int                         m_nbSamples;
    RecordState                 m_state;
    std::condition_variable     m_cvNotPause;  
    std::mutex                  m_mtxPause;
    std::condition_variable     m_cvVBufNotFull;
    std::condition_variable     m_cvVBufNotEmpty;
    std::mutex                  m_mtxVBuf;
    std::condition_variable     m_cvABufNotFull;
    std::condition_variable     m_cvABufNotEmpty;
    std::mutex                  m_mtxABuf;
    int64_t                     m_vCurPts;
    int64_t                     m_aCurPts;
};