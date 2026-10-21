
#include "mundo_x/infinite_core.hpp"
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iomanip>
#include <new>
namespace allocation {
struct alignas(std::max_align_t) Header { std::size_t size; };
std::size_t live=0, peak=0, calls=0, bytes=0;
void* get(std::size_t n) {
 if(n>SIZE_MAX-sizeof(Header))throw std::bad_alloc();
 auto* p=static_cast<Header*>(std::malloc(n+sizeof(Header)));
 if(!p)throw std::bad_alloc();p->size=n;live+=n;peak=std::max(peak,live);++calls;bytes+=n;return p+1;
}
void release(void* p)noexcept{if(p){auto*h=static_cast<Header*>(p)-1;live-=h->size;std::free(h);}}
}
void* operator new(std::size_t n){return allocation::get(n);}
void* operator new[](std::size_t n){return allocation::get(n);}
void operator delete(void*p)noexcept{allocation::release(p);}
void operator delete[](void*p)noexcept{allocation::release(p);}
void operator delete(void*p,std::size_t)noexcept{allocation::release(p);}
void operator delete[](void*p,std::size_t)noexcept{allocation::release(p);}

#include "mundo_x/infinite_session.hpp"
using E=mundo_x::EtherCore;using C=mundo_x::InfiniteCore;using S=mundo_x::InfiniteSession;
using Clock=std::chrono::steady_clock;
int main(int argc,char**argv){
 if(argc!=7)return 2;
 std::string mode=argv[1];auto depth=std::stoull(argv[2]), count=std::stoull(argv[3]);
 auto sites=std::stoull(argv[4]),digits=std::stoull(argv[5]),stride=std::stoull(argv[6]);
 E frame;frame.restore_rule_run(E::Rule::HAll,depth);
 E::QueryLimits limits;limits.max_sites=65536;limits.max_address_digits=4096;
 S session;
 if(mode=="session"){
  session.apply({{'H',"0",{}},{'C',"0","1"}});
  for(std::size_t i=0;i<depth;++i)if(i%2)session.cz_neighbors();else session.h_all();
 }
 std::vector<E::Term> terms;
 std::vector<S::Observable> batch;
 auto address=[&](std::size_t q){auto a=std::to_string(q);if(digits>a.size())a="1"+std::string(digits-a.size()-1,'0')+a;return a;};
 for(std::size_t j=0;j<sites;++j)terms.push_back({address(2000+j*stride),"XYZ"[j%3]});
 if(mode=="session")for(std::size_t i=0;i<count;++i){
  S::Observable q;q.sign=i%2?-1:1;
  for(std::size_t j=0;j<sites;++j)q.terms.push_back({std::to_string(64+i+j),"XYZ"[(i+j)%3]});
  batch.push_back(std::move(q));
 }
 auto calls=allocation::calls,bytes=allocation::bytes,live=allocation::live;allocation::peak=live;
 std::uint64_t checksum=1469598103934665603ULL;std::size_t output_terms=0;
 auto hash=[&](unsigned char v){checksum^=v;checksum*=1099511628211ULL;};
 auto start=Clock::now();
 if(mode=="session"){
  auto result=session.query_batch(batch);
  for(auto value:result.values)hash(static_cast<unsigned char>(value+1));
  output_terms=result.values.size();
 }else for(std::size_t i=0;i<count;++i){
  // Distinct queries: each repetition translates the full input support.
  for(std::size_t j=0;j<sites;++j)terms[j].address=address(2000+i+j*stride);
  auto result=frame.pullback(terms,i%2?-1:1,limits);output_terms+=result.terms.size();
  hash(result.sign==1?1:255);
  for(const auto&t:result.terms){for(unsigned char c:t.address)hash(c);hash(t.axis);}
 }
 auto ms=std::chrono::duration<double,std::milli>(Clock::now()-start).count();
 auto used_calls=allocation::calls-calls,used_bytes=allocation::bytes-bytes,peak=allocation::peak-live;
 std::size_t rss=0;
 {std::ifstream f("/proc/self/status");std::string l;while(std::getline(f,l))if(l.compare(0,6,"VmHWM:")==0)rss=std::stoull(l.substr(6));}
 std::cout<<std::setprecision(12)<<"{\"ms\":"<<ms<<",\"new_calls\":"<<used_calls
 <<",\"new_bytes\":"<<used_bytes<<",\"peak_new_bytes\":"<<peak
 <<",\"process_peak_rss_kib\":"<<rss<<",\"checksum\":"<<checksum<<",\"terms\":"<<output_terms<<"}\n";
}
