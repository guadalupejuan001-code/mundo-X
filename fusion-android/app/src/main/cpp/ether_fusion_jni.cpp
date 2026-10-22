#include <jni.h>

#include "mundo_x/ether_renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using mundo_x::EtherRenderer;
constexpr double kPi=3.141592653589793238462643383279502884;

// This is the actual repository-owned Ether Render X native ray/sphere kernel.
// Nuclear positions are EXPLICITLY schematic: a nucleus is a quantum many-body
// wavefunction, not a list of known positions of colored balls.
// This viewer never claims to implement a quantum nuclear Hamiltonian.

void fail(JNIEnv* env,const char* what) {
    jclass klass=env->FindClass("java/lang/IllegalStateException");
    if(klass)env->ThrowNew(klass,what);
}
void validate(int z,int a,const char* label) {
    if(z<0||a<0||z>a||a>320||(a==0&&z!=0)||(a>0&&z==0&&a!=1))
        throw std::invalid_argument(std::string("invalid isotope ")+label);
}
void nucleus(EtherRenderer& renderer, int& nextId, int maxBodies, int z, int a,
             double centerX, double centerY, double centerZ, bool neutronStandalone=false) {
    if(a<=0||maxBodies<=0||nextId>=64) return;
    const int display=std::min({a,maxBodies,64-nextId});
    const int protons=neutronStandalone ? 0 :
          std::clamp(static_cast<int>(std::lround(
               static_cast<double>(display)*static_cast<double>(z)/a)),0,display);
    // Even for light nuclei, these deterministic pseudo-positions are an
    // illustration only and do not predict actual nucleon locations.
    const double radius=display==1 ? 0.0 : 0.18 + 0.035*std::cbrt(static_cast<double>(a));
    const double sphereRadius=a<=6 ? 0.18 : (a<=35 ? 0.135 : 0.105);
    for(int i=0;i<display;i++) {
        const double y=1.0 - 2.0*(i+0.5)/display;
        const double band=std::sqrt(std::max(0.0,1.0-y*y));
        const double azimuth=i*(kPi*(3.0-std::sqrt(5.0)));
        // Mixed radial shells produce a readable 3D volume, without a
        // misleading single fixed "physical" nuclear radius.
        const double shell=0.32+0.68*std::cbrt((i+0.5)/display);
        const double x=std::cos(azimuth)*band*radius*shell;
        const double yy=y*radius*shell;
        const double zz=std::sin(azimuth)*band*radius*shell;
        EtherRenderer::Body body{};
        body.id=static_cast<std::uint32_t>(nextId++);
        body.center={centerX+x,centerY+yy,centerZ+zz};
        body.radius=sphereRadius;
        body.color=(i<protons) ? std::array<double,3>{{1.0,0.43,0.32}} :
                                  std::array<double,3>{{0.27,0.79,0.95}};
        renderer.add_body(body);
    }
}
void build(EtherRenderer& renderer,int za,int aa,int zb,int ab,
           int pza,int paa,int pzb,int pab,double progress,bool showProducts) {
    int id=0;
    const double p=std::clamp(progress,0.,1.);
    if(showProducts && p>=0.68) {
        const double t=(p-0.68)/0.32;
        const double spread=0.38+2.0*t;
        // Two-product primary channels only. Visual trajectories are
        // illustrative and not a computed energy/momentum partition.
        const int countA=std::min(30,paa),countB=std::min(30,pab);
        nucleus(renderer,id,countA,pza,paa,-spread,-0.12*std::sin(t*kPi),0.0,pza==0);
        nucleus(renderer,id,countB,pzb,pab,spread,0.22*std::sin(t*kPi),0.0,pzb==0);
    } else {
        const double approach=std::min(p/0.67,1.0);
        const double near=1.36-1.13*approach;
        // Unknown channels are kept separate after an illustrative approach.
        const double rebound=(p>0.68&&!showProducts)?(p-0.68)*2.2:0.0;
        const double spread=near+rebound;
        const int budgetA=std::min(30,aa),budgetB=std::min(30,ab);
        nucleus(renderer,id,budgetA,za,aa,-spread,0,0);
        nucleus(renderer,id,budgetB,zb,ab,spread,0,0);
    }
}
} // namespace

extern "C" JNIEXPORT jintArray JNICALL
Java_com_mundox_fusion3d_EtherRenderView_renderNative(
        JNIEnv* env,jclass,jint width,jint height,
        jint za,jint aa,jint zb,jint ab,
        jint pza,jint paa,jint pzb,jint pab,
        jfloat progress,jfloat yaw,jfloat pitch,jfloat cameraDistance,
        jboolean showProducts) {
    try {
        if(width<64||width>640||height<64||height>480||
           static_cast<std::size_t>(width)*static_cast<std::size_t>(height)>180000)
            throw std::invalid_argument("render resolution exceeds budget");
        validate(za,aa,"A");
        validate(zb,ab,"B");
        if(za<1||zb<1||aa<za||ab<zb)
            throw std::invalid_argument("reactants require real elements");
        if(showProducts==JNI_TRUE) {
            validate(pza,paa,"product A");
            validate(pzb,pab,"product B");
            if(pza+pzb!=za+zb||paa+pab!=aa+ab||paa<1||pab<1)
                throw std::invalid_argument("products do not conserve Z/A");
        }
        if(!std::isfinite(progress)||!std::isfinite(yaw)||!std::isfinite(pitch)||
           !std::isfinite(cameraDistance))
            throw std::invalid_argument("non-finite render control");
        const double safePitch=std::clamp(static_cast<double>(pitch),-1.32,1.32);
        const double distance=std::clamp(static_cast<double>(cameraDistance),4.,18.);
        EtherRenderer renderer(static_cast<std::size_t>(width),
                               static_cast<std::size_t>(height),32);
        const EtherRenderer::Vec3 eye{
            distance*std::cos(safePitch)*std::sin(yaw),
            distance*std::sin(safePitch),
            distance*std::cos(safePitch)*std::cos(yaw)
        };
        renderer.set_camera(eye,{0.,0.,0.},44.);
        build(renderer,za,aa,zb,ab,pza,paa,pzb,pab,progress,showProducts==JNI_TRUE);
        const auto stats=renderer.render(false);
        if(stats.bodies>60)
            throw std::logic_error("nucleon render budget exceeded");
        jintArray pixels=env->NewIntArray(width*height);
        if(!pixels) return nullptr;
        std::vector<jint> argb;
        argb.reserve(static_cast<std::size_t>(width)*height);
        for(const auto& pixel : renderer.frame())
            argb.push_back(static_cast<jint>(pixel.rgba));
        env->SetIntArrayRegion(pixels,0,width*height,argb.data());
        return pixels;
    } catch(const std::exception& error) {
        fail(env,error.what());
        return nullptr;
    }
}
