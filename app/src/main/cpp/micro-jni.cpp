#include <jni.h>
#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <unordered_map>
#include "mundo_x/micro_lab.hpp"

namespace {
struct Session {
  explicit Session(std::uint64_t seed) : lab(seed) {}
  std::mutex mutex;
  mundo_x::MicroLab lab;
};
std::mutex registry_mutex;
std::unordered_map<jlong,std::shared_ptr<Session>> sessions;
std::atomic<jlong> sequence{1};
std::shared_ptr<Session> find_session(jlong id) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  const auto it=sessions.find(id);
  if(it==sessions.end()) throw std::invalid_argument("micro session closed");
  return it->second;
}
void error(JNIEnv* env,const char* message) {
  jclass type=env->FindClass("java/lang/IllegalStateException");
  if(type) env->ThrowNew(type,message);
}
}

extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_lab_NativeCore_microCreate(JNIEnv* env,jclass,jlong seed) {
  try {
    auto session=std::make_shared<Session>(static_cast<std::uint64_t>(seed));
    const jlong id=sequence.fetch_add(1);
    std::lock_guard<std::mutex> lock(registry_mutex);
    if(sessions.size()>=8) throw std::length_error("too many micro sessions");
    sessions.emplace(id,std::move(session)); return id;
  } catch(const std::exception& e) { error(env,e.what()); return 0; }
}
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_lab_NativeCore_microDestroy(JNIEnv*,jclass,jlong id) {
  std::lock_guard<std::mutex> lock(registry_mutex);
  sessions.erase(id); // In-flight calls retain their shared_ptr until finished.
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_microCommand(JNIEnv* env,jclass,jlong id,jint op,jint a,jint b,jfloat x,jfloat y) {
  try {
    auto session=find_session(id); std::lock_guard<std::mutex> lock(session->mutex);
    auto& lab=session->lab;
    switch(op) {
      case 0: lab.mode(a); break;
      case 1: lab.gate(a,b); break;
      case 2: lab.preset(a); break;
      case 3: lab.measure_all(); break;
      case 4: lab.coupled(a!=0); break;
      case 5: lab.agitation(x); break;
      case 6: lab.growth(x); break;
      case 7: if(a<0) throw std::invalid_argument("negative particles"); lab.particle_count(a); break;
      case 8: lab.reset(); break;
      case 9: lab.inject(a,x,y); break;
      case 10: if(a<0) throw std::invalid_argument("negative seed"); lab.reseed(static_cast<unsigned>(a)); break;
      case 11: lab.advance(1); break;
      default: throw std::invalid_argument("unknown micro command");
    }
    return env->NewStringUTF(lab.json().c_str());
  } catch(const std::exception& e) { error(env,e.what()); return nullptr; }
}
extern "C" JNIEXPORT jint JNICALL
Java_com_mundox_lab_NativeCore_microFrame(JNIEnv* env,jclass,jlong id,jint steps,jobject buffer) {
  try {
    if(steps<0 || steps>8 || !buffer) throw std::invalid_argument("invalid frame request");
    void* memory=env->GetDirectBufferAddress(buffer);
    const jlong bytes=env->GetDirectBufferCapacity(buffer);
    if(!memory || bytes<0) throw std::invalid_argument("direct ByteBuffer required");
    if(static_cast<std::size_t>(bytes)<mundo_x::MicroLab::max_vertices*mundo_x::MicroLab::stride*sizeof(float)
        || reinterpret_cast<std::uintptr_t>(memory)%alignof(float)!=0)
      throw std::length_error("full aligned micro vertex buffer required");
    auto session=find_session(id); std::lock_guard<std::mutex> lock(session->mutex);
    session->lab.advance(static_cast<unsigned>(steps));
    return static_cast<jint>(session->lab.vertices(static_cast<float*>(memory),
        static_cast<std::size_t>(bytes)/(mundo_x::MicroLab::stride*sizeof(float))));
  } catch(const std::exception& e) { error(env,e.what()); return -1; }
}
extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_microSnapshot(JNIEnv* env,jclass,jlong id) {
  try {
    auto session=find_session(id); std::lock_guard<std::mutex> lock(session->mutex);
    return env->NewStringUTF(session->lab.json().c_str());
  } catch(const std::exception& e) { error(env,e.what()); return nullptr; }
}
