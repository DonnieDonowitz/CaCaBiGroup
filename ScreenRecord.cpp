#ifdef __cplusplus
extern "C" {
#endif
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libavdevice/avdevice.h>
    #include <libavutil/fifo.h>
    #include <libavutil/imgutils.h>
    #include <libswresample/swresample.h>
#ifdef __cplusplus
}
#endif

#include "ScreenRecord.h"
#include "ScreenRecordErrors.h"
#include <thread>
#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <cctype>
#include <algorithm>

int collectFrameCnt = 0;
int encodeFrameCnt = 0;

std::string GetStdoutFromCommand(std::string cmd) {

  std::string data;
  FILE * stream;
  const int max_buffer = 256;
  char buffer[max_buffer];
  cmd.append(" 2>&1");

  stream = popen(cmd.c_str(), "r");

  if (stream) {
    while (!feof(stream))
      if (fgets(buffer, max_buffer, stream) != NULL) data.append(buffer);
    pclose(stream);
  }
  return data;
}

ScreenRecord::ScreenRecord() :
    fps(30),
    vIndex(-1),
    vOutIndex(-1),
    vFmtCtx(nullptr),
    oFmtCtx(nullptr),
    vDecodeContext(nullptr),
    vEncodeContext(nullptr),
    dict(nullptr),
    vFifoBuf(nullptr),
    swsCtx(nullptr),
    state(RecordState::NotStarted) {}

void ScreenRecord::Init() {
    ShowStartMenu();
    filePath = "./output.mp4";
}


void ScreenRecord::Start() {
    if(state == RecordState::NotStarted) {
        std::cout << "Start recording" << std::endl;

        state = RecordState::Started;
        std::thread recordThread(&ScreenRecord::ScreenRecordThreadProc, this);
        recordThread.detach();
    } else if(state == RecordState::Paused) {
        std::cout << "Resume recording" << std::endl;

        state = RecordState::Started;
        cvNotPause.notify_one();
    }
}

void ScreenRecord::Pause() {
    std::cout << "Pause recording" << std::endl;

    state = RecordState::Paused;
}

void ScreenRecord::Stop() {
    std::cout << "Stop recording" << std::endl;

    if(state == RecordState::Paused) {
        cvNotPause.notify_one();
    }

    state = RecordState::Stopped;
}

int ScreenRecord::OpenVideo() {
    int ret = -1;
    AVInputFormat* ifmt = const_cast<AVInputFormat*>(av_find_input_format("dshow"));
    AVDictionary* options = nullptr;
    const AVCodec* decoder = nullptr;
    std::string command = GetStdoutFromCommand("echo $DISPLAY");
    
    av_dict_set(&options, "framerate", std::to_string(fps).c_str(), 0);
    //pulisco la stringa dagli spazi
    command.erase(std::remove_if(command.begin(), command.end(), ::isspace), command.end());
    if(avformat_open_input(&vFmtCtx, command.c_str(), ifmt, &options) != 0) {
        std::cout << "Can't open video input stream" << std::endl;
        return AVFORMAT_OPEN_INPUT_ERROR;
    }

    if(avformat_find_stream_info(vFmtCtx, nullptr) < 0) {
        std::cout << "Couldn't find stream information" << std::endl;
        return AVFORMAT_FIND_STREAM_INFO_ERROR;
    }

    for(int i = 0; i < vFmtCtx->nb_streams; ++i) {
        AVStream* stream = vFmtCtx->streams[i];

        if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            decoder = avcodec_find_decoder(stream->codecpar->codec_id);

            if(decoder == nullptr) {
                std::cout << "Video avcodec_find_decoder failed" << std::endl;
                return AVCODEC_FIND_DECODER_ERROR;
            }
            vDecodeContext = avcodec_alloc_context3(decoder);

            if((ret = avcodec_parameters_to_context(vDecodeContext, stream->codecpar)) < 0) {
                std::cout << "Video avcodec_parameters_to_context failed, error code: " << ret << std::endl;
                return AVCODEC_PARAMETERS_TO_CONTEXT_ERROR;
            }
            vIndex = i;
            break;
        }
    }

    if(avcodec_open2(vDecodeContext, decoder, &dict) < 0) {
        std::cout << "Video avcodec_open2 failed" << std::endl;
        return AVCODEC_OPEN2_ERROR;
    }

    swsCtx = sws_getContext(vDecodeContext->width, vDecodeContext->height, vDecodeContext->pix_fmt, width, height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    return SUCCESS;
}

int ScreenRecord::OpenOutput() {
    int ret = -1;
    AVStream* vStream = nullptr;
    const AVCodec* encoder;
    std::string outFilePath = filePath;
    
    if(avformat_alloc_output_context2(&oFmtCtx, nullptr, nullptr, outFilePath.c_str()) < 0) {
        std::cout << "Video avformat_alloc_output_context2 failed" << std::endl;
        return AVFORMAT_ALLOC_OUTPUT_CONTEXT2_ERROR;
    }

    if(vFmtCtx->streams[vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        vStream = avformat_new_stream(oFmtCtx, nullptr);

        if(!vStream) {
            std::cout << "Can't open new stream for output" << std::endl;
            return AVFORMAT_NEW_STREAM_ERROR;
        }

        vOutIndex = vStream->index;
        vStream->time_base.num = 1;
        vStream->time_base.den = fps;
        vEncodeContext = avcodec_alloc_context3(NULL);
        
        if(vEncodeContext == nullptr) {
            std::cout << "Video avcodec_alloc_context3 failed" << std::endl;
            return AVCODEC_ALLOC_CONTEXT3_ERROR;
        }

        SetEncoderParams();
        encoder = avcodec_find_encoder(vEncodeContext->codec_id);

        if(!encoder) {
            std::cout << "Can't find the encoder, id: " << vEncodeContext->codec_id << std::endl;
            return AVCODEC_FIND_ENCODER_ERROR;
        }

        vEncodeContext->codec_tag = 0;
        vEncodeContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_open2(vEncodeContext, encoder, nullptr);

        if(ret < 0) {
            std::cout << "Can't open encoder id: " << encoder->id << ". Error code: " << ret << std::endl;
            return AVCODEC_OPEN2_ERROR;
        }

        ret = avcodec_parameters_from_context(vStream->codecpar, vEncodeContext);

        if(ret < 0) {
            std::cout << "Output avcodec_parameters_frocontext, error code: " << ret << std::endl;
            return AVCODEC_PARAMETERS_FROM_CONTEXT_ERROR;
        }
    }

    if(!(oFmtCtx->oformat->flags & AVFMT_NOFILE)) {

        if(avio_open(&oFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cout << "Video avio_open failed" << std::endl;
            return AVIO_OPEN_ERROR;
        }
    }

    if(avformat_write_header(oFmtCtx, &dict) < 0) {
        std::cout << " Video avformat_write_header failed" << std::endl;
        return AVFORMAT_WRITE_HEADER_ERROR;
    }

    return SUCCESS;
}

void ScreenRecord::ScreenRecordThreadProc() {
    int ret = -1;
    bool done = false;
    int64_t vCurPts = 0;
    int vFrameIndex = 0;

    avdevice_register_all();

    if((ret = OpenVideo()) > 0) {
        switch(ret) {
            case AVFORMAT_OPEN_INPUT_ERROR:             std::cout << "In function ScreenRecordThreadProc()->OpenVideo() [Line 78]: AVFORMAT_OPEN_INPUT_ERROR error detected."; break;
            case AVFORMAT_FIND_STREAM_INFO_ERROR:       std::cout << "In function ScreenRecordThreadProc()->OpenVideo() [Line 83]: AVFORMAT_FIND_STREAM_INFO_ERROR error detected."; break;
            case AVCODEC_FIND_DECODER_ERROR:            std::cout << "In function ScreenRecordThreadProc()->OpenVideo() [Line 92]: AVCODEC_FIND_DECODER_ERROR error detected."; break;
            case AVCODEC_PARAMETERS_TO_CONTEXT_ERROR:   std::cout << "In function ScreenRecordThreadProc()->OpenVideo() [Line 100]: AVCODEC_PARAMETERS_TO_CONTEXT_ERROR error detected."; break;
            case AVCODEC_OPEN2_ERROR:                   std::cout << "In function ScreenRecordThreadProc()->OpenVideo() [Line 109]: AVCODEC_OPEN2_ERROR error detected."; break;
            default:                                    break;
        }

        return;
    }

    if((ret = OpenOutput()) > 0) {
        switch(ret) {
            case AVFORMAT_ALLOC_OUTPUT_CONTEXT2_ERROR:  std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 124]: AVFORMAT_ALLOC_OUTPUT_CONTEXT2_ERROR error detected."; break;
            case AVFORMAT_NEW_STREAM_ERROR:             std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 130]: AVFORMAT_NEW_STREAM_ERROR error detected."; break;
            case AVCODEC_ALLOC_CONTEXT3_ERROR:          std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 140]: AVCODEC_ALLOC_CONTEXT3_ERROR error detected."; break;
            case AVCODEC_FIND_ENCODER_ERROR:            std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 148]: AVCODEC_FIND_ENCODER_ERROR error detected."; break;
            case AVCODEC_OPEN2_ERROR:                   std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 157]: AVCODEC_OPEN2_ERROR error detected."; break;
            case AVCODEC_PARAMETERS_FROM_CONTEXT_ERROR: std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 164]: AVCODEC_PARAMETERS_FROM_CONTEXT_ERROR error detected."; break;
            case AVIO_OPEN_ERROR:                       std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 174]: AVIO_OPEN_ERROR error detected."; break;
            case AVFORMAT_WRITE_HEADER_ERROR:           std::cout << "In function ScreenRecordThreadProc()->OpenOutput() [Line 180]: AVFORMAT_WRITE_HEADER_ERROR error detected."; break;
            default:                                    break;
        }

        return;
    }

    InitBuffer();

    std::thread screenRecord(&ScreenRecord::ScreenRecordAcquireThread, this);
    screenRecord.detach();

    while(1) {

        if(state == RecordState::Stopped && !done)
        {
            done = true;
        }
            
        if(done) {
            std::lock_guard<std::mutex> lk(mtx);

            if(av_fifo_size(vFifoBuf) < vOutFrameSize)
            {
                break;
            }  
        } 
        
        {
            std::unique_lock<std::mutex> lk(mtx);
            cvNotEmpty.wait(lk, [this] { return av_fifo_size(vFifoBuf) >= vOutFrameSize; });
        }

        av_fifo_generic_read(vFifoBuf, vOutFrameBuf, vOutFrameSize, NULL);

        cvNotFull.notify_one();

        vOutFrame->pts = vFrameIndex;
        ++vFrameIndex;
        vOutFrame->format = vEncodeContext->pix_fmt;
        vOutFrame->width = width;
        vOutFrame->height = height;
        AVPacket pkt = { 0 };

        av_init_packet(&pkt);

        if((ret = avcodec_send_frame(vEncodeContext, vOutFrame)) != 0) {
            std::cout << "Video avcodec_send_frame failed. Error code: " << ret << std::endl;

            av_packet_unref(&pkt);
            continue;
        }

        if((ret = avcodec_receive_packet(vEncodeContext, &pkt)) != 0) {
            av_packet_unref(&pkt);

            if(ret == AVERROR(EAGAIN)) {
                std::cout << "EAGAIN avcodec_receive_packet" << std::endl;
                continue;
            }

            std::cout << "Video avcodec_reveice_packet failed, ret: " << ret << std::endl;
            return;
        }

        pkt.stream_index = vOutIndex;

        av_packet_rescale_ts(&pkt, vEncodeContext->time_base, oFmtCtx->streams[vOutIndex]->time_base);
        ret = av_interleaved_write_frame(oFmtCtx, &pkt);

        if(ret == 0) {
            std::cout << "Write video packet id: " << ++encodeFrameCnt << std::endl;
        } else {
            std::cout << "Video av_interleaved_write_frame failed, ret: " << ret << std::endl;
        }

        av_packet_unref(&pkt);
    }
    
    FlushEncoder();
    av_write_trailer(oFmtCtx);
    Release();
    SetFinished();

    std::cout << "Parent thread exit" << std::endl;
}

void ScreenRecord::ScreenRecordAcquireThread() {
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int y_size = width * height;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = av_frame_alloc();
    int newFrameBufSize = av_image_get_buffer_size(vEncodeContext->pix_fmt, width, height, 1);
    uint8_t* newFrameBuf = (uint8_t*) av_malloc(newFrameBufSize);

    av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf, vEncodeContext->pix_fmt, width, height, 1);

    while(state != RecordState::Stopped) {

        if(state == RecordState::Paused) {
            std::unique_lock<std::mutex> lk(mtxPause);
            cvNotPause.wait(lk, [this] { return state != RecordState::Paused; });
        }

        if(av_read_frame(vFmtCtx, &pkt) < 0) {
            std::cout << "Video av_read_frame < 0" << std::endl;
            continue;
        }

        if(pkt.stream_index != vIndex) {
            std::cout << "Not a video packet from video input" << std::endl;

            av_packet_unref(&pkt);
        }

        if((ret = avcodec_send_packet(vDecodeContext, &pkt)) != 0) {
            std::cout << "Video avcodec_send_packet failed, ret: " << ret << std::endl;

            av_packet_unref(&pkt);
            continue;
        }

        if((ret = avcodec_receive_frame(vDecodeContext, oldFrame)) != 0) {
            std::cout << "Video avcodec_receive_frame failed, ret: " << ret << std::endl;

            av_packet_unref(&pkt);
            continue;
        }

        ++collectFrameCnt;

        sws_scale(swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0, vEncodeContext->height, newFrame->data, newFrame->linesize);
        
        {
            std::unique_lock<std::mutex> lk(mtx);
            cvNotFull.wait(lk, [this] { return av_fifo_space(vFifoBuf) >= vOutFrameSize; });
        }

        av_fifo_generic_write(vFifoBuf, newFrame->data[0], y_size, NULL);
        av_fifo_generic_write(vFifoBuf, newFrame->data[1], y_size / 4, NULL);
        av_fifo_generic_write(vFifoBuf, newFrame->data[2], y_size / 4, NULL);

        cvNotEmpty.notify_one();
        av_packet_unref(&pkt);
    }

    FlushDecoder();
    av_free(newFrameBuf);
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);

    std::cout << "Screen record thread exit" << std::endl;
}

void ScreenRecord::SetEncoderParams() {
    vEncodeContext->width = width;
    vEncodeContext->height = height;
    vEncodeContext->codec_type = AVMEDIA_TYPE_VIDEO;
    vEncodeContext->time_base.num = 1;
    vEncodeContext->time_base.den = fps;
    vEncodeContext->pix_fmt = AV_PIX_FMT_YUV420P;
    vEncodeContext->codec_id = AV_CODEC_ID_H264;
    vEncodeContext->bit_rate = 800*1000;
    vEncodeContext->rc_max_rate = 800*1000;
    vEncodeContext->rc_buffer_size = 500*1000;
    vEncodeContext->gop_size = 30;
    vEncodeContext->max_b_frames = 3;
    vEncodeContext->qmin = 10;
    vEncodeContext->qmax = 31;
    vEncodeContext->max_qdiff = 4;
    vEncodeContext->me_range = 16;
    vEncodeContext->qcompress = 0.6;

    av_dict_set(&dict, "profile", "high", 0);
    av_dict_set(&dict, "preset", "superfast", 0);
    av_dict_set(&dict, "threads", "0", 0);
    av_dict_set(&dict, "crf", "26", 0);
    av_dict_set(&dict, "tune", "zerolatency", 0);

    return;
}

void ScreenRecord::FlushDecoder() {
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int y_size = width * height;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = av_frame_alloc();


    while((ret = avcodec_send_packet(vDecodeContext, nullptr)) >= 0) {
        ret = avcodec_receive_frame(vDecodeContext, oldFrame);

        if(ret < 0) {
            av_packet_unref(&pkt);
            if(ret == AVERROR(EAGAIN)) {
                std::cout << "Flush EAGAIN avcodec_reveive_frame" << std::endl;
                continue;
            }
            else if(ret == AVERROR_EOF) {
                std::cout << "Flush video encoder finished" << std::endl;
                break;
            }

            std::cout << "Flush video avcodec_receive_frame error, ret: " << ret << std::endl;
            return;
        }

        ++collectFrameCnt;
        sws_scale(swsCtx, (const uint8_t* const*) oldFrame->data, oldFrame->linesize, 0, vEncodeContext->height, newFrame->data, newFrame->linesize);
        
        {
            std::unique_lock<std::mutex> lk(mtx);
            cvNotFull.wait(lk, [this] { return av_fifo_space(vFifoBuf) >= vOutFrameSize; });
        }

        av_fifo_generic_write(vFifoBuf, newFrame->data[0], y_size, NULL);
        av_fifo_generic_write(vFifoBuf, newFrame->data[1], y_size / 4, NULL);
        av_fifo_generic_write(vFifoBuf, newFrame->data[2], y_size / 4, NULL);
        cvNotEmpty.notify_one();
    }

    std::cout << "Collect frame count: " << collectFrameCnt << std::endl;
}

void ScreenRecord::FlushEncoder() {
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    
    while((ret = avcodec_send_frame(vEncodeContext, nullptr)) >= 0) {

        ret = avcodec_receive_packet(vEncodeContext, &pkt);

        if(ret < 0) {
            av_packet_unref(&pkt);

            if(ret == AVERROR(EAGAIN)) {
                std::cout << "Flush EAGAIN avcodec_receive_packet" << std::endl;
                continue;
            } else if(ret == AVERROR_EOF) {
                std::cout << "Flush video encoder finished" << std::endl;
                break;
            }

            pkt.stream_index = vOutIndex;
            av_packet_rescale_ts(&pkt, vEncodeContext->time_base, oFmtCtx->streams[vOutIndex]->time_base);
            
            if((ret = av_interleaved_write_frame(oFmtCtx, &pkt)) == 0) {
                std::cout << "Flush write video packet id: " << ++encodeFrameCnt << std::endl;
            }
            else {
                std::cout << "Video av_interleaved_write_frame failed, ret: " << ret << std::endl;
            }

            av_packet_unref(&pkt);
        }
    }
}

void ScreenRecord::InitBuffer() {
    vOutFrameSize = av_image_get_buffer_size(vEncodeContext->pix_fmt, width, height, 1);
    vOutFrameBuf = (uint8_t*) av_malloc(vOutFrameSize);
    vOutFrame = av_frame_alloc();

    av_image_fill_arrays(vOutFrame->data, vOutFrame->linesize, vOutFrameBuf, vEncodeContext->pix_fmt, width, height, 1);

    if(!(vFifoBuf = av_fifo_alloc_array(30, vOutFrameSize))) {
        std::cout << "Video av_fifo_alloc_array failed" << std::endl;
        return;
    }
}

void ScreenRecord::Release() {
    av_frame_free(&vOutFrame);
    av_free(vOutFrameBuf);

    if(vDecodeContext) {
        avcodec_free_context(&vDecodeContext);
        vDecodeContext = nullptr;
    }

    if(vEncodeContext) {
        avcodec_free_context(&vEncodeContext);
        vEncodeContext = nullptr;
    }

    if(vFifoBuf) {
        av_fifo_freep(&vFifoBuf);
    }

    if(vFmtCtx) {
        avformat_close_input(&vFmtCtx);
        vFmtCtx = nullptr;
    }

    avio_close(oFmtCtx->pb);
    avformat_free_context(oFmtCtx);
}

void ScreenRecord::ShowStartMenu() {
    std::cout << "Welcome to CaCaBi Group Screen Recorder" << std::endl;
    std::cout << "Commands: \"p\" to pause the recorder, \"r\" to resume the recorder, \"s\" to stop the recorder" << std::endl;
    std::cout << "Please insert desired width of the recording: " << std::endl;
    std::cin >> width;
    std::cout << "Please insert desired height of the recording: " << std::endl;
    std::cin >> height;
}

void ScreenRecord::SetFinished() {
    finished = !finished;
}

bool ScreenRecord::GetFinished() {
    return finished;
}

