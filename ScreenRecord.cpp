#ifdef	__cplusplus
    extern "C"
    {
#endif

#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswscale/swscale.h"
#include "libavdevice/avdevice.h"
#include "libavutil/audio_fifo.h"
#include "libavutil/imgutils.h"
#include "libswresample/swresample.h"
#include "libavutil/avassert.h"

#ifdef __cplusplus
    };
#endif

#include "ScreenRecord.h"
#include <thread>
#include <fstream>
#include <signal.h>
#include <unistd.h>

#ifdef _WIN32
    #include <dshow.h>
#endif

int g_vCollectFrameCnt = 0;	
int g_vEncodeFrameCnt = 0;	
int g_aCollectFrameCnt = 0;	
int g_aEncodeFrameCnt = 0;	

ScreenRecord::ScreenRecord() :
      m_fps(30)
    , m_vIndex(-1), m_aIndex(-1)
    , m_vFmtCtx(nullptr), m_aFmtCtx(nullptr), m_oFmtCtx(nullptr)
    , m_vDecodeCtx(nullptr), m_aDecodeCtx(nullptr)
    , m_vEncodeCtx(nullptr), m_aEncodeCtx(nullptr)
    , m_vFifoBuf(nullptr), m_aFifoBuf(nullptr)
    , m_swsCtx(nullptr)
    , m_swrCtx(nullptr)
    , m_state(RecordState::NotStarted)
    , m_vCurPts(0), m_aCurPts(0)
{
}

void ScreenRecord::Init(std::string path, int width, int height, std::string video, std::string audio)
{
    isDone = false;
    m_filePath = path;
    m_width = width;
    m_height = height;
    m_fps = 30;
    m_audioBitrate = 128000;
    m_videoDevice = video;
    m_audioDevice = audio;
}

void ScreenRecord::Start()
{
    if (m_state == RecordState::NotStarted)
    {
        m_state = RecordState::Started;
        std::thread muxThread(&ScreenRecord::MuxThreadProc, this);
        muxThread.detach();
    }
    else if (m_state == RecordState::Paused)
    {
        m_state = RecordState::Started;
        m_cvNotPause.notify_all();
    }
}

void ScreenRecord::Pause()
{
    m_state = RecordState::Paused;
    std::cout << "Pausing the recording..." << std::endl;
}

void ScreenRecord::Stop()
{
    RecordState state = m_state;
    m_state = RecordState::Stopped;
    if (state == RecordState::Paused)
        m_cvNotPause.notify_all();
}

int ScreenRecord::OpenVideo()
{
    int ret = -1;
#ifdef _WIN32
    AVInputFormat *ifmt = av_find_input_format("gdigrab");
#else
    AVInputFormat *ifmt = av_find_input_format("x11grab");    
#endif
    AVDictionary *options = nullptr;
    AVCodec *decoder = nullptr;
    char* fps;

    fps = const_cast<char*>(std::to_string(m_fps).c_str());

    av_dict_set(&options, "framerate", fps, 0);
 
#ifdef _WIN32
    if (avformat_open_input(&m_vFmtCtx, "desktop", ifmt, &options) != 0)
    {
        return -1;
    }
#else
    if (avformat_open_input(&m_vFmtCtx, m_videoDevice.c_str(), ifmt, &options) != 0)
    {
        return -1;
    }
#endif
    if (avformat_find_stream_info(m_vFmtCtx, nullptr) < 0)
    {
        return -1;
    }
    for (int i = 0; i < m_vFmtCtx->nb_streams; ++i)
    {
        AVStream *stream = m_vFmtCtx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
        {
            decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (decoder == nullptr)
            {
                return -1;
            }
        
            m_vDecodeCtx = avcodec_alloc_context3(decoder);
            if ((ret = avcodec_parameters_to_context(m_vDecodeCtx, stream->codecpar)) < 0)
            {
                return -1;
            }
            m_vIndex = i;
            break;
        }
    }
    if (avcodec_open2(m_vDecodeCtx, decoder, nullptr) < 0)
    {
        return -1;
    }

    m_swsCtx = sws_getContext(m_vDecodeCtx->width, m_vDecodeCtx->height, m_vDecodeCtx->pix_fmt,
        m_width, m_height, AV_PIX_FMT_YUV420P,
        SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    return 0;
}

#ifdef _WIN32
static char *dup_wchar_to_utf8(wchar_t *w)
{
    char *s = NULL;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, 0, 0, 0, 0);
    s = (char *)av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, 0, 0);
    return s;
}
#endif

static int check_sample_fmt(const AVCodec *codec, enum AVSampleFormat sample_fmt)
{
    const enum AVSampleFormat *p = codec->sample_fmts;

    while (*p != AV_SAMPLE_FMT_NONE) {
        if (*p == sample_fmt)
            return 1;
        p++;
    }
    return 0;
}

int ScreenRecord::OpenAudio()
{
    int ret = -1;
    AVCodec *decoder = nullptr;
#ifdef _WIN32
    AVInputFormat *ifmt = av_find_input_format("dshow");
#else
    AVInputFormat *ifmt = av_find_input_format("pulse");
#endif
    if (avformat_open_input(&m_aFmtCtx, m_audioDevice.c_str(), ifmt, nullptr) < 0)
    {
        return -1;
    }
    if (avformat_find_stream_info(m_aFmtCtx, nullptr) < 0)
        return -1;

    for (int i = 0; i < m_aFmtCtx->nb_streams; ++i)
    {
        AVStream * stream = m_aFmtCtx->streams[i];
        if (stream->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
        {
            decoder = avcodec_find_decoder(stream->codecpar->codec_id);
            if (decoder == nullptr)
            {
                return -1;
            }
            
            m_aDecodeCtx = avcodec_alloc_context3(decoder);
            if ((ret = avcodec_parameters_to_context(m_aDecodeCtx, stream->codecpar)) < 0)
            {
                return -1;
            }
            m_aIndex = i;
            break;
        }
    }
    if (0 > avcodec_open2(m_aDecodeCtx, decoder, NULL))
    {
        return -1;
    }
    return 0;
}

int ScreenRecord::OpenOutput()
{
    int ret = -1;
    AVStream *vStream = nullptr, *aStream = nullptr;
    const char *outFileName = m_filePath.c_str();
    ret = avformat_alloc_output_context2(&m_oFmtCtx, nullptr, nullptr, outFileName);
    if (ret < 0)
    {
        return -1;
    }

    if (m_vFmtCtx->streams[m_vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        vStream = avformat_new_stream(m_oFmtCtx, nullptr);
        if (!vStream)
        {
            return -1;
        }
        m_vOutIndex = vStream->index;
        vStream->time_base = AVRational{ 1, m_fps };

        m_vEncodeCtx = avcodec_alloc_context3(NULL);
        if (nullptr == m_vEncodeCtx)
        {
            return -1;
        }
        m_vEncodeCtx->width = m_width;
        m_vEncodeCtx->height = m_height;
        m_vEncodeCtx->codec_type = AVMEDIA_TYPE_VIDEO;
        m_vEncodeCtx->time_base.num = 1;
        m_vEncodeCtx->time_base.den = m_fps;
        m_vEncodeCtx->pix_fmt = AV_PIX_FMT_YUV420P;
        m_vEncodeCtx->codec_id = AV_CODEC_ID_H264;
        m_vEncodeCtx->bit_rate = 800 * 1000;
        m_vEncodeCtx->rc_max_rate = 800 * 1000;
        m_vEncodeCtx->rc_buffer_size = 500 * 1000;
        m_vEncodeCtx->gop_size = 30;
        m_vEncodeCtx->max_b_frames = 3;
        m_vEncodeCtx->qmin = 10;	
        m_vEncodeCtx->qmax = 31;	
        m_vEncodeCtx->max_qdiff = 4;
        m_vEncodeCtx->me_range = 16;	
        m_vEncodeCtx->max_qdiff = 4;	
        m_vEncodeCtx->qcompress = 0.6;	

        AVCodec *encoder;
        encoder = avcodec_find_encoder(m_vEncodeCtx->codec_id);
        if (!encoder)
        {
            return -1;
        }
        m_vEncodeCtx->codec_tag = 0;
        m_vEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_open2(m_vEncodeCtx, encoder, nullptr);
        if (ret < 0)
        {
            return -1;
        }
        ret = avcodec_parameters_from_context(vStream->codecpar, m_vEncodeCtx);
        if (ret < 0)
        {
            return -1;
        }
    }
    if (m_aFmtCtx->streams[m_aIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        aStream = avformat_new_stream(m_oFmtCtx, NULL);
        if (!aStream)
        {
            return -1;
        }
        m_aOutIndex = aStream->index;

        AVCodec *encoder = avcodec_find_encoder(m_oFmtCtx->oformat->audio_codec);
        if (!encoder)
        {
            return -1;
        }
        m_aEncodeCtx = avcodec_alloc_context3(encoder);
        if (nullptr == m_vEncodeCtx)
        {
            return -1;
        }
        m_aEncodeCtx->sample_fmt = encoder->sample_fmts ? encoder->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
        m_aEncodeCtx->bit_rate = m_audioBitrate;
        m_aEncodeCtx->sample_rate = 44100;
        if (encoder->supported_samplerates)
        {
            m_aEncodeCtx->sample_rate = encoder->supported_samplerates[0];
            for (int i = 0; encoder->supported_samplerates[i]; ++i)
            {
                if (encoder->supported_samplerates[i] == 44100)
                    m_aEncodeCtx->sample_rate = 44100;
            }
        }
        m_aEncodeCtx->channels = av_get_channel_layout_nb_channels(m_aEncodeCtx->channel_layout);
        m_aEncodeCtx->channel_layout = AV_CH_LAYOUT_STEREO;
        if (encoder->channel_layouts)
        {
            m_aEncodeCtx->channel_layout = encoder->channel_layouts[0];
            for (int i = 0; encoder->channel_layouts[i]; ++i)
            {
                if (encoder->channel_layouts[i] == AV_CH_LAYOUT_STEREO)
                    m_aEncodeCtx->channel_layout = AV_CH_LAYOUT_STEREO;
            }
        }
        m_aEncodeCtx->channels = av_get_channel_layout_nb_channels(m_aEncodeCtx->channel_layout);
        aStream->time_base = AVRational{ 1, m_aEncodeCtx->sample_rate };

        m_aEncodeCtx->codec_tag = 0;
        m_aEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

        if (!check_sample_fmt(encoder, m_aEncodeCtx->sample_fmt))
        {
            return -1;
        }

        
        ret = avcodec_open2(m_aEncodeCtx, encoder, 0);
        if (ret < 0)
        {
            return -1;
        }
        ret = avcodec_parameters_from_context(aStream->codecpar, m_aEncodeCtx);
        if (ret < 0)
        {
            return -1;
        }

        m_swrCtx = swr_alloc();
        if (!m_swrCtx)
        {
            return -1;
        }
        av_opt_set_int(m_swrCtx, "in_channel_count", m_aDecodeCtx->channels, 0);	
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_aDecodeCtx->sample_rate, 0);	
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_aDecodeCtx->sample_fmt, 0);	
        av_opt_set_int(m_swrCtx, "out_channel_count", m_aEncodeCtx->channels, 0);	
        av_opt_set_int(m_swrCtx, "out_sample_rate", m_aEncodeCtx->sample_rate, 0);
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", m_aEncodeCtx->sample_fmt, 0);	

        if ((ret = swr_init(m_swrCtx)) < 0)
        {
            return -1;
        }
    }

    if (!(m_oFmtCtx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&m_oFmtCtx->pb, outFileName, AVIO_FLAG_WRITE) < 0)
        {
            return -1;
        }
    }
    if (avformat_write_header(m_oFmtCtx, nullptr) < 0)
    {
        return -1;
    }
    return 0;
}

std::string ScreenRecord::GetSpeakerDeviceName()
{
    char sName[256] = { 0 };
    std::string speaker = "";
    bool bRet = false;
#ifdef _WIN32
    ::CoInitialize(NULL);

    ICreateDevEnum* pCreateDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum,
        (void**)&pCreateDevEnum);

    IEnumMoniker* pEm;
    hr = pCreateDevEnum->CreateClassEnumerator(CLSID_AudioRendererCategory, &pEm, 0);
    if (hr != NOERROR)
    {
        ::CoUninitialize();
        return "";
    }

    pEm->Reset();
    ULONG cFetched;
    IMoniker *pM;
    while (hr = pEm->Next(1, &pM, &cFetched), hr == S_OK)
    {

        IPropertyBag* pBag = NULL;
        hr = pM->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pBag);
        if (SUCCEEDED(hr))
        {
            VARIANT var;
            var.vt = VT_BSTR;
            hr = pBag->Read(L"FriendlyName", &var, NULL)
            if (hr == NOERROR)
            {
                WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, sName, 256, "", NULL);
                speaker = QString::fromLocal8Bit(sName);
                SysFreeString(var.bstrVal);
            }
            pBag->Release();
        }
        pM->Release();
        bRet = true;
    }
    pCreateDevEnum = NULL;
    pEm = NULL;
    ::CoUninitialize();
#endif
    return speaker;
}

std::string ScreenRecord::GetMicrophoneDeviceName()
{
    char sName[256] = { 0 };
    std::string capture = "";
    bool bRet = false;
#ifdef _WIN32
    ::CoInitialize(NULL);

    ICreateDevEnum* pCreateDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum,
        NULL,
        CLSCTX_INPROC_SERVER,
        IID_ICreateDevEnum,
        (void**)&pCreateDevEnum);

    IEnumMoniker* pEm;
    hr = pCreateDevEnum->CreateClassEnumerator(CLSID_AudioInputDeviceCategory, &pEm, 0);
    if (hr != NOERROR)
    {
        ::CoUninitialize();
        return "";
    }

    pEm->Reset();
    ULONG cFetched;
    IMoniker *pM;
    while (hr = pEm->Next(1, &pM, &cFetched), hr == S_OK)
    {

        IPropertyBag* pBag = NULL;
        hr = pM->BindToStorage(0, 0, IID_IPropertyBag, (void**)&pBag);
        if (SUCCEEDED(hr))
        {
            VARIANT var;
            var.vt = VT_BSTR;
            hr = pBag->Read(L"FriendlyName", &var, NULL);
            if (hr == NOERROR)
            {
                WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, sName, 256, "", NULL);
                capture = QString::fromLocal8Bit(sName);
                SysFreeString(var.bstrVal);
            }
            pBag->Release();
        }
        pM->Release();
        bRet = true;
    }
    pCreateDevEnum = NULL;
    pEm = NULL;
    ::CoUninitialize();
#endif
    return capture;
}

AVFrame* ScreenRecord::AllocAudioFrame(AVCodecContext* c, int nbSamples)
{
    AVFrame *frame = av_frame_alloc();
    int ret;

    frame->format = c->sample_fmt;
    frame->channel_layout = c->channel_layout ? c->channel_layout : AV_CH_LAYOUT_STEREO;
    frame->sample_rate = c->sample_rate;
    frame->nb_samples = nbSamples;

    if (nbSamples)
    {
        ret = av_frame_get_buffer(frame, 0);
        if (ret < 0)
        {
            return nullptr;
        }
    }
    return frame;
}

void ScreenRecord::InitVideoBuffer()
{
    m_vOutFrameSize = av_image_get_buffer_size(m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
    m_vOutFrameBuf = (uint8_t *)av_malloc(m_vOutFrameSize);
    m_vOutFrame = av_frame_alloc();
    av_image_fill_arrays(m_vOutFrame->data, m_vOutFrame->linesize, m_vOutFrameBuf, m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
    if (!(m_vFifoBuf = av_fifo_alloc_array(30, m_vOutFrameSize)))
    {
        return;
    }
}

void ScreenRecord::InitAudioBuffer()
{
    m_nbSamples = m_aEncodeCtx->frame_size;
    if (!m_nbSamples)
    {
        m_nbSamples = 1024;
    }
    m_aFifoBuf = av_audio_fifo_alloc(m_aEncodeCtx->sample_fmt, m_aEncodeCtx->channels, 30 * m_nbSamples);
    if (!m_aFifoBuf)
    {
        return;
    }
}

void ScreenRecord::FlushVideoDecoder()
{
    int ret = -1;
    int y_size = m_width * m_height;
    AVFrame	*oldFrame = av_frame_alloc();
    AVFrame *newFrame = av_frame_alloc();

    ret = avcodec_send_packet(m_vDecodeCtx, nullptr);
    if (ret != 0)
    {
        return;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                break;
            }
            return;
        }
        ++g_vCollectFrameCnt;
        sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
            m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

        {
            std::unique_lock<std::mutex> lk(m_mtxVBuf);
            m_cvVBufNotFull.wait(lk, [this] { return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
        m_cvVBufNotEmpty.notify_one();
    }
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
}

void ScreenRecord::FlushAudioDecoder()
{
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int dstNbSamples, maxDstNbSamples;
    AVFrame *rawFrame = av_frame_alloc();
    AVFrame *newFrame = AllocAudioFrame(m_aEncodeCtx, m_nbSamples);
    maxDstNbSamples = dstNbSamples = av_rescale_rnd(m_nbSamples,
        m_aEncodeCtx->sample_rate, m_aDecodeCtx->sample_rate, AV_ROUND_UP);

    ret = avcodec_send_packet(m_aDecodeCtx, nullptr);
    if (ret != 0)
    {
        return;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(m_aDecodeCtx, rawFrame);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                break;
            }
            return;
        }
        ++g_aCollectFrameCnt;

        dstNbSamples = av_rescale_rnd(swr_get_delay(m_swrCtx, m_aDecodeCtx->sample_rate) + rawFrame->nb_samples,
            m_aEncodeCtx->sample_rate, m_aDecodeCtx->sample_rate, AV_ROUND_UP);
        if (dstNbSamples > maxDstNbSamples)
        {
            av_freep(&newFrame->data[0]);
            ret = av_samples_alloc(newFrame->data, newFrame->linesize, m_aEncodeCtx->channels,
                dstNbSamples, m_aEncodeCtx->sample_fmt, 1);
            if (ret < 0)
            {
                return;
            }
            maxDstNbSamples = dstNbSamples;
            m_aEncodeCtx->frame_size = dstNbSamples;
            m_nbSamples = newFrame->nb_samples;
        }
        newFrame->nb_samples = swr_convert(m_swrCtx, newFrame->data, dstNbSamples,
            (const uint8_t **)rawFrame->data, rawFrame->nb_samples);
        if (newFrame->nb_samples < 0)
        {
            return;
        }

        {
            std::unique_lock<std::mutex> lk(m_mtxABuf);
            m_cvABufNotFull.wait(lk, [newFrame, this] { return av_audio_fifo_space(m_aFifoBuf) >= newFrame->nb_samples; });
        }
        if (av_audio_fifo_write(m_aFifoBuf, (void **)newFrame->data, newFrame->nb_samples) < newFrame->nb_samples)
        {
            return;
        }
        m_cvABufNotEmpty.notify_one();
    }
    av_frame_free(&rawFrame);
    av_frame_free(&newFrame);
}

void ScreenRecord::FlushEncoders()
{
    int ret = -1;
    bool vBeginFlush = false;
    bool aBeginFlush = false;

    m_vCurPts = m_aCurPts = 0;

    int nFlush = 2;

    while (1)
    {
        AVPacket* pkt = av_packet_alloc();
        av_init_packet(pkt);
        if (av_compare_ts(m_vCurPts, m_oFmtCtx->streams[m_vOutIndex]->time_base,
            m_aCurPts, m_oFmtCtx->streams[m_aOutIndex]->time_base) <= 0)
        {
            if (!vBeginFlush)
            {
                vBeginFlush = true;
                ret = avcodec_send_frame(m_vEncodeCtx, nullptr);
                if (ret != 0)
                {
                    return;
                }
            }
            ret = avcodec_receive_packet(m_vEncodeCtx, pkt);
            if (ret < 0)
            {
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN))
                {
                    ret = 1;
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {

                    if (!(--nFlush))
                        break;
                    m_vCurPts = INT_MAX;
                    continue;
                }
                return;
            }
            pkt->stream_index = m_vOutIndex;
            
            av_packet_rescale_ts(pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
            m_vCurPts = pkt->pts;

            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            av_packet_free(&pkt);
        }
        else
        {
            if (!aBeginFlush)
            {
                aBeginFlush = true;
                ret = avcodec_send_frame(m_aEncodeCtx, nullptr);
                if (ret != 0)
                {
                    return;
                }
            }
            ret = avcodec_receive_packet(m_aEncodeCtx, pkt);
            if (ret < 0)
            {
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN))
                {
                    ret = 1;
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {
                    if (!(--nFlush))
                        break;
                    m_aCurPts = INT_MAX;
                    continue;
                }
                return;
            }
            pkt->stream_index = m_aOutIndex;
            av_packet_rescale_ts(pkt, m_aEncodeCtx->time_base, m_oFmtCtx->streams[m_aOutIndex]->time_base);
            m_aCurPts = pkt->pts;
            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            av_packet_free(&pkt);
        }
    }
}

void ScreenRecord::Release()
{
    if (m_vOutFrame)
    {
        av_frame_free(&m_vOutFrame);
        m_vOutFrame = nullptr;
    }
    if (m_vOutFrameBuf)
    {
        av_free(m_vOutFrameBuf);
        m_vOutFrameBuf = nullptr;
    }
    if (m_oFmtCtx)
    {
        avio_close(m_oFmtCtx->pb);
        avformat_free_context(m_oFmtCtx);
        m_oFmtCtx = nullptr;
    }

    if (m_aDecodeCtx)
    {
        avcodec_free_context(&m_aDecodeCtx);
        m_aDecodeCtx = nullptr;
    }
    if (m_vEncodeCtx)
    {
        avcodec_free_context(&m_vEncodeCtx);
        m_vEncodeCtx = nullptr;
    }
    if (m_aEncodeCtx)
    {
        avcodec_free_context(&m_aEncodeCtx);
        m_aEncodeCtx = nullptr;
    }
    if (m_vFifoBuf)
    {
        av_fifo_freep(&m_vFifoBuf);
        m_vFifoBuf = nullptr;
    }
    if (m_aFifoBuf)
    {
        av_audio_fifo_free(m_aFifoBuf);
        m_aFifoBuf = nullptr;
    }
    if (m_vFmtCtx)
    {
        avformat_close_input(&m_vFmtCtx);
        m_vFmtCtx = nullptr;
    }
    if (m_aFmtCtx)
    {
        avformat_close_input(&m_aFmtCtx);
        m_aFmtCtx = nullptr;
    }
}

void ScreenRecord::MuxThreadProc()
{
    int ret = -1;
    bool done = false;
    int vFrameIndex = 0, aFrameIndex = 0;

    avdevice_register_all();

    if (OpenVideo() < 0)
        return;
    if (OpenAudio() < 0)
        return;
    if (OpenOutput() < 0)
        return;

    InitVideoBuffer();
    InitAudioBuffer();

    std::thread screenRecord(&ScreenRecord::ScreenRecordThreadProc, this);
    std::thread soundRecord(&ScreenRecord::SoundRecordThreadProc, this);
    screenRecord.detach();
    soundRecord.detach();

    while (1)
    {
        if (m_state == RecordState::Stopped && !done)
            done = true;
        if (done)
        {
            std::unique_lock<std::mutex> vBufLock(m_mtxVBuf, std::defer_lock);
            std::unique_lock<std::mutex> aBufLock(m_mtxABuf, std::defer_lock);
            std::lock(vBufLock, aBufLock);
            if (av_fifo_size(m_vFifoBuf) < m_vOutFrameSize && av_audio_fifo_size(m_aFifoBuf) < m_nbSamples)
            {
                break;
            }
        }
        if (av_compare_ts(m_vCurPts, m_oFmtCtx->streams[m_vOutIndex]->time_base,
            m_aCurPts, m_oFmtCtx->streams[m_aOutIndex]->time_base) <= 0)
        {
            if (done)
            {
                std::lock_guard<std::mutex> lk(m_mtxVBuf);
                if (av_fifo_size(m_vFifoBuf) < m_vOutFrameSize)
                {
                    m_vCurPts = INT_MAX;
                    continue;
                }
            }
            else
            {
                std::unique_lock<std::mutex> lk(m_mtxVBuf);
                m_cvVBufNotEmpty.wait(lk, [this] { return av_fifo_size(m_vFifoBuf) >= m_vOutFrameSize; });
            }
            av_fifo_generic_read(m_vFifoBuf, m_vOutFrameBuf, m_vOutFrameSize, NULL);
            m_cvVBufNotFull.notify_one();

            m_vOutFrame->pts = vFrameIndex++;
            m_vOutFrame->format = m_vEncodeCtx->pix_fmt;
            m_vOutFrame->width = m_vEncodeCtx->width;
            m_vOutFrame->height = m_vEncodeCtx->height;

            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);
            ret = avcodec_send_frame(m_vEncodeCtx, m_vOutFrame);
            if (ret != 0)
            {
                av_packet_unref(pkt);
                continue;
            }
            ret = avcodec_receive_packet(m_vEncodeCtx, pkt);
            if (ret != 0)
            {
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = m_vOutIndex;
            av_packet_rescale_ts(pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
            m_vCurPts = pkt->pts;

            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            pkt = NULL;
        }
        else
        {
            if (done)
            {
                std::lock_guard<std::mutex> lk(m_mtxABuf);
                if (av_audio_fifo_size(m_aFifoBuf) < m_nbSamples)
                {
                    m_aCurPts = INT_MAX;
                    continue;
                }
            }
            else
            {
                std::unique_lock<std::mutex> lk(m_mtxABuf);
                m_cvABufNotEmpty.wait(lk, [this] { return av_audio_fifo_size(m_aFifoBuf) >= m_nbSamples; });
            }

            int ret = -1;
            AVFrame *aFrame = av_frame_alloc();
            aFrame->nb_samples = m_nbSamples;
            aFrame->channel_layout = m_aEncodeCtx->channel_layout;
            aFrame->format = m_aEncodeCtx->sample_fmt;
            aFrame->sample_rate = m_aEncodeCtx->sample_rate;
            aFrame->pts = m_nbSamples * aFrameIndex++;
            ret = av_frame_get_buffer(aFrame, 0);
            av_audio_fifo_read(m_aFifoBuf, (void **)aFrame->data, m_nbSamples);
            m_cvABufNotFull.notify_one();

            AVPacket* pkt = av_packet_alloc();
            av_init_packet(pkt);
            ret = avcodec_send_frame(m_aEncodeCtx, aFrame);
            if (ret != 0)
            {
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }
            ret = avcodec_receive_packet(m_aEncodeCtx, pkt);
            if (ret != 0)
            {
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = m_aOutIndex;

            av_packet_rescale_ts(pkt, m_aEncodeCtx->time_base, m_oFmtCtx->streams[m_aOutIndex]->time_base);

            m_aCurPts = pkt->pts;
            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            av_frame_free(&aFrame);
            av_packet_free(&pkt);
        }
    }
    FlushEncoders();
    ret = av_write_trailer(m_oFmtCtx);
    Release();
    isDone = true;
}

void ScreenRecord::ScreenRecordThreadProc()
{
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int y_size = m_width * m_height;
    AVFrame	*oldFrame = av_frame_alloc();
    AVFrame *newFrame = av_frame_alloc();

    int newFrameBufSize = av_image_get_buffer_size(m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
    uint8_t *newFrameBuf = (uint8_t*)av_malloc(newFrameBufSize);
    av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf,
        m_vEncodeCtx->pix_fmt, m_width, m_height, 1);

    while (m_state != RecordState::Stopped)
    {
        if (m_state == RecordState::Paused)
        {
            std::cout << "Pausing the video thread..." << std::endl;
            std::unique_lock<std::mutex> lk(m_mtxPause);
            m_cvNotPause.wait(lk, [this] { return m_state != RecordState::Paused; });
        }
        if (av_read_frame(m_vFmtCtx, &pkt) < 0)
        {
            continue;
        }
        if (pkt.stream_index != m_vIndex)
        {
            av_packet_unref(&pkt);
        }
        ret = avcodec_send_packet(m_vDecodeCtx, &pkt);
        if (ret != 0)
        {
            av_packet_unref(&pkt);
            continue;
        }
        ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
        if (ret != 0)
        {
            av_packet_unref(&pkt);
            continue;
        }
        ++g_vCollectFrameCnt;
        sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
            m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

        {
            std::unique_lock<std::mutex> lk(m_mtxVBuf);
            m_cvVBufNotFull.wait(lk, [this] { return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, NULL);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, NULL);
        m_cvVBufNotEmpty.notify_one();

        av_packet_unref(&pkt);
    }
    FlushVideoDecoder();

    av_free(newFrameBuf);
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
}

void ScreenRecord::SoundRecordThreadProc()
{
    int ret = -1;
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);
    int nbSamples = m_nbSamples;
    int dstNbSamples, maxDstNbSamples;
    AVFrame *rawFrame = av_frame_alloc();
    AVFrame *newFrame = AllocAudioFrame(m_aEncodeCtx, nbSamples);

    maxDstNbSamples = dstNbSamples = av_rescale_rnd(nbSamples,
        m_aEncodeCtx->sample_rate, m_aDecodeCtx->sample_rate, AV_ROUND_UP);

    while (m_state != RecordState::Stopped)
    {
        if (m_state == RecordState::Paused)
        {
            std::cout << "Pausing the audio thread..." << std::endl;
            std::unique_lock<std::mutex> lk(m_mtxPause);
            m_cvNotPause.wait(lk, [this] { return m_state != RecordState::Paused; });
        }
        if (av_read_frame(m_aFmtCtx, &pkt) < 0)
        {
            continue;
        }
        if (pkt.stream_index != m_aIndex)
        {
            av_packet_unref(&pkt);
            continue;
        }
        ret = avcodec_send_packet(m_aDecodeCtx, &pkt);
        if (ret != 0)
        {
            av_packet_unref(&pkt);
            continue;
        }
        ret = avcodec_receive_frame(m_aDecodeCtx, rawFrame);
        if (ret != 0)
        {
            av_packet_unref(&pkt);
            continue;
        }
        ++g_aCollectFrameCnt;

        dstNbSamples = av_rescale_rnd(swr_get_delay(m_swrCtx, m_aDecodeCtx->sample_rate) + rawFrame->nb_samples,
            m_aEncodeCtx->sample_rate, m_aDecodeCtx->sample_rate, AV_ROUND_UP);
        if (dstNbSamples > maxDstNbSamples)
        {
            av_freep(&newFrame->data[0]);
            ret = av_samples_alloc(newFrame->data, newFrame->linesize, m_aEncodeCtx->channels,
                dstNbSamples, m_aEncodeCtx->sample_fmt, 1);
            if (ret < 0)
            {
                return;
            }

            maxDstNbSamples = dstNbSamples;
            m_aEncodeCtx->frame_size = dstNbSamples;
            m_nbSamples = newFrame->nb_samples;	
        }

        newFrame->nb_samples = swr_convert(m_swrCtx, newFrame->data, dstNbSamples,
            (const uint8_t **)rawFrame->data, rawFrame->nb_samples);
        if (newFrame->nb_samples < 0)
        {
            return;
        }
        {
            std::unique_lock<std::mutex> lk(m_mtxABuf);
            m_cvABufNotFull.wait(lk, [newFrame, this] { return av_audio_fifo_space(m_aFifoBuf) >= newFrame->nb_samples; });
        }
        if (av_audio_fifo_write(m_aFifoBuf, (void **)newFrame->data, newFrame->nb_samples) < newFrame->nb_samples)
        {
            return;
        }
        m_cvABufNotEmpty.notify_one();
    }
    FlushAudioDecoder();
    av_frame_free(&rawFrame);
    av_frame_free(&newFrame);
}