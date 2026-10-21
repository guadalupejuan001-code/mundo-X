#include "mundo_x/physics_world.hpp"
#include <cstdlib>
#include <iostream>
#include <new>

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
int main(int argc,char**argv) {
  mundo_x::PhysicsWorld world;
  world.gravity_y=0;
  for(unsigned i=0;i<600;++i) world.bodies.push_back({double(i)*3,20,1,0,1,.5,false});
  const auto before=world.bodies;
  fail_after=argc>1 ? std::strtol(argv[1],nullptr,10) : 3;
  bool rejected=false;
  try {world.step(.01);} catch(const std::bad_alloc&) {rejected=true;}
  fail_after=-1;
  std::cout<<"allocation_rejected="<<rejected<<" first_body_unchanged="<<(world.bodies[0].x==before[0].x)<<std::endl;
  world.step(.01);
  std::cout<<"retry_ok="<<(world.bodies[0].x==.01)<<std::endl;
}
