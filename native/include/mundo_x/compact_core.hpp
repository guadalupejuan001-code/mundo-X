#pragma once

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace mundo_x {

// Compact Clifford-state engine using a bit-packed stabilizer tableau.
// It supports H, S, X, Z and CNOT.
// Memory is O(n^2) bits instead of O(2^n) complex amplitudes.
class CompactCore {
 public:
  explicit CompactCore(std::size_t qubits)
      : n_(qubits), words_((qubits + 63) / 64),
        x_(qubits * words_, 0), z_(qubits * words_, 0), phase_(qubits, 0) {
    if (qubits == 0) throw std::invalid_argument("invalid qubit count");
    // |0...0> is stabilized by Z_i.
    for (std::size_t i = 0; i < n_; ++i) set(z_, i, i, true);
  }

  std::size_t qubits() const { return n_; }
  std::size_t state_bytes() const {
    return (x_.size() + z_.size()) * sizeof(std::uint64_t) + phase_.size();
  }

  void hadamard(std::size_t q) {
    check(q);
    for (std::size_t r = 0; r < n_; ++r) {
      const bool x = get(x_, r, q);
      const bool z = get(z_, r, q);
      if (x && z) phase_[r] ^= 1;
      set(x_, r, q, z);
      set(z_, r, q, x);
    }
  }

  void phase_s(std::size_t q) {
    check(q);
    for (std::size_t r = 0; r < n_; ++r) {
      const bool x = get(x_, r, q);
      const bool z = get(z_, r, q);
      if (x && z) phase_[r] ^= 1;
      set(z_, r, q, z ^ x);
    }
  }

  void pauli_x(std::size_t q) {
    check(q);
    for (std::size_t r = 0; r < n_; ++r) {
      if (get(z_, r, q)) phase_[r] ^= 1;
    }
  }

  void pauli_z(std::size_t q) {
    check(q);
    for (std::size_t r = 0; r < n_; ++r) {
      if (get(x_, r, q)) phase_[r] ^= 1;
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

    for (std::size_t r = 0; r < n_; ++r) {
      const bool xc = get(x_, r, control);
      const bool zc = get(z_, r, control);
      const bool xt = get(x_, r, target);
      const bool zt = get(z_, r, target);

      phase_[r] ^= static_cast<std::uint8_t>(xc && zt && (xt ^ zc ^ true));
      set(x_, r, target, xt ^ xc);
      set(z_, r, control, zc ^ zt);
    }
  }

  bool hello_is_one() const {
    if (n_ != 1) return false;
    return !get(x_, 0, 0) && get(z_, 0, 0) && phase_[0] == 1;
  }

  bool is_cluster_signature(std::size_t rows, std::size_t cols) const {
    if (rows == 0 || cols == 0 || rows * cols != n_) return false;

    auto is_neighbor = [rows, cols](std::size_t a, std::size_t b) {
      const std::size_t ar = a / cols;
      const std::size_t ac = a % cols;
      const std::size_t br = b / cols;
      const std::size_t bc = b % cols;
      const std::size_t dr = ar > br ? ar - br : br - ar;
      const std::size_t dc = ac > bc ? ac - bc : bc - ac;
      return (dr + dc) == 1;
    };

    for (std::size_t r = 0; r < n_; ++r) {
      if (phase_[r]) return false;

      for (std::size_t q = 0; q < n_; ++q) {
        const bool expected_x = (q == r);
        const bool expected_z = is_neighbor(r, q);

        if (get(x_, r, q) != expected_x) return false;
        if (get(z_, r, q) != expected_z) return false;
      }
    }

    return true;
  }

  bool is_ghz_signature() const {
    if (n_ < 2) return false;

    if (phase_[0]) return false;
    for (std::size_t q = 0; q < n_; ++q) {
      if (!get(x_, 0, q) || get(z_, 0, q)) return false;
    }

    for (std::size_t r = 1; r < n_; ++r) {
      if (phase_[r]) return false;
      for (std::size_t q = 0; q < n_; ++q) {
        const bool expected_z = (q == r - 1 || q == r);
        if (get(x_, r, q)) return false;
        if (get(z_, r, q) != expected_z) return false;
      }
    }
    return true;
  }

  std::string generator_string(std::size_t row) const {
    if (row >= n_) throw std::out_of_range("row");
    std::ostringstream out;
    out << (phase_[row] ? '-' : '+');

    for (std::size_t q = 0; q < n_; ++q) {
      const bool x = get(x_, row, q);
      const bool z = get(z_, row, q);
      char p = 'I';
      if (x && !z) p = 'X';
      else if (!x && z) p = 'Z';
      else if (x && z) p = 'Y';
      out << p;
    }
    return out.str();
  }

 private:
  void check(std::size_t q) const {
    if (q >= n_) throw std::out_of_range("qubit");
  }

  bool get(const std::vector<std::uint64_t>& a, std::size_t row, std::size_t q) const {
    const std::size_t idx = row * words_ + q / 64;
    return (a[idx] >> (q % 64)) & 1ULL;
  }

  void set(std::vector<std::uint64_t>& a, std::size_t row, std::size_t q, bool value) {
    const std::size_t idx = row * words_ + q / 64;
    const std::uint64_t mask = 1ULL << (q % 64);
    if (value) a[idx] |= mask;
    else a[idx] &= ~mask;
  }

  std::size_t n_;
  std::size_t words_;
  std::vector<std::uint64_t> x_;
  std::vector<std::uint64_t> z_;
  std::vector<std::uint8_t> phase_;
};

}  // namespace mundo_x
