//
// Created by mattb on 09.11.21.
//

#ifndef SCREENAUDIOCAPTURE_SCREENAUDIOCAPTURE_H
#define SCREENAUDIOCAPTURE_SCREENAUDIOCAPTURE_H

#ifdef _WIN32
#include "ListAVDevices.h"
#endif

#include "ffmpeg.h"
#include <iostream>
#include <string>
#include <cstdint>
#include <thread>
#include <atomic>
#include <utility>
#include <mutex>
#include <condition_variable>

class ScreenAudioCapture
{

private:
    enum RecordState
    {
        NotStarted,
        Started,
        Paused,
        Stopped,
        Finished,
    };
    std::atomic<RecordState> state;
    std::string outFile;
    std::string deviceName;
    std::string failReason;

    AVInputFormat *audioInputFormat;
    AVDictionary *audioOptions;

    AVFormatContext *audioInFormatCtx{};
    AVStream *audioInStream{};
    AVCodecContext *audioInCodecCtx{};

    AVFormatContext *videoInFormatCtx;
    AVStream *videoInStream;
    AVCodecContext *videoInCodecCtx;

    SwrContext *audioConverter{};
    SwsContext *videoConverter;
    AVAudioFifo *audioFifo{};
    AVFifoBuffer *videoFifo;

    AVFormatContext *outFormatCtx{};
    AVStream *audioOutStream{};
    AVCodecContext *audioOutCodecCtx{};
    AVStream *videoOutStream;
    AVCodecContext *videoOutCodecCtx;

    std::mutex mtxAudioBuffer{};
    std::condition_variable cvAudioBufferNotEmpty{};
    std::condition_variable cvAudioBufferNotFull{};

    std::mutex mtxVideoBuffer;
    std::condition_variable cvVideoBufferNotEmpty;
    std::condition_variable cvVideoBufferNotFull;

    std::mutex mtxPause;
    std::condition_variable cvNotPause;

    uint64_t numberOfSamples;
    int audioIndex;
    int videoIndex;
    int audioOutIndex;
    int videoOutIndex;
    uint64_t audioCurrentPts;
    uint64_t videoCurrentPts;

    AVFrame *videoOutFrame;
    uint8_t *videoOutFrameBuffer;
    int videoOutFrameSize;
    bool doneSomething = false;

    int width;
    int height;
    int widthOffset;
    int heightOffset;

    bool fatal = false;

    void SoundRecordThreadProc();
    void ScreenRecordThreadProc();
    void MuxThreadProc();
    void FlushAudioDecoder();
    void FlushVideoDecoder();
    int *FlushMuxer();
    void LogStatus();

public:
    ScreenAudioCapture(std::string filePath, std::string device) : outFile(std::move(filePath)), deviceName(std::move(device))
    {
        state.store(NotStarted, std::memory_order_release);
    }

    void Start();
    void Stop();
    void Pause();
    void Resume();
    void OpenAudio();
    void OpenOutput();
    void InitAudioBuffer();
    void InitVideoBuffer();
    void OpenVideo();
    void Release();
    bool hasFinished() { return this->state == Finished; }

    std::string GetLastError() { return failReason; }

    bool DoneSomething() { return doneSomething; }

    void SetDimensions(int videoWidth, int videoWidthOffset, int videoHeight, int videoHeightOffset)
    {
        this->width = videoWidth;
        this->widthOffset = videoWidthOffset;
        this->height = videoHeight;
        this->heightOffset = videoHeightOffset;
    }

    void PrintDimensions()
    {
        std::cout << "Width: " << width << std::endl;
        std::cout << "Width offset: " << widthOffset << std::endl;
        std::cout << "Height: " << height << std::endl;
        std::cout << "Height offset: " << heightOffset << std::endl;
    }

    bool WasFatal()
    {
        return fatal;
    }
};

#endif //SCREENAUDIOCAPTURE_SCREENAUDIOCAPTURE_H
