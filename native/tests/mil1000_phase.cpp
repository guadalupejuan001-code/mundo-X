#include "mundo_x/adaptive_phase_core.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

static void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}

int main() {
  using mundo_x::AdaptivePhaseCore;
  const double pi = std::acos(-1.0);
  AdaptivePhaseCore core(1000);
  require(core.qubits()==1000, "1000 qubits not created");
  core.hadamard(999);
  core.phase_t(999);
  require(std::abs(core.pauli_expectation('X',999)-std::sqrt(.5))<1e-12,"T X incorrect");
  require(std::abs(core.pauli_expectation('Y',999)-std::sqrt(.5))<1e-12,"T Y incorrect");
  core.phase(999,.37);
  require(std::abs(core.pauli_expectation('X',999)-std::cos(pi/4+.37))<1e-12,"phase X incorrect");
  require(std::abs(core.pauli_expectation('Y',999)-std::sin(pi/4+.37))<1e-12,"phase Y incorrect");
  core.controlled_phase(0,999,.41);
  require(core.is_product(),"inactive control should not entangle");
  require(core.component_count()==1,"product expanded");
  require(core.logical_payload_bytes()<=1000*sizeof(AdaptivePhaseCore::Pair),"product payload unexpectedly expanded");
  require(std::abs(core.norm_squared()-1.)<1e-12,"norm incorrect");

  AdaptivePhaseCore entangled(1000);
  entangled.hadamard(0);
  bool rejected=false;
  try { entangled.cnot(0,999); }
  catch(const std::length_error&) { rejected=true; }
  require(rejected,"unsupported multiword entanglement was silently accepted");
  bool blocked=false;
  try { (void)entangled.norm_squared(); }
  catch(const std::logic_error&) { blocked=true; }
  require(blocked,"failed transition left readable state");

  AdaptivePhaseCore legacy(63);
  legacy.hadamard(0);
  legacy.cnot(0,62);
  require(!legacy.is_product(),"63-qubit fallback regression");

  std::cout<<"{\"suite\":\"mil1000_phase\",\"qubits\":1000,\"product_t\":true,\"general_phase\":true,\"controlled_phase\":true,\"entangling_1000\":false,\"checks\":12}\n";
}
