#include "mundo_x/ether_render_engine.hpp"
#include "mundo_x/ether_render_spatial.hpp"
#include <cassert>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

// Independent center-ray/plane oracle; does not call the X Render intersection.
int main(){
  using S=mundo_x::EtherRenderSpatial;
  using E=mundo_x::EtherRenderEngine;
  S s(160,90,16);
  s.set_camera({0,0,6},{0,0,0},60.);
  s.add_triangle(3,{-2,-2,0},{2,-2,0},{0,2,0},{{.89,.23,.14}});
  s.add_body({4,{0,0,-3},1.2,{{.19,.45,.76}}});
  s.add_ellipsoid(5,{2,0,-2},{.8,1.6,.7},.4,{{.11,.76,.34}});
  // +0.5 pixel center; horizontal and vertical coordinate inside the triangle.
  const double aspect=160./90.;
  const double nx=2.*(80.5/160.)-1.;
  const double ny=2.*(45.5/90.)-1.;
  const double factor=std::tan(60.*3.14159265358979323846/360.);
  const double rawX=nx*aspect*factor,rawY=ny*factor,rawZ=-1.;
  const double expected=6.*std::sqrt(rawX*rawX+rawY*rawY+rawZ*rawZ);
  const auto center=s.pixel(80,45);
  assert(center.body==3&&std::abs(center.depth-expected)<1e-11);
  std::size_t different=0;
  for(std::size_t y=0;y<90;++y)for(std::size_t x=0;x<160;++x){
    const auto exact=s.pixel(x,y),reference=s.pixel_reference(x,y);
    assert(exact==reference);
    if(exact.body>=0){
      auto reconstructed=s.depth_for_body((unsigned)exact.body,x,y);
      assert(std::abs(exact.depth-reconstructed)<1e-11);
      ++different;
    }
  }
  assert(different>120);
  E e(160,90,16);
  e.camera({0,0,6},{0,0,0},60);
  e.triangle(3,{-2,-2,0},{2,-2,0},{0,2,0},{{.89,.23,.14}});
  e.add({4,{0,0,-3},1.2,{{.19,.45,.76}}});
  e.ellipsoid(5,{2,0,-2},{.8,1.6,.7},.4,{{.11,.76,.34}});
  e.prepare();
  for(std::size_t y=0;y<90;++y)for(std::size_t x=0;x<160;++x)
    assert(e.observe(x,y)==s.pixel_reference(x,y));
  e.capture();
  for(std::size_t y=0;y<90;++y)for(std::size_t x=0;x<160;++x)
    assert(e.observe(x,y)==s.pixel_reference(x,y));
  bool rejected=false;
  try{e.triangle(6,{0,0,0},{1,0,0},{2,0,0},{{1,1,1}});}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  rejected=false;
  try{e.triangle(3,{0,0,0},{1,0,0},{0,1,0},{{1,1,1}});}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  rejected=false;
  try{e.triangle(6,{0,0,0},{1,0,0},{0,std::numeric_limits<double>::quiet_NaN(),0},{{1,1,1}});}
  catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);
  std::cout<<"ETHER_TRIANGLE_EXACT_OK pixels=14400 foreground="<<different<<"\n";
}
