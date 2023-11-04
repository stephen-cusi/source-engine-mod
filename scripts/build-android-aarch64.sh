#!/bin/sh
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export PATH="$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH"
./waf configure -T release --build-game=hl2sb --prefix=modlauncher-waf/android --android=aarch64,host,21 --target=../aarch64 -8 --disable-warns &&
./waf install --target=client,server --strip
