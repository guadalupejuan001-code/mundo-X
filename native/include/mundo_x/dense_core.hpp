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
