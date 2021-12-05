//
// Created by mattb on 09.11.21.
//

#include "ScreenAudioCapture.h"
#include <iostream>

void ScreenAudioCapture::Start()
{
    if (state.load(std::memory_order_acquire) == NotStarted)
    {
        OpenAudio();
        OpenVideo();
        OpenOutput();
        InitAudioBuffer();
        InitVideoBuffer();
        doneSomething = true;
        std::cout << "Finished setting things up." << std::endl
                  << std::endl;
        std::cout << "Start recording..." << std::endl
                  << std::endl;
        try
        {
            std::thread muxThread(&ScreenAudioCapture::MuxThreadProc, this);
            muxThread.detach();
        }
        catch (std::exception e)
        {
            this->failReason = e.what();
        }
        state.store(Started, std::memory_order_release);
    }
    else if (state.load(std::memory_order_acquire) == Started)
    {
        throw std::runtime_error("Already recording. Maybe you meant 'pause' or 'stop'. Try again.");
    }
    else if (state.load(std::memory_order_acquire) == Paused)
    {
        throw std::runtime_error("Already started, recording is on pause. Maybe you meant 'resume' or 'stop'. Try again.");
    }
    else
    {
        throw std::runtime_error("Invalid command. Try again.");
    }
}

void ScreenAudioCapture::Resume()
{
    if (state.load(std::memory_order_acquire) == Paused)
    {
        std::cout << "Resume recording..." << std::endl
                  << std::endl;
        state.store(Started, std::memory_order_release);
        avformat_open_input(&audioInFormatCtx, deviceName.c_str(), audioInputFormat, &audioOptions);
        cvNotPause.notify_all();
    }
    else if (state.load(std::memory_order_acquire) == NotStarted)
    {
        throw std::runtime_error("Nothing to resume, recording has not started yet. Maybe you meant 'start' or 'stop'. Try again.");
    }
    else if (state.load(std::memory_order_acquire) == Started)
    {
        throw std::runtime_error("Already recording. Maybe you meant 'pause' or 'stop'. Try again.");
    }
    else
    {
        throw std::runtime_error("Invalid command. Try again.");
    }
}

void ScreenAudioCapture::Pause()
{
    if (state.load(std::memory_order_acquire) != Started)
    {
        if (state.load(std::memory_order_acquire) == Paused)
        {
            throw std::runtime_error("Already paused. Maybe you meant 'resume' or 'stop'. Try again.");
        }
        else if (state.load(std::memory_order_acquire) == NotStarted)
        {
            throw std::runtime_error("Nothing to pause, recording has not started yet. Maybe you meant 'start' or 'stop'. Try again.");
        }
        else
        {
            throw std::runtime_error("Invalid command. Try again.");
        }
    }
    std::cout << "Pause recording..." << std::endl
              << std::endl;
    state.store(Paused, std::memory_order_release);
    cvNotPause.notify_all();
    cvAudioBufferNotFull.notify_all();
    cvAudioBufferNotEmpty.notify_all();
    cvVideoBufferNotFull.notify_all();
    cvVideoBufferNotEmpty.notify_all();
    avformat_close_input(&audioInFormatCtx);
}

void ScreenAudioCapture::Stop()
{
    if (state.load(std::memory_order_acquire) == Stopped)
    {
        throw std::runtime_error("Recording has already been stopped.");
    }
    else if (state.load(std::memory_order_acquire) == NotStarted)
    {
        std::cout << "Nothing done." << std::endl
                  << std::endl;
        state.store(Finished, std::memory_order_release);
        return;
    }
    std::cout << "Stop recording..." << std::endl
              << std::endl;
    RecordState oldstate = state.load(std::memory_order_acquire);
    state.store(Stopped, std::memory_order_release);
    if (oldstate == Paused)
    {
        cvNotPause.notify_all();
    }
    cvAudioBufferNotFull.notify_all();
    cvAudioBufferNotEmpty.notify_all();
    cvVideoBufferNotFull.notify_all();
    cvVideoBufferNotEmpty.notify_all();
}

void ScreenAudioCapture::OpenAudio()
{
    audioOptions = nullptr;
    audioInFormatCtx = nullptr;
    int ret;
#ifdef _WIN32
    if (deviceName.empty())
    {
        deviceName = DS_GetDefaultDevice("a");
        if (deviceName.empty())
        {
            fatal = true;
            throw std::runtime_error("Fail to get default audio device, maybe no microphone.");
        }
    }
    deviceName = "audio=" + deviceName;
    audioInputFormat = const_cast<AVInputFormat *>(av_find_input_format("dshow"));
#elif __APPLE__
    if (deviceName == "")
        deviceName = ":0";
    AVInputFormat *audioInputFormat = const_cast<AVInputFormat *>(av_find_input_format("avfoundation"));
#elif __unix
    if (deviceName == "")
        deviceName = "default";
    AVInputFormat *audioInputFormat = av_find_input_format("pulse");
#endif
    ret = avformat_open_input(&audioInFormatCtx, deviceName.c_str(), audioInputFormat, &audioOptions);
    if (ret != 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't open input audio.");
    }
    //audioInFormatCtx->bit_rate = 1411200;
    if (avformat_find_stream_info(audioInFormatCtx, nullptr) < 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't find audio stream information.");
    }
    audioInStream = nullptr;
    for (int i = 0; i < audioInFormatCtx->nb_streams; i++)
    {
        if (audioInFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            audioInStream = audioInFormatCtx->streams[i];
            audioIndex = i;
            break;
        }
    }
    if (!audioInStream)
    {
        fatal = true;
        throw std::runtime_error("Couldn't find an audio stream.");
    }
    //audioInStream->time_base.num = 1;
    //audioInStream->time_base.den = 10000000;
    auto *audioInCodec = const_cast<AVCodec *>(avcodec_find_decoder(audioInStream->codecpar->codec_id));
    audioInCodecCtx = avcodec_alloc_context3(audioInCodec);
    avcodec_parameters_to_context(audioInCodecCtx, audioInStream->codecpar);
    //av_opt_set_int(audioInCodecCtx, "rtbufsize", 3041280 * 5, 0);
    if (avcodec_open2(audioInCodecCtx, audioInCodec, nullptr) < 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't open audio codec.");
    }
}

void ScreenAudioCapture::InitAudioBuffer()
{
    numberOfSamples = audioOutCodecCtx->frame_size;
    if (!numberOfSamples)
    {
        std::cout << "Couldn't set number of samples from encode context, setting it manually at 1024." << std::endl;
        numberOfSamples = 1024;
    }
    audioFifo = av_audio_fifo_alloc(audioOutCodecCtx->sample_fmt, audioOutCodecCtx->channels, 30 * numberOfSamples);
    if (!audioFifo)
    {
        fatal = true;
        throw std::runtime_error("Couldn't alloc audio fifo.");
    }
}

void ScreenAudioCapture::OpenVideo()
{
    AVDictionary *options = nullptr;
    //av_dict_set(&options, "draw_mouse", "0", 0);
    av_dict_set(&options, "framerate", "30", 0);
    av_dict_set(&options, "show_region", "1", 0);
    /* av_dict_set(&options, "video_size", std::to_string(width).append("x").append(std::to_string(height)).c_str(), 0);
    av_dict_set(&options, "offset_x", std::to_string(widthOffset).c_str(), 0);
    av_dict_set(&options, "offset_y", std::to_string(heightOffset).c_str(), 0); */
    av_dict_set(&options, "crop", "w=100:h=100:x=12:y=34", 0);

    videoInFormatCtx = nullptr;
#ifdef __APPLE__
    auto *videoInputFormat = const_cast<AVInputFormat *>(av_find_input_format("avfoundation"));
    int ret;
    ret = avformat_open_input(&videoInFormatCtx, "1:none", videoInputFormat, &options);
#elif
    auto *videoInputFormat = const_cast<AVInputFormat *>(av_find_input_format("gdigrab"));
    int ret;
    ret = avformat_open_input(&videoInFormatCtx, "desktop", videoInputFormat, &options);
#endif
    if (ret != 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't open video input.");
    }
    ret = avformat_find_stream_info(videoInFormatCtx, nullptr);
    if (ret < 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't find video stream information.");
    }
    videoInStream = nullptr;
    for (int i = 0; i < videoInFormatCtx->nb_streams; i++)
    {
        if (videoInFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            videoInStream = videoInFormatCtx->streams[i];
            videoIndex = i;
            break;
        }
    }
    if (!videoInStream)
    {
        fatal = true;
        throw std::runtime_error("Couldn't find a video stream.");
    }
    auto *videoInCodec = const_cast<AVCodec *>(avcodec_find_decoder(videoInStream->codecpar->codec_id));
    videoInCodecCtx = avcodec_alloc_context3(videoInCodec);
    avcodec_parameters_to_context(videoInCodecCtx, videoInStream->codecpar);
    videoInCodecCtx->width = width;
    videoInCodecCtx->height = height;
    if (avcodec_open2(videoInCodecCtx, videoInCodec, nullptr) < 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't open video codec.");
    }
}

void ScreenAudioCapture::InitVideoBuffer()
{
    videoOutFrameSize = av_image_get_buffer_size(videoOutCodecCtx->pix_fmt, width, height, 1);
    videoOutFrameBuffer = (uint8_t *)av_malloc(videoOutFrameSize);
    videoOutFrame = av_frame_alloc();
    av_image_fill_arrays(videoOutFrame->data, videoOutFrame->linesize, videoOutFrameBuffer, videoOutCodecCtx->pix_fmt, width, height, 1);
    if (!(videoFifo = av_fifo_alloc_array(30, videoOutFrameSize)))
    {
        fatal = true;
        throw std::runtime_error("Couldn't alloc video fifo buffer.");
    }
}

void ScreenAudioCapture::OpenOutput()
{
    int ret;
    ret = avformat_alloc_output_context2(&outFormatCtx, nullptr, nullptr, outFile.c_str());
    if (ret < 0)
    {
        fatal = true;
        throw std::runtime_error("Failed to alloc output context.");
    }
    if (audioInFormatCtx->streams[audioIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        audioOutStream = avformat_new_stream(outFormatCtx, nullptr);
        if (!audioOutStream)
        {
            fatal = true;
            throw std::runtime_error("Couldn't open audio stream.");
        }
        audioOutIndex = audioOutStream->index;
        auto *audioOutCodec = const_cast<AVCodec *>(avcodec_find_encoder(outFormatCtx->oformat->audio_codec));
        if (!audioOutCodec)
        {
            fatal = true;
            throw std::runtime_error("Failed to find output audio encoder.");
        }
        audioOutCodecCtx = avcodec_alloc_context3(audioOutCodec);
        audioOutCodecCtx->sample_fmt = audioOutCodec->sample_fmts ? audioOutCodec->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        audioOutCodecCtx->bit_rate = 128000;
        audioOutCodecCtx->sample_rate = audioInStream->codecpar->sample_rate;
        audioOutCodecCtx->channels = av_get_channel_layout_nb_channels(audioOutCodecCtx->channel_layout);
        audioOutCodecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
        audioOutCodecCtx->channels = av_get_channel_layout_nb_channels(audioOutCodecCtx->channel_layout);
        audioOutStream->time_base = AVRational{1, audioOutCodecCtx->sample_rate};
        audioOutCodecCtx->codec_tag = 0;
        audioOutCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(audioOutCodecCtx, audioOutCodec, nullptr) < 0)
        {
            fatal = true;
            throw std::runtime_error("Failed to open output audio encoder.");
        }
        avcodec_parameters_from_context(audioOutStream->codecpar, audioOutCodecCtx);
        audioConverter = swr_alloc();
        if (!audioConverter)
        {
            fatal = true;
            throw std::runtime_error("Couldn't alloc audio converter.");
        }
        av_opt_set_int(audioConverter, "in_channel_count", audioInCodecCtx->channels, 0);
        av_opt_set_int(audioConverter, "in_sample_rate", audioInCodecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(audioConverter, "in_sample_fmt", audioInCodecCtx->sample_fmt, 0);
        av_opt_set_int(audioConverter, "out_channel_count", audioOutCodecCtx->channels, 0);
        av_opt_set_int(audioConverter, "out_sample_rate", audioOutCodecCtx->sample_rate, 0);
        av_opt_set_sample_fmt(audioConverter, "out_sample_fmt", audioOutCodecCtx->sample_fmt, 0);
        ret = swr_init(audioConverter);
        if (ret < 0)
        {
            fatal = true;
            throw std::runtime_error("Couldn't init audio converter.");
        }
    }
    if (videoInFormatCtx->streams[videoIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        videoOutStream = avformat_new_stream(outFormatCtx, nullptr);
        if (!videoOutStream)
        {
            fatal = true;
            throw std::runtime_error("Couldn't open video stream.");
        }
        videoOutIndex = videoOutStream->index;
        videoOutStream->time_base = AVRational{1, 30};
        videoOutCodecCtx = avcodec_alloc_context3(nullptr);
        videoOutCodecCtx->width = width;
        videoOutCodecCtx->height = height;
        videoOutCodecCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        videoOutCodecCtx->time_base.num = 1;
        videoOutCodecCtx->time_base.den = 30;
        videoOutCodecCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        videoOutCodecCtx->codec_id = AV_CODEC_ID_H264;
        videoOutCodecCtx->bit_rate = 800 * 1000;
        videoOutCodecCtx->rc_max_rate = 800 * 1000;
        videoOutCodecCtx->rc_buffer_size = 500 * 1000;
        videoOutCodecCtx->gop_size = 30;
        videoOutCodecCtx->max_b_frames = 1;
        videoOutCodecCtx->qmin = 10;
        videoOutCodecCtx->qmax = 31;
        videoOutCodecCtx->max_qdiff = 4;
        videoOutCodecCtx->me_range = 16;
        videoOutCodecCtx->max_qdiff = 4;
        videoOutCodecCtx->qcompress = 0.6;
        auto *videoOutCodec = const_cast<AVCodec *>(avcodec_find_encoder(videoOutCodecCtx->codec_id));
        if (!videoOutCodec)
        {
            fatal = true;
            throw std::runtime_error("Failed to find output video encoder.");
        }
        videoOutCodecCtx->codec_tag = 0;
        videoOutCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        if (avcodec_open2(videoOutCodecCtx, videoOutCodec, nullptr) < 0)
        {
            fatal = true;
            throw std::runtime_error("Failed to open output video encoder.");
        }
        avcodec_parameters_from_context(videoOutStream->codecpar, videoOutCodecCtx);
        videoConverter = sws_getContext(videoInCodecCtx->width, videoInCodecCtx->height, videoInCodecCtx->pix_fmt, width, height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR,
                                        nullptr, nullptr, nullptr);
    }
    ret = avio_open(&outFormatCtx->pb, outFile.c_str(), AVIO_FLAG_WRITE);
    if (ret < 0)
    {
        fatal = true;
        throw std::runtime_error("Failed to open output file.");
    }
    if (avformat_write_header(outFormatCtx, nullptr) < 0)
    {
        fatal = true;
        throw std::runtime_error("Failed to write header for output.");
    }
}

AVFrame *AllocAudioFrame(AVCodecContext *outputContext, uint64_t nb_samples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;
    frame->format = outputContext->sample_fmt;
    frame->channel_layout = outputContext->channel_layout ? outputContext->channel_layout : AV_CH_LAYOUT_STEREO;
    frame->sample_rate = outputContext->sample_rate;
    frame->nb_samples = nb_samples;
    if (nb_samples)
    {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0)
        {
            throw std::runtime_error("Couldn't get buffer from frame.");
        }
    }
    return frame;
}

void ScreenAudioCapture::SoundRecordThreadProc()
{
    AVFrame *audioRawInputFrame = av_frame_alloc();
    AVPacket *audioInputPacket = av_packet_alloc();
    AVFrame *audioNewInputFrame = nullptr;
    try
    {
        audioNewInputFrame = AllocAudioFrame(audioOutCodecCtx, numberOfSamples);
    }
    catch (std::runtime_error e)
    {
        fatal = true;
        throw std::runtime_error(e.what());
    }
    int dstNbSamples, maxDstNbSamples;
    int ret;
    maxDstNbSamples = av_rescale_rnd(numberOfSamples, audioOutCodecCtx->sample_rate, audioInCodecCtx->sample_rate, AV_ROUND_UP);
    while (state.load(std::memory_order_acquire) != Stopped)
    {
        if (state.load(std::memory_order_acquire) == Paused)
        {
            std::unique_lock<std::mutex> lk(mtxPause);
            cvNotPause.wait(lk, [this]
                            { return state.load(std::memory_order_acquire) != Paused; });
        }
        ret = av_read_frame(audioInFormatCtx, audioInputPacket);
        if (ret < 0)
        {
            /* fatal = true;
            throw std::runtime_error("Couldn't read audio frame."); */
            av_packet_unref(audioInputPacket);
            continue;
        }
        if (audioInputPacket->stream_index != audioIndex)
        {
            std::cout << "Not an audio packet." << std::endl;
            av_packet_unref(audioInputPacket);
            continue;
        }
        if (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused)
        {
            continue;
        }
        ret = avcodec_send_packet(audioInCodecCtx, audioInputPacket);
        if (ret < 0)
        {
            /* fatal = true;
            throw std::runtime_error("Couldn't send audio packet in decoding."); */
            av_packet_unref(audioInputPacket);
            continue;
        }
        ret = avcodec_receive_frame(audioInCodecCtx, audioRawInputFrame);
        if (ret < 0)
        {
            /*  fatal = true;
            throw std::runtime_error("Couldn't receive audio packet in decoding."); */
            av_packet_unref(audioInputPacket);
            continue;
        }
        dstNbSamples = av_rescale_rnd(swr_get_delay(audioConverter, audioInCodecCtx->sample_rate) + audioRawInputFrame->nb_samples, audioInCodecCtx->sample_rate, audioOutCodecCtx->sample_rate, AV_ROUND_UP);
        if (dstNbSamples > maxDstNbSamples)
        {
            maxDstNbSamples = dstNbSamples;
            av_freep(&audioNewInputFrame->data[0]);
            ret = av_samples_alloc(audioNewInputFrame->data, audioNewInputFrame->linesize, audioOutCodecCtx->channels, dstNbSamples, audioOutCodecCtx->sample_fmt, 1);
            if (ret < 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't alloc samples for new frame audio.");
            }
            audioOutCodecCtx->frame_size = dstNbSamples;
            std::cout
                << "Reallocating new frame audio." << std::endl
                << "New audio output codec context frame size: " << audioOutCodecCtx->frame_size
                << std::endl
                << std::endl;
            numberOfSamples = audioNewInputFrame->nb_samples;
        }
        audioNewInputFrame->nb_samples = swr_convert(audioConverter, audioNewInputFrame->data, dstNbSamples, (const uint8_t **)audioRawInputFrame->data, audioRawInputFrame->nb_samples);
        if (audioNewInputFrame->nb_samples < 0)
        {
            fatal = true;
            throw std::runtime_error("Couldn't convert raw audio frame.");
        }
        if (state.load(std::memory_order_acquire) == Started)
        {
            std::unique_lock<std::mutex> lk(mtxAudioBuffer);
            cvAudioBufferNotFull.wait(lk, [audioNewInputFrame, this]
                                      { return av_audio_fifo_space(audioFifo) >= audioNewInputFrame->nb_samples || (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused); });
        }
        if (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused)
        {
            continue;
        }
        if (av_audio_fifo_write(audioFifo, (void **)audioNewInputFrame->data, audioNewInputFrame->nb_samples) < audioNewInputFrame->nb_samples)
        {
            fatal = true;
            throw std::runtime_error("Couldn't write audio frame on fifo.");
        }
        cvAudioBufferNotEmpty.notify_one();
        av_frame_unref(audioRawInputFrame);
        av_packet_unref(audioInputPacket);
    }
    FlushAudioDecoder();
    av_packet_free(&audioInputPacket);
    av_frame_free(&audioRawInputFrame);
    std::cout << "Done recording audio and relative cleaning." << std::endl
              << std::endl;
}

void ScreenAudioCapture::FlushAudioDecoder()
{
    AVFrame *audioRawInputFrame = av_frame_alloc();
    AVPacket *audioInputPacket = av_packet_alloc();
    AVFrame *audioNewInputFrame = nullptr;
    try
    {
        audioNewInputFrame = AllocAudioFrame(audioOutCodecCtx, numberOfSamples);
    }
    catch (std::runtime_error e)
    {
        fatal = true;
        throw std::runtime_error(e.what());
    }
    int dstNbSamples, maxDstNbSamples;
    int ret;
    maxDstNbSamples = av_rescale_rnd(numberOfSamples, audioOutCodecCtx->sample_rate, audioInCodecCtx->sample_rate, AV_ROUND_UP);
    ret = avcodec_send_packet(audioInCodecCtx, nullptr);
    if (ret != 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't send flushed packet in decoding.");
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(audioInCodecCtx, audioRawInputFrame);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                fatal = true;
                throw std::runtime_error("EAGAIN in receiving flushed audio frames.");
            }
            fatal = true;
            throw std::runtime_error("Couldn't receive flushed audio packet in decoding.");
        }
        dstNbSamples = av_rescale_rnd(swr_get_delay(audioConverter, audioInCodecCtx->sample_rate) + audioRawInputFrame->nb_samples, audioInCodecCtx->sample_rate, audioOutCodecCtx->sample_rate, AV_ROUND_UP);
        if (dstNbSamples > maxDstNbSamples)
        {
            std::cout << "Reallocating new flushed frame audio." << std::endl;
            maxDstNbSamples = dstNbSamples;
            av_freep(&audioNewInputFrame->data[0]);
            ret = av_samples_alloc(audioNewInputFrame->data, audioNewInputFrame->linesize, audioOutCodecCtx->channels, dstNbSamples, audioOutCodecCtx->sample_fmt, 1);
            if (ret < 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't alloc samples for new flushed frame audio.");
            }
            audioOutCodecCtx->frame_size = dstNbSamples;
            numberOfSamples = audioNewInputFrame->nb_samples;
        }
        audioNewInputFrame->nb_samples = swr_convert(audioConverter, audioNewInputFrame->data, dstNbSamples, (const uint8_t **)audioRawInputFrame->data, audioRawInputFrame->nb_samples);
        if (audioNewInputFrame->nb_samples < 0)
        {
            fatal = true;
            throw std::runtime_error("Couldn't convert raw audio frame.");
        }
        {
            std::unique_lock<std::mutex> lk(mtxAudioBuffer);
            cvAudioBufferNotFull.wait(lk, [audioNewInputFrame, this]
                                      { return av_audio_fifo_space(audioFifo) >= audioNewInputFrame->nb_samples; });
        }
        if (av_audio_fifo_write(audioFifo, (void **)audioNewInputFrame->data, audioNewInputFrame->nb_samples) < audioNewInputFrame->nb_samples)
        {
            fatal = true;
            throw std::runtime_error("Couldn't write audio frame on fifo.");
        }
        cvAudioBufferNotEmpty.notify_one();
        av_frame_unref(audioRawInputFrame);
        av_packet_unref(audioInputPacket);
    }
    av_frame_free(&audioRawInputFrame);
    av_packet_free(&audioInputPacket);
    std::cout << "Finished flushing audio decoder." << std::endl;
}

void ScreenAudioCapture::ScreenRecordThreadProc()
{
    int ret;
    AVPacket *videoInputPacket = av_packet_alloc();
    int y_size = width * height;
    AVFrame *oldVideoInputFrame = av_frame_alloc();
    AVFrame *newVideoInputFrame = av_frame_alloc();
    int newVideoFrameBufferSize = av_image_get_buffer_size(videoOutCodecCtx->pix_fmt, width, height, 1);
    auto *newVideoOutFrameBuffer = (uint8_t *)av_malloc(newVideoFrameBufferSize);
    av_image_fill_arrays(newVideoInputFrame->data, newVideoInputFrame->linesize, newVideoOutFrameBuffer, videoOutCodecCtx->pix_fmt, width, height, 1);
    while (state.load(std::memory_order_acquire) != Stopped)
    {
        if (state.load(std::memory_order_acquire) == Paused)
        {
            std::unique_lock<std::mutex> lk(mtxPause);
            cvNotPause.wait(lk, [this]
                            { return state.load(std::memory_order_acquire) != Paused; });
        }
        ret = av_read_frame(videoInFormatCtx, videoInputPacket);
        if (ret < 0)
        {
            /* fatal = true;
            throw std::runtime_error("Couldn't read video frame."); */
            continue;
        }
        //std::cout << "VideoInputPacket->pts: " << videoInputPacket->pts << std::endl;
        //std::cout << "VideoInputPacket->dts: " << videoInputPacket->dts << std::endl;
        //std::cout << "VideoInputPacket->pts rescaled_q: " <<
        //av_rescale_q(videoInputPacket->pts, videoInCodecCtx->time_base, videoOutCodecCtx->time_base); //<< std::endl;
        //std::cout << "VideoInputPacket->dts rescaled_q: " <<
        //av_rescale_q(videoInputPacket->dts, videoInCodecCtx->time_base, videoOutCodecCtx->time_base); //<< std::endl;
        //std::cout << "----------------------------------" << std::endl;
        if (videoInputPacket->stream_index != videoIndex)
        {
            std::cout << "Not a video packet." << std::endl;
            av_packet_unref(videoInputPacket);
            continue;
        }
        ret = avcodec_send_packet(videoInCodecCtx, videoInputPacket);
        if (ret != 0)
        {
            /* fatal = true;
            throw std::runtime_error("Couldn't send video packet in decoding."); */
            av_packet_unref(videoInputPacket);
            continue;
        }
        ret = avcodec_receive_frame(videoInCodecCtx, oldVideoInputFrame);
        if (ret != 0)
        {
            /* fatal = true;
            throw std::runtime_error("Couldn't receive video packet in decoding."); */
            av_packet_unref(videoInputPacket);
            continue;
        }
        sws_scale(videoConverter, (const uint8_t *const *)oldVideoInputFrame->data, oldVideoInputFrame->linesize, 0, videoOutCodecCtx->height, newVideoInputFrame->data, newVideoInputFrame->linesize);
        if (state.load(std::memory_order_acquire) == Started)
        {
            std::unique_lock<std::mutex> lk(mtxVideoBuffer);
            cvVideoBufferNotFull.wait(lk, [this]
                                      { return av_fifo_space(videoFifo) >= videoOutFrameSize || (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused); });
        }
        if (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused)
            continue;
        av_fifo_generic_write(videoFifo, newVideoInputFrame->data[0], y_size, nullptr);
        av_fifo_generic_write(videoFifo, newVideoInputFrame->data[1], y_size / 4, nullptr);
        av_fifo_generic_write(videoFifo, newVideoInputFrame->data[2], y_size / 4, nullptr);
        cvVideoBufferNotEmpty.notify_one();
        av_packet_unref(videoInputPacket);
    }
    FlushVideoDecoder();
    av_packet_free(&videoInputPacket);
    av_frame_free(&newVideoInputFrame);
    av_frame_free(&oldVideoInputFrame);
    av_free(newVideoOutFrameBuffer);
    std::cout << "Done recording video and relative cleaning." << std::endl
              << std::endl;
}

void ScreenAudioCapture::FlushVideoDecoder()
{
    int ret;
    AVPacket *videoInputPacket = av_packet_alloc();
    int y_size = width * height;
    AVFrame *oldVideoInputFrame = av_frame_alloc();
    AVFrame *newVideoInputFrame = av_frame_alloc();
    ret = avcodec_send_packet(videoInCodecCtx, nullptr);
    if (ret != 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't send flushed video packet in decoding.");
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(videoInCodecCtx, oldVideoInputFrame);
        if (ret != 0)
        {
            if (ret == AVERROR_EOF)
            {
                break;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                fatal = true;
                throw std::runtime_error("EAGAIN in receiving flushed video frames.");
            }
            fatal = true;
            throw std::runtime_error("Couldn't receive flushed video packet in decoding.");
        }
        sws_scale(videoConverter, (const uint8_t *const *)oldVideoInputFrame->data, oldVideoInputFrame->linesize, 0, videoOutCodecCtx->height, newVideoInputFrame->data, newVideoInputFrame->linesize);
        {
            std::unique_lock<std::mutex> lk(mtxVideoBuffer);
            cvVideoBufferNotFull.wait(lk, [this]
                                      { return av_fifo_space(videoFifo) >= videoOutFrameSize; });
        }
        av_fifo_generic_write(videoFifo, newVideoInputFrame->data[0], y_size, nullptr);
        av_fifo_generic_write(videoFifo, newVideoInputFrame->data[1], y_size / 4, nullptr);
        av_fifo_generic_write(videoFifo, newVideoInputFrame->data[2], y_size / 4, nullptr);
        cvVideoBufferNotEmpty.notify_one();
        av_packet_unref(videoInputPacket);
    }
    av_packet_free(&videoInputPacket);
    av_frame_free(&newVideoInputFrame);
    av_frame_free(&oldVideoInputFrame);
    std::cout << "Finished flushing video decoder." << std::endl;
}

void ScreenAudioCapture::MuxThreadProc()
{
    ScreenAudioCapture::LogStatus();
    std::thread screenRecord(&ScreenAudioCapture::ScreenRecordThreadProc, this);
    std::thread audioRecord(&ScreenAudioCapture::SoundRecordThreadProc, this);
    screenRecord.detach();
    audioRecord.detach();
    audioCurrentPts = 0;
    videoCurrentPts = 0;
    int ret;
    bool done = false;
    int audioFrameCount = 0, videoFrameCount = 0;
    AVPacket *audioOutputPacket = av_packet_alloc();
    AVPacket *videoOutputPacket = av_packet_alloc();
    int frameWritten = 0;
    while (true)
    {
        if (state.load(std::memory_order_acquire) == Paused)
        {
            std::unique_lock<std::mutex> lk(mtxPause);
            cvNotPause.wait(lk, [this]
                            { return state.load(std::memory_order_acquire) != Paused; });
        }
        if (state.load(std::memory_order_acquire) == Stopped && !done)
        {
            done = true;
        }
        if (done)
        {
            std::unique_lock<std::mutex> audioBufferLock(mtxAudioBuffer, std::defer_lock);
            std::unique_lock<std::mutex> videoBufferLock(mtxVideoBuffer, std::defer_lock);
            std::lock(audioBufferLock, videoBufferLock);
            //std::cout << "Audio: " << av_audio_fifo_size(audioFifo) << " < " << numberOfSamples << std::endl;
            //std::cout << "Video: " << av_fifo_size(videoFifo) << " < " << videoOutFrameSize << std::endl;
            if (av_audio_fifo_size(audioFifo) < numberOfSamples && av_fifo_size(videoFifo) < videoOutFrameSize)
            {
                std::cout << "Both audio and video buffers are empty." << std::endl;
                break;
            }
        }
        if (frameWritten % 100 == 0 && frameWritten != 0)
        {
            std::cout << "Total frames written: " << frameWritten << std::endl;
        }
        if (av_compare_ts(audioCurrentPts, outFormatCtx->streams[audioOutIndex]->time_base, videoCurrentPts, outFormatCtx->streams[videoOutIndex]->time_base) < 0)
        {
            /*if(audioCurrentPts % 100 == 0 && audioCurrentPts != 0) {
                std::cout << "AudioCurrentPts: " << audioCurrentPts << std::endl;
            }*/
            if (done)
            {
                std::lock_guard<std::mutex> lk(mtxAudioBuffer);
                if (av_audio_fifo_size(audioFifo) < numberOfSamples)
                {
                    std::cout << "Audio write done" << std::endl;
                    audioCurrentPts = INT_MAX;
                    continue;
                }
            }
            else if (state.load(std::memory_order_acquire) == Started)
            {
                std::unique_lock<std::mutex> lk(mtxAudioBuffer);
                cvAudioBufferNotEmpty.wait(lk, [this]
                                           { return av_audio_fifo_size(audioFifo) >= numberOfSamples || (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused); });
            }
            else
                continue;
            if (av_audio_fifo_size(audioFifo) < numberOfSamples)
                continue;
            AVFrame *audioOutputFrame = av_frame_alloc();
            audioOutputFrame->nb_samples = numberOfSamples;
            audioOutputFrame->channel_layout = audioOutCodecCtx->channel_layout;
            audioOutputFrame->format = audioOutCodecCtx->sample_fmt;
            audioOutputFrame->sample_rate = audioOutCodecCtx->sample_rate;
            audioOutputFrame->pts = numberOfSamples * audioFrameCount++;
            ret = av_frame_get_buffer(audioOutputFrame, 0);
            if (ret < 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't get audio frame buffer.");
            }
            av_audio_fifo_read(audioFifo, (void **)audioOutputFrame->data, numberOfSamples);
            cvAudioBufferNotFull.notify_one();
            ret = avcodec_send_frame(audioOutCodecCtx, audioOutputFrame);
            if (ret != 0)
            {
                std::cout << "Couldn't send audio frame to encoder, ret: " << ret << "." << std::endl;
                av_frame_free(&audioOutputFrame);
                continue;
            }
            ret = avcodec_receive_packet(audioOutCodecCtx, audioOutputPacket);
            if (ret < 0)
            {
                //std::cout << "Couldn't receive audio packet from encoder, ret: " << ret << "." << std::endl;
                audioFrameCount--;
                av_frame_free(&audioOutputFrame);
                av_packet_unref(audioOutputPacket);
                continue;
            }
            audioOutputPacket->stream_index = audioOutIndex;
            av_packet_rescale_ts(audioOutputPacket, audioOutCodecCtx->time_base, outFormatCtx->streams[audioOutIndex]->time_base);
            audioCurrentPts = audioOutputPacket->pts;
            //std::cout << "AudioCurrentPts: " << audioCurrentPts << std::endl;
            ret = av_interleaved_write_frame(outFormatCtx, audioOutputPacket);
            if (ret != 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't write audio frame on output context.");
            }
            frameWritten++;
            av_frame_free(&audioOutputFrame);
            av_packet_unref(audioOutputPacket);
        }
        else
        {
            /*if(videoCurrentPts % 1000 == 0 && videoCurrentPts != 0) {
                //std::cout << "VideoCurrentPts: " << videoCurrentPts << std::endl;
            }*/
            if (done)
            {
                std::lock_guard<std::mutex> lk(mtxVideoBuffer);
                if (av_fifo_size(videoFifo) < videoOutFrameSize)
                {
                    videoCurrentPts = INT_MAX;
                    continue;
                }
            }
            else if (state.load(std::memory_order_acquire) == Started)
            {
                std::unique_lock<std::mutex> lk(mtxVideoBuffer);
                //std::cout << av_fifo_size(videoFifo) << std::endl;
                cvVideoBufferNotEmpty.wait(lk, [this]
                                           { return av_fifo_size(videoFifo) >= videoOutFrameSize || (state.load(std::memory_order_acquire) != Started && state.load(std::memory_order_acquire) != Paused); });
            }
            else
                continue;
            if (av_fifo_size(videoFifo) < videoOutFrameSize)
                continue;
            av_fifo_generic_read(videoFifo, videoOutFrameBuffer, videoOutFrameSize, nullptr);
            cvVideoBufferNotFull.notify_one();
            videoOutFrame->pts = videoFrameCount++;
            //std::cout << "Frame: " << videoOutFrame->pts << std::endl;
            videoOutFrame->format = videoOutCodecCtx->pix_fmt;
            videoOutFrame->width = videoOutCodecCtx->width;
            videoOutFrame->height = videoOutCodecCtx->height;
            ret = avcodec_send_frame(videoOutCodecCtx, videoOutFrame);
            if (ret != 0)
            {
                std::cout << "Couldn't send video frame to encoder, ret: " << ret << "." << std::endl;
                continue;
            }
            ret = avcodec_receive_packet(videoOutCodecCtx, videoOutputPacket);
            if (ret != 0)
            {
                //std::cout << "Couldn't receive video packet from encoder, ret: " << ret << "." << std::endl;
                videoFrameCount--;
                av_packet_unref(videoOutputPacket);
                continue;
            }
            videoOutputPacket->stream_index = videoOutIndex;
            av_packet_rescale_ts(videoOutputPacket, videoOutCodecCtx->time_base, outFormatCtx->streams[videoOutIndex]->time_base);
            videoCurrentPts = videoOutputPacket->pts;
            //std::cout << "VideoCurrentPts: " << videoCurrentPts << std::endl;
            ret = av_interleaved_write_frame(outFormatCtx, videoOutputPacket);
            if (ret != 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't write video frame on output context.");
            }
            frameWritten++;
            av_packet_unref(videoOutputPacket);
        }
    }
    int *flushed = FlushMuxer();
    av_packet_free(&audioOutputPacket);
    av_packet_free(&videoOutputPacket);
    std::cout << "Total audio frames encoded: " << audioFrameCount + flushed[0] << " (" << flushed[0] << " flushed)." << std::endl;
    std::cout << "Total video frames encoded: " << videoFrameCount + flushed[1] << " (" << flushed[1] << " flushed)." << std::endl;
    std::cout << "Total frames encoded: " << frameWritten + flushed[2] << " (" << flushed[2] << " flushed)." << std::endl
              << std::endl;
    delete[] flushed;
    ret = av_write_trailer(outFormatCtx);
    if (ret < 0)
    {
        fatal = true;
        throw std::runtime_error("Couldn't write file trailer.");
    }
    std::cout << "Done muxing audio and video and relative cleaning." << std::endl
              << std::endl;
    state.store(Finished, std::memory_order_release);
}

int *ScreenAudioCapture::FlushMuxer()
{
    int ret;
    int flushedAudioCount = 0;
    int flushedVideoCount = 0;
    int totalFrameWrittenMuxed = 0;
    bool videoBeginFlush = false;
    bool audioBeginFlush = false;
    audioCurrentPts = videoCurrentPts = 0;
    int number_of_flushes = 2;
    AVPacket *packet_to_flush = av_packet_alloc();
    while (true)
    {
        if (totalFrameWrittenMuxed % 100 == 0 && totalFrameWrittenMuxed != 0)
        {
            std::cout << "Total frames written (flushed): " << totalFrameWrittenMuxed << std::endl;
        }
        if (av_compare_ts(audioCurrentPts, outFormatCtx->streams[audioOutIndex]->time_base, videoCurrentPts, outFormatCtx->streams[videoOutIndex]->time_base) <= 0)
        {
            if (!audioBeginFlush)
            {
                audioBeginFlush = true;
                ret = avcodec_send_frame(audioOutCodecCtx, nullptr);
                if (ret != 0)
                {
                    fatal = true;
                    throw std::runtime_error("Couldn't send flushed audio frame.");
                }
            }
            ret = avcodec_receive_packet(audioOutCodecCtx, packet_to_flush);
            if (ret < 0)
            {
                av_packet_unref(packet_to_flush);
                if (ret == AVERROR(EAGAIN))
                {
                    fatal = true;
                    throw std::runtime_error("EAGAIN in flushed audio received packet.");
                }
                else if (ret == AVERROR_EOF)
                {
                    std::cout << "Finished flushing audio encoder." << std::endl;
                    if (!(--number_of_flushes))
                        break;
                    audioCurrentPts = INT_MAX;
                    continue;
                }
                fatal = true;
                throw std::runtime_error("Couldn't receive flushed audio packet.");
            }
            packet_to_flush->stream_index = audioOutIndex;
            av_packet_rescale_ts(packet_to_flush, audioOutCodecCtx->time_base, outFormatCtx->streams[audioOutIndex]->time_base);
            audioCurrentPts = packet_to_flush->pts;
            ret = av_interleaved_write_frame(outFormatCtx, packet_to_flush);
            if (ret != 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't write flushed audio frame on output context.");
            }
            totalFrameWrittenMuxed++;
            flushedAudioCount++;
        }
        else
        {
            if (!videoBeginFlush)
            {
                videoBeginFlush = true;
                ret = avcodec_send_frame(videoOutCodecCtx, nullptr);
                if (ret != 0)
                {
                    fatal = true;
                    throw std::runtime_error("Couldn't send flushed video frame.");
                }
            }
            ret = avcodec_receive_packet(videoOutCodecCtx, packet_to_flush);
            if (ret < 0)
            {
                av_packet_unref(packet_to_flush);
                if (ret == AVERROR(EAGAIN))
                {
                    fatal = true;
                    throw std::runtime_error("EAGAIN in flushed video received packet.");
                }
                else if (ret == AVERROR_EOF)
                {
                    std::cout << "Finished flushing video encoder." << std::endl;
                    if (!(--number_of_flushes))
                        break;
                    videoCurrentPts = INT_MAX;
                    continue;
                }
                fatal = true;
                throw std::runtime_error("Couldn't receive flushed video packet.");
            }
            packet_to_flush->stream_index = videoOutIndex;
            av_packet_rescale_ts(packet_to_flush, videoOutCodecCtx->time_base, outFormatCtx->streams[videoOutIndex]->time_base);
            videoCurrentPts = packet_to_flush->pts;
            ret = av_interleaved_write_frame(outFormatCtx, packet_to_flush);
            if (ret != 0)
            {
                fatal = true;
                throw std::runtime_error("Couldn't write flushed video frame on output context.");
            }
            totalFrameWrittenMuxed++;
            flushedVideoCount++;
        }
        av_packet_unref(packet_to_flush);
    }
    av_packet_free(&packet_to_flush);
    std::cout << "Finished flushing muxer." << std::endl
              << std::endl;
    int *r = new int[3];
    r[0] = flushedAudioCount;
    r[1] = flushedVideoCount;
    r[2] = totalFrameWrittenMuxed;
    return r;
}

void ScreenAudioCapture::Release()
{
    if (audioInputFormat)
    {
        /* av_freep(&audioInputFormat);
        audioInputFormat = nullptr; */
    }
    if (audioOptions)
    {
        av_dict_free(&audioOptions);
        audioOptions = nullptr;
    }
    if (videoOutFrame)
    {
        av_frame_free(&videoOutFrame);
    }
    if (videoOutFrameBuffer)
    {
        av_free(videoOutFrameBuffer);
        videoOutFrameBuffer = nullptr;
    }
    if (audioInStream)
    {
        //av_freep(&audioInStream);
    }
    if (videoInStream)
    {
        av_freep(&videoInStream);
    }
    if (audioOutStream)
    {
        av_freep(&audioOutStream);
    }
    if (videoOutStream)
    {
        av_freep(&videoOutStream);
    }
    if (outFormatCtx)
    {
        if (outFormatCtx->pb)
            avio_close(outFormatCtx->pb);
        av_freep(&outFormatCtx);
    }
    if (audioInCodecCtx)
    {
        avcodec_free_context(&audioInCodecCtx);
    }
    if (videoInCodecCtx)
    {
        avcodec_free_context(&videoInCodecCtx);
    }
    if (audioOutCodecCtx)
    {
        avcodec_free_context(&audioOutCodecCtx);
    }
    if (videoOutCodecCtx)
    {
        avcodec_free_context(&videoOutCodecCtx);
    }
    if (audioFifo)
    {
        av_audio_fifo_free(audioFifo);
        audioFifo = nullptr;
    }
    if (videoFifo)
    {
        av_fifo_freep(&videoFifo);
    }
    if (audioInFormatCtx)
    {
        av_freep(&audioInFormatCtx);
    }
    if (videoInFormatCtx)
    {
        av_freep(&videoInFormatCtx);
    }
    if (audioConverter)
    {
        swr_free(&audioConverter);
    }
    if (videoConverter)
    {
        av_freep(&videoConverter);
    }
}

void ScreenAudioCapture::LogStatus()
{
    std::cout
        << "====================== INITIALIZATION STATUS ======================" << std::endl
        << "Output file: " << outFile << std::endl
        << "Audio device name: " << deviceName << std::endl
        << "Audio input format context bit rate: " << audioInFormatCtx->bit_rate << std::endl
        << "Audio input stream time base: AVRational { " << audioInStream->time_base.num << ", " << audioInStream->time_base.den << " }" << std::endl
        << "Audio input codec context sample rate: " << audioInCodecCtx->sample_rate << std::endl
        << "Audio input codec context time base: AVRational { " << audioInCodecCtx->time_base.num << ", " << audioInCodecCtx->time_base.den << " }" << std::endl
        << "Video input stream time base: AVRational { " << videoInStream->time_base.num << ", " << videoInStream->time_base.den << " }" << std::endl
        << "Video input codec context dimensions: " << videoInCodecCtx->width << " - " << videoInCodecCtx->height << std::endl
        << "Output format context probe size: " << outFormatCtx->probesize << std::endl
        << "Audio output stream time base: AVRational { " << audioOutStream->time_base.num << ", " << audioOutStream->time_base.den << " }" << std::endl
        << "Audio output codec context sample rate: " << audioOutCodecCtx->sample_rate << std::endl
        << "Audio output codec context time base: AVRational { " << audioOutCodecCtx->time_base.num << ", " << audioOutCodecCtx->time_base.den << " }" << std::endl
        << "Audio output codec context frame size: " << audioOutCodecCtx->frame_size << std::endl
        << "Video output stream time base: AVRational { " << videoOutStream->time_base.num << ", " << videoOutStream->time_base.den << " }" << std::endl
        << "Video output codec context time base: AVRational { " << videoOutCodecCtx->time_base.num << ", " << videoOutCodecCtx->time_base.den << " }" << std::endl
        << "Video output codec context dimensions: " << videoOutCodecCtx->width << " - " << videoOutCodecCtx->height
        << std::endl
        << std::endl
        << std::endl;
}