#include <jni.h>

#include "mil100_ether_scene.hpp"

#include <cmath>
#include <cstdint>
#include <exception>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Scene=mundo_x::Mil100EtherScene;

Scene& scene(jlong handle) {
  if(!handle)throw std::invalid_argument("MIL 100q scene session is closed");
  return *reinterpret_cast<Scene*>(static_cast<std::intptr_t>(handle));
}
void fail(JNIEnv* env,const char* message) {
  jclass klass=env->FindClass("java/lang/IllegalStateException");
  if(klass)env->ThrowNew(klass,message);
}
} // namespace

extern "C" JNIEXPORT jlong JNICALL
Java_com_mundox_fusion3d_Mil100EtherView_createNative(
        JNIEnv* env,jclass,jint width,jint height) {
  try {
    if(width<96||height<64||width>640||height>480||
       static_cast<std::size_t>(width)*static_cast<std::size_t>(height)>180000)
      throw std::invalid_argument("100q Ether framebuffer outside budget");
    auto* fresh=new Scene(static_cast<std::size_t>(width),
                          static_cast<std::size_t>(height));
    return static_cast<jlong>(reinterpret_cast<std::intptr_t>(fresh));
  } catch(const std::exception& e) {
    fail(env,e.what());
    return 0;
  }
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_mundox_fusion3d_Mil100EtherView_renderNative(
        JNIEnv* env,jclass,jlong handle,
        jfloat yaw,jfloat pitch,jfloat distance,jfloat phase,
        jboolean assertExact) {
  try {
    auto& model=scene(handle);
    const auto result=model.render(yaw,pitch,distance,phase,
                       assertExact==JNI_TRUE);
    if(result.visible_objects!=100||result.qubits!=100||
       result.components>Scene::kMaxComponents ||
       std::abs(result.norm-1.)>1e-8 ||
       result.exact_mismatches!=0)
      throw std::runtime_error("100q MIL scene violated its declared invariants");
    const auto& frame=model.frame();
    if(frame.size()>180000||frame.size()!=model.width()*model.height())
      throw std::runtime_error("invalid 100q composited framebuffer");
    jintArray pixels=env->NewIntArray(static_cast<jsize>(frame.size()));
    if(!pixels)return nullptr;
    std::vector<jint> output;
    output.reserve(frame.size());
    for(const auto& p:frame)output.push_back(static_cast<jint>(p.argb));
    env->SetIntArrayRegion(pixels,0,
        static_cast<jsize>(output.size()),output.data());
    return pixels;
  } catch(const std::exception& e) {
    fail(env,e.what());return nullptr;
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_fusion3d_Mil100EtherView_statusNative(
        JNIEnv* env,jclass,jlong handle) {
  try {
    const auto& s=scene(handle).summary();
    std::ostringstream out;
    out<<std::setprecision(17)<<'{'
       <<"\"qubits\":"<<s.qubits
       <<",\"visible_objects\":"<<s.visible_objects
       <<",\"passes\":"<<s.passes
       <<",\"components\":"<<s.components
       <<",\"state_bytes\":"<<s.state_bytes
       <<",\"norm\":"<<s.norm
       <<",\"local_z0\":"<<s.local_z0
       <<",\"distant_zz\":"<<s.distant_zz
       <<",\"global_x\":"<<s.global_x
       <<",\"phase\":"<<s.phase
       <<",\"quantum_refresh_ms\":"<<s.quantum_refresh_ms
       <<",\"raster_ms\":"<<s.raster_ms
       <<",\"candidate_tests\":"<<s.candidate_tests
       <<",\"brute_tests\":"<<s.brute_tests
       <<",\"exact_mismatches\":"<<s.exact_mismatches
       <<",\"framebuffer_hash\":\""<<s.framebuffer_hash<<"\""
       <<",\"physical_nuclear_solver\":false"
       <<",\"quantum_hardware\":false"
       <<'}';
    return env->NewStringUTF(out.str().c_str());
  } catch(const std::exception& e) {
    fail(env,e.what());return nullptr;
  }
}

extern "C" JNIEXPORT void JNICALL
Java_com_mundox_fusion3d_Mil100EtherView_destroyNative(
        JNIEnv*,jclass,jlong handle) {
  // Android executes this on the SAME serialized worker as renderNative.
  if(!handle)return;
  delete reinterpret_cast<Scene*>(static_cast<std::intptr_t>(handle));
}
