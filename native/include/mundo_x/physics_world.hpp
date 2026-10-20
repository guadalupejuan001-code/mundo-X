#pragma once
#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace mundo_x {
// Deterministic 2D circles and ground, recovered from PR22. This remains an
// independent finite-body model; it is not the procedural graphics geometry.
struct PhysicsBody {
  double x=0,y=0,vx=0,vy=0,mass=1,radius=0.5;
  bool fixed=false;
};
class PhysicsWorld {
 public:
  struct StepStats {
    std::size_t pair_tests=0,contacts=0,tree_queries=0,node_tests=0,scratch_bytes=0;
    bool dense_fallback=false;
  };
  double gravity_y=-9.81;
  double restitution=0.5;
  std::vector<PhysicsBody> bodies;
  const StepStats& step_stats() const { return stats_; }

  void step(double dt) {
    if (!(dt>0 && dt<=0.05) || !std::isfinite(dt))
      throw std::invalid_argument("invalid physics timestep");
    if (!std::isfinite(gravity_y)||!std::isfinite(restitution)||restitution<0||restitution>1)
      throw std::invalid_argument("invalid physics rules");
    for(const auto& b:bodies) validate(b);
    // Reused transactional workspace: allocation/arithmetic failure cannot
    // leave a half-integrated public world. RAM is linear, not pair-sized.
    work_=bodies;
    StepStats next;
    for(auto& b:work_) if(!b.fixed) {
      b.vy+=gravity_y*dt;
      b.x+=b.vx*dt; b.y+=b.vy*dt;
    }
    for(auto& b:work_) if(!b.fixed && b.y<b.radius) {
      b.y=b.radius;
      if(b.vy<0) b.vy=-b.vy*restitution;
    }
    for(const auto& b:work_) validate(b);
    bool indexed=work_.size()>512;
    if(indexed) {
      if(work_.size()>nodes_.max_size()/2) throw std::length_error("physics index size");
      if(order_.size()!=work_.size()||stats_.node_tests/work_.size()>64) {
        order_.resize(work_.size());std::iota(order_.begin(),order_.end(),std::size_t{0});
        leaves_.resize(work_.size());nodes_.clear();nodes_.reserve(work_.size()*2-1);
        build(0,work_.size(),none);
      } else {
        // Refit after movement instead of sorting and allocating every tick.
        // Public edits/reordering are safe: each leaf reads its current body.
        // Rebuild on the next tick if the topology lost spatial locality.
        for(auto k=nodes_.size();k-->0;)
          if(nodes_[k].id!=none)nodes_[k].box=bounds(work_[nodes_[k].id]);else refit(k);
      }
    }
    for(std::size_t i=0;i<work_.size();++i) {
      std::size_t cursor=i+1;
      while(cursor<work_.size()) {
        if(!indexed) {
          for(std::size_t j=cursor;j<work_.size();++j) collide(work_[i],work_[j],next);
          break;
        }
        candidates_.clear();++next.tree_queries;
        query(0,bounds(work_[i]),cursor,next);
        // Dense contact clouds retain the original quadratic solver. Avoid
        // repeatedly rebuilding candidate lists when most pairs are possible.
        if(candidates_.size()>std::max<std::size_t>(64,work_.size()/4)) {
          next.dense_fallback=true;indexed=false;continue;
        }
        std::sort(candidates_.begin(),candidates_.end());
        bool moved=false;
        for(const auto j:candidates_) {
          cursor=j+1;
          if(collide(work_[i],work_[j],next)) {
            update(i);update(j);moved=true;break;
          }
        }
        if(!moved) break;
        // An impulse correction can create a NEW overlap with a later pair.
        // Query the updated tree after each contact, preserving exactly the
        // old lexicographic (i,j) order, including such cascading contacts.
      }
    }
    for(const auto& b:work_) validate(b);
    next.scratch_bytes=work_.capacity()*sizeof(PhysicsBody)+nodes_.capacity()*sizeof(Node)
        +(order_.capacity()+leaves_.capacity()+candidates_.capacity())*sizeof(std::size_t);
    bodies.swap(work_);stats_=next;
  }

 private:
  struct Box { double lx,ly,hx,hy; };
  static constexpr std::size_t none=std::numeric_limits<std::size_t>::max();
  struct Node { Box box;std::size_t left=none,right=none,parent=none,id=none,max_id=0; };
  std::vector<PhysicsBody> work_;
  std::vector<Node> nodes_;
  std::vector<std::size_t> order_,leaves_,candidates_;
  StepStats stats_;
  static void validate(const PhysicsBody& b) {
    if(!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.vx)||
       !std::isfinite(b.vy)||!std::isfinite(b.mass)||b.mass<=0||
       !std::isfinite(b.radius)||b.radius<=0)
      throw std::invalid_argument("invalid or overflowed physics body");
    // The original squared-distance solver requires representable squares.
    // Reject unsupported magnitudes before mutation instead of producing NaN.
    const double bound=std::sqrt(std::numeric_limits<double>::max())/8;
    if(std::abs(b.x)>bound||std::abs(b.y)>bound||b.radius>bound||!std::isfinite(1/b.mass))
      throw std::overflow_error("physics numeric range");
  }
  static Box bounds(const PhysicsBody& b) {
    // Outward padding covers cancellation/rounding in both the circle test
    // and x +/- radius, including small radii at large world coordinates.
    const double pad=16*std::numeric_limits<double>::epsilon()*
        std::max({std::abs(b.x),std::abs(b.y),b.radius,1.0});
    return {b.x-b.radius-pad,b.y-b.radius-pad,b.x+b.radius+pad,b.y+b.radius+pad};
  }
  static Box unite(const Box& a,const Box& b) {
    return {std::min(a.lx,b.lx),std::min(a.ly,b.ly),std::max(a.hx,b.hx),std::max(a.hy,b.hy)};
  }
  std::size_t build(std::size_t first,std::size_t end,std::size_t parent) {
    const auto index=nodes_.size();nodes_.push_back({bounds(work_[order_[first]]),none,none,parent,none,0});
    if(end-first==1) {
      nodes_[index].id=nodes_[index].max_id=order_[first];leaves_[order_[first]]=index;return index;
    }
    Box box=nodes_[index].box;
    for(auto k=first+1;k<end;++k)box=unite(box,bounds(work_[order_[k]]));
    const bool x=box.hx-box.lx>=box.hy-box.ly;
    const auto mid=first+(end-first)/2;
    std::nth_element(order_.begin()+first,order_.begin()+mid,order_.begin()+end,[&](auto a,auto b){
      const double va=x?work_[a].x:work_[a].y,vb=x?work_[b].x:work_[b].y;
      return va==vb?a<b:va<vb;
    });
    const auto l=build(first,mid,index),r=build(mid,end,index);
    nodes_[index].left=l;nodes_[index].right=r;
    refit(index);return index;
  }
  void refit(std::size_t index) {
    auto& n=nodes_[index];const auto& a=nodes_[n.left];const auto& b=nodes_[n.right];
    n.box=unite(a.box,b.box);n.max_id=std::max(a.max_id,b.max_id);
  }
  void update(std::size_t id) {
    validate(work_[id]);auto index=leaves_[id];nodes_[index].box=bounds(work_[id]);
    while(nodes_[index].parent!=none) { index=nodes_[index].parent;refit(index); }
  }
  void query(std::size_t index,const Box& b,std::size_t first,StepStats& stats) {
    const auto& n=nodes_[index];++stats.node_tests;
    if(n.max_id<first||n.box.lx>b.hx||n.box.hx<b.lx||n.box.ly>b.hy||n.box.hy<b.ly)return;
    if(n.id!=none) { candidates_.push_back(n.id);return; }
    query(n.left,b,first,stats);query(n.right,b,first,stats);
  }
  bool collide(PhysicsBody& a,PhysicsBody& b,StepStats& stats) const {
    ++stats.pair_tests;
    double dx=b.x-a.x,rad=a.radius+b.radius;
    if(std::abs(dx)>=rad)return false;
    double dy=b.y-a.y;
    if(std::abs(dy)>=rad)return false;
    double dist2=dx*dx+dy*dy;
    if(dist2>=rad*rad)return false;
    double dist=std::sqrt(dist2),nx=dist>1e-12?dx/dist:1.0,ny=dist>1e-12?dy/dist:0.0;
    double ia=a.fixed?0:1/a.mass,ib=b.fixed?0:1/b.mass,sum=ia+ib;
    if(sum==0)return false;
    if(!std::isfinite(sum))throw std::overflow_error("physics inverse mass sum");
    ++stats.contacts;
    double overlap=rad-dist;
    a.x-=nx*overlap*ia/sum;a.y-=ny*overlap*ia/sum;
    b.x+=nx*overlap*ib/sum;b.y+=ny*overlap*ib/sum;
    double relative=(b.vx-a.vx)*nx+(b.vy-a.vy)*ny;
    if(relative<0) {
      double impulse=-(1+restitution)*relative/sum;
      a.vx-=impulse*ia*nx;a.vy-=impulse*ia*ny;
      b.vx+=impulse*ib*nx;b.vy+=impulse*ib*ny;
    }
    return true;
  }
};
} // namespace mundo_x
