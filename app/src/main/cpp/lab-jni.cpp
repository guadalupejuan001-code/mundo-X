#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/lab_circuit.hpp"
#include "mundo_x/lab_memory.hpp"
#include "mundo_x/lab_protocols.hpp"

namespace {
mundo_x::LabMemoryJob memory_job;

jstring json_string(JNIEnv* env, const std::string& value) {
  return env->NewStringUTF(value.c_str());
}
jstring error(JNIEnv* env, const char* message) {
  std::ostringstream out;
  out << "{\"ok\":false,\"error\":";
  mundo_x::lab_detail::quoted(out, message);
  out << '}';
  return json_string(env, out.str());
}
template <typename Function>
jstring guarded(JNIEnv* env, Function function) {
  try { return json_string(env, function()); }
  catch (const std::exception& e) { return error(env, e.what()); }
}

class UtfChars {
 public:
  UtfChars(JNIEnv* env, jstring value)
      : env_(env), value_(value), chars_(env->GetStringUTFChars(value, nullptr)) {}
  ~UtfChars() { if (chars_) env_->ReleaseStringUTFChars(value_, chars_); }
  const char* get() const { return chars_; }
 private:
  JNIEnv* env_;
  jstring value_;
  const char* chars_;
};
}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_labCircuit(JNIEnv* env, jclass, jint qubits,
                                         jstring script, jint steps, jint shots,
                                         jlong seed) {
  if (!script) return error(env, "El circuito esta vacio");
  if (env->GetStringUTFLength(script) > 16 * 1024)
    return error(env, "El circuito supera 16 KiB");
  if (qubits < 1 || shots < 0) return error(env, "Qubits o lecturas fuera de rango");
  UtfChars text(env, script);
  if (!text.get()) return nullptr;  // Preserve the pending Java allocation error.
  return guarded(env, [&] {
    return mundo_x::run_lab_circuit(static_cast<std::size_t>(qubits), text.get(),
        steps, static_cast<std::size_t>(shots), static_cast<std::uint64_t>(seed));
  });
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_labProtocol(JNIEnv* env, jclass, jint protocol,
                                          jint value, jint shots, jlong seed) {
  if (shots < 0) return error(env, "Lecturas fuera de rango");
  return guarded(env, [&] {
    return mundo_x::run_lab_protocol(protocol, value,
        static_cast<std::size_t>(shots), static_cast<std::uint64_t>(seed));
  });
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_labMemoryRun(JNIEnv* env, jclass, jint budget_mib,
                                           jint qubits, jint family) {
  if (budget_mib < 8 || qubits < 2) return error(env, "Presupuesto o qubits fuera de rango");
  return guarded(env, [&] {
    return memory_job.run(static_cast<std::size_t>(budget_mib),
                          static_cast<std::size_t>(qubits), family);
  });
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_labMemoryProgress(JNIEnv* env, jclass) {
  return guarded(env, [&] { return memory_job.progress_json(); });
}

extern "C" JNIEXPORT void JNICALL
Java_com_mundox_lab_NativeCore_labMemoryCancel(JNIEnv*, jclass) {
  memory_job.cancel();
}
