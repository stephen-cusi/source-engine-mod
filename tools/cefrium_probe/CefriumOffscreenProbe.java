package org.hl2sb.cefriumprobe;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;

import com.cefrium.Cefrium;
import com.cefrium.CefriumBrowser;
import com.cefrium.CefriumRenderHandler;

import java.nio.ByteBuffer;

/** An isolated offscreen API probe; this is not a game browser backend. */
public final class CefriumOffscreenProbe {
    public interface Listener {
        void onFrame(int width, int height, byte[] pixels);
        void onError(String message);
    }

    private final Activity activity;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    private CefriumBrowser browser;
    private boolean closed;

    public CefriumOffscreenProbe(Activity activity, Listener listener) {
        if (activity == null || listener == null) throw new IllegalArgumentException();
        this.activity = activity;
        this.listener = listener;
    }

    public void start() {
        main.post(() -> {
            if (closed || browser != null) return;
            try {
                if (!Cefrium.isInitialized() && !Cefrium.initialize(activity.getApplicationContext())) {
                    listener.onError("Cefrium.initialize returned false");
                    return;
                }
                browser = new CefriumBrowser(320, 200);
                browser.setRenderHandler(new CefriumRenderHandler() {
                    @Override public void onPaint(int width, int height, ByteBuffer pixels) {
                        if (width <= 0 || height <= 0 || pixels == null) return;
                        long required = (long) width * height * 4;
                        if (required > Integer.MAX_VALUE) return;
                        ByteBuffer copy = pixels.duplicate();
                        copy.rewind();
                        if (copy.remaining() < required) {
                            main.post(() -> listener.onError("Frame buffer is smaller than width * height * 4"));
                            return;
                        }
                        byte[] frame = new byte[(int) required];
                        copy.get(frame);
                        main.post(() -> { if (!closed) listener.onFrame(width, height, frame); });
                    }
                });
                browser.loadHtml("<!doctype html><meta charset='utf-8'><body style='background:rgb(17,34,51);color:white'>Cefrium offscreen test</body>");
            } catch (RuntimeException | LinkageError error) {
                listener.onError(error.toString());
            }
        });
    }

    public void close() {
        main.post(() -> {
            closed = true;
            if (browser != null) {
                browser.close();
                browser = null;
            }
        });
    }
}
