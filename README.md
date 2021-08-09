# CaCaBiGroup

Linee di codice per installare correttamente ffmpeg (cd in cartella di download di ffmpeg)
``` 
./configure --enable-shared --enable-libx264 --enable-gpl
make
sudo make install
```

compilazione:
```
g++ -g main.cpp ScreenRecord.cpp $(pkg-config --libs libavformat libavcodec libavdevice libavfilter libavutil libswscale libswresample) -lz -lpthread -o main
```

esecuzione:
```
./main
```
per interrompere inserire un carattere qualsiasi e dare Invio, dopo aver configurato il file di output il programma si arresterà.
Il file output.mp4 conterrà il video.
