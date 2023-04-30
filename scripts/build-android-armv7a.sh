#!/bin/sh

git submodule init && git submodule update
wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz -o /dev/null
tar -xf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz
export PATH="$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH"
./waf configure -T release --build-game=missinginfo --android=armeabi-v7a-hard,host,21 --target=../armeabi-v7a --disable-warns &&
./waf build --target=client,server
cp build/game/server/libserver.so modlauncher-waf/android/lib/armeabi-v7a
cp build/game/client/libclient.so modlauncher-waf/android/lib/armeabi-v7a
rm modlauncher-waf/android/armeabi-v7a/.deleteme
