#include "cefrium_runtime.h"

#include <jni.h>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {
struct ViewState {
  std::mutex mutex;
  uint64_t frame = 0;
  std::string error;
};

std::mutex g_mutex;
JavaVM *g_vm = nullptr;
jclass g_runtime = nullptr;
std::unordered_map<uint64_t, std::shared_ptr<ViewState>> g_views;
std::atomic<uint64_t> g_next(1);

std::shared_ptr<ViewState> Find(uint64_t id) {
  std::lock_guard<std::mutex> lock(g_mutex);
  auto it = g_views.find(id);
  return it == g_views.end() ? nullptr : it->second;
}

void Error(uint64_t id, const char *message) {
  auto view = Find(id);
  if (!view) return;
  std::lock_guard<std::mutex> lock(view->mutex);
  view->error = message ? message : "Cefrium operation failed";
}

struct ScopedEnv {
  JavaVM *vm = nullptr;
  JNIEnv *env = nullptr;
  bool attached = false;
  ScopedEnv() {
    { std::lock_guard<std::mutex> lock(g_mutex); vm = g_vm; }
    if (!vm) return;
    jint result = vm->GetEnv(reinterpret_cast<void **>(&env), JNI_VERSION_1_6);
    if (result == JNI_EDETACHED) {
#if defined(__ANDROID__)
      if (vm->AttachCurrentThread(&env, nullptr) == JNI_OK) attached = true;
#else
      if (vm->AttachCurrentThread(reinterpret_cast<void **>(&env), nullptr) == JNI_OK) attached = true;
#endif
      else env = nullptr;
    } else if (result != JNI_OK) env = nullptr;
  }
  ~ScopedEnv() { if (attached) vm->DetachCurrentThread(); }
};

bool PendingException(JNIEnv *env, uint64_t id) {
  if (!env->ExceptionCheck()) return false;
  env->ExceptionDescribe();
  env->ExceptionClear();
  Error(id, "Cefrium Java operation raised an exception");
  return true;
}

jmethodID Method(JNIEnv *env, const char *name, const char *signature, uint64_t id) {
  jclass type;
  { std::lock_guard<std::mutex> lock(g_mutex); type = g_runtime; }
  if (!type) return nullptr;
  jmethodID method = env->GetStaticMethodID(type, name, signature);
  if (PendingException(env, id) || !method) return nullptr;
  return method;
}

/* JNI modified UTF-8 differs from normal UTF-8 for supplementary code points. */
jstring Utf8String(JNIEnv *env, const char *text) {
  if (!text) return nullptr;
  try {
  std::u16string out;
  const unsigned char *p = reinterpret_cast<const unsigned char *>(text);
  while (*p) {
    unsigned value = *p++;
    if (value >= 0x80) {
      unsigned count = value >= 0xf0 && value <= 0xf4 ? 3 :
                       value >= 0xe0 && value <= 0xef ? 2 :
                       value >= 0xc2 && value <= 0xdf ? 1 : 0;
      unsigned min = count == 1 ? 0x80 : count == 2 ? 0x800 : 0x10000;
      value = count ? value & ((1u << (6 - count)) - 1) : 0xfffd;
      bool valid = count != 0;
      for (unsigned i = 0; i < count; ++i) {
        if (!*p || (*p & 0xc0) != 0x80) { valid = false; break; }
        value = (value << 6) | (*p++ & 0x3f);
      }
      if (!valid || value < min || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff))
        value = 0xfffd;
    }
    if (value < 0x10000) out.push_back(static_cast<char16_t>(value));
    else {
      value -= 0x10000;
      out.push_back(static_cast<char16_t>(0xd800 + (value >> 10)));
      out.push_back(static_cast<char16_t>(0xdc00 + (value & 0x3ff)));
    }
  }
  return env->NewString(reinterpret_cast<const jchar *>(out.data()), static_cast<jsize>(out.size()));
  } catch (...) {
    return nullptr;
  }
}

int StringCall(uint64_t id, const char *method_name, const char *text) {
  if (!Find(id) || !text) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  jmethodID method = Method(call.env, method_name, "(JLjava/lang/String;)V", id);
  if (!method) return 0;
  jstring value = Utf8String(call.env, text);
  if (!value || PendingException(call.env, id)) return 0;
  call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), value);
  call.env->DeleteLocalRef(value);
  return !PendingException(call.env, id);
}
}

extern "C" JNIEXPORT jboolean JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_nativeAttach(JNIEnv *env, jclass type) {
  JavaVM *vm = nullptr;
  if (env->GetJavaVM(&vm) != JNI_OK) return JNI_FALSE;
  std::lock_guard<std::mutex> lock(g_mutex);
  if (g_runtime) return env->IsSameObject(g_runtime, type) ? JNI_TRUE : JNI_FALSE;
  g_runtime = static_cast<jclass>(env->NewGlobalRef(type));
  if (!g_runtime) return JNI_FALSE;
  g_vm = vm;
  return JNI_TRUE;
}

extern "C" JNIEXPORT void JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_nativeReady(JNIEnv *, jclass, jlong id, jlong frame) {
  auto view = Find(static_cast<uint64_t>(id));
  if (!view) return;
  std::lock_guard<std::mutex> lock(view->mutex);
  view->frame = static_cast<uint64_t>(frame);
}

extern "C" JNIEXPORT void JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_nativeError(JNIEnv *env, jclass, jlong id, jstring message) {
  const char *value = message ? env->GetStringUTFChars(message, nullptr) : nullptr;
  Error(static_cast<uint64_t>(id), value);
  if (value) env->ReleaseStringUTFChars(message, value);
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_runtime_attached() {
  std::lock_guard<std::mutex> lock(g_mutex);
  return g_vm && g_runtime;
}

extern "C" __attribute__((visibility("default"))) uint64_t hl2sb_cefrium_view_create(int width, int height) {
  if (width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  uint64_t id = g_next.fetch_add(1);
  if (!id) return 0;
  try {
    auto view = std::make_shared<ViewState>();
    std::lock_guard<std::mutex> lock(g_mutex);
    if (!g_views.emplace(id, view).second) return 0;
  } catch (...) {
    return 0;
  }
  jmethodID method = Method(call.env, "create", "(JII)V", id);
  if (method) call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), width, height);
  if (!method || PendingException(call.env, id)) {
    std::lock_guard<std::mutex> lock(g_mutex);
    g_views.erase(id);
    return 0;
  }
  return id;
}

extern "C" __attribute__((visibility("default"))) void hl2sb_cefrium_view_destroy(uint64_t id) {
  if (!Find(id)) return;
  ScopedEnv call;
  if (call.env) {
    jmethodID method = Method(call.env, "destroy", "(J)V", id);
    if (method) call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id));
    PendingException(call.env, id);
  }
  std::lock_guard<std::mutex> lock(g_mutex);
  g_views.erase(id);
}

extern "C" __attribute__((visibility("default"))) uint64_t hl2sb_cefrium_view_frame(uint64_t id) {
  auto view = Find(id);
  if (!view) return 0;
  std::lock_guard<std::mutex> lock(view->mutex);
  return view->frame;
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_resize(uint64_t id, int width, int height) {
  if (!Find(id) || width <= 0 || height <= 0 || width > 8192 || height > 8192) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  jmethodID method = Method(call.env, "resize", "(JII)V", id);
  if (!method) return 0;
  call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), width, height);
  return !PendingException(call.env, id);
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_navigate(uint64_t id, const char *url) {
  return StringCall(id, "navigate", url);
}
extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_eval(uint64_t id, const char *script) {
  return StringCall(id, "eval", script);
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_history(uint64_t id, int action) {
  if (!Find(id) || action < 0 || action > 3) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  jmethodID method = Method(call.env, "history", "(JI)V", id);
  if (!method) return 0;
  call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), action);
  return !PendingException(call.env, id);
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_mouse(uint64_t id, int type, int x, int y,
    int button, int modifiers, int dx, int dy) {
  if (!Find(id)) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  jmethodID method = Method(call.env, "mouse", "(JIIIIIII)V", id);
  if (!method) return 0;
  call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), type, x, y, button, modifiers, dx, dy);
  return !PendingException(call.env, id);
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_key(uint64_t id, int type, int native_key,
    int virtual_key, int modifiers, int character) {
  if (!Find(id)) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  jmethodID method = Method(call.env, "key", "(JIIIII)V", id);
  if (!method) return 0;
  call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), type, native_key, virtual_key, modifiers, character);
  return !PendingException(call.env, id);
}

extern "C" __attribute__((visibility("default"))) int hl2sb_cefrium_view_ime(uint64_t id, int stage, const char *text) {
  if (!Find(id) || !text) return 0;
  ScopedEnv call;
  if (!call.env) return 0;
  jmethodID method = Method(call.env, "ime", "(JILjava/lang/String;)V", id);
  if (!method) return 0;
  jstring value = Utf8String(call.env, text);
  if (!value || PendingException(call.env, id)) return 0;
  call.env->CallStaticVoidMethod(g_runtime, method, static_cast<jlong>(id), stage, value);
  call.env->DeleteLocalRef(value);
  return !PendingException(call.env, id);
}

extern "C" __attribute__((visibility("default"))) size_t hl2sb_cefrium_view_error(uint64_t id, char *buffer, size_t capacity) {
  auto view = Find(id);
  if (!view) return 0;
  std::lock_guard<std::mutex> lock(view->mutex);
  size_t length = view->error.size();
  if (buffer && capacity > length) {
    std::memcpy(buffer, view->error.c_str(), length + 1);
    view->error.clear();
  }
  return length;
}
