#include "mundo_x/physics_world.hpp"
#include <cassert>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace {
bool same(const mundo_x::PhysicsBody& a,const mundo_x::PhysicsBody& b) {
  return a.x==b.x&&a.y==b.y&&a.vx==b.vx&&a.vy==b.vy&&
         a.mass==b.mass&&a.radius==b.radius&&a.fixed==b.fixed;
}
double kinetic(const mundo_x::PhysicsWorld& w) {
  double e=0;for(const auto& b:w.bodies)if(!b.fixed)e+=.5*b.mass*(b.vx*b.vx+b.vy*b.vy);return e;
}
}

int main() {
  // Equal-mass elastic head-on collision: exchange velocities, conserve
  // momentum and kinetic energy.
  mundo_x::PhysicsWorld w;
  w.gravity_y=0;w.restitution=1;
  w.bodies={{0,1,1,0,1,0.5,false},{1,1,-1,0,1,0.5,false}};
  const double energy=kinetic(w);
  w.step(0.01);
  assert(w.bodies[0].vx < -0.99 && w.bodies[1].vx > 0.99);
  assert(std::abs(w.bodies[0].vx+w.bodies[1].vx)<1e-10);
  assert(std::abs(kinetic(w)-energy)<1e-10);

  // Invalid timesteps and invalid bodies are rejected before any mutation.
  auto before=w.bodies;
  try {w.step(-1);assert(false);} catch(const std::invalid_argument&) {}
  assert(same(w.bodies[0],before[0])&&same(w.bodies[1],before[1]));
  w.bodies[1].mass=std::numeric_limits<double>::quiet_NaN();
  before=w.bodies;
  try {w.step(.01);assert(false);} catch(const std::invalid_argument&) {}
  assert(same(w.bodies[0],before[0]));
  assert(std::isnan(w.bodies[1].mass)&&std::isnan(before[1].mass));
  w.bodies[1].mass=1;

  // Gravity and ground contact never leave a body below the plane.
  mundo_x::PhysicsWorld falling;
  falling.restitution=.25;
  falling.bodies={{0,2,0,0,1,0.5,false}};
  for(int i=0;i<200;++i)falling.step(0.01);
  assert(falling.bodies[0].y>=0.5);

  // Fixed bodies stay fixed while participating in collision response.
  mundo_x::PhysicsWorld wall;
  wall.gravity_y=0;wall.restitution=1;
  wall.bodies={{0,1,0,0,1,0.5,true},{-.9,1,1,0,1,0.5,false}};
  wall.step(.01);
  assert(wall.bodies[0].x==0&&wall.bodies[0].y==1);
  assert(wall.bodies[1].vx<0);

  // Same inputs and fixed timestep produce bit-for-bit deterministic state.
  mundo_x::PhysicsWorld a,b;
  a.gravity_y=b.gravity_y=-9.81;
  a.restitution=b.restitution=.6;
  a.bodies={{-1,3,2,0,2,.4,false},{1,2,-1,.5,1,.5,false}};
  b=a;
  for(int i=0;i<500;++i){a.step(.005);b.step(.005);}
  assert(a.bodies.size()==b.bodies.size());
  for(std::size_t i=0;i<a.bodies.size();++i)assert(same(a.bodies[i],b.bodies[i]));
}
