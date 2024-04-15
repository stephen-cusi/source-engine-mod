#!/bin/sh
git submodule init && git submodule update

wget https://dl.google.com/android/repository/android-ndk-r10e-linux-x86_64.zip -o /dev/null -nc    
wget https://github.com/llvm/llvm-project/releases/download/llvmorg-11.1.0/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz -o /dev/null -nc

unzip android-ndk-r10e-linux-x86_64.zip -n
tar -xf clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04.tar.xz --skip-old-files

export ANDROID_NDK_HOME=$PWD/android-ndk-r10e/
export PATH="$PWD/clang+llvm-11.1.0-x86_64-linux-gnu-ubuntu-16.04/bin:$PATH"

python3 ./waf configure -T release --prefix=../android/build --android=aarch64,host,21 --target=../android/aarch64 --disable-warns --togles
python3 ./waf install --strip
