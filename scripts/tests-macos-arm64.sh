#!/bin/sh
set -eu

MACHINE=$(uname -m)
if [ "$MACHINE" != "arm64" ]; then
    echo "Expected arm64 host, got: $MACHINE" >&2
    exit 1
fi

git submodule init && git submodule update
./waf configure -T release --sanitize=address,undefined --disable-warns --tests --prefix=out/ $* &&
./waf install &&
cd out &&
DYLD_LIBRARY_PATH=bin/ ./unittest || exit 1
