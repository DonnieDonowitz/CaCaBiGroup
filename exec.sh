g++ -g -Wno-all main.cpp ScreenRecord.cpp $(pkg-config --libs libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample) -lz -lpthread -o main;
./main