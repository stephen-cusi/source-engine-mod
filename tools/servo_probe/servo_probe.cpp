#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <dlfcn.h>
#endif

// These declarations match the Servo C API at commit facef05f121a7ac657050eb8d5e450188efcb69f.
struct Servo;
struct ServoBuilder;
struct ServoOptions;
struct RenderingContext;
struct ServoWebViewBuilder;
struct WebView;

struct ServoWebViewDelegate {
    void *user_data;
    void (*notify_load_status_changed)(WebView *, int32_t, void *);
    void (*notify_new_frame_ready)(WebView *, void *);
};

typedef void (*ScreenshotCallback)(const uint8_t *, uint32_t, uint32_t, int32_t, void *);

struct Api {
    ServoBuilder *(*builder_create)();
    Servo *(*builder_build)(ServoBuilder *);
    void (*builder_free)(ServoBuilder *);
    void (*builder_set_options)(ServoBuilder *, ServoOptions *);
    ServoOptions *(*options_create)();
    void (*options_set_multiprocess)(ServoOptions *, bool);
    void (*servo_free)(Servo *);
    void (*spin_event_loop)(Servo *);
    RenderingContext *(*context_create_software)(uint32_t, uint32_t);
    void (*context_free)(RenderingContext *);
    ServoWebViewBuilder *(*webview_builder_create)(Servo *, RenderingContext *);
    void (*webview_builder_set_delegate)(ServoWebViewBuilder *, ServoWebViewDelegate);
    int32_t (*webview_builder_set_url)(ServoWebViewBuilder *, const char *);
    WebView *(*webview_builder_build)(ServoWebViewBuilder *);
    void (*webview_builder_free)(ServoWebViewBuilder *);
    void (*webview_paint)(WebView *);
    void (*webview_take_screenshot)(WebView *, ScreenshotCallback, void *);
    void (*webview_free)(WebView *);
};

class Library {
public:
    explicit Library(const char *path) : handle_(nullptr) {
#if defined(_WIN32)
        handle_ = LoadLibraryA(path);
#else
        handle_ = dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
    }
    ~Library() {
        if (handle_) {
#if defined(_WIN32)
            FreeLibrary(static_cast<HMODULE>(handle_));
#else
            dlclose(handle_);
#endif
        }
    }
    bool loaded() const { return handle_ != nullptr; }
    template <typename T> bool resolve(T &function, const char *name) {
#if defined(_WIN32)
        FARPROC address = GetProcAddress(static_cast<HMODULE>(handle_), name);
#else
        void *address = dlsym(handle_, name);
#endif
        if (!address) {
            std::fprintf(stderr, "Missing Servo symbol: %s\n", name);
            return false;
        }
        static_assert(sizeof(function) == sizeof(address), "Unsupported function pointer size");
        std::memcpy(&function, &address, sizeof(function));
        return true;
    }
private:
    void *handle_;
    Library(const Library &);
    Library &operator=(const Library &);
};

static bool resolve_api(Library &library, Api &api) {
#define RESOLVE(field, symbol) if (!library.resolve(api.field, symbol)) return false
    RESOLVE(builder_create, "servo_builder_create");
    RESOLVE(builder_build, "servo_builder_build");
    RESOLVE(builder_free, "servo_builder_free");
    RESOLVE(builder_set_options, "servo_builder_set_options");
    RESOLVE(options_create, "servo_options_create");
    RESOLVE(options_set_multiprocess, "servo_options_set_multiprocess");
    RESOLVE(servo_free, "servo_free");
    RESOLVE(spin_event_loop, "servo_spin_event_loop");
    RESOLVE(context_create_software, "servo_rendering_context_create_software");
    RESOLVE(context_free, "servo_rendering_context_free");
    RESOLVE(webview_builder_create, "servo_webview_builder_create");
    RESOLVE(webview_builder_set_delegate, "servo_webview_builder_set_delegate");
    RESOLVE(webview_builder_set_url, "servo_webview_builder_set_url");
    RESOLVE(webview_builder_build, "servo_webview_builder_build");
    RESOLVE(webview_builder_free, "servo_webview_builder_free");
    RESOLVE(webview_paint, "servo_webview_paint");
    RESOLVE(webview_take_screenshot, "servo_webview_take_screenshot");
    RESOLVE(webview_free, "servo_webview_free");
#undef RESOLVE
    return true;
}

struct State {
    Api *api;
    const char *output;
    bool check_red;
    bool loaded;
    bool painted;
    bool requested;
    bool done;
    bool succeeded;
};

static void on_screenshot(const uint8_t *rgba, uint32_t width, uint32_t height,
                          int32_t error, void *user_data) {
    State &state = *static_cast<State *>(user_data);
    state.done = true;
    if (!rgba || width != 320 || height != 240) {
        std::fprintf(stderr, "Screenshot failed or has unexpected dimensions: %ux%u, error %d\n",
                     width, height, error);
        return;
    }
    FILE *file = std::fopen(state.output, "wb");
    if (!file) {
        std::fprintf(stderr, "Cannot create screenshot: %s\n", state.output);
        return;
    }
    bool ok = std::fprintf(file, "P6\n%u %u\n255\n", width, height) > 0;
    bool red = true;
    for (uint32_t i = 0; i < width * height; ++i) {
        const uint8_t *pixel = rgba + i * 4;
        if (state.check_red && (pixel[0] != 255 || pixel[1] != 0 || pixel[2] != 0 || pixel[3] != 255))
            red = false;
        if (std::fwrite(pixel, 1, 3, file) != 3)
            ok = false;
    }
    if (std::fclose(file) != 0)
        ok = false;
    state.succeeded = ok && red;
    std::printf("Screenshot %ux%u: %s%s\n", width, height,
                state.succeeded ? "PASS" : "FAIL", state.check_red ? " (red pixel check)" : "");
}

static void on_frame(WebView *webview, void *user_data) {
    State &state = *static_cast<State *>(user_data);
    state.api->webview_paint(webview);
    state.painted = true;
}

static void on_load(WebView *, int32_t status, void *user_data) {
    State &state = *static_cast<State *>(user_data);
    state.loaded = status == 2;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 4) {
        std::fprintf(stderr, "Usage: servo_probe <servo_capi library> [URL] [output.ppm]\n");
        return 2;
    }
    Library library(argv[1]);
    if (!library.loaded()) {
        std::fprintf(stderr, "Cannot load Servo library: %s (check dependencies and architecture)\n", argv[1]);
        return 2;
    }
    Api api = {};
    if (!resolve_api(library, api))
        return 2;

    const bool check_red = argc < 3;
    const char *url = check_red ? "data:text/html,<html><body style='background:red'></body></html>" : argv[2];
    State state = {&api, argc < 4 ? "servo_probe.ppm" : argv[3], check_red, false, false, false, false, false};
    ServoBuilder *builder = api.builder_create();
    if (!builder) {
        std::fprintf(stderr, "Cannot create Servo builder\n");
        return 1;
    }
    ServoOptions *options = api.options_create();
    if (!options) {
        std::fprintf(stderr, "Cannot create Servo options\n");
        api.builder_free(builder);
        return 1;
    }
    api.options_set_multiprocess(options, false);
    api.builder_set_options(builder, options);
    Servo *servo = api.builder_build(builder);
    if (!servo) {
        std::fprintf(stderr, "Cannot initialize Servo\n");
        return 1;
    }
    RenderingContext *context = api.context_create_software(320, 240);
    if (!context) {
        std::fprintf(stderr, "Cannot create Servo software rendering context\n");
        api.servo_free(servo);
        return 1;
    }
    ServoWebViewBuilder *view_builder = api.webview_builder_create(servo, context);
    if (!view_builder) {
        std::fprintf(stderr, "Cannot create Servo webview builder\n");
        api.context_free(context);
        api.servo_free(servo);
        return 1;
    }
    ServoWebViewDelegate delegate = {&state, on_load, on_frame};
    api.webview_builder_set_delegate(view_builder, delegate);
    if (api.webview_builder_set_url(view_builder, url) != 0) {
        std::fprintf(stderr, "Invalid URL: %s\n", url);
        api.webview_builder_free(view_builder);
        api.servo_free(servo);
        return 1;
    }
    WebView *webview = api.webview_builder_build(view_builder);
    if (!webview) {
        std::fprintf(stderr, "Cannot create Servo webview\n");
        api.servo_free(servo);
        return 1;
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
    while (!state.done && std::chrono::steady_clock::now() < deadline) {
        api.spin_event_loop(servo);
        if (state.loaded && state.painted && !state.requested) {
            state.requested = true;
            api.webview_take_screenshot(webview, on_screenshot, &state);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
    }
    if (!state.done)
        std::fprintf(stderr, "Timed out waiting for the Servo screenshot\n");
    api.webview_free(webview);
    api.servo_free(servo);
    return state.succeeded ? 0 : 1;
}
