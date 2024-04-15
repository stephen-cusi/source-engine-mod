#!/bin/sh
git submodule init && git submodule update

sudo apt-get update
sudo apt-get install -f -y libopenal-dev g++-multilib gcc-multilib libpng-dev libjpeg-dev libfreetype6-dev libfontconfig1-dev libcurl4-gnutls-dev libsdl2-dev zlib1g-dev libbz2-dev libedit-dev

wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -nc    
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz -nc

unzip android-ndk-r10e-linux-x86_64.zip
tar -xf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz

export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export PATH=$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH

python3 ./waf configure -T release --prefix=../android_build --android=armeabi-v7a-hard,host,21 --target=../android_build/armeabi-v7a --disable-warns -4 --togles
python3 ./waf install --strip

# # 移除多余lib
# rm modlauncher-waf/android/lib/armeabi-v7a/libtier0.so
# rm modlauncher-waf/android/lib/armeabi-v7a/libsteam_api.so
# rm modlauncher-waf/android/lib/armeabi-v7a/libvstdlib.so
# # 加引擎脑子纯粹喀了屎