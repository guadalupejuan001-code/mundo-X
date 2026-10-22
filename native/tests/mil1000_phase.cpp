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
  bothH(0);bothCX(0,7);bothT(7);bothH(3);bothP(3,.231);bothCX(3,4);bothT(4);
  for(std::size_t q=0;q<8;++q)for(char axis:{'X','Y','Z'}){
    const double a=wide.pauli_expectation(axis,q);
    const double b=reduced.pauli_expectation(axis,q);
    require(std::abs(a-b)<1e-10,"wide/reduced local observable mismatch");
  }
  require(std::abs(wide.coefficient_norm_squared()-reduced.coefficient_norm_squared())<1e-10,
          "wide/reduced norm mismatch");

  std::cout
      <<"{\"suite\":\"mil1000_universal\","
      <<"\"qubits\":1000,"
      <<"\"ghz1000\":true,"
      <<"\"t_q999\":true,"
      <<"\"general_phase_q999\":true,"
      <<"\"post_t_clifford\":true,"
      <<"\"wide_small_crosscheck\":true,"
      <<"\"components\":"<<core.component_count()<<","
      <<"\"payload_bytes\":"<<core.logical_payload_bytes()
      <<"}\n";
}
