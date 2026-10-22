#include "mundo_x/ether_mil_bridge.hpp"

#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
std::size_t checks=0;
void require(bool condition,const char* what) {
  ++checks;
  if(!condition)throw std::runtime_error(what);
}
void near(double actual,double expected,double tolerance,const char* what) {
  ++checks;
  if(!std::isfinite(actual)||std::abs(actual-expected)>tolerance)
    throw std::runtime_error(what);
}
template<class F> void rejects(F fn,const char* what) {
  bool rejected=false;
  try {fn();}catch(const std::exception&){rejected=true;}
  require(rejected,what);
}
}

int main() {
  try {
    using Core=mundo_x::ReducedExactTRepresentation;
    using Render=mundo_x::EtherRenderer;
    using Bridge=mundo_x::MilEtherRenderBridge;
    constexpr std::size_t n=1000;
    Core mil(n,128);
    require(mil.qubits()==1000&&mil.base_tableau_bytes()==257000,
            "MIL must use current 1000-qubit universal backend");
    mil.hadamard(999);

    Render renderer(160,90,16);
    renderer.set_camera({0,0,10},{0,0,0},47.);
    renderer.add_body({0,{0,0,0},1.8,{{.35,.55,.75}}});
    renderer.add_body({1,{3.,1.,-7.},.7,{{.9,.3,.2}}});
    const auto unbound=renderer.render(true);
    require(unbound.exact_mismatches==0,"unbound oracle mismatch");

    Bridge link(mil,renderer);
    rejects([&]{link.bind_axis(0,'Q',999,.5);},"invalid Pauli axis was accepted");
    rejects([&]{link.bind_axis(0,'X',1000,.5);},"q1000 was accepted");
    rejects([&]{link.bind_axis(2,'X',999,.5);},"unknown body was accepted");
    rejects([&]{link.bind_axis(0,'X',999,1.1);},"invalid strength was accepted");
    rejects([&]{link.bind(0,"X",.5);},"truncated multiword Pauli was accepted");

    link.bind_axis(0,'X',999,.75);
    require(link.refresh()==1&&!link.dirty(),"MIL material binding did not refresh");
    near(link.signal(0),1.,1e-12,"initial X999 signal");
    const auto before=renderer.render(true);
    require(before.exact_mismatches==0,"bound oracle mismatch");
    require(before.frame_hash!=unbound.frame_hash,"MIL material did not affect frame");
    const auto before_candidates=before.candidate_tests;
    const auto before_mask=renderer.mask_at(80,45);
    const auto snapshot_components=mil.component_count();
    const auto snapshot_t=mil.t_count();
    require(link.refresh()==0,"unchanged MIL state caused another observer pass");
    require(mil.component_count()==snapshot_components&&mil.t_count()==snapshot_t,
            "read-only rendering mutated MIL");
    near(mil.sparse_norm(),1.,1e-12,"MIL norm changed by rendering");

    mil.phase_t(999);
    link.invalidate();
    require(link.refresh()==1,"T gate invalidation was lost");
    near(link.signal(0),1./std::sqrt(2.),1e-12,
         "non-Clifford q999 did not reach renderer");
    const auto after=renderer.render(true);
    require(after.exact_mismatches==0,"magic-state render oracle mismatch");
    require(after.frame_hash!=before.frame_hash,"T did not change rendered image");
    require(after.candidate_tests==before_candidates &&
            renderer.mask_at(80,45)==before_mask,
            "color-only MIL update rebuilt or changed spatial candidates");
    near(mil.sparse_norm(),1.,1e-12,"T lost normalization");

    mil.phase_t_dagger(999);
    link.invalidate();link.refresh();
    const auto restored=renderer.render(true);
    require(restored.exact_mismatches==0,"inverse T oracle mismatch");
    require(restored.frame_hash==before.frame_hash,
            "reversible quantum material failed to restore original frame");

    // Remote entanglement across all 16 Pauli words, plus general controlled phase.
    Core entangled(n,128);
    entangled.hadamard(999);
    entangled.cnot(999,0);
    entangled.controlled_phase(999,0,.37);
    Render pair(96,54,16);
    pair.add_body({0,{0,0,0},2.,{{.4,.6,.8}}});
    pair.add_body({1,{3.,1.,-5.},.8,{{.5,.9,.2}}});
    Bridge correlation(entangled,pair);
    std::string xx(n,'I');xx[0]='X';xx[999]='X';
    correlation.bind(0,xx,.65);
    correlation.bind(1,xx,.4);
    require(correlation.refresh()==2,"correlated MIL signals not published");
    near(correlation.signal(0),std::cos(.37),1e-11,
         "q0-q999 controlled phase correlation mismatch");
    near(correlation.signal(1),correlation.signal(0),1e-14,
         "duplicate Pauli binding changed value");
    require(pair.render(true).exact_mismatches==0,"entangled oracle mismatch");
    require(entangled.component_count()<=8,"unbounded MIL magic components");

    correlation.unbind(1);
    require(correlation.binding_count()==1,"unbind still active");
    rejects([&]{(void)correlation.signal(1);},"unbound signal remained readable");
    correlation.clear();
    require(correlation.binding_count()==0,"clear kept ghost bindings");

    std::cout<<"{\"suite\":\"ether_mil_renderer\",\"ok\":true,"
             <<"\"qubits\":1000,\"checks\":"<<checks
             <<",\"components\":"<<mil.component_count()
             <<",\"before_hash\":"<<before.frame_hash
             <<",\"magic_hash\":"<<after.frame_hash
             <<",\"restored_hash\":"<<restored.frame_hash
             <<",\"candidate_tests\":"<<after.candidate_tests
             <<",\"brute_tests\":"<<after.brute_tests
             <<",\"mismatches\":"<<after.exact_mismatches<<"}\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"ether_mil_renderer: "<<e.what()<<'\n';
    return 1;
  }
}
