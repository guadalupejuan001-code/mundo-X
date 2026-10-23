#pragma once

#include "mundo_x/visual_scene.hpp"
#include "mundo_x/quantum_runtime.hpp"
#include "mundo_x/rule_physics.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// One World session with three canonical Mundo X authorities:
// VisualScene is the generation rule for a 1024x1024 logical spatial lattice;
// QuantumRuntime/InverseMemory (COW256) owns sparse edits and an optional
// small, simulated quantum field; RulePhysics evolves bounded active probes.
// Only viewed/observed cells are reconstructed. The million-cell array does
// not exist in memory, and this is NOT a sensor scan of external reality.
class EtherWorld {
 public:
  static constexpr int kHalf = 512;
  static constexpr int kWidth = 1024;
  static constexpr std::uint32_t kLogicalCells = 1048576;
  static constexpr std::size_t kMaxScans = 256;
  static constexpr std::size_t kMaxPulses = 12;
  static constexpr std::size_t kMaxProbes = 10;
  static constexpr int kScanRadius = 7;
  static constexpr double kCellSize = 2.2;
  static constexpr std::size_t kMaxSaveBytes = 1024 * 1024;

  struct Region {
    int x=0,z=0,radius=kScanRadius;
  };
  struct Pulse {
    double x=0,z=0;
    int age=0;
  };
  struct Cell {
    int x=0,z=0;
    float height=0,tone=0;
    std::uint8_t edit=0;
    bool revealed=false;
  };
  struct Stats {
    std::int32_t seed=0;
    int center_x=0,center_z=0,selected_x=0,selected_z=0;
    double angle=0,zoom=0;
    std::uint64_t ticks=0,revision=0;
    std::size_t scans=0,explored=0,edits=0,probes=0,pulses=0;
    std::size_t quantum_active=0,quantum_components=0;
    double quantum_field=0;
  };

  explicit EtherWorld(std::int32_t seed=1000)
      : seed_(seed), rule_(kLogicalCells, seed), ether_(
          InfiniteCore::Background::Zero, quantum_limits(), memory_limits()),
        physics_(physical_rules()) {
    // Simulated Bell lattice; two finite qubits, never 1 million qubits.
    ether_.execute("h 0\ncx 0 1\n");
    scans_.push_back({0,0,kScanRadius});
    explored_ = region_cell_count(0,0,kScanRadius);
    spawn_probes();
  }

  static bool valid_coord(int x,int z) noexcept {
    return x>=-kHalf && x<kHalf && z>=-kHalf && z<kHalf;
  }
  static std::uint32_t id(int x,int z) {
    require_coord(x,z);
    return static_cast<std::uint32_t>((z+kHalf)*kWidth+(x+kHalf));
  }
  static double world_coord(int logical) noexcept {
    return (static_cast<double>(logical) + 0.5) * kCellSize;
  }
  static int logical_coord(double physical) {
    if(!std::isfinite(physical) || std::abs(physical)>1e6)
      throw std::out_of_range("invalid world position");
    return static_cast<int>(std::floor(physical/kCellSize));
  }

  Cell cell(int x,int z) const {
    Cell out;
    out.x=x;out.z=z;
    if(!valid_coord(x,z))return out;
    const auto base=rule_.transform(id(x,z));
    out.height=base[2];out.tone=base[3];
    const auto r=ether_.memory().read(std::to_string(id(x,z)),ether_.core());
    if(!r.deterministic)throw std::logic_error("world spatial edit is quantum; refusing implicit measurement");
    if(r.value>4)throw std::runtime_error("corrupted Ether spatial edit");
    out.edit=r.value;
    out.revealed=is_revealed(x,z);
    return out;
  }

  // Unknown regions are not measured/rendered as explored terrain. Their
  // generation rules persist and reproduce exact cells on demand.
  bool is_revealed(int x,int z) const noexcept {
    if(!valid_coord(x,z))return false;
    for(const auto& region:scans_) {
      const std::int64_t dx=static_cast<std::int64_t>(x)-region.x;
      const std::int64_t dz=static_cast<std::int64_t>(z)-region.z;
      if(dx*dx+dz*dz<=region.radius*region.radius)return true;
    }
    return false;
  }

  std::size_t scan(int x,int z) {
    require_coord(x,z);
    if(scans_.size()>=kMaxScans)
      throw std::length_error("se alcanzó el límite de 256 escaneos; guarda y explora otro mundo");
    const std::size_t added=reveal_region(x,z,kScanRadius);
    if(added==0)return 0;
    scans_.push_back({x,z,kScanRadius});
    explored_+=added;
    ++revision_;
    return added;
  }
  std::size_t scan_center() { return scan(center_x_,center_z_); }

  void move(int dx,int dz) {
    if(dx < -6 || dx >6 || dz< -6 || dz >6)
      throw std::invalid_argument("move maximum distance 6");
    const int nx=std::clamp(center_x_+dx,-kHalf,kHalf-1);
    const int nz=std::clamp(center_z_+dz,-kHalf,kHalf-1);
    if(nx==center_x_&&nz==center_z_)return;
    center_x_=nx;center_z_=nz;
    select(nx,nz);
    ++revision_;
  }
  void origin() {
    center_x_=center_z_=0;
    select(0,0);
    ++revision_;
  }
  void select(int x,int z) {
    require_coord(x,z);
    selected_x_=x;selected_z_=z;
  }
  void orbit(double radians) {
    if(!std::isfinite(radians)||std::abs(radians)>1.)
      throw std::invalid_argument("invalid orbit step");
    constexpr double twopi=6.28318530717958647692;
    angle_=std::fmod(angle_+radians,twopi);
    if(angle_<0)angle_+=twopi;
    ++revision_;
  }
  void magnify(int direction) {
    if(direction < -1 || direction>1)throw std::invalid_argument("invalid zoom step");
    zoom_=std::clamp(zoom_+direction*2.5,9.0,30.0);
    ++revision_;
  }
  void follow_probe() {
    if(physics_.bodies().empty())return;
    const auto& p=physics_.bodies().front().position;
    center_x_=std::clamp(logical_coord(p.x),-kHalf,kHalf-1);
    center_z_=std::clamp(logical_coord(p.z),-kHalf,kHalf-1);
    select(center_x_,center_z_);
    ++revision_;
  }

  // Edit 0 restores the generated rule. Nonzero states live exclusively
  // in Ether InverseMemory, not in a shadow per-cell scene array.
  void edit(int x,int z,unsigned state) {
    require_coord(x,z);
    if(!is_revealed(x,z))throw std::logic_error("escanea primero la zona antes de editar");
    if(state>4)throw std::invalid_argument("world cell edit must be 0..4");
    const Cell old=cell(x,z);
    if(old.edit==state)return;
    const std::string line="mem_store "+std::to_string(id(x,z))+
        " "+std::to_string(state)+"\n";
    ether_.execute(line);
    if(cell(x,z).edit!=state)
      throw std::runtime_error("Ether memory rejected a world mutation");
    ++revision_;
  }
  void edit_selected(unsigned state) { edit(selected_x_,selected_z_,state); }

  void emit_pulse() {
    if(!is_revealed(selected_x_,selected_z_))
      throw std::logic_error("selecciona una zona revelada para emitir un campo");
    if(pulses_.size()>=kMaxPulses)pulses_.erase(pulses_.begin());
    pulses_.push_back({static_cast<double>(selected_x_),
                       static_cast<double>(selected_z_),0});
    // Simulated, explicitly defined local quantum-gauge transformation.
    // It changes the virtual field factor and therefore the world appearance.
    ether_.execute("x 2\n");
    ++revision_;
  }

  // Physical simulation of bounded autonomous bodies, not a physics engine
  // applied to all million logical positions. Exactly 0.05s per step.
  void step(unsigned steps) {
    if(steps<1||steps>64)throw std::invalid_argument("step count must be 1..64");
    for(unsigned n=0;n<steps;++n){
      auto previous=physics_.bodies();
      for(std::size_t k=0;k<previous.size();++k) {
        auto& b=previous[k];
        const int x=std::clamp(logical_coord(b.position.x),-kHalf+1,kHalf-2);
        const int z=std::clamp(logical_coord(b.position.z),-kHalf+1,kHalf-2);
        const double gx=static_cast<double>(rule_.transform(id(x+1,z))[3])
                      -static_cast<double>(rule_.transform(id(x-1,z))[3]);
        const double gz=static_cast<double>(rule_.transform(id(x,z+1))[3])
                      -static_cast<double>(rule_.transform(id(x,z-1))[3]);
        const double t=(static_cast<double>(ticks_)+k*127.)*0.024;
        b.velocity.x=std::clamp(b.velocity.x*.988+gx*.012+std::sin(t)*.006,-1.6,1.6);
        b.velocity.z=std::clamp(b.velocity.z*.988+gz*.012+std::cos(t*.91)*.006,-1.6,1.6);
        b.velocity.y=0;
        const double edge=(kHalf-3)*kCellSize;
        if(b.position.x > edge)b.velocity.x=-std::abs(b.velocity.x);
        if(b.position.x < -edge)b.velocity.x= std::abs(b.velocity.x);
        if(b.position.z > edge)b.velocity.z=-std::abs(b.velocity.z);
        if(b.position.z < -edge)b.velocity.z= std::abs(b.velocity.z);
        b.position.y=1.2; // flat local collision layer; terrain visualization is separate.
        physics_.set_body(k,b);
      }
      physics_.step(.05);
      for(auto& pulse:pulses_)pulse.age++;
      pulses_.erase(std::remove_if(pulses_.begin(),pulses_.end(),
          [](const Pulse& p){return p.age>64;}),pulses_.end());
      ++ticks_;
      if(ticks_%24==0 && scans_.size()<kMaxScans) {
        for(std::size_t k=0;k<std::min<std::size_t>(3,physics_.bodies().size());++k){
          const auto& pos=physics_.bodies()[k].position;
          const int x=std::clamp(logical_coord(pos.x),-kHalf,kHalf-1);
          const int z=std::clamp(logical_coord(pos.z),-kHalf,kHalf-1);
          if(reveal_region(x,z,3)>0){
            // Count new region before it becomes visible.
            const std::size_t delta=reveal_region(x,z,3);
            scans_.push_back({x,z,3});
            explored_+=delta;
          }
        }
      }
    }
    ++revision_;
  }

  double simulated_quantum_field() const {
    return ether_.core().probability_one("2"); // 0 or 1, never sampled RNG.
  }
  double wave(int x,int z) const noexcept {
    double value=0.;
    for(const auto& pulse:pulses_){
      const double d=std::hypot(static_cast<double>(x)-pulse.x,
                                static_cast<double>(z)-pulse.z);
      const double front=.28*pulse.age;
      const double band=std::abs(d-front);
      value+=std::exp(-band*1.65)*std::exp(-.033*pulse.age);
    }
    return std::min(1.0,value);
  }

  const VisualScene& rules() const noexcept{return rule_;}
  const QuantumRuntime& ether() const noexcept{return ether_;}
  const RulePhysics& physics() const noexcept{return physics_;}
  const std::vector<Region>& scans() const noexcept{return scans_;}
  const std::vector<Pulse>& pulses() const noexcept{return pulses_;}

  Stats stats() const {
    const auto q=ether_.core().stats();
    return {seed_,center_x_,center_z_,selected_x_,selected_z_,angle_,zoom_,
            ticks_,revision_,scans_.size(),explored_,
            ether_.memory().stats().overrides,physics_.bodies().size(),
            pulses_.size(),q.active_qubits,q.components,simulated_quantum_field()};
  }

  // Snapshots include both canonical Ether owners, actual body velocities,
  // the compact scan coverage rule and all field impulses. Import is strict
  // and applied to a fresh candidate by JNI: invalid saves cannot mutate
  // the user's existing running simulation.
  std::string save() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out<<std::setprecision(17);
    out<<"ETHER_WORLD_V1\n";
    out<<seed_<<' '<<center_x_<<' '<<center_z_<<' '
       <<selected_x_<<' '<<selected_z_<<' '<<angle_<<' '<<zoom_<<' '
       <<ticks_<<' '<<revision_<<'\n';
    out<<scans_.size()<<'\n';
    for(const auto& region:scans_)
      out<<region.x<<' '<<region.z<<' '<<region.radius<<'\n';
    out<<pulses_.size()<<'\n';
    for(const auto& pulse:pulses_)
      out<<pulse.x<<' '<<pulse.z<<' '<<pulse.age<<'\n';
    const auto& bodies=physics_.bodies();
    out<<bodies.size()<<'\n';
    for(const auto& b:bodies)
      out<<b.position.x<<' '<<b.position.y<<' '<<b.position.z<<' '
         <<b.velocity.x<<' '<<b.velocity.y<<' '<<b.velocity.z<<' '
         <<b.inverse_mass<<' '<<b.radius<<' '<<b.restitution<<'\n';
    const auto core=ether_.core_snapshot(),mem=ether_.memory_snapshot();
    out<<"CORE "<<core.size()<<'\n'<<core
       <<"MEM "<<mem.size()<<'\n'<<mem;
    const std::string serialized=out.str();
    if(serialized.size()>kMaxSaveBytes)
      throw std::length_error("world save exceeds 1 MiB");
    return serialized;
  }

  static std::unique_ptr<EtherWorld> load(const std::string& save) {
    if(save.empty()||save.size()>kMaxSaveBytes)
      throw std::invalid_argument("World save missing or exceeds size budget");
    std::istringstream in(save);
    in.imbue(std::locale::classic());
    std::string magic;
    require(static_cast<bool>(std::getline(in,magic)) && magic=="ETHER_WORLD_V1",
            "World save version is invalid");
    int seed=0,cx=0,cz=0,sx=0,sz=0;
    double yaw=0,zoom=0;
    std::uint64_t ticks=0,revision=0;
    require(static_cast<bool>(in>>seed>>cx>>cz>>sx>>sz>>yaw>>zoom>>ticks>>revision),
            "World header missing");
    require(valid_coord(cx,cz)&&valid_coord(sx,sz)&&
            std::isfinite(yaw)&&yaw>=0&&yaw<=6.283185308 &&
            std::isfinite(zoom)&&zoom>=9&&zoom<=30 &&
            ticks<=100000000ULL&&revision<=100000000ULL,
            "World header out of range");
    auto result=std::make_unique<EtherWorld>(seed);
    result->center_x_=cx;result->center_z_=cz;
    result->selected_x_=sx;result->selected_z_=sz;
    result->angle_=yaw;result->zoom_=zoom;
    result->ticks_=ticks;result->revision_=revision;
    result->scans_.clear();result->explored_=0;

    std::size_t count=0;
    require(static_cast<bool>(in>>count)&&count<=kMaxScans,"Invalid scan count");
    for(std::size_t k=0;k<count;++k){
      Region p;
      require(static_cast<bool>(in>>p.x>>p.z>>p.radius),"Truncated scan");
      require(valid_coord(p.x,p.z)&&(p.radius==kScanRadius||p.radius==3),
              "Invalid scan coordinates");
      const std::size_t delta=result->reveal_region(p.x,p.z,p.radius);
      require(delta>0,"World scan not canonical");
      result->scans_.push_back(p);
      result->explored_+=delta;
    }

    require(static_cast<bool>(in>>count)&&count<=kMaxPulses,
            "Invalid pulse count");
    result->pulses_.clear();
    for(std::size_t k=0;k<count;++k){
      Pulse p;
      require(static_cast<bool>(in>>p.x>>p.z>>p.age),"Truncated pulse");
      require(std::isfinite(p.x)&&std::isfinite(p.z)&&
              std::abs(p.x)<=kHalf&&std::abs(p.z)<=kHalf &&
              p.age>=0&&p.age<=64,"Invalid pulse state");
      result->pulses_.push_back(p);
    }
    require(static_cast<bool>(in>>count)&&count<=kMaxProbes&&count>0,
            "Invalid probe count");
    result->physics_.clear();
    for(std::size_t k=0;k<count;++k){
      RulePhysics::Body b;
      require(static_cast<bool>(in>>b.position.x>>b.position.y>>b.position.z
                >>b.velocity.x>>b.velocity.y>>b.velocity.z
                >>b.inverse_mass>>b.radius>>b.restitution),"Truncated probe");
      require(finite(b.position.x)&&finite(b.position.y)&&finite(b.position.z)
           &&finite(b.velocity.x)&&finite(b.velocity.y)&&finite(b.velocity.z)
           &&std::abs(b.position.x)<kHalf*kCellSize
           &&std::abs(b.position.z)<kHalf*kCellSize
           &&b.inverse_mass>0&&b.inverse_mass<=1
           &&b.radius>0&&b.radius<=2
           &&b.restitution>=0&&b.restitution<=1,"Invalid probe values");
      result->physics_.add(b);
    }
    std::string keyword;
    std::size_t size=0;
    require(static_cast<bool>(in>>keyword>>size)&&keyword=="CORE"
            &&size<=kMaxSaveBytes,"Invalid core length");
    require(in.get()=='\n',"Invalid core separator");
    std::string core(size,'\0');
    require(static_cast<bool>(in.read(core.data(),static_cast<std::streamsize>(size))),
            "Truncated core");
    require(static_cast<bool>(in>>keyword>>size)&&keyword=="MEM"
            &&size<=kMaxSaveBytes,"Invalid memory length");
    require(in.get()=='\n',"Invalid memory separator");
    std::string mem(size,'\0');
    require(static_cast<bool>(in.read(mem.data(),static_cast<std::streamsize>(size))),
            "Truncated Ether memory");
    require(in.peek()==std::char_traits<char>::eof(),"World save trailing bytes");
    result->ether_.restore(core,mem);
    // Stored spatial edits must stay deterministic and within the data format.
    require(result->ether_.memory().stats().overrides<=4096,
            "World edit budget exceeded");
    return result;
  }

 private:
  static void require(bool valid,const char* message) {
    if(!valid)throw std::invalid_argument(message);
  }
  static bool finite(double value) noexcept{return std::isfinite(value);}
  static void require_coord(int x,int z) {
    if(!valid_coord(x,z))throw std::out_of_range("world coordinates outside -512..511");
  }
  static InfiniteCore::Limits quantum_limits() {
    InfiniteCore::Limits limits;
    limits.max_active_qubits=32;
    limits.max_component_qubits=32;
    limits.max_state_bytes=1<<20;
    limits.max_query_terms=32;
    return limits;
  }
  static InverseMemory::Limits memory_limits() {
    InverseMemory::Limits limits;
    limits.max_address_digits=30;
    limits.max_overrides=4096;
    limits.max_quantum_bindings=0;
    limits.max_materialize_bytes=4096;
    return limits;
  }
  static RulePhysics::Rules physical_rules() {
    RulePhysics::Rules rules;
    rules.uniform_acceleration={0.,0.,0.};
    rules.max_bodies=kMaxProbes;
    rules.max_substep=0.05;
    rules.max_substeps=16;
    rules.max_pair_tests=4096;
    return rules;
  }
  std::size_t region_cell_count(int x,int z,int radius) const {
    std::size_t count=0;
    for(int dz=-radius;dz<=radius;++dz)
      for(int dx=-radius;dx<=radius;++dx)
        if(dx*dx+dz*dz<=radius*radius&&valid_coord(x+dx,z+dz))
          ++count;
    return count;
  }
  std::size_t reveal_region(int x,int z,int radius) const {
    std::size_t result=0;
    for(int dz=-radius;dz<=radius;++dz)
      for(int dx=-radius;dx<=radius;++dx)
        if(dx*dx+dz*dz<=radius*radius&&
           valid_coord(x+dx,z+dz)&&!is_revealed(x+dx,z+dz))
           ++result;
    return result;
  }
  void spawn_probes() {
    for(std::size_t k=0;k<6;++k){
      RulePhysics::Body b;
      const double a=(k+1)*6.283185307179586/6.0;
      b.position={4.6*std::cos(a),1.2,4.6*std::sin(a)};
      b.velocity={0.24*std::cos(a+1.),0,0.24*std::sin(a+1.)};
      b.inverse_mass=1;
      b.radius=.35;
      b.restitution=.65;
      physics_.add(b);
    }
  }

  std::int32_t seed_;
  VisualScene rule_;
  QuantumRuntime ether_;
  RulePhysics physics_;
  std::vector<Region> scans_;
  std::vector<Pulse> pulses_;
  int center_x_=0,center_z_=0;
  int selected_x_=0,selected_z_=0;
  double angle_=0.8,zoom_=17.0;
  std::uint64_t ticks_=0,revision_=0;
  std::size_t explored_=0;
};

} // namespace mundo_x
