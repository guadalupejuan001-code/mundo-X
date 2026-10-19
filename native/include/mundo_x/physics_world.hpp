#pragma once
#include <cmath>
#include <cstddef>
#include <stdexcept>
#include <vector>

namespace mundo_x {
// Independent, deterministic foundation for a finite set of dynamic bodies.
// Not coupled to the quantum register or rendering yet.
struct PhysicsBody {
  double x=0,y=0,vx=0,vy=0,mass=1,radius=0.5;
  bool fixed=false;
};
class PhysicsWorld {
 public:
  double gravity_y=-9.81;
  double restitution=0.5;
  std::vector<PhysicsBody> bodies;
  void step(double dt) {
    if (!(dt>0 && dt<=0.05) || !std::isfinite(dt))
      throw std::invalid_argument("invalid physics timestep");
    for(const auto& b:bodies)
      if(!std::isfinite(b.x)||!std::isfinite(b.y)||!std::isfinite(b.vx)||
         !std::isfinite(b.vy)||!std::isfinite(b.mass)||b.mass<=0||
         !std::isfinite(b.radius)||b.radius<=0)
        throw std::invalid_argument("invalid body");
    for(auto& b:bodies) if(!b.fixed) {
      b.vy+=gravity_y*dt;
      b.x+=b.vx*dt; b.y+=b.vy*dt;
    }
    // Ground contact: center y = radius. This is a discrete collision model.
    for(auto& b:bodies) if(!b.fixed && b.y<b.radius) {
      b.y=b.radius;
      if(b.vy<0) b.vy=-b.vy*restitution;
    }
    // Pairwise circles, impulse response with inverse-mass weighting.
    for(std::size_t i=0;i<bodies.size();++i)
      for(std::size_t j=i+1;j<bodies.size();++j) {
        auto& a=bodies[i];auto& b=bodies[j];
        double dx=b.x-a.x,dy=b.y-a.y;
        double dist2=dx*dx+dy*dy,rad=a.radius+b.radius;
        if(dist2>=rad*rad)continue;
        double dist=std::sqrt(dist2),nx=dist>1e-12?dx/dist:1.0,ny=dist>1e-12?dy/dist:0.0;
        double ia=a.fixed?0:1/a.mass,ib=b.fixed?0:1/b.mass,sum=ia+ib;
        if(sum==0)continue;
        double overlap=rad-dist;
        a.x-=nx*overlap*ia/sum;a.y-=ny*overlap*ia/sum;
        b.x+=nx*overlap*ib/sum;b.y+=ny*overlap*ib/sum;
        double relative=(b.vx-a.vx)*nx+(b.vy-a.vy)*ny;
        if(relative>=0)continue;
        double impulse=-(1+restitution)*relative/sum;
        a.vx-=impulse*ia*nx;a.vy-=impulse*ia*ny;
        b.vx+=impulse*ib*nx;b.vy+=impulse*ib*ny;
      }
  }
};
} // namespace mundo_x
