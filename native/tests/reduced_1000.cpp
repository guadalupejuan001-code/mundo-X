#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

#include "mundo_x/adaptive_phase_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"

namespace {
using Reduced = mundo_x::ReducedExactTRepresentation;
using Adaptive = mundo_x::AdaptivePhaseCore;

std::size_t checks = 0;

void require(bool ok, const char* message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}

void near(double actual, double expected, double tolerance, const char* message) {
  ++checks;
  if (!std::isfinite(actual) || std::abs(actual - expected) > tolerance)
    throw std::runtime_error(message);
}
}  // namespace

int main() {
  try {
    constexpr std::size_t n = 1000;
    const double root_half = 1.0 / std::sqrt(2.0);

    Reduced q(n, 64);
    require(q.qubits() == n, "Reduced width is not 1000");
    require(q.base_tableau_bytes() == 257000, "1000-qubit base tableau changed");

    // Non-Clifford state on the highest qubit: (|0>+e^(i*pi/4)|1>)/sqrt(2).
    q.hadamard(999);
    q.phase_t(999);
    near(q.sparse_norm(), 1.0, 1e-12, "q999 T norm");
    near(q.pauli_expectation('X', 999), root_half, 1e-12, "q999 T X");
    near(q.pauli_expectation('Y', 999), root_half, 1e-12, "q999 T Y");
    require(q.component_count() == 2, "q999 T did not create two exact cosets");

    // Arbitrary phase on another high-word qubit.
    q.hadamard(998);
    q.phase(998, 0.37);
    near(q.pauli_expectation('X', 998), std::cos(0.37), 1e-12,
         "q998 general phase X");
    near(q.pauli_expectation('Y', 998), std::sin(0.37), 1e-12,
         "q998 general phase Y");

    // Cross the entire register: q999 controls q0. Verify the resulting
    // Bell-like Z correlation using the new multiword observable API.
    q.cnot(999, 0);
    std::vector<std::uint64_t> x(16), z(16);
    z[0] |= 1ULL;
    z[999 / 64] |= std::uint64_t{1} << (999 % 64);
    const double norm = q.sparse_norm();
    near(q.pauli_expectation(x, z).real() / norm, 1.0, 1e-12,
         "q0-q999 ZZ correlation");
    require(q.t_count() == 1, "T count changed");
    require(q.component_count() <= 4, "1000-qubit sparse support grew unexpectedly");

    // Direct controlled-phase must also cross the multiword boundary without
    // transiently expanding through three separate phase operations.
    Reduced cp(n, 64);
    cp.hadamard(0);
    cp.hadamard(999);
    cp.controlled_phase(0, 999, 0.23);
    near(cp.sparse_norm(), 1.0, 1e-12, "q0-q999 controlled-phase norm");
    require(cp.component_count() <= 4,
            "direct controlled-phase exceeded four Pauli components");

    // The adaptive front-end must also be able to transfer a 1000-qubit
    // product state into the universal reduced engine when entanglement appears.
    Adaptive adaptive(n);
    adaptive.hadamard(999);
    adaptive.phase_t(999);
    near(adaptive.pauli_expectation('X', 999), root_half, 1e-12,
         "adaptive q999 magic X");
    adaptive.cnot(999, 0);
    require(!adaptive.is_product(), "adaptive 1000 did not transfer on entanglement");
    require(adaptive.transferred_qubit_count() == n,
            "adaptive transfer did not preserve all 1000 qubits");
    near(adaptive.norm_squared(), 1.0, 1e-12, "adaptive 1000 norm");

    std::cout << "{\"suite\":\"reduced_1000\",\"ok\":true,\"checks\":"
              << checks
              << ",\"qubits\":1000,\"highest_qubit\":999,\"t_gates\":1"
              << ",\"components\":" << q.component_count()
              << ",\"base_tableau_bytes\":" << q.base_tableau_bytes()
              << ",\"adaptive_transferred\":" << adaptive.transferred_qubit_count()
              << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "reduced_1000: " << e.what() << '\n';
    return 1;
  }
}
