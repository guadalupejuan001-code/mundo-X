#include "watch_ghz_core.hpp"
#include <jni.h>
#include <mutex>

namespace {
std::mutex guard;
mundo_x::WatchGhzCore engine;
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_ghzwatch_NativeGhz_command(JNIEnv* env, jclass, jint operation,
                                        jint qubits, jlong seed) {
  try {
    std::lock_guard<std::mutex> lock(guard);
    mundo_x::WatchGhzCore::Report result;
    switch (operation) {
      case 0:
        if (qubits < 3 || qubits > 1000)
          throw std::invalid_argument("Cantidad de qubits fuera de rango");
        result = engine.prepare(static_cast<std::size_t>(qubits),
                                static_cast<std::uint64_t>(seed));
        break;
      case 1: result = engine.advance_phase(); break;
      case 2: result = engine.measure(); break;
      case 3: result = engine.reset(); break;
      case 4: result = engine.report(); break;
      default: throw std::invalid_argument("Operacion desconocida");
    }
    return env->NewStringUTF(result.json().c_str());
  } catch (const std::exception& error) {
    env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), error.what());
    return nullptr;
  } catch (...) {
    env->ThrowNew(env->FindClass("java/lang/IllegalStateException"), "Fallo del nucleo GHZ");
    return nullptr;
  }
}
