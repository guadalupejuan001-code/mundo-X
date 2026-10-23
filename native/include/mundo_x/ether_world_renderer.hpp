#pragma once

#include "mundo_x/ether_world.hpp"
#include "mundo_x/ether_renderer.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mundo_x {

// Ether Render X is the original renderer: 64-bit conservative region masks,
// nearest sphere rays, front-to-back distance pruning and exact oracle.
// The procedural floor is a separate bounded spatial-map presentation layer.
// This class does not allocate textures for all 1,048,576 logical world cells.
class EtherWorldRenderer {
 public:
  static constexpr int kWidth=320;
  static constexpr int kHeight=208;
  static constexpr int kMapTiles=45;
  static constexpr int kMapPixel=4;
  static constexpr int kMapWidth=kMapTiles*kMapPixel;
  static constexpr int kCacheRadius=32;
  static constexpr int kCacheSide=2*kCacheRadius+1;
  using Vec3=EtherRenderer::Vec3;
  struct Output {
    std::vector<std::uint32_t> view;
    std::vector<std::uint32_t> map;
    EtherRenderer::Stats renderer;
    double render_ms=0;
    std::size_t visible_cells=0;
    std::size_t reconstructed_cells=0;
  };

  EtherWorldRenderer():renderer_(kWidth,kHeight,24) {
    cache_.resize(static_cast<std::size_t>(kCacheSide)*kCacheSide);
  }
  int tile_size() const noexcept{return tile_;}
  EtherRenderer::TrainingResult optimize() {
    const auto selected=renderer_.train_tiles({16,24,32,48});
    tile_=static_cast<int>(selected.chosen_tile);
    return selected;
  }

  Output render(const EtherWorld& world, bool exact_oracle=false) {
    const auto start=std::chrono::steady_clock::now();
    const auto stats=world.stats();
    const auto centerX=stats.center_x,centerZ=stats.center_z;
    prepare_cache(world,centerX,centerZ);
    populate_objects(world);
    const double centerWorldX=EtherWorld::world_coord(centerX);
    const double centerWorldZ=EtherWorld::world_coord(centerZ);
    const double c=std::cos(stats.angle),s=std::sin(stats.angle);
    eye_={centerWorldX+c*stats.zoom,4.6+stats.zoom*.56,
          centerWorldZ+s*stats.zoom};
    target_={centerWorldX,0,centerWorldZ};
    renderer_.set_camera(eye_,target_,58.0);
    const auto render_stats=renderer_.render(exact_oracle);
    if(exact_oracle && render_stats.exact_mismatches!=0)
      throw std::runtime_error("Ether spatial culling mismatch vs brute oracle");

    Output output;
    output.renderer=render_stats;
    output.view.resize(static_cast<std::size_t>(kWidth)*kHeight);
    output.map.resize(static_cast<std::size_t>(kMapWidth)*kMapWidth);
    compose_floor(world,output.view);
    compose_map(world,output.map);
    output.visible_cells=visible_cells_;
    output.reconstructed_cells=cache_.size();
    output.render_ms=std::chrono::duration<double,std::milli>(
        std::chrono::steady_clock::now()-start).count();
    return output;
  }

 private:
  struct CachedCell {
    std::uint32_t color=0xff080f17u;
    std::uint8_t edit=0;
    bool explored=false;
    float height=0.f;
    float tone=0.f;
    double energy=0.;
  };

  static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
  static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
  static Vec3 mul(Vec3 a,double t){return {a.x*t,a.y*t,a.z*t};}
  static double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
  static Vec3 cross(Vec3 a,Vec3 b){
    return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};
  }
  static Vec3 normalize(Vec3 a){
    const double d=std::sqrt(dot(a,a));
    if(!(d>1e-12))throw std::runtime_error("invalid Ether spatial camera");
    return mul(a,1./d);
  }
  static std::uint32_t argb(unsigned r,unsigned g,unsigned b) {
    return 0xff000000u |
        (std::min(r,255u)<<16) | (std::min(g,255u)<<8) | std::min(b,255u);
  }
  static unsigned byte(double value){
    if(!std::isfinite(value))return 0;
    return static_cast<unsigned>(std::lround(std::clamp(value,0.,255.)));
  }
  static std::uint32_t darken(std::uint32_t original,double factor){
    return argb(byte(((original>>16)&255)*factor),
                byte(((original>>8)&255)*factor),
                byte((original&255)*factor));
  }
  static std::uint32_t blend(std::uint32_t a,std::uint32_t b,double t){
    const double x=std::clamp(t,0.,1.);
    return argb(byte(((a>>16)&255)*(1-x)+((b>>16)&255)*x),
                byte(((a>>8)&255)*(1-x)+((b>>8)&255)*x),
                byte((a&255)*(1-x)+(b&255)*x));
  }
  static std::uint32_t color_cell(const EtherWorld::Cell& cell,
                                  double wave,double quantum_field) {
    if(!cell.revealed)return 0xff111a24u;
    const double h=static_cast<double>(cell.height);
    const double t=static_cast<double>(cell.tone);
    double r=0,g=0,b=0;
    if(t<.16){r=16;g=56;b=90;}          // rivers / ocean
    else if(t<.36){r=28;g=91;b=80;}     // vegetation
    else if(t<.56){r=67;g=95;b=70;}     // grass
    else if(t<.76){r=116;g=105;b=85;}  // sandstone
    else if(t<.92){r=90;g=91;b=119;}   // mineral rock
    else{r=112;g=77;b=164;}             // crystalline matter
    if(cell.edit==1){r=49;g=204;b=214;} // beacon
    if(cell.edit==2){r=236;g=172;b=56;} // grown structure
    if(cell.edit==3){r=8;g=10;b=25;}    // void
    if(cell.edit==4){r=201;g=61;b=124;}// barrier
    const double brightness=std::clamp(.58+.13*h+.12*quantum_field,0.3,1.25);
    const auto base=argb(byte(r*brightness),byte(g*brightness),byte(b*brightness));
    if(wave<=.001)return base;
    return blend(base,0xff99faffu,std::clamp(wave*.85,0.,.85));
  }

  void prepare_cache(const EtherWorld& world,int cx,int cz){
    visible_cells_=0;
    const auto quantum=world.simulated_quantum_field();
    for(int dz=-kCacheRadius;dz<=kCacheRadius;++dz)
      for(int dx=-kCacheRadius;dx<=kCacheRadius;++dx){
        auto& out=cache_[cache_index(dx,dz)];
        const auto cell=world.cell(cx+dx,cz+dz);
        out.explored=cell.revealed;
        if(cell.revealed)++visible_cells_;
        out.edit=cell.edit;out.height=cell.height;out.tone=cell.tone;
        out.energy=cell.revealed?world.wave(cx+dx,cz+dz):0.;
        out.color=color_cell(cell,out.energy,quantum);
      }
  }
  static std::size_t cache_index(int dx,int dz){
    return static_cast<std::size_t>(dz+kCacheRadius)*kCacheSide+(dx+kCacheRadius);
  }
  const CachedCell* cached(int worldX,int worldZ,int centerX,int centerZ) const {
    const int dx=worldX-centerX,dz=worldZ-centerZ;
    if(std::abs(dx)>kCacheRadius||std::abs(dz)>kCacheRadius)return nullptr;
    return &cache_[cache_index(dx,dz)];
  }

  void populate_objects(const EtherWorld& world) {
    renderer_.clear_bodies();
    std::uint32_t index=0;
    // The sphere renderer is deliberately capped to 64 local bodies.
    // Ambient cells still exist in the rule lattice but are not instantiated.
    for(int radius=0;radius<=11&&index<45;++radius)
      for(int dz=-radius;dz<=radius&&index<45;++dz)
        for(int dx=-radius;dx<=radius&&index<45;++dx) {
          if(std::max(std::abs(dx),std::abs(dz))!=radius)continue;
          const auto stats=world.stats();
          const int x=stats.center_x+dx,z=stats.center_z+dz;
          if(!EtherWorld::valid_coord(x,z))continue;
          const auto cell=world.cell(x,z);
          if(!cell.revealed||cell.edit==3)continue;
          if(cell.edit==0 && cell.tone<.78f)continue;
          const bool beacon=cell.edit==1;
          const bool raised=cell.edit==2||cell.edit==4;
          const double h=cell.height;
          EtherRenderer::Body body;
          body.id=index;
          body.center={EtherWorld::world_coord(x),std::max(.25,h*.55),
                       EtherWorld::world_coord(z)};
          body.radius=beacon?.85:raised?.71:(.23+.19*h);
          if(beacon)body.color={{.2,.95,.98}};
          else if(cell.edit==2)body.color={{1.,.71,.19}};
          else if(cell.edit==4)body.color={{.97,.29,.48}};
          else body.color={{.46+.33*cell.tone,.32+.1*cell.tone,.58+.21*cell.tone}};
          renderer_.add_body(body);
          renderer_.set_quantum_material(index,
              world.simulated_quantum_field()*2.-1.,.19);
          ++index;
        }
    for(const auto& probe:world.physics().bodies()) {
      if(index>=64)break;
      const auto& p=probe.position;
      if(std::abs(p.x-EtherWorld::world_coord(world.stats().center_x))>40 ||
         std::abs(p.z-EtherWorld::world_coord(world.stats().center_z))>40)continue;
      renderer_.add_body({index,{p.x,std::max(.45,p.y),p.z},.37,{{1.,.37,.29}}});
      ++index;
    }
  }

  void compose_floor(const EtherWorld& world,std::vector<std::uint32_t>& target){
    const auto st=world.stats();
    Vec3 forward=normalize(sub(target_,eye_));
    Vec3 right=normalize(cross(forward,{0,1,0}));
    Vec3 up=normalize(cross(right,forward));
    const double tanHalf=std::tan(58.0*3.141592653589793/360.0);
    const double aspect=static_cast<double>(kWidth)/kHeight;
    const auto& objects=renderer_.frame();
    for(int y=0;y<kHeight;++y) {
      // EtherRenderer works in a bottom-up coordinate convention. Convert
      // only on presentation so the renderer/oracle results remain unmodified.
      const int sy=kHeight-1-y;
      const double py=(2*(sy+.5)/kHeight-1)*tanHalf;
      for(int x=0;x<kWidth;++x) {
        const double px=(2*(x+.5)/kWidth-1)*tanHalf*aspect;
        const Vec3 ray=normalize(add(forward,add(mul(right,px),mul(up,py))));
        const auto& sphere=objects[static_cast<std::size_t>(sy)*kWidth+x];
        std::uint32_t output;
        const double floorDistance=ray.y<-.0001?-eye_.y/ray.y:
                                    std::numeric_limits<double>::infinity();
        if(floorDistance>0 && floorDistance<120.) {
          const auto hit=add(eye_,mul(ray,floorDistance));
          const int tx=EtherWorld::logical_coord(hit.x),
                    tz=EtherWorld::logical_coord(hit.z);
          const CachedCell* cell=cached(tx,tz,st.center_x,st.center_z);
          if(cell) {
            output=cell->color;
            if(cell->explored){
              // Coordinate lines reveal the structure of the lattice.
              const double cx=hit.x/EtherWorld::kCellSize-.5;
              const double cz=hit.z/EtherWorld::kCellSize-.5;
              const double bx=std::abs(cx-std::round(cx));
              const double bz=std::abs(cz-std::round(cz));
              if(std::min(bx,bz)>.465)output=darken(output,.66);
              const double d=std::hypot(hit.x-target_.x,hit.z-target_.z);
              const double fog=std::clamp((d-26.)/60.,0.,.60);
              output=blend(output,0xff09131cu,fog);
            } else {
              const double gridX=std::abs(std::remainder(hit.x/EtherWorld::kCellSize,1.));
              const double gridZ=std::abs(std::remainder(hit.z/EtherWorld::kCellSize,1.));
              if(std::min(gridX,gridZ)>.47)output=0xff192b32u;
            }
          } else output=0xff061019u;
        } else {
          // Sky is not a physical camera feed: deterministic virtual gradient.
          const double h=std::clamp((ray.y+.1)*.5+.15,0.,1.);
          output=argb(byte(7+20*h),byte(14+26*h),byte(32+37*h));
          const std::uint32_t star=(x*37199u)^(y*7121u)^(static_cast<std::uint32_t>(st.seed)*13u);
          if(ray.y>-.10&&(star%3823u)==7u)output=0xffb7d9d5u;
        }
        if(sphere.body>=0 && sphere.depth<floorDistance)
          output=sphere.rgba;
        target[static_cast<std::size_t>(y)*kWidth+x]=output;
      }
    }
  }

  void compose_map(const EtherWorld& world,std::vector<std::uint32_t>& map) const {
    const auto st=world.stats();
    const int half=kMapTiles/2;
    for(int z=0;z<kMapTiles;++z)
      for(int x=0;x<kMapTiles;++x){
        const auto tile=world.cell(st.center_x+(x-half),st.center_z+(z-half));
        const auto* cachedCell=cached(st.center_x+(x-half),st.center_z+(z-half),
                                      st.center_x,st.center_z);
        std::uint32_t color=cachedCell?cachedCell->color:
            color_cell(tile,world.wave(tile.x,tile.z),st.quantum_field);
        if(!tile.revealed){
          const bool line=((x%5)==0)||((z%5)==0);
          color=line?0xff202c3au:0xff111925u;
        }
        if(tile.x==st.selected_x&&tile.z==st.selected_z)color=0xffffdf77u;
        else if(tile.x==st.center_x&&tile.z==st.center_z)color=0xffe8f5f1u;
        for(const auto& p:world.physics().bodies()){
          const int cx=EtherWorld::logical_coord(p.position.x),
                    cz=EtherWorld::logical_coord(p.position.z);
          if(tile.x==cx&&tile.z==cz){color=0xfffa6556u;break;}
        }
        for(int oy=0;oy<kMapPixel;++oy)
          for(int ox=0;ox<kMapPixel;++ox){
            const std::size_t offset=(static_cast<std::size_t>(z)*kMapPixel+oy)*kMapWidth+
                                     (static_cast<std::size_t>(x)*kMapPixel+ox);
            map[offset]=(ox==0||oy==0)?darken(color,.70):color;
          }
      }
  }

  EtherRenderer renderer_;
  std::vector<CachedCell> cache_;
  Vec3 eye_{},target_{};
  int tile_=24;
  std::size_t visible_cells_=0;
};

} // namespace mundo_x
