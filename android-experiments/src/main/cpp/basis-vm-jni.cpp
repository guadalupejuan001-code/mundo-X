#include <jni.h>
#include <string>
#include "mundo_x/basis_vm.hpp"

static jstring as_jstring(JNIEnv* env, const std::string& s) {
  return env->NewStringUTF(s.c_str());
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_BasisVmNative_basisVmRun(JNIEnv* env, jclass, jstring source,
                                        jint max_steps) {
  if (!source) return as_jstring(env, "{\"status\":\"ERROR\",\"message\":\"Falta el programa.\"}");
  if (env->GetStringUTFLength(source) > 16384)
    return as_jstring(env, "{\"status\":\"ERROR\",\"message\":\"Programa demasiado largo: maximo 16 KiB.\"}");
  const char* utf = env->GetStringUTFChars(source, nullptr);
  if (!utf) return nullptr;  // The JVM already has an OutOfMemoryError pending.
  std::string program;
  try {
    program.assign(utf);
  } catch (...) {
    env->ReleaseStringUTFChars(source, utf);
    return as_jstring(env, "{\"status\":\"ERROR\",\"message\":\"No se pudo leer el programa.\"}");
  }
  env->ReleaseStringUTFChars(source, utf);
  try {
    return as_jstring(env, mundo_x::basis_vm::run_json(program, max_steps));
  } catch (...) {
    return as_jstring(env, "{\"status\":\"ERROR\",\"message\":\"Fallo interno al ejecutar la VM.\"}");
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_BasisVmNative_basisVmSelfTest(JNIEnv* env, jclass) {
  try {
    return as_jstring(env, mundo_x::basis_vm::self_test_json());
  } catch (...) {
    return as_jstring(env, "{\"status\":\"ERROR\",\"message\":\"Fallo interno en las autopruebas.\"}");
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_BasisVmNative_basisVmExample(JNIEnv* env, jclass, jint index) {
  try {
    return as_jstring(env, mundo_x::basis_vm::example_source(index));
  } catch (...) {
    return as_jstring(env, "# No se pudo cargar el ejemplo\nHALT\n");
  }
}
