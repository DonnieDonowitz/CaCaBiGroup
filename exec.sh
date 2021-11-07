clang++ -std=c++11 -g main.cpp ScreenRecord.cpp $(pkg-config --libs libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample) -lz -lpthread -o main;
audio=$(echo ${DISPLAY});
video=$(echo ${DISPLAY});
./main output.mp4 2560 1600 $video $audio