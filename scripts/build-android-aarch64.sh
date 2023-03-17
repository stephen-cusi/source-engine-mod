#!/bin/sh

git submodule init && git submodule update
wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
./waf configure -T release --build-game=hl2sb --android=aarch64,host,21 --target=../aarch64 -8 --togles --disable-warns &&
./waf install --target=client,server
