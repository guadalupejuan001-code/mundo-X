#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>

namespace mundo_x {

// Five-qubit Grover-style visibility register for the 24-body Solar catalogue.
//
// This is a real quantum-state simulation: the 32 basis states start in
// superposition, a phase oracle marks body ids, and diffusion uses interference
// to amplify the marked subspace. It is still executed by a classical CPU.
//
// The class intentionally does NOT claim that a quantum search can return the
// complete candidate mask in one observation. Solar needs a complete,
// conservative mask, so this component is experimental until a full exact
// consumer contract is demonstrated and benchmarked against the classical
// geometric planner.
class QuantumVisibilityOracle {
 public:
  using Complex = std::complex<double>;
  static constexpr std::size_t kQubits = 5;
  static constexpr std::size_t kStates = std::size_t{1} << kQubits;
  static constexpr std::size_t kMaxBodies = 24;
  static constexpr std::uint32_t kBodyMask =
      (std::uint32_t{1} << kMaxBodies) - 1U;

  struct Observation {
    std::size_t rounds = 0;
    std::size_t marked = 0;
    double marked_probability = 0.0;
    std::size_t most_likely_marked = kStates;
    std::array<double, kStates> probabilities{};
  };

  QuantumVisibilityOracle() { reset_uniform(); }

  void reset_uniform() {
    const double amplitude = 1.0 / std::sqrt(static_cast<double>(kStates));
    for (auto& value : state_) value = Complex{amplitude, 0.0};
  }

  // U_f|x> = (-1)^f(x)|x>. Only catalogue ids 0..23 are accepted.
  void phase_oracle(std::uint32_t candidate_mask) {
    validate_mask(candidate_mask);
    for (std::size_t id = 0; id < kMaxBodies; ++id)
      if (candidate_mask & (std::uint32_t{1} << id)) state_[id] = -state_[id];
  }

  // 2|s><s| - I over the complete five-qubit address register.
  void diffuse() {
    Complex mean{0.0, 0.0};
    for (const auto& value : state_) mean += value;
    mean /= static_cast<double>(kStates);
    for (auto& value : state_) value = Complex{2.0, 0.0} * mean - value;
  }

  static std::size_t recommended_rounds(std::size_t marked) {
    if (marked == 0 || marked >= kStates / 2) return 0;
    if (marked > kMaxBodies) throw std::out_of_range("marked bodies exceed Solar catalogue");
    const double theta = std::asin(std::sqrt(
        static_cast<double>(marked) / static_cast<double>(kStates)));
    const double estimate = std::acos(-1.0) / (4.0 * theta) - 0.5;
    return static_cast<std::size_t>(std::max(0.0, std::floor(estimate + 0.5)));
  }

  Observation amplify(std::uint32_t candidate_mask,
                      std::size_t rounds = std::numeric_limits<std::size_t>::max()) {
    validate_mask(candidate_mask);
    reset_uniform();
    const auto marked = popcount(candidate_mask);
    if (rounds == std::numeric_limits<std::size_t>::max())
      rounds = recommended_rounds(marked);
    if (rounds > 64) throw std::out_of_range("visibility Grover round budget exceeded");
    for (std::size_t i = 0; i < rounds; ++i) {
      phase_oracle(candidate_mask);
      diffuse();
    }
    Observation result;
    result.rounds = rounds;
    result.marked = marked;
    double best = -1.0;
    for (std::size_t id = 0; id < kStates; ++id) {
      const double p = std::norm(state_[id]);
      result.probabilities[id] = p;
      if (id < kMaxBodies && (candidate_mask & (std::uint32_t{1} << id))) {
        result.marked_probability += p;
        if (p > best) {
          best = p;
          result.most_likely_marked = id;
        }
      }
    }
    return result;
  }

  double norm_squared() const {
    double total = 0.0;
    for (const auto& value : state_) total += std::norm(value);
    return total;
  }

  const std::array<Complex, kStates>& state() const noexcept { return state_; }

 private:
  static void validate_mask(std::uint32_t mask) {
    if (mask & ~kBodyMask)
      throw std::out_of_range("visibility mask contains a non-catalogue body");
  }

  static std::size_t popcount(std::uint32_t value) {
    std::size_t count = 0;
    while (value) {
      value &= value - 1U;
      ++count;
    }
    return count;
  }

  std::array<Complex, kStates> state_{};
};

}  // namespace mundo_x
