// Differential workload compiled by system_optimization_check.py against the
// pinned pre-change algorithms and the current code in the same process.
#include "mundo_x/physics_world.hpp"
#include "mundo_x/micro_lab.hpp"
#include "mundo_x/infinite_session.hpp"
#include "baseline_physics.hpp"
#include "baseline_micro.hpp"
#include "baseline_observer.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>
#include <atomic>
#include <sys/resource.h>

using Clock=std::chrono::steady_clock;
static std::size_t checks=0;
static void require(bool value,const char* message) {++checks;if(!value)throw std::runtime_error(message);}
static bool bits(double a,double b){return std::memcmp(&a,&b,sizeof(a))==0;}
static double median(std::vector<double> v){std::sort(v.begin(),v.end());return v[v.size()/2];}
template<class F> double ms(F f) {auto t=Clock::now();f();return std::chrono::duration<double,std::milli>(Clock::now()-t).count();}
template<class A,class B> void same_bodies(const A& a,const B& b) {
 require(a.size()==b.size(),"body count");
 for(std::size_t i=0;i<a.size();++i) {
  const auto& x=a[i];const auto& y=b[i];
  require(bits(x.x,y.x)&&bits(x.y,y.y)&&bits(x.vx,y.vx)&&bits(x.vy,y.vy)&&
          bits(x.mass,y.mass)&&bits(x.radius,y.radius)&&x.fixed==y.fixed,"physics bits differ");
 }
}
void physics() {
 using World=mundo_x::PhysicsWorld;using Old=physics_reference::PhysicsWorld;
 std::mt19937_64 rng(42);std::uniform_real_distribution<double> unit(0,1);
 std::size_t contacts=0,fallbacks=0;
 for(unsigned c=0;c<128;++c) {
  World w;Old old;w.gravity_y=old.gravity_y=c%2?-9.81:0;w.restitution=old.restitution=unit(rng);
  const auto n=(c%2?std::size_t{513}:std::size_t{65})+(c%5)*43;
  const double extent=c%4==0?.1:c%4==1?10:100;
  for(std::size_t i=0;i<n;++i) {
   mundo_x::PhysicsBody b{(unit(rng)-.5)*extent,1+unit(rng)*extent,
     (unit(rng)-.5)*20,(unit(rng)-.5)*20,.1+unit(rng)*10,.05+unit(rng),i%9==0};
   if(c%8==0&&i==0)b.radius=extent;
   if(c%13==0){b.x=double(i)*.8;b.y=10;b.vx=0;b.vy=0;b.radius=.5;}
   if(c%17==0){b.x=0;b.y=10;b.vx=0;b.vy=0;}
   w.bodies.push_back(b);old.bodies.push_back({b.x,b.y,b.vx,b.vy,b.mass,b.radius,b.fixed});
  }
  for(unsigned k=0;k<12;++k) {
   const double dt=.002+unit(rng)*.02;old.step(dt);w.step(dt);same_bodies(w.bodies,old.bodies);
   contacts+=w.step_stats().contacts;fallbacks+=w.step_stats().dense_fallback;
  }
 }
 require(contacts>100&&fallbacks>0,"adversarial contact coverage");
 World w;w.bodies={{0,1,0,0,1,.5,false}};const auto saved=w.bodies;
 for(int mode=0;mode<4;++mode) {
  w.gravity_y=mode==0?std::numeric_limits<double>::infinity():-9.81;
  w.restitution=mode==1?1.1:.5;
  w.bodies=saved;if(mode==2)w.bodies[0].vx=std::numeric_limits<double>::max();
  const auto input=w.bodies;bool rejected=false;
  try{w.step(mode==3?std::numeric_limits<double>::quiet_NaN():.05);}catch(const std::exception&){rejected=true;}
  require(rejected,"invalid physics accepted");same_bodies(w.bodies,input);
 }
 std::cout<<"{\"checks\":"<<checks<<",\"contacts\":"<<contacts<<",\"dense_fallbacks\":"<<fallbacks<<",\"benchmarks\":[";
 bool comma=false;
 for(std::size_t n:{256,4096,16384,32768}) {
  World a;Old b;a.gravity_y=b.gravity_y=-9.81;
  for(std::size_t i=0;i<n;++i) {
   const double x=double((i*7919)%n)*3,y=10+double(i%17)*3;
   a.bodies.push_back({x,y,.1,-.3,1,.5,false});b.bodies.push_back({x,y,.1,-.3,1,.5,false});
  }
  std::vector<double> before,after;
  for(unsigned k=0;k<7;++k) {
   double p,q;if(k%2){q=ms([&]{a.step(.01);});p=ms([&]{b.step(.01);});}
   else {p=ms([&]{b.step(.01);});q=ms([&]{a.step(.01);});}
   same_bodies(a.bodies,b.bodies);if(k){before.push_back(p);after.push_back(q);}
  }
  if(comma)std::cout<<',';
  comma=true;
  std::cout<<"{\"bodies\":"<<n<<",\"before_ms\":"<<median(before)<<",\"after_ms\":"<<median(after)
    <<",\"possible_pairs\":"<<n*(n-1)/2<<",\"tested_pairs\":"<<a.step_stats().pair_tests
    <<",\"node_tests\":"<<a.step_stats().node_tests<<",\"scratch_bytes\":"<<a.step_stats().scratch_bytes<<'}';
 }
 // A larger fresh world: bounded target, no claim that old quadratic path ran.
 World large;for(std::size_t i=0;i<100000;++i)large.bodies.push_back({double(i%1000)*3,10+double(i/1000)*3,0,0,1,.5,false});
 const double large_ms=ms([&]{large.step(.01);});
 std::cout<<"],\"large_target\":{\"bodies\":100000,\"ms\":"<<large_ms<<",\"scratch_bytes\":"<<large.step_stats().scratch_bytes<<"}}\n";
}
void observer() {
 std::mt19937_64 rng(41);double total=0;std::cout<<"{\"benchmarks\":[";bool comma=false;
 for(std::size_t n:{64,256,1000})for(unsigned kind=0;kind<3;++kind) {
  mundo_x::CompactCore core(n);
  if(kind==1) { core.hadamard(0);for(std::size_t q=1;q<n;++q)core.cnot(0,q); }
  if(kind==2)for(std::size_t k=0;k<8*n;++k) {
    auto a=rng()%n,b=rng()%n;core.hadamard(a);core.phase_s(b);if(a!=b)core.cnot(a,b);
  }
  mundo_x_bridge::PauliObserver now(core);observer_reference::PauliObserver old(core);
  std::vector<std::string> words;
  for(unsigned k=0;k<2048;++k) {
   std::string p(n,'I');for(unsigned t=0;t<1+k%9;++t)p[rng()%n]="XYZ"[rng()%3];
   if(k%13==0) for(char& c:p)c="IXYZ"[rng()%4];
   words.push_back(p);
  }
  std::vector<mundo_x_bridge::Pauli> np;std::vector<observer_reference::Pauli> op;
  for(const auto& p:words){np.push_back(mundo_x_bridge::Pauli::parse(p,n));op.push_back(observer_reference::Pauli::parse(p,n));}
  for(unsigned k=0;k<words.size();++k)require(now.expectation(np[k])==old.expectation(op[k]),"observer difference");
  std::vector<double> before,after;
  for(unsigned k=0;k<9;++k) {
   auto f=[&]{for(const auto& p:op)total+=old.expectation(p);};auto g=[&]{for(const auto& p:np)total+=now.expectation(p);};
   double a,b;if(k%2){b=ms(g);a=ms(f);}else{a=ms(f);b=ms(g);}if(k){before.push_back(a);after.push_back(b);}
  }
  if(comma)std::cout<<',';
  comma=true;
  std::cout<<"{\"qubits\":"<<n<<",\"kind\":"<<kind<<",\"queries\":2048,\"before_ms\":"<<median(before)
    <<",\"after_ms\":"<<median(after)<<",\"before_payload\":"<<old.payload_bytes()<<",\"after_payload\":"<<now.payload_bytes()<<'}';
 }
 // Full live-session route, different sparse queries, writer + reader stress.
 mundo_x::InfiniteSession session;session.apply({{'H',"0",{}},{'C',"0","1"}});
 const auto saved=session.snapshot();std::atomic<bool> failed{false};
 auto reader=[&]{for(unsigned i=0;i<300;++i){const auto r=session.query_batch({{1,{{"0",'Z'},{"1",'Z'}}},{1,{{"0",'X'},{"1",'X'}}}});if(r.values[0]!=1||std::abs(r.values[1])!=1)failed=true;}};
 std::thread a(reader),b(reader);for(unsigned i=0;i<300;++i)session.apply({{'Z',"0",{}}});a.join();b.join();
 require(!failed,"torn observation");session.restore(saved);require(session.snapshot()==saved,"restore state");
 mundo_x::CompactCore small(3);mundo_x_bridge::PauliObserver s(small);
 auto bad=mundo_x_bridge::Pauli::parse("III",3);bad.x[0]=1ULL<<63;bool rejected=false;
 try{s.expectation(bad);}catch(const std::invalid_argument&){rejected=true;}require(rejected,"packed padding accepted");
 std::cout<<"],\"checks\":"<<checks<<",\"checksum\":"<<total<<"}\n";
}
template<class Lab> auto vertices(Lab& lab,int mode) {
 lab.mode(mode);std::vector<float> v(Lab::max_vertices*Lab::stride);v.resize(lab.vertices(v.data(),Lab::max_vertices)*Lab::stride);return v;
}
void micro() {
 std::cout<<"{\"benchmarks\":[";bool comma=false;std::size_t floats=0;
 for(auto count:{256,4096,16384})for(bool coupled:{false,true}) {
  mundo_x::MicroLab a(42);micro_reference::MicroLab b(42);
  a.particle_count(count);b.particle_count(count);a.coupled(coupled);b.coupled(coupled);
  std::vector<double> before,after;
  for(unsigned k=0;k<12;++k) {
   if(k%3==0){a.inject(2,.2f,-.4f);b.inject(2,.2f,-.4f);a.gate(1,2);b.gate(1,2);}
   double p,q;if(k%2){q=ms([&]{a.advance(6);});p=ms([&]{b.advance(6);});}
   else{p=ms([&]{b.advance(6);});q=ms([&]{a.advance(6);});}
   for(int mode=0;mode<3;++mode){auto av=vertices(a,mode),bv=vertices(b,mode);floats+=av.size();require(av.size()==bv.size()&&std::memcmp(av.data(),bv.data(),av.size()*sizeof(float))==0,"micro vertex bits differ");}
   require(a.json()==b.json(),"micro state differs");if(k){before.push_back(p);after.push_back(q);}
  }
  if(comma)std::cout<<',';
  comma=true;
  std::cout<<"{\"particles\":"<<count<<",\"coupled\":"<<(coupled?"true":"false")
   <<",\"steps_per_sample\":6,\"before_ms\":"<<median(before)<<",\"after_ms\":"<<median(after)<<'}';
 }
 std::cout<<"],\"checks\":"<<checks<<",\"compared_floats\":"<<floats<<"}\n";
}
int main(int argc,char** argv) {try{
 if(argc!=2)throw std::invalid_argument("physics|observer|micro");
 std::string mode=argv[1];if(mode=="physics")physics();else if(mode=="observer")observer();else if(mode=="micro")micro();else throw std::invalid_argument("mode");
rusage usage{};getrusage(RUSAGE_SELF,&usage);std::cerr<<"RSS_KIB "<<usage.ru_maxrss<<'\n';
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
