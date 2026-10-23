#pragma once

#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/quantum_runtime.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace mundo_x {

// Explicit, reproducible, CLASSICAL hardware bridge:
// sensors -> MIL/Reduced quantum simulation -> bounded output setting.
// No physical qubits exist in a smartwatch or in this C++ simulator.
//
// Two CANONICAL Mundo X backends, without copied implementations:
//  1. MIL ReducedExactTRepresentation: 1000 logical qubits, exact
//     non-Clifford Pauli-component evolution for this bounded circuit.
//  2. QuantumRuntime/InverseMemory: 256-page COW journal as of PR #54,
//     with atomic XI operations and no hidden measurement.
//
// This is not a physical Bell experiment. ZZ and XX are calculated
// expectations of one software state. They do not establish an
// exterior CHSH violation or photon entanglement.
class WorldLinkQuantumBridge {
 public:
  static constexpr std::size_t kQubits = 1000;
  static constexpr std::size_t kTarget = 999;
  static constexpr std::size_t kMaxComponents = 32;
  static constexpr std::uint64_t kJournalSlots = 128;
  static constexpr double kMinHz = 180.0;
  static constexpr double kMaxHz = 1200.0;

  struct Report {
    bool magnetic_available = false;
    bool motion_available = false;
    bool light_available = false;
    double phase_radians = 0.0;
    double xx = 0.0;
    double zz = 0.0;
    double norm = 0.0;
    int frequency_hz = 440;
    std::uint64_t event = 0;
    std::size_t components = 0;
    std::size_t mil_logical_bytes = 0;
    std::size_t ether_active_qubits = 0;
    std::size_t ether_memory_overrides = 0;
    double calculation_ms = 0.0;
  };

  WorldLinkQuantumBridge() {
    // Ether itself prepares an actual entangled *SIMULATED* stabilizer state
    // on q0 / q999, never referencing physical qubits or hardware sensors.
    ether_.execute("h 0\ncx 0 999\n");
    auto state = std::make_unique<ReducedExactTRepresentation>(
        kQubits, kMaxComponents);
    state->hadamard(0);
    state->cnot(0, kTarget);
    mil_ = std::move(state);
  }

  static bool available(double value) {
    return std::isfinite(value) && value >= 0.;
  }

  // A reading of -1 means the device did NOT supply that sensor.
  // Readings outside reasonable numerical domains are clamped, never used
  // as an excuse to request excessive sound amplitude/frequency.
  Report evolve(double magnetic_uT, double angular_rate, double lux) {
    const auto begin = std::chrono::steady_clock::now();
    Report out;
    out.magnetic_available = available(magnetic_uT);
    out.motion_available = available(angular_rate);
    out.light_available = available(lux);

    // Three *classical* inputs become bounded phase controls for a
    // genuine MIL simulated quantum circuit. No values are fabricated
    // to make a missing sensor look present.
    const double magnet = out.magnetic_available
        ? std::clamp((magnetic_uT - 40.0) / 120.0, -1.0, 1.0)
        : 0.0;
    const double twist = out.motion_available
        ? std::clamp(angular_rate / 9.0, 0.0, 1.0)
        : 0.0;
    const double illumination = out.light_available
        ? std::clamp(lux / 800.0, 0.0, 1.0)
        : 0.0;

    constexpr double kPi = 3.1415926535897932384626433832795;
    constexpr double kControlledPhase = 0.19;
    const double phase = kPi * (0.60 * magnet + 0.65 * twist +
                                0.25 * illumination);
    out.phase_radians = phase + kPi / 4.0 + kControlledPhase;

    // Every run reconstructs an independently bounded circuit, rather than
    // repeatedly growing non-Clifford branches without a stopping condition.
    auto candidate = std::make_unique<ReducedExactTRepresentation>(
        kQubits, kMaxComponents);
    candidate->hadamard(0);
    candidate->cnot(0, kTarget);
    candidate->phase_t(kTarget);
    candidate->phase(kTarget, phase);
    candidate->controlled_phase(0, kTarget, kControlledPhase);

    const double norm = candidate->sparse_norm();
    if (!(norm > 0.) || !std::isfinite(norm))
      throw std::runtime_error("MIL normalization unavailable");

    // Crucial multiword Pauli observables: q0 AND q999.
    // No 64-bit truncation; 1000 qubits are represented by 16 words.
    std::vector<std::uint64_t> mx(16, 0), mz(16, 0), zero(16, 0);
    mx[0] = 1ULL;
    mx[kTarget / 64] |= std::uint64_t{1} << (kTarget % 64);
    mz[0] = 1ULL;
    mz[kTarget / 64] |= std::uint64_t{1} << (kTarget % 64);

    const auto xx_complex = candidate->pauli_expectation(mx, zero);
    const auto zz_complex = candidate->pauli_expectation(zero, mz);
    if (std::abs(xx_complex.imag()) > 1e-8 ||
        std::abs(zz_complex.imag()) > 1e-8)
      throw std::runtime_error("MIL Pauli output is not a real expectation");

    out.norm = norm;
    out.xx = std::clamp(xx_complex.real() / norm, -1.0, 1.0);
    out.zz = std::clamp(zz_complex.real() / norm, -1.0, 1.0);
    if (std::abs(out.zz - 1.0) > 1e-8)
      throw std::runtime_error("MIL Bell ZZ parity failed");

    out.components = candidate->component_count();
    out.mil_logical_bytes = candidate->logical_payload_bytes();
    const double proportion = std::clamp(0.5 * (out.xx + 1.), 0., 1.);
    out.frequency_hz = static_cast<int>(std::lround(
        kMinHz + (kMaxHz - kMinHz) * proportion));

    // Keep a bounded, inspectable *classical* journal in Ether's optimized
    // InverseMemory. execute is atomic; no implicit measurement is performed.
    // Circular address mapping avoids unbounded memory growth on-device.
    const std::uint64_t next = event_ + 1;
    const std::string address = std::to_string(1000000 + (next % kJournalSlots));
    const unsigned byte = static_cast<unsigned>(1 + std::lround(
        std::clamp(proportion, 0., 1.) * 254.0));
    ether_.execute("mem_store " + address + " " +
                   std::to_string(byte) + "\n");

    // Read-after-write directly from the *canonical* Ether runtime,
    // using only a deterministic classical view, never collapsing qubits.
    const auto persisted = ether_.memory().read(address, ether_.core());
    if (!persisted.deterministic || persisted.value != byte)
      throw std::runtime_error("Ether memory journal lost a control event");

    mil_ = std::move(candidate);
    event_ = next;
    out.event = event_;
    out.ether_active_qubits = ether_.core().stats().active_qubits;
    out.ether_memory_overrides = ether_.memory().stats().overrides;
    out.calculation_ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - begin).count();
    return out;
  }

  void reset() {
    WorldLinkQuantumBridge candidate;
    *this = std::move(candidate);
  }

  std::uint64_t events() const noexcept { return event_; }
  std::size_t active_simulated_qubits() const {
    return ether_.core().stats().active_qubits;
  }

 private:
  QuantumRuntime ether_;
  std::unique_ptr<ReducedExactTRepresentation> mil_;
  std::uint64_t event_ = 0;
};

}  // namespace mundo_x
