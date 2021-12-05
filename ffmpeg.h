//
// Created by mattb on 09.11.21.
//

#ifndef SCREENAUDIOCAPTURE_FFMPEG_H
#define SCREENAUDIOCAPTURE_FFMPEG_H


#pragma warning(disable:4819)

extern "C" {
#define __STDC_CONSTANT_MACROS
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavformat/avio.h>
#include <libavutil/avutil.h>
#include <libavutil/file.h>
#include <ctime>
#include <libavutil/audio_fifo.h>
#include <libavutil/imgutils.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>
#include <libavdevice/avdevice.h>
}

#endif //SCREENAUDIOCAPTURE_FFMPEG_H