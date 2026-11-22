#ifndef HL2SB_HTML_BACKEND_ABI_H
#define HL2SB_HTML_BACKEND_ABI_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HL2SB_HTML_ABI_VERSION 1u

typedef struct HtmlBackend HtmlBackend;
typedef struct HtmlView HtmlView;

typedef enum HtmlEventType {
    HTML_EVENT_LOAD_START = 1,
    HTML_EVENT_DOCUMENT_READY,
    HTML_EVENT_LOAD_END,
    HTML_EVENT_LOAD_ERROR,
    HTML_EVENT_TITLE,
    HTML_EVENT_CONSOLE,
    HTML_EVENT_CURSOR,
    HTML_EVENT_NEW_WINDOW,
    HTML_EVENT_JS_DIALOG,
    HTML_EVENT_JS_CALL,
    HTML_EVENT_NAV_REQUEST,
    HTML_EVENT_ASSET_REQUEST,
    HTML_EVENT_ADDRESS_CHANGED
} HtmlEventType;

typedef enum HtmlMouseEventType {
    HTML_MOUSE_MOVE = 0, HTML_MOUSE_DOWN, HTML_MOUSE_UP, HTML_MOUSE_WHEEL
} HtmlMouseEventType;

typedef enum HtmlKeyEventType {
    HTML_KEY_DOWN = 0, HTML_KEY_UP, HTML_KEY_CHAR
} HtmlKeyEventType;

typedef enum HtmlImeStage {
    HTML_IME_START = 0, HTML_IME_UPDATE, HTML_IME_COMMIT
} HtmlImeStage;

typedef enum HtmlEventModifiers {
    HTML_MOD_SHIFT = 1u << 1,
    HTML_MOD_CONTROL = 1u << 2,
    HTML_MOD_ALT = 1u << 3,
    HTML_MOD_LEFT_MOUSE = 1u << 4,
    HTML_MOD_MIDDLE_MOUSE = 1u << 5,
    HTML_MOD_RIGHT_MOUSE = 1u << 6
} HtmlEventModifiers;

/*
 * Event strings remain valid until the next poll on this view or its destruction.
 * a contains the URL for loading/asset events, JSON for JS calls, or the text
 * for console and title events. JS call ids are answered with reply().
 * For console events b is the source, code is CEF severity, and the low
 * 32 bits of id contain the signed source line number.
 */
typedef struct HtmlEvent {
    uint32_t type;
    uint64_t id;
    int32_t code;
    const char *a;
    const char *b;
} HtmlEvent;

typedef struct HtmlRect {
    int32_t x, y, width, height;
} HtmlRect;

/* Pixel and rectangle pointers remain valid until unlock_frame. Pixels are BGRA8. */
typedef struct HtmlFrame {
    const uint8_t *pixels;
    uint32_t width, height, stride;
    uint64_t sequence;
    const HtmlRect *dirty_rects;
    size_t dirty_count;
} HtmlFrame;

/* Public calls are made on the thread that created the backend. */
typedef struct HtmlBackendVTable {
    uint32_t abi_version;
    uint32_t struct_size;
    HtmlBackend *(*create)(const char *cache_dir, const char *helper_path);
    void (*destroy)(HtmlBackend *backend);
    void (*pump)(HtmlBackend *backend);
    HtmlView *(*view_create)(HtmlBackend *backend, uint32_t width, uint32_t height);
    void (*view_destroy)(HtmlView *view);
    void (*view_resize)(HtmlView *view, uint32_t width, uint32_t height);
    void (*navigate)(HtmlView *view, const char *url);
    void (*back)(HtmlView *view);
    void (*forward)(HtmlView *view);
    void (*reload)(HtmlView *view);
    void (*stop)(HtmlView *view);
    void (*eval_js)(HtmlView *view, const char *script);
    void (*register_js)(HtmlView *view, const char *object, const char *function);
    void (*reply)(HtmlView *view, uint64_t id, const char *json);
    void (*asset_reply)(HtmlView *view, uint64_t id, int32_t status,
                        const char *mime, const uint8_t *data, size_t length);
    int (*poll)(HtmlView *view, HtmlEvent *event);
    void (*mouse)(HtmlView *view, int32_t type, int32_t x, int32_t y,
                  int32_t button, uint32_t modifiers, int32_t dx, int32_t dy);
    void (*key)(HtmlView *view, int32_t type, int32_t native_key,
                int32_t virtual_key, uint32_t modifiers, uint32_t character);
    void (*ime)(HtmlView *view, int32_t stage, const char *utf8);
    int (*lock_frame)(HtmlView *view, HtmlFrame *frame);
    void (*unlock_frame)(HtmlView *view);
} HtmlBackendVTable;

typedef const HtmlBackendVTable *(*HtmlGetBackendFn)(void);

#ifdef __cplusplus
}
#endif

#endif
