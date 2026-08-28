#!/bin/sh
set -eu

# Build for macOS x86_64 (Intel). `macos-latest` runners are now arm64, so this
# must run on an Intel runner (e.g. macos-15-intel). waf derives the target CPU
# from the Python interpreter, so also guard against a Rosetta-mis-detected arch.

MACHINE=$(uname -m)
if [ "$MACHINE" != "x86_64" ]; then
    echo "Expected x86_64 host, got: $MACHINE" >&2
    exit 1
fi

git submodule init && git submodule update

brew untap aws/tap || true

brew install sdl2 pkg-config ffmpeg

./waf configure -T release --disable-warns $* &&
./waf build
