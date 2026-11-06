#include <cstdint>
#include <vector>

#if defined(_WIN32)
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C" __attribute__((visibility("default")))
#endif

struct ServoBuilder {};
struct ServoOptions {};
struct Context { uint32_t w, h; };
struct View;
struct Delegate { void *user; void (*load)(View *, int32_t, void *); void (*frame)(View *, void *); };
typedef void (*Screenshot)(const uint8_t *, uint32_t, uint32_t, int32_t, void *);
struct View { uint32_t w, h, requestedW, requestedH; Delegate delegate; Screenshot screenshot; void *user; bool dirty; };
struct Servo { std::vector<View *> views; };
struct ViewBuilder { Servo *servo; Context *context; Delegate delegate; };
static Servo *engine;
static Screenshot deferred;
static void *deferredUser;

EXPORT ServoBuilder *servo_builder_create() { return new ServoBuilder; }
EXPORT void servo_builder_free(ServoBuilder *b) { delete b; }
EXPORT ServoOptions *servo_options_create() { return new ServoOptions; }
EXPORT void servo_options_set_multiprocess(ServoOptions *, bool) {}
EXPORT void servo_builder_set_options(ServoBuilder *, ServoOptions *o) { delete o; }
EXPORT Servo *servo_builder_build(ServoBuilder *b) { delete b; return engine = new Servo; }
EXPORT void servo_free(Servo *s) { delete s; engine = nullptr; deferred = nullptr; deferredUser = nullptr; }
EXPORT Context *servo_rendering_context_create_software(uint32_t w, uint32_t h) { return new Context{w, h}; }
EXPORT void servo_rendering_context_free(Context *c) { delete c; }
EXPORT ViewBuilder *servo_webview_builder_create(Servo *s, Context *c) { return new ViewBuilder{s, c, {}}; }
EXPORT void servo_webview_builder_set_delegate(ViewBuilder *b, Delegate d) { b->delegate = d; }
EXPORT void servo_webview_builder_free(ViewBuilder *b) { delete b->context; delete b; }
EXPORT View *servo_webview_builder_build(ViewBuilder *b) {
    View *v = new View{b->context->w, b->context->h, 0, 0, b->delegate, nullptr, nullptr, true};
    b->servo->views.push_back(v); delete b->context; delete b; return v;
}
EXPORT void servo_webview_free(View *v) {
    if (v->screenshot) { deferred = v->screenshot; deferredUser = v->user; }
    for (auto i = engine->views.begin(); i != engine->views.end(); ++i) {
        if (*i == v) { engine->views.erase(i); break; }
    }
    delete v;
}
EXPORT void servo_webview_paint(View *) {}
EXPORT void servo_webview_resize(View *v, uint32_t w, uint32_t h) { v->w = w; v->h = h; v->dirty = true; }
EXPORT int32_t servo_webview_load(View *v, const char *) { v->dirty = true; return 0; }
EXPORT void servo_webview_take_screenshot(View *v, Screenshot cb, void *user) {
    v->requestedW = v->w; v->requestedH = v->h; v->screenshot = cb; v->user = user;
}
EXPORT void servo_spin_event_loop(Servo *s) {
    if (deferred) { Screenshot cb = deferred; deferred = nullptr; cb(nullptr, 0, 0, 1, deferredUser); }
    for (View *v : s->views) {
        if (v->screenshot) {
            Screenshot cb = v->screenshot; v->screenshot = nullptr;
            std::vector<uint8_t> pixels(size_t(v->requestedW) * v->requestedH * 4, 42);
            cb(pixels.data(), v->requestedW, v->requestedH, 0, v->user);
        }
        if (v->dirty) { v->dirty = false; v->delegate.frame(v, v->delegate.user); }
    }
}
