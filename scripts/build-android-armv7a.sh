#!/bin/sh

git submodule init && git submodule update
wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
./waf configure -T release --build-game=hl2sb --android=armeabi-v7a-hard,host,21 --target=../armeabi-v7a --togles --disable-warns &&
./waf install --target=client,server
