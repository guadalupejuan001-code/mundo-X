#pragma once

// Exact, streamed CPU image observer for large sphere scenes. It does not
// allocate one Pixel per output pixel and does not mutate MIL/Ether state.
// Designed as an optional large-scene path; EtherRenderer's public ABI and
// existing 64-body/XASM contracts remain unchanged.

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mundo_x {

class EtherRenderSpatial {
 public:
  struct Vec3 { double x=0.,y=0.,z=0.; };
  struct Body {
    std::uint32_t id=0;
    Vec3 center{};
    double radius=1.;
    std::array<double,3> color{{1.,1.,1.}};
  };
  struct Pixel {
    std::int32_t body=-1;
    double depth=std::numeric_limits<double>::infinity();
    std::uint32_t rgba=0xff000000u;
    bool operator==(const Pixel& b) const {
      return body==b.body && rgba==b.rgba &&
        ((std::isinf(depth)&&std::isinf(b.depth)) || depth==b.depth);
    }
  };
  struct Stats {
    std::size_t width=0,height=0,bodies=0,tile=0,tiles=0,nonempty_tiles=0;
    std::uint64_t tile_references=0,ray_queries=0,intersection_tests=0;
    std::uint64_t oracle_tests=0,hash=1469598103934665603ULL;
    std::uint64_t sparse_queries=0,sparse_surfaces=0;
    double sparse_ms=0.;
    double planning_ms=0.,render_ms=0.;
  };

  explicit EtherRenderSpatial(std::size_t width,std::size_t height,std::size_t tile=32)
      : width_(width),height_(height),tile_(tile) {
    if(!width||!height||width>8192||height>8192)
      throw std::invalid_argument("spatial render dimensions must be 1..8192");
    if(tile<8||tile>1024)throw std::invalid_argument("spatial tile must be 8..1024");
    resize_tiles();
    set_camera({0.,0.,10.},{0.,0.,0.},47.);
  }

  void set_camera(Vec3 eye,Vec3 target,double fov) {
    if(!finite(eye)||!finite(target)||!std::isfinite(fov)||fov<=1.||fov>=170.)
      throw std::invalid_argument("invalid spatial camera");
    const Vec3 dir=sub(target,eye);
    if(!(length(dir)>1e-12))throw std::invalid_argument("spatial camera eye equals target");
    eye_=eye;forward_=normalize(dir);
    Vec3 world_up{0.,1.,0.};
    if(std::abs(dot(forward_,world_up))>.999)world_up={0.,0.,1.};
    right_=normalize(cross(forward_,world_up));
    up_=normalize(cross(right_,forward_));
    tan_half_=std::tan(fov*std::acos(-1.)/360.);
    aspect_=static_cast<double>(width_)/static_cast<double>(height_);
    dirty_=true;
  }

  void add_body(const Body& b) {
    if(bodies_.size()>=4096||b.id>=4096)throw std::length_error("spatial render supports 4096 bodies/IDs");
    if(!finite(b.center)||!std::isfinite(b.radius)||!(b.radius>0.))
      throw std::invalid_argument("invalid spatial geometry");
    for(const auto c:b.color)if(!std::isfinite(c)||c<0.||c>1.)
      throw std::invalid_argument("invalid spatial color");
    for(const auto& old:bodies_)if(old.id==b.id)
      throw std::invalid_argument("duplicate spatial body ID");
    bodies_.push_back(b);dirty_=true;
  }

  void clear_bodies(){bodies_.clear();gains_.fill(1.);dirty_=true;}
  void set_material_gain(std::uint32_t id,double gain) {
    if(id>=4096||!std::isfinite(gain)||gain<0.||gain>2.)
      throw std::invalid_argument("invalid spatial material gain");
    gains_[id]=gain;
  }
  void set_tile(std::size_t tile) {
    if(tile<8||tile>1024)throw std::invalid_argument("spatial tile must be 8..1024");
    tile_=tile;resize_tiles();dirty_=true;
  }

  // Planning is fully conservative: the frustum bound is enlarged by one
  // pixel in NDC, then tile lists are sorted by a ray-independent lower bound.
  // A candidate may be superfluous but no positive ray/sphere hit is omitted.
  void plan() {
    const auto begin=std::chrono::steady_clock::now();
    std::sort(bodies_.begin(),bodies_.end(),[](const Body& a,const Body& b){return a.id<b.id;});
    for(auto& bin:bins_)bin.clear();
    row_runs_.assign(height_,{});
    offsets_.resize(bodies_.size());squared_.resize(bodies_.size());
    radius2_.resize(bodies_.size());near_.resize(bodies_.size());
    for(std::size_t i=0;i<bodies_.size();++i){
      const auto& b=bodies_[i];
      const Vec3 c=sub(b.center,eye_);offsets_[i]=c;
      squared_[i]=dot(c,c);radius2_[i]=b.radius*b.radius;
      near_[i]=std::max(0.,length(c)-b.radius);
      project(i);
    }
    for(auto& runs:row_runs_){
      std::sort(runs.begin(),runs.end(),[](const Run& a,const Run& b){
        return a.x0<b.x0||(a.x0==b.x0&&a.x1<b.x1);
      });
      std::size_t dst=0;
      for(const auto& run:runs){
        if(dst&&run.x0<=runs[dst-1].x1+1)
          runs[dst-1].x1=std::max(runs[dst-1].x1,run.x1);
        else runs[dst++]=run;
      }
      runs.resize(dst);
    }
    stats_=Stats{};stats_.width=width_;stats_.height=height_;
    stats_.bodies=bodies_.size();stats_.tile=tile_;stats_.tiles=bins_.size();
    for(auto& bin:bins_){
      std::sort(bin.begin(),bin.end(),[&](std::uint32_t a,std::uint32_t b){
        if(near_[a]!=near_[b])return near_[a]<near_[b];
        return bodies_[a].id<bodies_[b].id;
      });
      stats_.tile_references+=bin.size();
      if(!bin.empty())++stats_.nonempty_tiles;
    }
    stats_.planning_ms=ms_since(begin);dirty_=false;
  }

  Pixel pixel(std::size_t x,std::size_t y) {
    bounds(x,y);if(dirty_)plan();
    std::uint64_t ignored=0;
    return trace(ray_for(x,y),bins_[(y/tile_)*tiles_x_+(x/tile_)],&ignored);
  }

  Pixel pixel_reference(std::size_t x,std::size_t y) {
    bounds(x,y);if(dirty_)plan();
    const auto ray=ray_for(x,y);
    Pixel best;const Body* hit=nullptr;
    for(std::size_t i=0;i<bodies_.size();++i){
      const auto& b=bodies_[i];
      const double along=dot(offsets_[i],ray);
      const double h2=radius2_[i]-(squared_[i]-along*along);
      if(h2<0.)continue;
      const double root=std::sqrt(h2);
      double depth=along-root;if(depth<=1e-6)depth=along+root;
      if(depth<=1e-6||depth>=best.depth)continue;
      best.body=static_cast<std::int32_t>(b.id);best.depth=depth;hit=&b;
    }
    return shade(best,hit,ray);
  }

  // Stream in raster order so caller may send strips to display/encoder.
  // Frame hash includes ID, RGBA and exact depth bits (also for background).
  // Memory for the image itself is O(tiles + candidate references + row footprints), not O(WH).
  template<class Observer>
  Stats render(Observer&& observe) {
    if(dirty_)plan();
    stats_.ray_queries=0;stats_.intersection_tests=0;
    stats_.oracle_tests=static_cast<std::uint64_t>(width_)*height_*bodies_.size();
    stats_.hash=1469598103934665603ULL;
    const auto begin=std::chrono::steady_clock::now();
    for(std::size_t y=0;y<height_;++y){
      const auto ty=(y/tile_)*tiles_x_;
      for(std::size_t tx=0;tx<tiles_x_;++tx){
        const auto& bin=bins_[ty+tx];
        const auto x_end=std::min(width_,(tx+1)*tile_);
        for(std::size_t x=tx*tile_;x<x_end;++x){
          const auto ray=ray_for(x,y);
          const auto p=trace(ray,bin,&stats_.intersection_tests);
          ++stats_.ray_queries;hash(p);observe(x,y,p);
        }
      }
    }
    stats_.render_ms=ms_since(begin);
    return stats_;
  }
  Stats render(){return render([](std::size_t,std::size_t,const Pixel&){});}
  // Structural observation: iterate only the pixels inside conservative
  // screen-space footprints. Background remains an implicit analytic rule.
  // This is NOT a presented full frame or display FPS measurement.
  template<class Observer>
  Stats render_sparse_surfaces(Observer&& observe){
    if(dirty_)plan();
    stats_.sparse_queries=stats_.sparse_surfaces=0;
    const auto begin=std::chrono::steady_clock::now();
    for(std::size_t y=0;y<height_;++y){
      const auto ty=(y/tile_)*tiles_x_;
      for(const auto& run:row_runs_[y])for(std::size_t x=run.x0;x<=run.x1;++x){
        const auto p=trace(ray_for(x,y),bins_[ty+x/tile_],nullptr);
        ++stats_.sparse_queries;
        if(p.body>=0){++stats_.sparse_surfaces;observe(x,y,p);}
      }
    }
    stats_.sparse_ms=ms_since(begin);
    return stats_;
  }
  Stats render_sparse_surfaces(){return render_sparse_surfaces([](std::size_t,std::size_t,const Pixel&){});}

  Pixel background(std::size_t x,std::size_t y)const {
    bounds(x,y);return shade(Pixel{},nullptr,ray_for(x,y));
  }
  bool may_contain_surface(std::size_t x,std::size_t y){
    bounds(x,y);if(dirty_)plan();
    for(const auto& run:row_runs_[y])if(x>=run.x0&&x<=run.x1)return true;
    return false;
  }
  const Stats& stats()const noexcept{return stats_;}
  const std::vector<Body>& bodies()const noexcept{return bodies_;}

 private:
  struct Run {std::size_t x0=0,x1=0;};
  static bool finite(Vec3 a){return std::isfinite(a.x)&&std::isfinite(a.y)&&std::isfinite(a.z);}
  static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
  static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
  static Vec3 mul(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
  static double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
  static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
  static double length(Vec3 a){return std::sqrt(dot(a,a));}
  static Vec3 normalize(Vec3 a){const double n=length(a);if(!(n>1e-15))throw std::invalid_argument("zero render vector");return mul(a,1./n);}
  static double ms_since(std::chrono::steady_clock::time_point t){
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-t).count();
  }
  void resize_tiles(){
    tiles_x_=(width_+tile_-1)/tile_;tiles_y_=(height_+tile_-1)/tile_;
    bins_.resize(tiles_x_*tiles_y_);
  }
  void bounds(std::size_t x,std::size_t y)const{
    if(x>=width_||y>=height_)throw std::out_of_range("spatial pixel outside image");
  }
  void fill(std::uint32_t i,int x0,int x1,int y0,int y1){
    x0=std::max(x0,0);y0=std::max(y0,0);
    x1=std::min(x1,static_cast<int>(tiles_x_)-1);
    y1=std::min(y1,static_cast<int>(tiles_y_)-1);
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)
      bins_[static_cast<std::size_t>(y)*tiles_x_+x].push_back(i);
  }
  void register_rectangle(int x0,int x1,int y0,int y1){
    x0=std::max(x0,0);y0=std::max(y0,0);
    x1=std::min(x1,static_cast<int>(width_)-1);
    y1=std::min(y1,static_cast<int>(height_)-1);
    if(x0>x1||y0>y1)return;
    for(int y=y0;y<=y1;++y)
      row_runs_[y].push_back({static_cast<std::size_t>(x0),static_cast<std::size_t>(x1)});
  }
  void project(std::uint32_t index){
    const auto& b=bodies_[index];const auto c=offsets_[index];
    const double x=dot(c,right_),y=dot(c,up_),z=dot(c,forward_),r=b.radius;
    constexpr double kNear=1e-6;
    if(z+r<=kNear)return;
    if(z<=r+kNear){
      fill(index,0,static_cast<int>(tiles_x_)-1,0,static_cast<int>(tiles_y_)-1);
      register_rectangle(0,static_cast<int>(width_)-1,0,static_cast<int>(height_)-1);
      return;
    }
    const double zn=z-r,zf=z+r;
    const double ax=(x-r)/zn,bx=(x-r)/zf,cx=(x+r)/zn,dx=(x+r)/zf;
    const double ay=(y-r)/zn,by=(y-r)/zf,cy=(y+r)/zn,dy=(y+r)/zf;
    const double mx=2./static_cast<double>(width_);
    const double my=2./static_cast<double>(height_);
    const double sx=tan_half_*aspect_,sy=tan_half_;
    double left=std::min({ax,bx,cx,dx})/sx-mx;
    double right=std::max({ax,bx,cx,dx})/sx+mx;
    double bottom=std::min({ay,by,cy,dy})/sy-my;
    double top=std::max({ay,by,cy,dy})/sy+my;
    if(right<-1.||left>1.||top<-1.||bottom>1.)return;
    left=std::max(-1.,left);right=std::min(1.,right);
    bottom=std::max(-1.,bottom);top=std::min(1.,top);
    // Conservatively cover every tile intersected by the projected screen box.
    const auto px0=(left*.5+.5)*static_cast<double>(width_);
    const auto px1=(right*.5+.5)*static_cast<double>(width_);
    const auto py0=(bottom*.5+.5)*static_cast<double>(height_);
    const auto py1=(top*.5+.5)*static_cast<double>(height_);
    register_rectangle(static_cast<int>(std::floor(px0))-1,
                       static_cast<int>(std::ceil(px1))+1,
                       static_cast<int>(std::floor(py0))-1,
                       static_cast<int>(std::ceil(py1))+1);
    fill(index,static_cast<int>(std::floor(px0/tile_)),
               static_cast<int>(std::floor(px1/tile_)),
               static_cast<int>(std::floor(py0/tile_)),
               static_cast<int>(std::floor(py1/tile_)));
  }
  Vec3 ray_for(std::size_t x,std::size_t y)const{
    const double nx=2.*(static_cast<double>(x)+.5)/static_cast<double>(width_)-1.;
    const double ny=2.*(static_cast<double>(y)+.5)/static_cast<double>(height_)-1.;
    return normalize(add(forward_,add(mul(right_,nx*aspect_*tan_half_),mul(up_,ny*tan_half_))));
  }
  Pixel shade(Pixel best,const Body* hit,const Vec3& ray)const{
    if(!hit){
      const double v=std::max(0.,std::min(1.,.025+.08*(ray.y*.5+.5)));
      const auto c=static_cast<std::uint32_t>(std::lround(v*255.));
      best.rgba=(255u<<24)|(c<<16)|(c<<8)|(std::min<std::uint32_t>(255,c+5));
      return best;
    }
    const auto point=add(eye_,mul(ray,best.depth));
    const auto normal=normalize(sub(point,hit->center));
    static const auto light=normalize(Vec3{.37,.74,.56});
    const double l=.12+.88*std::max(0.,dot(normal,light));
    const double gain=gains_[hit->id];
    auto channel=[&](double c){return static_cast<std::uint32_t>(
      std::lround(std::max(0.,std::min(1.,c*l*gain))*255.));};
    const auto r=channel(hit->color[0]),g=channel(hit->color[1]),b=channel(hit->color[2]);
    best.rgba=(255u<<24)|(r<<16)|(g<<8)|b;
    return best;
  }
  Pixel trace(Vec3 ray,const std::vector<std::uint32_t>& bin,std::uint64_t* tests)const{
    Pixel best;const Body* hit=nullptr;
    for(const auto i:bin){
      if(best.body>=0&&near_[i]>best.depth)break;
      const auto& b=bodies_[i];
      if(tests)++*tests;
      const double along=dot(offsets_[i],ray);
      const double h2=radius2_[i]-(squared_[i]-along*along);
      if(h2<0.)continue;
      const double root=std::sqrt(h2);
      double depth=along-root;if(depth<=1e-6)depth=along+root;
      if(depth<=1e-6)continue;
      if(depth>best.depth||(depth==best.depth&&best.body>=0&&
         static_cast<std::int32_t>(b.id)>=best.body))continue;
      best.depth=depth;best.body=static_cast<std::int32_t>(b.id);hit=&b;
    }
    return shade(best,hit,ray);
  }
  void hash(Pixel p){
    auto mix=[&](std::uint64_t v){for(unsigned i=0;i<8;++i){stats_.hash^=(v>>(i*8))&0xffu;stats_.hash*=1099511628211ULL;}};
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.body)));
    mix(p.rgba);
    std::uint64_t depth_bits=0;std::memcpy(&depth_bits,&p.depth,sizeof(depth_bits));
    mix(depth_bits);
  }

  std::size_t width_,height_,tile_,tiles_x_=0,tiles_y_=0;
  Vec3 eye_{},forward_{},right_{},up_{};
  double tan_half_=0.,aspect_=1.;
  std::vector<Body> bodies_;
  std::vector<std::vector<std::uint32_t>> bins_;
  std::vector<std::vector<Run>> row_runs_;
  std::vector<Vec3> offsets_;
  std::vector<double> squared_,radius2_,near_;
  std::array<double,4096> gains_= []{std::array<double,4096> x{};x.fill(1.);return x;}();
  Stats stats_{};
  bool dirty_=true;
};

} // namespace mundo_x
