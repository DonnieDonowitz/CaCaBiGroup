#pragma ide diagnostic ignored "cppcoreguidelines-narrowing-conversions"
#pragma ide diagnostic ignored "OCDFAInspection"
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
    }
#endif

#include "ScreenRecord.h"
#include <thread>
#include <fstream>
#include <utility>
#include <unistd.h>
#include <sstream>

#ifdef _WIN32
    #include <dshow.h>

#endif
#pragma clang diagnostic push


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

    //Just to avoid warnings
    , isDone(false)
    , m_width(0)
    , m_height(0)
    , m_audioBitrate(0)
    , m_vOutIndex(0)
    , m_aOutIndex(0)
    , m_vOutFrame(nullptr)
    , m_vOutFrameBuf(nullptr)
    , m_vOutFrameSize(0)
    , m_nbSamples(0)
{
}

void ScreenRecord::Init(std::string path, int width, int height, std::string video, const std::string& audio)
{
    isDone = false;
    m_filePath = std::move(path);
    m_width = width;
    m_height = height;
    m_fps = 30;
    m_audioBitrate = 128000;
    m_videoDevice = std::move(video);
#ifdef _WIN32
    m_audioDevice = "audio=" + GetMicrophoneDeviceName();
#else
    m_audioDevice = audio;
#endif
}

void ScreenRecord::Start()
{
    std::cout << "File path: " + m_filePath << std::endl;
    if (m_state == RecordState::NotStarted)
    {
        std::cout << "Start recording..." << std::endl;
        m_state = RecordState::Started;
        std::thread muxThread(&ScreenRecord::MuxThreadProc, this);
        muxThread.detach();
    }
    else if (m_state == RecordState::Paused)
    {
        std::cout << "Resume recording..." << std::endl;
        m_state = RecordState::Started;
        m_cvNotPause.notify_all();
    }
}

void ScreenRecord::Pause()
{
    m_state = RecordState::Paused;
    std::cout << "Pause recording..." << std::endl;
}

void ScreenRecord::Stop()
{
    std::cout << "Stop recording..." << std::endl;
    RecordState state = m_state;
    m_state = RecordState::Stopped;
    if (state == RecordState::Paused)
        m_cvNotPause.notify_all();
}

int ScreenRecord::OpenVideo()
{
    int ret = -1;
#ifdef _WIN32
    const AVInputFormat *ifmt = av_find_input_format("gdigrab");
#else
    const AVInputFormat *ifmt = av_find_input_format("x11grab");
#endif
    AVDictionary *options = nullptr;
    const AVCodec *decoder = nullptr;
    std::stringstream temp_fps;
    temp_fps << (m_fps);
    std::string fps = temp_fps.str();
    av_dict_set(&options, "framerate", fps.c_str(), 0);
    if (avformat_open_input(&m_vFmtCtx, m_videoDevice.c_str(), ifmt, &options) != 0)
    {
        std::cout << "Cant not open video input stream" << std::endl;
        return -1;
    }
    if (avformat_find_stream_info(m_vFmtCtx, nullptr) < 0)
    {
        std::cout << "Couldn't find stream information" << std::endl;
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
                std::cout << "could not find video decoder" << std::endl;
                return -1;
            }
            m_vDecodeCtx = avcodec_alloc_context3(decoder);
            if ((ret = avcodec_parameters_to_context(m_vDecodeCtx, stream->codecpar)) < 0)
            {
                std::cout << "Video avcodec_parameters_to_context failed,error code: " << ret << std::endl;
                return -1;
            }
            m_vIndex = i;
            break;
        }
    }
    if (avcodec_open2(m_vDecodeCtx, decoder, nullptr) < 0)
    {
        std::cout << "Could not open video codec" << std::endl;
        return -1;
    }

    m_swsCtx = sws_getContext(m_vDecodeCtx->width, m_vDecodeCtx->height, m_vDecodeCtx->pix_fmt,
                              m_width, m_height, AV_PIX_FMT_YUV420P,
                              SWS_FAST_BILINEAR, nullptr, nullptr, nullptr);
    return 0;
}

#ifdef _WIN32
[[maybe_unused]] static char *dup_wchar_to_utf8(wchar_t *w)
{
    char *s = nullptr;
    int l = WideCharToMultiByte(CP_UTF8, 0, w, -1, nullptr, 0, nullptr, nullptr);
    s = (char *)av_malloc(l);
    if (s)
        WideCharToMultiByte(CP_UTF8, 0, w, -1, s, l, nullptr, nullptr);
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
    const AVCodec *decoder = nullptr;
    std::cout << GetMicrophoneDeviceName() << std::endl;
#ifdef _WIN32
    const AVInputFormat *ifmt = av_find_input_format("dshow");
#else
    const AVInputFormat *ifmt = av_find_input_format("pulse");
#endif

    if (avformat_open_input(&m_aFmtCtx, m_audioDevice.c_str(), ifmt, nullptr) < 0)
    {
        std::cout << "Can not open audio input stream" << std::endl;
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
                std::cout << "Codec not found" << std::endl;
                return -1;
            }
            m_aDecodeCtx = avcodec_alloc_context3(decoder);
            if ((ret = avcodec_parameters_to_context(m_aDecodeCtx, stream->codecpar)) < 0)
            {
                std::cout << "Audio avcodec_parameters_to_context failed,error code: " << ret << std::endl;
                return -1;
            }
            m_aIndex = i;
            break;
        }
    }
    if (0 > avcodec_open2(m_aDecodeCtx, decoder, nullptr))
    {
        std::cout << "can not find or open audio decoder!" << std::endl;
        return -1;
    }
    return 0;
}

int ScreenRecord::OpenOutput()
{
    int ret = -1;
    AVStream *vStream = nullptr, *aStream = nullptr;
    std::string fileName = m_filePath;
    const char *outFileName = fileName.c_str();
    ret = avformat_alloc_output_context2(&m_oFmtCtx, nullptr, nullptr, outFileName);
    if (ret < 0)
    {
        std::cout << "avformat_alloc_output_context2 failed" << std::endl;
        return -1;
    }

    if (m_vFmtCtx->streams[m_vIndex]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
    {
        vStream = avformat_new_stream(m_oFmtCtx, nullptr);
        if (!vStream)
        {
            std::cout << "can not new stream for output!" << std::endl;
            return -1;
        }
        m_vOutIndex = vStream->index;
        vStream->time_base = AVRational{ 1, m_fps };

        m_vEncodeCtx = avcodec_alloc_context3(nullptr);
        if (nullptr == m_vEncodeCtx)
        {
            std::cout << "avcodec_alloc_context3 failed" << std::endl;
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
        m_vEncodeCtx->rc_buffer_size = 500 * 1000,
        m_vEncodeCtx->gop_size = 30;
        m_vEncodeCtx->max_b_frames = 3;
        m_vEncodeCtx->qmin = 10;	//2
        m_vEncodeCtx->qmax = 31;	//31
        m_vEncodeCtx->max_qdiff = 4;
        m_vEncodeCtx->me_range = 16;	//0
        m_vEncodeCtx->max_qdiff = 4;	//3
        m_vEncodeCtx->qcompress = 0.6;	//0.5

        const AVCodec *encoder = avcodec_find_encoder(m_vEncodeCtx->codec_id);
        if (!encoder)
        {
            std::cout << "Can not find the encoder, id: " << m_vEncodeCtx->codec_id << std::endl;
            return -1;
        }
        m_vEncodeCtx->codec_tag = 0;
        m_vEncodeCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
        ret = avcodec_open2(m_vEncodeCtx, encoder, nullptr);
        if (ret < 0)
        {
            std::cout << "Can not open encoder id: " << encoder->id << "error code: " << ret << std::endl;
            return -1;
        }
        ret = avcodec_parameters_from_context(vStream->codecpar, m_vEncodeCtx);
        if (ret < 0)
        {
            std::cout << "Output avcodec_parameters_from_context,error code:" << ret << std::endl;
            return -1;
        }
    }
    if (m_aFmtCtx->streams[m_aIndex]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
    {
        aStream = avformat_new_stream(m_oFmtCtx, nullptr);
        if (!aStream)
        {
            printf("can not new audio stream for output!\n");
            return -1;
        }
        m_aOutIndex = aStream->index;

        const AVCodec *encoder = avcodec_find_encoder(m_oFmtCtx->oformat->audio_codec);
        if (!encoder)
        {
            std::cout << "Can not find audio encoder, id: " << m_oFmtCtx->oformat->audio_codec << std::endl;
            return -1;
        }
        m_aEncodeCtx = avcodec_alloc_context3(encoder);
        if (nullptr == m_vEncodeCtx)
        {
            std::cout << "audio avcodec_alloc_context3 failed" << std::endl;
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
            std::cout << "Encoder does not support sample format " << av_get_sample_fmt_name(m_aEncodeCtx->sample_fmt) << std::endl;
            return -1;
        }

        ret = avcodec_open2(m_aEncodeCtx, encoder, nullptr);
        if (ret < 0)
        {
            std::cout << "Can not open the audio encoder, id: " << encoder->id << "error code: " << ret << std::endl;
            return -1;
        }
        ret = avcodec_parameters_from_context(aStream->codecpar, m_aEncodeCtx);
        if (ret < 0)
        {
            std::cout << "Output audio avcodec_parameters_from_context,error code:" << ret << std::endl;
            return -1;
        }

        m_swrCtx = swr_alloc();
        if (!m_swrCtx)
        {
            std::cout << "swr_alloc failed" << std::endl;
            return -1;
        }
        av_opt_set_int(m_swrCtx, "in_channel_count", m_aDecodeCtx->channels, 0);	//2
        av_opt_set_int(m_swrCtx, "in_sample_rate", m_aDecodeCtx->sample_rate, 0);	//44100
        av_opt_set_sample_fmt(m_swrCtx, "in_sample_fmt", m_aDecodeCtx->sample_fmt, 0);	//AV_SAMPLE_FMT_S16
        av_opt_set_int(m_swrCtx, "out_channel_count", m_aEncodeCtx->channels, 0);	//2
        av_opt_set_int(m_swrCtx, "out_sample_rate", m_aEncodeCtx->sample_rate, 0);	//44100
        av_opt_set_sample_fmt(m_swrCtx, "out_sample_fmt", m_aEncodeCtx->sample_fmt, 0);	//AV_SAMPLE_FMT_FLTP

        if ((ret = swr_init(m_swrCtx)) < 0)
        {
            std::cout << "swr_init failed" << std::endl;
            return -1;
        }
    }

    if (!(m_oFmtCtx->oformat->flags & AVFMT_NOFILE))
    {
        if (avio_open(&m_oFmtCtx->pb, outFileName, AVIO_FLAG_WRITE) < 0)
        {
            std::cout << "can not open output file handle!" << std::endl;
            return -1;
        }
    }
    if (avformat_write_header(m_oFmtCtx, nullptr) < 0)
    {
        std::cout << "can not write the header of the output file!" << std::endl;
        return -1;
    }
    return 0;
}

[[maybe_unused]] std::string ScreenRecord::GetSpeakerDeviceName()
{
    char sName[256] = { 0 };
    std::string speaker;
    bool bRet = false;
#ifdef _WIN32
    ::CoInitialize(nullptr);

    ICreateDevEnum* pCreateDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum,
        nullptr,
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

        IPropertyBag* pBag = nullptr;
        hr = pM->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&pBag);
        if (SUCCEEDED(hr))
        {
            VARIANT var;
            var.vt = VT_BSTR;
            hr = pBag->Read(L"FriendlyName", &var, nullptr);
            if (hr == NOERROR)
            {
                WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, sName, 256, "", nullptr);
                std::cout << "Speaker Name: " << sName << std::endl;
                speaker = std::string(sName);
                //speaker = QString::fromLocal8Bit(sName);
                SysFreeString(var.bstrVal);
            }
            pBag->Release();
        }
        pM->Release();
        bRet = true;
    }
    pCreateDevEnum = nullptr;
    pEm = nullptr;
    ::CoUninitialize();
#endif
    return speaker;
}

std::string ScreenRecord::GetMicrophoneDeviceName()
{
    char sName[256] = { 0 };
    std::string capture;
    bool bRet = false;
#ifdef _WIN32
    ::CoInitialize(nullptr);

    ICreateDevEnum* pCreateDevEnum;
    HRESULT hr = CoCreateInstance(CLSID_SystemDeviceEnum,
        nullptr,
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

        IPropertyBag* pBag = nullptr;
        hr = pM->BindToStorage(nullptr, nullptr, IID_IPropertyBag, (void**)&pBag);
        if (SUCCEEDED(hr))
        {
            VARIANT var;
            var.vt = VT_BSTR;
            hr = pBag->Read(L"FriendlyName", &var, nullptr);
            if (hr == NOERROR)
            {
                WideCharToMultiByte(CP_ACP, 0, var.bstrVal, -1, sName, 256, "", nullptr);
                //std::cout << "Microphone Name: " << sName << std::endl;
                capture = std::string(sName);
                //capture = QString::fromLocal8Bit(sName);
                SysFreeString(var.bstrVal);
            }
            pBag->Release();
        }
        pM->Release();
        bRet = true;
    }
    pCreateDevEnum = nullptr;
    pEm = nullptr;
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
            std::cout << "av_frame_get_buffer failed" << std::endl;
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
        std::cout << "av_fifo_alloc_array failed" << std::endl;
        return;
    }
}

void ScreenRecord::InitAudioBuffer()
{
    m_nbSamples = m_aEncodeCtx->frame_size;
    if (!m_nbSamples)
    {
        std::cout << "m_nbSamples==0" << std::endl;
        m_nbSamples = 1024;
    }
    m_aFifoBuf = av_audio_fifo_alloc(m_aEncodeCtx->sample_fmt, m_aEncodeCtx->channels, 30 * m_nbSamples);
    if (!m_aFifoBuf)
    {
        std::cout << "av_audio_fifo_alloc failed" << std::endl;
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
        std::cout << "flush video avcodec_send_packet failed, ret: " << ret << std::endl;
        return;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                std::cout << "flush video decoder finished" << std::endl;
                break;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                std::cout << "flush EAGAIN avcodec_receive_frame" << std::endl;
            }
            std::cout << "flush video avcodec_receive_frame error, ret: " << ret << std::endl;
            return;
        }
        ++g_vCollectFrameCnt;
        sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
                  m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

        {
            std::unique_lock<std::mutex> lk(m_mtxVBuf);
            m_cvVBufNotFull.wait(lk, [this] { return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, nullptr);
        m_cvVBufNotEmpty.notify_one();
    }
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
    std::cout << "video collect frame count: " << g_vCollectFrameCnt << std::endl;
}

void ScreenRecord::FlushAudioDecoder()
{
    int ret = -1;
    int dstNbSamples, maxDstNbSamples;
    AVFrame *rawFrame = av_frame_alloc();
    AVFrame *newFrame = AllocAudioFrame(m_aEncodeCtx, m_nbSamples);
    maxDstNbSamples = dstNbSamples = av_rescale_rnd(m_nbSamples,
                                                    m_aEncodeCtx->sample_rate, m_aDecodeCtx->sample_rate, AV_ROUND_UP);

    ret = avcodec_send_packet(m_aDecodeCtx, nullptr);
    if (ret != 0)
    {
        std::cout << "flush audio avcodec_send_packet  failed, ret: " << ret << std::endl;
        return;
    }
    while (ret >= 0)
    {
        ret = avcodec_receive_frame(m_aDecodeCtx, rawFrame);
        if (ret < 0)
        {
            if (ret == AVERROR_EOF)
            {
                std::cout << "flush audio decoder finished" << std::endl;
                break;
            }
            else if (ret == AVERROR(EAGAIN))
            {
                std::cout << "flush audio EAGAIN avcodec_receive_frame" << std::endl;
            }
            std::cout << "flush audio avcodec_receive_frame error, ret: " << ret << std::endl;
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

    std::cout << "audio collect frame count: " << g_aCollectFrameCnt << std::endl;
}

void ScreenRecord::FlushEncoders()
{
    int ret = -1;
    bool vBeginFlush = false;
    bool aBeginFlush = false;

    m_vCurPts = m_aCurPts = 0;

    int nFlush = 2;

    while (true)
    {
        AVPacket* pkt = av_packet_alloc();
        if (av_compare_ts(m_vCurPts, m_oFmtCtx->streams[m_vOutIndex]->time_base,
                          m_aCurPts, m_oFmtCtx->streams[m_aOutIndex]->time_base) <= 0)
        {
            if (!vBeginFlush)
            {
                vBeginFlush = true;
                ret = avcodec_send_frame(m_vEncodeCtx, nullptr);
                if (ret != 0)
                {
                    std::cout << "flush video avcodec_send_frame failed, ret: " << ret << std::endl;
                    return;
                }
            }
            ret = avcodec_receive_packet(m_vEncodeCtx, pkt);
            if (ret < 0)
            {
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN))
                {
                    std::cout << "flush video EAGAIN avcodec_receive_packet" << std::endl;
                    ret = 1;
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {
                    std::cout << "flush video encoder finished" << std::endl;
                    //break;
                    if (!(--nFlush))
                        break;
                    m_vCurPts = INT_MAX;
                    continue;
                }
                std::cout << "flush video avcodec_receive_packet failed, ret: " << ret << std::endl;
                return;
            }
            pkt->stream_index = m_vOutIndex;
            //将pts从编码层的timebase转成复用层的timebase
            av_packet_rescale_ts(pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);
            m_vCurPts = pkt->pts;
            //qDebug() << "m_vCurPts: " << m_vCurPts;

            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            if (ret == 0)
                std::cout << "flush Write video packet id: " << ++g_vEncodeFrameCnt << std::endl;
            else
                std::cout << "flush video av_interleaved_write_frame failed, ret:" << ret << std::endl;
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
                    std::cout << "flush audio avcodec_send_frame failed, ret: " << ret << std::endl;
                    return;
                }
            }
            ret = avcodec_receive_packet(m_aEncodeCtx, pkt);
            if (ret < 0)
            {
                av_packet_unref(pkt);
                if (ret == AVERROR(EAGAIN))
                {
                    std::cout << "flush EAGAIN avcodec_receive_packet" << std::endl;
                    ret = 1;
                    continue;
                }
                else if (ret == AVERROR_EOF)
                {
                    std::cout << "flush audio encoder finished" << std::endl;
                    /*break;*/
                    if (!(--nFlush))
                        break;
                    m_aCurPts = INT_MAX;
                    continue;
                }
                std::cout << "flush audio avcodec_receive_packet failed, ret: " << ret << std::endl;
                return;
            }
            pkt->stream_index = m_aOutIndex;
            //将pts从编码层的timebase转成复用层的timebase
            av_packet_rescale_ts(pkt, m_aEncodeCtx->time_base, m_oFmtCtx->streams[m_aOutIndex]->time_base);
            m_aCurPts = pkt->pts;
            std::cout << "m_aCurPts: " << m_aCurPts << std::endl;
            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            if (ret == 0)
                std::cout << "flush write audio packet id: " << ++g_aEncodeFrameCnt << std::endl;
            else
                std::cout << "flush audio av_interleaved_write_frame failed, ret: " << ret << std::endl;
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

    while (true)
    {
        if (m_state == RecordState::Stopped && !done)
            done = true;
        if (done)
        {
            std::unique_lock<std::mutex> vBufLock(m_mtxVBuf, std::defer_lock);
            std::unique_lock<std::mutex> aBufLock(m_mtxABuf, std::defer_lock);
            std::lock(vBufLock, aBufLock);
            if (av_fifo_size(m_vFifoBuf) < m_vOutFrameSize &&
            av_audio_fifo_size(m_aFifoBuf) < m_nbSamples)
            {
                std::cout << "both video and audio fifo buf are empty, break" << std::endl;
                break;
            }
        }
        if (av_compare_ts(m_vCurPts, m_oFmtCtx->streams[m_vOutIndex]->time_base,
                          m_aCurPts, m_oFmtCtx->streams[m_aOutIndex]->time_base) <= 0)
            /*	if (av_compare_ts(vCurPts, m_vEncodeCtx->time_base,
                    aCurPts, m_aEncodeCtx->time_base) <= 0)*/
            {
            if (done)
            {
                std::lock_guard<std::mutex> lk(m_mtxVBuf);
                if (av_fifo_size(m_vFifoBuf) < m_vOutFrameSize)
                {
                    std::cout << "video write done" << std::endl;
                    //break;
                    //m_vCurPts = 0x7ffffffffffffffe;
                    m_vCurPts = INT_MAX;
                    continue;
                }
            }
            else
            {
                std::unique_lock<std::mutex> lk(m_mtxVBuf);
                m_cvVBufNotEmpty.wait(lk, [this] { return av_fifo_size(m_vFifoBuf) >= m_vOutFrameSize; });
            }
            av_fifo_generic_read(m_vFifoBuf, m_vOutFrameBuf, m_vOutFrameSize, nullptr);
            m_cvVBufNotFull.notify_one();

            //m_vOutFrame->pts = vFrameIndex * ((m_oFmtCtx->streams[m_vOutIndex]->time_base.den / m_oFmtCtx->streams[m_vOutIndex]->time_base.num) / m_fps);
            m_vOutFrame->pts = vFrameIndex++;
            //m_vOutFrame->pts = av_frame_get_best_effort_timestamp(m_vOutFrame);
            m_vOutFrame->format = m_vEncodeCtx->pix_fmt;
            m_vOutFrame->width = m_vEncodeCtx->width;
            m_vOutFrame->height = m_vEncodeCtx->height;

            AVPacket* pkt = av_packet_alloc();
            ret = avcodec_send_frame(m_vEncodeCtx, m_vOutFrame);
            if (ret != 0)
            {
                std::cout << "video avcodec_send_frame failed, ret: " << ret << std::endl;
                av_packet_unref(pkt);
                continue;
            }
            ret = avcodec_receive_packet(m_vEncodeCtx, pkt);
            if (ret != 0)
            {
                //std::cout << "video avcodec_receive_packet failed, ret: " << ret << std::endl;
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = m_vOutIndex;
            av_packet_rescale_ts(pkt, m_vEncodeCtx->time_base, m_oFmtCtx->streams[m_vOutIndex]->time_base);

            m_vCurPts = pkt->pts;
            //std::cout << "m_vCurPts: " << m_vCurPts << std::endl;

            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            if (ret == 0)
            {
                //std::cout << "Write video packet id: " << ++g_vEncodeFrameCnt << std::endl;
            }
            else
                std::cout << "video av_interleaved_write_frame failed, ret:" << ret << std::endl;
            av_packet_free(&pkt);
        }
        else
        {
            if (done)
            {
                std::lock_guard<std::mutex> lk(m_mtxABuf);
                if (av_audio_fifo_size(m_aFifoBuf) < m_nbSamples)
                {
                    std::cout << "audio write done" << std::endl;
                    //m_aCurPts = 0x7fffffffffffffff;
                    m_aCurPts = INT_MAX;
                    continue;
                }
            }
            else
            {
                std::unique_lock<std::mutex> lk(m_mtxABuf);
                m_cvABufNotEmpty.wait(lk, [this] { return av_audio_fifo_size(m_aFifoBuf) >= m_nbSamples; });
            }

            ret = -1;
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
            ret = avcodec_send_frame(m_aEncodeCtx, aFrame);
            if (ret != 0)
            {
                std::cout << "audio avcodec_send_frame failed, ret: " << ret << std::endl;
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }
            ret = avcodec_receive_packet(m_aEncodeCtx, pkt);
            if (ret != 0)
            {
                //std::cout << "audio avcodec_receive_packet failed, ret: " << ret << std::endl;
                av_frame_free(&aFrame);
                av_packet_unref(pkt);
                continue;
            }
            pkt->stream_index = m_aOutIndex;

            av_packet_rescale_ts(pkt, m_aEncodeCtx->time_base, m_oFmtCtx->streams[m_aOutIndex]->time_base);

            m_aCurPts = pkt->pts;
            //std::cout << "aCurPts: " << m_aCurPts << std::endl;

            ret = av_interleaved_write_frame(m_oFmtCtx, pkt);
            if (ret == 0)
            {
                //std::cout << "Write audio packet id: " << ++g_aEncodeFrameCnt << std::endl;
            }
            else
                std::cout << "audio av_interleaved_write_frame failed, ret: " << ret << std::endl;

            av_frame_free(&aFrame);
            av_packet_free(&pkt);
        }
    }
    FlushEncoders();
    ret = av_write_trailer(m_oFmtCtx);
    if(ret < 0) {
        std::cout << "could not write trailer" << std::endl;
    }
    Release();
    std::cout << "parent thread exit" << std::endl;
}

void ScreenRecord::ScreenRecordThreadProc()
{
    int ret = -1;
    AVPacket* pkt = av_packet_alloc();
    int y_size = m_width * m_height;
    AVFrame	*oldFrame = av_frame_alloc();
    AVFrame *newFrame = av_frame_alloc();

    int newFrameBufSize = av_image_get_buffer_size(m_vEncodeCtx->pix_fmt, m_width, m_height, 1);
    auto *newFrameBuf = (uint8_t*)av_malloc(newFrameBufSize);
    av_image_fill_arrays(newFrame->data, newFrame->linesize, newFrameBuf,
                         m_vEncodeCtx->pix_fmt, m_width, m_height, 1);

    while (m_state != RecordState::Stopped)
    {
        if (m_state == RecordState::Paused)
        {
            std::unique_lock<std::mutex> lk(m_mtxPause);
            m_cvNotPause.wait(lk, [this] { return m_state != RecordState::Paused; });
        }
        if (av_read_frame(m_vFmtCtx, pkt) < 0)
        {
            std::cout << "video av_read_frame < 0" << std::endl;
            continue;
        }
        if (pkt->stream_index != m_vIndex)
        {
            std::cout << "not a video packet from video input" << std::endl;
            av_packet_unref(pkt);
        }
        ret = avcodec_send_packet(m_vDecodeCtx, pkt);
        if (ret != 0)
        {
            std::cout << "video avcodec_send_packet failed, ret:" << ret << std::endl;
            av_packet_unref(pkt);
            continue;
        }
        ret = avcodec_receive_frame(m_vDecodeCtx, oldFrame);
        if (ret != 0)
        {
            std::cout << "video avcodec_receive_frame failed, ret:" << ret << std::endl;
            av_packet_unref(pkt);
            continue;
        }
        //AVRational tb = m_vFmtCtx->streams[m_vIndex]->time_base;
        //int currentPos = pkt.pts * av_q2d(tb);

        ++g_vCollectFrameCnt;
        sws_scale(m_swsCtx, (const uint8_t* const*)oldFrame->data, oldFrame->linesize, 0,
                  m_vEncodeCtx->height, newFrame->data, newFrame->linesize);

        {
            std::unique_lock<std::mutex> lk(m_mtxVBuf);
            m_cvVBufNotFull.wait(lk, [this] { return av_fifo_space(m_vFifoBuf) >= m_vOutFrameSize; });
        }
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[0], y_size, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[1], y_size / 4, nullptr);
        av_fifo_generic_write(m_vFifoBuf, newFrame->data[2], y_size / 4, nullptr);
        m_cvVBufNotEmpty.notify_one();

        av_packet_unref(pkt);
    }
    FlushVideoDecoder();

    av_free(newFrameBuf);
    av_frame_free(&oldFrame);
    av_frame_free(&newFrame);
    std::cout << "screen record thread exit" << std::endl;
}

void ScreenRecord::SoundRecordThreadProc()
{
    int ret = -1;
    AVPacket* pkt = av_packet_alloc();
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
            std::unique_lock<std::mutex> lk(m_mtxPause);
            m_cvNotPause.wait(lk, [this] { return m_state != RecordState::Paused; });
        }
        if (av_read_frame(m_aFmtCtx, pkt) < 0)
        {
            std::cout << "audio av_read_frame < 0" << std::endl;
            continue;
        }
        if (pkt->stream_index != m_aIndex)
        {
            std::cout << "not a audio packet" << std::endl;
            av_packet_unref(pkt);
            continue;
        }
        ret = avcodec_send_packet(m_aDecodeCtx, pkt);
        if (ret != 0)
        {
            std::cout << "audio avcodec_send_packet failed, ret: " << ret << std::endl;
            av_packet_unref(pkt);
            continue;
        }
        ret = avcodec_receive_frame(m_aDecodeCtx, rawFrame);
        if (ret != 0)
        {
            std::cout << "audio avcodec_receive_frame failed, ret: " << ret << std::endl;
            av_packet_unref(pkt);
            continue;
        }
        ++g_aCollectFrameCnt;

        dstNbSamples = av_rescale_rnd(swr_get_delay(m_swrCtx, m_aDecodeCtx->sample_rate) + rawFrame->nb_samples,
                                      m_aEncodeCtx->sample_rate, m_aDecodeCtx->sample_rate, AV_ROUND_UP);
        if (dstNbSamples > maxDstNbSamples)
        {
            av_freep(&newFrame->data[0]);
            //nb_samples*nb_channels*Bytes_sample_fmt
            ret = av_samples_alloc(newFrame->data, newFrame->linesize, m_aEncodeCtx->channels,
                                   dstNbSamples, m_aEncodeCtx->sample_fmt, 1);
            if (ret < 0)
            {
                std::cout << "av_samples_alloc failed" << std::endl;
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
            std::cout << "swr_convert error" << std::endl;
            return;
        }
        {
            std::unique_lock<std::mutex> lk(m_mtxABuf);
            m_cvABufNotFull.wait(lk, [newFrame, this] { return av_audio_fifo_space(m_aFifoBuf) >= newFrame->nb_samples; });
        }
        if (av_audio_fifo_write(m_aFifoBuf, (void **)newFrame->data, newFrame->nb_samples) < newFrame->nb_samples)
        {
            std::cout << "av_audio_fifo_write" << std::endl;
            return;
        }
        m_cvABufNotEmpty.notify_one();
    }
    FlushAudioDecoder();
    av_frame_free(&rawFrame);
    av_frame_free(&newFrame);
    std::cout << "sound record thread exit" << std::endl;
}
#pragma clang diagnostic pop