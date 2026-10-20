#include "mundo_x/physics_world.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
std::size_t checks=0;
void require(bool condition,const char* message) {
  ++checks;
  if(!condition) throw std::runtime_error(message);
}
bool same(const mundo_x::PhysicsBody& a,const mundo_x::PhysicsBody& b) {
  return a.x==b.x&&a.y==b.y&&a.vx==b.vx&&a.vy==b.vy&&
         a.mass==b.mass&&a.radius==b.radius&&a.fixed==b.fixed;
}
double kinetic(const mundo_x::PhysicsWorld& w) {
  double e=0;for(const auto& b:w.bodies)if(!b.fixed)e+=.5*b.mass*(b.vx*b.vx+b.vy*b.vy);return e;
}
}

int main() {
  try {
    mundo_x::PhysicsWorld w;
    w.gravity_y=0;w.restitution=1;
    w.bodies={{0,1,1,0,1,0.5,false},{1,1,-1,0,1,0.5,false}};
    const double energy=kinetic(w);
    w.step(0.01);
    require(w.bodies[0].vx < -0.99 && w.bodies[1].vx > 0.99,
            "elastic equal-mass collision did not exchange velocities");
    require(std::abs(w.bodies[0].vx+w.bodies[1].vx)<1e-10,
            "elastic collision did not conserve momentum");
    require(std::abs(kinetic(w)-energy)<1e-10,
            "elastic collision did not conserve kinetic energy");

    auto before=w.bodies;
    bool rejected=false;
    try {w.step(-1);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"negative timestep accepted");
    require(same(w.bodies[0],before[0])&&same(w.bodies[1],before[1]),
            "invalid timestep mutated world");

    w.bodies[1].mass=std::numeric_limits<double>::quiet_NaN();
    before=w.bodies;rejected=false;
    try {w.step(.01);} catch(const std::invalid_argument&) {rejected=true;}
    require(rejected,"non-finite body accepted");
    require(same(w.bodies[0],before[0]),"invalid body mutated earlier body");
    require(std::isnan(w.bodies[1].mass)&&std::isnan(before[1].mass),
            "invalid body validation changed source data");
    w.bodies[1].mass=1;

    mundo_x::PhysicsWorld falling;
    falling.restitution=.25;
    falling.bodies={{0,2,0,0,1,0.5,false}};
    for(int i=0;i<200;++i)falling.step(0.01);
    require(falling.bodies[0].y>=0.5,"ground contact left body below plane");

    mundo_x::PhysicsWorld wall;
    wall.gravity_y=0;wall.restitution=1;
    wall.bodies={{0,1,0,0,1,0.5,true},{-.9,1,1,0,1,0.5,false}};
    wall.step(.01);
    require(wall.bodies[0].x==0&&wall.bodies[0].y==1,"fixed body moved");
    require(wall.bodies[1].vx<0,"dynamic body did not bounce from fixed body");

    mundo_x::PhysicsWorld a,b;
    a.gravity_y=b.gravity_y=-9.81;
    a.restitution=b.restitution=.6;
    a.bodies={{-1,3,2,0,2,.4,false},{1,2,-1,.5,1,.5,false}};
    b=a;
    for(int i=0;i<500;++i){a.step(.005);b.step(.005);}
    require(a.bodies.size()==b.bodies.size(),"deterministic worlds changed size");
    for(std::size_t i=0;i<a.bodies.size();++i)
      require(same(a.bodies[i],b.bodies[i]),"fixed-step simulation is not deterministic");

    // A contact between distant IDs must survive spatial ordering and refit.
    mundo_x::PhysicsWorld spatial;spatial.gravity_y=0;spatial.restitution=1;
    for(unsigned i=0;i<600;++i)spatial.bodies.push_back({double(i)*3+100,20,0,0,1,.5,false});
    spatial.bodies.front()={0,20,1,0,1,.5,false};
    spatial.bodies.back()={1,20,-1,0,1,.5,false};
    spatial.step(.01);
    require(spatial.bodies.front().vx==-1&&spatial.bodies.back().vx==1,"spatial contact lost");
    require(spatial.step_stats().pair_tests<16,"sparse index tested unrelated pairs");
    std::reverse(spatial.bodies.begin(),spatial.bodies.end());
    spatial.step(.01);
    require(spatial.bodies.front().vx==1&&spatial.bodies.back().vx==-1,"refit assumed unchanged public ordering");
    spatial.bodies.resize(10);spatial.step(.01);
    require(spatial.bodies.size()==10,"shrinking body set failed");

    mundo_x::PhysicsWorld dense;dense.gravity_y=0;
    dense.bodies.assign(600,{0,20,0,0,1,.5,true});dense.step(.01);
    require(dense.step_stats().dense_fallback,"dense contact cloud missed bounded fallback");
    require(dense.step_stats().pair_tests==600*599/2,"dense fallback changed pair order/count");

    before=spatial.bodies;spatial.restitution=std::numeric_limits<double>::quiet_NaN();
    rejected=false;try{spatial.step(.01);}catch(const std::invalid_argument&){rejected=true;}
    require(rejected,"non-finite material accepted");
    for(std::size_t i=0;i<before.size();++i)require(same(before[i],spatial.bodies[i]),"invalid material mutated world");

    std::cout << "{\"ok\":true,\"checks\":" << checks << "}\n";
    return 0;
  } catch(const std::exception& error) {
    std::cerr << "physics_world: " << error.what() << '\n';
    return 1;
  }
}
