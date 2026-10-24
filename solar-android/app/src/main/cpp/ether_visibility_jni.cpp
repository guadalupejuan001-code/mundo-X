#include "mundo_x/ether_render_gpu_extensions.hpp"

#include <jni.h>
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void reject(JNIEnv* env,const char* type,const std::string& why){
  if(env->ExceptionCheck())return;
  jclass cls=env->FindClass(type);
  if(cls)env->ThrowNew(cls,why.c_str());
}
void require_array(JNIEnv* env,jfloatArray value,jsize size,const char* name){
  if(!value||env->GetArrayLength(value)<size)
    throw std::invalid_argument(std::string("Ether GPU array too small: ")+name);
}
mundo_x::EtherRenderSpatial::Vec3 triple(const float* x){
  return {x[0],x[1],x[2]};
}
} // namespace

// Shared native scene planner. Renderer geometry and masks are owned by
// EtherRenderSpatial. This JNI adapter NEVER produces an 8K CPU framebuffer.
extern "C" JNIEXPORT void JNICALL
Java_com_mundox_solar_EtherVisibilityNative_plan(
    JNIEnv* env,jclass,
    jint width,jint height,jint tile,jfloat tanHalf,
    jfloatArray jforward,jfloatArray jright,jfloatArray jup,
    jfloatArray jbody,jfloatArray jappearance,jfloatArray jatmosphere,
    jint count,jintArray jmasks) {
  try {
    if(width<1||height<1||width>8192||height>8192||
       tile<16||tile>1024||count<0||count>24||
       !std::isfinite(tanHalf)||tanHalf<=0.f)
      throw std::invalid_argument("invalid Ether GPU image/camera parameters");
    const std::size_t tx=(static_cast<std::size_t>(width)+tile-1)/tile;
    const std::size_t ty=(static_cast<std::size_t>(height)+tile-1)/tile;
    if(tx*ty>static_cast<std::size_t>(std::numeric_limits<jsize>::max()))
      throw std::length_error("Ether GPU tile grid too large");
    if(!jmasks||env->GetArrayLength(jmasks)!=static_cast<jsize>(tx*ty))
      throw std::invalid_argument("Ether GPU tile mask dimensions differ");
    require_array(env,jforward,3,"forward");
    require_array(env,jright,3,"right");
    require_array(env,jup,3,"up");
    require_array(env,jbody,count*4,"bodies");
    require_array(env,jappearance,count*4,"appearance");
    require_array(env,jatmosphere,count*4,"atmosphere");

    float forward[3],right[3],up[3];
    env->GetFloatArrayRegion(jforward,0,3,forward);
    env->GetFloatArrayRegion(jright,0,3,right);
    env->GetFloatArrayRegion(jup,0,3,up);
    if(env->ExceptionCheck())return;
    std::vector<float> bodies(static_cast<std::size_t>(count)*4);
    std::vector<float> appearance(bodies.size()),atmosphere(bodies.size());
    if(count){
      env->GetFloatArrayRegion(jbody,0,count*4,bodies.data());
      env->GetFloatArrayRegion(jappearance,0,count*4,appearance.data());
      env->GetFloatArrayRegion(jatmosphere,0,count*4,atmosphere.data());
      if(env->ExceptionCheck())return;
    }
    const double fov=2.*std::atan(static_cast<double>(tanHalf))*180./std::acos(-1.);
    mundo_x::EtherRenderSpatial renderer(
        static_cast<std::size_t>(width),static_cast<std::size_t>(height),
        static_cast<std::size_t>(tile));
    renderer.set_camera_basis({0.,0.,0.},triple(forward),triple(right),triple(up),fov);
    for(int id=0;id<count;++id){
      const std::size_t p=static_cast<std::size_t>(id)*4;
      const auto radius=bodies[p+3];
      if(!std::isfinite(radius)||radius<=0.f)continue;
      if(!std::isfinite(bodies[p])||!std::isfinite(bodies[p+1])||!std::isfinite(bodies[p+2]))
        throw std::invalid_argument("invalid Ether GPU body position");
      // Spheres, rings and atmosphere share the same conservative envelope.
      // Preserve all eclipse/corona work independent of camera visibility.
      const double ring=std::isfinite(appearance[p+3])?appearance[p+3]:1.;
      const double atmosphereTop=std::isfinite(atmosphere[p])?1.+atmosphere[p]:1.;
      const double extent=std::max({1.,ring,atmosphereTop});
      renderer.add_body({static_cast<std::uint32_t>(id),
        {bodies[p],bodies[p+1],bodies[p+2]},radius*extent,
        {{1.,1.,1.}}});
    }
    renderer.plan();
    // Dilated 1 tile: preserves subpixel/float edge guard-band.
    const auto masks=renderer.tile_masks32(1);
    // Only storage type differs: jint and uint32_t are both 32-bit on Android.
    std::vector<jint> out(masks.begin(),masks.end());
    env->SetIntArrayRegion(jmasks,0,static_cast<jsize>(out.size()),out.data());
  }catch(const std::exception& e){
    reject(env,"java/lang/IllegalArgumentException",e.what());
  }
}
