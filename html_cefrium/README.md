# Android Cefrium frame adapter

This is an experimental JNI frame transport and Java view wrapper for the
separately supplied Cefrium 0.17.1 SDK. It is not connected to the launcher,
engine HTML panel, or desktop HTML backend ABI. No Cefrium SDK binary is stored
in this source tree.

The Java owner must load `libhtml_cefrium.so` before constructing a view. The
constructor schedules Cefrium initialization and view creation on the Android
UI thread. After `Listener.onReady`, the native engine can copy the newest
frame using `hl2sb_cefrium_copy_rgba` and the handle supplied to the listener.
The copy contains premultiplied RGBA pixels; dimensions and sequence are
returned in `Hl2sbCefriumFrameInfo`. The returned size allows the caller to
allocate a buffer before copying. A concurrent resize can change that size;
callers must retry when the reported size exceeds capacity. `close()` retires
the handle and posts browser destruction to the UI thread. The containing app
owns the global Cefrium initialization and shutdown lifecycle.

Independent compile checks performed with SDK 0.17.1, JDK 25, Android 34
`android.jar`, and NDK r27b API 29:

```
mkdir -p "$HOME/.cache/hl2sb-cefrium-adapter/classes"
"$HOME/.cache/hl2sb-cefrium-jdk25/bin/javac" \
  -cp "$HOME/.local/android-build-toolchain/usr/lib/android-sdk/platforms/android-34/android.jar:/mnt/d/tmp/browser/cefrium-sdk/classes.jar" \
  -d "$HOME/.cache/hl2sb-cefrium-adapter/classes" \
  /mnt/d/project/source-engine/html_cefrium/CefriumView.java
"$HOME/ndk/ndk/27.1.12297006/toolchains/llvm/prebuilt/linux-aarch64/bin/aarch64-linux-android29-clang++" \
  -std=c++17 -fexceptions -fPIC -shared -pthread \
  /mnt/d/project/source-engine/html_cefrium/cefrium_frame.cpp \
  -o "$HOME/.cache/hl2sb-cefrium-adapter/libhtml_cefrium.so"
```

The standalone NDK command above uses `libc++_shared.so`; the Waf build may
select a different C++ runtime according to the engine configuration. Javac reports missing AndroidX
annotation metadata when the SDK's transitive dependencies are not on the
classpath. The `wscript` target builds only the native frame module; the caller
must compile and package the Java class and the complete matching Cefrium AAR
and dependencies separately. APK requirements must be checked against the
chosen SDK release and launcher before deployment.

The native adapter is also an optional Android arm64 Waf target. With the
Android NDK environment configured, enable it during the engine configure
step and build the target:

```
python3 waf configure --android=aarch64,host,21 --togles --build-games=hl2sb --enable-cefrium
python3 waf build --targets=html_cefrium -j12
```

The target produces `html_cefrium/libhtml_cefrium.so` under the Waf build
directory. It does not include or package the Cefrium SDK.

The file `patches/cefrium-0.17.1-asset-scheme.patch` is an unbuilt draft
against the official Cefrium 0.17.1 source. It registers the `asset` scheme in
the browser and renderer processes. It does not provide a resource handler or
connect the game filesystem; both the patched runtime and its scheme factory
still need to be built and tested together. The released, unmodified Cefrium
binary does not provide this registration.

This adapter does not implement `asset://` resource delivery, the GMod
JavaScript/Lua bridge, navigation policy, load events, popup compositing, or the desktop backend ABI.
It must not be selected as a working game HTML backend. Compilation and a
native frame concurrency test do not establish that browser initialization,
input, IME, frame colors, close behavior, or subprocess packaging work on a
device. Those require integration and physical-device testing.
