#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace mundo_x {

class DenseCore {
 public:
  using Complex = std::complex<float>;

  explicit DenseCore(std::size_t qubits) : n_(qubits) {
    if (qubits == 0 || qubits >= 63) throw std::invalid_argument("invalid qubit count");
    const std::size_t dim = std::size_t{1} << qubits;
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
    const float s = 1.0f / std::sqrt(2.0f);
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
    const float s = 1.0f / std::sqrt(2.0f);
    const Complex phase{s, s};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if (i & bit) state_[i] *= phase;
    }
  }

  void phase(std::size_t q, double theta) {
    check(q);
    if (!std::isfinite(theta)) throw std::invalid_argument("non-finite phase");
    const auto bit = std::size_t{1} << q;
    const Complex factor{static_cast<float>(std::cos(theta)),
                         static_cast<float>(std::sin(theta))};
    for (std::size_t i = 0; i < state_.size(); ++i)
      if (i & bit) state_[i] *= factor;
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

  float cluster_generator_expectation(std::size_t vertex,
                                      std::size_t rows,
                                      std::size_t cols) const {
    if (rows == 0 || cols == 0 || rows * cols != n_)
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
      float phase = 1.0f;
      for (const auto q : neighbors) {
        if (i & (std::size_t{1} << q)) phase = -phase;
      }

      const std::size_t j = i ^ xbit;
      sum += std::conj(state_[i]) * phase * state_[j];
    }

    return sum.real();
  }

  bool cluster_is_valid(std::size_t rows, std::size_t cols,
                        float tolerance = 1e-4f) const {
    if (rows == 0 || cols == 0 || rows * cols != n_) return false;
    for (std::size_t v = 0; v < n_; ++v) {
      if (std::abs(cluster_generator_expectation(v, rows, cols) - 1.0f) >
          tolerance)
        return false;
    }
    return true;
  }

  float probability(std::size_t index) const {
    if (index >= state_.size()) throw std::out_of_range("index");
    return std::norm(state_[index]);
  }

 private:
  void check(std::size_t q) const {
    if (q >= n_) throw std::out_of_range("qubit");
  }

  std::size_t n_;
  std::vector<Complex> state_;
};

}  // namespace mundo_x
