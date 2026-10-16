#include <cmath>
#include <complex>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <vector>
#include "mundo_x/adaptive_phase_core.hpp"

using Core = mundo_x::AdaptivePhaseCore;
using Reduced = mundo_x::ReducedExactTRepresentation;
using Complex = std::complex<double>;
using Pair = Core::Pair;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}
template<class F> void rejects(F function) {
  bool threw = false;
  try { function(); } catch (const std::exception&) { threw = true; }
  require(threw, "expected rejection");
}

// Reference tensor product, independent of the stabilizer/coset machinery.
void verify_product(const std::vector<Pair>& product) {
  auto state = Reduced::from_product(product);
  const auto actual = state.dense_from_reduced();
  double expected_norm = 0.;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    Complex expected{1., 0.};
    for (std::size_t q = 0; q < product.size(); ++q)
      expected *= product[q][(i >> q) & 1U];
    require(std::abs(actual[i]-expected) < 2e-6, "complex amplitude or phase lost");
    expected_norm += std::norm(expected);
  }
  require(std::abs(state.coefficient_norm_squared()-expected_norm) < 1e-12,
          "import changed norm");
  for (std::size_t q = 0; q < product.size(); ++q) {
    const auto a = product[q][0], b = product[q][1];
    const double norm = std::norm(a)+std::norm(b);
    const double expected[] = {2.*(std::conj(a)*b).real()/norm,
                              2.*(std::conj(a)*b).imag()/norm,
                              (std::norm(a)-std::norm(b))/norm};
    for (std::size_t axis = 0; axis < 3; ++axis)
      require(std::abs(state.pauli_expectation("XYZ"[axis],q)-expected[axis]) < 1e-12,
              "import changed observable");
  }
  state.hadamard(0); state.phase(0,.381);
  if (product.size() > 1) {
    state.cnot(0,product.size()-1);
    state.controlled_phase(product.size()-1,0,-.227);
  }
  const auto evolved = state.dense_from_reduced();
  const auto reference = state.dense_reference();
  for (std::size_t i = 0; i < evolved.size(); ++i)
    require(std::abs(evolved[i]-reference[i]) < 2e-6,
            "evolution or reference seed changed phase");
}

int main() {
  const double s = 1./std::sqrt(2.);
  const std::vector<Pair> eigenstates{
      Pair{Complex{1.,0.},Complex{}}, Pair{Complex{},Complex{1.,0.}},
      Pair{Complex{s,0.},Complex{s,0.}}, Pair{Complex{s,0.},Complex{-s,0.}},
      Pair{Complex{s,0.},Complex{0.,s}}, Pair{Complex{s,0.},Complex{0.,-s}}};
  // Exercise every Pauli eigenstate, including nontrivial global phase/norm.
  for (auto first : eigenstates) for (auto second : eigenstates) {
    const Pair phased{first[0]*Complex{.3,.7},first[1]*Complex{.3,.7}};
    verify_product({phased,second});
  }
  std::mt19937 random(392047);
  std::uniform_real_distribution<double> angle(-3.,3.);
  for (std::size_t n = 1; n <= 7; ++n) for (int trial = 0; trial < 10; ++trial) {
    std::vector<Pair> product;
    for (std::size_t q = 0; q < n; ++q) {
      const double theta = angle(random), phi = angle(random);
      const auto phase = std::polar(1.,angle(random));
      product.push_back({phase*std::cos(theta),phase*std::sin(theta)*std::polar(1.,phi)});
    }
    verify_product(product);
  }
  auto wide = std::vector<Pair>(63,eigenstates[2]);
  auto stabilizer = Reduced::from_product(wide,1);
  require(stabilizer.component_count() == 1, "stabilizer product expanded");
  require(std::abs(stabilizer.pauli_expectation('X',62)-1.) < 1e-12, "high bit lost");
  wide.assign(63,eigenstates[0]); wide[62] = eigenstates[1];
  auto high = Reduced::from_product(wide,1);
  high.cnot(62,0);
  require(high.pauli_expectation('Z',0) == -1., "high-bit CNOT failed");

  rejects([] { Reduced::from_product({}); });
  rejects([&] { Reduced::from_product({eigenstates[0]},0); });
  rejects([] { Reduced::from_product({Pair{Complex{},Complex{}}}); });
  rejects([] { Reduced::from_product({Pair{Complex{NAN,0.},Complex{}}}); });
  rejects([] { Reduced::from_product({Pair{Complex{INFINITY,0.},Complex{}}}); });
  const Pair generic{Complex{std::sqrt(.7),0.},Complex{std::sqrt(.3),0.}};
  rejects([&] { Reduced::from_product(std::vector<Pair>(20,generic),4); });
  rejects([] { Core s(2,1e-14,65536,static_cast<Core::Transition>(99)); });

  Core transfer(50), replay(50,1e-14,65536,Core::Transition::Replay);
  transfer.hadamard(0); replay.hadamard(0);
  for (int i = 0; i < 10000; ++i) { transfer.phase(0,.001); replay.phase(0,.001); }
  require(transfer.logical_payload_bytes() == 50*sizeof(Pair), "product memory grew with depth");
  require(transfer.history_gate_count() == 0, "transfer recorded redundant history");
  require(replay.history_gate_count() == 10001, "replay compatibility lost");
  transfer.cnot(0,49);
  replay.cnot(0,49);
  require(transfer.transferred_qubit_count() == 50 && transfer.replayed_gate_count() == 0,
          "transition replayed history");
  require(replay.replayed_gate_count() == 10002, "legacy replay did not run");
  // Undo the entanglement so the accumulated relative phase is visible in
  // local X/Y observables, not hidden in correlations alone.
  transfer.cnot(0,49); replay.cnot(0,49);
  require(std::abs(transfer.pauli_expectation('X',0)-std::cos(10.)) < 1e-10,
          "long prefix lost its relative phase");
  require(std::abs(transfer.pauli_expectation('Y',0)-std::sin(10.)) < 1e-10,
          "long prefix changed relative phase sign");
  for (std::size_t q = 0; q < 50; ++q) for (char axis : {'X','Y','Z'})
    require(std::abs(transfer.pauli_expectation(axis,q)-replay.pauli_expectation(axis,q)) < 1e-10,
            "transition changed long-prefix result");

  Core residual(2);
  residual.hadamard(0); residual.hadamard(1);
  residual.controlled_phase(0,1,1e-15);
  const auto accepted = residual.product_residual_sum();
  require(accepted > 0., "tiny residual test did not activate");
  residual.hadamard(1); residual.cnot(0,1);
  require(!residual.is_product() && residual.product_residual_sum() >= accepted,
          "transfer hid previously accepted residuals");
  Core limited(3,1e-14,1);
  limited.hadamard(0); limited.phase(0,.31);
  rejects([&] { limited.cnot(0,1); });
  rejects([&] { limited.norm_squared(); });
  rejects([&] { limited.phase_s(0); });
  std::cout << "product transfer: phases, norms, observables, limits, memory, replay: PASS\n";
}
