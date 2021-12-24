#include "ScreenRecord.h"

#define FATAL(x)    { fatal = true; throw std::runtime_error(x); }
#define LOG(x)      std::cout << x << std::endl

void ScreenRecord::Start()
{
    if (state == RecordState::NotStarted)
    {
        state = RecordState::Started;
        LOG("Launching the muxing thread...");
        
        std::thread muxThread(&ScreenRecord::MuxThreadProc, this);
        muxThread.detach();
    }
    else if(state == RecordState::Started)
    {
        throw std::runtime_error("Already recording. Maybe you meant 'pause' or 'stop'. Try again.");
    }
    else if(state == RecordState::Paused)
    {
        throw std::runtime_error("Already started, recording is on pause. Maybe you meant 'resume' or 'stop'. Try again.");
    }
    else
    {
        throw std::runtime_error("Invalid command. Try again.");
    }
}

void ScreenRecord::Resume()
{
    if (state == RecordState::Paused)
    {
        if(recordAudio)
        {
            avformat_open_input(&audioFormatContext, audioDevice.c_str(), audioInputFormat, nullptr);
        }

        state = RecordState::Started;
        LOG("Resuming the recording...");

        cvNotPause.notify_all();
    }
    else if(state == RecordState::NotStarted)
    {
        throw std::runtime_error("Nothing to resume, recording not started yet. Maybe you meant 'start' or 'stop'. Try again.");
    }
    else if(state == RecordState::Started)
    {
        throw std::runtime_error("Already recording. Maybe you meant 'pause' or 'stop'. Try again.");
    }
    else
    {
        throw std::runtime_error("Invalid command. Try again.");
    }
}

void ScreenRecord::Pause()
{
    if(state != RecordState::Started)
    {
        if(state == RecordState::Paused)
        {
            throw std::runtime_error("Already paused. Maybe you meant 'resume' or 'stop'. Try again.");
        }
        else if(state == RecordState::NotStarted)
        {
            throw std::runtime_error("Nothing to pause, recording has not started yet. Maybe you meant 'start' or 'stop'. Try again.");
        }
        else
        {
            throw std::runtime_error("Invalid command. Try again.");
        }
    }

    if(recordAudio)
    {
        avformat_close_input(&audioFormatContext);
    }

    state = RecordState::Paused;
    LOG("Pausing the recording...");

    cvNotPause.notify_all();
}

void ScreenRecord::Stop()
{
    if(state == RecordState::Stopped)
    {
        throw std::runtime_error("Recording has already been stopped.");
    }
    else if(state == RecordState::NotStarted)
    {
        LOG("Nothing done. Stopping the recording.");
        state = RecordState::Finished;
        return;
    }

    if (state == RecordState::Paused)
    {
        if(recordAudio)
        {
            avformat_open_input(&audioFormatContext, audioDevice.c_str(), audioInputFormat, nullptr);
        }

        cvNotPause.notify_all();
    }

    LOG("Stopping the recording...");
    state = RecordState::Stopped;
}

void ScreenRecord::LogStatus()
{
    std::cout
    << "====================== INITIALIZATION STATUS ======================" << std::endl
    << "Output file: " << filePath << std::endl;
    if(recordAudio)
    {
        std::cout << "Audio device name: " << audioDevice << std::endl
        << "Audio input format context bit rate: " << audioFormatContext->bit_rate << std::endl
        << "Audio input codec context sample rate: " << audioDecodeContext->sample_rate << std::endl
        << "Audio input codec context time base: AVRational { " << audioDecodeContext->time_base.num << ", " << audioDecodeContext->time_base.den << " }" << std::endl;
    }
    
    std::cout << "Video input codec context dimensions: " << videoDecodeContext->width << " - " << videoDecodeContext->height << std::endl
    << "Output format context probe size: " << outFormatContext->probesize << std::endl;

    if(recordAudio)
    {
        std::cout << "Audio output codec context sample rate: " << audioEncodeContext->sample_rate << std::endl
        << "Audio output codec context time base: AVRational { " << audioEncodeContext->time_base.num << ", " << audioEncodeContext->time_base.den << " }" << std::endl
        << "Audio output codec context frame size: " << audioEncodeContext->frame_size << std::endl;
    }
  
    std::cout << "Video output codec context time base: AVRational { " << videoEncodeContext->time_base.num << ", " << videoEncodeContext->time_base.den << " }" << std::endl
    << "Video output codec context dimensions: " << videoEncodeContext->width << " - " << videoEncodeContext->height
    << std::endl << std::endl << std::endl;
}

void ScreenRecord::OpenVideo()
{
    AVInputFormat *ifmt = const_cast<AVInputFormat*>(av_find_input_format("x11grab"));    
    AVDictionary *options = nullptr;
    AVCodec *decoder = nullptr;

    av_dict_set(&options, "framerate", std::to_string(fps).c_str(), 0);
    av_dict_set(&options, "video_size", std::to_string(width).append("x").append(std::to_string(height)).c_str(), 0);

    if (avformat_open_input(&videoFormatContext, videoDevice.append(".0+").append(std::to_string(widthOffset)).append(",").append(std::to_string(heightOffset)).c_str(), ifmt, &options) != 0)
    {
        FATAL("Can't open video input format.");
    }

    if (avformat_find_stream_info(videoFormatContext, nullptr) < 0)
    {
        FATAL("Can't find video stream informations.");
    }

    for (int i = 0; i < videoFormatContext->nb_streams; ++i)
    {
        AVStream *stream = videoFormatContext->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            decoder = const_cast<AVCodec*>(avcodec_find_decoder(stream->codecpar->codec_id));

            if (decoder == nullptr)
            {
                FATAL("Can't find video decoder.");
            }
        
            videoDecodeContext = avcodec_alloc_context3(decoder);

            if (avcodec_parameters_to_context(videoDecodeContext, stream->codecpar) < 0)
            {
                FATAL("Can't convert parameters to video decode context.");
            }

            videoIndex = i;
            break;
        }
    }

    if (avcodec_open2(videoDecodeContext, decoder, nullptr) < 0)
    {
        FATAL("Can't open video decode context.");
    }

    swsContext = sws_getContext(videoDecodeContext->width, videoDecodeContext->height, videoDecodeContext->pix_fmt, width, height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    return;
}

static bool check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
        {
            return true;
        }

        p++;
    }

    return false;
}

void ScreenRecord::OpenAudio()
{
    AVCodec *decoder = nullptr;

    audioInputFormat = const_cast<AVInputFormat*>(av_find_input_format("pulse"));

    if (avformat_open_input(&audioFormatContext, audioDevice.c_str(), audioInputFormat, nullptr) < 0)
    {
        FATAL("Can't open audio input.");
    }

    if (avformat_find_stream_info(audioFormatContext, nullptr) < 0)
    {
        FATAL("Can't find audio stream informations.");
    }

    for (int i = 0; i < audioFormatContext->nb_streams; ++i)
    {
        AVStream* stream = audioFormatContext->streams[i];

        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            decoder = const_cast<AVCodec*>(avcodec_find_decoder(stream->codecpar->codec_id));

            if (decoder == nullptr)
            {
                FATAL("Can't find audio decoder.");
            }
            
            audioDecodeContext = avcodec_alloc_context3(decoder);

            if (avcodec_parameters_to_context(audioDecodeContext, stream->codecpar) < 0)
            {
                FATAL("Can't convert parameters to audio decode context.");
            }
            
            audioIndex = i;
            break;
        }
    }

    if (avcodec_open2(audioDecodeContext, decoder, NULL) < 0)
    {
        FATAL("Can't open audio decode context.");        
    }

    return;
}

void ScreenRecord::OpenOutput()
{
    AVStream* vStream = nullptr;
    AVStream* aStream = nullptr;

    if (avformat_alloc_output_context2(&outFormatContext, nullptr, nullptr, filePath.c_str()) < 0)
    {
        FATAL("Can't allocate output format context.");
    }

    if (videoFormatContext->streams[videoIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        vStream = avformat_new_stream(outFormatContext, nullptr);

        if (!vStream)
        {
            FATAL("Can't istantiate a new video stream.");
        }

        videoOutIndex = vStream->index;
        vStream->time_base = AVRational{ 1, fps };

        videoEncodeContext = avcodec_alloc_context3(NULL);

        if (videoEncodeContext == nullptr)
        {
            FATAL("Can't allocate video encode context.");
        }

        videoEncodeContext->width = width;
        videoEncodeContext->height = height;
        videoEncodeContext->codec_type = AVMEDIA_TYPE_VIDEO;
        videoEncodeContext->time_base.num = 1;
        videoEncodeContext->time_base.den = fps;
        videoEncodeContext->pix_fmt = AV_PIX_FMT_YUV420P;
        videoEncodeContext->codec_id = AV_CODEC_ID_H264;
        videoEncodeContext->bit_rate = 800 * 1000;
        videoEncodeContext->rc_max_rate = 800 * 1000;
        videoEncodeContext->rc_buffer_size = 500 * 1000;
        videoEncodeContext->gop_size = 30;
        videoEncodeContext->max_b_frames = 3;
        videoEncodeContext->qmin = 10;	
        videoEncodeContext->qmax = 31;	
        videoEncodeContext->max_qdiff = 4;
        videoEncodeContext->me_range = 16;	
        videoEncodeContext->max_qdiff = 4;	
        videoEncodeContext->qcompress = 0.6;	

        AVCodec *encoder;
        encoder = const_cast<AVCodec*>(avcodec_find_encoder(videoEncodeContext->codec_id));

        if (!encoder)
        {
            FATAL("Can't find video encoder.");
        }

        videoEncodeContext->codec_tag = 0;
        videoEncodeContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (avcodec_open2(videoEncodeContext, encoder, nullptr) < 0)
        {
            FATAL("Can't open video encode context.");
        }

        if (avcodec_parameters_from_context(vStream->codecpar, videoEncodeContext) < 0)
        {
            FATAL("Can't convert parameters from video encode context.");
        }
    }

    if(recordAudio)
    {
        if (audioFormatContext->streams[audioIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            aStream = avformat_new_stream(outFormatContext, NULL);

            if (!aStream)
            {
                FATAL("Can't istantiate a new audio stream.");
            }

            audioOutIndex = aStream->index;

            AVCodec *encoder = const_cast<AVCodec*>(avcodec_find_encoder(outFormatContext->oformat->audio_codec));

            if (!encoder)
            {
                FATAL("Can't find audio encoder.");
            }

            audioEncodeContext = avcodec_alloc_context3(encoder);

            if (videoEncodeContext == nullptr)
            {
                FATAL("Can't allocate audio encode context.");
            }

            audioEncodeContext->sample_fmt = encoder->sample_fmts ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
            audioEncodeContext->bit_rate = audioBitrate;
            audioEncodeContext->sample_rate = 44100;

            if (encoder->supported_samplerates)
            {
                audioEncodeContext->sample_rate = encoder->supported_samplerates[0];

                for (int i = 0; encoder->supported_samplerates[i]; ++i)
                {
                    if (encoder->supported_samplerates[i] == 44100)
                    {
                        audioEncodeContext->sample_rate = 44100;
                    }
                }
            }

            audioEncodeContext->channels = av_get_channel_layout_nb_channels(audioEncodeContext->channel_layout);
            audioEncodeContext->channel_layout = AV_CH_LAYOUT_STEREO;

            if (encoder->channel_layouts)
            {
                audioEncodeContext->channel_layout = encoder->channel_layouts[0];

                for (int i = 0; encoder->channel_layouts[i]; ++i)
                {
                    if (encoder->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    {
                        audioEncodeContext->channel_layout = AV_CH_LAYOUT_STEREO;
                    }
                }
            }

            audioEncodeContext->channels = av_get_channel_layout_nb_channels(audioEncodeContext->channel_layout);
            aStream->time_base = AVRational{ 1, audioEncodeContext->sample_rate };

            audioEncodeContext->codec_tag = 0;
            audioEncodeContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

            if (!check_sample_fmt(encoder, audioEncodeContext->sample_fmt))
            {
                FATAL("Audio encoder sample format not supported by the audio encode context.");
            }

            if (avcodec_open2(audioEncodeContext, encoder, 0) < 0)
            {
                FATAL("Can't open audio encode context");
            }

            if (avcodec_parameters_from_context(aStream->codecpar, audioEncodeContext) < 0)
            {
                FATAL("Can't convert parameters from audio encode context.");
            }

            swrContext = swr_alloc();
            if (!swrContext)
            {
                FATAL("Can't allocate swr context.");
            }

            av_opt_set_int(swrContext, "in_channel_count", audioDecodeContext->channels, 0);	
            av_opt_set_int(swrContext, "in_sample_rate", audioDecodeContext->sample_rate, 0);	
            av_opt_set_sample_fmt(swrContext, "in_sample_fmt", audioDecodeContext->sample_fmt, 0);

            av_opt_set_int(swrContext, "out_channel_count", audioEncodeContext->channels, 0);	
            av_opt_set_int(swrContext, "out_sample_rate", audioEncodeContext->sample_rate, 0);
            av_opt_set_sample_fmt(swrContext, "out_sample_fmt", audioEncodeContext->sample_fmt, 0);	

            if (swr_init(swrContext) < 0)
            {
                FATAL("Can't initialise swr context.");
            }
        }
    }
    

    if (!(outFormatContext->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&outFormatContext->pb, filePath.c_str(), AVIO_FLAG_WRITE) < 0)
        {
            FATAL("Can't open given file path.");
        }
    }

    if (avformat_write_header(outFormatContext, nullptr) < 0)
    {
        FATAL("Can't write header to output format context.");
    }

    return;
}

AVFrame* ScreenRecord::AllocAudioFrame(AVCodecContext* c, int nbSamples)
{
    AVFrame *frame = av_frame_alloc();

    frame->format = c->sample_fmt;
    frame->channel_layout = c->channel_layout ? c->channel_layout : AV_CH_LAYOUT_STEREO;
    frame->sample_rate = c->sample_rate;
    frame->nb_samples = nbSamples;

    if (nbSamples)
    {
        if (av_frame_get_buffer(frame, 0) < 0)
        {
            return nullptr;
        }
    }

    return frame;
}

void ScreenRecord::InitVideoBuffer()
{
    videoOutFrameSize = av_image_get_buffer_size(videoEncodeContext->pix_fmt, width, height, 1);
    videoOutFrameBuffer = (uint8_t *)av_malloc(videoOutFrameSize);
    videoOutFrame = av_frame_alloc();

    av_image_fill_arrays(videoOutFrame->data, videoOutFrame->linesize, videoOutFrameBuffer, videoEncodeContext->pix_fmt, width, height, 1);
    videoFifoBuffer = av_fifo_alloc_array(30, videoOutFrameSize);

    if (!videoFifoBuffer)
    {
        LOG("Can't allocate video fifo buffer.");
        return;
    }
}

void ScreenRecord::InitAudioBuffer()
{
    numberOfSamples = audioEncodeContext->frame_size;

    if (!numberOfSamples)
    {
        numberOfSamples = 1024;
    }

    audioFifoBuffer = av_audio_fifo_alloc(audioEncodeContext->sample_fmt, audioEncodeContext->channels, 30 * numberOfSamples);

    if (!audioFifoBuffer)
    {
        LOG("Can't allocate audio fifo buffer.");
        return;
    }
}

void ScreenRecord::FlushVideoDecoder()
{
    int ret = -1;
    int size = width * height;
    AVFrame	*oldFrame = av_frame_alloc();
    AVFrame *newFrame = av_frame_alloc();

    ret = avcodec_send_packet(videoDecodeContext, nullptr);
    
    if (ret != 0)
    {
        FATAL("Can't send packet to the video decode context.");
        return;
    }

    while (ret >= 0)
    {
        ret = avcodec_receive_frame(videoDecodeContext, oldFrame);
        
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                LOG("AVERROR_EOF");
                break;
            }

            FATAL("Can't receive fram from the video decode context.")
            return;
        }

        sws_scale(swsContext, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0, videoEncodeContext->height, newFrame->data, newFrame->linesize);

        {
            std::unique_lock<std::mutex> lk(mutexVideoBuffer);
            cvVideoBufferNotFull.wait(lk, [this] { return av_fifo_space(this->videoFifoBuffer) >= this->videoOutFrameSize; });
        }

        av_fifo_generic_write(videoFifoBuffer, newFrame->data[0], size, NULL);
        av_fifo_generic_write(videoFifoBuffer, newFrame->data[1], size / 4, NULL);
        av_fifo_generic_write(videoFifoBuffer, newFrame->data[2], size / 4, NULL);
        cvVideoBufferNotEmpty.notify_one();
    }

    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
}

void ScreenRecord::FlushAudioDecoder()
{
    int ret = -1;
    int dstNbSamples, maxDstNbSamples;
    AVFrame *rawFrame = av_frame_alloc();
    AVFrame *newFrame = AllocAudioFrame(audioEncodeContext, numberOfSamples);
    AVPacket *pkt = av_packet_alloc();

    av_init_packet(pkt);
    
    maxDstNbSamples = dstNbSamples = av_rescale_rnd(numberOfSamples, audioEncodeContext->sample_rate, audioDecodeContext->sample_rate, AV_ROUND_UP);

    ret = avcodec_send_packet(audioDecodeContext, nullptr);

    if (ret != 0)
    {
        FATAL("Can't send packet to the audio decode context.");
        return;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(audioDecodeContext, rawFrame);

        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                LOG("AVERROR_EOF");
                break;
            }

            FATAL("Can't receive frame from the audio decode context.");
            return;
        }

        dstNbSamples = av_rescale_rnd(swr_get_delay(swrContext, audioDecodeContext->sample_rate) + rawFrame->nb_samples, audioEncodeContext->sample_rate, audioDecodeContext->sample_rate, AV_ROUND_UP);

        if (dstNbSamples > maxDstNbSamples)
        {
            av_freep(&newFrame->data[0]);
            ret = av_samples_alloc(newFrame->data, newFrame->linesize, audioEncodeContext->channels, dstNbSamples, audioEncodeContext->sample_fmt, 1);

            if (ret < 0)
            {
                FATAL("Can't allocate audio samples.");
                return;
            }

            maxDstNbSamples = dstNbSamples;
            audioEncodeContext->frame_size = dstNbSamples;
            numberOfSamples = newFrame->nb_samples;
        }

        newFrame->nb_samples = swr_convert(swrContext, newFrame->data, dstNbSamples, (const uint8_t **)rawFrame->data, rawFrame->nb_samples);

        if (newFrame->nb_samples < 0)
        {
            FATAL("Can't convert raw audio frame to new audio frame.");
            return;
        }

        {
            std::unique_lock<std::mutex> lk(mutexAudioBuffer);
            cvAudioBufferNotFull.wait(lk, [newFrame, this] { return av_audio_fifo_space(this->audioFifoBuffer) >= newFrame->nb_samples; });
        }

        if (av_audio_fifo_write(audioFifoBuffer, (void **)newFrame->data, newFrame->nb_samples) < newFrame->nb_samples)
        {
            FATAL("Can't write the new frame to the audio fifo buffer.");
            return;
        }

        cvAudioBufferNotEmpty.notify_one();
    }

    av_frame_free(&rawFrame);
    av_frame_free(&newFrame);
}

int* ScreenRecord::FlushEncoders()
{
    int ret = -1;
    int nFlush = 2;
    bool vBeginFlush = false;
    bool aBeginFlush = false;
    int flushedAudioCount = 0;
    int flushedVideoCount = 0;
    int totalFrameWritten = 0;
    
    videoCurrentPts = 0;
    audioCurrentPts = 0;

    while (1)
    {
        AVPacket* pkt = av_packet_alloc();
        av_init_packet(pkt);

        if (recordAudio && av_compare_ts(audioCurrentPts, outFormatContext->streams[audioOutIndex]->time_base, videoCurrentPts, outFormatContext->streams[videoOutIndex]->time_base) <= 0)
        {
            if (!aBeginFlush)
            {
                aBeginFlush = true;
                ret = avcodec_send_frame(audioEncodeContext, NULL);

                if (ret != 0)
                {
                    FATAL("Can't send frame to the audio encode context.");
                    return nullptr;
                }
            }
            ret = avcodec_receive_packet(audioEncodeContext, pkt);

            if (ret < 0)
            {
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN))
                {
                    LOG("AVERROR(EAGAIN)");
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {
                    if (!(--nFlush)) 
                    {
                        break;
                    }

                    audioCurrentPts = INT_MAX;

                    LOG("AVERROR_EOF");
                    continue;
                }

                FATAL("Can't receive packet from the audio encode context.");
                return nullptr;
            }

            pkt->stream_index = audioOutIndex;

            av_packet_rescale_ts(pkt, audioEncodeContext->time_base, outFormatContext->streams[audioOutIndex]->time_base);
            audioCurrentPts = pkt->pts;

            av_interleaved_write_frame(outFormatContext, pkt);

            totalFrameWritten++;
            flushedAudioCount++;
            
            av_packet_free(&pkt);
        }
        else
        {
            if (!vBeginFlush)
            {
                vBeginFlush = true;
                ret = avcodec_send_frame(videoEncodeContext, NULL);

                if (ret != 0)
                {
                    FATAL("Can't send frame to the video encode context.");
                    return nullptr;
                }
            }

            ret = avcodec_receive_packet(videoEncodeContext, pkt);

            if (ret < 0)
            {
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN))
                {
                    LOG("AVERROR(EAGAIN)");
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {
                    if (!(--nFlush)) 
                    {
                        break;
                    }

                    videoCurrentPts = INT_MAX;

                    LOG("AVERROR_EOF");
                    continue;
                }

                FATAL("Can't receive packet from the video encode context.");
                return nullptr; 
            }

            pkt->stream_index = videoOutIndex;
            
            av_packet_rescale_ts(pkt, videoEncodeContext->time_base, outFormatContext->streams[videoOutIndex]->time_base);

            videoCurrentPts = pkt->pts;

            av_interleaved_write_frame(outFormatContext, pkt);

            totalFrameWritten++;
            flushedVideoCount++;

            av_packet_free(&pkt);
        }
    }

    std::cout << "Finished flushing encoders." << std::endl;

    int* flushed = new int[3];
    flushed[0] = flushedAudioCount;
    flushed[1] = flushedVideoCount;
    flushed[2] = totalFrameWritten;

    return flushed;
}

void ScreenRecord::Release()
{
    if (videoOutFrame)
    {
        av_frame_free(&videoOutFrame);
        videoOutFrame = nullptr;
    }
    
    if (videoOutFrameBuffer)
    {
        av_free(videoOutFrameBuffer);
        videoOutFrameBuffer = nullptr;
    }
    
    if (outFormatContext)
    {
        avio_close(outFormatContext->pb);
        avformat_free_context(outFormatContext);
        outFormatContext = nullptr;
    }

    if (recordAudio && audioDecodeContext)
    {
        avcodec_free_context(&audioDecodeContext);
        audioDecodeContext = nullptr;
    }
    
    if (videoEncodeContext)
    {
        avcodec_free_context(&videoEncodeContext);
        videoEncodeContext = nullptr;
    }

    if (recordAudio && audioEncodeContext)
    {
        avcodec_free_context(&audioEncodeContext);
        audioEncodeContext = nullptr;
    }

    if (videoFifoBuffer)
    {
        av_fifo_freep(&videoFifoBuffer);
        videoFifoBuffer = nullptr;
    }

    if (recordAudio && audioFifoBuffer)
    {
        av_audio_fifo_free(audioFifoBuffer);
        audioFifoBuffer = nullptr;
    }

    if (videoFormatContext)
    {
        avformat_close_input(&videoFormatContext);
        videoFormatContext = nullptr;
    }

    if (recordAudio && audioFormatContext)
    {
        avformat_close_input(&audioFormatContext);
        audioFormatContext = nullptr;
    }
}

void ScreenRecord::MuxThreadProc()
{
    int ret = -1;
    bool done = false;
    int vFrameIndex = 0, aFrameIndex = 0;

    avdevice_register_all();

    OpenVideo();

    if(recordAudio) 
    {
        OpenAudio();
    }

    OpenOutput();
    InitVideoBuffer();
    if(recordAudio)
    {
        InitAudioBuffer();
    }

    LogStatus();

    std::thread screenRecord(&ScreenRecord::ScreenRecordThreadProc, this);
    screenRecord.detach();
    
    if(recordAudio) 
    {
        std::thread soundRecord(&ScreenRecord::SoundRecordThreadProc, this);
        soundRecord.detach();
    }

    while (1)
    {
        if (state == RecordState::Stopped && !done)
        {
            done = true;
        }

        if (recordAudio && done)
        {
            std::unique_lock<std::mutex> vBufLock(mutexVideoBuffer, std::defer_lock);
            std::unique_lock<std::mutex> aBufLock(mutexAudioBuffer, std::defer_lock);

            std::lock(vBufLock, aBufLock);

            if (av_fifo_size(videoFifoBuffer) < videoOutFrameSize && av_audio_fifo_size(audioFifoBuffer) < numberOfSamples)
            {
                LOG("Video fifo buffer and audio fifo buffer with size smaller than expected.");
                break;
            }
        } 
        else if(done)
        {
            break;
        }

        if (recordAudio && av_compare_ts(audioCurrentPts, outFormatContext->streams[audioOutIndex]->time_base, videoCurrentPts, outFormatContext->streams[videoOutIndex]->time_base) <= 0)
        {
            if (done)
            {
                std::lock_guard<std::mutex> lk(mutexAudioBuffer);
                if (av_audio_fifo_size(audioFifoBuffer) < numberOfSamples)
                { 
                    audioCurrentPts = INT_MAX;
                    continue;
                }
            }
            else
            {
                std::unique_lock<std::mutex> lk(mutexAudioBuffer);
                cvAudioBufferNotEmpty.wait(lk, [this] { return av_audio_fifo_size(this->audioFifoBuffer) >= this->numberOfSamples; });
            }

            AVFrame *aFrame = av_frame_alloc();

            aFrame->nb_samples = numberOfSamples;
            aFrame->channel_layout = audioEncodeContext->channel_layout;
            aFrame->format = audioEncodeContext->sample_fmt;
            aFrame->sample_rate = audioEncodeContext->sample_rate;
            aFrame->pts = numberOfSamples * aFrameIndex++;

            av_frame_get_buffer(aFrame, 0);
            av_audio_fifo_read(audioFifoBuffer, (void **)aFrame->data, numberOfSamples);
            cvAudioBufferNotFull.notify_one();

            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);

            ret = avcodec_send_frame(audioEncodeContext, aFrame);

            if (ret != 0)
            {
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }
            ret = avcodec_receive_packet(audioEncodeContext, pkt);

            if (ret != 0)
            {
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }

            pkt->stream_index = audioOutIndex;

            av_packet_rescale_ts(pkt, audioEncodeContext->time_base, outFormatContext->streams[audioOutIndex]->time_base);

            audioCurrentPts = pkt->pts;

            if(!av_interleaved_write_frame(outFormatContext, pkt))
            {
                continue;
            }

            av_frame_free(&aFrame);
            av_packet_free(&pkt);
        }
        else
        {
            if (done)
            {
                std::lock_guard<std::mutex> lk(mutexVideoBuffer);

                if (av_fifo_size(videoFifoBuffer) < videoOutFrameSize)
                {
                    videoCurrentPts = INT_MAX;
                    continue;
                }
            }
            else
            {
                std::unique_lock<std::mutex> lk(mutexVideoBuffer);
                cvVideoBufferNotEmpty.wait(lk, [this] { return av_fifo_size(this->videoFifoBuffer) >= this->videoOutFrameSize; });
            }

            av_fifo_generic_read(videoFifoBuffer, videoOutFrameBuffer, videoOutFrameSize, nullptr);
            cvVideoBufferNotFull.notify_one();

            videoOutFrame->pts = vFrameIndex++;
            videoOutFrame->format = videoEncodeContext->pix_fmt;
            videoOutFrame->width = videoEncodeContext->width;
            videoOutFrame->height = videoEncodeContext->height;

            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);

            ret = avcodec_send_frame(videoEncodeContext, videoOutFrame);

            if (ret != 0)
            {
                av_packet_unref(pkt);
                continue;
            }

            ret = avcodec_receive_packet(videoEncodeContext, pkt);
            
            if (ret != 0)
            {
                av_packet_unref(pkt);
                continue;
            }

            pkt->stream_index = videoOutIndex;

            av_packet_rescale_ts(pkt, videoEncodeContext->time_base, outFormatContext->streams[videoOutIndex]->time_base);
            videoCurrentPts = pkt->pts;

            if(!av_interleaved_write_frame(outFormatContext, pkt))
            {
                continue;
            }

            av_packet_free(&pkt);
        }
    }

    int* flushed = FlushEncoders();
    
    if(flushed)
    {
        if(recordAudio) 
        {
            std::cout << "Total audio frames encoded: " << aFrameIndex + flushed[0] << " (" << flushed[0] << " flushed)." << std::endl;
        }

        std::cout << "Total video frames encoded: " << vFrameIndex + flushed[1] << " (" << flushed[1] << " flushed)." << std::endl;

        delete[] flushed;
    }

    av_write_trailer(outFormatContext);

    Release();

    if(recordAudio)
    {
        std::cout << "Done muxing audio and video and relative cleaning." << std::endl << std::endl;
    }
    else
    {
        std::cout << "Done muxing video and relative cleaning." << std::endl << std::endl;
    }
    state = RecordState::Finished;
}

void ScreenRecord::ScreenRecordThreadProc()
{
    int ret = -1;
    int size = width * height;
    int frameWritten = 0;
    AVFrame	*oldFrame = av_frame_alloc();
    AVFrame *newFrame = av_frame_alloc();

    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);

    int newFrameBufSize = av_image_get_buffer_size(videoEncodeContext->pix_fmt, width, height, 1);
    uint8_t *newFrameBuf = (uint8_t*)av_malloc(newFrameBufSize);

    av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf, videoEncodeContext->pix_fmt, width, height, 1);

    while (state != RecordState::Stopped)
    {
        if (state == RecordState::Paused)
        {
            LOG("Pausing the video thread...");
            std::unique_lock<std::mutex> lk(mutexPause);
            cvNotPause.wait(lk, [this] { return state != RecordState::Paused; });
        }

        if(frameWritten % 100 == 0 && frameWritten != 0)
        {
            LOG(std::string("Video frame written: ").append(std::to_string(frameWritten)));
        }

        if (av_read_frame(videoFormatContext, pkt) < 0)
        {
            LOG("Can't read frame from the video format context.");
            continue;
        }

        if (pkt->stream_index != videoIndex)
        {
            av_packet_unref(pkt);
        }

        ret = avcodec_send_packet(videoDecodeContext, pkt);

        if (ret != 0)
        {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_receive_frame(videoDecodeContext, oldFrame);

        if (ret != 0)
        {
            av_packet_unref(pkt);
            continue;
        }

        sws_scale(swsContext, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0, videoEncodeContext->height, newFrame->data, newFrame->linesize);

        {
            std::unique_lock<std::mutex> lk(mutexVideoBuffer);
            cvVideoBufferNotFull.wait(lk, [this] { return av_fifo_space(videoFifoBuffer) >= videoOutFrameSize; });
        }

        av_fifo_generic_write(videoFifoBuffer, newFrame->data[0], size, NULL);
        av_fifo_generic_write(videoFifoBuffer, newFrame->data[1], size / 4, NULL);
        av_fifo_generic_write(videoFifoBuffer, newFrame->data[2], size / 4, NULL);
        cvVideoBufferNotEmpty.notify_one();

        frameWritten++;

        av_packet_unref(pkt);
    }

    FlushVideoDecoder();

    av_free(newFrameBuf);
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
}

void ScreenRecord::SoundRecordThreadProc()
{
    int ret = -1;
    int nbSamples = numberOfSamples;
    int dstNbSamples, maxDstNbSamples;
    int frameWritten = 0;

    AVFrame *rawFrame = av_frame_alloc();
    AVFrame *newFrame = AllocAudioFrame(audioEncodeContext, nbSamples);

    AVPacket* pkt = av_packet_alloc();
    av_init_packet(pkt);

    maxDstNbSamples = dstNbSamples = av_rescale_rnd(nbSamples, audioEncodeContext->sample_rate, audioDecodeContext->sample_rate, AV_ROUND_UP);

    while (state != RecordState::Stopped)
    {
        if (state == RecordState::Paused)
        {
            LOG("Pausing the audio thread...");
            std::unique_lock<std::mutex> lk(mutexPause);
            cvNotPause.wait(lk, [this] { return state != RecordState::Paused; });
        }

        if(frameWritten % 100 == 0 && frameWritten != 0)
        {
            LOG(std::string("Audio frame written: ").append(std::to_string(frameWritten)));
        }

        if (av_read_frame(audioFormatContext, pkt) < 0)
        {
            LOG("Can't read frame from the audio format context.");
            continue;
        }

        if (pkt->stream_index != audioIndex)
        {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_send_packet(audioDecodeContext, pkt);

        if (ret != 0)
        {
            av_packet_unref(pkt);
            continue;
        }

        ret = avcodec_receive_frame(audioDecodeContext, rawFrame);

        if (ret != 0)
        {
            av_packet_unref(pkt);
            continue;
        }

        dstNbSamples = av_rescale_rnd(swr_get_delay(swrContext, audioDecodeContext->sample_rate) + rawFrame->nb_samples, audioEncodeContext->sample_rate, audioDecodeContext->sample_rate, AV_ROUND_UP);

        if (dstNbSamples > maxDstNbSamples)
        {
            av_freep(&newFrame->data[0]);
            ret = av_samples_alloc(newFrame->data, newFrame->linesize, audioEncodeContext->channels, dstNbSamples, audioEncodeContext->sample_fmt, 1);

            if (ret < 0)
            {
                FATAL("Can't allocate audio samples.");
                return;
            }

            maxDstNbSamples = dstNbSamples;
            audioEncodeContext->frame_size = dstNbSamples;
            numberOfSamples = newFrame->nb_samples;	
        }

        newFrame->nb_samples = swr_convert(swrContext, newFrame->data, dstNbSamples, (const uint8_t **)rawFrame->data, rawFrame->nb_samples);

        if (newFrame->nb_samples < 0)
        {
            FATAL("Can't convert raw audio frame to a new frame.");
            return;
        }

        {
            std::unique_lock<std::mutex> lk(mutexAudioBuffer);
            cvAudioBufferNotFull.wait(lk, [newFrame, this] { return av_audio_fifo_space(audioFifoBuffer) >= newFrame->nb_samples; });
        }

        if (av_audio_fifo_write(audioFifoBuffer, (void **)newFrame->data, newFrame->nb_samples) < newFrame->nb_samples)
        {
            FATAL("Can't write frame to the audio fifo buffer.");
            return;
        }

        frameWritten++;

        cvAudioBufferNotEmpty.notify_one();
    }

    FlushAudioDecoder();
    av_frame_free(&rawFrame);
    av_frame_free(&newFrame);
}
