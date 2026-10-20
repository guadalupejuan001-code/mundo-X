#include "mundo_x/ether_core.hpp"
#include <chrono>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mundo_x::EtherCore;
static std::size_t checks=0;
static void require(bool ok,const char* msg){++checks;if(!ok)throw std::runtime_error(msg);}
static std::string pred(std::string x){
  for(std::size_t i=x.size();i-->0;){if(x[i]!='0'){--x[i];break;}x[i]='9';}
  if(x.size()>1&&x[0]=='0')x.erase(x.begin());
  return x;
}
static std::string succ(std::string x){
  for(std::size_t i=x.size();i-->0;){if(x[i]!='9'){++x[i];return x;}x[i]='0';}
  return "1"+x;
}

using C=std::complex<double>;
static std::vector<C> cluster_state(int n){
  const std::size_t dim=std::size_t{1}<<n;
  std::vector<C> psi(dim,C(1.0/std::sqrt((double)dim),0));
  for(std::size_t basis=0;basis<dim;++basis){
    int phase=0;
    for(int q=0;q<n-1;++q) if(((basis>>q)&1)&&((basis>>(q+1))&1)) phase^=1;
    if(phase) psi[basis]=-psi[basis];
  }
  return psi;
}
static C pauli_phase(char axis,int bit,int& flipped){
  flipped=bit;
  switch(axis){
    case 'I': return 1.0;
    case 'Z': return bit?C(-1,0):C(1,0);
    case 'X': flipped=bit^1; return 1.0;
    case 'Y': flipped=bit^1; return bit?C(0,-1):C(0,1);
  }
  throw std::runtime_error("bad axis");
}
static double finite_expectation(const std::vector<C>& psi,
                                 const std::vector<std::pair<int,char>>& terms){
  C answer=0;
  for(std::size_t basis=0;basis<psi.size();++basis){
    std::size_t target=basis; C factor=1;
    for(const auto& t:terms){
      const int bit=(basis>>t.first)&1;int out=bit;
      factor*=pauli_phase(t.second,bit,out);
      if(out!=bit)target^=(std::size_t{1}<<t.first);
    }
    answer+=std::conj(psi[basis])*factor*psi[target];
  }
  require(std::abs(answer.imag())<1e-10,
          "oracle produced complex Hermitian expectation");
  const double r=answer.real();
  if(std::abs(r)<1e-10)return 0.0;
  if(std::abs(r-1)<1e-10)return 1.0;
  if(std::abs(r+1)<1e-10)return -1.0;
  throw std::runtime_error("unexpected stabilizer expectation");
}

int main(){
 try{
  EtherCore e;
  require(e.expectation({{"0",'Z'}})==1.0,"base q0 Z");
  std::string huge="1"+std::string(1000,'0');
  require(e.expectation({{huge,'Z'}})==1.0,"base huge Z");
  e.h_all();
  require(e.expectation({{huge,'X'}})==1.0,"H all huge X");
  require(e.expectation({{huge,'Z'}})==0.0,"H all huge Z");
  e.cz_neighbors();
  require(e.expectation({{"0",'X'},{"1",'Z'}})==1.0,
          "cluster boundary stabilizer");
  require(e.expectation({{pred(huge),'Z'},{huge,'X'},{succ(huge),'Z'}})==1.0,
          "cluster huge stabilizer");
  require(e.expectation({{huge,'X'}})==0.0,"cluster single X");

  const auto psi=cluster_state(6);
  const char axes[]={'I','X','Y','Z'};
  for(char a:axes)for(char b:axes)for(char c:axes)for(char d:axes){
    std::vector<EtherCore::Term> q;
    std::vector<std::pair<int,char>> fq;
    const char local[]={a,b,c,d};
    for(int k=0;k<4;++k)if(local[k]!='I'){
      q.push_back({std::to_string(k+1),local[k]});
      fq.emplace_back(k+1,local[k]);
    }
    const double got=e.expectation(q),expected=finite_expectation(psi,fq);
    require(got==expected,"EtherCore disagrees with finite cluster oracle");
  }

  const auto rules=e.rule_count();
  e.cz_neighbors();e.cz_neighbors();
  require(e.rule_count()==rules,"adjacent CZ cancellation pair");
  e.h_all();e.h_all();
  require(e.rule_count()==rules,"adjacent H cancellation pair");

  constexpr int iterations=100000;
  auto start=std::chrono::steady_clock::now();
  double sum=0;
  for(int i=0;i<iterations;++i)
    sum+=e.expectation({{pred(huge),'Z'},{huge,'X'},{succ(huge),'Z'}});
  auto end=std::chrono::steady_clock::now();
  require(sum==iterations,"stress expectation drift");
  const double ms=std::chrono::duration<double,std::milli>(end-start).count();
  const auto s=e.stats();
  std::cout<<"{\"ok\":true,\"checks\":"<<checks
           <<",\"rules\":"<<s.rules
           <<",\"address_digits\":"<<huge.size()
           <<",\"oracle_paulis\":256"
           <<",\"queries\":"<<iterations
           <<",\"elapsed_ms\":"<<ms
           <<",\"us_per_query\":"<<(ms*1000.0/iterations)
           <<",\"peak_query_sites\":"<<s.last_query_peak_sites
           <<"}\n";
  return 0;
 }catch(const std::exception& e){
   std::cerr<<"ether_core: "<<e.what()<<"\n";return 1;
 }
}
