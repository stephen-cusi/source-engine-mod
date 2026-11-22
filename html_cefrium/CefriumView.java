package org.hl2sb.cefrium;

import android.app.Activity;
import android.os.Handler;
import android.os.Looper;

import com.cefrium.Cefrium;
import com.cefrium.CefriumBrowser;
import com.cefrium.CefriumRenderHandler;

import java.nio.ByteBuffer;

/* Initial offscreen adapter. The caller must load libhtml_cefrium.so first. */
public final class CefriumView implements AutoCloseable {
    public interface Listener {
        void onReady(long frameHandle);
        void onError(String error);
    }

    private static native long nativeCreateFrame();
    private static native void nativeDestroyFrame(long handle);
    private static native boolean nativeSubmitFrame(long handle, int width, int height, ByteBuffer pixels);

    private final Handler main = new Handler(Looper.getMainLooper());
    private final Activity activity;
    private final Listener listener;
    private CefriumBrowser browser;
    private volatile long handle;
    private volatile boolean closed;

    public CefriumView(Activity activity, Listener listener, int width, int height) {
        if (activity == null || listener == null || width <= 0 || height <= 0 || width > 8192 || height > 8192)
            throw new IllegalArgumentException();
        this.activity = activity;
        this.listener = listener;
        handle = nativeCreateFrame();
        if (handle == 0) throw new IllegalStateException("Cannot allocate frame state");
        main.post(() -> initialize(width, height));
    }

    private synchronized void initialize(int width, int height) {
        if (closed) return;
        try {
            if (!Cefrium.isInitialized() && !Cefrium.initialize(activity.getApplicationContext())) {
                close();
                listener.onError("Cefrium.initialize returned false");
                return;
            }
            browser = new CefriumBrowser(width, height);
            browser.setRenderHandler(new CefriumRenderHandler() {
                @Override public void onPaint(int w, int h, ByteBuffer pixels) {
                    long current = handle;
                    if (current != 0 && pixels != null)
                        nativeSubmitFrame(current, w, h, pixels.slice());
                }
            });
            listener.onReady(handle);
        } catch (RuntimeException | LinkageError error) {
            close();
            listener.onError(error.toString());
        }
    }

    private void post(java.util.function.Consumer<CefriumBrowser> operation) {
        main.post(() -> { if (!closed && browser != null) operation.accept(browser); });
    }

    public long frameHandle() { return handle; }
    public void resize(int width, int height) {
        if (width <= 0 || height <= 0 || width > 8192 || height > 8192) throw new IllegalArgumentException();
        post(b -> b.setSize(width, height));
    }
    public void loadUrl(String url) { if (url == null) throw new IllegalArgumentException(); post(b -> b.loadUrl(url)); }
    public void loadHtml(String html) { if (html == null) throw new IllegalArgumentException(); post(b -> b.loadHtml(html)); }
    public void evaluate(String script) { if (script == null) throw new IllegalArgumentException(); post(b -> b.evaluateJavaScript(script)); }
    public void goBack() { post(CefriumBrowser::goBack); }
    public void goForward() { post(CefriumBrowser::goForward); }
    public void reload() { post(CefriumBrowser::reload); }
    public void stop() { post(CefriumBrowser::stopLoading); }
    public void mouseMove(int x, int y) { post(b -> b.sendMouseMove(x, y)); }
    public void mouseDown(int x, int y, int button, int modifiers) { post(b -> b.sendMouseDown(x, y, button, modifiers)); }
    public void mouseUp(int x, int y, int button, int modifiers) { post(b -> b.sendMouseUp(x, y, button, modifiers)); }
    public void mouseWheel(int x, int y, int dx, int dy) { post(b -> b.sendMouseWheel(x, y, dx, dy)); }
    public void touchDown(float x, float y) { post(b -> b.sendTouchDown(x, y)); }
    public void touchMove(float x, float y) { post(b -> b.sendTouchMove(x, y)); }
    public void touchUp(float x, float y) { post(b -> b.sendTouchUp(x, y)); }
    public void key(int type, int nativeCode, int windowsCode, int modifiers, char character) {
        post(b -> b.sendKeyEvent(type, nativeCode, windowsCode, modifiers, character));
    }
    public void imeCommit(String text) { if (text == null) throw new IllegalArgumentException(); post(b -> b.imeCommitText(text, 0)); }
    public void imeComposition(String text, int selectionStart, int selectionEnd) {
        if (text == null) throw new IllegalArgumentException();
        post(b -> b.imeSetComposition(text, selectionStart, selectionEnd));
    }
    public void imeCancel() { post(CefriumBrowser::imeCancelComposition); }
    public void onPause() { post(CefriumBrowser::onPause); }
    public void onResume() { post(CefriumBrowser::onResume); }

    @Override public synchronized void close() {
        if (closed) return;
        closed = true;
        long old = handle;
        handle = 0;
        if (old != 0) nativeDestroyFrame(old);
        main.post(() -> { if (browser != null) { browser.close(); browser = null; } });
    }
}
