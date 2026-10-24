#include "mundo_x/ether_render_spatial.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>

using Render=mundo_x::EtherRenderSpatial;
static std::uint64_t checks=0;
static void require(bool b,const char* reason){++checks;if(!b)throw std::runtime_error(reason);}

static void compare(Render& r,std::size_t x,std::size_t y){
  auto fast=r.pixel(x,y),slow=r.pixel_reference(x,y);
  require(fast==slow,"spatial candidate differs from exhaustive oracle: id/depth/RGBA");
}

int main(){
  try {
    // Exact dimensions and camera-dependent fallback, including camera inside sphere.
    Render basic(129,71,16);
    basic.add_body({0,{0,0,0},1.4,{{.2,.6,1.}}});
    basic.add_body({1,{3.4,.7,-2.},.9,{{1.,.45,.1}}});
    basic.add_body({2,{-4.,-1.,-8.},1.8,{{.55,.8,.3}}});
    basic.plan();
    for(std::size_t y=0;y<71;++y)for(std::size_t x=0;x<129;++x)compare(basic,x,y);
    basic.set_camera({0,0,0},{1.,0,0},75);
    for(std::size_t y=0;y<71;++y)for(std::size_t x=0;x<129;++x)compare(basic,x,y);
    // Degenerate overlapping spheres and ID tie breaking.
    Render overlap(120,60,8);
    overlap.add_body({9,{0,0,0},2.,{{1.,.5,.1}}});
    overlap.add_body({2,{0,0,0},2.,{{.1,1.,.5}}});
    for(std::size_t y=0;y<60;++y)for(std::size_t x=0;x<120;++x)compare(overlap,x,y);
    require(overlap.pixel(60,30).body==2,"equal depth must favor smaller body id");

    std::mt19937_64 rng(20261024);
    auto unit=[&]{return static_cast<double>(rng()%1000000)/1000000.;};
    for(unsigned scene=0;scene<97;++scene){
      Render r(320,180,8u<<(scene%4));
      const double near=.8+unit()*10.;
      r.set_camera({(unit()-.5)*8.,(unit()-.5)*6.,near},
                   {(unit()-.5)*5.,(unit()-.5)*4.,-4.},20.+unit()*100.);
      const unsigned bodies=1+static_cast<unsigned>(rng()%64);
      for(unsigned i=0;i<bodies;++i){
        Render::Body b;
        b.id=i;
        b.center={(unit()-.5)*28.,(unit()-.5)*18.,-2.-unit()*26.};
        b.radius=.02+unit()*4.;b.color={{unit(),unit(),unit()}};
        r.add_body(b);
      }
      for(std::size_t y=0;y<180;++y)for(std::size_t x=0;x<320;++x){
        compare(r,x,y);
        if(!r.may_contain_surface(x,y))
          require(r.pixel_reference(x,y).body<0,"spatial footprint omitted visible sphere");
      }
      std::uint64_t emitted=0;
      const auto sparse=r.render_sparse_surfaces([&](std::size_t x,std::size_t y,const Render::Pixel& p){
        require(p==r.pixel_reference(x,y),"sparse stream differed from reference");
        ++emitted;
      });
      require(emitted==sparse.sparse_surfaces,"sparse output accounting error");
    }

    // 4096 distinct objects beyond the 64-mask limit; check real 8K coordinates.
    Render ultra(7680,4320,64);
    ultra.set_camera({0.,0.,12.},{0.,0.,0.},47.);
    for(std::uint32_t i=0;i<4096;++i){
      const double x=(static_cast<int>((i*37)%311)-155)*.41;
      const double y=(static_cast<int>((i*31)%211)-105)*.35;
      const double z=-20.-static_cast<double>((i*17)%97);
      ultra.add_body({i,{x,y,z},.08+static_cast<double>(i%11)*.03,{{.3,.6,.8}}});
    }
    ultra.plan();
    for(std::size_t i=0;i<2048;++i){
      compare(ultra,(i*7919)%7680,(i*3467)%4320);
    }
    require(ultra.stats().bodies==4096,"4096 bodies not accepted");
    require(ultra.stats().tile_references>0,"no nonempty 8K tiles");
    std::cout<<"SPATIAL_EXACT_PASS checks="<<checks
             <<" scenes=97 dimensions=7680x4320 bodies=4096"
             <<" tile_refs="<<ultra.stats().tile_references<<"\n";
    return 0;
  } catch(const std::exception& e){std::cerr<<"SPATIAL_EXACT_FAIL "<<e.what()<<"\n";return 1;}
}
