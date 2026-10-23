#include <jni.h>
#include <iomanip>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/worldlink_quantum_bridge.hpp"

namespace {
std::mutex mutex;
std::unique_ptr<mundo_x::WorldLinkQuantumBridge> engine;

jstring result_string(JNIEnv* env, const std::string& value) {
  return env->NewStringUTF(value.c_str());
}
std::string escape_json(const std::string& value) {
  std::string out;
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') out += '\\';
    if (c < 0x20) out += ' ';
    else out += static_cast<char>(c);
  }
  return out;
}
std::string error_json(const std::string& text) {
  return "{\"ok\":false,\"error\":\"" + escape_json(text) + "\"}";
}
mundo_x::WorldLinkQuantumBridge& active() {
  if (!engine) engine=std::make_unique<mundo_x::WorldLinkQuantumBridge>();
  return *engine;
}
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_worldlink_phone_QuantumBridge_nativeEvolve(
    JNIEnv* env, jclass, jfloat magnetic_uT, jfloat angular_speed, jfloat lux) {
  try {
    std::lock_guard<std::mutex> lock(mutex);
    const auto r=active().evolve(magnetic_uT,angular_speed,lux);
    std::ostringstream o;
    o << std::fixed << std::setprecision(8);
    o << "{\"ok\":true"
      << ",\"model\":\"MIL-ReducedExactT-1000+Ether-InverseMemory-COW256\""
      << ",\"basis\":\"simulated\""
      << ",\"physical_entanglement\":false"
      << ",\"logical_qubits\":1000"
      << ",\"magnetic_available\":" << (r.magnetic_available?"true":"false")
      << ",\"motion_available\":" << (r.motion_available?"true":"false")
      << ",\"light_available\":" << (r.light_available?"true":"false")
      << ",\"xx\":" << r.xx
      << ",\"zz\":" << r.zz
      << ",\"norm\":" << r.norm
      << ",\"phase\":" << r.phase_radians
      << ",\"frequency_hz\":" << r.frequency_hz
      << ",\"components\":" << r.components
      << ",\"mil_logical_bytes\":" << r.mil_logical_bytes
      << ",\"ether_active_qubits\":" << r.ether_active_qubits
      << ",\"ether_memory_overrides\":" << r.ether_memory_overrides
      << ",\"event\":" << r.event
      << ",\"calculation_ms\":" << r.calculation_ms
      << "}";
    return result_string(env,o.str());
  } catch(const std::exception& e) {
    return result_string(env,error_json(e.what()));
  } catch(...) {
    return result_string(env,error_json("Unhandled native failure"));
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_worldlink_phone_QuantumBridge_nativeStatus(
    JNIEnv* env, jclass) {
  try {
    std::lock_guard<std::mutex> lock(mutex);
    std::ostringstream o;
    o << "{\"ok\":true"
      << ",\"model\":\"MIL-ReducedExactT-1000+Ether-InverseMemory-COW256\""
      << ",\"logical_qubits\":1000"
      << ",\"physical_entanglement\":false"
      << ",\"event_count\":" << active().events()
      << ",\"ether_active_qubits\":" << active().active_simulated_qubits()
      << "}";
    return result_string(env,o.str());
  } catch(const std::exception& e) {
    return result_string(env,error_json(e.what()));
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_worldlink_phone_QuantumBridge_nativeReset(
    JNIEnv* env, jclass) {
  try {
    std::lock_guard<std::mutex> lock(mutex);
    auto candidate=std::make_unique<mundo_x::WorldLinkQuantumBridge>();
    engine=std::move(candidate);
    return result_string(env,
      "{\"ok\":true,\"reset\":true,\"physical_entanglement\":false}");
  } catch(const std::exception& e) {
    return result_string(env,error_json(e.what()));
  }
}
