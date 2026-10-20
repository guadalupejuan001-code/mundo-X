#include <jni.h>
#include <cstdint>
#include <exception>
#include "mundo_x/infinite_bridge.hpp"

// Test-only second owner for the same production shared-session handle.
extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_graphics_EtherFusionJni_share(JNIEnv* env, jclass, jlong value) {
  try {
    auto* handle = reinterpret_cast<void*>(static_cast<std::uintptr_t>(value));
    auto* shared = mundo_x::share_infinite_session(mundo_x::infinite_session_from_handle(handle));
    return static_cast<jlong>(reinterpret_cast<std::uintptr_t>(shared));
  } catch (const std::exception& error) {
    auto type = env->FindClass("java/lang/IllegalStateException");
    if (type) { env->ThrowNew(type, error.what()); env->DeleteLocalRef(type); }
    return 0;
  }
}
