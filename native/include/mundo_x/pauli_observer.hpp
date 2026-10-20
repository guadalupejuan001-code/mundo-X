#pragma once

// Shared form of the user's existing Graphics Pauli observer. The signed Pauli
// convention and GF(2) reduction are unchanged; the canonical core header is
// now used by both platforms instead of separate vendored copies.
#include "mundo_x/compact_core.hpp"
#include <algorithm>

namespace mundo_x_bridge {

inline unsigned parity64(std::uint64_t value) {
#if defined(__GNUG__) || defined(__clang__)
  return static_cast<unsigned>(__builtin_parityll(value));
#else
  unsigned parity = 0;
  while (value) {
    parity ^= 1U;
    value &= value - 1;
  }
  return parity;
#endif
}

// P = i^phase X^x Z^z, in qubit order q0, q1, ... .
// Keeping the exponent modulo four handles Y and multiplication signs exactly.
struct Pauli {
  std::vector<std::uint64_t> x, z;
  unsigned phase = 0;

  static Pauli parse(const std::string& s, std::size_t n) {
    std::size_t start = 0;
    Pauli p;
    p.x.assign(n / 64 + (n % 64 != 0), 0);
    p.z.assign(n / 64 + (n % 64 != 0), 0);
    if (s.size() > n && s.size() - n == 1 &&
        (s[0] == '+' || s[0] == '-')) {
      start = 1;
      if (s[0] == '-') p.phase = 2;
    }
    if (s.size() - start != n) throw std::invalid_argument("Pauli length");
    for (std::size_t q = 0; q < n; ++q) {
      const char c = s[start + q];
      const auto bit = std::uint64_t{1} << (q % 64);
      if (c == 'X' || c == 'Y') p.x[q / 64] |= bit;
      if (c == 'Z' || c == 'Y') p.z[q / 64] |= bit;
      if (c == 'Y') p.phase = (p.phase + 1) & 3;
      if (c != 'I' && c != 'X' && c != 'Y' && c != 'Z')
        throw std::invalid_argument("Pauli character");
    }
    return p;
  }

  bool bit(std::size_t column, std::size_t n) const {
    const auto& a = column < n ? x : z;
    const auto q = column < n ? column : column - n;
    return (a[q / 64] >> (q % 64)) & 1;
  }

  void multiply_right(const Pauli& b) {
    unsigned parity = 0;
    for (std::size_t w = 0; w < x.size(); ++w)
      parity ^= parity64(z[w] & b.x[w]);
    phase = (phase + b.phase + 2 * parity) & 3;
    for (std::size_t w = 0; w < x.size(); ++w) {
      x[w] ^= b.x[w];
      z[w] ^= b.z[w];
    }
  }

  bool identity_bits() const {
    for (std::size_t w = 0; w < x.size(); ++w)
      if (x[w] || z[w]) return false;
    return true;
  }
};

// Read-only snapshot index. The original CompactCore is never measured or
// modified here. One GF(2) elimination is shared by all queries at a revision.
class PauliObserver {
 public:
  explicit PauliObserver(const mundo_x::CompactCore& core) : n_(core.qubits()) {
    rows_.reserve(n_);
    pivots_.reserve(n_);
    for (std::size_t r = 0; r < n_; ++r) {
      auto packed = core.generator_packed(r);
      rows_.push_back({std::move(packed.x), std::move(packed.z), packed.phase});
    }
    std::size_t rank = 0;
    for (std::size_t c = 0; c < 2 * n_ && rank < n_; ++c) {
      std::size_t pivot = rank;
      while (pivot < n_ && !rows_[pivot].bit(c, n_)) ++pivot;
      if (pivot == n_) continue;
      std::swap(rows_[rank], rows_[pivot]);
      for (std::size_t r = rank + 1; r < n_; ++r)
        if (rows_[r].bit(c, n_)) rows_[r].multiply_right(rows_[rank]);
      pivots_.push_back(c);
      ++rank;
    }
    if (rank != n_) throw std::logic_error("Not a full-rank pure stabilizer");
    // The rows are in echelon form. A residual's first set column either has
    // one pivot or is already a proof that this Pauli is outside the span.
    // Index the pivots once, instead of scanning every row for every query.
    pivot_rows_.assign(2 * n_, n_);
    for (std::size_t r = 0; r < n_; ++r) pivot_rows_[pivots_[r]] = r;
  }

  double expectation(Pauli p) const {
    const auto width = n_ / 64 + (n_ % 64 != 0);
    if (p.x.size() != width || p.z.size() != width ||
        (n_ % 64 && ((p.x.back() | p.z.back()) >> (n_ % 64))))
      throw std::invalid_argument("Pauli packed dimensions");
    // XOR with a pivot row cannot introduce a bit before that pivot. The
    // cursor therefore only advances, including across the X/Z boundary.
    for (unsigned axis = 0; axis < 2; ++axis) {
      auto& words = axis == 0 ? p.x : p.z;
      for (std::size_t w = 0; w < words.size(); ++w) {
        while (words[w]) {
          const auto column = axis * n_ + w * 64 + first_bit(words[w]);
          const auto row = pivot_rows_[column];
          if (row == n_) return 0.0;
          p.multiply_right(rows_[row]);
        }
      }
    }
    if (p.phase & 1) throw std::logic_error("Non-Hermitian residual");
    return p.phase == 0 ? 1.0 : -1.0;
  }

  std::size_t payload_bytes() const {
    std::size_t count = (pivots_.size() + pivot_rows_.size()) * sizeof(std::size_t);
    for (const auto& row : rows_)
      count += (row.x.size() + row.z.size()) * sizeof(std::uint64_t) + sizeof(row.phase);
    return count; // Excludes vector objects, spare capacities and allocator.
  }

 private:
  static unsigned first_bit(std::uint64_t value) {
#if defined(__GNUG__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_ctzll(value));
#else
    unsigned bit = 0;
    while (!(value & 1)) { ++bit; value >>= 1; }
    return bit;
#endif
  }
  std::size_t n_;
  std::vector<Pauli> rows_;
  std::vector<std::size_t> pivots_, pivot_rows_;
};

} // namespace mundo_x_bridge
