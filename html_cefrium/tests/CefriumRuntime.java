package org.hl2sb.cefrium;

public final class CefriumRuntime {
    private static native boolean nativeAttach();
    private static native void nativeReady(long id, long frame);
    private static native void nativeError(long id, String message);
    private static native long probeCreate();
    private static native long probeFrame(long id);
    private static native boolean probeNavigate(long id, String url);
    private static native boolean probeKey(long id);
    private static native String probeError(long id);
    private static native void probeDestroy(long id);

    private static String lastUrl;
    private static int lastKey;
    private static int destroyed;

    public static void create(long id, int width, int height) {
        if (width != 320 || height != 200) throw new AssertionError("size");
        nativeReady(id, 42);
    }
    public static void destroy(long id) { destroyed++; }
    public static void resize(long id, int width, int height) {}
    public static void navigate(long id, String url) { lastUrl = url; }
    public static void eval(long id, String script) {}
    public static void history(long id, int action) {}
    public static void mouse(long id, int type, int x, int y, int button, int modifiers, int dx, int dy) {}
    public static void key(long id, int type, int nativeKey, int virtualKey, int modifiers, int character) {
        lastKey = character;
        nativeError(id, "failure");
    }
    public static void ime(long id, int stage, String text) {}

    public static void main(String[] args) {
        System.load(args[0]);
        if (!nativeAttach()) throw new AssertionError("attach");
        long id = probeCreate();
        if (id == 0 || probeFrame(id) != 42) throw new AssertionError("ready");
        if (!probeNavigate(id, "asset://example/中文/\uD83D\uDE42") ||
            !"asset://example/中文/\uD83D\uDE42".equals(lastUrl)) throw new AssertionError("UTF-8");
        if (!probeKey(id) || lastKey != 97) throw new AssertionError("key");
        if (!"failure".equals(probeError(id)) || !"".equals(probeError(id)))
            throw new AssertionError("error queue");
        probeDestroy(id);
        if (destroyed != 1 || probeFrame(id) != 0 || probeNavigate(id, "about:blank"))
            throw new AssertionError("destroy");
        System.out.println("Cefrium JNI runtime contract PASS");
    }
}
