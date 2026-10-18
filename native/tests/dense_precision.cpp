#include <cmath>
#include <complex>
#include <cstddef>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>

#include "mundo_x/dense_core.hpp"

static void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

int main() {
  static_assert(std::is_same<mundo_x::DenseCore::Complex,
                             std::complex<float>>::value, "legacy precision changed");
  static_assert(std::is_same<mundo_x::DenseCore64::Complex,
                             std::complex<double>>::value, "double precision missing");
  mundo_x::DenseCore legacy(4);
  mundo_x::DenseCore64 wide(4);
  require(legacy.state_bytes() == 128, "legacy storage contract changed");
  require(wide.state_bytes() == 256, "double storage accounting is incorrect");

  mundo_x::DenseCore64 ghz(5);
  ghz.hadamard(0);
  for (std::size_t q = 1; q < 5; ++q) ghz.cnot(q - 1, q);
  require(std::abs(ghz.probability(0) - 0.5) < 1e-14 &&
          std::abs(ghz.probability(31) - 0.5) < 1e-14,
          "GHZ probabilities are incorrect");

  mundo_x::DenseCore64 dense(1);
  mundo_x::DenseCore old_dense(1);
  for (std::size_t k = 0; k < 50000; ++k) {
    dense.hadamard(0);
    dense.phase_t(0);
    old_dense.hadamard(0);
    old_dense.phase_t(0);
  }
  double norm = 0.0;
  double old_norm = 0.0;
  for (const auto& a : dense.state()) norm += std::norm(a);
  for (const auto& a : old_dense.state()) old_norm += std::norm(a);
  require(std::abs(norm - 1.0) < 1e-9,
          "long double-precision unitary evolution lost normalization");

  const auto huge = std::numeric_limits<std::size_t>::max();
  require(!dense.cluster_is_valid(huge, huge), "overflowing cluster accepted");
  bool rejected = false;
  try {
    dense.cluster_generator_expectation(0, huge, huge);
  } catch (const std::invalid_argument&) {
    rejected = true;
  }
  require(rejected, "overflowing cluster expectation accepted");
  std::cout << "PASS gates=100000 double_norm_error=" << std::abs(norm - 1.0)
            << " legacy_float_norm_error=" << std::abs(old_norm - 1.0) << '\n';
}
