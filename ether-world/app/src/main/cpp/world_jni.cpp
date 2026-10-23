#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <exception>
#include <vector>
#include "mundo_x/ether_renderer.hpp"

namespace {
constexpr int W=240,H=144;
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_mundox_etherworld_WorldActivity_nativeRender(
    JNIEnv* env,jclass,jfloatArray objects,jfloat cameraX,jfloat cameraZ,jfloat yaw) {
  if(!objects) return nullptr;
  jsize n=env->GetArrayLength(objects);
  if(n<0 || n%6 || n>64*6 || !std::isfinite(cameraX) ||
     !std::isfinite(cameraZ) || !std::isfinite(yaw)) return nullptr;
  std::vector<jfloat> values(static_cast<size_t>(n));
  if(n)env->GetFloatArrayRegion(objects,0,n,values.data());
  try {
    mundo_x::EtherRenderer renderer(W,H,16);
    renderer.set_camera(
      {cameraX,3.3,cameraZ},
      {cameraX+std::sin(yaw)*7.,0.4,cameraZ-std::cos(yaw)*7.},
      64.);
    for(jsize i=0;i<n;i+=6) {
      bool valid=true;
      for(int j=0;j<6;j++)if(!std::isfinite(values[i+j]))valid=false;
      if(!valid)continue;
      const double radius=std::clamp(static_cast<double>(values[i+3]),0.12,2.5);
      const double hue=std::clamp(static_cast<double>(values[i+4]),0.,1.);
      const double lum=std::clamp(static_cast<double>(values[i+5]),0.1,1.);
      renderer.add_body({static_cast<std::uint32_t>(i/6),
        {values[i],values[i+1],values[i+2]},radius,
        {{0.15+0.75*hue,0.18+0.65*lum,0.45+0.5*(1-hue)}}});
    }
    renderer.render();
    jintArray output=env->NewIntArray(W*H);
    if(!output)return nullptr;
    std::vector<jint> pixels(W*H);
    const auto& frame=renderer.frame();
    for(size_t i=0;i<frame.size();++i)
      pixels[i]=static_cast<jint>(frame[i].rgba);
    env->SetIntArrayRegion(output,0,W*H,pixels.data());
    return output;
  }catch(const std::exception& e){
    jclass cls=env->FindClass("java/lang/IllegalStateException");
    if(cls)env->ThrowNew(cls,e.what());
    return nullptr;
  }
}
