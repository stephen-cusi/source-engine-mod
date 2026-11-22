#include "../cefrium_runtime.h"
#include <jni.h>
#include <string>

extern "C" JNIEXPORT jlong JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_probeCreate(JNIEnv *, jclass) {
  return static_cast<jlong>(hl2sb_cefrium_view_create(320, 200));
}
extern "C" JNIEXPORT jlong JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_probeFrame(JNIEnv *, jclass, jlong id) {
  return static_cast<jlong>(hl2sb_cefrium_view_frame(id));
}
extern "C" JNIEXPORT jboolean JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_probeNavigate(JNIEnv *env, jclass, jlong id, jstring url) {
  // Use ordinary UTF-8 bytes, including a supplementary code point.
  (void)env; (void)url;
  return hl2sb_cefrium_view_navigate(id, "asset://example/\xe4\xb8\xad\xe6\x96\x87/\xf0\x9f\x99\x82") ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jboolean JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_probeKey(JNIEnv *, jclass, jlong id) {
  return hl2sb_cefrium_view_key(id, 2, 0, 0, 0, 97) ? JNI_TRUE : JNI_FALSE;
}
extern "C" JNIEXPORT jstring JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_probeError(JNIEnv *env, jclass, jlong id) {
  size_t length = hl2sb_cefrium_view_error(id, nullptr, 0);
  std::string text(length + 1, '\0');
  if (length) hl2sb_cefrium_view_error(id, &text[0], text.size());
  return env->NewStringUTF(text.c_str());
}
extern "C" JNIEXPORT void JNICALL
Java_org_hl2sb_cefrium_CefriumRuntime_probeDestroy(JNIEnv *, jclass, jlong id) {
  hl2sb_cefrium_view_destroy(id);
}
