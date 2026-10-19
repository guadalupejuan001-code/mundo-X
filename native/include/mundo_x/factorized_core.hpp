#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include <vector>

namespace mundo_x {

// Experimental floating-point pure-state simulator. A state is a product of
// disjoint dense factors; each factor uses little-endian bits in qubits order.
// This is not symbolic arithmetic and does not remove exponential worst cases.
// max_logical_amplitude_bytes bounds active logical amplitude payload, NOT
// retained inactive singleton capacity, allocator overhead, O(n) metadata,
// temporary allocations during merges/splits, or dense exports.
// Gates do not automatically search for factorization. Explicit try_split has
// O(2^k) work on one k-qubit block; it accepts only coefficient-wise == after
// reconstruction, with no tolerance, pruning, or approximate mode.
// Exact singleton eigenstates can avoid a merge for CX/CZ. Splits rebalance
// factors with reversible powers of two when coefficient-wise reconstruction
// remains unchanged; no amplitudes or global phase are discarded.
// Singleton coefficients use one flat 32*n-byte buffer; this capacity remains
// reserved when qubits enter larger factors. Factor ids are representative
// qubits (the first qubit in each block), eliminating a per-qubit free list.
class FactorizedCore {
 public:
  using Complex = std::complex<double>;
  struct LogAmplitude {
    bool zero;             // An actual zero coefficient in a stored factor.
    double log_magnitude;  // Natural logarithm; -infinity when zero is true.
    double phase;          // Radians, reduced to [-pi, pi].
  };
  struct StorageEstimate {
    std::size_t singleton_capacity_bytes;
    std::size_t active_singleton_amplitude_bytes;
    std::size_t retained_singleton_capacity_bytes;
    std::size_t multiqubit_amplitude_capacity_bytes;
    std::size_t estimated_metadata_bytes;
    std::size_t estimated_reserved_bytes;
  };

  explicit FactorizedCore(
      std::size_t qubits, std::size_t max_block_qubits = 20,
      std::size_t max_logical_amplitude_bytes = std::size_t{256} * 1024 * 1024)
      : n_(qubits), max_block_qubits_(max_block_qubits),
        max_amplitudes_(max_logical_amplitude_bytes / sizeof(Complex)) {
    if (n_ == 0) throw std::invalid_argument("invalid qubit count");
    if (max_block_qubits_ == 0 ||
        max_block_qubits_ >= std::numeric_limits<std::size_t>::digits)
      throw std::invalid_argument("invalid maximum factor size");
    if (n_ > max_amplitudes_ / 2)
      throw std::length_error("initial amplitude payload exceeds budget");
    if (n_ > blocks_.max_size() || n_ > location_.max_size() ||
        n_ > singletons_.max_size())
      throw std::length_error("factor metadata exceeds addressable capacity");
    blocks_.resize(n_);
    location_.resize(n_);
    singletons_.resize(n_);
    width_counts_.assign(max_block_qubits_ + 1, 0);
    for (std::size_t q = 0; q < n_; ++q) {
      singletons_[q] = {Complex{1.0, 0.0}, Complex{0.0, 0.0}};
      location_[q] = Location{q, 0};
    }
    factor_count_ = n_;
    amplitude_count_ = n_ * 2;
    width_counts_[1] = n_;
    largest_width_ = 1;
  }

  FactorizedCore(const FactorizedCore&) = delete;
  FactorizedCore& operator=(const FactorizedCore&) = delete;
  FactorizedCore(FactorizedCore&&) = default;
  FactorizedCore& operator=(FactorizedCore&&) = default;

  std::size_t qubits() const { return n_; }
  std::size_t factor_count() const { return factor_count_; }
  bool same_factor(std::size_t a, std::size_t b) const {
    check(a); check(b);
    return location_[a].block == location_[b].block;
  }
  std::vector<std::size_t> factor_qubits(std::size_t q) const {
    check(q);
    const std::size_t id = location_[q].block;
    return blocks_[id] ? blocks_[id]->qubits : std::vector<std::size_t>{id};
  }
  std::size_t factor_qubit_count(std::size_t q) const {
    check(q);
    return factor_width(location_[q].block);
  }
  std::size_t amplitude_count() const { return amplitude_count_; }
  std::size_t logical_amplitude_bytes() const {
    return amplitude_count_ * sizeof(Complex);
  }
  std::size_t largest_factor_qubits() const { return largest_width_; }
  // O(1) capacities of owned buffers/objects. Excludes allocator overhead,
  // temporary merge/split buffers, query exports, stacks, and shared libraries;
  // this is a storage estimate, never a measurement of process RSS.
  StorageEstimate storage_estimate() const {
    StorageEstimate result{};
    result.singleton_capacity_bytes = singletons_.capacity() * sizeof(Singleton);
    result.active_singleton_amplitude_bytes = width_counts_[1] * sizeof(Singleton);
    result.retained_singleton_capacity_bytes = result.singleton_capacity_bytes -
                                              result.active_singleton_amplitude_bytes;
    result.multiqubit_amplitude_capacity_bytes = dense_amplitude_capacity_bytes_;
    result.estimated_metadata_bytes = checked_sum(
        {sizeof(*this), blocks_.capacity() * sizeof(std::unique_ptr<Block>),
         location_.capacity() * sizeof(Location), width_counts_.capacity() * sizeof(std::size_t),
         dense_qubit_capacity_bytes_, dense_object_count_ * sizeof(Block)});
    result.estimated_reserved_bytes = checked_sum(
        {result.singleton_capacity_bytes, result.multiqubit_amplitude_capacity_bytes,
         result.estimated_metadata_bytes});
    return result;
  }
  std::vector<std::vector<std::size_t>> block_partition() const {
    std::vector<std::vector<std::size_t>> partition;
    partition.reserve(factor_count_);
    for (std::size_t id = 0; id < n_; ++id) {
      if (!active(id)) continue;
      partition.push_back(blocks_[id] ? blocks_[id]->qubits : std::vector<std::size_t>{id});
      std::sort(partition.back().begin(), partition.back().end());
    }
    std::sort(partition.begin(), partition.end());
    return partition;
  }

  void hadamard(std::size_t q) {
    check(q);
    if (!blocks_[location_[q].block]) {
      auto& state = singletons_[q];
      const Complex a = state[0], b = state[1];
      const double scale = 1.0 / std::sqrt(2.0);
      state[0] = (a + b) * scale;
      state[1] = (a - b) * scale;
      return;
    }
    auto& block = *blocks_[location_[q].block];
    const std::size_t bit = std::size_t{1} << location_[q].bit;
    const double scale = 1.0 / std::sqrt(2.0);
    for (std::size_t base = 0; base < block.state.size(); base += bit * 2) {
      for (std::size_t j = 0; j < bit; ++j) {
        const Complex a = block.state[base + j];
        const Complex b = block.state[base + j + bit];
        block.state[base + j] = (a + b) * scale;
        block.state[base + j + bit] = (a - b) * scale;
      }
    }
  }
  void pauli_x(std::size_t q) {
    check(q);
    if (!blocks_[location_[q].block]) {
      std::swap(singletons_[q][0], singletons_[q][1]);
      return;
    }
    auto& block = *blocks_[location_[q].block];
    const std::size_t bit = std::size_t{1} << location_[q].bit;
    for (std::size_t base = 0; base < block.state.size(); base += bit * 2)
      for (std::size_t j = 0; j < bit; ++j)
        std::swap(block.state[base + j], block.state[base + j + bit]);
  }
  void pauli_z(std::size_t q) { phase(q, Complex{-1.0, 0.0}); }
  void phase_s(std::size_t q) { phase(q, Complex{0.0, 1.0}); }
  void phase_t(std::size_t q) {
    const double scale = 1.0 / std::sqrt(2.0);
    phase(q, Complex{scale, scale});
  }
  void cnot(std::size_t control, std::size_t target) {
    two_qubit(control, target, false);
  }
  void controlled_z(std::size_t a, std::size_t b) {
    two_qubit(a, b, true);
  }

  // A product can underflow even when every stored factor is nonzero. Use
  // log_amplitude for large products; ordinary complex<double> cannot encode
  // arbitrarily small coefficients.
  Complex amplitude(const std::vector<bool>& bits) const {
    if (bits.size() != n_) throw std::invalid_argument("basis width mismatch");
    Complex answer{1.0, 0.0};
    for (std::size_t id = 0; id < n_; ++id)
      if (active(id)) answer *= coefficient(id, bits);
    return answer;
  }

  LogAmplitude log_amplitude(const std::vector<bool>& bits) const {
    if (bits.size() != n_) throw std::invalid_argument("basis width mismatch");
    double log_sum = 0.0, log_correction = 0.0;
    double phase_sum = 0.0, phase_correction = 0.0;
    for (std::size_t id = 0; id < n_; ++id) {
      if (!active(id)) continue;
      const Complex value = coefficient(id, bits);
      if (value == Complex{0.0, 0.0})
        return LogAmplitude{true, -std::numeric_limits<double>::infinity(), 0.0};
      const double magnitude = std::abs(value);
      if (!(magnitude > 0.0) || !std::isfinite(magnitude))
        throw std::runtime_error("non-finite factor coefficient");
      compensated_add(std::log(magnitude), log_sum, log_correction);
      compensated_add(std::arg(value), phase_sum, phase_correction);
    }
    return LogAmplitude{false, log_sum,
                        std::remainder(phase_sum, 2.0 * std::acos(-1.0))};
  }

  // Probabilities and expectations are normalized physical observables. Only
  // the queried block is visited; global floating-point norm drift cancels.
  double probability_one(std::size_t q) const {
    check(q);
    const std::size_t id = location_[q].block;
    const Complex* state = factor_state(id);
    const std::size_t dimension = factor_dimension(id);
    const std::size_t bit = std::size_t{1} << location_[q].bit;
    // Factor scales are a gauge: one may be very small while another is
    // correspondingly large. Scale the query before squaring so a nonzero
    // finite factor cannot look like a zero/infinite norm through underflow
    // or overflow. This does not modify the represented state.
    double scale = 0.0;
    for (std::size_t i = 0; i < dimension; ++i) {
      if (!std::isfinite(state[i].real()) || !std::isfinite(state[i].imag()))
        throw std::runtime_error("non-finite or zero factor norm");
      scale = std::max(scale, std::abs(state[i].real()));
      scale = std::max(scale, std::abs(state[i].imag()));
    }
    if (!(scale > 0.0))
      throw std::runtime_error("non-finite or zero factor norm");
    double numerator = 0.0, denominator = 0.0;
    for (std::size_t i = 0; i < dimension; ++i) {
      const double real = state[i].real() / scale;
      const double imag = state[i].imag() / scale;
      const double mass = real * real + imag * imag;
      denominator += mass;
      if (i & bit) numerator += mass;
    }
    if (!(denominator > 0.0) || !std::isfinite(denominator))
      throw std::runtime_error("non-finite or zero factor norm");
    return numerator / denominator;
  }
  double expectation_z(std::size_t q) const {
    return 1.0 - 2.0 * probability_one(q);
  }

  std::vector<Complex> dense_state(std::size_t max_qubits = 20) const {
    if (n_ > max_qubits || n_ >= std::numeric_limits<std::size_t>::digits)
      throw std::length_error("dense export exceeds qubit budget");
    const std::size_t dim = std::size_t{1} << n_;
    std::vector<Complex> state;
    if (dim > state.max_size())
      throw std::length_error("dense export exceeds addressable capacity");
    state.assign(dim, Complex{1.0, 0.0});
    for (std::size_t id = 0; id < n_; ++id) {
      if (!active(id)) continue;
      if (!blocks_[id]) {
        for (std::size_t index = 0; index < dim; ++index)
          state[index] *= singletons_[id][(index >> id) & 1];
        continue;
      }
      const auto& block = blocks_[id];
      for (std::size_t index = 0; index < dim; ++index) {
        std::size_t local = 0;
        for (std::size_t j = 0; j < block->qubits.size(); ++j)
          if ((index >> block->qubits[j]) & 1)
            local |= std::size_t{1} << j;
        state[index] *= block->state[local];
      }
    }
    return state;
  }

  // Returns true if q was already independent or was strictly separated.
  bool try_factorize(std::size_t q) {
    check(q);
    if (!blocks_[location_[q].block]) return true;
    return try_split(std::vector<std::size_t>{q});
  }

  // Explicit bipartition within one current block. No partition search is
  // performed. A full block is already separate and returns true unchanged.
  // Ratios need not reconstruct exactly despite mathematical separability;
  // those false negatives are intentional. Global phase stays in left factor.
  bool try_split(const std::vector<std::size_t>& left_qubits) {
    if (left_qubits.empty()) throw std::invalid_argument("empty split");
    check(left_qubits.front());
    const std::size_t block_id = location_[left_qubits.front()].block;
    const std::size_t width = factor_width(block_id);
    std::vector<bool> is_left(width, false);
    for (const auto q : left_qubits) {
      check(q);
      if (location_[q].block != block_id)
        throw std::invalid_argument("split spans multiple factors");
      const std::size_t bit = location_[q].bit;
      if (is_left[bit]) throw std::invalid_argument("duplicate split qubit");
      is_left[bit] = true;
    }
    if (left_qubits.size() == width) return true;
    const auto& block = *blocks_[block_id];

    auto left = std::make_unique<Block>();
    auto right = std::make_unique<Block>();
    std::vector<std::size_t> left_positions, right_positions;
    for (std::size_t bit = 0; bit < block.qubits.size(); ++bit) {
      if (is_left[bit]) {
        left->qubits.push_back(block.qubits[bit]);
        left_positions.push_back(bit);
      } else {
        right->qubits.push_back(block.qubits[bit]);
        right_positions.push_back(bit);
      }
    }
    const auto left_indices = embedded_indices(left_positions);
    const auto right_indices = embedded_indices(right_positions);
    std::size_t pivot = 0;
    double largest = 0.0;
    for (std::size_t i = 0; i < block.state.size(); ++i) {
      const double magnitude = std::abs(block.state[i]);
      if (magnitude > largest) { largest = magnitude; pivot = i; }
    }
    if (!(largest > 0.0) || !std::isfinite(largest)) return false;
    const Complex pivot_value = block.state[pivot];
    std::size_t pivot_left = 0, pivot_right = 0;
    for (const auto bit : left_positions) pivot_left |= pivot & (std::size_t{1} << bit);
    for (const auto bit : right_positions) pivot_right |= pivot & (std::size_t{1} << bit);
    left->state.resize(left_indices.size());
    right->state.resize(right_indices.size());
    for (std::size_t a = 0; a < left_indices.size(); ++a)
      left->state[a] = block.state[left_indices[a] | pivot_right];
    for (std::size_t b = 0; b < right_indices.size(); ++b) {
      const Complex value = block.state[pivot_left | right_indices[b]];
      right->state[b] = (value == pivot_value) ? Complex{1.0, 0.0} : value / pivot_value;
    }
    for (std::size_t b = 0; b < right_indices.size(); ++b)
      for (std::size_t a = 0; a < left_indices.size(); ++a)
        if (left->state[a] * right->state[b] !=
            block.state[left_indices[a] | right_indices[b]]) return false;

    rebalance_split(*left, *right);

    // Every allocation and strict check is complete before modifying state.
    const std::size_t old_width = block.qubits.size();
    const std::size_t left_width = left->qubits.size();
    const std::size_t right_width = right->qubits.size();
    const std::size_t new_count = amplitude_count_ - block.state.size() +
                                 left->state.size() + right->state.size();
    const std::size_t left_id = left->qubits.front();
    const std::size_t right_id = right->qubits.front();
    const std::size_t new_dense_amplitude_bytes = dense_amplitude_capacity_bytes_ -
        block.state.capacity() * sizeof(Complex) + retained_amplitude_bytes(*left) + retained_amplitude_bytes(*right);
    const std::size_t new_dense_qubit_bytes = dense_qubit_capacity_bytes_ -
        block.qubits.capacity() * sizeof(std::size_t) + retained_qubit_bytes(*left) + retained_qubit_bytes(*right);
    const std::size_t new_dense_objects = dense_object_count_ - 1 +
        std::size_t(left_width > 1) + std::size_t(right_width > 1);
    // The old representative is first in one of the cuts; the other id was
    // inactive. Installing either side cannot invalidate the prepared other.
    install_factor(left_id, std::move(left));
    install_factor(right_id, std::move(right));
    dense_amplitude_capacity_bytes_ = new_dense_amplitude_bytes;
    dense_qubit_capacity_bytes_ = new_dense_qubit_bytes;
    dense_object_count_ = new_dense_objects;
    amplitude_count_ = new_count;
    ++factor_count_;
    --width_counts_[old_width];
    ++width_counts_[left_width];
    ++width_counts_[right_width];
    while (width_counts_[largest_width_] == 0) --largest_width_;
    return true;
  }

 private:
  struct Block {
    std::vector<std::size_t> qubits;
    std::vector<Complex> state;
  };
  struct Location { std::size_t block; std::size_t bit; };
  using Singleton = std::array<Complex, 2>;

  static std::size_t checked_sum(std::initializer_list<std::size_t> values) {
    std::size_t result = 0;
    for (const auto value : values) {
      if (value > std::numeric_limits<std::size_t>::max() - result)
        throw std::overflow_error("storage estimate exceeds addressable capacity");
      result += value;
    }
    return result;
  }
  bool active(std::size_t id) const { return location_[id].block == id; }
  std::size_t factor_width(std::size_t id) const {
    return blocks_[id] ? blocks_[id]->qubits.size() : 1;
  }
  std::size_t factor_dimension(std::size_t id) const {
    return blocks_[id] ? blocks_[id]->state.size() : 2;
  }
  const Complex* factor_state(std::size_t id) const {
    return blocks_[id] ? blocks_[id]->state.data() : singletons_[id].data();
  }
  const Complex& coefficient(std::size_t id, const std::vector<bool>& bits) const {
    if (!blocks_[id]) return singletons_[id][bits[id] ? 1 : 0];
    const auto& block = *blocks_[id];
    std::size_t local = 0;
    for (std::size_t bit = 0; bit < block.qubits.size(); ++bit)
      if (bits[block.qubits[bit]]) local |= std::size_t{1} << bit;
    return block.state[local];
  }
  static std::size_t retained_amplitude_bytes(const Block& block) {
    return block.qubits.size() == 1 ? 0 : block.state.capacity() * sizeof(Complex);
  }
  static Complex scale_binary(Complex value, int exponent) {
    return {std::scalbn(value.real(), exponent),
            std::scalbn(value.imag(), exponent)};
  }
  static bool reversible_scale(Complex value, int exponent) {
    const Complex scaled = scale_binary(value, exponent);
    return std::isfinite(scaled.real()) && std::isfinite(scaled.imag()) &&
           scale_binary(scaled, -exponent) == value;
  }
  static int binary_norm_exponent(const std::vector<Complex>& state) {
    double peak = 0.0;
    for (const auto value : state) {
      peak = std::max(peak, std::abs(value.real()));
      peak = std::max(peak, std::abs(value.imag()));
    }
    const int exponent = std::ilogb(peak);
    double squared_norm = 0.0;
    for (const auto value : state)
      squared_norm += std::norm(scale_binary(value, -exponent));
    return exponent + std::ilogb(std::sqrt(squared_norm));
  }
  static void rebalance_split(Block& left, Block& right) {
    // Balance both norms so repeatedly retaining either side of a cut cannot
    // accumulate all of the small scale in that side. The exponent estimate
    // only chooses a candidate gauge; the exact checks below decide acceptance.
    const int exponent = (binary_norm_exponent(right.state) -
                          binary_norm_exponent(left.state)) / 2;
    if (exponent == 0) return;
    // Powers of two avoid introducing a rounded sqrt/norm into the factors.
    // Reject even this rescaling if it would drop a subnormal component,
    // overflow, or change any reconstructed coefficient. Keep the original
    // strictly verified split in that case.
    for (const auto value : left.state)
      if (!reversible_scale(value, exponent)) return;
    for (const auto value : right.state)
      if (!reversible_scale(value, -exponent)) return;
    for (const auto b : right.state)
      for (const auto a : left.state)
        if (scale_binary(a, exponent) * scale_binary(b, -exponent) != a * b)
          return;
    for (auto& value : left.state) value = scale_binary(value, exponent);
    for (auto& value : right.state) value = scale_binary(value, -exponent);
  }
  static std::size_t retained_qubit_bytes(const Block& block) {
    return block.qubits.size() == 1 ? 0 : block.qubits.capacity() * sizeof(std::size_t);
  }
  void install_factor(std::size_t id, std::unique_ptr<Block> factor) noexcept {
    if (factor->qubits.size() == 1) {
      singletons_[id][0] = factor->state[0];
      singletons_[id][1] = factor->state[1];
      blocks_[id].reset();
      location_[id] = Location{id, 0};
    } else {
      blocks_[id] = std::move(factor);
      update_locations(id);
    }
  }

  static void compensated_add(double value, double& sum, double& correction) {
    const double adjusted = value - correction;
    const double next = sum + adjusted;
    correction = (next - sum) - adjusted;
    sum = next;
  }

  void check(std::size_t q) const {
    if (q >= n_) throw std::out_of_range("qubit");
  }
  void phase(std::size_t q, Complex value) {
    check(q);
    if (!blocks_[location_[q].block]) {
      singletons_[q][1] *= value;
      return;
    }
    auto& block = *blocks_[location_[q].block];
    const std::size_t bit = std::size_t{1} << location_[q].bit;
    for (std::size_t i = 0; i < block.state.size(); ++i)
      if (i & bit) block.state[i] *= value;
  }
  void update_locations(std::size_t id) {
    const auto& qubits = blocks_[id]->qubits;
    for (std::size_t bit = 0; bit < qubits.size(); ++bit)
      location_[qubits[bit]] = Location{id, bit};
  }
  static std::vector<std::size_t> embedded_indices(
      const std::vector<std::size_t>& positions) {
    std::vector<std::size_t> indices(std::size_t{1} << positions.size(), 0);
    for (std::size_t i = 0; i < indices.size(); ++i)
      for (std::size_t j = 0; j < positions.size(); ++j)
        if ((i >> j) & 1) indices[i] |= std::size_t{1} << positions[j];
    return indices;
  }
  static void apply_two(Block& block, std::size_t a, std::size_t b, bool cz) {
    const std::size_t abit = std::size_t{1} << a;
    const std::size_t bbit = std::size_t{1} << b;
    for (std::size_t i = 0; i < block.state.size(); ++i) {
      if (!(i & abit)) continue;
      if (cz) {
        if (i & bbit) block.state[i] = -block.state[i];
      } else if (!(i & bbit)) {
        std::swap(block.state[i], block.state[i | bbit]);
      }
    }
  }
  void two_qubit(std::size_t a, std::size_t b, bool cz) {
    check(a); check(b);
    if (a == b) throw std::invalid_argument("two-qubit gate requires distinct qubits");
    const Location a_location = location_[a];
    const Location b_location = location_[b];
    // A computational-basis singleton control cannot become entangled.
    // Exact zero checks preserve any coefficient/global phase of that factor.
    if (!blocks_[a_location.block]) {
      const auto& control = singletons_[a];
      if (control[1] == Complex{0.0, 0.0}) return;
      if (control[0] == Complex{0.0, 0.0}) {
        if (cz) pauli_z(b); else pauli_x(b);
        return;
      }
    }
    // CZ is symmetric: a basis-state singleton on either side is enough.
    // CX on an X eigenstate target only kicks its +/- sign back to control.
    // Equality is exact, including complex/global scale. Near eigenstates
    // take the ordinary entangling path; no numerical threshold is used.
    if (!blocks_[b_location.block]) {
      const auto& target = singletons_[b];
      if (cz) {
        if (target[1] == Complex{0.0, 0.0}) return;
        if (target[0] == Complex{0.0, 0.0}) {
          pauli_z(a);
          return;
        }
      } else {
        if (target[0] == target[1]) return;
        if (target[0] == -target[1]) {
          pauli_z(a);
          return;
        }
      }
    }
    if (a_location.block == b_location.block) {
      apply_two(*blocks_[a_location.block], a_location.bit, b_location.bit, cz);
      return;
    }
    const std::size_t left_width = factor_width(a_location.block);
    const std::size_t right_width = factor_width(b_location.block);
    if (left_width > max_block_qubits_ - right_width)
      throw std::length_error("merged factor exceeds qubit budget");
    const std::size_t joined_qubits = left_width + right_width;
    const std::size_t dimension = std::size_t{1} << joined_qubits;
    const std::size_t left_dim = factor_dimension(a_location.block);
    const std::size_t right_dim = factor_dimension(b_location.block);
    const Complex* left_state = factor_state(a_location.block);
    const Complex* right_state = factor_state(b_location.block);
    const std::size_t unchanged = amplitude_count_ - left_dim - right_dim;
    if (dimension > max_amplitudes_ - unchanged)
      throw std::length_error("merged factor exceeds amplitude payload budget");
    auto joined = std::make_unique<Block>();
    if (dimension > joined->state.max_size())
      throw std::length_error("merged factor exceeds addressable capacity");
    joined->qubits.reserve(joined_qubits);
    for (const auto id : {a_location.block, b_location.block}) {
      if (blocks_[id])
        joined->qubits.insert(joined->qubits.end(), blocks_[id]->qubits.begin(), blocks_[id]->qubits.end());
      else joined->qubits.push_back(id);
    }
    joined->state.resize(dimension);
    for (std::size_t j = 0; j < right_dim; ++j)
      for (std::size_t i = 0; i < left_dim; ++i)
        joined->state[i | (j << left_width)] = left_state[i] * right_state[j];
    apply_two(*joined, a_location.bit, left_width + b_location.bit, cz);
    std::size_t old_amplitude_bytes = 0, old_qubit_bytes = 0, old_objects = 0;
    for (const auto id : {a_location.block, b_location.block}) {
      if (!blocks_[id]) continue;
      old_amplitude_bytes += blocks_[id]->state.capacity() * sizeof(Complex);
      old_qubit_bytes += blocks_[id]->qubits.capacity() * sizeof(std::size_t);
      ++old_objects;
    }
    const std::size_t new_amplitude_bytes = dense_amplitude_capacity_bytes_ -
        old_amplitude_bytes + joined->state.capacity() * sizeof(Complex);
    const std::size_t new_qubit_bytes = dense_qubit_capacity_bytes_ -
        old_qubit_bytes + joined->qubits.capacity() * sizeof(std::size_t);
    // No throwing operations remain: a rejected merge cannot change the state.
    blocks_[a_location.block] = std::move(joined);
    blocks_[b_location.block].reset();
    update_locations(a_location.block);
    dense_amplitude_capacity_bytes_ = new_amplitude_bytes;
    dense_qubit_capacity_bytes_ = new_qubit_bytes;
    dense_object_count_ = dense_object_count_ - old_objects + 1;
    amplitude_count_ = unchanged + dimension;
    --factor_count_;
    --width_counts_[left_width];
    --width_counts_[right_width];
    ++width_counts_[joined_qubits];
    largest_width_ = std::max(largest_width_, joined_qubits);
  }

  std::size_t n_;
  std::size_t max_block_qubits_;
  std::size_t max_amplitudes_;
  std::size_t factor_count_ = 0;
  std::size_t amplitude_count_ = 0;
  std::size_t largest_width_ = 0;
  std::size_t dense_amplitude_capacity_bytes_ = 0;
  std::size_t dense_qubit_capacity_bytes_ = 0;
  std::size_t dense_object_count_ = 0;
  std::vector<Singleton> singletons_;
  std::vector<std::unique_ptr<Block>> blocks_;
  std::vector<Location> location_;
  std::vector<std::size_t> width_counts_;
};

}  // namespace mundo_x
