g++ -g main.cpp ScreenRecord.cpp $(pkg-config --libs libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample) -lz -lpthread -o main;
temp=$(pactl list short sources | grep "RUNNING");
audio=$(echo ${temp} | sed -e 's/^[ \t]*//' | cut -d " " -f 2);
video=$(echo ${DISPLAY});
./main output.mp4 2560 1440 $video $audio