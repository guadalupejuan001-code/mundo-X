#include "mundo_x/ether_render_engine.hpp"
#include "mundo_x/ether_render_spatial.hpp"
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>
int main(){
 using S=mundo_x::EtherRenderSpatial;
 S ref(160,90,16);
 ref.set_camera({2,4,13},{0,0,0},58);
 ref.add_body({0,{0,-9,0},9.1,{{.1,.25,.15}}});
 ref.add_ellipsoid(1,{0,.3,0},{3,.15,8},.32,{{.33,.33,.36}});
 ref.add_ellipsoid(2,{2,1.7,-4},{1,.7,2.4},1.2,{{.72,.11,.09}});
 ref.add_body({3,{1,4,-8},1.4,{{.12,.25,.6}}});
 for(std::size_t y=0;y<90;y++)for(std::size_t x=0;x<160;x++){
  const auto a=ref.pixel(x,y),b=ref.pixel_reference(x,y);
  assert(a==b);
  if(a.body>=0)assert(std::abs(ref.depth_for_body(unsigned(a.body),x,y)-a.depth)<1e-10);
 }
 mundo_x::EtherRenderEngine engine(160,90,16);
 engine.camera({2,4,13},{0,0,0},58);
 engine.add({0,{0,-9,0},9.1,{{.1,.25,.15}}});
 engine.ellipsoid(1,{0,.3,0},{3,.15,8},.32,{{.33,.33,.36}});
 engine.ellipsoid(2,{2,1.7,-4},{1,.7,2.4},1.2,{{.72,.11,.09}});
 engine.add({3,{1,4,-8},1.4,{{.12,.25,.6}}});
 engine.capture();
 for(std::size_t y=0;y<90;y++)for(std::size_t x=0;x<160;x++)
  assert(engine.observe(x,y)==ref.pixel_reference(x,y));
 bool invalid=false;
 try{engine.ellipsoid(4,{0,0,0},{1,0,1},0.,{{1,1,1}});}
 catch(const std::invalid_argument&){invalid=true;}
 assert(invalid);
 invalid=false;
 try{engine.ellipsoid(1,{0,0,0},{1,1,1},0.,{{1,1,1}});}
 catch(const std::invalid_argument&){invalid=true;}
 assert(invalid);
 std::cout<<"ETHER_ELLIPSOID_EXACT_OK 14400 pixels + packed depth\n";
}
