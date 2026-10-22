#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "mundo_x/adaptive_phase_core.hpp"

void require(bool ok) { if (!ok) throw std::runtime_error("phase test failed"); }
template<class F> void rejects(F f) {
  bool threw = false;
  try { f(); } catch (const std::exception&) { threw = true; }
  require(threw);
}
int main() {
  using mundo_x::AdaptivePhaseCore;
  rejects([] { AdaptivePhaseCore s(0); });
  rejects([] { AdaptivePhaseCore s(1001); });
  rejects([] { AdaptivePhaseCore s(2, -1.); });
  rejects([] { AdaptivePhaseCore s(2, 1e-14, 0); });
  AdaptivePhaseCore s(2);
  rejects([&] { s.phase(0, std::numeric_limits<double>::quiet_NaN()); });
  rejects([&] { s.controlled_phase(0, 1, std::numeric_limits<double>::infinity()); });
  rejects([&] { s.controlled_phase(0, 0, .3); });
  rejects([&] { s.hadamard(2); });
  require(s.gate_count() == 0 && s.norm_squared() == 1.);
  s.hadamard(0); s.phase_s(0);
  require(std::abs(s.pauli_expectation('Y',0)-1.) < 1e-14);
  s.cnot(0,1);
  require(!s.is_product());
  require(std::abs(s.pauli_expectation('Z',0)) < 1e-14);
  require(std::abs(s.norm_squared()-1.) < 1e-14);

  AdaptivePhaseCore limited(2,1e-14,1);
  limited.hadamard(0); limited.hadamard(1);
  rejects([&] { limited.controlled_phase(0,1,.31); });
  rejects([&] { limited.norm_squared(); });
  rejects([&] { limited.pauli_expectation('Z',0); });
  rejects([&] { limited.hadamard(0); });
  rejects([&] { limited.dense_state(); });

  AdaptivePhaseCore large(50);
  large.phase(49,.17);
  rejects([&] { large.dense_state(); });

  AdaptivePhaseCore thousand(1000);
  thousand.hadamard(999); thousand.phase_t(999);
  require(std::abs(thousand.pauli_expectation('X',999)-std::sqrt(.5)) < 1e-12);
  require(std::abs(thousand.pauli_expectation('Y',999)-std::sqrt(.5)) < 1e-12);
  thousand.cnot(999,0);
  require(!thousand.is_product());
  require(thousand.transferred_qubit_count()==1000);
  require(std::abs(thousand.norm_squared()-1.) < 1e-12);
  mundo_x::ReducedExactTRepresentation reduced(3);
  reduced.hadamard(0); reduced.phase(0,.37); reduced.cnot(0,1);
  reduced.hadamard(2); reduced.controlled_phase(1,2,-.191);
  reduced.phase_t(0); reduced.phase(1,.52); reduced.hadamard(1);
  const auto a=reduced.dense_from_reduced(), b=reduced.dense_reference();
  for (std::size_t i=0; i<a.size(); ++i) require(std::abs(a[i]-b[i]) < 1e-6);
  require(std::abs(reduced.coefficient_norm_squared()-1.) < 1e-14);
  require(reduced.t_count()==1);
  std::cout << "phase API, fallback, replay, guards: PASS\n";
}
