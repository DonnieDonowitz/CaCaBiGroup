wget https://launchpad.net/ubuntu/+archive/primary/+sourcefiles/ffmpeg/7:4.2.2-1ubuntu1/ffmpeg_4.2.2.orig.tar.xz;
tar -xvf ffmpeg_4.2.2.orig.tar.xz;
cd ffmpeg-4.2.2;
sudo ./configure --enable-shared --enable-gpl --disable-x86asm --enable-libx264 --enable-libxcb --enable-libpulse --enable-indev=pulse --enable-indev=xcbgrab;
sudo make;
sudo make install;