#ifndef HL2SB_SERVO_BACKEND_H
#define HL2SB_SERVO_BACKEND_H

#include <stddef.h>
#include <stdint.h>

// Experimental wrapper for Servo C API revision facef05f121a7ac657050eb8d5e450188efcb69f.
// All functions must run on the thread that created the runtime. No VGUI or
// GMod navigation policy is provided here. The caller must validate navigation.
struct HLSBServoRuntime;
struct HLSBServoView;

// Library path must identify a trusted Servo library matching the process ABI.
// Only one runtime is permitted at a time. It may own multiple views.
HLSBServoRuntime *HLSBServoCreateRuntime(const char *libraryPath);
// Returns false on another thread or during Pump; no destruction then occurs.
bool HLSBServoDestroyRuntime(HLSBServoRuntime *runtime);
HLSBServoView *HLSBServoCreateView(HLSBServoRuntime *runtime, uint32_t width, uint32_t height);
bool HLSBServoDestroyView(HLSBServoView *view);
bool HLSBServoPump(HLSBServoRuntime *runtime);
// URL must be valid UTF-8. This low-level API does not enforce GMod URL policy.
bool HLSBServoNavigate(HLSBServoView *view, const char *url);
bool HLSBServoResize(HLSBServoView *view, uint32_t width, uint32_t height);
// -1 means no event yet, 0 started, 1 head parsed, 2 complete, 3 stopped.
int HLSBServoLoadStatus(HLSBServoView *view);
bool HLSBServoPopLoadEvent(HLSBServoView *view, int *status);
bool HLSBServoPopBridgeEvent(HLSBServoView *view, uint64_t *id, char *message, size_t capacity);
bool HLSBServoBridgeReply(HLSBServoView *view, uint64_t id, const char *reply);
bool HLSBServoPopAssetEvent(HLSBServoView *view, uint64_t *id, char *url, size_t capacity);
bool HLSBServoAssetReply(HLSBServoView *view, uint64_t id, uint16_t status,
                         const char *mime, const uint8_t *bytes, size_t length);
bool HLSBServoEvalJS(HLSBServoView *view, const char *script);
bool HLSBServoSetDocumentScript(HLSBServoView *view, const char *script);
bool HLSBServoMouseMove(HLSBServoView *view, float x, float y);
bool HLSBServoMouseButton(HLSBServoView *view, float x, float y, int button, bool down);
bool HLSBServoMouseWheel(HLSBServoView *view, float x, float y, double dx, double dy);
bool HLSBServoKey(HLSBServoView *view, const char *key, bool down);
bool HLSBServoText(HLSBServoView *view, const char *text);
// Stage: 0 starts, 1 updates, 2 ends a composition.
bool HLSBServoComposition(HLSBServoView *view, int stage, const char *text);
bool HLSBServoReload(HLSBServoView *view);
bool HLSBServoBack(HLSBServoView *view);
bool HLSBServoForward(HLSBServoView *view);
// Pixels are tightly packed RGBA. Returns required bytes; copies only if the
// supplied buffer is large enough. Sequence changes only after a valid frame.
size_t HLSBServoCopyFrame(HLSBServoView *view, void *buffer, size_t capacity,
                         uint32_t *width, uint32_t *height, uint64_t *sequence);

#endif
