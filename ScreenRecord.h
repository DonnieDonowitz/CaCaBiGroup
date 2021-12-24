#pragma once

#include "ffmpeg.h"

extern "C"
{
    struct AVFormatContext;
    struct AVCodecContext;
    struct AVCodec;
    struct AVFifoBuffer;
    struct AVAudioFifo;
    struct AVFrame;
    struct SwsContext;
    struct SwrContext;
    struct AVInputFormat;
};

class ScreenRecord
{
private:
    enum RecordState {
        NotStarted,
        Started,
        Paused,
        Stopped,
        Finished,
    };

public:
    ScreenRecord(std::string path, std::string video, std::string audio, bool isAudioOn) :
      fps(30), videoIndex(-1), audioIndex(-1)
    , videoFormatContext(nullptr), audioFormatContext(nullptr)
    , outFormatContext(nullptr)
    , videoDecodeContext(nullptr), audioDecodeContext(nullptr)
    , videoEncodeContext(nullptr), audioEncodeContext(nullptr)
    , videoFifoBuffer(nullptr), audioFifoBuffer(nullptr)
    , swsContext(nullptr), swrContext(nullptr)
    , state(RecordState::NotStarted)
    , videoCurrentPts(0), audioCurrentPts(0)
    {
        av_log_set_level(AV_LOG_ERROR);
        filePath= path;
        audioBitrate = 128000;
        videoDevice = video;
        audioDevice = audio;
        recordAudio = isAudioOn;
    }

    void Start();
    void Pause();
    void Stop();
    void Resume();

    bool hasFinished()          { return state == RecordState::Finished; }

    bool wasFatal()             { return fatal; }

    void SetDimensions(int w, int wo, int h, int ho)
    {
        width = w;
        height = h;
        widthOffset = wo;
        heightOffset = ho;
    }

    void PrintDimensions()
    {
        std::cout << "Width: " << width << std::endl;
        std::cout << "Width Offset: " << widthOffset << std::endl;
        std::cout << "Height: " << height << std::endl;
        std::cout << "Height Offset: " << heightOffset << std::endl;
    }

private:
    void            MuxThreadProc();
    void            ScreenRecordThreadProc();
    void            SoundRecordThreadProc();

    void            OpenVideo();
    void            OpenAudio();
    void            OpenOutput();
    void            LogStatus();

    AVFrame*        AllocAudioFrame(AVCodecContext* c, int nbSamples);
    void            InitVideoBuffer();
    void            InitAudioBuffer();

    void            FlushVideoDecoder();
    void            FlushAudioDecoder();
    int*            FlushEncoders();

    void            Release();

private:
    std::string                 filePath;
    std::string                 audioDevice;
    std::string                 videoDevice;
    int                         width;
    int                         height;
    int                         widthOffset;
    int                         heightOffset;
    int                         fps;
    int                         audioBitrate;

    int                         videoIndex;       
    int                         audioIndex;       
    int                         videoOutIndex;   
    int                         audioOutIndex; 

    AVFormatContext*            videoFormatContext;
    AVFormatContext*            audioFormatContext;
    AVFormatContext*            outFormatContext;

    AVCodecContext*             videoDecodeContext;
    AVCodecContext*             audioDecodeContext;
    AVCodecContext*             videoEncodeContext;
    AVCodecContext*             audioEncodeContext;
    SwsContext*                 swsContext;
    SwrContext*                 swrContext;
    AVFifoBuffer*               videoFifoBuffer;
    AVAudioFifo*                audioFifoBuffer;
    AVInputFormat*              audioInputFormat;

    bool                        fatal;
    bool                        recordAudio;

    AVFrame*                    videoOutFrame;
    uint8_t*                    videoOutFrameBuffer;
    int                         videoOutFrameSize;

    int                         numberOfSamples;
    
    RecordState                 state;

    std::condition_variable     cvNotPause;  
    std::mutex                  mutexPause;

    std::condition_variable     cvVideoBufferNotFull;
    std::condition_variable     cvVideoBufferNotEmpty;
    std::mutex                  mutexVideoBuffer;

    std::condition_variable     cvAudioBufferNotFull;
    std::condition_variable     cvAudioBufferNotEmpty;
    std::mutex                  mutexAudioBuffer;

    int64_t                     videoCurrentPts;
    int64_t                     audioCurrentPts;
};