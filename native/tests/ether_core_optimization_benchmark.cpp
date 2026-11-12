#include "mundo_x/ether_core.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>
using Core=mundo_x::EtherCore;
int main(int argc,char**argv) {
  if (argc!=3) return 2;
  const std::string mode=argv[1];
  const int count=std::atoi(argv[2]);
  if (count<=0||count>300000) return 2;
  Core core;
  std::vector<Core::Term> q;
  if (mode=="short") {
    core.h_all();core.cz_neighbors();
    q={{"4",'X'},{"5",'Z'}};
  } else if (mode=="long") {
    core.h_all();core.cz_neighbors();core.h_all();
    const std::string digits(1024,'0');
    q={{"1"+digits,'X'},{"2"+digits,'Y'},{"3"+digits,'Z'}};
  } else if (mode=="dense") {
    for (int j=0;j<7;++j) if(j&1) core.cz_neighbors();else core.h_all();
    for (int j=0;j<32;++j)
      q.push_back({std::to_string(j*3+1),j%4==0?'Y':(j%4==1?'X':'Z')});
  } else if (mode=="deep") {
    for (int j=0;j<64;++j) if(j&1) core.cz_neighbors();else core.h_all();
    q={{"10000000",'X'}};
  } else return 2;
  volatile std::int64_t result=0;
  const auto begin=std::chrono::steady_clock::now();
  for (int n=0;n<count;++n) result+=static_cast<int>(core.expectation(q));
  const auto end=std::chrono::steady_clock::now();
  const auto us=std::chrono::duration<double,std::micro>(end-begin).count();
  std::cout<<"{\"mode\":\""<<mode<<"\",\"queries\":"<<count
           <<",\"checksum\":"<<result<<",\"us\":"<<us<<"}\n";
}
