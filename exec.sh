temp=$(pactl list short sources | grep "RUNNING");
audio=$(echo ${temp} | sed -e 's/^[ \t]*//' | cut -d " " -f 2);
video=$(echo ${DISPLAY});
./main $video $audio
