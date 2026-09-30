#!/bin/sh
git submodule init && git submodule update

sudo apt-get update
sudo apt-get install -f -y libopenal-dev g++-multilib gcc-multilib libpng-dev libjpeg-dev libfreetype6-dev libfontconfig1-dev libcurl4-gnutls-dev libsdl2-dev zlib1g-dev libbz2-dev libedit-dev

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

python3 ./waf configure -T release --prefix=../android_build --android=aarch64,host,21 --target=../android_build/aarch64 --disable-warns --togles
python3 ./waf install --strip
