#include "mundo_x/rule_physics.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

namespace {
using Physics = mundo_x::RulePhysics;
std::size_t checks = 0;
void require(bool ok, const char* message) { ++checks; if (!ok) throw std::runtime_error(message); }
void near(double a, double b, double eps, const char* message) {
  ++checks; if (!std::isfinite(a) || std::abs(a-b) > eps) throw std::runtime_error(message);
}
}

int main() {
  try {
    {
      Physics::Rules rules; rules.max_substep = 0.01;
      Physics world(rules);
      Physics::Body body; body.radius=.1; world.add(body);
      world.step(1.0);
      near(world.bodies()[0].velocity.y, -9.80665, 1e-10, "uniform gravity velocity");
      // Semi-implicit Euler with dt=.01 has the known discrete position sum.
      near(world.bodies()[0].position.y, -9.80665*0.01*0.01*5050.0, 1e-9,
           "uniform gravity position");
    }
    {
      Physics::Rules rules; rules.uniform_acceleration={0,0,0}; rules.max_substep=1.0;
      Physics world(rules);
      Physics::Body a,b; a.position={-.49,0,0};b.position={.49,0,0};
      a.velocity={1,0,0};b.velocity={-1,0,0};a.radius=b.radius=.5;
      a.restitution=b.restitution=1;
      world.add(a);world.add(b);world.step(.01);
      near(world.bodies()[0].velocity.x,-1,1e-12,"elastic collision a");
      near(world.bodies()[1].velocity.x, 1,1e-12,"elastic collision b");
      require(world.bodies()[1].position.x-world.bodies()[0].position.x>=.999999,
              "collision penetration correction");
    }
    {
      Physics::Rules rules; rules.uniform_acceleration={0,0,0};rules.pair_gravity=1;
      rules.softening=1e-6;rules.max_substep=.001;
      Physics world(rules);
      Physics::Body a,b; a.position={-1,0,0};b.position={1,0,0};a.radius=b.radius=.01;
      world.add(a);world.add(b);world.step(.01);
      near(world.bodies()[0].velocity.x,-world.bodies()[1].velocity.x,1e-12,
           "pair gravity momentum symmetry");
      require(world.bodies()[0].velocity.x>0,"pair gravity direction");
    }
    {
      Physics::Rules rules; rules.uniform_acceleration={0,-10,0};rules.max_substep=.01;
      Physics world(rules);
      Physics::Body fixed; fixed.inverse_mass=0; fixed.position={1,2,3}; world.add(fixed);
      world.step(1);
      near(world.bodies()[0].position.y,2,0,"kinematic body moved");
    }
    {
      Physics::Rules rules; rules.max_substep=.01;rules.max_substeps=4;
      Physics world(rules);world.add({});
      bool rejected=false;try{world.step(.1);}catch(const std::length_error&){rejected=true;}
      require(rejected,"substep budget ignored");
    }
    std::cout<<"{\"suite\":\"rule_physics\",\"ok\":true,\"checks\":"<<checks<<"}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr<<"rule_physics: "<<e.what()<<"\n"; return 1;
  }
}
