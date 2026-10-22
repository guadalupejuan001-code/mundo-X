#include "mundo_x/mil_universal_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>

static void require(bool ok,const char* message){
  if(!ok)throw std::runtime_error(message);
}

static std::string pauli(std::size_t n,char fill,char last){
  std::string out(n,fill);out.back()=last;return out;
}

int main(){
  using mundo_x::MilUniversalCore;
  using mundo_x::ReducedExactTRepresentation;

  const double pi=std::acos(-1.0);
  const double s=std::sqrt(.5);

  // Main target: one 1000-qubit register, entangled while still compact,
  // then promoted to the multiword non-Clifford representation.
  MilUniversalCore core(1000,4096);
  core.hadamard(0);
  for(std::size_t q=1;q<1000;++q)core.cnot(q-1,q);
  require(!core.promoted(),"Clifford preparation promoted early");
  require(std::abs(core.expectation(std::string(1000,'X'))-1.)<1e-12,
          "GHZ1000 X product before T");

  core.phase_t(999);
  require(core.promoted(),"T did not promote wide backend");
  require(core.component_count()<=2,"one T used more than two components");
  require(std::abs(core.expectation(std::string(1000,'X'))-s)<1e-10,
          "GHZ1000 T all-X mismatch");
  require(std::abs(core.expectation(pauli(1000,'X','Y'))-s)<1e-10,
          "GHZ1000 T X...Y mismatch");

  const double extra=.37;
  core.phase(999,extra);
  const double angle=pi/4.+extra;
  require(std::abs(core.expectation(std::string(1000,'X'))-std::cos(angle))<1e-10,
          "1000-qubit arbitrary phase all-X mismatch");
  require(std::abs(core.expectation(pauli(1000,'X','Y'))-std::sin(angle))<1e-10,
          "1000-qubit arbitrary phase X...Y mismatch");

  // Clifford evolution after promotion must remain legal. Apply an involution
  // twice and require the original non-Clifford correlations to return.
  core.cnot(0,999);
  core.cnot(0,999);
  require(std::abs(core.expectation(std::string(1000,'X'))-std::cos(angle))<1e-10,
          "post-T Clifford evolution changed restored observable");

  // Cross-check the wide engine against the already validated 64-bit Reduced
  // backend on a small circuit, including T and a general phase.
  mundo_x::WideReducedExactTRepresentation wide(8,4096);
  ReducedExactTRepresentation reduced(8,4096);
  auto bothH=[&](std::size_t q){wide.hadamard(q);reduced.hadamard(q);};
  auto bothCX=[&](std::size_t a,std::size_t b){wide.cnot(a,b);reduced.cnot(a,b);};
  auto bothT=[&](std::size_t q){wide.phase_t(q);reduced.phase_t(q);};
  auto bothP=[&](std::size_t q,double t){wide.phase(q,t);reduced.phase(q,t);};
  auto bothCP=[&](std::size_t a,std::size_t b,double t){
    wide.controlled_phase(a,b,t);reduced.controlled_phase(a,b,t);
  };
  bothH(0);bothCX(0,7);bothT(7);bothH(3);bothP(3,.231);
  bothCX(3,4);bothCP(0,4,-.173);bothT(4);
  for(std::size_t q=0;q<8;++q)for(char axis:{'X','Y','Z'}){
    const double a=wide.pauli_expectation(axis,q);
    const double b=reduced.pauli_expectation(axis,q);
    require(std::abs(a-b)<1e-10,"wide/reduced local observable mismatch");
  }
  require(std::abs(wide.coefficient_norm_squared()-reduced.coefficient_norm_squared())<1e-10,
          "wide/reduced norm mismatch");

  // 1000-qubit reversible magic stress. Start from a line-cluster stabilizer,
  // leave the stabilizer manifold with separated T gates + controlled phase,
  // mix with H, then execute the exact inverse and verify cluster generators.
  MilUniversalCore stress(1000,16384);
  for(std::size_t q=0;q<1000;++q)stress.hadamard(q);
  for(std::size_t q=0;q+1<1000;++q)stress.controlled_z(q,q+1);
  const std::size_t magic_sites[]={0,257,613,999};
  for(auto q:magic_sites)stress.phase_t(q);
  stress.controlled_phase(123,876,.217);
  stress.hadamard(500);
  const auto peak_components=stress.component_count();
  require(peak_components>1&&peak_components<=16384,"1000q magic stress component budget");

  stress.hadamard(500);
  stress.controlled_phase(123,876,-.217);
  for(std::size_t i=4;i-->0;)stress.phase_t_dagger(magic_sites[i]);

  auto cluster_generator=[](std::size_t q){
    std::string g(1000,'I');
    g[q]='X';
    if(q>0)g[q-1]='Z';
    if(q+1<1000)g[q+1]='Z';
    return g;
  };
  for(auto q:{std::size_t{0},std::size_t{257},std::size_t{500},std::size_t{999}})
    require(std::abs(stress.expectation(cluster_generator(q))-1.)<1e-9,
            "reversible 1000q magic stress did not restore cluster stabilizer");

  std::cout
      <<"{\"suite\":\"mil1000_universal\","
      <<"\"qubits\":1000,"
      <<"\"ghz1000\":true,"
      <<"\"t_q999\":true,"
      <<"\"general_phase_q999\":true,"
      <<"\"post_t_clifford\":true,"
      <<"\"wide_small_crosscheck\":true,\"controlled_phase_crosscheck\":true,"
      <<"\"components\":"<<core.component_count()<<","
      <<"\"stress_peak_components\":"<<peak_components<<","
      <<"\"payload_bytes\":"<<core.logical_payload_bytes()
      <<"}\n";
}
