#include "mundo_x/physics_world.hpp"
#include <cassert>
#include <cmath>
#include <stdexcept>
int main() {
  mundo_x::PhysicsWorld w;
  w.gravity_y=0;w.restitution=1;
  w.bodies={{0,1,1,0,1,0.5,false},{1,1,-1,0,1,0.5,false}};
  w.step(0.01);
  assert(w.bodies[0].vx < -0.99 && w.bodies[1].vx > 0.99);
  assert(std::abs(w.bodies[0].vx+w.bodies[1].vx)<1e-10);
  auto before=w.bodies;
  try {w.step(-1);assert(false);} catch(const std::invalid_argument&) {}
  assert(w.bodies[0].x==before[0].x);
  mundo_x::PhysicsWorld falling;
  falling.bodies={{0,2,0,0,1,0.5,false}};
  for(int i=0;i<200;++i)falling.step(0.01);
  assert(falling.bodies[0].y>=0.5);
}
