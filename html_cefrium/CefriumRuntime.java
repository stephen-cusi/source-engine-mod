package org.hl2sb.cefrium;

import android.app.Activity;
import android.app.Application;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;

import java.lang.ref.WeakReference;
import java.util.HashMap;
import java.util.Map;
import java.util.function.Consumer;

import com.cefrium.CefriumBrowser;

/** Android UI-thread owner of views requested by the native game thread. */
public final class CefriumRuntime {
    private static final String TAG = "HL2SBCefrium";
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    // Accessed only on the Android main thread.
    private static final Map<Long, Entry> VIEWS = new HashMap<>();
    private static WeakReference<Activity> active = new WeakReference<>(null);
    private static boolean installed;
    private static boolean attached;

    private static native boolean nativeAttach();
    private static native void nativeReady(long id, long frameHandle);
    private static native void nativeError(long id, String message);

    private static final class Entry {
        CefriumView view;
    }

    private CefriumRuntime() {}

    /** Called once from the launcher provider; does not initialize a browser. */
    public static void install(Application application) {
        if (installed) return;
        installed = true;
        application.registerActivityLifecycleCallbacks(new Application.ActivityLifecycleCallbacks() {
            @Override public void onActivityPreCreated(Activity activity, Bundle state) {
                if (!isGameActivity(activity)) return;
                active = new WeakReference<>(activity);
                if (!attached) {
                    try {
                        System.loadLibrary("html_cefrium");
                        attached = nativeAttach();
                        if (!attached) Log.e(TAG, "Native bridge attachment failed");
                    } catch (LinkageError | RuntimeException error) {
                        Log.e(TAG, "Native bridge unavailable", error);
                    }
                }
            }
            @Override public void onActivityCreated(Activity activity, Bundle state) {}
            @Override public void onActivityStarted(Activity activity) {}
            @Override public void onActivityResumed(Activity activity) {
                if (active.get() == activity) for (Entry entry : VIEWS.values()) entry.view.onResume();
            }
            @Override public void onActivityPaused(Activity activity) {
                if (active.get() == activity) for (Entry entry : VIEWS.values()) entry.view.onPause();
            }
            @Override public void onActivityStopped(Activity activity) {}
            @Override public void onActivitySaveInstanceState(Activity activity, Bundle state) {}
            @Override public void onActivityDestroyed(Activity activity) {
                if (active.get() != activity) return;
                active.clear();
                closeAll("Game activity destroyed");
            }
        });
    }

    private static boolean isGameActivity(Activity activity) {
        Class<?> type = activity.getClass();
        while (type != null) {
            if ("org.libsdl.app.SDLActivity".equals(type.getName())) return true;
            type = type.getSuperclass();
        }
        return false;
    }

    private static void report(long id, String error) {
        if (attached) nativeError(id, error);
        else Log.e(TAG, "View " + id + ": " + error);
    }

    /** All following methods may be called from an attached native thread. */
    public static void create(long id, int width, int height) {
        MAIN.post(() -> {
            Activity activity = active.get();
            if (!attached || activity == null || activity.isFinishing()) {
                report(id, "Game activity or native bridge is unavailable");
                return;
            }
            if (id <= 0 || VIEWS.containsKey(id)) {
                report(id, "Invalid or duplicate view id");
                return;
            }
            Entry entry = new Entry();
            try {
                entry.view = new CefriumView(activity, new CefriumView.Listener() {
                    @Override public void onReady(long frameHandle) {
                        if (VIEWS.get(id) == entry && attached) nativeReady(id, frameHandle);
                    }
                    @Override public void onError(String error) {
                        if (VIEWS.get(id) != entry) return;
                        VIEWS.remove(id);
                        report(id, error);
                    }
                }, width, height);
                VIEWS.put(id, entry);
            } catch (RuntimeException | LinkageError error) {
                report(id, error.toString());
            }
        });
    }

    public static void destroy(long id) {
        MAIN.post(() -> {
            Entry entry = VIEWS.remove(id);
            if (entry != null) entry.view.close();
        });
    }

    private static void withView(long id, Consumer<CefriumView> action) {
        MAIN.post(() -> {
            Entry entry = VIEWS.get(id);
            if (entry == null) return;
            try { action.accept(entry.view); }
            catch (RuntimeException | LinkageError error) { report(id, error.toString()); }
        });
    }

    public static void resize(long id, int width, int height) { withView(id, v -> v.resize(width, height)); }
    public static void navigate(long id, String url) {
        if (url == null) { MAIN.post(() -> report(id, "Null URL")); return; }
        // The current SDK has no game VFS resource handler. Do not pretend
        // asset URLs work by forwarding them to the external protocol handler.
        if (url.regionMatches(true, 0, "asset:", 0, 6)) {
            MAIN.post(() -> report(id, "Game asset protocol is not implemented"));
            return;
        }
        withView(id, v -> v.loadUrl(url));
    }
    public static void eval(long id, String script) { withView(id, v -> v.evaluate(script)); }
    public static void history(long id, int action) {
        withView(id, v -> {
            switch (action) {
                case 0: v.goBack(); break;
                case 1: v.goForward(); break;
                case 2: v.reload(); break;
                case 3: v.stop(); break;
                default: throw new IllegalArgumentException("Invalid history action");
            }
        });
    }
    public static void mouse(long id, int type, int x, int y, int button, int modifiers, int dx, int dy) {
        withView(id, v -> {
            switch (type) {
                case 0: v.mouseMove(x, y); break;
                case 1: v.mouseDown(x, y, button, modifiers); break;
                case 2: v.mouseUp(x, y, button, modifiers); break;
                case 3: v.mouseWheel(x, y, dx, dy); break;
                default: throw new IllegalArgumentException("Invalid mouse event");
            }
        });
    }
    public static void key(long id, int type, int nativeKey, int virtualKey, int modifiers, int character) {
        withView(id, v -> {
            int cefType;
            switch (type) {
                case 0: cefType = CefriumBrowser.KEY_RAW_DOWN; break;
                case 1: cefType = CefriumBrowser.KEY_UP; break;
                case 2: cefType = CefriumBrowser.KEY_CHAR; break;
                default: throw new IllegalArgumentException("Invalid key event");
            }
            v.key(cefType, nativeKey, virtualKey, modifiers, (char) character);
        });
    }
    public static void ime(long id, int stage, String text) {
        withView(id, v -> {
            String value = text == null ? "" : text;
            switch (stage) {
                case 0: break;
                case 1: v.imeComposition(value, value.length(), value.length()); break;
                case 2: v.imeCommit(value); break;
                case 3: v.imeCancel(); break;
                default: throw new IllegalArgumentException("Invalid IME stage");
            }
        });
    }
    private static void closeAll(String reason) {
        for (Map.Entry<Long, Entry> entry : VIEWS.entrySet()) {
            entry.getValue().view.close();
            report(entry.getKey(), reason);
        }
        VIEWS.clear();
    }
    public static void shutdown() { MAIN.post(() -> closeAll("HTML backend shut down")); }
}
