#pragma once

extern "C"
{
    #include "libavcodec/avcodec.h"
    #include "libavformat/avformat.h"
    #include "libswscale/swscale.h"
    #include "libavdevice/avdevice.h"
    #include "libavutil/audio_fifo.h"
    #include "libavutil/imgutils.h"
    #include "libswresample/swresample.h"
    #include "libavutil/avassert.h"
};

#include <atomic>
#include <mutex>
#include <condition_variable>
#include <cstring>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <iostream>
#include <thread>
#include <fstream>
#include <signal.h>
#include <unistd.h>