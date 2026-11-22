# Cefrium offscreen API probe

This standalone Java source probes the separately supplied Android Cefrium SDK.
It is not wired into the game or launcher and does not implement the HTML ABI.
Create it from an Activity, call `start()`, and observe its listener. Each frame
is copied before the SDK callback returns. Cefrium 0.17.1's supplied
`CefriumRenderHandler.java` documents UI-thread callbacks and a direct,
premultiplied RGBA buffer of `width * height * 4` bytes, valid only during the
callback. Popup frames have a separate callback. The rendering output still
needs device verification before use as an engine texture. Call
`close()` when finished. The host owns Cefrium's global shutdown lifecycle.

Compile-time check with JDK 25, the extracted SDK 0.17.1 classes.jar, and an
Android platform jar. The locally supplied SDK classes have class-file major
version 69; JDK 17 and 21 cannot read them:

```
javac -cp <android.jar>:<cefrium-classes.jar> -d <output-dir> CefriumOffscreenProbe.java
```

This source compiled with JDK 25 and the Android 34 platform jar. With only
the SDK classes jar on the classpath, javac reported missing AndroidX
annotation metadata; the full AAR dependency set was not included in that
check.

The provided SDK source identifies itself as Cefrium 0.17.1 and Chromium
156.0.8078.80. It does not contain `cef_api_hash.h`; no desktop CEF API hash
compatibility is established by this probe.

Compiling this file does not verify SDK initialization, manifest merging,
renderer subprocess packaging, resource installation, thread behavior, input,
or game integration. A consuming application must package the matching AAR and
its dependencies. These need device verification before use in the launcher.
