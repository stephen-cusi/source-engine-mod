#!/bin/sh
git submodule init && git submodule update
wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null
unzip android-ndk-r10e-linux-x86_64.zip
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz -o /dev/null
tar -xf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz
export PATH="$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH"
./waf configure -T release --prefix=modlauncher-waf/android --android=armeabi-v7a-hard,host,21 --target=../armeabi-v7a --disable-warns -4 --build-game=nh &&
./waf install --target=client,server --strip
# 移除多余lib
rm modlauncher-waf/android/lib/armeabi-v7a/libtier0.so
rm modlauncher-waf/android/lib/armeabi-v7a/libsteam_api.so
rm modlauncher-waf/android/lib/armeabi-v7a/libvstdlib.so
# 加引擎脑子纯粹喀了屎