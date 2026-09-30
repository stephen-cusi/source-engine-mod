#!/bin/sh
if [ -z "$ANDROID_NDK_HOME" ]; then
	wget https://dl.google.com/android/repository/android-ndk-r27b-linux.zip -nc -q
	unzip -q android-ndk-r27b-linux.zip
	export ANDROID_NDK_HOME=$PWD/android-ndk-r27b
fi

case "$(uname -m)" in
	aarch64) NDK_PREBUILT=linux-aarch64 ;;
	*) NDK_PREBUILT=linux-x86_64 ;;
esac
export PATH="$ANDROID_NDK_HOME/toolchains/llvm/prebuilt/$NDK_PREBUILT/bin:$PATH"

./waf configure -T release --build-games=hl2sb --android=aarch64,host,21 --target=../aarch64 --disable-warns &&
./waf build --target=client,server
cp build/game/server/libserver.so srceng-androidwaf/android/lib/arm64-v8a
cp build/game/client/libclient.so srceng-androidwaf/android/lib/arm64-v8a
# HL2SB (sbrust): Lua 宿主库 -- libclient/libserver 的 DT_NEEDED 依赖，
# 必须随 APK 的 jniLibs 一起打包（srceng-androidwaf 仓打包时带上它）。
cp build/lua/liblua_shared.so srceng-androidwaf/android/lib/arm64-v8a
