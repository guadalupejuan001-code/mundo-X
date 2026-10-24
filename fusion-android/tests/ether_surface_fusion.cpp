#include "mundo_x/ether_render_engine.hpp"
#include "mundo_x/ether_renderer.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

using New=mundo_x::EtherRenderEngine;
using Old=mundo_x::EtherRenderer;
static std::uint64_t checks=0;
static void need(bool ok,const char*why){++checks;if(!ok)throw std::runtime_error(why);}
static void scene(double yaw,double pitch){
  constexpr int width=344,height=224;
  Old legacy(width,height,32);New engine(width,height,32);
  const double d=5.8;
  const Old::Vec3 eye{d*std::cos(pitch)*std::sin(yaw),
                      d*std::sin(pitch),d*std::cos(pitch)*std::cos(yaw)};
  legacy.set_camera(eye,{0,0,0},44);
  engine.camera({eye.x,eye.y,eye.z},{0,0,0},44);
  for(unsigned i=0;i<60;++i){
    const double x=(int(i%10)-4.5)*.23;
    const double y=(int(i/10)-2.5)*.23;
    const double z=-.20*int(i%4);
    const double rad=.10+.015*(i%3);
    const std::array<double,3> c=(i%3)?std::array<double,3>{{1,.43,.32}}:
                                          std::array<double,3>{{.27,.79,.95}};
    legacy.add_body({i,{x,y,z},rad,c});
    engine.add({i,{x,y,z},rad,c});
  }
  legacy.render(false);
  engine.prepare();
  std::vector<New::Pixel> output(width*height);
  engine.export_image([&](std::size_t x,std::size_t y,const New::Pixel& p){
    output[y*width+x]=p;
  });
  for(std::size_t i=0;i<output.size();++i){
    const auto& a=output[i];const auto& b=legacy.frame()[i];
    need(a.body==b.body,"new viewer ID differs from legacy");
    need(a.rgba==b.rgba,"new viewer RGBA differs from legacy");
    need((std::isinf(a.depth)&&std::isinf(b.depth))||a.depth==b.depth,
         "new viewer depth differs from legacy");
  }
  need(engine.info().buffer.logical_bytes==0,"lazy frame created a dense buffer");
  engine.capture();
  for(std::size_t i=0;i<output.size();++i){
    const auto p=engine.observe(i%width,i/width);
    need(p==output[i],"packed surface differs from exact lazy observer");
  }
}
int main(){
  try{
    scene(.25,.17);scene(1.5,-.4);scene(-.8,.9);
    std::cout<<"ETHER_FUSION_NEW_ENGINE_PASS checks="<<checks
             <<" backend=EtherRenderEngine buffer=EtherSurfaceBuffer\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"ETHER_FUSION_NEW_ENGINE_FAIL "<<e.what()<<"\n";return 1;
  }
}
