g++ -g main.cpp ScreenRecord.cpp -I /usr/local/include/ $(pkg-config --libs libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample) -lz -lpthread -o main;
./main.exe