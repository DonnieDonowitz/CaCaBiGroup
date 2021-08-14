# CaCaBiGroup

## Installazione ffmpeg
Linee di codice per installare correttamente ffmpeg (cd in cartella di download di ffmpeg)
``` 
./configure --enable-shared --enable-libx264 --enable-gpl
make
sudo make install
```

## Compilazione ed Esecuzione:
```
bash exec.sh
```
Per mettere in pausa il recording premere `p` ed invio.


Per riprendere il recording dopo una pausa premere `r` ed invio.


Per stoppare il recording premere `s` ed invio.


Il file output.mp4 conterrà il video.
