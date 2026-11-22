#include "cefrium_frame.h"

#include <jni.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace {
struct Frame {
  std::mutex mutex;
  std::vector<uint8_t> pixels;
  Hl2sbCefriumFrameInfo info{};
};

std::mutex g_mutex;
std::unordered_map<uint64_t, std::shared_ptr<Frame>> g_frames;
std::atomic<uint64_t> g_next(1);

std::shared_ptr<Frame> Find(uint64_t handle) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_frames.find(handle);
  return it == g_frames.end() ? nullptr : it->second;
}
}

extern "C" __attribute__((visibility("default")))
size_t hl2sb_cefrium_copy_rgba(uint64_t handle, void *output, size_t capacity,
                               Hl2sbCefriumFrameInfo *info) {
  auto frame = Find(handle);
  if (!frame) return 0;
  std::lock_guard<std::mutex> lock(frame->mutex);
  const size_t size = frame->pixels.size();
  if (info) *info = frame->info;
  if (size && output && capacity >= size) std::memcpy(output, frame->pixels.data(), size);
  return size;
}

extern "C" JNIEXPORT jlong JNICALL
Java_org_hl2sb_cefrium_CefriumView_nativeCreateFrame(JNIEnv *, jclass) {
  try {
    auto frame = std::make_shared<Frame>();
    const uint64_t handle = g_next.fetch_add(1);
    if (handle == 0) return 0;
    std::lock_guard<std::mutex> lock(g_mutex);
    return g_frames.emplace(handle, std::move(frame)).second ? static_cast<jlong>(handle) : 0;
  } catch (...) {
    return 0;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_org_hl2sb_cefrium_CefriumView_nativeDestroyFrame(JNIEnv *, jclass, jlong id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  g_frames.erase(static_cast<uint64_t>(id));
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_hl2sb_cefrium_CefriumView_nativeSubmitFrame(JNIEnv *env, jclass, jlong id,
                                                     jint width, jint height, jobject buffer) {
  if (!buffer || width <= 0 || height <= 0 || width > 8192 || height > 8192) return JNI_FALSE;
  const size_t size = static_cast<size_t>(width) * static_cast<size_t>(height) * 4;
  void *pixels = env->GetDirectBufferAddress(buffer);
  const jlong capacity = env->GetDirectBufferCapacity(buffer);
  if (!pixels || capacity < 0 || static_cast<uint64_t>(capacity) < size) return JNI_FALSE;
  auto frame = Find(static_cast<uint64_t>(id));
  if (!frame) return JNI_FALSE;
  try {
    std::lock_guard<std::mutex> lock(frame->mutex);
    frame->pixels.resize(size);
    std::memcpy(frame->pixels.data(), pixels, size);
    frame->info.width = static_cast<uint32_t>(width);
    frame->info.height = static_cast<uint32_t>(height);
    frame->info.stride = static_cast<uint32_t>(width) * 4;
    ++frame->info.sequence;
    return JNI_TRUE;
  } catch (...) {
    return JNI_FALSE;
  }
}
