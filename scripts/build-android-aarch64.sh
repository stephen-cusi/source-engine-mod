#!/bin/sh
export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export PATH="$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH"
./waf configure -T release --build-game=hl2sb --android=aarch64,host,21 --target=../aarch64 -8 --togles --disable-warns &&
./waf build --target=client,server
cp build/game/server/libserver.so srceng-androidwaf/android/lib/arm64-v8a
cp build/game/client/libclient.so srceng-androidwaf/android/lib/arm64-v8a

