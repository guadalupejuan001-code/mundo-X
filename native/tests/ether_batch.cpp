#include "mundo_x/infinite_session.hpp"
#include <iostream>
#include <random>

using Session=mundo_x::InfiniteSession;
using Core=mundo_x::InfiniteCore;
static unsigned checks=0;
static void check(bool b,const char* text){++checks;if(!b)throw std::runtime_error(text);}

int main(){try{
  std::mt19937_64 rng(42);
  {
    Session zero;
    std::vector<Core::Gate> gates;
    for(unsigned i=1;i<4096;i++)gates.push_back({'C',"0",std::to_string(i)});
    zero.apply(gates);
    check(zero.stats().active_qubits==0,"identity controls materialized zero background");
    check(zero.probability_one("4095")==0,"identity controls changed far site");
    zero.apply({{'H',"0",{}},{'D',"0","4095"}});
    check(zero.stats().active_qubits==1&&zero.probability_one("0")==.5,"CZ with implicit zero changed control");
    zero.apply({{'C',"0","4095"}});
    check(zero.query_batch({{1,{{"0",'X'},{"4095",'X'}}}}).values[0]==1,"entangling control was incorrectly skipped");
    Session bell(Core::Background::BellPairs);bell.apply({{'C',"0","1"}});
    check(bell.probability_one("1")==0&&bell.probability_one("0")==.5,"Bell background was treated as zero");
  }
  for(auto background:{Core::Background::Zero,Core::Background::BellPairs}){
    Session s(background);
    for(unsigned step=0;step<80;++step){
      const auto a=std::to_string(rng()%12),b=std::to_string(rng()%12);
      if(step%8==0)s.h_all();
      else if(step%8==1)s.cz_neighbors();
      else if(a!=b)s.apply({{'H',a,{}},{'C',a,b},{'S',b,{}}});
      std::vector<Session::Observable> queries;
      for(unsigned i=0;i<40;++i){
        Session::Observable q;q.sign=rng()%2?-1:1;
        for(unsigned j=0;j<12;++j)if(rng()%3==0)
          q.terms.push_back({std::to_string(j),"IXYZ"[rng()%4]});
        queries.push_back(std::move(q));
      }
      const auto saved=s.snapshot();const auto reference=Core::from_snapshot(saved,s.limits());
      auto observed=s.query_batch(queries);
      for(std::size_t i=0;i<queries.size();++i)
        check(observed.values[i]==reference.expectation(queries[i].terms,queries[i].sign),"batched signs/state differ");
      check(observed.observer_builds<=observed.stats.components,"rebuilt component within batch");
      check(s.snapshot()==saved,"batch changed state or RNG");
      const auto epoch=s.epoch();s.restore(saved);
      check(s.query_batch(queries).values==observed.values&&s.epoch()>epoch,"restore reused stale cache");
      queries.push_back({1,{{"00",'X'},{"0",'Z'}}});
      bool rejected=false;try{s.query_batch(queries);}catch(const std::invalid_argument&){rejected=true;}
      check(rejected&&s.snapshot()==saved,"invalid tail query changed state");
    }
  }
  Session s;s.apply({{'H',"0",{}},{'C',"0","1"}});s.h_all();
  std::vector<Session::Observable> q(4096,{1,{{"0",'X'},{"1",'X'}}});
  auto r=s.query_batch(q);
  check(r.observer_builds==1&&r.values.size()==4096,"one component was rebuilt");
  for(double v:r.values)check(v==1,"Bell analytic XX wrong");
  s.apply({{'Z',"0",{}}});r=s.query_batch(q);
  for(double v:r.values)check(v==-1,"writer failed to invalidate observer");
  std::cout<<"PASS ether_batch "<<checks<<" checks\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
