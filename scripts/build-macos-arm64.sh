#!/bin/sh
set -eu

# Build for macOS arm64 (Apple Silicon).
#
# waf detects the target CPU from the Python interpreter that runs it. On a
# native arm64 runner this must be the arm64 Python, otherwise configure
# mis-detects the CPU as x86_64 and the build produces the wrong architecture
# (see nillerusr/source-engine#359). GitHub's `macos-latest` runner is arm64.

MACHINE=$(uname -m)
if [ "$MACHINE" != "arm64" ]; then
    echo "Expected arm64 host, got: $MACHINE" >&2
    exit 1
fi

PYMACHINE=$(python3 -c 'import platform; print(platform.machine())')
if [ "$PYMACHINE" != "arm64" ]; then
    echo "Python reports $PYMACHINE (likely running under Rosetta); arm64 build requires native Python" >&2
    echo "Try: arch -arm64 /opt/homebrew/bin/python3 $(pwd)/waf ..." >&2
    exit 1
fi

git submodule init && git submodule update

brew untap aws/tap || true

brew install sdl2 pkg-config

./waf configure -T release --disable-warns $* &&
./waf build
