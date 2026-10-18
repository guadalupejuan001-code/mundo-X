#include "mundo_x/dense_core.hpp"
#include "mundo_x/factorized_core.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void compare(const mundo_x::FactorizedCore& factor,
             const mundo_x::DenseCore64& reference, double tolerance = 1e-11) {
  const auto actual = factor.dense_state();
  require(actual.size() == reference.state().size(), "state dimension");
  double norm = 0.0;
  for (const auto value : reference.state()) norm += std::norm(value);
  for (std::size_t i = 0; i < actual.size(); ++i)
    require(std::abs(actual[i] - reference.state()[i]) < tolerance,
            "phase-sensitive amplitude disagreement");
  for (std::size_t q = 0; q < factor.qubits(); ++q) {
    double mass = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i)
      if ((i >> q) & 1) mass += std::norm(reference.state()[i]);
    require(std::abs(factor.probability_one(q) - mass / norm) < tolerance,
            "local normalized probability");
    require(std::abs(factor.expectation_z(q) - (1.0 - 2.0 * mass / norm)) < tolerance,
            "local Z expectation");
  }
}
template <class Exception, class Function>
void throws(Function fn, const std::string& message) {
  bool caught = false;
  try { fn(); } catch (const Exception&) { caught = true; }
  require(caught, message);
}
void random_correctness() {
  std::mt19937 rng(29071991);
  for (std::size_t n = 1; n <= 9; ++n) {
    mundo_x::FactorizedCore factor(n);
    mundo_x::DenseCore64 reference(n);
    for (std::size_t step = 0; step < 240; ++step) {
      const std::size_t q = rng() % n;
      const std::size_t target = (n == 1) ? 0 : (q + 1 + rng() % (n - 1)) % n;
      const auto gate = rng() % ((n == 1) ? 5 : 7);
      switch (gate) {
        case 0: factor.hadamard(q); reference.hadamard(q); break;
        case 1: factor.phase_s(q); reference.phase_s(q); break;
        case 2: factor.phase_t(q); reference.phase_t(q); break;
        case 3: factor.pauli_x(q); reference.pauli_x(q); break;
        case 4: factor.pauli_z(q); reference.pauli_z(q); break;
        case 5: factor.cnot(q, target); reference.cnot(q, target); break;
        case 6: factor.controlled_z(q, target); reference.controlled_z(q, target); break;
      }
      if (step % 7 == 0) {
        factor.try_factorize(q);
        compare(factor, reference);
      }
    }
    compare(factor, reference);
  }
}
void product_and_phase() {
  mundo_x::FactorizedCore factor(4);
  mundo_x::DenseCore64 reference(4);
  // XZXZ = -I: compare the global phase, not just fidelity/probabilities.
  for (int i = 0; i < 2; ++i) {
    factor.pauli_x(0); reference.pauli_x(0);
    factor.pauli_z(0); reference.pauli_z(0);
  }
  factor.hadamard(2); reference.hadamard(2);
  factor.phase_t(2); reference.phase_t(2);
  factor.cnot(0, 2); reference.cnot(0, 2);
  require(factor.factor_count() == 4, "zero control remains independent");
  compare(factor, reference);
  factor.pauli_x(0); reference.pauli_x(0);
  factor.cnot(0, 2); reference.cnot(0, 2);
  factor.controlled_z(0, 2); reference.controlled_z(0, 2);
  require(factor.factor_count() == 4, "one control remains independent");
  compare(factor, reference);
  require(factor.try_factorize(2), "singleton already factorized");
  require(factor.factor_qubit_count(2) == 1, "constant-time factor width");
  require(!factor.same_factor(0, 2), "direct factor identity lookup");
}
void separation() {
  mundo_x::FactorizedCore factor(4);
  mundo_x::DenseCore64 reference(4);
  factor.hadamard(0); reference.hadamard(0);
  factor.cnot(0, 1); reference.cnot(0, 1);
  require(!factor.try_factorize(0), "Bell qubit must remain entangled");
  require(factor.factor_count() == 3, "Bell partition");
  factor.cnot(0, 1); reference.cnot(0, 1);
  require(factor.try_factorize(0), "uncomputed Bell splits strictly");
  require(factor.factor_count() == 4, "recovered singleton factors");
  compare(factor, reference);

  mundo_x::FactorizedCore pairs(4);
  mundo_x::DenseCore64 pairs_reference(4);
  for (const auto q : {0u, 2u}) {
    pairs.hadamard(q); pairs_reference.hadamard(q);
    pairs.cnot(q, q + 1); pairs_reference.cnot(q, q + 1);
  }
  pairs.phase_s(0); pairs_reference.phase_s(0);
  pairs.cnot(1, 2); pairs_reference.cnot(1, 2);
  require(!pairs.try_split({0, 1}), "entangling bridge not separable");
  pairs.cnot(1, 2); pairs_reference.cnot(1, 2);
  require(!pairs.try_factorize(0), "Bell pair has no singleton split");
  require(pairs.try_split({0, 1}), "known Bell-pair cut recovers factors");
  require(pairs.factor_count() == 2, "two pair factors recovered");
  compare(pairs, pairs_reference);

  // Noncontiguous cut and phase attribution after a merge/uncompute.
  mundo_x::FactorizedCore crossed(4);
  mundo_x::DenseCore64 crossed_reference(4);
  for (const auto q : {0u, 1u}) {
    crossed.hadamard(q); crossed_reference.hadamard(q);
    crossed.cnot(q, q + 2); crossed_reference.cnot(q, q + 2);
  }
  crossed.phase_s(1); crossed_reference.phase_s(1);
  crossed.cnot(2, 3); crossed_reference.cnot(2, 3);
  crossed.cnot(2, 3); crossed_reference.cnot(2, 3);
  require(crossed.try_split({2, 0}), "noncontiguous explicit cut");
  compare(crossed, crossed_reference);
}
void rejection_transaction() {
  mundo_x::FactorizedCore bounded(3, 2);
  bounded.hadamard(0);
  bounded.cnot(0, 1);
  bounded.hadamard(2);
  bounded.phase_t(2);  // An X eigenstate would not require a merge for CX.
  const auto before = bounded.dense_state();
  const auto partition = bounded.block_partition();
  throws<std::length_error>([&] { bounded.cnot(1, 2); }, "block budget rejects");
  require(bounded.dense_state() == before, "rejected merge preserves coefficients");
  require(bounded.block_partition() == partition, "rejected merge preserves partition");
  throws<std::invalid_argument>([&] { bounded.cnot(1, 1); }, "same qubit rejects");
  throws<std::out_of_range>([&] { bounded.controlled_z(0, 3); }, "bad qubit rejects");
  throws<std::out_of_range>([&] { bounded.hadamard(3); }, "bad local qubit rejects");
  throws<std::invalid_argument>([&] { bounded.try_split({0, 0}); }, "duplicate split rejects");
  throws<std::invalid_argument>([&] { bounded.try_split({0, 2}); }, "cross-factor split rejects");
  throws<std::invalid_argument>([&] { bounded.try_split({}); }, "empty split rejects");
  require(bounded.dense_state() == before, "invalid inputs preserve coefficients");
  require(bounded.block_partition() == partition, "invalid inputs preserve partition");
  throws<std::length_error>([&] { bounded.dense_state(2); }, "dense export cap");
  throws<std::invalid_argument>([&] { bounded.amplitude({false}); }, "basis width check");
  throws<std::invalid_argument>([] { mundo_x::FactorizedCore x(0); }, "zero qubits reject");
  throws<std::invalid_argument>([] { mundo_x::FactorizedCore x(1, 64); }, "unsafe shift rejects");
  throws<std::length_error>([] { mundo_x::FactorizedCore x(100, 20, 16); }, "initial payload budget");
  mundo_x::FactorizedCore payload(3, 3, 6 * sizeof(std::complex<double>));
  payload.hadamard(0); payload.cnot(0, 1); payload.hadamard(2);
  const auto payload_before = payload.dense_state();
  throws<std::length_error>([&] { payload.controlled_z(1, 2); }, "logical payload rejects");
  require(payload.dense_state() == payload_before, "payload rejection preserves coefficients");
}
void large_product() {
  constexpr std::size_t n = 100000;
  mundo_x::FactorizedCore product(n);
  for (std::size_t q = 0; q < n; ++q) {
    product.hadamard(q);
    product.phase_t(q);
    if (q % 997 == 0)
      require(std::abs(product.probability_one(q) - 0.5) < 1e-15,
              "large product local probability");
  }
  require(product.factor_count() == n, "product factors grow linearly");
  require(product.amplitude_count() == 2 * n, "product payload linear");
  require(product.largest_factor_qubits() == 1, "product maximum factor width");
  require(product.logical_amplitude_bytes() == 2 * n * sizeof(std::complex<double>),
          "logical payload excludes metadata");
  throws<std::length_error>([&] { product.dense_state(); }, "large dense export guarded");
  std::vector<bool> bits(n, true);
  const auto logarithm = product.log_amplitude(bits);
  require(!logarithm.zero, "log query distinguishes nonzero from raw underflow");
  require(std::abs(logarithm.log_magnitude + double(n) * std::log(2.0) / 2.0) < 1e-9,
          "large product compensated logarithm");
  require(std::abs(logarithm.phase) < 1e-10, "large product phase modulo two pi");
  require(product.amplitude(bits) == mundo_x::FactorizedCore::Complex{0.0, 0.0},
          "raw double product underflow is explicit");
  mundo_x::FactorizedCore basis(2);
  const auto zero = basis.log_amplitude({true, false});
  require(zero.zero && std::isinf(zero.log_magnitude) && zero.log_magnitude < 0,
          "log query reports actual basis zero");
  throws<std::invalid_argument>([&] { product.log_amplitude({true}); }, "log basis width check");
}
void repeated_strict_splits() {
  mundo_x::FactorizedCore factor(2);
  mundo_x::DenseCore64 reference(2);
  factor.hadamard(0); reference.hadamard(0);
  factor.hadamard(1); reference.hadamard(1);
  for (std::size_t step = 0; step < 2000; ++step) {
    factor.controlled_z(0, 1); reference.controlled_z(0, 1);
    factor.controlled_z(0, 1); reference.controlled_z(0, 1);
    require(factor.try_split({0}), "repeated known product cut reconstructs strictly");
    require(factor.largest_factor_qubits() == 1, "histogram after repeated split");
    if (step % 100 == 0) compare(factor, reference, 1e-10);
  }
  compare(factor, reference, 1e-10);
}
}

int main() {
  try {
    random_correctness();
    product_and_phase();
    separation();
    rejection_transaction();
    large_product();
    repeated_strict_splits();
    std::cout << "factorized_core PASS checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "factorized_core FAIL: " << error.what() << '\n';
    return 1;
  }
}
