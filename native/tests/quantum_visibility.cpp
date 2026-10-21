#include "mundo_x/quantum_visibility.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <vector>

namespace {
using Oracle = mundo_x::QuantumVisibilityOracle;
std::size_t checks = 0;
volatile std::uint64_t sink = 0;

void require(bool ok, const char* message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}

std::size_t first_classical(std::uint32_t mask) {
  for (std::size_t id = 0; id < Oracle::kMaxBodies; ++id)
    if (mask & (std::uint32_t{1} << id)) return id;
  return Oracle::kStates;
}

unsigned count_bits(std::uint32_t value) {
  unsigned count = 0;
  while (value) { value &= value - 1U; ++count; }
  return count;
}
std::uint32_t random_sparse_mask(std::mt19937_64& rng) {
  std::uint32_t mask = 0;
  const unsigned wanted = 1U + static_cast<unsigned>(rng() % 4U);
  while (count_bits(mask) < wanted)
    mask |= std::uint32_t{1} << static_cast<unsigned>(rng() % Oracle::kMaxBodies);
  return mask;
}
}

int main() {
  try {
    Oracle oracle;
    require(std::abs(oracle.norm_squared() - 1.0) < 1e-12,
            "uniform quantum visibility state is not normalized");

    for (std::size_t id = 0; id < Oracle::kMaxBodies; ++id) {
      const auto result = oracle.amplify(std::uint32_t{1} << id);
      require(result.rounds == 4, "single-target Grover round count changed");
      require(result.most_likely_marked == id, "single target was not amplified");
      require(result.marked_probability > .999,
              "single-target quantum visibility probability is too low");
      double total = 0.0;
      for (double p : result.probabilities) total += p;
      require(std::abs(total - 1.0) < 1e-12,
              "quantum visibility probability normalization drifted");
    }

    // Multi-candidate regions remain a superposition over all possible bodies.
    const std::uint32_t three =
        (std::uint32_t{1} << 3) | (std::uint32_t{1} << 9) | (std::uint32_t{1} << 14);
    const auto multi = oracle.amplify(three);
    require(multi.marked == 3, "multi-target count changed");
    require(multi.marked_probability > 0.95,
            "multi-target marked subspace was not amplified");
    require((three & (std::uint32_t{1} << multi.most_likely_marked)) != 0,
            "most likely state is not a marked body");

    bool rejected = false;
    try { oracle.amplify(std::uint32_t{1} << 31); }
    catch (const std::out_of_range&) { rejected = true; }
    require(rejected, "non-catalogue quantum visibility state was accepted");

    // Measure the actual simulator overhead against the operation Solar needs
    // today: select a body id from a small exact candidate mask. This benchmark
    // is diagnostic only; timing is reported, not used as a pass/fail threshold.
    std::mt19937_64 rng(20261021);
    constexpr std::size_t samples = 100000;
    std::vector<std::uint32_t> masks;
    masks.reserve(samples);
    for (std::size_t i = 0; i < samples; ++i) masks.push_back(random_sparse_mask(rng));

    auto begin = std::chrono::steady_clock::now();
    std::uint64_t classical_sum = 0;
    for (auto mask : masks) classical_sum += first_classical(mask);
    auto middle = std::chrono::steady_clock::now();

    std::uint64_t quantum_sum = 0;
    double probability_sum = 0.0;
    for (auto mask : masks) {
      const auto observation = oracle.amplify(mask);
      quantum_sum += observation.most_likely_marked;
      probability_sum += observation.marked_probability;
      require((mask & (std::uint32_t{1} << observation.most_likely_marked)) != 0,
              "quantum visibility returned an unmarked body");
    }
    auto end = std::chrono::steady_clock::now();
    sink = classical_sum + quantum_sum + static_cast<std::uint64_t>(probability_sum);

    const double classical_ms =
        std::chrono::duration<double, std::milli>(middle - begin).count();
    const double quantum_ms =
        std::chrono::duration<double, std::milli>(end - middle).count();
    const double ratio = classical_ms > 0.0 ? quantum_ms / classical_ms : 0.0;

    std::cout << "{\"suite\":\"quantum_visibility\",\"checks\":" << checks
              << ",\"states\":" << Oracle::kStates
              << ",\"catalogue_bodies\":" << Oracle::kMaxBodies
              << ",\"single_target_success_gt\":0.999"
              << ",\"benchmark_samples\":" << samples
              << ",\"classical_ms\":" << classical_ms
              << ",\"quantum_ms\":" << quantum_ms
              << ",\"quantum_over_classical\":" << ratio
              << ",\"production_enabled\":false"
              << ",\"reason\":\"complete conservative mask still required\"}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "quantum_visibility: " << error.what() << '\n';
    return 1;
  }
}
