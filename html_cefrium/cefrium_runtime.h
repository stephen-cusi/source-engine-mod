#ifndef HL2SB_CEFRIUM_RUNTIME_H
#define HL2SB_CEFRIUM_RUNTIME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Calls dispatch to the Android UI thread and do not wait for completion. */
int hl2sb_cefrium_runtime_attached(void);
uint64_t hl2sb_cefrium_view_create(int width, int height);
void hl2sb_cefrium_view_destroy(uint64_t id);
uint64_t hl2sb_cefrium_view_frame(uint64_t id);
int hl2sb_cefrium_view_resize(uint64_t id, int width, int height);
int hl2sb_cefrium_view_navigate(uint64_t id, const char *url);
int hl2sb_cefrium_view_eval(uint64_t id, const char *script);
/* action: 0 back, 1 forward, 2 reload, 3 stop. */
int hl2sb_cefrium_view_history(uint64_t id, int action);
int hl2sb_cefrium_view_mouse(uint64_t id, int type, int x, int y, int button,
                            int modifiers, int dx, int dy);
int hl2sb_cefrium_view_key(uint64_t id, int type, int native_key, int virtual_key,
                          int modifiers, int character);
int hl2sb_cefrium_view_ime(uint64_t id, int stage, const char *text);
/* Returns the full error length, or zero when no error is queued. */
size_t hl2sb_cefrium_view_error(uint64_t id, char *buffer, size_t capacity);

#ifdef __cplusplus
}
#endif
#endif
