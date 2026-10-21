#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <vector>

namespace mundo_x {

// Recovered from PR21 fd0bd14. Finite 3D spheres with one shared rule set.
// Bodies are explicit classical state, not qubits or Ether's Clifford state.
// The complete public step commits once. Broadphase rejection is conservative
// and preserves the original lexicographic contact order after every movement.
class RulePhysics {
 public:
  struct Vec3 {
    double x=0,y=0,z=0;
    Vec3& operator+=(const Vec3& b){x+=b.x;y+=b.y;z+=b.z;return *this;}
    Vec3& operator-=(const Vec3& b){x-=b.x;y-=b.y;z-=b.z;return *this;}
    Vec3& operator*=(double s){x*=s;y*=s;z*=s;return *this;}
  };
  struct Body {
    Vec3 position,velocity;
    double inverse_mass=1,radius=.5,restitution=.2;
  };
  struct Rules {
    Vec3 uniform_acceleration{0,-9.80665,0};
    double pair_gravity=0,softening=1e-6,max_substep=1./120.;
    unsigned max_substeps=4096;
    std::size_t max_bodies=65536;
    // Exact work budgets: exceeding one rejects the entire step, never drops
    // a force/contact. Pair gravity remains quadratic and is opt-in.
    std::size_t max_pair_tests=8000000,max_tree_visits=64000000;
  };
  struct StepStats {
    std::size_t collision_pairs=0,gravity_pairs=0,contacts=0,node_tests=0;
    std::size_t scratch_bytes=0;
    unsigned substeps=0;
    bool dense_fallback=false;
  };

  RulePhysics():RulePhysics(Rules{}){}
  explicit RulePhysics(const Rules& rules):rules_(rules){validate_rules(rules_);}
  const Rules& rules() const noexcept{return rules_;}
  const std::vector<Body>& bodies() const noexcept{return bodies_;}
  const StepStats& step_stats() const noexcept{return stats_;}
  std::uint64_t revision() const noexcept{return revision_;}
  void set_rules(const Rules& rules) {
    validate_rules(rules);
    if(bodies_.size()>rules.max_bodies)throw std::length_error("existing bodies exceed new budget");
    require_revision();rules_=rules;++revision_;
  }
  std::size_t add(Body body) {
    validate_body(body);require_revision();
    if(bodies_.size()>=rules_.max_bodies)throw std::length_error("physics body budget exceeded");
    bodies_.push_back(body);++revision_;return bodies_.size()-1;
  }
  void set_body(std::size_t id,Body body) {
    if(id>=bodies_.size())throw std::out_of_range("physics body index");
    validate_body(body);require_revision();bodies_[id]=body;++revision_;
  }
  void clear() {
    if(bodies_.empty())return;
    require_revision();bodies_.clear();index_ready_=false;++revision_;
  }
  std::size_t body_payload_bytes() const noexcept{return bodies_.size()*sizeof(Body);}

  void step(double seconds) {
    if(!std::isfinite(seconds)||seconds<0)
      throw std::invalid_argument("physics step must be finite and nonnegative");
    if(seconds==0||bodies_.empty())return;
    // Check the floating quotient before conversion: out-of-range float to
    // integer conversion is undefined behavior, including finite seconds.
    const double quotient=seconds/rules_.max_substep;
    const double requested=std::max(1.0,std::ceil(quotient));
    if(!std::isfinite(requested)||requested>rules_.max_substeps)
      throw std::length_error("physics step exceeds substep budget");
    const auto count=static_cast<unsigned>(requested);
    require_revision();
    const double dt=seconds/count;
    work_=bodies_;
    acceleration_.resize(work_.size());
    StepStats next;next.substeps=count;
    for(unsigned i=0;i<count;++i)substep(dt,next);
    next.scratch_bytes=work_.capacity()*sizeof(Body)+acceleration_.capacity()*sizeof(Vec3)
        +nodes_.capacity()*sizeof(Node)
        +(order_.capacity()+leaves_.capacity()+candidates_.capacity())*sizeof(std::size_t);
    // Every fallible step precedes this commit. Body references remain valid.
    std::copy(work_.begin(),work_.end(),bodies_.begin());stats_=next;++revision_;
  }

 private:
  struct Box{double lx,ly,lz,hx,hy,hz;};
  static constexpr std::size_t none=std::numeric_limits<std::size_t>::max();
  struct Node{Box box;std::size_t left=none,right=none,parent=none,id=none,max_id=0;};
  Rules rules_;
  std::vector<Body> bodies_,work_;
  std::vector<Vec3> acceleration_;
  std::vector<Node> nodes_;
  std::vector<std::size_t> order_,leaves_,candidates_;
  bool index_ready_=false;
  StepStats stats_;
  std::uint64_t revision_=0;

  static Vec3 sub(Vec3 a,const Vec3& b){return a-=b;}
  static Vec3 mul(Vec3 a,double s){return a*=s;}
  static double dot(const Vec3& a,const Vec3& b){return a.x*b.x+a.y*b.y+a.z*b.z;}
  static bool finite3(const Vec3& v){return std::isfinite(v.x)&&std::isfinite(v.y)&&std::isfinite(v.z);}
  static void validate_rules(const Rules& r) {
    if(!finite3(r.uniform_acceleration)||!std::isfinite(r.pair_gravity)||r.pair_gravity<0||
       !std::isfinite(r.softening)||r.softening<=0||!std::isfinite(r.softening*r.softening)||
       r.softening*r.softening==0||!std::isfinite(r.max_substep)||r.max_substep<=0||
       r.max_substeps==0||r.max_bodies==0||r.max_pair_tests==0||r.max_tree_visits==0)
      throw std::invalid_argument("invalid physics rules");
  }
  static void validate_body(const Body& b) {
    if(!finite3(b.position)||!finite3(b.velocity)||!std::isfinite(b.inverse_mass)||
       b.inverse_mass<0||(b.inverse_mass>0&&!std::isfinite(1/b.inverse_mass))||
       !std::isfinite(b.radius)||b.radius<=0||!std::isfinite(b.restitution)||
       b.restitution<0||b.restitution>1)throw std::invalid_argument("invalid or overflowed physics body");
    const double bound=std::sqrt(std::numeric_limits<double>::max())/16;
    if(std::max({std::abs(b.position.x),std::abs(b.position.y),std::abs(b.position.z),b.radius})>bound)
      throw std::overflow_error("physics position/radius exceeds squared-distance range");
  }
  void require_revision() const {
    if(revision_==std::numeric_limits<std::uint64_t>::max())throw std::overflow_error("physics revision overflow");
  }
  void count_pair(StepStats& s,bool gravity) const {
    // Individual counters and their sum stay <= the configured size_t budget.
    if(s.collision_pairs>=rules_.max_pair_tests-s.gravity_pairs)
      throw std::length_error("physics pair-work budget exceeded");
    if(gravity)++s.gravity_pairs;else ++s.collision_pairs;
  }
  static Box bounds(const Body& b) {
    const auto& p=b.position;
    const double pad=16*std::numeric_limits<double>::epsilon()*
        std::max({std::abs(p.x),std::abs(p.y),std::abs(p.z),b.radius,1.0});
    return {p.x-b.radius-pad,p.y-b.radius-pad,p.z-b.radius-pad,
            p.x+b.radius+pad,p.y+b.radius+pad,p.z+b.radius+pad};
  }
  static Box unite(const Box& a,const Box& b) {
    return {std::min(a.lx,b.lx),std::min(a.ly,b.ly),std::min(a.lz,b.lz),
            std::max(a.hx,b.hx),std::max(a.hy,b.hy),std::max(a.hz,b.hz)};
  }
  void refit(std::size_t index) {
    auto& n=nodes_[index];const auto& a=nodes_[n.left];const auto& b=nodes_[n.right];
    n.box=unite(a.box,b.box);n.max_id=std::max(a.max_id,b.max_id);
  }
  std::size_t build(std::size_t first,std::size_t end,std::size_t parent) {
    const auto index=nodes_.size();nodes_.push_back({bounds(work_[order_[first]]),none,none,parent,none,0});
    if(end-first==1){nodes_[index].id=nodes_[index].max_id=order_[first];leaves_[order_[first]]=index;return index;}
    Box box=nodes_[index].box;
    for(auto k=first+1;k<end;++k)box=unite(box,bounds(work_[order_[k]]));
    const double spans[]={box.hx-box.lx,box.hy-box.ly,box.hz-box.lz};
    const auto axis=std::max_element(spans,spans+3)-spans;
    const auto coordinate=[&](std::size_t id){const auto& p=work_[id].position;return axis==0?p.x:axis==1?p.y:p.z;};
    const auto mid=first+(end-first)/2;
    std::nth_element(order_.begin()+first,order_.begin()+mid,order_.begin()+end,[&](auto a,auto b){
      const double va=coordinate(a),vb=coordinate(b);return va==vb?a<b:va<vb;
    });
    const auto left=build(first,mid,index),right=build(mid,end,index);
    nodes_[index].left=left;nodes_[index].right=right;refit(index);return index;
  }
  void prepare_index() {
    if(work_.size()>nodes_.max_size()/2)throw std::length_error("physics index dimensions");
    if(!index_ready_||order_.size()!=work_.size()||
       stats_.node_tests/work_.size()/std::max(1U,stats_.substeps)>64) {
      index_ready_=false;
      order_.resize(work_.size());std::iota(order_.begin(),order_.end(),std::size_t{0});
      leaves_.resize(work_.size());nodes_.clear();nodes_.reserve(work_.size()*2-1);
      build(0,work_.size(),none);index_ready_=true;
    } else {
      for(auto k=nodes_.size();k-->0;)
        if(nodes_[k].id!=none)nodes_[k].box=bounds(work_[nodes_[k].id]);else refit(k);
    }
  }
  void update(std::size_t id) {
    validate_body(work_[id]);auto index=leaves_[id];nodes_[index].box=bounds(work_[id]);
    while(nodes_[index].parent!=none){index=nodes_[index].parent;refit(index);}
  }
  void query(std::size_t index,const Box& b,std::size_t first,StepStats& stats) {
    if(stats.node_tests>=rules_.max_tree_visits)throw std::length_error("physics tree-work budget exceeded");
    ++stats.node_tests;const auto& n=nodes_[index];
    if(n.max_id<first||n.box.lx>b.hx||n.box.hx<b.lx||n.box.ly>b.hy||n.box.hy<b.ly||n.box.lz>b.hz||n.box.hz<b.lz)return;
    if(n.id!=none){candidates_.push_back(n.id);return;}
    query(n.left,b,first,stats);query(n.right,b,first,stats);
  }
  void substep(double dt,StepStats& stats) {
    std::fill(acceleration_.begin(),acceleration_.end(),rules_.uniform_acceleration);
    if(rules_.pair_gravity>0) {
      const double soft2=rules_.softening*rules_.softening;
      for(std::size_t i=0;i<work_.size();++i) {
        if(work_[i].inverse_mass==0)continue;
        for(std::size_t j=i+1;j<work_.size();++j) {
          if(work_[j].inverse_mass==0)continue;
          count_pair(stats,true);
          const Vec3 d=sub(work_[j].position,work_[i].position);
          const double r2=dot(d,d)+soft2,inv_r=1./std::sqrt(r2),inv_r3=inv_r*inv_r*inv_r;
          const Vec3 direction=mul(d,rules_.pair_gravity*inv_r3);
          acceleration_[i]+=mul(direction,1./work_[j].inverse_mass);
          acceleration_[j]-=mul(direction,1./work_[i].inverse_mass);
        }
      }
    }
    for(std::size_t i=0;i<work_.size();++i) {
      auto& b=work_[i];if(b.inverse_mass==0)continue;
      if(!finite3(acceleration_[i]))throw std::overflow_error("physics acceleration overflow");
      b.velocity+=mul(acceleration_[i],dt);b.position+=mul(b.velocity,dt);validate_body(b);
    }
    bool indexed=work_.size()>128;
    if(indexed)prepare_index();
    for(std::size_t i=0;i<work_.size();++i) {
      std::size_t cursor=i+1;
      while(cursor<work_.size()) {
        if(!indexed){for(std::size_t j=cursor;j<work_.size();++j)collide(work_[i],work_[j],stats);break;}
        candidates_.clear();query(0,bounds(work_[i]),cursor,stats);
        if(candidates_.size()>std::max<std::size_t>(64,work_.size()/4)) {
          stats.dense_fallback=true;indexed=false;continue;
        }
        std::sort(candidates_.begin(),candidates_.end());bool moved=false;
        for(const auto j:candidates_) {
          cursor=j+1;
          if(collide(work_[i],work_[j],stats)){update(i);update(j);moved=true;break;}
        }
        if(!moved)break;
      }
    }
    for(const auto& b:work_)validate_body(b);
  }
  bool collide(Body& a,Body& b,StepStats& stats) const {
    count_pair(stats,false);
    const Vec3 delta=sub(b.position,a.position);
    const double minimum=a.radius+b.radius,d2=dot(delta,delta);
    if(d2>=minimum*minimum)return false;
    const double distance=std::sqrt(std::max(d2,1e-24));
    const Vec3 normal=distance>1e-12?mul(delta,1./distance):Vec3{1,0,0};
    const double inv_sum=a.inverse_mass+b.inverse_mass;
    if(inv_sum==0)return false;
    if(!std::isfinite(inv_sum))throw std::overflow_error("physics inverse mass sum");
    ++stats.contacts;
    const Vec3 correction=mul(normal,(minimum-distance)/inv_sum);
    if(a.inverse_mass>0)a.position-=mul(correction,a.inverse_mass);
    if(b.inverse_mass>0)b.position+=mul(correction,b.inverse_mass);
    const double closing=dot(sub(b.velocity,a.velocity),normal);
    if(closing<0) {
      const double impulse=-(1.+std::min(a.restitution,b.restitution))*closing/inv_sum;
      if(a.inverse_mass>0)a.velocity-=mul(normal,impulse*a.inverse_mass);
      if(b.inverse_mass>0)b.velocity+=mul(normal,impulse*b.inverse_mass);
    }
    validate_body(a);validate_body(b);return true;
  }
};
} // namespace mundo_x
