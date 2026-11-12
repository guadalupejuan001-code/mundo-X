#include "mundo_x/ether_core.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <typeinfo>
#include <vector>

// Identical test binary is built against pinned main and the candidate header.
// Hash logical outputs, phases, order, peak statistics, and exception contracts;
// output must be byte-for-byte identical. No clock, random_device or UB.
using Core = mundo_x::EtherCore;
static std::uint64_t hash_value=14695981039346656037ULL;
static std::size_t checks=0, failures=0, successes=0;
static std::uint64_t seed=0x9e3779b97f4a7c15ULL;
static std::uint64_t next() {
  seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17; return seed;
}
static void feed(const std::string& value) {
  for (const auto c:value) {
    hash_value ^= static_cast<unsigned char>(c);
    hash_value *= 1099511628211ULL;
  }
  hash_value ^= 0xff;
  hash_value *= 1099511628211ULL;
}
static void stats(const Core::Stats& s) {
  feed(std::to_string(s.rules));feed(std::to_string(s.last_query_sites));
  feed(std::to_string(s.last_query_peak_sites));feed(std::to_string(s.last_query_address_bytes));
}
static void query(Core& core, const std::vector<Core::Term>& terms,
                  int sign, Core::QueryLimits limits, bool expectation_too) {
  ++checks;
  feed(std::to_string(sign));
  feed(std::to_string(limits.max_sites));
  feed(std::to_string(limits.max_address_digits));
  for (const auto& term:terms) {feed(term.address);feed(std::string(1,term.axis));}
  try {
    const auto out=core.pullback(terms,sign,limits);
    ++successes;
    feed("OK");feed(std::to_string(out.sign));stats(out.stats);
    for (const auto& term:out.terms) {
      feed(term.address);feed(std::string(1,term.axis));
    }
    if (expectation_too) {
      const auto exp=core.expectation(terms,sign);
      const auto expected = [&]() {
        for (const auto& term:out.terms)
          if (term.axis=='X'||term.axis=='Y') return 0.0;
        return static_cast<double>(out.sign);
      }();
      if (exp!=expected) throw std::runtime_error("expectation disagrees with pullback");
      feed(std::to_string(exp));stats(core.stats());
    }
  } catch (const std::exception& e) {
    ++failures;feed("ERR");feed(typeid(e).name());feed(e.what());
    if (std::string(e.what())=="expectation disagrees with pullback") throw;
  }
}
int main() {
  try {
    const std::vector<std::size_t> depths={0,1,2,3,4,7,8,9,15,16,31,32,33,48,64,257,4096};
    const char axes[]={'I','X','Y','Z'};
    for (const auto depth:depths) {
      Core core;
      for (std::size_t step=0;step<depth;++step)
        if (step%2) core.cz_neighbors(); else core.h_all();
      feed(std::to_string(core.rule_count()));
      feed(std::to_string(static_cast<unsigned>(core.rule_run().first)));
      feed(std::to_string(core.rules().size()));
      const int rounds=depth>=257?32:160;
      for (int i=0;i<rounds;++i) {
        std::vector<Core::Term> terms;
        const int n=depth>=257?1:static_cast<int>(1+next()%7);
        for (int j=0;j<n;++j) {
          const auto r=next();
          std::string addr=std::to_string(r%40);
          if (r%5==0) addr="000"+addr;
          if (r%13==0) addr="1"+std::string(91,'0')+addr;
          terms.push_back({std::move(addr),axes[(r>>8)&3U]});
        }
        Core::QueryLimits limits;
        limits.max_sites=i%11==0?0:(i%7==0?8:256);
        limits.max_address_digits=i%17==0?3:4096;
        query(core,terms,i%2?-1:1,limits,depth<=32&&i%17==1&&limits.max_sites==256);
      }
      const std::string huge="1"+std::string(1024,'0');
      query(core,{{huge,'X'}},1,{256,2048},depth<=32);
      query(core,{{huge,'Y'}},-1,{2,2048},false);
      query(core,{{"00001",'I'},{"1",'X'}},1,{},false);
      query(core,{{"00001",'X'}},0,{},false);
      query(core,{{"",'Z'}},1,{},false);
      query(core,{{"123a",'Z'}},1,{},false);
      query(core,{{"00001",'X'}},1,{100,4},false);
    }
    if (successes<500||failures<80) throw std::runtime_error("Test coverage unexpectedly small");
    std::cout<<"{\"checks\":"<<checks<<",\"successes\":"<<successes
             <<",\"exceptions\":"<<failures<<",\"digest\":\""<<std::hex
             <<hash_value<<"\"}\n";
  } catch(const std::exception& e) {
    std::cerr<<"Ether optimized/original equivalence failed: "<<e.what()<<"\n";
    return 1;
  }
}
