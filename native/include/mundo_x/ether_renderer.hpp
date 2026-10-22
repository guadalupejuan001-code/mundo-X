#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mundo_x {

class EtherRenderer {
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
    bool operator==(const Pixel& other) const {
      return body==other.body && rgba==other.rgba &&
             ((std::isinf(depth)&&std::isinf(other.depth))||depth==other.depth);
    }
  };
  struct Stats {
    std::size_t width=0,height=0,tile=0,tiles_x=0,tiles_y=0,bodies=0;
    std::uint64_t candidate_references=0,brute_references=0;
    std::uint64_t candidate_tests=0,brute_tests=0;
    std::uint64_t distance_pruned_candidates=0;
    std::size_t max_candidates=0,nonempty_regions=0;
    std::size_t exact_mismatches=0;
    std::uint64_t frame_hash=1469598103934665603ULL;
    double candidate_ratio() const {
      return brute_references?static_cast<double>(candidate_references)/brute_references:0.;
    }
    double test_ratio() const {
      return brute_tests?static_cast<double>(candidate_tests)/brute_tests:0.;
    }
  };
  struct TrainingResult {
    std::size_t chosen_tile=0;
    double median_ms=0.;
    std::uint64_t candidate_tests=0;
    std::uint64_t distance_pruned_candidates=0;
    std::uint64_t frame_hash=0;
    std::size_t exact_mismatches=0;
  };

  EtherRenderer(std::size_t width,std::size_t height,std::size_t tile)
      : width_(width),height_(height),tile_(tile) {
    if(width<1||height<1||width>8192||height>8192)
      throw std::invalid_argument("render dimensions must be 1..8192");
    if(tile<8||tile>1024)throw std::invalid_argument("render tile must be 8..1024");
    tiles_x_=1+(width_-1)/tile_;tiles_y_=1+(height_-1)/tile_;
    masks_.assign(tiles_x_*tiles_y_,0);
    frame_.assign(width_*height_,Pixel{});
    set_camera({0,0,10},{0,0,0},47.);
  }

  void set_camera(Vec3 eye,Vec3 target,double fov_deg) {
    if(!finite(eye)||!finite(target)||!std::isfinite(fov_deg)||fov_deg<=1.||fov_deg>=170.)
      throw std::invalid_argument("invalid render camera");
    eye_=eye;forward_=sub(target,eye);
    const double flen=length(forward_);
    if(!(flen>1e-12))throw std::invalid_argument("camera eye equals target");
    forward_=mul(forward_,1./flen);
    Vec3 world_up{0,1,0};
    if(std::abs(dot(forward_,world_up))>.999)world_up={0,0,1};
    right_=normalize(cross(forward_,world_up));
    up_=normalize(cross(right_,forward_));
    tan_half_fov_=std::tan(fov_deg*std::acos(-1.)/360.);
    aspect_=static_cast<double>(width_)/static_cast<double>(height_);
    dirty_=true;
  }

  void add_body(const Body& body) {
    if(body.id>=64)throw std::invalid_argument("render body id must be 0..63");
    if(!finite(body.center)||!std::isfinite(body.radius)||!(body.radius>0.))
      throw std::invalid_argument("invalid render body geometry");
    for(double c:body.color)if(!std::isfinite(c)||c<0.||c>1.)
      throw std::invalid_argument("render body color must be 0..1");
    for(const auto& existing:bodies_)if(existing.id==body.id)
      throw std::invalid_argument("duplicate render body id");
    if(bodies_.size()>=64)throw std::length_error("render supports at most 64 bodies");
    bodies_.push_back(body);
    std::sort(bodies_.begin(),bodies_.end(),[](const Body& a,const Body& b){return a.id<b.id;});
    dirty_=true;
  }

  void clear_bodies(){bodies_.clear();dirty_=true;}

  void set_tile(std::size_t tile) {
    if(tile<8||tile>1024)throw std::invalid_argument("render tile must be 8..1024");
    tile_=tile;tiles_x_=1+(width_-1)/tile_;tiles_y_=1+(height_-1)/tile_;
    masks_.assign(tiles_x_*tiles_y_,0);dirty_=true;planned_=false;
  }

  const std::vector<Body>& bodies() const noexcept{return bodies_;}
  const Stats& stats() const noexcept{return stats_;}
  const std::vector<Pixel>& frame() const noexcept{return frame_;}

  void plan() {
    std::fill(masks_.begin(),masks_.end(),std::uint64_t{0});
    rebuild_distance_order();
    for(const auto& body:bodies_)project_body(body);
    stats_=Stats{};
    stats_.width=width_;stats_.height=height_;stats_.tile=tile_;
    stats_.tiles_x=tiles_x_;stats_.tiles_y=tiles_y_;stats_.bodies=bodies_.size();
    for(const auto mask:masks_){
      const auto count=popcount(mask);
      stats_.candidate_references+=count;
      stats_.max_candidates=std::max(stats_.max_candidates,count);
      if(mask)++stats_.nonempty_regions;
    }
    stats_.brute_references=static_cast<std::uint64_t>(masks_.size())*bodies_.size();
    planned_=true;dirty_=false;
  }

  Stats render(bool compare_bruteforce=false) {
    if(dirty_||!planned_)plan();
    stats_.candidate_tests=0;stats_.brute_tests=0;stats_.exact_mismatches=0;
    stats_.distance_pruned_candidates=0;
    stats_.frame_hash=1469598103934665603ULL;
    for(std::size_t y=0;y<height_;++y)for(std::size_t x=0;x<width_;++x){
      const auto ray=ray_for(x,y);
      const auto mask=masks_[(y/tile_)*tiles_x_+(x/tile_)];
      Pixel optimized=trace_ordered(ray,mask,&stats_.candidate_tests,&stats_.distance_pruned_candidates);
      frame_[y*width_+x]=optimized;
      hash_pixel(optimized);
      if(compare_bruteforce){
        const Pixel brute=trace(ray,body_mask(),&stats_.brute_tests);
        if(!(optimized==brute))++stats_.exact_mismatches;
      }
    }
    if(!compare_bruteforce)
      stats_.brute_tests=static_cast<std::uint64_t>(width_)*height_*bodies_.size();
    return stats_;
  }

  TrainingResult train_tiles(const std::vector<std::size_t>& candidates={8,16,32,64,128,256}) {
    if(candidates.empty())throw std::invalid_argument("render training requires tile candidates");
    TrainingResult best;bool have=false;std::uint64_t reference_hash=0;
    for(const auto tile:candidates){
      set_tile(tile);plan();
      // One warm-up keeps allocation/cache setup outside the measured samples.
      render(false);
      std::array<double,3> samples{};
      std::uint64_t tests=0,pruned=0,hash=0;
      for(std::size_t repeat=0;repeat<samples.size();++repeat){
        const auto begin=std::chrono::steady_clock::now();
        const auto measured=render(false);
        const auto end=std::chrono::steady_clock::now();
        samples[repeat]=std::chrono::duration<double,std::milli>(end-begin).count();
        tests=measured.candidate_tests;pruned=measured.distance_pruned_candidates;hash=measured.frame_hash;
      }
      std::sort(samples.begin(),samples.end());
      if(!have)reference_hash=hash;
      else if(hash!=reference_hash)throw std::runtime_error("render training changed the frame across tile sizes");
      if(!have||samples[1]<best.median_ms||
         (samples[1]==best.median_ms&&tests<best.candidate_tests)){
        have=true;best.chosen_tile=tile;best.median_ms=samples[1];
        best.candidate_tests=tests;best.distance_pruned_candidates=pruned;best.frame_hash=hash;
      }
    }
    set_tile(best.chosen_tile);plan();
    const auto exact=render(true);
    best.exact_mismatches=exact.exact_mismatches;
    best.candidate_tests=exact.candidate_tests;
    best.distance_pruned_candidates=exact.distance_pruned_candidates;
    best.frame_hash=exact.frame_hash;
    if(best.exact_mismatches)throw std::runtime_error("trained render path differs from brute-force oracle");
    return best;
  }

  const Pixel& pixel(std::size_t x,std::size_t y) const {
    if(x>=width_||y>=height_)throw std::out_of_range("render pixel outside framebuffer");
    return frame_[y*width_+x];
  }

  std::uint64_t mask_at(std::size_t x,std::size_t y) const {
    if(x>=width_||y>=height_)throw std::out_of_range("render mask query outside framebuffer");
    return masks_[(y/tile_)*tiles_x_+(x/tile_)];
  }

 private:
  static bool finite(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
  static Vec3 sub(Vec3 a,Vec3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
  static Vec3 add(Vec3 a,Vec3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
  static Vec3 mul(Vec3 a,double s){return {a.x*s,a.y*s,a.z*s};}
  static double dot(Vec3 a,Vec3 b){return a.x*b.x+a.y*b.y+a.z*b.z;}
  static Vec3 cross(Vec3 a,Vec3 b){return {a.y*b.z-a.z*b.y,a.z*b.x-a.x*b.z,a.x*b.y-a.y*b.x};}
  static double length(Vec3 a){return std::sqrt(dot(a,a));}
  static Vec3 normalize(Vec3 a){const double n=length(a);if(!(n>1e-15))throw std::invalid_argument("zero render vector");return mul(a,1./n);}
  static std::size_t popcount(std::uint64_t v){std::size_t n=0;while(v){v&=v-1;++n;}return n;}
  std::uint64_t body_mask() const {
    std::uint64_t mask=0;for(const auto& b:bodies_)mask|=std::uint64_t{1}<<b.id;return mask;
  }
  void fill(std::uint32_t id,int x0,int x1,int y0,int y1){
    const auto bit=std::uint64_t{1}<<id;
    x0=std::max(0,x0);y0=std::max(0,y0);
    x1=std::min(static_cast<int>(tiles_x_)-1,x1);
    y1=std::min(static_cast<int>(tiles_y_)-1,y1);
    if(x0>x1||y0>y1)return;
    for(int y=y0;y<=y1;++y)for(int x=x0;x<=x1;++x)masks_[static_cast<std::size_t>(y)*tiles_x_+x]|=bit;
  }
  void project_body(const Body& b){
    const Vec3 relative=sub(b.center,eye_);
    const double x=dot(relative,right_),y=dot(relative,up_),z=dot(relative,forward_),r=b.radius;
    constexpr double near=1e-6;
    if(z+r<=near)return;
    if(z<=r+near){fill(b.id,0,static_cast<int>(tiles_x_)-1,0,static_cast<int>(tiles_y_)-1);return;}
    const double zn=z-r,zf=z+r;
    const double ax=(x-r)/zn,bx=(x-r)/zf,cx=(x+r)/zn,dx=(x+r)/zf;
    const double ay=(y-r)/zn,by=(y-r)/zf,cy=(y+r)/zn,dy=(y+r)/zf;
    const double sx=tan_half_fov_*aspect_,sy=tan_half_fov_;
    const double margin_x=2.*static_cast<double>(tile_)/static_cast<double>(width_);
    const double margin_y=2.*static_cast<double>(tile_)/static_cast<double>(height_);
    double left=std::min(std::min(ax,bx),std::min(cx,dx))/sx-margin_x;
    double right=std::max(std::max(ax,bx),std::max(cx,dx))/sx+margin_x;
    double bottom=std::min(std::min(ay,by),std::min(cy,dy))/sy-margin_y;
    double top=std::max(std::max(ay,by),std::max(cy,dy))/sy+margin_y;
    if(right<-1.||left>1.||top<-1.||bottom>1.)return;
    left=std::max(-1.,left);right=std::min(1.,right);
    bottom=std::max(-1.,bottom);top=std::min(1.,top);
    const auto px0=(left*.5+.5)*static_cast<double>(width_);
    const auto px1=(right*.5+.5)*static_cast<double>(std::max<std::size_t>(1,width_-1));
    const auto py0=(bottom*.5+.5)*static_cast<double>(height_);
    const auto py1=(top*.5+.5)*static_cast<double>(std::max<std::size_t>(1,height_-1));
    fill(b.id,static_cast<int>(std::floor(px0/tile_)),static_cast<int>(std::floor(px1/tile_)),
              static_cast<int>(std::floor(py0/tile_)),static_cast<int>(std::floor(py1/tile_)));
  }
  Vec3 ray_for(std::size_t x,std::size_t y) const {
    const double nx=2.*(static_cast<double>(x)+.5)/static_cast<double>(width_)-1.;
    const double ny=2.*(static_cast<double>(y)+.5)/static_cast<double>(height_)-1.;
    return normalize(add(forward_,add(mul(right_,nx*aspect_*tan_half_fov_),mul(up_,ny*tan_half_fov_))));
  }
  Pixel trace(const Vec3& ray,std::uint64_t mask,std::uint64_t* tests) const {
    Pixel best;const Body* hit=nullptr;
    for(const auto& b:bodies_){
      if((mask&(std::uint64_t{1}<<b.id))==0)continue;
      if(tests)++*tests;
      const Vec3 c=sub(b.center,eye_);
      const double along=dot(c,ray);
      const double h2=b.radius*b.radius-(dot(c,c)-along*along);
      if(h2<0.)continue;
      const double root=std::sqrt(h2);
      double t=along-root;if(t<=1e-6)t=along+root;
      if(t<=1e-6||t>=best.depth)continue;
      best.depth=t;best.body=static_cast<std::int32_t>(b.id);hit=&b;
    }
    if(!hit){
      const double v=std::max(0.,std::min(1.,.025+.08*(ray.y*.5+.5)));
      const auto c=static_cast<std::uint32_t>(std::lround(v*255.));
      best.rgba=(255u<<24)|(c<<16)|(c<<8)|(std::min<std::uint32_t>(255,c+5));
      return best;
    }
    const Vec3 point=add(eye_,mul(ray,best.depth));
    const Vec3 normal=normalize(sub(point,hit->center));
    const Vec3 light=normalize(Vec3{.37,.74,.56});
    const double l=.12+.88*std::max(0.,dot(normal,light));
    auto channel=[&](double c){return static_cast<std::uint32_t>(std::lround(std::max(0.,std::min(1.,c*l))*255.));};
    const auto r=channel(hit->color[0]),g=channel(hit->color[1]),b=channel(hit->color[2]);
    best.rgba=(255u<<24)|(r<<16)|(g<<8)|b;return best;
  }
  void rebuild_distance_order(){
    distance_order_.clear();distance_lower_bound_.assign(bodies_.size(),0.);
    distance_order_.reserve(bodies_.size());
    for(std::size_t i=0;i<bodies_.size();++i){
      distance_lower_bound_[i]=std::max(0.,length(sub(bodies_[i].center,eye_))-bodies_[i].radius);
      distance_order_.push_back(i);
    }
    std::stable_sort(distance_order_.begin(),distance_order_.end(),[&](std::size_t a,std::size_t b){
      return distance_lower_bound_[a]<distance_lower_bound_[b];
    });
  }
  Pixel trace_ordered(const Vec3& ray,std::uint64_t mask,std::uint64_t* tests,std::uint64_t* pruned) const {
    Pixel best;const Body* hit=nullptr;std::size_t order_pos=0;
    for(;order_pos<distance_order_.size();++order_pos){
      const auto index=distance_order_[order_pos];
      if(best.body>=0&&distance_lower_bound_[index]>=best.depth)break;
      const auto& b=bodies_[index];
      if((mask&(std::uint64_t{1}<<b.id))==0)continue;
      if(tests)++*tests;
      const Vec3 c=sub(b.center,eye_);
      const double along=dot(c,ray);
      const double h2=b.radius*b.radius-(dot(c,c)-along*along);
      if(h2<0.)continue;
      const double root=std::sqrt(h2);
      double t=along-root;if(t<=1e-6)t=along+root;
      if(t<=1e-6||t>=best.depth)continue;
      best.depth=t;best.body=static_cast<std::int32_t>(b.id);hit=&b;
    }
    if(pruned&&best.body>=0){
      for(;order_pos<distance_order_.size();++order_pos){
        const auto index=distance_order_[order_pos];
        if(mask&(std::uint64_t{1}<<bodies_[index].id))++*pruned;
      }
    }
    if(!hit){
      const double v=std::max(0.,std::min(1.,.025+.08*(ray.y*.5+.5)));
      const auto c=static_cast<std::uint32_t>(std::lround(v*255.));
      best.rgba=(255u<<24)|(c<<16)|(c<<8)|(std::min<std::uint32_t>(255,c+5));
      return best;
    }
    const Vec3 point=add(eye_,mul(ray,best.depth));
    const Vec3 normal=normalize(sub(point,hit->center));
    const Vec3 light=normalize(Vec3{.37,.74,.56});
    const double l=.12+.88*std::max(0.,dot(normal,light));
    auto channel=[&](double c){return static_cast<std::uint32_t>(std::lround(std::max(0.,std::min(1.,c*l))*255.));};
    const auto r=channel(hit->color[0]),g=channel(hit->color[1]),b=channel(hit->color[2]);
    best.rgba=(255u<<24)|(r<<16)|(g<<8)|b;return best;
  }

  void hash_pixel(const Pixel& p){
    auto mix=[&](std::uint64_t value){for(unsigned i=0;i<8;++i){stats_.frame_hash^=(value>>(i*8))&0xffu;stats_.frame_hash*=1099511628211ULL;}};
    mix(static_cast<std::uint64_t>(static_cast<std::int64_t>(p.body)));mix(p.rgba);
  }

  std::size_t width_,height_,tile_,tiles_x_,tiles_y_;
  Vec3 eye_{},forward_{},right_{},up_{};
  double tan_half_fov_=0.,aspect_=1.;
  std::vector<Body> bodies_;
  std::vector<std::uint64_t> masks_;
  std::vector<std::size_t> distance_order_;
  std::vector<double> distance_lower_bound_;
  std::vector<Pixel> frame_;
  Stats stats_{};
  bool dirty_=true,planned_=false;
};

}  // namespace mundo_x
