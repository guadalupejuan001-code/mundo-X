#pragma once

#include <cstddef>
#include <cstdint>
#include <array>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// Compact Clifford-state engine using a bit-packed stabilizer tableau.
// It supports H, S, X, Z and CNOT.
// Memory is O(n^2) bits instead of O(2^n) complex amplitudes.
class CompactCore {
 public:
  struct ZMeasurement {
    bool outcome_one;
    double probability;  // Probability of the returned outcome before projection.
    bool random;
  };

  // Independent read-only export, P = i^phase X^x Z^z. Owning the copied
  // words keeps observers valid after subsequent mutations of the core.
  struct PackedGenerator {
    std::vector<std::uint64_t> x, z;
    unsigned phase;
  };

  explicit CompactCore(std::size_t qubits)
      : n_(checked_qubit_count(qubits)), words_(n_ / 64 + (n_ % 64 != 0)),
        x_(n_ * words_, 0), z_(n_ * words_, 0), phase_(n_, 0) {
    // |0...0> is stabilized by Z_i.
    for (std::size_t i = 0; i < n_; ++i) set(z_, i, i, true);
  }

  std::size_t qubits() const { return n_; }
  std::size_t state_bytes() const {
    return (x_.size() + z_.size()) * sizeof(std::uint64_t) + phase_.size();
  }

  // Concatenate two independent *current* stabilizer states. Left qubits keep
  // indices [0,left.n), right qubits follow them. This copies the signed packed
  // generators, including a right-hand word shifted across a 64-bit boundary;
  // it does not replay gates, measure either input, or assume a basis state.
  // As with every stabilizer tableau, an unobservable global state phase is
  // not represented. All generator signs and physical correlations are kept.
  static CompactCore tensor_product(const CompactCore& left,
                                    const CompactCore& right) {
    if (right.n_ > std::numeric_limits<std::size_t>::max() - left.n_)
      throw std::length_error("tensor product qubit count overflows");
    CompactCore result(left.n_ + right.n_); // Validates storage before allocation.
    for (auto& word : result.z_) word = 0; // Remove the constructor's +Z_i rows.
    auto append = [&result](const CompactCore& source, std::size_t qubit_offset) {
      const std::size_t word_offset = qubit_offset / 64;
      const unsigned shift = static_cast<unsigned>(qubit_offset % 64);
      for (std::size_t row = 0; row < source.n_; ++row) {
        const std::size_t output_row = (qubit_offset + row) * result.words_;
        const std::size_t input_row = row * source.words_;
        for (std::size_t word = 0; word < source.words_; ++word) {
          const std::size_t destination = word_offset + word;
          const auto x = source.x_[input_row + word];
          const auto z = source.z_[input_row + word];
          result.x_[output_row + destination] |= x << shift;
          result.z_[output_row + destination] |= z << shift;
          if (shift != 0 && destination + 1 < result.words_) {
            result.x_[output_row + destination + 1] |= x >> (64 - shift);
            result.z_[output_row + destination + 1] |= z >> (64 - shift);
          }
        }
        result.phase_[qubit_offset + row] = source.phase_[row];
      }
    };
    append(left, 0);
    append(right, left.n_);
    result.basis_canonical_ = left.basis_canonical_ && right.basis_canonical_;
    return result;
  }

  void hadamard(std::size_t q) {
    check(q);
    basis_canonical_ = false;
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
    basis_canonical_ = false;
    // CZ conjugation in one tableau pass. The sign is required when the
    // two X components have different Z components (for example XY).
    for (std::size_t r = 0; r < n_; ++r) {
      const bool xa = get(x_, r, a), za = get(z_, r, a);
      const bool xb = get(x_, r, b), zb = get(z_, r, b);
      phase_[r] ^= static_cast<std::uint8_t>(xa && xb && (za ^ zb));
      set(z_, r, a, za ^ xb);
      set(z_, r, b, zb ^ xa);
    }
  }

  void cnot(std::size_t control, std::size_t target) {
    check(control);
    check(target);
    if (control == target) throw std::invalid_argument("control == target");
    basis_canonical_ = false;

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
    if (rows == 0 || cols == 0 || n_ % cols != 0 || rows != n_ / cols)
      return false;

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

  PackedGenerator generator_packed(std::size_t row) const {
    if (row >= n_) throw std::out_of_range("row");
    const auto offset = row * words_;
    unsigned exponent = 2U * phase_[row];
    for (std::size_t w = 0; w < words_; ++w)
      exponent = (exponent + popcount64(x_[offset + w] & z_[offset + w])) & 3U;
    return {{x_.begin() + offset, x_.begin() + offset + words_},
            {z_.begin() + offset, z_.begin() + offset + words_}, exponent};
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


  // Born probability for a pure stabilizer state: exactly 0, 1/2, or 1.
  // This query never changes the tableau. A general deterministic query uses
  // polynomial GF(2) elimination, not a dense statevector: O(n^3) scalar work
  // and O(n^2) bits of scratch in the worst case. Explicit single-qubit
  // generators avoid that elimination; random queries only scan n generators.
  double probability_one_z(std::size_t q) const {
    check(q);
    for (std::size_t r = 0; r < n_; ++r)
      if (get(x_, r, q)) return 0.5;
    return deterministic_z_outcome_one(q) ? 1.0 : 0.0;
  }

  // Sample and project using the existing packed tableau. Deterministic
  // outcomes neither consume RNG output nor modify the represented state.
  // The low bit of mt19937_64 gives a reproducible unbiased random branch.
  ZMeasurement measure_z(std::size_t q, std::mt19937_64& rng) {
    const double probability_one = probability_one_z(q);
    if (probability_one != 0.5)
      return ZMeasurement{probability_one == 1.0, 1.0, false};
    const bool outcome_one = (rng() & 1U) != 0;
    measure_z_forced(q, outcome_one);
    return ZMeasurement{outcome_one, 0.5, true};
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
    basis_canonical_ = false;

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

  // Restricted helpers for a deterministic computational-basis machine.
  // Canonical means row q is exactly +/-Z_q. The flag tracks *all* mutations;
  // raw CNOT/H/measurement invalidate it instead of assuming a row ordering.
  bool computational_basis_bit(std::size_t q) const {
    check(q);
    if (!basis_canonical_)
      throw std::logic_error("computational basis is not canonical");
    return phase_[q] != 0;
  }

  void canonicalize_computational_basis() {
    for (const auto word : x_)
      if (word) throw std::logic_error("state is not a computational basis state");
    basis_canonical_ = false;
    for (std::size_t q = 0; q < n_; ++q) {
      std::size_t pivot = q;
      while (pivot < n_ && !get(z_, pivot, q)) ++pivot;
      if (pivot == n_) throw std::logic_error("basis stabilizers lost rank");
      if (pivot != q) {
        for (std::size_t w = 0; w < words_; ++w) {
          std::swap(x_[q * words_ + w], x_[pivot * words_ + w]);
          std::swap(z_[q * words_ + w], z_[pivot * words_ + w]);
        }
        std::swap(phase_[q], phase_[pivot]);
      }
      for (std::size_t r = 0; r < n_; ++r)
        if (r != q && get(z_, r, q)) multiply_rows(r, q);
    }
    basis_canonical_ = true;
  }

  void computational_basis_cnot(std::size_t control, std::size_t target) {
    if (!basis_canonical_)
      throw std::logic_error("computational basis is not canonical");
    cnot(control, target);
    // CNOT maps the target generator Z_t to Z_c Z_t. Multiplication by
    // the control generator restores +/-Z_t without changing the state.
    multiply_rows(target, control);
    basis_canonical_ = true;
  }

 private:
  // Called only when every generator commutes with Z_q. For a pure full-rank
  // stabilizer tableau, either +Z_q or -Z_q belongs to its stabilizer group.
  bool deterministic_z_outcome_one(std::size_t q) const {
    const std::size_t q_word = q / 64;
    const std::uint64_t q_mask = std::uint64_t{1} << (q % 64);
    for (std::size_t r = 0; r < n_; ++r) {
      if (!get(z_, r, q)) continue;
      bool single_z = true;
      for (std::size_t w = 0; w < words_; ++w) {
        if (x_[r * words_ + w] != 0 ||
            z_[r * words_ + w] != (w == q_word ? q_mask : 0)) {
          single_z = false;
          break;
        }
      }
      if (single_z) return phase_[r] != 0;
    }

    CompactCore basis(*this);
    std::vector<std::uint64_t> target_x(words_, 0), target_z(words_, 0);
    target_z[q_word] = q_mask;
    bool target_negative = false;
    std::size_t rank = 0;
    for (std::size_t column = 0; column < 2 * n_ && rank < n_; ++column) {
      const bool x_column = column < n_;
      const std::size_t bit = x_column ? column : column - n_;
      const auto& labels = x_column ? basis.x_ : basis.z_;
      std::size_t pivot = rank;
      while (pivot < n_ && !basis.get(labels, pivot, bit)) ++pivot;
      if (pivot == n_) continue;
      if (pivot != rank) {
        for (std::size_t w = 0; w < words_; ++w) {
          std::swap(basis.x_[rank * words_ + w], basis.x_[pivot * words_ + w]);
          std::swap(basis.z_[rank * words_ + w], basis.z_[pivot * words_ + w]);
        }
        std::swap(basis.phase_[rank], basis.phase_[pivot]);
      }
      for (std::size_t r = rank + 1; r < n_; ++r)
        if (basis.get(labels, r, bit)) basis.multiply_rows(r, rank);

      const auto& target_labels = x_column ? target_x : target_z;
      if ((target_labels[bit / 64] >> (bit % 64)) & 1U) {
        const auto offset = rank * words_;
        const unsigned phase_exp =
            (2U * static_cast<unsigned>(target_negative) +
             2U * basis.phase_[rank] +
             packed_product_phase(target_x.data(), target_z.data(),
                                  basis.x_.data() + offset,
                                  basis.z_.data() + offset, words_)) & 3U;
        if (phase_exp & 1U)
          throw std::logic_error("deterministic Z reduction lost commutation");
        for (std::size_t w = 0; w < words_; ++w) {
          target_x[w] ^= basis.x_[rank * words_ + w];
          target_z[w] ^= basis.z_[rank * words_ + w];
        }
        target_negative = phase_exp == 2;
        bool identity = true;
        for (std::size_t w = 0; w < words_; ++w)
          if (target_x[w] != 0 || target_z[w] != 0) { identity = false; break; }
        if (identity) return target_negative;
      }
      ++rank;
    }
    throw std::logic_error("deterministic Z is missing from stabilizer group");
  }

  static std::size_t checked_qubit_count(std::size_t qubits) {
    if (qubits == 0) throw std::invalid_argument("invalid qubit count");
    const std::size_t words = qubits / 64 + (qubits % 64 != 0);
    const std::size_t maximum = std::numeric_limits<std::size_t>::max();
    // Check both the vector length and the total byte count before any
    // multiplication or allocation. This is a representability check, not a
    // memory budget: allocations can still fail normally with std::bad_alloc.
    const std::size_t maximum_entries =
        (maximum - qubits) / (2 * sizeof(std::uint64_t));
    if (qubits > maximum_entries / words ||
        qubits > std::vector<std::uint64_t>().max_size() / words)
      throw std::length_error("tableau dimensions exceed addressable storage");
    return qubits;
  }

  static unsigned popcount64(std::uint64_t value) {
#if defined(__GNUG__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_popcountll(value));
#else
    // Portable fixed-cost fallback; no target-specific instruction required.
    value -= (value >> 1U) & 0x5555555555555555ULL;
    value = (value & 0x3333333333333333ULL) +
            ((value >> 2U) & 0x3333333333333333ULL);
    value = (value + (value >> 4U)) & 0x0f0f0f0f0f0f0f0fULL;
    return static_cast<unsigned>((value * 0x0101010101010101ULL) >> 56U);
#endif
  }

  static unsigned packed_product_phase(const std::uint64_t* ax,
                                       const std::uint64_t* az,
                                       const std::uint64_t* bx,
                                       const std::uint64_t* bz,
                                       std::size_t words) {
    // Canonical Hermitian Pauli = i^popcount(x&z) X^x Z^z. Multiplication
    // adds the two Y counts, the Z_a/X_b reordering sign, then removes
    // the output Y count. Unsigned arithmetic modulo four is exact here.
    unsigned exponent = 0;
    for (std::size_t w = 0; w < words; ++w) {
      exponent += popcount64(ax[w] & az[w]) + popcount64(bx[w] & bz[w]) +
                  2U * popcount64(az[w] & bx[w]);
      exponent -= popcount64((ax[w] ^ bx[w]) & (az[w] ^ bz[w]));
      exponent &= 3U;
    }
    return exponent;
  }

  void clear_row(std::size_t row) {
    for (std::size_t w = 0; w < words_; ++w) {
      x_[row * words_ + w] = 0;
      z_[row * words_ + w] = 0;
    }
    phase_[row] = 0;
  }

  void multiply_rows(std::size_t target, std::size_t source) {
    const auto a = target * words_, b = source * words_;
    const unsigned phase_exp =
        (2U * phase_[target] + 2U * phase_[source] +
         packed_product_phase(x_.data() + a, z_.data() + a,
                              x_.data() + b, z_.data() + b, words_)) & 3U;
    if (phase_exp & 1U)
      throw std::logic_error("attempted to multiply non-commuting stabilizers");
    for (std::size_t w = 0; w < words_; ++w) {
      x_[a + w] ^= x_[b + w];
      z_[a + w] ^= z_[b + w];
    }
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
  bool basis_canonical_ = true;
};

}  // namespace mundo_x
