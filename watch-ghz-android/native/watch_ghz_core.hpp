#pragma once

#include "mundo_x/quantum_runtime.hpp"

#include <chrono>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// A bounded on-watch owner of the canonical Mundo X runtime. All reported
// quantum quantities are SIMULATED. No sensors, physical qubits or remote QPU.
// No formulas stand in for core operations: preparation, S, projection and
// Pauli observations below execute against QuantumRuntime / InfiniteCore.
class WatchGhzCore {
 public:
  static constexpr std::size_t kMaxQubits = 1000;

  struct Report {
    std::size_t qubits = 0, active_qubits = 0, components = 0;
    std::size_t tableau_bytes = 0, address_bytes = 0;
    unsigned phase_quarters = 0;
    int outcome = -1;
    double probability_one = 0, x_all = 0, yx_all = 0, zz_ends = 0;
    double calculation_ms = 0;
    bool collapsed = false, all_equal_verified = false;

    std::string json() const {
      std::ostringstream out;
      out.imbue(std::locale::classic());
      out << std::setprecision(12)
          << "{\"schema\":1,\"simulated\":true,\"qubits\":" << qubits
          << ",\"active_qubits\":" << active_qubits
          << ",\"components\":" << components
          << ",\"tableau_bytes\":" << tableau_bytes
          << ",\"address_bytes\":" << address_bytes
          << ",\"phase_quarters\":" << phase_quarters
          << ",\"outcome\":" << outcome
          << ",\"probability_one\":" << probability_one
          << ",\"x_all\":" << x_all
          << ",\"yx_all\":" << yx_all
          << ",\"zz_ends\":" << zz_ends
          << ",\"calculation_ms\":" << calculation_ms
          << ",\"collapsed\":" << (collapsed ? "true" : "false")
          << ",\"all_equal_verified\":" << (all_equal_verified ? "true" : "false")
          << '}';
      return out.str();
    }
  };

  WatchGhzCore() : runtime_(empty_runtime()) {}

  Report prepare(std::size_t n, std::uint64_t seed) {
    if (n < 3 || n > kMaxQubits)
      throw std::invalid_argument("GHZ admite de 3 a 1000 qubits simulados");
    const auto start = Clock::now();
    auto candidate = empty_runtime();
    std::string program = "begin\nh 0\n";
    for (std::size_t q = 1; q < n; ++q)
      program += "cx 0 " + std::to_string(q) + "\n";
    program += "end\nseed " + std::to_string(seed) + "\n";
    candidate.execute(program);
    auto result = observe(candidate, n, 0, false, -1);
    result.calculation_ms = elapsed(start);
    runtime_ = std::move(candidate);
    report_ = result;
    return report_;
  }

  Report advance_phase() {
    require_prepared();
    if (report_.collapsed)
      throw std::logic_error("Prepara GHZ de nuevo para cambiar su fase relativa");
    const auto start = Clock::now();
    auto candidate = runtime_;
    candidate.execute("s 0\n");
    auto result = observe(candidate, report_.qubits,
                          (report_.phase_quarters + 1) % 4, false, -1);
    result.calculation_ms = elapsed(start);
    runtime_ = std::move(candidate);
    report_ = result;
    return report_;
  }

  Report measure() {
    require_prepared();
    const auto start = Clock::now();
    auto candidate = runtime_;
    candidate.execute("measure 0\n");
    const double one = candidate.core().probability_one("0");
    if (one != 0 && one != 1)
      throw std::runtime_error("La proyeccion Z no produjo un estado definido");
    auto result = observe(candidate, report_.qubits, report_.phase_quarters,
                          true, one == 1 ? 1 : 0);
    result.calculation_ms = elapsed(start);
    runtime_ = std::move(candidate);
    report_ = result;
    return report_;
  }

  Report reset() {
    runtime_ = empty_runtime();
    report_ = Report{};
    return report_;
  }

  const Report& report() const noexcept { return report_; }

 private:
  using Clock = std::chrono::steady_clock;
  static double elapsed(Clock::time_point start) {
    return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
  }

  static QuantumRuntime empty_runtime() {
    InfiniteCore::Limits limits;
    limits.max_active_qubits = kMaxQubits;
    limits.max_component_qubits = kMaxQubits;
    limits.max_state_bytes = 512 * 1024;
    limits.max_query_terms = kMaxQubits;
    limits.max_address_digits = 8;
    limits.max_ether_rules = 0;
    limits.max_ether_query_sites = kMaxQubits;
    return QuantumRuntime(InfiniteCore::Background::Zero, limits,
                          InverseMemory::Limits{});
  }

  static Report observe(const QuantumRuntime& runtime, std::size_t n,
                        unsigned phase, bool collapsed, int outcome) {
    Report result;
    result.qubits = n;
    result.phase_quarters = phase;
    result.collapsed = collapsed;
    result.outcome = outcome;
    const auto& core = runtime.core();
    const auto stats = core.stats();
    result.active_qubits = stats.active_qubits;
    result.components = stats.components;
    result.tableau_bytes = stats.tableau_bytes;
    result.address_bytes = stats.address_bytes;
    result.probability_one = core.probability_one("0");
    std::vector<InfiniteCore::Term> terms;
    terms.reserve(n);
    for (std::size_t q = 0; q < n; ++q)
      terms.push_back({std::to_string(q), 'X'});
    result.x_all = core.expectation(terms);
    terms.front().axis = 'Y';
    result.yx_all = core.expectation(terms);
    result.zz_ends = core.expectation({{"0", 'Z'}, {std::to_string(n - 1), 'Z'}});
    if (collapsed) {
      result.all_equal_verified = true;
      for (std::size_t q = 0; q < n; ++q)
        if (core.probability_one(std::to_string(q)) != outcome)
          throw std::runtime_error("La correlacion GHZ proyectada no coincide");
    }
    return result;
  }

  void require_prepared() const {
    if (report_.qubits == 0) throw std::logic_error("Primero prepara un estado GHZ");
  }

  QuantumRuntime runtime_;
  Report report_;
};

}  // namespace mundo_x
