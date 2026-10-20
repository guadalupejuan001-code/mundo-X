// Reproducible bounded workload, also compiled unchanged against the audit base.
// Allocation counters measure requested C++ new bytes (not malloc/driver RSS).
#include "mundo_x/infinite_session.hpp"
#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <thread>
#include <iomanip>
#include <cstddef>
#include <new>

namespace allocation {
struct alignas(std::max_align_t) Header { std::size_t size; };
std::atomic<std::size_t> live{0}, peak{0}, count{0}, total{0};
void* get(std::size_t n) {
  if (n > SIZE_MAX-sizeof(Header)) throw std::bad_alloc();
  auto* p=static_cast<Header*>(std::malloc(n+sizeof(Header)));
  if (!p) throw std::bad_alloc();
  p->size=n; count.fetch_add(1); total.fetch_add(n);
  const auto v=live.fetch_add(n)+n; auto old=peak.load();
  while(old<v&&!peak.compare_exchange_weak(old,v)) {}
  return p+1;
}
void release(void* p) noexcept {
  if(!p)return;
  auto* h=static_cast<Header*>(p)-1;
  live.fetch_sub(h->size);std::free(h);
}
}
void* operator new(std::size_t n){return allocation::get(n);}
void* operator new[](std::size_t n){return allocation::get(n);}
void operator delete(void* p) noexcept {allocation::release(p);}
void operator delete[](void* p) noexcept {allocation::release(p);}
void operator delete(void* p,std::size_t) noexcept {allocation::release(p);}
void operator delete[](void* p,std::size_t) noexcept {allocation::release(p);}

using Core=mundo_x::InfiniteCore;
using Session=mundo_x::InfiniteSession;
using Clock=std::chrono::steady_clock;
void require(bool b,const char* m){if(!b)throw std::runtime_error(m);}
double ms(Clock::time_point p){return std::chrono::duration<double,std::milli>(Clock::now()-p).count();}
std::string address(std::size_t i,std::size_t digits){
  auto s=std::to_string(i);if(digits>s.size())s="1"+std::string(digits-s.size()-1,'0')+s;return s;
}
void ghz(Session& s,std::size_t n){
  std::vector<Core::Gate> g{{'H',"0",{}}};
  for(std::size_t i=1;i<n;i++)g.push_back({'C',"0",std::to_string(i)});
  s.apply(g);
}
int main(int argc,char**argv){
  if(argc<3)return 2;
  const std::string mode=argv[1];const auto n=std::stoull(argv[2]);
  const auto extra=argc>3?std::stoull(argv[3]):1;
  const auto start=Clock::now();double setup=0,work=0,checksum=0;
  std::string status="ok",error;Core::Stats stats{};
  std::size_t count0=0,total0=0,live0=0,work_peak=0,peak_sites=0,snapshot_bytes=0,work_calls=0,work_bytes=0;
  auto begin_work=[&](){setup=ms(start);count0=allocation::count;total0=allocation::total;
    live0=allocation::live;allocation::peak=live0;return Clock::now();};
  try {
    if(mode=="batch"||mode=="framed_batch"){
      Session s;ghz(s,n);if(mode=="framed_batch")s.h_all();
      const char axis=mode=="batch"?'Z':'X';
      std::vector<Session::Observable> q;
      for(std::size_t i=0;i<extra;i++)q.push_back({i%2?-1:1,{{"0",axis},{std::to_string(1+i%(n-1)),axis}}});
      const auto before=s.snapshot();auto t=begin_work();
      for(int repeat=0;repeat<5;repeat++){
        auto r=s.query_batch(q);for(std::size_t i=0;i<r.values.size();i++){
          require(r.values[i]==(i%2?-1.:1.),"GHZ analytic result");checksum+=r.values[i];
        }
      }
      work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;stats=s.stats();
      require(s.snapshot()==before,"query mutated state/RNG");
    }else if(mode=="rules"||mode=="support"){
      Core c;for(std::size_t i=0;i<(mode=="rules"?n:2);i++)if(i%2)c.cz_neighbors();else c.h_all();
      std::vector<Core::Term> q;
      if(mode=="rules")q.push_back({address(10000,extra),'X'});
      else for(std::size_t i=0;i<n;i++)q.push_back({std::to_string(i*3+10000),'X'});
      auto t=begin_work();for(int i=0;i<3;i++)checksum+=c.expectation(q);
      work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;stats=c.stats();peak_sites=c.ether_stats().last_query_peak_sites;
      require(stats.active_qubits==0,"symbolic query materialized domain");
    }else if(mode=="noop"){
      Session s;std::vector<Core::Gate> gates;
      for(std::size_t i=1;i<n;i++)gates.push_back({'C',"0",std::to_string(i)});
      auto t=begin_work();s.apply(gates);checksum=s.probability_one("0");
      require(checksum==0&&s.probability_one(std::to_string(n-1))==0,"zero controls changed state");
      work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;stats=s.stats();
    }else if(mode=="addresses"){
      Session s;std::vector<Core::Gate> g;
      for(std::size_t i=0;i<n;i++)g.push_back({'X',address(i,extra),{}});
      auto t=begin_work();s.apply(g);stats=s.stats();
      checksum=s.probability_one(address(n-1,extra));require(checksum==1,"long address lost");
      // Include serialization and strict decoding in the work budget.
      const auto saved=s.snapshot();snapshot_bytes=saved.size();s.restore(saved);
      require(s.snapshot()==saved,"long address snapshot mismatch");
      work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;
    }else if(mode=="operations"){
      Session s;s.h_all();s.cz_neighbors();std::vector<Core::Gate> gates(n,{'Z',"10",{}});
      auto t=begin_work();s.apply(gates);
      checksum=s.query_batch({{1,{{"9",'Z'},{"10",'X'},{"11",'Z'}}}}).values[0];
      require(checksum==(n%2?-1.:1.),"cluster parity wrong");work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;stats=s.stats();
    }else if(mode=="lifecycle"){
      auto t=begin_work();for(std::size_t i=0;i<n;i++){
        Session s;s.h_all();s.cz_neighbors();s.apply({{'H',"5",{}}});s.seed(42+i);
        auto saved=s.snapshot();Session restored;restored.restore(saved);
        for(int q=0;q<8;q++){
          auto a=s.measure_z(std::to_string(q)),b=restored.measure_z(std::to_string(q));
          require(a.outcome_one==b.outcome_one,"restore RNG divergence");checksum+=a.outcome_one;
        }require(s.snapshot()==restored.snapshot(),"restored continuation differs");stats=s.stats();
      }work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;
    }else if(mode=="threads"){
      Session s;s.h_all();s.cz_neighbors();std::atomic<bool> bad{false};std::atomic<std::size_t> checks{0};
      auto t=begin_work();std::vector<std::thread> threads;
      for(std::size_t k=0;k<n;k++)threads.emplace_back([&,k](){try{
        for(std::size_t j=0;j<extra;j++){
          if(k==0)s.apply({{'Z',"10",{}}});
          else {auto r=s.query_batch({{1,{{"9",'Z'},{"10",'X'},{"11",'Z'}}}});
            if(r.values[0]!=((r.revision-2)%2?-1.:1.))bad=true;checks++;}
        }}catch(...){bad=true;}});
      for(auto& thread:threads)thread.join();require(!bad,"torn or failed concurrent state");
      checksum=checks;stats=s.stats();work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;
    }else if(mode=="invalid"){
      Session s;s.h_all();s.cz_neighbors();const auto saved=s.snapshot();auto t=begin_work();
      const std::vector<std::string> bad={"x -1", "h 0\nt 2", "x 00\ncx 0 00", "h 2\nexpect X:00 Z:0", "h 2\nforce 0 7", "seed 18446744073709551616"};
      for(std::size_t i=0;i<n;i++){
        bool rejected=false;try{s.execute(bad[i%bad.size()]);}catch(const std::exception&){rejected=true;}
        require(rejected&&s.snapshot()==saved,"invalid input committed");checksum++;
      }work=ms(t);work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;stats=s.stats();
    }else throw std::invalid_argument("unknown mode");
  }catch(const std::length_error& e){status="budget";error=e.what();}
  catch(const std::bad_alloc&){status="allocation_limit";error="allocation rejected";}
  catch(const std::exception& e){status="error";error=e.what();}
  if(!work_peak){work_peak=allocation::peak;work_calls=allocation::count-count0;work_bytes=allocation::total-total0;work=ms(start)-setup;}
  for(char&c:error)if(c=='"'||c=='\\'||c=='\n')c=' ';
  std::cout<<std::setprecision(12)<<"{\"mode\":\""<<mode<<"\",\"n\":"<<n<<",\"extra\":"<<extra
    <<",\"status\":\""<<status<<"\",\"error\":\""<<error<<"\",\"elapsed_ms\":"<<ms(start)
    <<",\"setup_ms\":"<<setup<<",\"work_ms\":"<<work<<",\"checksum\":"<<checksum
    <<",\"state_tableau_bytes\":"<<stats.tableau_bytes<<",\"state_address_bytes\":"<<stats.address_bytes
    <<",\"active\":"<<stats.active_qubits<<",\"components\":"<<stats.components
    <<",\"rules\":"<<stats.ether_rules<<",\"peak_cone_sites\":"<<peak_sites<<",\"snapshot_bytes\":"<<snapshot_bytes
    <<",\"new_calls_work\":"<<work_calls<<",\"new_bytes_work\":"<<work_bytes
    <<",\"new_live_at_work_start\":"<<live0<<",\"new_peak_work\":"<<work_peak<<"}\n";
  return status=="error"?1:0;
}
