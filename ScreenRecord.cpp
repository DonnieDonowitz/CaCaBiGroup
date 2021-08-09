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
#include <thread>

int g_collectFrameCnt = 0;
int g_encodeFrameCnt = 0;

ScreenRecord::ScreenRecord() :
    m_fps(30),
    m_vIndex(-1),
    m_vOutIndex(-1),
    m_vFmtCtx(nullptr),
    m_oFmtCtx(nullptr),
    m_vDecodeContext(nullptr),
    m_vEncodeContext(nullptr),
    m_dict(nullptr),
    m_vFifoBuf(nullptr),
    m_swsCtx(nullptr),
    m_state(RecordState::NotStarted) {}
void ScreenRecord::Init() {
    m_filePath = "./output.mp4";
    m_width = 2560;
    m_height = 1440;
    m_fps = 30;
}
void ScreenRecord::Start() {
    if(m_state == RecordState::NotStarted) {
        std::cout << "Start record" << std::endl;
        m_state = RecordState::Started;
        std::thread recordThread(&ScreenRecord::ScreenRecordThreadProc, this);
        recordThread.detach();
    }
    else if(m_state == RecordState::Paused) {
        std::cout << "Continue record" << std::endl;
        m_state = RecordState::Started;
        m_cvNotPause.notify_one();
    }
}
void ScreenRecord::Pause() {
    std::cout << "Pause record" << std::endl;
    m_state = RecordState::Paused;
}
void ScreenRecord::Stop() {
    std::cout << "Stop record" << std::endl;
    if(m_state == RecordState::Paused)
        m_cvNotPause.notify_one();
    m_state = RecordState::Stopped;
}
int ScreenRecord::OpenVideo() {
    int ret = -1;
    AVInputFormat* ifmt = av_find_input_format("x11grab");
    AVDictionary* options = nullptr;
    const AVCodec* decoder = nullptr;
    av_dict_set(&options, "framerate", std::to_string(m_fps).c_str(), 0);
    if(avformat_open_input(&m_vFmtCtx, ":0.0", ifmt, &options) != 0) {
        std::cout << "Can't open video input stream" << std::endl;
        return -1;
    }
    if(avformat_find_stream_info(m_vFmtCtx, nullptr) < 0) {
        std::cout << "Couldn't find stream information" << std::endl;
        return -1;
    }
    for(int i = 0; i < m_vFmtCtx->nb_streams; ++i) {
        AVStream* stream = m_vFmtCtx->streams[i];
        if(stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
            decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if(decoder == nullptr) {
                std::cout << "avcodec_find_decoder failed" << std::endl;
                return -1;
            }
            m_vDecodeContext = avcodec_alloc_context3(decoder);
            if((ret = avcodec_parameters_to_context(m_vDecodeContext, stream->codecpar)) < 0) {
                std::cout << "Video avcodec_parameters_to_context failed, error code: " << ret << std::endl;
                return -1;
            }
            m_vIndex = i;
            break;
        }
    }
    if(avcodec_open2(m_vDecodeContext, decoder, &m_dict) < 0) {
        std::cout << "avcodec_open2 failed" << std::endl;
        return -1;
    }
    m_swsCtx = sws_getContext(m_vDecodeContext->width, m_vDecodeContext->height, m_vDecodeContext->pix_fmt, m_width, m_height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    return 0;
}
int ScreenRecord::OpenOutput() {
    int ret = -1;
    AVStream* vStream = nullptr;
    std::string outFilePath = m_filePath;
    ret = avformat_alloc_output_context2(&m_oFmtCtx, nullptr, nullptr, outFilePath.c_str());
    if(ret < 0) {
        std::cout << "avformat_alloc_output_context2 failed" << std::endl;
        return -1;
    }
    if(m_vFmtCtx->streams[m_vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
        vStream = avformat_new_stream(m_oFmtCtx, nullptr);
        if(!vStream) {
            std::cout << "Can't open new stream for output" << std::endl;
            return -1;
        }
        m_vOutIndex = vStream->index;
        vStream->time_base.num = 1;
        vStream->time_base.den = m_fps;
        m_vEncodeContext = avcodec_alloc_context3(NULL);
        if(nullptr == m_vEncodeContext) {
            std::cout << "avcodec_alloc_context3 failed" << std::endl;
            return -1;
        }
        SetEncoderParams();
        const AVCodec* encoder;
        encoder = avcodec_find_encoder(m_vEncodeContext->codec_id);
        if(!encoder) {
            std::cout << "Can't find the encoder, id: " << m_vEncodeContext->codec_id << std::endl;
            return -1;
        }
        m_vEncodeContext->codec_tag = 0;
        m_vEncodeContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_open2(m_vEncodeContext, encoder, nullptr);
        if(ret < 0) {
            std::cout << "Can't open encoder id: " << encoder->id << ". Error code: " << ret << std::endl;
            return -1;
        }
        ret = avcodec_parameters_from_context(vStream->codecpar, m_vEncodeContext);
        if(ret < 0) {
            std::cout << "Output avcodec_parameters_from_context, error code: " << ret << std::endl;
            return -1;
        }
    }
    if(!(m_oFmtCtx->oformat->flags & AVFMT_NOFILE)) {
        if(avio_open(&m_oFmtCtx->pb, outFilePath.c_str(), AVIO_FLAG_WRITE) < 0) {
            std::cout << "avio_open failed" << std::endl;
            return -1;
        }
    }
    if(avformat_write_header(m_oFmtCtx, &m_dict) < 0) {
        std::cout << "avformat_write_header failed" << std::endl;
        return -1;
    }
    return 0;
}
void ScreenRecord::ScreenRecordThreadProc() {
    int ret = -1;
    bool done = false;
    int64_t vCurPts = 0;
    int vFrameIndex = 0;
    avdevice_register_all();
    if(OpenVideo() < 0) {
        return;
    }
    if(OpenOutput() < 0) {
        return;
    }
    InitBuffer();
    std::thread screenRecord(&ScreenRecord::ScreenRecordAcquireThread, this);
    screenRecord.detach();
    while(1) {
        if(m_state == RecordState::Stopped && !done)
            done = true;
        if(done) {
            std::lock_guard<std::mutex> lk(m_mtx);
            if(av_fifo_size(m_vFifoBuf) < m_vOutFrameSize)
                break;
        } {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_cvNotEmpty.wait(lk, [this] { return av_fifo_size(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_read(m_vFifoBuf, m_vOutFrameBuf, m_vOutFrameSize, NULL);
        m_cvNotFull.notify_one();
        m_vOutFrame->pts = vFrameIndex;
        ++vFrameIndex;
        m_vOutFrame->format = m_vEncodeContext->pix_fmt;
        m_vOutFrame->width = m_width;
        m_vOutFrame->height = m_height;
        AVPacket pkt = { 0 };
        av_init_packet(&pkt);
        ret = avcodec_send_frame(m_vEncodeContext, m_vOutFrame);
        if(ret != 0) {
            std::cout << "video avcodec_send_frame failed, ret: " << ret << std::endl;
            av_packet_unref(&pkt);
            continue;
        }
        ret = avcodec_receive_packet(m_vEncodeContext, &pkt);
        if(ret != 0) {
            av_packet_unref(&pkt);
            if(ret == AVERROR(EAGAIN)) {
                std::cout << "EAGAIN avcodec_receive_packet" << std::endl;
                continue;
            }
            std::cout << "video avcodec_reveice_packet failed, ret: " << ret << std::endl;
            return;
        }
        pkt.stream_index = m_vOutIndex;
        av_packet_rescale_ts(&pkt, m_vEncodeContext->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
        ret = av_interleaved_write_frame(m_oFmtCtx, &pkt);
        if(ret == 0) {
            std::cout << "Write video packet id: " << ++g_encodeFrameCnt << std::endl;
        }
        else {
            std::cout << "video av_interleaved_write_frame failed, ret: " << ret << std::endl;
        }
        av_packet_unref(&pkt);
    }
    FlushEncoder();
    av_write_trailer(m_oFmtCtx);
    Release();
    SetFinito();
    std::cout << "Parent thread exit" << std::endl;
}
void ScreenRecord::ScreenRecordAcquireThread() {
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int y_size = m_width * m_height;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = av_frame_alloc();
    int newFrameBufSize = av_image_get_buffer_size(m_vEncodeContext->pix_fmt, m_width, m_height, 1);
    uint8_t* newFrameBuf = (uint8_t*) av_malloc(newFrameBufSize);
    av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf, m_vEncodeContext->pix_fmt, m_width, m_height, 1);
    while(m_state != RecordState::Stopped) {
        if(m_state == RecordState::Paused) {
            std::unique_lock<std::mutex> lk(m_mtxPause);
            m_cvNotPause.wait(lk, [this] { return m_state != RecordState::Paused; });
        }
        if(av_read_frame(m_vFmtCtx, &pkt) < 0) {
            std::cout << "video av_read_frame < 0" << std::endl;
            continue;
        }
        if(pkt.stream_index != m_vIndex) {
            std::cout << "Not a video packet from video input" << std::endl;
            av_packet_unref(&pkt);
        }
        ret = avcodec_send_packet(m_vDecodeContext, &pkt);
        if(ret != 0) {
            std::cout << "avcodec_send_packet failed, ret: " << ret << std::endl;
            av_packet_unref(&pkt);
            continue;
        }
        ret = avcodec_receive_frame(m_vDecodeContext, oldFrame);
        if(ret != 0) {
            std::cout << "avcodec_receive_frame failed, ret: " << ret << std::endl;
            av_packet_unref(&pkt);
            continue;
        }
        ++g_collectFrameCnt;
        sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0, m_vEncodeContext->height, newFrame->data, newFrame->linesize);
        {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_cvNotFull.wait(lk, [this] { return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
        m_cvNotEmpty.notify_one();
        av_packet_unref(&pkt);
    }
    FlushDecoder();
    av_free(newFrameBuf);
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
    std::cout << "Screen record thread exit" << std::endl;
}
void ScreenRecord::SetEncoderParams() {
    m_vEncodeContext->width = m_width;
    m_vEncodeContext->height = m_height;
    m_vEncodeContext->codec_type = AVMEDIA_TYPE_VIDEO;
    m_vEncodeContext->time_base.num = 1;
    m_vEncodeContext->time_base.den = m_fps;
    m_vEncodeContext->pix_fmt = AV_PIX_FMT_YUV420P;
    m_vEncodeContext->codec_id = AV_CODEC_ID_H264;
    m_vEncodeContext->bit_rate = 800*1000;
    m_vEncodeContext->rc_max_rate = 800*1000;
    m_vEncodeContext->rc_buffer_size = 500*1000;
    m_vEncodeContext->gop_size = 30;
    m_vEncodeContext->max_b_frames = 3;
    m_vEncodeContext->qmin = 10;
    m_vEncodeContext->qmax = 31;
    m_vEncodeContext->max_qdiff = 4;
    m_vEncodeContext->me_range = 16;
    m_vEncodeContext->max_qdiff = 4;
    m_vEncodeContext->qcompress = 0.6;
    av_dict_set(&m_dict, "profile", "high", 0);
    av_dict_set(&m_dict, "preset", "superfast", 0);
    av_dict_set(&m_dict, "threads", "0", 0);
    av_dict_set(&m_dict, "crf", "26", 0);
    av_dict_set(&m_dict, "tune", "zerolatency", 0);
    return;
}
void ScreenRecord::FlushDecoder() {
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int y_size = m_width * m_height;
    AVFrame* oldFrame = av_frame_alloc();
    AVFrame* newFrame = av_frame_alloc();
    ret = avcodec_send_packet(m_vDecodeContext, nullptr);
    std::cout << "Flush avcodec_send_packet, ret: " << ret << std::endl;
    while(ret >= 0) {
        ret = avcodec_receive_frame(m_vDecodeContext, oldFrame);
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
        ++g_collectFrameCnt;
        sws_scale(m_swsCtx, (const uint8_t* const*) oldFrame->data, oldFrame->linesize, 0, m_vEncodeContext->height, newFrame->data, newFrame->linesize);
        {
            std::unique_lock<std::mutex> lk(m_mtx);
            m_cvNotFull.wait(lk, [this] { return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
        m_cvNotEmpty.notify_one();
    }
    std::cout << "Collect frame count: " << g_collectFrameCnt << std::endl;
}
void ScreenRecord::FlushEncoder() {
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    ret = avcodec_send_frame(m_vEncodeContext, nullptr);
    std::cout << "avcodec_send_frame ret: " << ret << std::endl;
    while(ret >= 0) {
        ret = avcodec_receive_packet(m_vEncodeContext, &pkt);
        if(ret < 0) {
            av_packet_unref(&pkt);
            if(ret == AVERROR(EAGAIN)) {
                std::cout << "Flush EAGAIN avcodec_receive_packet" << std::endl;
                continue;
            }
            else if(ret == AVERROR_EOF) {
                std::cout << "Flush video encoder finished" << std::endl;
                break;
            }
            pkt.stream_index = m_vOutIndex;
            av_packet_rescale_ts(&pkt, m_vEncodeContext->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
            ret = av_interleaved_write_frame(m_oFmtCtx, &pkt);
            if(ret == 0) {
                std::cout << "Flush write video packet id: " << ++g_encodeFrameCnt << std::endl;
            }
            else {
                std::cout << "Video av_interleaved_write_frame failed, ret: " << ret << std::endl;
            }
            av_packet_unref(&pkt);
        }
    }
}
void ScreenRecord::InitBuffer() {
    m_vOutFrameSize = av_image_get_buffer_size(m_vEncodeContext->pix_fmt, m_width, m_height, 1);
    m_vOutFrameBuf = (uint8_t*) av_malloc(m_vOutFrameSize);
    m_vOutFrame = av_frame_alloc();
    av_image_fill_arrays(m_vOutFrame->data, m_vOutFrame->linesize, m_vOutFrameBuf, m_vEncodeContext->pix_fmt, m_width, m_height, 1);
    if(!(m_vFifoBuf = av_fifo_alloc_array(30, m_vOutFrameSize))) {
        std::cout << "av_fifo_alloc_array failed" << std::endl;
        return;
    }
}
void ScreenRecord::Release() {
    av_frame_free(&m_vOutFrame);
    av_free(m_vOutFrameBuf);
    if(m_vDecodeContext) {
        avcodec_free_context(&m_vDecodeContext);
        m_vDecodeContext = nullptr;
    }
    if(m_vEncodeContext) {
        avcodec_free_context(&m_vEncodeContext);
        m_vEncodeContext = nullptr;
    }
    if(m_vFifoBuf) {
        av_fifo_freep(&m_vFifoBuf);
    }
    if(m_vFmtCtx) {
        avformat_close_input(&m_vFmtCtx);
        m_vFmtCtx = nullptr;
    }
    avio_close(m_oFmtCtx->pb);
    avformat_free_context(m_oFmtCtx);
}
bool ScreenRecord::GetFinito() {
    return finito;
}
void ScreenRecord::SetFinito() {
    finito = true;
}