#!/bin/sh

git submodule init && git submodule update

brew untap aws/tap || true

brew install sdl2 pkg-config ffmpeg

./waf configure -T release --disable-warns $* &&
./waf build
