#include "servo_backend.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <limits>
#include <new>
#include <string>
#include <thread>
#include <vector>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace {
std::atomic<bool> s_runtimeExists(false);
struct Servo;
struct ServoBuilder;
struct ServoOptions;
struct ServoPreferences;
struct RenderingContext;
struct WebViewBuilder;
struct WebView;
struct Delegate {
    void *user;
    void (*load)(WebView *, int32_t, void *);
    void (*frame)(WebView *, void *);
    bool (*bridge)(uint64_t, const char *, void *);
    bool (*asset)(uint64_t, const char *, void *);
};
typedef void (*ScreenshotCallback)(const uint8_t *, uint32_t, uint32_t, int32_t, void *);

struct Api {
    ServoBuilder *(*builder_create)();
    Servo *(*builder_build)(ServoBuilder *);
    void (*builder_free)(ServoBuilder *);
    void (*builder_set_options)(ServoBuilder *, ServoOptions *);
    ServoPreferences *(*preferences_create)();
    void (*preferences_set_string)(ServoPreferences *, const char *, const char *);
    void (*preferences_set_f64_array)(ServoPreferences *, const char *, const double *, size_t);
    void (*builder_set_preferences)(ServoBuilder *, ServoPreferences *);
    ServoOptions *(*options_create)();
    void (*options_set_multiprocess)(ServoOptions *, bool);
    void (*servo_free)(Servo *);
    void (*spin)(Servo *);
    RenderingContext *(*context_create)(uint32_t, uint32_t);
    void (*context_free)(RenderingContext *);
    WebViewBuilder *(*view_builder_create)(Servo *, RenderingContext *);
    void (*view_builder_delegate)(WebViewBuilder *, Delegate);
    WebView *(*view_builder_build)(WebViewBuilder *);
    void (*view_builder_free)(WebViewBuilder *);
    void (*view_free)(WebView *);
    void (*view_paint)(WebView *);
    void (*view_resize)(WebView *, uint32_t, uint32_t);
    int32_t (*view_load)(WebView *, const char *);
    void (*view_screenshot)(WebView *, ScreenshotCallback, void *);
    int32_t (*eval_js)(WebView *, const char *);
    int32_t (*set_document_script)(WebView *, const char *);
    bool (*bridge_reply)(uint64_t, const char *);
    void (*bridge_cancel)(void *);
    bool (*asset_reply)(uint64_t, uint16_t, const char *, const uint8_t *, size_t);
    void (*asset_cancel)(void *);
    int32_t (*mouse_move)(WebView *, float, float);
    int32_t (*mouse_button)(WebView *, float, float, int32_t, bool);
    int32_t (*mouse_wheel)(WebView *, float, float, double, double);
    int32_t (*key)(WebView *, const char *, bool);
    int32_t (*text)(WebView *, const char *);
    int32_t (*composition)(WebView *, int32_t, const char *);
    int32_t (*reload)(WebView *);
    int32_t (*back)(WebView *);
    int32_t (*forward)(WebView *);
};

class Library {
public:
    explicit Library(const char *path) : handle_(NULL) {
#if defined(_WIN32)
        handle_ = LoadLibraryA(path);
#else
        handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    }
    ~Library() {
        if (!handle_) return;
#if defined(_WIN32)
        FreeLibrary(static_cast<HMODULE>(handle_));
#else
        dlclose(handle_);
#endif
    }
    bool Loaded() const { return handle_ != NULL; }
    template <class T> bool Symbol(T &out, const char *name) {
#if defined(_WIN32)
        FARPROC symbol = handle_ ? GetProcAddress(static_cast<HMODULE>(handle_), name) : NULL;
#else
        void *symbol = handle_ ? dlsym(handle_, name) : NULL;
#endif
        if (!symbol || sizeof(symbol) != sizeof(out)) return false;
        std::memcpy(&out, &symbol, sizeof(out));
        return true;
    }
private:
    void *handle_;
    Library(const Library &);
    Library &operator=(const Library &);
};

bool LoadApi(Library &lib, Api &api) {
#define LOAD(field, name) if (!lib.Symbol(api.field, name)) return false
    LOAD(builder_create, "servo_builder_create");
    LOAD(builder_build, "servo_builder_build");
    LOAD(builder_free, "servo_builder_free");
    LOAD(builder_set_options, "servo_builder_set_options");
    LOAD(options_create, "servo_options_create");
    LOAD(options_set_multiprocess, "servo_options_set_multiprocess");
    LOAD(preferences_create, "servo_preferences_create");
    LOAD(preferences_set_string, "servo_preferences_set_string");
    LOAD(preferences_set_f64_array, "servo_preferences_set_f64_array");
    LOAD(builder_set_preferences, "servo_builder_set_preferences");
    LOAD(servo_free, "servo_free");
    LOAD(spin, "servo_spin_event_loop");
    LOAD(context_create, "servo_rendering_context_create_software");
    LOAD(context_free, "servo_rendering_context_free");
    LOAD(view_builder_create, "servo_webview_builder_create");
    LOAD(view_builder_delegate, "servo_webview_builder_set_delegate");
    LOAD(view_builder_build, "servo_webview_builder_build");
    LOAD(view_builder_free, "servo_webview_builder_free");
    LOAD(view_free, "servo_webview_free");
    LOAD(view_paint, "servo_webview_paint");
    LOAD(view_resize, "servo_webview_resize");
    LOAD(view_load, "servo_webview_load");
    LOAD(view_screenshot, "servo_webview_take_screenshot");
    LOAD(bridge_reply, "servo_bridge_reply");
    LOAD(bridge_cancel, "servo_bridge_cancel");
    LOAD(asset_reply, "servo_asset_reply");
    LOAD(asset_cancel, "servo_asset_cancel");
    LOAD(set_document_script, "servo_webview_set_document_script");
#undef LOAD
    lib.Symbol(api.eval_js, "servo_webview_eval_js");
    lib.Symbol(api.mouse_move, "servo_webview_mouse_move");
    lib.Symbol(api.mouse_button, "servo_webview_mouse_button");
    lib.Symbol(api.mouse_wheel, "servo_webview_mouse_wheel");
    lib.Symbol(api.key, "servo_webview_key");
    lib.Symbol(api.text, "servo_webview_text");
    lib.Symbol(api.composition, "servo_webview_composition");
    lib.Symbol(api.reload, "servo_webview_reload");
    lib.Symbol(api.back, "servo_webview_go_back");
    lib.Symbol(api.forward, "servo_webview_go_forward");
    return true;
}

// Servo's C API rejects non-UTF-8 URLs by panicking across the FFI boundary.
bool ValidUTF8(const char *text, size_t limit = 16384) {
    if (!text) return false;
    const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
    size_t bytes = 0;
    while (*p) {
        if (++bytes > limit) return false;
        unsigned c = *p++;
        if (c < 0x80) continue;
        unsigned value, minimum, count;
        if (c >= 0xc2 && c <= 0xdf) { value = c & 0x1f; minimum = 0x80; count = 1; }
        else if (c >= 0xe0 && c <= 0xef) { value = c & 0x0f; minimum = 0x800; count = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { value = c & 7; minimum = 0x10000; count = 3; }
        else return false;
        while (count--) {
            if (!*p || (*p & 0xc0) != 0x80 || ++bytes > limit) return false;
            value = (value << 6) | (*p++ & 0x3f);
        }
        if (value < minimum || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) return false;
    }
    return true;
}
bool ValidSize(uint32_t width, uint32_t height) {
    return width > 0 && height > 0 && width <= 8192 && height <= 8192;
}
#if defined(_WIN32)
std::string SiblingLibrary(const char *path, const char *name) {
    std::string result(path);
    const size_t slash = result.find_last_of("/\\");
    return slash == std::string::npos ? std::string(name) : result.substr(0, slash + 1) + name;
}
#endif
}

struct HLSBServoRuntime {
    explicit HLSBServoRuntime(const char *path) :
#if defined(_WIN32)
        gles(SiblingLibrary(path, "libGLESv2.dll").c_str()),
        egl(SiblingLibrary(path, "libEGL.dll").c_str()),
#endif
        lib(path), api(), servo(NULL), owner(std::this_thread::get_id()), pumping(false) {}
#if defined(_WIN32)
    Library gles;
    Library egl;
#endif
    Library lib;
    Api api;
    Servo *servo;
    std::thread::id owner;
    bool pumping;
    std::vector<HLSBServoView *> views;
    std::vector<struct CallbackState *> callbacks;
};

struct CallbackState { HLSBServoView *view; };

struct HLSBServoView {
    HLSBServoRuntime *runtime;
    WebView *view;
    uint32_t width, height, frameWidth, frameHeight;
    uint64_t generation, captureGeneration, sequence;
    bool dirty, capturing;
    bool initialized;
    std::string pendingURL;
    int loadStatus;
    std::vector<int> loadEvents;
    std::vector<std::pair<uint64_t, std::string> > bridgeEvents;
    std::vector<std::pair<uint64_t, std::string> > assetEvents;
    CallbackState *callback;
    std::vector<uint8_t> pixels;
};

namespace {
bool Owner(const HLSBServoRuntime *r) {
    return r && r->owner == std::this_thread::get_id();
}
bool Available(const HLSBServoRuntime *r) { return Owner(r) && !r->pumping; }
void LoadCallback(WebView *, int32_t status, void *user) {
    HLSBServoView *v = static_cast<CallbackState *>(user)->view;
    if (v) {
        if (!v->initialized) {
            if (status == 2 || status == 3) v->initialized = true;
            return;
        }
        v->loadStatus = status;
        if (status == 2) v->dirty = true;
        try { v->loadEvents.push_back(status); } catch (...) {}
    }
}
void FrameCallback(WebView *, void *user) {
    HLSBServoView *v = static_cast<CallbackState *>(user)->view;
    if (v) v->dirty = true;
}
bool BridgeCallback(uint64_t id, const char *message, void *user) {
    HLSBServoView *v = static_cast<CallbackState *>(user)->view;
    if (!v || !message || std::strlen(message) > 65535) return false;
    try { v->bridgeEvents.push_back(std::make_pair(id, std::string(message))); return true; } catch (...) { return false; }
}
bool AssetCallback(uint64_t id, const char *url, void *user) {
    HLSBServoView *v = static_cast<CallbackState *>(user)->view;
    if (!v || v->assetEvents.size() >= 256 || !ValidUTF8(url, 8191)) return false;
    try { v->assetEvents.push_back(std::make_pair(id, std::string(url))); return true; } catch (...) { return false; }
}
void Screenshot(const uint8_t *data, uint32_t width, uint32_t height, int32_t, void *user) {
    HLSBServoView *v = static_cast<CallbackState *>(user)->view;
    if (!v) return;
    v->capturing = false;
    if (!data || v->captureGeneration != v->generation || width != v->width || height != v->height ||
        !ValidSize(width, height) || size_t(width) > (std::numeric_limits<size_t>::max)() / 4 / height) {
        v->dirty = true;
        return;
    }
    try {
        v->pixels.assign(data, data + size_t(width) * height * 4);
        for (size_t i = 0; i < v->pixels.size(); i += 4) {
            const unsigned alpha = v->pixels[i + 3];
            if (!alpha) {
                v->pixels[i] = v->pixels[i + 1] = v->pixels[i + 2] = 0;
            } else if (alpha < 255) {
                for (size_t channel = 0; channel < 3; ++channel) {
                    const unsigned value = (unsigned(v->pixels[i + channel]) * 255u + alpha / 2u) / alpha;
                    v->pixels[i + channel] = static_cast<uint8_t>((std::min)(value, 255u));
                }
            }
        }
        v->frameWidth = width;
        v->frameHeight = height;
        ++v->sequence;
    } catch (...) {
        // Never unwind through the Servo C callback.
    }
}
}

HLSBServoRuntime *HLSBServoCreateRuntime(const char *path) {
    if (!path || !*path) return NULL;
    bool expected = false;
    if (!s_runtimeExists.compare_exchange_strong(expected, true)) return NULL;
    HLSBServoRuntime *r = new (std::nothrow) HLSBServoRuntime(path);
    if (!r) { s_runtimeExists = false; return NULL; }
#if defined(_WIN32)
    if (!r->egl.Loaded() || !r->gles.Loaded()) { delete r; s_runtimeExists = false; return NULL; }
#endif
    if (!LoadApi(r->lib, r->api)) { delete r; s_runtimeExists = false; return NULL; }
    ServoBuilder *builder = r->api.builder_create();
    if (!builder) { delete r; s_runtimeExists = false; return NULL; }
    ServoOptions *options = r->api.options_create();
    if (!options) { r->api.builder_free(builder); delete r; s_runtimeExists = false; return NULL; }
    r->api.options_set_multiprocess(options, false);
    r->api.builder_set_options(builder, options);
    ServoPreferences *preferences = r->api.preferences_create();
    if (!preferences) { r->api.builder_free(builder); delete r; s_runtimeExists = false; return NULL; }
    const double transparent[4] = {0.0, 0.0, 0.0, 0.0};
    r->api.preferences_set_f64_array(preferences, "shell_background_color_rgba", transparent, 4);
#if defined(_WIN32)
    r->api.preferences_set_string(preferences, "user_agent",
        "Mozilla/5.0 (Windows NT; Valve Source Client) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/86.0.4240.198 Safari/537.36 GMod/13");
#endif
    r->api.builder_set_preferences(builder, preferences);
    r->servo = r->api.builder_build(builder);
    if (!r->servo) { delete r; s_runtimeExists = false; return NULL; }
    return r;
}

bool HLSBServoDestroyRuntime(HLSBServoRuntime *r) {
    if (!Available(r)) return false;
    while (!r->views.empty()) HLSBServoDestroyView(r->views.back());
    r->api.servo_free(r->servo);
    for (size_t i = 0; i < r->callbacks.size(); ++i) delete r->callbacks[i];
    delete r;
    s_runtimeExists = false;
    return true;
}

HLSBServoView *HLSBServoCreateView(HLSBServoRuntime *r, uint32_t width, uint32_t height) {
    if (!Available(r) || !ValidSize(width, height)) return NULL;
    HLSBServoView *v = new (std::nothrow) HLSBServoView();
    if (!v) return NULL;
    v->runtime = r; v->view = NULL; v->width = width; v->height = height;
    v->frameWidth = v->frameHeight = 0; v->generation = v->captureGeneration = v->sequence = 0;
    v->dirty = v->capturing = v->initialized = false;
    v->loadStatus = -1;
    v->callback = new (std::nothrow) CallbackState{v};
    if (!v->callback) { delete v; return NULL; }
    try { r->callbacks.push_back(v->callback); } catch (...) { delete v->callback; delete v; return NULL; }
    RenderingContext *context = r->api.context_create(width, height);
    if (!context) { v->callback->view = NULL; delete v; return NULL; }
    WebViewBuilder *builder = r->api.view_builder_create(r->servo, context);
    if (!builder) { r->api.context_free(context); v->callback->view = NULL; delete v; return NULL; }
    Delegate delegate = {v->callback, LoadCallback, FrameCallback, BridgeCallback, AssetCallback};
    r->api.view_builder_delegate(builder, delegate);
    v->view = r->api.view_builder_build(builder);
    if (!v->view) { v->callback->view = NULL; delete v; return NULL; }
    try { r->views.push_back(v); } catch (...) { v->callback->view = NULL; r->api.view_free(v->view); delete v; return NULL; }
    return v;
}

bool HLSBServoDestroyView(HLSBServoView *v) {
    if (!v || !Available(v->runtime)) return false;
    HLSBServoRuntime *r = v->runtime;
    r->api.bridge_cancel(v->callback);
    r->api.asset_cancel(v->callback);
    v->callback->view = NULL;
    r->api.view_free(v->view);
    r->views.erase(std::remove(r->views.begin(), r->views.end(), v), r->views.end());
    delete v;
    return true;
}

bool HLSBServoPump(HLSBServoRuntime *r) {
    if (!Available(r)) return false;
    r->pumping = true;
    r->api.spin(r->servo);
    for (size_t i = 0; i < r->views.size(); ++i) {
        HLSBServoView *v = r->views[i];
        if (v->initialized && !v->pendingURL.empty()) {
            std::string url;
            url.swap(v->pendingURL);
            if (r->api.view_load(v->view, url.c_str()) != 0) v->loadStatus = 3;
        }
        if (v->dirty) {
            v->dirty = false;
            r->api.view_paint(v->view);
            if (!v->capturing) {
                v->capturing = true;
                v->captureGeneration = v->generation;
                r->api.view_screenshot(v->view, Screenshot, v->callback);
            }
        }
    }
    r->pumping = false;
    return true;
}

bool HLSBServoNavigate(HLSBServoView *v, const char *url) {
    if (!v || !Available(v->runtime) || !ValidUTF8(url, 8u * 1024u * 1024u)) return false;
    v->runtime->api.bridge_cancel(v->callback);
    v->runtime->api.asset_cancel(v->callback);
    v->bridgeEvents.clear();
    v->assetEvents.clear();
    if (!v->initialized) {
        try { v->pendingURL = url; } catch (...) { return false; }
    } else if (v->runtime->api.view_load(v->view, url) != 0) return false;
    ++v->generation;
    v->loadStatus = 0;
    v->loadEvents.clear();
    v->pixels.clear(); v->frameWidth = v->frameHeight = 0;
    return true;
}

bool HLSBServoResize(HLSBServoView *v, uint32_t width, uint32_t height) {
    if (!v || !Available(v->runtime) || !ValidSize(width, height)) return false;
    if (width == v->width && height == v->height) return true;
    v->runtime->api.view_resize(v->view, width, height);
    v->width = width; v->height = height; ++v->generation;
    v->pixels.clear(); v->frameWidth = v->frameHeight = 0;
    return true;
}

int HLSBServoLoadStatus(HLSBServoView *v) {
    return v && Available(v->runtime) ? v->loadStatus : -1;
}
bool HLSBServoPopLoadEvent(HLSBServoView *v, int *status) {
    if (!v || !status || !Available(v->runtime) || v->loadEvents.empty()) return false;
    *status = v->loadEvents.front();
    v->loadEvents.erase(v->loadEvents.begin());
    return true;
}

bool HLSBServoPopBridgeEvent(HLSBServoView *v, uint64_t *id, char *message, size_t capacity) {
    if (!v || !id || !message || !Available(v->runtime) || v->bridgeEvents.empty()) return false;
    const std::pair<uint64_t, std::string> &event = v->bridgeEvents.front();
    if (event.second.size() >= capacity) return false;
    *id = event.first;
    std::memcpy(message, event.second.c_str(), event.second.size() + 1);
    v->bridgeEvents.erase(v->bridgeEvents.begin());
    return true;
}

bool HLSBServoBridgeReply(HLSBServoView *v, uint64_t id, const char *reply) {
    return v && Available(v->runtime) && ValidUTF8(reply, 65536) && v->runtime->api.bridge_reply(id, reply);
}

bool HLSBServoPopAssetEvent(HLSBServoView *v, uint64_t *id, char *url, size_t capacity) {
    if (!v || !id || !url || !Available(v->runtime) || v->assetEvents.empty()) return false;
    const std::pair<uint64_t, std::string> &event = v->assetEvents.front();
    if (event.second.size() >= capacity) return false;
    *id = event.first;
    std::memcpy(url, event.second.c_str(), event.second.size() + 1);
    v->assetEvents.erase(v->assetEvents.begin());
    return true;
}

bool HLSBServoAssetReply(HLSBServoView *v, uint64_t id, uint16_t status,
                         const char *mime, const uint8_t *bytes, size_t length) {
    return v && Available(v->runtime) && (status == 200 || status == 404) &&
        ValidUTF8(mime, 128) && length <= 32u * 1024u * 1024u && (bytes || !length) &&
        v->runtime->api.asset_reply(id, status, mime, bytes, length);
}

bool HLSBServoEvalJS(HLSBServoView *v, const char *script) {
    return v && Available(v->runtime) && ValidUTF8(script, 8u * 1024u * 1024u) && v->runtime->api.eval_js && v->runtime->api.eval_js(v->view, script) == 0;
}
bool HLSBServoSetDocumentScript(HLSBServoView *v, const char *script) {
    return v && Available(v->runtime) && ValidUTF8(script, 8u * 1024u * 1024u) &&
        v->runtime->api.set_document_script(v->view, script) == 0;
}
bool HLSBServoMouseMove(HLSBServoView *v, float x, float y) {
    return v && Available(v->runtime) && v->runtime->api.mouse_move && v->runtime->api.mouse_move(v->view, x, y) == 0;
}
bool HLSBServoMouseButton(HLSBServoView *v, float x, float y, int button, bool down) {
    return v && Available(v->runtime) && v->runtime->api.mouse_button && v->runtime->api.mouse_button(v->view, x, y, button, down) == 0;
}
bool HLSBServoMouseWheel(HLSBServoView *v, float x, float y, double dx, double dy) {
    return v && Available(v->runtime) && v->runtime->api.mouse_wheel && v->runtime->api.mouse_wheel(v->view, x, y, dx, dy) == 0;
}
bool HLSBServoKey(HLSBServoView *v, const char *key, bool down) {
    return v && Available(v->runtime) && ValidUTF8(key) && v->runtime->api.key && v->runtime->api.key(v->view, key, down) == 0;
}
bool HLSBServoText(HLSBServoView *v, const char *text) {
    return v && Available(v->runtime) && ValidUTF8(text) && v->runtime->api.text && v->runtime->api.text(v->view, text) == 0;
}
bool HLSBServoComposition(HLSBServoView *v, int stage, const char *text) {
    return v && Available(v->runtime) && stage >= 0 && stage <= 2 && ValidUTF8(text) &&
        v->runtime->api.composition && v->runtime->api.composition(v->view, stage, text) == 0;
}
bool HLSBServoReload(HLSBServoView *v) {
    return v && Available(v->runtime) && v->runtime->api.reload && v->runtime->api.reload(v->view) == 0;
}
bool HLSBServoBack(HLSBServoView *v) {
    return v && Available(v->runtime) && v->runtime->api.back && v->runtime->api.back(v->view) == 0;
}
bool HLSBServoForward(HLSBServoView *v) {
    return v && Available(v->runtime) && v->runtime->api.forward && v->runtime->api.forward(v->view) == 0;
}

size_t HLSBServoCopyFrame(HLSBServoView *v, void *buffer, size_t capacity,
                         uint32_t *width, uint32_t *height, uint64_t *sequence) {
    if (!v || !Available(v->runtime)) return 0;
    if (width) *width = v->frameWidth;
    if (height) *height = v->frameHeight;
    if (sequence) *sequence = v->sequence;
    const size_t size = v->pixels.size();
    if (buffer && capacity >= size && size) std::memcpy(buffer, v->pixels.data(), size);
    return size;
}
