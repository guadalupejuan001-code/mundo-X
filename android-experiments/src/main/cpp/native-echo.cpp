#include <jni.h>
#include <atomic>
#include <mutex>
#include <string>
#include "mundo_x/quantum_echo.hpp"

namespace {
// Each invocation owns a generation. Cancelling a queued job, rotating the
// activity, or starting a new job cannot silently revive an older invocation.
std::atomic<std::uint64_t> generation{0};
std::mutex execution;
jstring reply(JNIEnv* env, const std::string& json) {
  return env->NewStringUTF(json.c_str());
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_echoes_MainActivity_nativePrepare(JNIEnv*, jclass) {
  return static_cast<jlong>(generation.fetch_add(1, std::memory_order_relaxed) + 1);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_echoes_MainActivity_nativeRun(
    JNIEnv* env, jclass, jlong token, jint rows, jint cols, jint depth, jlong seed,
    jint perturb, jint probe, jint rounds, jint max_components,
    jboolean compare_dense) {
  std::lock_guard<std::mutex> lock(execution);
  try {
    mundo_x::quantum_echo::Config config;
    config.rows = static_cast<std::size_t>(rows);
    config.cols = static_cast<std::size_t>(cols);
    config.depth = static_cast<std::size_t>(depth);
    config.seed = static_cast<std::uint64_t>(seed);
    config.perturb = static_cast<std::size_t>(perturb);
    config.probe = static_cast<std::size_t>(probe);
    config.rounds = static_cast<std::size_t>(rounds);
    config.max_components = static_cast<std::size_t>(max_components);
    config.compare_dense = compare_dense == JNI_TRUE;
    const auto result = mundo_x::quantum_echo::run_echo(config, [token] {
      return generation.load(std::memory_order_relaxed) != static_cast<std::uint64_t>(token);
    });
    return reply(env, mundo_x::quantum_echo::to_json(result));
  } catch (const std::bad_alloc&) {
    return reply(env, R"({"status":"LIMIT","validation":"UNVERIFIED","message":"Memoria insuficiente. Reduce el circuito o el presupuesto de componentes."})");
  } catch (...) {
    return reply(env, R"({"status":"ERROR","validation":"UNVERIFIED","message":"El experimento no termino. No hay resultado valido."})");
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_echoes_MainActivity_nativeSelfTest(JNIEnv* env, jclass, jlong token) {
  std::lock_guard<std::mutex> lock(execution);
  try {
    return reply(env, mundo_x::quantum_echo::self_test_json([token] {
      return generation.load(std::memory_order_relaxed) != static_cast<std::uint64_t>(token);
    }));
  } catch (...) {
    return reply(env, R"({"status":"ERROR","passed":0,"total":1,"message":"No se pudieron completar las pruebas."})");
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_mundox_echoes_MainActivity_nativeCancel(JNIEnv*, jclass, jlong token) {
  auto expected = static_cast<std::uint64_t>(token);
  generation.compare_exchange_strong(expected, expected + 1, std::memory_order_relaxed);
}
