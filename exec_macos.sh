rm main
rm output.mp4
clang++ -std=c++11 -g main.cpp ScreenAudioCapture.cpp $(pkg-config --libs libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample) -lz -lpthread -o main;
./main