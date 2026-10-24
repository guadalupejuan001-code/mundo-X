#include "mundo_x/ether_surface_buffer.hpp"
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

using Spatial=mundo_x::EtherRenderSpatial;
using Surface=mundo_x::EtherSurfaceBuffer;
static std::uint64_t checks=0;
static void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
static bool throws_stale(Surface& buffer){try{(void)buffer.at(0,0);}catch(const std::logic_error&){return true;}return false;}

static void compare_all(Spatial& renderer){
  Surface buffer(renderer);
  buffer.prepare_lazy();
  need(buffer.mode()==Surface::Mode::lazy,"did not prepare lazy buffer");
  need(buffer.metrics().logical_bytes==0,"lazy observation allocated full frame");
  const auto a=buffer.metrics().prepare_ms;
  (void)a;
  for(std::size_t y=0;y<renderer.height();++y)
    for(std::size_t x=0;x<renderer.width();++x)
      need(buffer.at(x,y)==renderer.pixel_reference(x,y),"lazy surface mismatch");
  need(buffer.metrics().lazy_queries==renderer.width()*renderer.height(),"lazy query accounting");
  buffer.capture_surfaces();
  need(buffer.mode()==Surface::Mode::packed,"did not capture packed buffer");
  need(buffer.metrics().logical_bytes>=6*buffer.metrics().visible_pixels,
       "packed output accounts for less than six bytes per surface");
  for(std::size_t y=0;y<renderer.height();++y)
    for(std::size_t x=0;x<renderer.width();++x)
      need(buffer.at(x,y)==renderer.pixel_reference(x,y),"packed surface mismatch (including depth)");
  need(buffer.metrics().packed_queries==renderer.width()*renderer.height(),"packed query accounting");
  renderer.set_material_gain(0,0.25);
  need(throws_stale(buffer),"material edit did not invalidate cache");
  buffer.capture_surfaces();
  need(buffer.at(renderer.width()/2,renderer.height()/2)==
       renderer.pixel_reference(renderer.width()/2,renderer.height()/2),
       "material edit was not recaptured");
  renderer.set_tile(16);
  need(throws_stale(buffer),"tile edit did not invalidate cache");
  buffer.prepare_lazy();
  renderer.set_camera({0.,0.,8.},{0.,0.,0.},65.);
  need(throws_stale(buffer),"camera edit did not invalidate lazy descriptor");
}

int main(){
  try {
    Spatial empty(127,73,32);
    compare_all(empty);
    Spatial contained(92,56,8);
    contained.set_camera({0,0,0},{0,0,-1},47);
    contained.add_body({0,{0,0,0},3.,{{.4,.7,.9}}});
    compare_all(contained);

    std::mt19937_64 rng(20261024);
    auto random=[&](){return static_cast<double>(rng()%1000000)/1000000.;};
    for(unsigned scene=0;scene<20;++scene){
      Spatial r(160,90,8u<<(scene%4));
      r.set_camera({(random()-.5)*8.,(random()-.5)*5.,1.+random()*12.},
                   {0,0,-3},20.+100*random());
      for(unsigned i=0;i<20;++i){
        r.add_body({i,{(random()-.5)*24.,(random()-.5)*14.,-2.-random()*30.},
                    .2+random()*3.,{{random(),random(),random()}}});
      }
      compare_all(r);
    }
    // Highest supported index 4095 remains a real 16-bit ID.
    Spatial special(96,54,8);
    special.add_body({4095,{0,0,0},1.8,{{.2,.5,.8}}});
    Surface small(special);small.capture_surfaces();
    need(small.at(48,27).body==4095,"highest ID truncated in 16 bit buffer");
    need(small.at(48,27)==special.pixel_reference(48,27),"high ID depth recovery mismatch");
    // A realistic 8K/4096 sparse scene, without allocating 8K full Pixel array.
    Spatial ultra(7680,4320,16);
    ultra.set_camera({0,0,12},{0,0,0},47);
    for(std::uint32_t i=0;i<4096;++i){
      const double x=(static_cast<int>((i*37)%311)-155)*.41;
      const double y=(static_cast<int>((i*31)%211)-105)*.35;
      const double z=-20.-static_cast<double>((i*17)%97);
      ultra.add_body({i,{x,y,z},.08+double(i%11)*.03,{{.3,.6,.8}}});
    }
    Surface packed(ultra);packed.prepare_lazy();
    need(packed.metrics().logical_bytes==0,"8K lazy buffer materialized image");
    for(std::size_t i=0;i<2048;++i){
      const auto x=(i*7919)%7680,y=(i*3467)%4320;
      need(packed.at(x,y)==ultra.pixel_reference(x,y),"8K lazy oracle differs");
    }
    packed.capture_surfaces();
    const auto m=packed.metrics();
    need(m.visible_pixels>0&&m.spans>0,"8K packed buffer empty");
    need(m.logical_bytes < m.full_rgba_bytes,"8K custom buffer did not save payload");
    for(std::size_t i=0;i<2048;++i){
      const auto x=(i*7919)%7680,y=(i*3467)%4320;
      need(packed.at(x,y)==ultra.pixel_reference(x,y),"8K packed ID/color/depth oracle differs");
    }
    std::cout<<"ETHER_SURFACE_BUFFER_PASS checks="<<checks
             <<" 8k_surfaces="<<m.visible_pixels
             <<" compressed_bytes="<<m.logical_bytes
             <<" full_rgba_bytes="<<m.full_rgba_bytes
             <<" spans="<<m.spans<<"\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"ETHER_SURFACE_BUFFER_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
