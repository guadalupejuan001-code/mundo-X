#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <unordered_map>
#include <algorithm>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"

namespace mundo_x {

// Exact experimental decomposition of T gates into a linear combination of
// stabilizer branches:
//
//   T = alpha * I + beta * Z
//
// where
//   alpha = (1 + exp(i*pi/4)) / 2
//   beta  = (1 - exp(i*pi/4)) / 2
//
// Each branch remains a CompactCore Clifford/stabilizer state. No pruning,
// truncation or approximate branch merging is performed.
//
// Important: CompactCore tableaus intentionally ignore global phase. To preserve
// exact relative phases between branches, the experiment stores the original
// circuit plus one packed branch-choice bit per T gate. Dense reconstruction is
// used only as a validation oracle for small circuits; it is not part of the
// compact branch representation itself.

enum class ExactExperimentOp : std::uint8_t {
  H,
  S,
  X,
  Z,
  CX,
  CZ,
  T,
};

struct ExactExperimentGate {
  ExactExperimentOp op;
  std::uint16_t a;
  std::uint16_t b;
};

class ExactTExperiment {
 public:
  struct Branch {
    CompactCore state;
    std::complex<double> coefficient;
    std::uint64_t t_choices;

    explicit Branch(std::size_t qubits)
        : state(qubits), coefficient(1.0, 0.0), t_choices(0) {}
  };

  explicit ExactTExperiment(std::size_t qubits, std::size_t max_branches = 1u << 16)
      : n_(qubits), max_branches_(max_branches) {
    if (qubits == 0) throw std::invalid_argument("invalid qubit count");
    if (qubits > std::numeric_limits<std::uint16_t>::max())
      throw std::invalid_argument("qubit count too large for experiment gate log");
    branches_.emplace_back(qubits);
  }

  std::size_t qubits() const { return n_; }
  std::size_t t_count() const { return t_count_; }
  std::size_t branch_count() const { return branches_.size(); }
  std::size_t circuit_gate_count() const { return circuit_.size(); }

  std::size_t tableau_bytes() const {
    std::size_t total = 0;
    for (const auto& b : branches_) total += b.state.state_bytes();
    return total;
  }

  // Approximate explicit payload represented by the current data structures.
  // It includes Branch objects, CompactCore dynamic tableau payloads and the
  // circuit log. Standard allocator metadata is not included.
  std::size_t approximate_explicit_bytes() const {
    std::size_t total = branches_.size() * sizeof(Branch);
    total += tableau_bytes();
    total += circuit_.size() * sizeof(ExactExperimentGate);
    return total;
  }

  void hadamard(std::size_t q) {
    append_unary(ExactExperimentOp::H, q);
    for (auto& b : branches_) b.state.hadamard(q);
  }

  void phase_s(std::size_t q) {
    append_unary(ExactExperimentOp::S, q);
    for (auto& b : branches_) b.state.phase_s(q);
  }

  void pauli_x(std::size_t q) {
    append_unary(ExactExperimentOp::X, q);
    for (auto& b : branches_) b.state.pauli_x(q);
  }

  void pauli_z(std::size_t q) {
    append_unary(ExactExperimentOp::Z, q);
    for (auto& b : branches_) b.state.pauli_z(q);
  }

  void cnot(std::size_t control, std::size_t target) {
    check(control);
    check(target);
    circuit_.push_back({ExactExperimentOp::CX,
                        static_cast<std::uint16_t>(control),
                        static_cast<std::uint16_t>(target)});
    for (auto& b : branches_) b.state.cnot(control, target);
  }

  void controlled_z(std::size_t a, std::size_t b) {
    check(a);
    check(b);
    circuit_.push_back({ExactExperimentOp::CZ,
                        static_cast<std::uint16_t>(a),
                        static_cast<std::uint16_t>(b)});
    for (auto& branch : branches_) branch.state.controlled_z(a, b);
  }

  void phase_t(std::size_t q) {
    check(q);
    if (t_count_ >= 63)
      throw std::runtime_error("exact experiment supports at most 63 packed T choices");
    if (branches_.size() > max_branches_ / 2)
      throw std::runtime_error("branch safety limit reached");

    const double s = 1.0 / std::sqrt(2.0);
    const std::complex<double> e{s, s};
    const std::complex<double> alpha =
        (std::complex<double>{1.0, 0.0} + e) * 0.5;
    const std::complex<double> beta =
        (std::complex<double>{1.0, 0.0} - e) * 0.5;

    const std::size_t old_count = branches_.size();
    std::vector<Branch> next;
    next.reserve(old_count * 2);

    for (const auto& source : branches_) {
      Branch i_branch = source;
      i_branch.coefficient *= alpha;
      next.push_back(std::move(i_branch));

      Branch z_branch = source;
      z_branch.coefficient *= beta;
      z_branch.state.pauli_z(q);
      z_branch.t_choices |= (std::uint64_t{1} << t_count_);
      next.push_back(std::move(z_branch));
    }

    branches_ = std::move(next);
    circuit_.push_back({ExactExperimentOp::T,
                        static_cast<std::uint16_t>(q), 0});
    ++t_count_;
  }

  std::vector<std::complex<double>> dense_reference() const {
    ensure_dense_safe();
    DenseCore dense(n_);
    for (const auto& gate : circuit_) apply_reference_gate(dense, gate);
    return to_double(dense.state());
  }

  std::vector<std::complex<double>> dense_from_decomposition() const {
    ensure_dense_safe();
    const std::size_t dim = std::size_t{1} << n_;
    std::vector<std::complex<double>> sum(dim, {0.0, 0.0});

    for (const auto& branch : branches_) {
      DenseCore dense(n_);
      replay_branch(dense, branch);
      const auto& state = dense.state();
      for (std::size_t i = 0; i < dim; ++i) {
        sum[i] += branch.coefficient *
                  std::complex<double>{state[i].real(), state[i].imag()};
      }
    }

    return sum;
  }

  double fidelity() const {
    const auto reference = dense_reference();
    const auto decomposed = dense_from_decomposition();

    std::complex<double> overlap{0.0, 0.0};
    double ref_norm = 0.0;
    double dec_norm = 0.0;

    for (std::size_t i = 0; i < reference.size(); ++i) {
      overlap += std::conj(reference[i]) * decomposed[i];
      ref_norm += std::norm(reference[i]);
      dec_norm += std::norm(decomposed[i]);
    }

    if (ref_norm <= 0.0 || dec_norm <= 0.0) return 0.0;
    return std::norm(overlap) / (ref_norm * dec_norm);
  }

  double decomposition_norm() const {
    const auto decomposed = dense_from_decomposition();
    double norm = 0.0;
    for (const auto& a : decomposed) norm += std::norm(a);
    return norm;
  }

  struct RedundancyAnalysis {
    std::size_t total_branches = 0;
    std::size_t unique_tableaus = 0;
    std::size_t max_tableau_multiplicity = 0;
    std::size_t unique_physical_states = 0;
    std::size_t max_physical_multiplicity = 0;
    double grouped_fidelity = 0.0;
    double grouped_norm = 0.0;
  };

  RedundancyAnalysis analyze_redundancy_dense_oracle() const {
    ensure_dense_safe();

    RedundancyAnalysis out;
    out.total_branches = branches_.size();

    std::unordered_map<std::string, std::size_t> tableau_counts;
    for (const auto& branch : branches_) {
      std::ostringstream key;
      for (std::size_t r = 0; r < branch.state.qubits(); ++r) {
        key << branch.state.generator_string(r) << '|';
      }
      const std::size_t count = ++tableau_counts[key.str()];
      out.max_tableau_multiplicity =
          std::max(out.max_tableau_multiplicity, count);
    }
    out.unique_tableaus = tableau_counts.size();

    struct Group {
      std::vector<std::complex<double>> canonical;
      std::complex<double> weight{0.0, 0.0};
      std::size_t multiplicity = 0;
    };

    std::unordered_map<std::string, Group> groups;

    for (const auto& branch : branches_) {
      DenseCore dense(n_);
      replay_branch(dense, branch);
      auto state = to_double(dense.state());

      std::complex<double> phase{1.0, 0.0};
      for (const auto& a : state) {
        const double mag = std::abs(a);
        if (mag > 1e-8) {
          phase = a / mag;
          break;
        }
      }

      std::vector<std::complex<double>> canonical(state.size());
      std::ostringstream key;
      key << std::setprecision(0) << std::fixed;
      constexpr double quant = 1e6;

      for (std::size_t i = 0; i < state.size(); ++i) {
        canonical[i] = state[i] / phase;
        const long long re =
            static_cast<long long>(std::llround(canonical[i].real() * quant));
        const long long im =
            static_cast<long long>(std::llround(canonical[i].imag() * quant));
        key << re << ',' << im << ';';
      }

      auto& group = groups[key.str()];
      if (group.multiplicity == 0) group.canonical = std::move(canonical);
      group.weight += branch.coefficient * phase;
      ++group.multiplicity;
      out.max_physical_multiplicity =
          std::max(out.max_physical_multiplicity, group.multiplicity);
    }

    out.unique_physical_states = groups.size();

    const std::size_t dim = std::size_t{1} << n_;
    std::vector<std::complex<double>> grouped(dim, {0.0, 0.0});
    for (const auto& entry : groups) {
      const auto& group = entry.second;
      for (std::size_t i = 0; i < dim; ++i)
        grouped[i] += group.weight * group.canonical[i];
    }

    const auto reference = dense_reference();
    std::complex<double> overlap{0.0, 0.0};
    double ref_norm = 0.0;
    double grouped_norm = 0.0;

    for (std::size_t i = 0; i < dim; ++i) {
      overlap += std::conj(reference[i]) * grouped[i];
      ref_norm += std::norm(reference[i]);
      grouped_norm += std::norm(grouped[i]);
    }

    out.grouped_norm = grouped_norm;
    if (ref_norm > 0.0 && grouped_norm > 0.0)
      out.grouped_fidelity =
          std::norm(overlap) / (ref_norm * grouped_norm);

    return out;
  }

  bool branch_tableaus_match_dense(double tolerance = 2e-4) const {
    ensure_dense_safe();
    for (const auto& branch : branches_) {
      DenseCore dense(n_);
      replay_branch(dense, branch);

      for (std::size_t r = 0; r < branch.state.qubits(); ++r) {
        const auto generator = branch.state.generator_string(r);
        const auto expectation = pauli_expectation(dense.state(), generator);
        if (std::abs(expectation.real() - 1.0) > tolerance ||
            std::abs(expectation.imag()) > tolerance) {
          return false;
        }
      }
    }
    return true;
  }

 private:
  void check(std::size_t q) const {
    if (q >= n_) throw std::out_of_range("qubit");
  }

  void append_unary(ExactExperimentOp op, std::size_t q) {
    check(q);
    circuit_.push_back({op, static_cast<std::uint16_t>(q), 0});
  }

  void ensure_dense_safe() const {
    if (n_ >= 24)
      throw std::runtime_error("dense validation safety limit is 23 qubits");
  }

  static std::vector<std::complex<double>> to_double(
      const std::vector<DenseCore::Complex>& input) {
    std::vector<std::complex<double>> out;
    out.reserve(input.size());
    for (const auto& a : input) out.emplace_back(a.real(), a.imag());
    return out;
  }

  static void apply_reference_gate(DenseCore& dense,
                                   const ExactExperimentGate& gate) {
    switch (gate.op) {
      case ExactExperimentOp::H: dense.hadamard(gate.a); break;
      case ExactExperimentOp::S: dense.phase_s(gate.a); break;
      case ExactExperimentOp::X: dense.pauli_x(gate.a); break;
      case ExactExperimentOp::Z: dense.pauli_z(gate.a); break;
      case ExactExperimentOp::CX: dense.cnot(gate.a, gate.b); break;
      case ExactExperimentOp::CZ: dense.controlled_z(gate.a, gate.b); break;
      case ExactExperimentOp::T: dense.phase_t(gate.a); break;
    }
  }

  void replay_branch(DenseCore& dense, const Branch& branch) const {
    std::size_t t_index = 0;
    for (const auto& gate : circuit_) {
      if (gate.op == ExactExperimentOp::T) {
        if ((branch.t_choices >> t_index) & 1ULL)
          dense.pauli_z(gate.a);
        ++t_index;
      } else {
        apply_reference_gate(dense, gate);
      }
    }
  }

  static std::complex<double> pauli_expectation(
      const std::vector<DenseCore::Complex>& state,
      const std::string& generator) {
    if (generator.size() < 2) return {0.0, 0.0};
    const std::size_t n = generator.size() - 1;
    const std::size_t dim = state.size();
    const double sign = generator[0] == '-' ? -1.0 : 1.0;

    std::complex<double> sum{0.0, 0.0};
    const std::complex<double> I{0.0, 1.0};

    for (std::size_t i = 0; i < dim; ++i) {
      std::size_t j = i;
      std::complex<double> phase{sign, 0.0};

      for (std::size_t q = 0; q < n; ++q) {
        const bool bit = ((i >> q) & 1ULL) != 0;
        switch (generator[q + 1]) {
          case 'I':
            break;
          case 'X':
            j ^= (std::size_t{1} << q);
            break;
          case 'Z':
            if (bit) phase = -phase;
            break;
          case 'Y':
            j ^= (std::size_t{1} << q);
            phase *= bit ? -I : I;
            break;
          default:
            throw std::runtime_error("invalid Pauli generator");
        }
      }

      const std::complex<double> ai{state[i].real(), state[i].imag()};
      const std::complex<double> aj{state[j].real(), state[j].imag()};
      sum += std::conj(aj) * phase * ai;
    }

    return sum;
  }

  std::size_t n_;
  std::size_t max_branches_;
  std::size_t t_count_ = 0;
  std::vector<Branch> branches_;
  std::vector<ExactExperimentGate> circuit_;
};


class ReducedExactTRepresentation {
 public:
  struct PauliKey {
    std::uint64_t x = 0;
    std::uint64_t z = 0;

    bool operator==(const PauliKey& other) const {
      return x == other.x && z == other.z;
    }
  };

  struct PauliKeyHash {
    std::size_t operator()(const PauliKey& p) const {
      std::uint64_t h = p.x * 0x9E3779B185EBCA87ULL;
      h ^= p.z + 0xC2B2AE3D27D4EB4FULL + (h << 6U) + (h >> 2U);
      return static_cast<std::size_t>(h ^ (h >> 32U));
    }
  };

  explicit ReducedExactTRepresentation(std::size_t qubits)
      : n_(qubits), base_(qubits) {
    if (qubits == 0 || qubits > 63)
      throw std::invalid_argument("reduced representation supports 1..63 qubits");
    components_[PauliKey{}] = {1.0, 0.0};
  }

  std::size_t qubits() const { return n_; }
  std::size_t t_count() const { return t_count_; }
  std::size_t component_count() const { return components_.size(); }
  std::size_t base_tableau_bytes() const { return base_.state_bytes(); }

  std::size_t logical_payload_bytes() const {
    return base_.state_bytes() +
           components_.size() *
               (sizeof(PauliKey) + sizeof(std::complex<double>)) +
           circuit_.size() * sizeof(ExactExperimentGate) +
           base_circuit_.size() * sizeof(ExactExperimentGate);
  }

  void hadamard(std::size_t q) {
    apply_clifford({ExactExperimentOp::H, narrow(q), 0});
  }

  void phase_s(std::size_t q) {
    apply_clifford({ExactExperimentOp::S, narrow(q), 0});
  }

  void pauli_x(std::size_t q) {
    apply_clifford({ExactExperimentOp::X, narrow(q), 0});
  }

  void pauli_z(std::size_t q) {
    apply_clifford({ExactExperimentOp::Z, narrow(q), 0});
  }

  void cnot(std::size_t control, std::size_t target) {
    check(control);
    check(target);
    if (control == target) throw std::invalid_argument("control == target");
    apply_clifford(
        {ExactExperimentOp::CX, narrow(control), narrow(target)});
  }

  void controlled_z(std::size_t a, std::size_t b) {
    check(a);
    check(b);
    if (a == b) throw std::invalid_argument("a == b");
    apply_clifford({ExactExperimentOp::CZ, narrow(a), narrow(b)});
  }

  void phase_t(std::size_t q) {
    check(q);

    const double s = 1.0 / std::sqrt(2.0);
    const std::complex<double> e{s, s};
    const std::complex<double> alpha =
        (std::complex<double>{1.0, 0.0} + e) * 0.5;
    const std::complex<double> beta =
        (std::complex<double>{1.0, 0.0} - e) * 0.5;

    const auto basis = stabilizer_basis();
    ComponentMap next;
    next.reserve(std::min<std::size_t>(
        std::size_t{1} << std::min<std::size_t>(n_, 20),
        components_.size() * 2 + 1));

    const Pauli zq = pauli_z_op(q);

    for (const auto& entry : components_) {
      const Pauli p{entry.first.x, entry.first.z, 0};
      accumulate(next, p, entry.second * alpha, basis);
      accumulate(next, multiply(zq, p), entry.second * beta, basis);
    }

    components_ = std::move(next);
    circuit_.push_back({ExactExperimentOp::T, narrow(q), 0});
    ++t_count_;
  }

  std::vector<std::complex<double>> dense_reference() const {
    ensure_dense_safe();
    DenseCore dense(n_);
    for (const auto& gate : circuit_) apply_dense_gate(dense, gate);
    return to_double_state(dense.state());
  }

  std::vector<std::complex<double>> dense_from_reduced() const {
    ensure_dense_safe();

    DenseCore dense_base(n_);
    for (const auto& gate : base_circuit_) apply_dense_gate(dense_base, gate);
    const auto base_state = to_double_state(dense_base.state());

    const std::size_t dim = std::size_t{1} << n_;
    std::vector<std::complex<double>> sum(dim, {0.0, 0.0});

    for (const auto& entry : components_) {
      const auto acted = apply_pauli_state(base_state, entry.first);
      for (std::size_t i = 0; i < dim; ++i)
        sum[i] += entry.second * acted[i];
    }

    return sum;
  }

  double fidelity() const {
    const auto reference = dense_reference();
    const auto reduced = dense_from_reduced();

    std::complex<double> overlap{0.0, 0.0};
    double ref_norm = 0.0;
    double reduced_norm = 0.0;

    for (std::size_t i = 0; i < reference.size(); ++i) {
      overlap += std::conj(reference[i]) * reduced[i];
      ref_norm += std::norm(reference[i]);
      reduced_norm += std::norm(reduced[i]);
    }

    if (ref_norm <= 0.0 || reduced_norm <= 0.0) return 0.0;
    return std::norm(overlap) / (ref_norm * reduced_norm);
  }

  double reduced_norm() const {
    const auto reduced = dense_from_reduced();
    double norm = 0.0;
    for (const auto& a : reduced) norm += std::norm(a);
    return norm;
  }

 private:
  struct Pauli {
    std::uint64_t x = 0;
    std::uint64_t z = 0;
    std::uint8_t phase = 0;  // i^phase * X^x Z^z
  };

  struct StabilizerBasis {
    std::vector<Pauli> rows;
    std::vector<std::size_t> pivots;
  };

  using ComponentMap =
      std::unordered_map<PauliKey, std::complex<double>, PauliKeyHash>;

  void check(std::size_t q) const {
    if (q >= n_) throw std::out_of_range("qubit");
  }

  std::uint16_t narrow(std::size_t q) const {
    check(q);
    if (q > std::numeric_limits<std::uint16_t>::max())
      throw std::out_of_range("qubit index");
    return static_cast<std::uint16_t>(q);
  }

  static unsigned parity(std::uint64_t x) {
#if defined(__GNUG__) || defined(__clang__)
    return static_cast<unsigned>(__builtin_parityll(x));
#else
    unsigned p = 0;
    while (x) {
      p ^= 1U;
      x &= x - 1;
    }
    return p;
#endif
  }

  static std::complex<double> phase_unit(std::uint8_t phase) {
    switch (phase & 3U) {
      case 0: return {1.0, 0.0};
      case 1: return {0.0, 1.0};
      case 2: return {-1.0, 0.0};
      default: return {0.0, -1.0};
    }
  }

  static Pauli multiply(const Pauli& a, const Pauli& b) {
    Pauli out;
    out.x = a.x ^ b.x;
    out.z = a.z ^ b.z;
    out.phase = static_cast<std::uint8_t>(
        (a.phase + b.phase + 2U * parity(a.z & b.x)) & 3U);
    return out;
  }

  static bool bit_at(const Pauli& p, std::size_t n, std::size_t col) {
    if (col < n) return ((p.x >> col) & 1ULL) != 0;
    return ((p.z >> (col - n)) & 1ULL) != 0;
  }

  static Pauli pauli_x_op(std::size_t q) {
    return {std::uint64_t{1} << q, 0, 0};
  }

  static Pauli pauli_z_op(std::size_t q) {
    return {0, std::uint64_t{1} << q, 0};
  }

  static Pauli pauli_y_op(std::size_t q) {
    return {std::uint64_t{1} << q, std::uint64_t{1} << q, 1};
  }

  Pauli parse_generator(const std::string& g) const {
    if (g.size() != n_ + 1)
      throw std::runtime_error("unexpected stabilizer generator length");

    Pauli p;
    if (g[0] == '-') p.phase = 2;
    else if (g[0] != '+') throw std::runtime_error("invalid generator sign");

    for (std::size_t q = 0; q < n_; ++q) {
      switch (g[q + 1]) {
        case 'I':
          break;
        case 'X':
          p.x |= (std::uint64_t{1} << q);
          break;
        case 'Z':
          p.z |= (std::uint64_t{1} << q);
          break;
        case 'Y':
          p.x |= (std::uint64_t{1} << q);
          p.z |= (std::uint64_t{1} << q);
          p.phase = static_cast<std::uint8_t>((p.phase + 1U) & 3U);
          break;
        default:
          throw std::runtime_error("invalid generator Pauli");
      }
    }
    return p;
  }

  StabilizerBasis stabilizer_basis() const {
    StabilizerBasis basis;
    basis.rows.reserve(n_);

    for (std::size_t r = 0; r < n_; ++r)
      basis.rows.push_back(parse_generator(base_.generator_string(r)));

    std::size_t rank = 0;
    for (std::size_t col = 0; col < 2 * n_ && rank < n_; ++col) {
      std::size_t pivot = rank;
      while (pivot < basis.rows.size() &&
             !bit_at(basis.rows[pivot], n_, col)) {
        ++pivot;
      }
      if (pivot == basis.rows.size()) continue;

      std::swap(basis.rows[rank], basis.rows[pivot]);

      for (std::size_t r = 0; r < basis.rows.size(); ++r) {
        if (r != rank && bit_at(basis.rows[r], n_, col))
          basis.rows[r] = multiply(basis.rows[r], basis.rows[rank]);
      }

      basis.pivots.push_back(col);
      ++rank;
    }

    if (rank != n_)
      throw std::runtime_error("stabilizer basis lost rank");

    basis.rows.resize(rank);
    return basis;
  }

  Pauli canonicalize(Pauli p, const StabilizerBasis& basis) const {
    for (std::size_t r = 0; r < basis.pivots.size(); ++r) {
      if (bit_at(p, n_, basis.pivots[r]))
        p = multiply(p, basis.rows[r]);  // right multiply by +1 stabilizer
    }
    return p;
  }

  void accumulate(ComponentMap& map, Pauli p,
                  std::complex<double> coefficient,
                  const StabilizerBasis& basis) const {
    p = canonicalize(p, basis);
    coefficient *= phase_unit(p.phase);
    p.phase = 0;

    const PauliKey key{p.x, p.z};
    auto it = map.find(key);
    if (it == map.end())
      map.emplace(key, coefficient);
    else
      it->second += coefficient;
  }

  Pauli image_x(std::size_t q, const ExactExperimentGate& gate) const {
    Pauli p = pauli_x_op(q);
    switch (gate.op) {
      case ExactExperimentOp::H:
        if (q == gate.a) p = pauli_z_op(q);
        break;
      case ExactExperimentOp::S:
        if (q == gate.a) p = pauli_y_op(q);
        break;
      case ExactExperimentOp::X:
        break;
      case ExactExperimentOp::Z:
        if (q == gate.a) p.phase = 2;
        break;
      case ExactExperimentOp::CX:
        if (q == gate.a) {
          p = multiply(pauli_x_op(gate.a), pauli_x_op(gate.b));
        }
        break;
      case ExactExperimentOp::CZ:
        if (q == gate.a) {
          p = multiply(pauli_x_op(gate.a), pauli_z_op(gate.b));
        } else if (q == gate.b) {
          p = multiply(pauli_z_op(gate.a), pauli_x_op(gate.b));
        }
        break;
      case ExactExperimentOp::T:
        throw std::logic_error("T is not Clifford");
    }
    return p;
  }

  Pauli image_z(std::size_t q, const ExactExperimentGate& gate) const {
    Pauli p = pauli_z_op(q);
    switch (gate.op) {
      case ExactExperimentOp::H:
        if (q == gate.a) p = pauli_x_op(q);
        break;
      case ExactExperimentOp::S:
        break;
      case ExactExperimentOp::X:
        if (q == gate.a) p.phase = 2;
        break;
      case ExactExperimentOp::Z:
        break;
      case ExactExperimentOp::CX:
        if (q == gate.b) {
          p = multiply(pauli_z_op(gate.a), pauli_z_op(gate.b));
        }
        break;
      case ExactExperimentOp::CZ:
        break;
      case ExactExperimentOp::T:
        throw std::logic_error("T is not Clifford");
    }
    return p;
  }

  Pauli conjugate(Pauli p, const ExactExperimentGate& gate) const {
    Pauli out{0, 0, p.phase};
    for (std::size_t q = 0; q < n_; ++q) {
      if ((p.x >> q) & 1ULL) out = multiply(out, image_x(q, gate));
      if ((p.z >> q) & 1ULL) out = multiply(out, image_z(q, gate));
    }
    return out;
  }

  void update_base(const ExactExperimentGate& gate) {
    switch (gate.op) {
      case ExactExperimentOp::H: base_.hadamard(gate.a); break;
      case ExactExperimentOp::S: base_.phase_s(gate.a); break;
      case ExactExperimentOp::X: base_.pauli_x(gate.a); break;
      case ExactExperimentOp::Z: base_.pauli_z(gate.a); break;
      case ExactExperimentOp::CX: base_.cnot(gate.a, gate.b); break;
      case ExactExperimentOp::CZ: base_.controlled_z(gate.a, gate.b); break;
      case ExactExperimentOp::T:
        throw std::logic_error("T cannot update stabilizer base");
    }
  }

  void apply_clifford(const ExactExperimentGate& gate) {
    circuit_.push_back(gate);
    base_circuit_.push_back(gate);

    update_base(gate);
    const auto basis = stabilizer_basis();

    ComponentMap next;
    next.reserve(components_.size() + 1);

    for (const auto& entry : components_) {
      const Pauli p{entry.first.x, entry.first.z, 0};
      accumulate(next, conjugate(p, gate), entry.second, basis);
    }

    components_ = std::move(next);
  }

  static void apply_dense_gate(DenseCore& dense,
                               const ExactExperimentGate& gate) {
    switch (gate.op) {
      case ExactExperimentOp::H: dense.hadamard(gate.a); break;
      case ExactExperimentOp::S: dense.phase_s(gate.a); break;
      case ExactExperimentOp::X: dense.pauli_x(gate.a); break;
      case ExactExperimentOp::Z: dense.pauli_z(gate.a); break;
      case ExactExperimentOp::CX: dense.cnot(gate.a, gate.b); break;
      case ExactExperimentOp::CZ: dense.controlled_z(gate.a, gate.b); break;
      case ExactExperimentOp::T: dense.phase_t(gate.a); break;
    }
  }

  static std::vector<std::complex<double>> to_double_state(
      const std::vector<DenseCore::Complex>& input) {
    std::vector<std::complex<double>> out;
    out.reserve(input.size());
    for (const auto& a : input)
      out.emplace_back(static_cast<double>(a.real()),
                       static_cast<double>(a.imag()));
    return out;
  }

  static std::vector<std::complex<double>> apply_pauli_state(
      const std::vector<std::complex<double>>& input,
      const PauliKey& p) {
    std::vector<std::complex<double>> out(input.size(), {0.0, 0.0});
    for (std::size_t i = 0; i < input.size(); ++i) {
      const std::size_t j = i ^ static_cast<std::size_t>(p.x);
      const double sign = parity(p.z & static_cast<std::uint64_t>(i))
                              ? -1.0
                              : 1.0;
      out[j] += sign * input[i];
    }
    return out;
  }

  void ensure_dense_safe() const {
    if (n_ >= 24)
      throw std::runtime_error("dense validation safety limit is 23 qubits");
  }

  std::size_t n_;
  std::size_t t_count_ = 0;
  CompactCore base_;
  ComponentMap components_;
  std::vector<ExactExperimentGate> circuit_;
  std::vector<ExactExperimentGate> base_circuit_;
};

inline ExactTExperimentResult run_reduced_exact_t_experiment(
    std::size_t qubits, std::size_t t_gates) {
  if (qubits == 0 || qubits > 12)
    throw std::invalid_argument("reduced experiment validation limit is 1..12 qubits");
  if (t_gates > 128)
    throw std::invalid_argument("reduced experiment safety limit is 128 T gates");

  ReducedExactTRepresentation q(qubits);

  q.hadamard(0);
  for (std::size_t i = 1; i < qubits; ++i) {
    q.cnot(i - 1, i);
    if ((i & 1U) == 0U) q.phase_s(i);
  }

  for (std::size_t k = 0; k < t_gates; ++k) {
    const std::size_t a = k % qubits;
    const std::size_t b = (a + 1) % qubits;

    q.phase_t(a);
    q.hadamard(a);
    if (qubits > 1) q.cnot(a, b);
    if ((k & 1U) != 0U) q.phase_s(b);
  }

  const double fidelity = q.fidelity();
  const double norm = q.reduced_norm();
  const bool fidelity_ok = std::abs(fidelity - 1.0) < 3e-5;
  const bool norm_ok = std::abs(norm - 1.0) < 3e-5;

  DenseCore dense(qubits);
  const std::size_t dense_bytes = dense.state_bytes();

  std::ostringstream out;
  out << std::fixed << std::setprecision(9);
  out << "REDUCED EXACT T REPRESENTATION\n";
  out << "qubits=" << qubits << "\n";
  out << "t_gates=" << t_gates << "\n";
  if (t_gates < 63)
    out << "naive_branches=" << (std::uint64_t{1} << t_gates) << "\n";
  else
    out << "naive_branches=2^" << t_gates << " (not materialized)\n";
  out << "reduced_components=" << q.component_count() << "\n";
  out << "component_upper_bound=" << (std::uint64_t{1} << qubits) << "\n";
  out << "base_tableau_bytes=" << q.base_tableau_bytes() << "\n";
  out << "logical_payload_bytes=" << q.logical_payload_bytes() << "\n";
  out << "dense_reference_bytes=" << dense_bytes << "\n";
  out << "fidelity=" << fidelity << "\n";
  out << "reduced_norm=" << norm << "\n";
  out << "result=" << ((fidelity_ok && norm_ok) ? "PASS" : "FAIL") << "\n";
  out << "note=2^T branches are never materialized; Dense Core is validation only\n";

  ExactTExperimentResult result;
  result.all_passed = fidelity_ok && norm_ok;
  result.report = out.str();
  return result;
}

struct ExactTExperimentResult {
  bool all_passed = false;
  std::string report;
};

inline ExactTExperimentResult run_exact_t_experiment(std::size_t qubits,
                                                      std::size_t t_gates) {
  if (qubits == 0) throw std::invalid_argument("qubits must be >= 1");
  if (qubits > 10)
    throw std::invalid_argument("experiment validation limit is 10 qubits");
  if (t_gates > 12)
    throw std::invalid_argument("experiment safety limit is 12 T gates");

  ExactTExperiment q(qubits);

  // Prepare a nontrivial stabilizer state.
  q.hadamard(0);
  for (std::size_t i = 1; i < qubits; ++i) {
    q.cnot(i - 1, i);
    if ((i & 1U) == 0U) q.phase_s(i);
  }

  // Interleave T gates with Clifford mixing so subsequent T operations do not
  // remain confined to a trivial computational-basis phase.
  for (std::size_t k = 0; k < t_gates; ++k) {
    const std::size_t a = k % qubits;
    const std::size_t b = (a + 1) % qubits;

    q.phase_t(a);
    q.hadamard(a);
    if (qubits > 1) q.cnot(a, b);
    if ((k & 1U) != 0U) q.phase_s(b);
  }

  const double fidelity = q.fidelity();
  const double norm = q.decomposition_norm();
  const bool tableau_ok = q.branch_tableaus_match_dense();
  const auto redundancy = q.analyze_redundancy_dense_oracle();

  const std::size_t expected_branches =
      t_gates >= 63 ? 0 : (std::size_t{1} << t_gates);
  const bool branches_ok = q.branch_count() == expected_branches;
  const bool fidelity_ok = std::abs(fidelity - 1.0) < 2e-5;
  const bool norm_ok = std::abs(norm - 1.0) < 2e-5;
  const bool grouped_fidelity_ok =
      std::abs(redundancy.grouped_fidelity - 1.0) < 2e-5;
  const bool grouped_norm_ok =
      std::abs(redundancy.grouped_norm - 1.0) < 2e-5;

  DenseCore dense(qubits);
  const std::size_t dense_bytes = dense.state_bytes();

  std::ostringstream out;
  out << std::fixed << std::setprecision(9);
  out << "EXACT T DECOMPOSITION EXPERIMENT\n";
  out << "qubits=" << qubits << "\n";
  out << "t_gates=" << t_gates << "\n";
  out << "branches=" << q.branch_count() << "\n";
  out << "expected_branches=" << expected_branches << "\n";
  out << "tableau_bytes=" << q.tableau_bytes() << "\n";
  out << "approx_explicit_bytes=" << q.approximate_explicit_bytes() << "\n";
  out << "dense_reference_bytes=" << dense_bytes << "\n";
  out << "fidelity=" << fidelity << "\n";
  out << "decomposition_norm=" << norm << "\n";
  out << "branch_tableaus=" << (tableau_ok ? "PASS" : "FAIL") << "\n";
  out << "branches_exact=" << (branches_ok ? "PASS" : "FAIL") << "\n";
  out << "unique_tableaus=" << redundancy.unique_tableaus << "\n";
  out << "max_tableau_multiplicity=" << redundancy.max_tableau_multiplicity << "\n";
  out << "oracle_unique_physical_states="
      << redundancy.unique_physical_states << "\n";
  out << "oracle_max_physical_multiplicity="
      << redundancy.max_physical_multiplicity << "\n";
  out << "oracle_grouped_fidelity=" << redundancy.grouped_fidelity << "\n";
  out << "oracle_grouped_norm=" << redundancy.grouped_norm << "\n";
  out << "oracle_mergeable_branches="
      << (redundancy.total_branches - redundancy.unique_physical_states) << "\n";
  out << "oracle_reduction_factor="
      << (redundancy.unique_physical_states == 0
              ? 0.0
              : static_cast<double>(redundancy.total_branches) /
                    static_cast<double>(redundancy.unique_physical_states))
      << "\n";
  out << "stabilizer_pauli_orbit_upper_bound="
      << (std::size_t{1} << qubits) << "\n";
  out << "result="
      << ((tableau_ok && branches_ok && fidelity_ok && norm_ok &&
           grouped_fidelity_ok && grouped_norm_ok) ? "PASS" : "FAIL")
      << "\n";
  out << "note=no pruning in representation; oracle grouping is diagnostic only\n";

  ExactTExperimentResult result;
  result.all_passed = tableau_ok && branches_ok && fidelity_ok && norm_ok &&
                      grouped_fidelity_ok && grouped_norm_ok;
  result.report = out.str();
  return result;
}

}  // namespace mundo_x
