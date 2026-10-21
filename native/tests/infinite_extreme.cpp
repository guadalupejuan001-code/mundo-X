// Recovered from PR20 d4de6d0, adapted to the current InfiniteSession batch
// contract. A correctness/resource gate, without performance pass thresholds.
#include "mundo_x/infinite_session.hpp"
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mundo_x::InfiniteCore;
using mundo_x::InfiniteSession;
static std::size_t checks=0;
static void require(bool value,const char* message) {
  ++checks;if(!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F action,const char* message) {
  bool rejected=false;try{action();}catch(const std::exception&){rejected=true;}
  require(rejected,message);
}
static std::string far_address(std::size_t q) {
  const auto tail=std::to_string(q);
  return std::string(995,'9')+std::string(6-tail.size(),'0')+tail;
}
int main() {
  try {
    InfiniteSession active;
    std::vector<InfiniteCore::Gate> prepare{{'H',"0",{}}};
    for(std::size_t q=1;q<1024;++q)prepare.push_back({'C',"0",std::to_string(q)});
    active.apply(prepare);
    require(active.stats().active_qubits==1024,"GHZ materialization width");
    std::vector<InfiniteSession::Observable> queries;
    for(std::size_t i=0;i<256;++i)
      queries.push_back({1,{{"0",'Z'},{std::to_string(1+(i*997)%1023),'Z'}}});
    const auto saved=active.snapshot();
    const auto scalar=InfiniteCore::from_snapshot(saved,active.limits());
    const auto batch=active.query_batch(queries);
    require(batch.observer_builds==1,"batch rebuilt one component multiple times");
    for(std::size_t i=0;i<queries.size();++i) {
      require(batch.values[i]==1.0,"GHZ ZZ correlation lost");
      // Sample the independent call path; every batch answer has an analytic oracle.
      if(i%8==0)require(batch.values[i]==scalar.expectation(queries[i].terms),"scalar/batch mismatch");
    }
    require(active.snapshot()==saved,"query changed state or RNG");
    rejects([&]{active.apply({{'H',"1024",{}},{'C',"0","1024"}});},"component budget ignored");
    require(active.snapshot()==saved,"component-budget failure changed state");

    InfiniteSession remote(InfiniteCore::Background::BellPairs);
    queries.clear();
    for(std::size_t i=0;i<4096;++i)queries.push_back({1,{{far_address(i*2),'Z'}}});
    const auto observed=remote.query_batch(queries);
    require(observed.values.size()==4096,"remote query count");
    for(double value:observed.values)require(value==0.0,"remote Bell singleton expectation");
    require(observed.stats.active_qubits==0&&observed.stats.tableau_bytes==0,
            "read materialized the rule-defined background");
    queries.push_back({1,{{"0",'Z'}}});
    rejects([&]{remote.query_batch(queries);},"observable transport cap ignored");

    InfiniteSession churn;
    std::vector<InfiniteCore::Gate> gates(65536,{'X',"7",{}});
    churn.apply(gates);
    require(churn.probability_one("7")==0.0,"even-X result changed");
    // Exact implementations may immediately return this site to the implicit
    // background. Require the result and bounded retained support, not storage.
    const auto retained=churn.stats().active_qubits;
    require(retained<=1,"even-X batch retained unrelated sites");
    const auto before_bad=churn.snapshot();
    rejects([&]{churn.apply({{'Q',"7",{}}});},"unsupported phase accepted by Clifford session");
    require(churn.snapshot()==before_bad,"invalid gate changed state");
    gates.push_back({'X',"7",{}});
    rejects([&]{churn.apply(gates);},"gate transport cap ignored");
    require(churn.snapshot()==before_bad,"oversized batch changed state");
    require(churn.compact_background()==retained&&churn.stats().active_qubits==0,
            "exact compaction failed");
    std::cout<<"{\"suite\":\"infinite_extreme\",\"ok\":true,\"checks\":"<<checks
             <<",\"ghz_qubits\":1024,\"batch_observables\":256,\"remote_observables\":4096,"
               "\"remote_address_digits\":1001,\"remote_active_after\":0,\"stress_gates\":65536}\n";
    return 0;
  } catch(const std::exception& e){std::cerr<<"infinite_extreme: "<<e.what()<<'\n';return 1;}
}
