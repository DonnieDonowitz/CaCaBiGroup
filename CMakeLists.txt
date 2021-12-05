CMAKE_MINIMUM_REQUIRED(VERSION 3.13)
PROJECT(Screen-Audio-Capture-FFMPEG)
SET(CMAKE_CXX_STANDARD 17)
set(CMAKE_FIND_DEBUG_MODE 1)

set(CMAKE_C_FLAGS "-Wno-narrowing")

include_directories(./include/)

link_directories(./lib/)

set(SOURCES main.cpp ScreenAudioCapture.cpp ListAVDevices.cpp)

ADD_EXECUTABLE(main ${SOURCES})

target_link_libraries(
        main
        avcodec
        avdevice
        avfilter
        avformat
        avutil
        postproc
        swresample
        swscale
)