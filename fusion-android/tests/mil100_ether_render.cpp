#include "mil100_ether_scene.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Scene=mundo_x::Mil100EtherScene;
using Vec=mundo_x::EtherRenderer::Vec3;
void require(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
void near(double x,double y,double e,const char* why){
  if(!std::isfinite(x)||std::abs(x-y)>e)
    throw std::runtime_error(std::string(why)+" observed="+std::to_string(x)+" expected="+std::to_string(y));
}
Vec sub(Vec a,Vec b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
Vec add(Vec a,Vec b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
Vec mul(Vec a,double f){return {a.x*f,a.y*f,a.z*f};}
double dot(Vec a,Vec b){return a.x*b.x+a.y*b.y+a.z*b.z;}
Vec cross(Vec a,Vec b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
Vec norm(Vec a){const double m=std::sqrt(dot(a,a));return mul(a,1./m);}

void brute_sample(const Scene& scene,double yaw,double pitch,double distance){
  const double tan_half=std::tan(47.*std::acos(-1.)/360.);
  const Vec eye{
      distance*std::cos(pitch)*std::sin(yaw),
      distance*std::sin(pitch),
      distance*std::cos(pitch)*std::cos(yaw)
  };
  const Vec forward=norm(mul(eye,-1.));
  Vec upRef{0,1,0};
  if(std::abs(dot(forward,upRef))>.999)upRef={0,0,1};
  const Vec right=norm(cross(forward,upRef)),up=norm(cross(right,forward));
  const double aspect=static_cast<double>(scene.width())/scene.height();
  const auto& frame=scene.frame();
  const auto& spheres=scene.objects();
  std::size_t checked=0;
  for(std::size_t y=0;y<scene.height();y+=11){
    for(std::size_t x=0;x<scene.width();x+=13){
      const double nx=2.*(x+.5)/scene.width()-1.;
      const double ny=2.*(y+.5)/scene.height()-1.;
      const Vec ray=norm(add(forward,
          add(mul(right,nx*aspect*tan_half),mul(up,ny*tan_half))));
      double best=std::numeric_limits<double>::infinity();
      int nearest=-1;
      for(std::size_t q=0;q<spheres.size();q++){
        const auto& body=spheres[q];
        const Vec c=sub(body.center,eye);
        const double along=dot(c,ray);
        const double h2=body.radius*body.radius-(dot(c,c)-along*along);
        if(h2<0.)continue;
        const double root=std::sqrt(h2);
        double t=along-root;
        if(t<=1e-6)t=along+root;
        if(t<=1e-6||t>=best)continue;
        nearest=static_cast<int>(q);
        best=t;
      }
      const auto& measured=frame[y*scene.width()+x];
      if(nearest!=measured.logical_qubit)
        throw std::runtime_error("100-body composed ray does not match independent brute-force oracle");
      if(nearest>=0)near(measured.depth,best,1e-10,"composite nearest depth");
      checked++;
    }
  }
  require(checked>200,"insufficient independent ray checks");
}

} // namespace
int main(){
  try {
    constexpr std::size_t W=256,H=160;
    Scene scene(W,H);
    const double yaw=.31,pitch=.16,distance=8.8;
    const auto zero=scene.render(yaw,pitch,distance,0.,true);
    require(zero.qubits==100&&zero.visible_objects==100&&zero.passes==2,
            "Render scene must show all 100 logical qubits");
    require(zero.exact_mismatches==0,
            "Original Ether render paths disagree with their brute oracle");
    near(zero.norm,1.,1e-11,"GHZ100 norm");
    near(zero.local_z0,0.,1e-11,"GHZ100 local Z0 is maximally mixed");
    near(zero.distant_zz,1.,1e-11,"GHZ100 Z0Z99 correlation");
    near(zero.global_x,1.,1e-11,"GHZ100 global X coherence");
    require(zero.state_bytes<200000 && zero.components<=Scene::kMaxComponents,
            "100 qubit storage exceeded budget");
    brute_sample(scene,yaw,pitch,distance);

    const auto before_pixels=scene.frame();
    const double shift=std::acos(-1.)/4.;
    const auto phase=scene.render(yaw,pitch,distance,shift,true);
    require(phase.exact_mismatches==0,"non-Clifford render exact oracle failed");
    near(phase.global_x,1./std::sqrt(2.),1e-10,"T-size arbitrary phase coherence");
    require(phase.framebuffer_hash!=zero.framebuffer_hash,
            "MIL Pauli signal did not change actual rendered output");
    require(phase.candidate_tests==zero.candidate_tests,
            "material-only phase change incorrectly modified spatial candidates");
    for(std::size_t i=0;i<scene.frame().size();i++){
      const auto& after=scene.frame()[i];
      const auto& before=before_pixels[i];
      require(before.logical_qubit==after.logical_qubit,
              "material-only MIL phase changed ray visibility");
      require(before.depth==after.depth || 
             (std::isinf(before.depth)&&std::isinf(after.depth)),
              "material-only MIL phase changed nearest 100-sphere depth");
    }
    const auto rotated=scene.render(-.8,.48,9.1,shift,true);
    require(rotated.exact_mismatches==0,"camera orbit exact oracle failed");
    require(rotated.framebuffer_hash!=phase.framebuffer_hash,"orbit did not change true 3D frame");
    brute_sample(scene,-.8,.48,9.1);
    const auto inverse=scene.render(yaw,pitch,distance,0.,true);
    require(inverse.exact_mismatches==0,"inverse phase exact oracle failed");
    near(inverse.global_x,1.,1e-10,"inverse phase did not restore GHZ coherence");
    require(inverse.framebuffer_hash==zero.framebuffer_hash,
            "reversible GHZ quantum shading failed framebuffer restoration");
    bool failed=false;
    try{scene.render(yaw,pitch,distance,9.9,false);}
    catch(const std::exception&){failed=true;}
    require(failed,"100q phase out of bounds accepted");

    std::cout<<std::setprecision(14)
      <<"MIL_100Q_ETHER_RENDER_PASS qubits="<<inverse.qubits
      <<" visible_objects="<<inverse.visible_objects
      <<" passes="<<inverse.passes
      <<" ghz_zz="<<inverse.distant_zz
      <<" original_norm="<<inverse.norm
      <<" T_coherence="<<phase.global_x
      <<" coherent_hash="<<zero.framebuffer_hash
      <<" phased_hash="<<phase.framebuffer_hash
      <<" restored_hash="<<inverse.framebuffer_hash
      <<" mil_components="<<inverse.components
      <<" mil_payload_bytes="<<inverse.state_bytes
      <<" ray_oracle=independent_100_bodies"
      <<" inner_ether_exact_mismatches="<<inverse.exact_mismatches
      <<" cpu_renderer=true"
      <<"\n";
    return 0;
  } catch(const std::exception& e) {
    std::cerr<<"MIL_100Q_ETHER_RENDER_FAIL "<<e.what()<<'\n';
    return 1;
  }
}
