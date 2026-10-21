// Host-only combined-load fixture. These models have separate authoritative
// states; this does not pretend PhysicsWorld is already the 3D scene geometry.
#include "mundo_x/physics_world.hpp"
#include "mundo_x/micro_lab.hpp"
#include <cstring>
#include <string>
struct Workload {
 mundo_x::PhysicsWorld physics;
 mundo_x::MicroLab micro{42};
 std::vector<float> vertices;
 Workload():vertices(mundo_x::MicroLab::max_vertices*mundo_x::MicroLab::stride) {
  for(unsigned i=0;i<4096;++i)physics.bodies.push_back({double(i%64)*3,10+double(i/64)*3,.1,-.2,1,.5,false});
  micro.particle_count(16384);
 }
};
static thread_local std::string error;
extern "C" {
const char* mxw_error(){return error.c_str();}
void* mxw_create(){try{return new Workload;}catch(const std::exception& e){error=e.what();return nullptr;}}
void mxw_destroy(void* p){delete static_cast<Workload*>(p);}
int mxw_step(void* p,std::uint64_t* checksum){try{
 auto& w=*static_cast<Workload*>(p);w.physics.step(1./60);w.micro.advance(1);
 std::uint64_t h=1469598103934665603ULL;
 auto hash=[&](const void* p,std::size_t n){const auto* b=static_cast<const unsigned char*>(p);while(n--){h^=*b++;h*=1099511628211ULL;}};
 for(const auto& b:w.physics.bodies){hash(&b.x,sizeof(b.x));hash(&b.y,sizeof(b.y));hash(&b.vx,sizeof(b.vx));hash(&b.vy,sizeof(b.vy));}
 for(int mode=0;mode<3;++mode){w.micro.mode(mode);const auto n=w.micro.vertices(w.vertices.data(),mundo_x::MicroLab::max_vertices);hash(w.vertices.data(),n*mundo_x::MicroLab::stride*sizeof(float));}
 *checksum=h;return 0;
}catch(const std::exception& e){error=e.what();return -1;}}
}
