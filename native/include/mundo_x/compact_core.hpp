#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
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

  // Compact, deterministic observables used by the Android visual experiment.
  // These are derived from the stabilizer tableau itself; they do not expose or
  // materialize a dense 2^n statevector.
  std::array<float, 8> visual_metrics() const {
    std::uint64_t x_count = 0;
    std::uint64_t z_count = 0;
    std::uint64_t y_count = 0;
    std::uint64_t phase_count = 0;
    std::uint64_t diagonal_x = 0;
    std::uint64_t diagonal_z = 0;
    std::uint64_t neighbor_activity = 0;
    std::uint64_t hash = 1469598103934665603ULL;

    for (std::size_t r = 0; r < n_; ++r) {
      phase_count += phase_[r] ? 1ULL : 0ULL;
      for (std::size_t q = 0; q < n_; ++q) {
        const bool xb = get(x_, r, q);
        const bool zb = get(z_, r, q);
        x_count += xb ? 1ULL : 0ULL;
        z_count += zb ? 1ULL : 0ULL;
        y_count += (xb && zb) ? 1ULL : 0ULL;
        if (r == q) {
          diagonal_x += xb ? 1ULL : 0ULL;
          diagonal_z += zb ? 1ULL : 0ULL;
        }
        if (q + 1 < n_ && (xb || zb) &&
            (get(x_, r, q + 1) || get(z_, r, q + 1))) {
          neighbor_activity += 1ULL;
        }

        const std::uint64_t code =
            (xb ? 1ULL : 0ULL) | (zb ? 2ULL : 0ULL) |
            ((r + 1ULL) << 2ULL) ^ ((q + 1ULL) << 17ULL);
        hash ^= code;
        hash *= 1099511628211ULL;
      }
      hash ^= static_cast<std::uint64_t>(phase_[r]) + (r << 8ULL);
      hash *= 1099511628211ULL;
    }

    const float cells = static_cast<float>(n_ * n_);
    const float rows = static_cast<float>(n_);
    const float neighbor_den =
        n_ > 1 ? static_cast<float>(neighbor_activity) /
                     static_cast<float>(n_ * (n_ - 1))
               : 0.0f;
    const float hash_unit =
        static_cast<float>((hash >> 40ULL) & 0xFFFFFFULL) / 16777215.0f;

    return {
        static_cast<float>(x_count) / cells,
        static_cast<float>(z_count) / cells,
        static_cast<float>(y_count) / cells,
        static_cast<float>(phase_count) / rows,
        static_cast<float>(diagonal_x) / rows,
        static_cast<float>(diagonal_z) / rows,
        neighbor_den,
        hash_unit,
    };
  }


  // Projective Z measurement for a branch whose result is chosen explicitly.
  // Returns false only when Z_q is already deterministic in the current state.
  // This is useful for reproducible Clifford experiments such as teleportation,
  // where both Bell-measurement outcomes are non-deterministic branches.
  bool measure_z_forced(std::size_t q, bool outcome_one) {
    check(q);

    std::size_t pivot = n_;
    for (std::size_t r = 0; r < n_; ++r) {
      if (get(x_, r, q)) {
        pivot = r;
        break;
      }
    }

    if (pivot == n_) return false;

    // Make every non-pivot generator commute with the measured Z_q by
    // eliminating X/Y on the measured column.
    for (std::size_t r = 0; r < n_; ++r) {
      if (r != pivot && get(x_, r, q)) multiply_rows(r, pivot);
    }

    clear_row(pivot);
    set(z_, pivot, q, true);
    phase_[pivot] = outcome_one ? 1 : 0;

    // Canonicalize the measured qubit so all other rows are identity there.
    for (std::size_t r = 0; r < n_; ++r) {
      if (r != pivot && get(z_, r, q)) multiply_rows(r, pivot);
    }

    return true;
  }

  // Returns I/X/Y/Z when an explicit single-qubit stabilizer generator is
  // present after canonicalization. The optional output receives its sign.
  char single_qubit_stabilizer(std::size_t q, bool* negative = nullptr) const {
    check(q);
    for (std::size_t r = 0; r < n_; ++r) {
      bool only_q = true;
      for (std::size_t k = 0; k < n_; ++k) {
        if (k == q) continue;
        if (get(x_, r, k) || get(z_, r, k)) {
          only_q = false;
          break;
        }
      }
      if (!only_q) continue;

      const bool xb = get(x_, r, q);
      const bool zb = get(z_, r, q);
      if (!xb && !zb) continue;

      if (negative) *negative = phase_[r] != 0;
      if (xb && zb) return 'Y';
      if (xb) return 'X';
      return 'Z';
    }

    if (negative) *negative = false;
    return 'I';
  }

 private:
  static int local_product_phase(int a, int b) {
    // Canonical Pauli codes: I=0, X=1, Z=2, Y=3.
    // Return exponent k in i^k for P(a)P(b).
    if (a == 0 || b == 0 || a == b) return 0;
    if ((a == 1 && b == 3) ||
        (a == 3 && b == 2) ||
        (a == 2 && b == 1))
      return 1;
    return 3;
  }

  void clear_row(std::size_t row) {
    for (std::size_t w = 0; w < words_; ++w) {
      x_[row * words_ + w] = 0;
      z_[row * words_ + w] = 0;
    }
    phase_[row] = 0;
  }

  void multiply_rows(std::size_t target, std::size_t source) {
    int phase_exp =
        2 * static_cast<int>(phase_[target]) +
        2 * static_cast<int>(phase_[source]);

    for (std::size_t q = 0; q < n_; ++q) {
      const int a = (get(x_, target, q) ? 1 : 0) |
                    (get(z_, target, q) ? 2 : 0);
      const int b = (get(x_, source, q) ? 1 : 0) |
                    (get(z_, source, q) ? 2 : 0);
      phase_exp += local_product_phase(a, b);
    }

    for (std::size_t q = 0; q < n_; ++q) {
      set(x_, target, q, get(x_, target, q) ^ get(x_, source, q));
      set(z_, target, q, get(z_, target, q) ^ get(z_, source, q));
    }

    phase_exp &= 3;
    if (phase_exp & 1)
      throw std::logic_error("attempted to multiply non-commuting stabilizers");
    phase_[target] = static_cast<std::uint8_t>(phase_exp == 2 ? 1 : 0);
  }

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
