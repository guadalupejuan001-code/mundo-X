#include "mundo_x/ether_render_gpu_extensions.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

using Render=mundo_x::EtherRenderSpatial;
using Vec3=Render::Vec3;
static std::uint64_t checks=0;
static void require(bool b,const char* why){++checks;if(!b)throw std::runtime_error(why);}
static Vec3 sub(Vec3 x,Vec3 y){return{x.x-y.x,x.y-y.y,x.z-y.z};}
static Vec3 mul(Vec3 x,double t){return{x.x*t,x.y*t,x.z*t};}
static Vec3 add(Vec3 x,Vec3 y){return{x.x+y.x,x.y+y.y,x.z+y.z};}
static double dot(Vec3 x,Vec3 y){return x.x*y.x+x.y*y.y+x.z*y.z;}
static Vec3 norm(Vec3 x){return mul(x,1./std::sqrt(dot(x,x)));}
static bool ray_hits(Vec3 c,double r,Vec3 rd){
  const double along=dot(c,rd);
  const double discriminant=r*r-(dot(c,c)-along*along);
  if(discriminant<0.)return false;
  const auto root=std::sqrt(discriminant);
  double depth=along-root;
  if(depth<=1e-6)depth=along+root;
  return depth>1e-6;
}
static void verify(Render& r,const std::vector<Render::Body>& bodies,
                   int width,int height,int tile,Vec3 forward,Vec3 right,Vec3 up,double fov,
                   unsigned step=1){
  const auto masks=r.tile_masks32(1);
  require(masks.size()==r.tiles_x()*r.tiles_y(),"GPU mask grid size incorrect");
  const auto th=std::tan(fov*std::acos(-1.)/360.);
  const double aspect=double(width)/height;
  for(int y=0;y<height;y+=step)for(int x=0;x<width;x+=step){
    const double nx=2.*(x+.5)/width-1.,ny=2.*(y+.5)/height-1.;
    const auto rd=norm(add(forward,add(mul(right,nx*th*aspect),mul(up,ny*th))));
    const auto mask=masks[(y/tile)*r.tiles_x()+x/tile];
    for(const auto& b:bodies){
      if(ray_hits(b.center,b.radius,rd))
        require((mask&(std::uint32_t{1}<<b.id))!=0,"GPU culling omitted ray/object hit");
    }
  }
}
int main(){
  try{
    std::mt19937_64 rng(20261024);
    const auto unit=[&](){return double(rng()%100000)/100000.;};
    for(int scene=0;scene<64;++scene){
      const int width=129,height=73,tile=scene%2==0?16:32;
      const double fov=20.+120.*unit();
      Render r(width,height,tile);
      const Vec3 forward{0.,0.,-1.},right{1.,0.,0.},up{0.,1.,0.};
      r.set_camera_basis({0,0,0},forward,right,up,fov);
      std::vector<Render::Body> bodies;
      for(int i=0;i<24;++i){
        const double dist=.05+unit()*40.;
        const Vec3 center{(unit()-.5)*60.,(unit()-.5)*40.,-dist};
        const double base=.0001+unit()*4.,extent=1.+unit()*3.;
        bodies.push_back({static_cast<std::uint32_t>(i),center,base*extent,{{.5,.5,.5}}});
        r.add_body(bodies.back());
      }
      r.plan();
      verify(r,bodies,width,height,tile,forward,right,up,fov);
    }
    // A camera inside atmosphere/ring always remains a candidate.
    {
      Render inside(160,90,32);inside.set_camera_basis(
          {0,0,0},{0,0,-1},{1,0,0},{0,1,0},47.);
      inside.add_body({0,{0,0,0},5.,{{1,1,1}}});
      const auto bits=inside.tile_masks32(1);
      require(std::all_of(bits.begin(),bits.end(),[](auto x){return (x&1)!=0;}),
              "camera-inside atmosphere was culled");
    }
    // 8K scale, not an 8K per-pixel CPU image.
    {
      Render uhd(7680,4320,128);
      uhd.set_camera_basis({0,0,0},{0,0,-1},{1,0,0},{0,1,0},47.);
      std::vector<Render::Body> bodies;
      for(unsigned i=0;i<24;++i){
        Render::Body b{i,{(double(i)-12.)*.8,(double(i%7)-3.)*.6,-20.-(i%8)*2.},
            .1+.05*(i%5),{{1,1,1}}};
        uhd.add_body(b);bodies.push_back(b);
      }
      uhd.plan();
      verify(uhd,bodies,7680,4320,128,{0,0,-1},{1,0,0},{0,1,0},47.,17);
      const auto masks=uhd.tile_masks32(1);
      require(masks.size()==2040,"8K must allocate 2040 128px masks");
      unsigned nonempty=0;
      for(const auto m:masks)if(m)++nonempty;
      require(nonempty>0&&nonempty<masks.size(),"8K indexing did not cull regions");
    }
    // Enforce the 32-bit bound.
    {
      Render outside(64,64,16);
      outside.add_body({32,{0,0,0},1.0,{{1,1,1}}});
      bool threw=false;try{(void)outside.tile_masks32(1);}
      catch(const std::invalid_argument&){threw=true;}
      require(threw,"32-bit mask silently truncated a high object ID");
    }
    std::cout<<"ETHER_GPU_VISIBILITY_PASS checks="<<checks<<" randomized_scenes=64 8k_masks=2040\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"ETHER_GPU_VISIBILITY_FAIL "<<e.what()<<"\n";return 1;
  }
}
