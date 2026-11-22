#ifndef HL2SB_CEFRIUM_FRAME_H
#define HL2SB_CEFRIUM_FRAME_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Hl2sbCefriumFrameInfo {
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint64_t sequence;
} Hl2sbCefriumFrameInfo;

/*
 * Copies the newest premultiplied RGBA frame under a lock. Returns the number
 * of bytes required; no pixels are copied if capacity is too small. A zero
 * return means the handle has been closed or no frame has arrived. The
 * caller owns the output buffer and may call from any thread.
 */
size_t hl2sb_cefrium_copy_rgba(uint64_t handle, void *output, size_t capacity,
                               Hl2sbCefriumFrameInfo *info);

#ifdef __cplusplus
}
#endif
#endif
