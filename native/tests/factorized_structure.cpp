#include "mundo_x/dense_core.hpp"
#include "mundo_x/factorized_core.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Core = mundo_x::FactorizedCore;
using Dense = mundo_x::DenseCore64;
std::size_t checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void compare(const Core& core, const Dense& dense) {
  const auto actual = core.dense_state();
  for (std::size_t i = 0; i < actual.size(); ++i)
    require(std::abs(actual[i] - dense.state()[i]) < 1e-12,
            "phase-sensitive dense comparison");
}

// Test +/-/0/1 targets with all four global phases, with the control itself
// entangled to another qubit. A probability-only check would miss phase kickback.
void singleton_eigenstates() {
  for (unsigned kind = 0; kind < 4; ++kind) {
    for (unsigned phase = 0; phase < 4; ++phase) {
      Core core(3, 2);
      Dense dense(3);
      core.hadamard(0); dense.hadamard(0);
      core.phase_t(0); dense.phase_t(0);
      core.cnot(0, 1); dense.cnot(0, 1);
      core.pauli_x(2); dense.pauli_x(2);
      for (unsigned p = 0; p < phase; ++p) {
        core.phase_s(2); dense.phase_s(2);
      }
      if (kind == 0) { core.pauli_x(2); dense.pauli_x(2); }
      if (kind >= 2) {
        core.hadamard(2); dense.hadamard(2);
        if (kind == 2) { core.pauli_z(2); dense.pauli_z(2); }
      }
      const auto partition = core.block_partition();
      const auto storage = core.storage_estimate().estimated_reserved_bytes;
      for (unsigned repeat = 0; repeat < 3; ++repeat) {
        if (kind < 2) {
          core.controlled_z(1, 2); dense.controlled_z(1, 2);
        } else {
          core.cnot(1, 2); dense.cnot(1, 2);
        }
        compare(core, dense);
        require(core.block_partition() == partition, "eigenstate needs no merge");
        require(core.storage_estimate().estimated_reserved_bytes == storage,
                "eigenstate operation leaves owned storage unchanged");
      }
    }
  }
}

void genuine_entanglement_still_rejects() {
  for (const bool cz : {false, true}) {
    Core core(2, 1);
    core.hadamard(0);
    core.hadamard(1);
    if (!cz) core.phase_t(1);
    const auto before = core.dense_state();
    bool rejected = false;
    try {
      if (cz) core.controlled_z(0, 1); else core.cnot(0, 1);
    } catch (const std::length_error&) { rejected = true; }
    require(rejected, "genuine entanglement cannot bypass block budget");
    require(core.dense_state() == before, "rejection preserves amplitudes");
    require(core.factor_count() == 2, "rejection preserves factors");
  }
}

void dense_split_phase() {
  Core core(12, 2);
  Dense dense(12);
  for (std::size_t q = 0; q < 12; ++q) {
    core.hadamard(q); dense.hadamard(q);
  }
  core.phase_s(0); dense.phase_s(0);
  for (std::size_t q = 1; q < 12; ++q) {
    core.controlled_z(0, q); dense.controlled_z(0, q);
    require(!core.try_factorize(0), "actual entanglement cannot split");
    core.controlled_z(0, q); dense.controlled_z(0, q);
    require(core.try_factorize(0), "uncomputed product strictly separates");
    compare(core, dense);
  }
}

void fresh_factor_gauge_regression(bool retain_left) {
  constexpr std::size_t n = 8192;
  Core core(n, 2);
  for (std::size_t q = 0; q < n; ++q) core.hadamard(q);
  core.phase_s(0);
  // Use CZ, so the CX eigenstate optimization cannot hide the old gauge bug.
  // Each pair is the identity; fresh partners used to move their scale into
  // factor 0 until probability_one failed on partner 1074.
  for (std::size_t q = 1; q < n; ++q) {
    core.controlled_z(0, q);
    core.controlled_z(0, q);
    require(core.try_factorize(retain_left ? 0 : q), "fresh product cut must remain exact");
    require(std::abs(core.probability_one(0) - 0.5) < 1e-15,
            "repeated redistribution preserves control probability");
    require(std::abs(core.probability_one(q) - 0.5) < 1e-15,
            "repeated redistribution preserves partner probability");
  }
  require(core.factor_count() == n, "all recovered factors remain independent");
  require(core.logical_amplitude_bytes() == 32 * n, "recovered payload stays linear");
  std::vector<bool> bits(n, false);
  bits[0] = true;
  const auto logarithm = core.log_amplitude(bits);
  require(!logarithm.zero, "gauge redistribution must not erase a coefficient");
  require(std::abs(logarithm.log_magnitude + n * std::log(2.0) / 2) < 1e-9,
          "full-state log magnitude survives redistribution");
  require(std::abs(logarithm.phase - std::acos(-1.0) / 2) < 1e-14,
          "full-state complex phase survives redistribution");
}

void wide_nonentangling_fanout() {
  constexpr std::size_t n = 100000;
  Core core(n, 1);
  for (std::size_t q = 0; q < n; ++q) core.hadamard(q);
  core.phase_t(0);
  for (std::size_t q = 1; q < n; ++q) core.cnot(0, q);
  require(core.factor_count() == n, "CX plus targets stay in independent factors");
  require(core.logical_amplitude_bytes() == 32 * n, "fanout payload remains linear");
  require(core.storage_estimate().multiqubit_amplitude_capacity_bytes == 0,
          "fanout never allocates a dense block");
  std::vector<bool> bits(n, false);
  bits[0] = true;
  const auto amplitude = core.log_amplitude(bits);
  require(!amplitude.zero, "nonentangling circuit amplitude remains nonzero");
  require(std::abs(amplitude.log_magnitude + n * std::log(2.0) / 2) < 1e-9,
          "fanout log magnitude");
  require(std::abs(amplitude.phase - std::acos(-1.0) / 4) < 1e-14,
          "fanout non-Clifford phase");
}
}

int main() {
  try {
    singleton_eigenstates();
    genuine_entanglement_still_rejects();
    dense_split_phase();
    fresh_factor_gauge_regression(true);
    fresh_factor_gauge_regression(false);
    wide_nonentangling_fanout();
    std::cout << "factorized_structure PASS checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "factorized_structure FAIL: " << error.what() << '\n';
    return 1;
  }
}
