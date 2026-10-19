#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>

namespace mundo_x {

template <typename Real>
class BasicDenseCore {
 public:
  static_assert(std::is_floating_point<Real>::value, "floating point precision required");
  using Complex = std::complex<Real>;

  explicit BasicDenseCore(std::size_t qubits) : n_(qubits) {
    if (qubits == 0 || qubits >= 63 ||
        qubits >= std::numeric_limits<std::size_t>::digits)
      throw std::invalid_argument("invalid qubit count");
    const std::size_t dim = std::size_t{1} << qubits;
    if (dim > state_.max_size())
      throw std::length_error("dense state dimension exceeds addressable capacity");
    state_.assign(dim, Complex{0.0f, 0.0f});
    state_[0] = Complex{1.0f, 0.0f};
  }

  std::size_t qubits() const { return n_; }
  std::size_t amplitudes() const { return state_.size(); }
  std::size_t state_bytes() const { return state_.size() * sizeof(Complex); }
  const std::vector<Complex>& state() const { return state_; }

  void hadamard(std::size_t q) {
    check(q);
    const std::size_t bit = std::size_t{1} << q;
    const Real s = Real{1} / std::sqrt(Real{2});
    for (std::size_t base = 0; base < state_.size(); base += bit << 1) {
      for (std::size_t i = 0; i < bit; ++i) {
        const std::size_t a = base + i;
        const std::size_t b = a + bit;
        const Complex va = state_[a];
        const Complex vb = state_[b];
        state_[a] = (va + vb) * s;
        state_[b] = (va - vb) * s;
      }
    }
  }

  void pauli_x(std::size_t q) {
    check(q);
    const std::size_t bit = std::size_t{1} << q;
    for (std::size_t base = 0; base < state_.size(); base += bit << 1) {
      for (std::size_t i = 0; i < bit; ++i) {
        std::swap(state_[base + i], state_[base + i + bit]);
      }
    }
  }

  void pauli_z(std::size_t q) {
    check(q);
    const std::size_t bit = std::size_t{1} << q;
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if (i & bit) state_[i] = -state_[i];
    }
  }

  void phase_s(std::size_t q) {
    check(q);
    const std::size_t bit = std::size_t{1} << q;
    const Complex phase{0.0f, 1.0f};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if (i & bit) state_[i] *= phase;
    }
  }

  void phase_t(std::size_t q) {
    check(q);
    const std::size_t bit = std::size_t{1} << q;
    const Real s = Real{1} / std::sqrt(Real{2});
    const Complex phase{s, s};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if (i & bit) state_[i] *= phase;
    }
  }

  void controlled_z(std::size_t a, std::size_t b) {
    check(a);
    check(b);
    if (a == b) throw std::invalid_argument("a == b");
    hadamard(b);
    cnot(a, b);
    hadamard(b);
  }

  void cnot(std::size_t control, std::size_t target) {
    check(control);
    check(target);
    if (control == target) throw std::invalid_argument("control == target");
    const std::size_t cbit = std::size_t{1} << control;
    const std::size_t tbit = std::size_t{1} << target;
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if ((i & cbit) && !(i & tbit)) {
        std::swap(state_[i], state_[i | tbit]);
      }
    }
  }

  Real cluster_generator_expectation(std::size_t vertex,
                                      std::size_t rows,
                                      std::size_t cols) const {
    if (!valid_cluster_dimensions(rows, cols))
      throw std::invalid_argument("invalid cluster dimensions");
    check(vertex);

    const std::size_t vr = vertex / cols;
    const std::size_t vc = vertex % cols;
    const std::size_t xbit = std::size_t{1} << vertex;

    std::vector<std::size_t> neighbors;
    if (vr > 0) neighbors.push_back((vr - 1) * cols + vc);
    if (vr + 1 < rows) neighbors.push_back((vr + 1) * cols + vc);
    if (vc > 0) neighbors.push_back(vr * cols + (vc - 1));
    if (vc + 1 < cols) neighbors.push_back(vr * cols + (vc + 1));

    Complex sum{0.0f, 0.0f};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      Real phase = Real{1};
      for (const auto q : neighbors) {
        if (i & (std::size_t{1} << q)) phase = -phase;
      }

      const std::size_t j = i ^ xbit;
      sum += std::conj(state_[i]) * phase * state_[j];
    }

    return sum.real();
  }

  bool cluster_is_valid(std::size_t rows, std::size_t cols,
                        Real tolerance = Real(1e-4)) const {
    if (!valid_cluster_dimensions(rows, cols)) return false;
    for (std::size_t v = 0; v < n_; ++v) {
      if (std::abs(cluster_generator_expectation(v, rows, cols) - 1.0f) >
          tolerance)
        return false;
    }
    return true;
  }

  Real probability(std::size_t index) const {
    if (index >= state_.size()) throw std::out_of_range("index");
    return std::norm(state_[index]);
  }

 private:
  bool valid_cluster_dimensions(std::size_t rows, std::size_t cols) const {
    return rows > 0 && cols > 0 && rows <= n_ && cols <= n_ &&
           n_ / cols == rows && n_ % cols == 0;
  }

  void check(std::size_t q) const {
    if (q >= n_) throw std::out_of_range("qubit");
  }

  std::size_t n_;
  std::vector<Complex> state_;
};

// Preserve the original benchmark/storage contract while allowing long
// validation runs to use a double-precision reference explicitly.
using DenseCore = BasicDenseCore<float>;
using DenseCore64 = BasicDenseCore<double>;

}  // namespace mundo_x
