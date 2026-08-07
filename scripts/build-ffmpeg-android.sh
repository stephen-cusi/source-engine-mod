#!/bin/sh
# Builds a minimal static FFmpeg for the given Android arch and installs it
# next to the other android prebuilt libs (lib/android/<cpu>).
#
# Usage: build-ffmpeg-android.sh <aarch64|armeabi-v7a|armeabi-v7a-hard>
#
# Requires ANDROID_NDK_HOME to point at an NDK (r10e like the engine CI) and
# clang/lld on PATH (the engine scripts put the standalone LLVM bin dir first).

set -e

if [ -z "$ANDROID_NDK_HOME" ] || [ ! -d "$ANDROID_NDK_HOME" ]; then
	echo "ANDROID_NDK_HOME must point to the Android NDK" >&2
	exit 1
fi

case "$1" in
	aarch64)
		ffarch="aarch64"
		ndk_arch="arm64"
		libcpu="aarch64"
		clang_target="aarch64-linux-android21"
		gcc_toolchain="$ANDROID_NDK_HOME/toolchains/aarch64-linux-android-4.9/prebuilt/linux-x86_64"
		hardfp=0
		;;
	armeabi-v7a-hard)
		ffarch="arm"
		ndk_arch="arm"
		libcpu="arm"
		clang_target="arm-linux-androideabi21"
		gcc_toolchain="$ANDROID_NDK_HOME/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64"
		hardfp=1
		;;
	armeabi-v7a)
		ffarch="arm"
		ndk_arch="arm"
		libcpu="arm"
		clang_target="arm-linux-androideabi21"
		gcc_toolchain="$ANDROID_NDK_HOME/toolchains/arm-linux-androideabi-4.9/prebuilt/linux-x86_64"
		hardfp=0
		;;
	*)
		echo "Unknown Android arch: $1" >&2
		echo "Supported: aarch64, armeabi-v7a, armeabi-v7a-hard" >&2
		exit 1
		;;
esac

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
FFVER="4.4.4"
FFDIR="$ROOT/thirdparty/ffmpeg-$FFVER"
PREFIX="$ROOT/lib/android/$libcpu"
SYSROOT="$ANDROID_NDK_HOME/platforms/android-21/arch-$ndk_arch"

if [ ! -d "$SYSROOT" ]; then
	echo "Missing NDK sysroot: $SYSROOT" >&2
	exit 1
fi

if [ -f "$PREFIX/libavcodec.a" ]; then
	echo "FFmpeg already built for $libcpu, skipping."
	exit 0
fi

if [ ! -d "$FFDIR" ]; then
	mkdir -p "$ROOT/thirdparty"
	echo "Downloading FFmpeg $FFVER..."
	TARBALL=/tmp/ffmpeg-$FFVER.tar.xz
	if [ ! -f "$TARBALL" ]; then
		# GitHub mirror first (fast/reliable), ffmpeg.org as fallback
		if ! curl -L --fail --retry 5 --retry-all-errors --connect-timeout 30 --max-time 600 -o "$TARBALL" \
			"https://github.com/FFmpeg/FFmpeg/archive/refs/tags/n$FFVER.tar.gz"; then
			rm -f "$TARBALL"
			curl -L --fail --retry 5 --retry-all-errors --connect-timeout 30 --max-time 600 -o "$TARBALL" \
				"https://ffmpeg.org/releases/ffmpeg-$FFVER.tar.xz"
		fi
	fi
	rm -rf "$FFDIR"
	tar -xf "$TARBALL" -C "$ROOT/thirdparty"
	# GitHub tarball extracts to FFmpeg-n$FFVER, release tarball to ffmpeg-$FFVER
	if [ -d "$ROOT/thirdparty/FFmpeg-n$FFVER" ]; then
		mv "$ROOT/thirdparty/FFmpeg-n$FFVER" "$FFDIR"
	elif [ -d "$ROOT/thirdparty/ffmpeg-$FFVER" ]; then
		mv "$ROOT/thirdparty/ffmpeg-$FFVER" "$FFDIR"
	fi
fi

if [ ! -f "$FFDIR/configure" ]; then
	echo "FFmpeg source extraction failed: $FFDIR/configure missing" >&2
	exit 1
fi

mkdir -p "$PREFIX"

# Mirror the engine's xcompile.py toolchain flags so clang can find libgcc/crt
CROSS_CFLAGS="--sysroot=$SYSROOT -I$ANDROID_NDK_HOME/sources/android/support/include -DANDROID -D__ANDROID__ -fPIC"
CROSS_LDFLAGS="--sysroot=$SYSROOT --gcc-toolchain=$gcc_toolchain -fuse-ld=lld -Wl,--hash-style=both -Wl,--no-undefined -no-canonical-prefixes"

if [ "$ffarch" = "arm" ]; then
	CROSS_CFLAGS="$CROSS_CFLAGS -march=armv7-a -mfpu=neon-vfpv4 -mcpu=cortex-a7"
	CROSS_LDFLAGS="$CROSS_LDFLAGS -lgcc -march=armv7-a"
	if [ "$hardfp" = "1" ]; then
		CROSS_CFLAGS="$CROSS_CFLAGS -mfloat-abi=hard -D_NDK_MATH_NO_SOFTFP=1 -DLOAD_HARDFP -DSOFTFP_LINK -fno-builtin-strtod -fno-builtin-strtof -fno-builtin-strtold"
		CROSS_LDFLAGS="$CROSS_LDFLAGS -Wl,--no-warn-mismatch -lm_hard"
	fi
fi

cd "$FFDIR"

if [ -f Makefile ]; then
	make distclean >/dev/null 2>&1 || true
fi

./configure \
	--target-os=android \
	--arch=$ffarch \
	--enable-cross-compile \
	--cc="clang --target=$clang_target" \
	--sysroot="$SYSROOT" \
	--extra-cflags="$CROSS_CFLAGS" \
	--extra-ldflags="$CROSS_LDFLAGS" \
	--enable-static \
	--disable-shared \
	--disable-programs \
	--disable-doc \
	--disable-avdevice \
	--disable-avfilter \
	--disable-postproc \
	--disable-network \
	--disable-autodetect \
	--disable-asm \
	--disable-everything \
	--enable-avcodec \
	--enable-avformat \
	--enable-avutil \
	--enable-swresample \
	--enable-demuxer=bink \
	--enable-decoder=bink \
	--enable-decoder=binkaudio_dct \
	--enable-decoder=binkaudio_rdft \
	--enable-protocol=file \
	--libdir="$PREFIX" \
	--incdir="$PREFIX/include"

make -j$(nproc)
make install

echo "FFmpeg $FFVER built for $libcpu into $PREFIX"
