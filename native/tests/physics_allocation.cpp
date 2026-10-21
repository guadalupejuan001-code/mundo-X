#include "mundo_x/physics_world.hpp"
#include "mundo_x/rule_physics.hpp"
#include <cstdlib>
#include <iostream>
#include <new>
#include <stdexcept>

// Fail one real allocation, then allow recovery. This exercises scratch
// construction and reuse rather than altering the solver's arithmetic.
static long fail_after=-1;
void* operator new(std::size_t size) {
  if(fail_after==0) { fail_after=-1; throw std::bad_alloc(); }
  if(fail_after>0) --fail_after;
  if(void* p=std::malloc(size ? size : 1)) return p;
  throw std::bad_alloc();
}
void operator delete(void* p) noexcept {std::free(p);}
void operator delete(void* p,std::size_t) noexcept {std::free(p);}
void* operator new[](std::size_t size) {return ::operator new(size);}
void operator delete[](void* p) noexcept {::operator delete(p);}
void operator delete[](void* p,std::size_t) noexcept {::operator delete(p);}

static void require(bool value,const char* message) {
  if(!value) throw std::runtime_error(message);
}
static bool same(const mundo_x::PhysicsBody& a,const mundo_x::PhysicsBody& b) {
  return a.x==b.x&&a.y==b.y&&a.vx==b.vx&&a.vy==b.vy&&
         a.mass==b.mass&&a.radius==b.radius&&a.fixed==b.fixed;
}
static bool same(const mundo_x::RulePhysics::Body& a,const mundo_x::RulePhysics::Body& b) {
  return a.position.x==b.position.x&&a.position.y==b.position.y&&a.position.z==b.position.z&&
         a.velocity.x==b.velocity.x&&a.velocity.y==b.velocity.y&&a.velocity.z==b.velocity.z&&
         a.inverse_mass==b.inverse_mass&&a.radius==b.radius&&a.restitution==b.restitution;
}
int main() {
  try {
    unsigned failures=0, checks=0;
    for(long failure=0;failure<10;++failure) {
      mundo_x::PhysicsWorld actual;
      actual.gravity_y=0;actual.restitution=1;
      for(unsigned i=0;i<600;++i)
        actual.bodies.push_back({double(i)*3+100,20,0,0,1,.5,false});
      actual.bodies.front()={0,20,1,0,1,.5,false};
      actual.bodies.back()={1,20,-1,0,1,.5,false};
      const auto before=actual.bodies;
      auto reference=actual;reference.step(.01);
      fail_after=failure;
      bool rejected=false;
      try {actual.step(.01);} catch(const std::bad_alloc&) {rejected=true;}
      fail_after=-1;
      if(rejected) {
        ++failures;
        for(std::size_t i=0;i<before.size();++i) {
          require(same(before[i],actual.bodies[i]),"allocation failure published a partial step");++checks;
        }
        actual.step(.01);
      }
      for(std::size_t i=0;i<before.size();++i) {
        require(same(reference.bodies[i],actual.bodies[i]),"retry changed the physical result");++checks;
      }
      require(actual.step_stats().contacts==reference.step_stats().contacts,
              "retry changed the contact count");++checks;
    }
    require(failures>=4,"allocation fault coverage missed index construction");
    unsigned recovered3d=0;
    for(long failure=0;failure<12;++failure) {
      mundo_x::RulePhysics::Rules rules;rules.uniform_acceleration={0,0,0};
      mundo_x::RulePhysics actual(rules);
      for(unsigned i=0;i<160;++i) {
        mundo_x::RulePhysics::Body body;body.position={double(i)*3,20,0};body.velocity={1,0,0};
        actual.add(body);
      }
      const auto before=actual.bodies();const auto revision=actual.revision();
      auto reference=actual;reference.step(.005);
      fail_after=failure;
      bool rejected=false;
      try {actual.step(.005);}catch(const std::bad_alloc&){rejected=true;}
      fail_after=-1;
      if(rejected) {
        ++recovered3d;require(actual.revision()==revision,"3D allocation failure changed revision");
        for(std::size_t i=0;i<before.size();++i) {
          require(same(before[i],actual.bodies()[i]),"3D allocation failure published a partial step");++checks;
        }
        actual.step(.005);
      }
      for(std::size_t i=0;i<before.size();++i) {
        require(same(reference.bodies()[i],actual.bodies()[i]),"3D recovery changed result");++checks;
      }
    }
    require(recovered3d>=5,"3D allocation fault coverage missed index construction");
    std::cout<<"{\"suite\":\"physics_allocation\",\"ok\":true,\"failures_recovered\":"
             <<failures<<",\"failures_recovered_3d\":"<<recovered3d<<",\"checks\":"<<checks<<"}\n";
    return 0;
  } catch(const std::exception& e) { fail_after=-1;std::cerr<<e.what()<<'\n';return 1; }
}
