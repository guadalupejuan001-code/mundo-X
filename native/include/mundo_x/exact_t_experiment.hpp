#pragma once

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
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
  TDagger,
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
      case ExactExperimentOp::TDagger:
        dense.phase_t(gate.a);
        for (unsigned i = 0; i < 3; ++i) dense.phase_s(gate.a);
        break;
    }
  }

  void replay_branch(DenseCore& dense, const Branch& branch) const {
    std::size_t t_index = 0;
    for (const auto& gate : circuit_) {
      if (gate.op == ExactExperimentOp::T || gate.op == ExactExperimentOp::TDagger) {
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


struct ExactTExperimentResult {
  bool all_passed = false;
  std::string report;
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

  explicit ReducedExactTRepresentation(
      std::size_t qubits,
      std::size_t max_components = std::numeric_limits<std::size_t>::max(),
      std::function<bool()> cancelled = {})
      : n_(validated_qubits(qubits)), max_components_(max_components), cancelled_(std::move(cancelled)), base_(n_) {
    if (max_components_ == 0)
      throw std::invalid_argument("max_components must be positive");
    check_cancelled();
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

  void phase_t(std::size_t q) { apply_t(q, false); }

  void phase_t_dagger(std::size_t q) { apply_t(q, true); }

  // The canonical Pauli cosets acting on a pure stabilizer base form an
  // orthonormal basis. Thus no dense 2^n vector is needed for these observables.
  // All results are UNNORMALIZED: divide expectation by sparse_norm() when
  // computing a normalized observable. "Exact" means no branch truncation or
  // approximate pruning; coefficients still use ordinary double arithmetic.
  double sparse_norm() const {
    check_cancelled();
    double total = 0.0, correction = 0.0;
    std::size_t visited = 0;
    for (const auto& entry : components_) {
      const double term = std::norm(entry.second) - correction;
      const double next = total + term;
      correction = (next - total) - term;
      total = next;
      poll_components(++visited);
    }
    return total;
  }

  // Q = i^phase X^x Z^z, with phase interpreted modulo four. For example,
  // one-qubit Y has x=1, z=1, phase=1. Non-Hermitian Paulis are supported and
  // may yield complex results. Bits outside the declared register are errors.
  std::complex<double> pauli_expectation(std::uint64_t x, std::uint64_t z,
                                         std::uint8_t phase = 0) const {
    if (((x | z) >> n_) != 0) throw std::out_of_range("Pauli mask outside qubit register");
    check_cancelled();
    const auto basis = stabilizer_basis();
    const Pauli observable{x, z, static_cast<std::uint8_t>(phase & 3U)};
    std::complex<double> total{0.0, 0.0};
    std::size_t visited = 0;
    for (const auto& entry : components_) {
      const Pauli p{entry.first.x, entry.first.z, 0};
      const auto acted = canonicalize(multiply(observable, p), basis);
      const auto found = components_.find(PauliKey{acted.x, acted.z});
      if (found != components_.end())
        total += std::conj(found->second) * phase_unit(acted.phase) * entry.second;
      poll_components(++visited);
    }
    return total;
  }

  double z_expectation(std::size_t q) const {
    check(q);
    return pauli_expectation(0, std::uint64_t{1} << q).real();
  }

 private:
  void apply_t(std::size_t q, bool dagger) {
    check(q);
    check_cancelled();

    const double s = 1.0 / std::sqrt(2.0);
    const std::complex<double> e{s, dagger ? -s : s};
    const std::complex<double> alpha =
        (std::complex<double>{1.0, 0.0} + e) * 0.5;
    const std::complex<double> beta =
        (std::complex<double>{1.0, 0.0} - e) * 0.5;

    const auto basis = stabilizer_basis();
    ComponentMap next;
    next.reserve(std::min<std::size_t>(
        std::size_t{1} << std::min<std::size_t>(n_, 20),
        std::min(max_components_, components_.size() > (std::numeric_limits<std::size_t>::max() - 1) / 2
            ? std::numeric_limits<std::size_t>::max() : components_.size() * 2 + 1)));

    const Pauli zq = pauli_z_op(q);

    std::size_t visited = 0;
    for (const auto& entry : components_) {
      const Pauli p{entry.first.x, entry.first.z, 0};
      accumulate(next, p, entry.second * alpha, basis);
      accumulate(next, multiply(zq, p), entry.second * beta, basis);
      poll_components(++visited);
    }

    check_cancelled();
    circuit_.push_back({dagger ? ExactExperimentOp::TDagger : ExactExperimentOp::T, narrow(q), 0});
    components_ = std::move(next);
    ++t_count_;
  }

 public:
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

  struct CoefficientStructureProfile {
    std::size_t full_coefficients = 0;
    std::size_t active_coefficients = 0;
    std::vector<std::size_t> cut_ranks;
    std::size_t max_cut_rank = 0;
    std::size_t factorized_complex_scalars = 0;
  };

  CoefficientStructureProfile coefficient_structure_profile() const {
    if (n_ > 20)
      throw std::runtime_error("coefficient profile safety limit is 20 qubits");

    const auto basis = stabilizer_basis();

    std::vector<bool> is_pivot(2 * n_, false);
    for (const auto p : basis.pivots) is_pivot[p] = true;

    std::vector<std::size_t> free_cols;
    free_cols.reserve(n_);
    for (std::size_t col = 0; col < 2 * n_; ++col) {
      if (!is_pivot[col]) free_cols.push_back(col);
    }
    if (free_cols.size() != n_)
      throw std::runtime_error("unexpected stabilizer quotient dimension");

    const std::size_t dim = std::size_t{1} << n_;
    std::vector<std::complex<double>> coeffs(dim, {0.0, 0.0});

    for (const auto& entry : components_) {
      const Pauli p{entry.first.x, entry.first.z, 0};
      std::size_t index = 0;
      for (std::size_t bit = 0; bit < free_cols.size(); ++bit) {
        if (bit_at(p, n_, free_cols[bit]))
          index |= (std::size_t{1} << bit);
      }
      coeffs[index] += entry.second;
    }

    CoefficientStructureProfile out;
    out.full_coefficients = dim;
    for (const auto& a : coeffs) {
      if (std::abs(a) > 1e-12) ++out.active_coefficients;
    }

    if (n_ <= 1) {
      out.factorized_complex_scalars = dim;
      return out;
    }

    out.cut_ranks.reserve(n_ - 1);
    for (std::size_t cut = 1; cut < n_; ++cut) {
      const std::size_t rows = std::size_t{1} << cut;
      const std::size_t cols = std::size_t{1} << (n_ - cut);

      std::vector<std::complex<double>> matrix(rows * cols);
      for (std::size_t index = 0; index < dim; ++index) {
        const std::size_t row = index & (rows - 1);
        const std::size_t col = index >> cut;
        matrix[row * cols + col] = coeffs[index];
      }

      const std::size_t rank = complex_matrix_rank(matrix, rows, cols);
      out.cut_ranks.push_back(rank);
      out.max_cut_rank = std::max(out.max_cut_rank, rank);
    }

    std::size_t previous_rank = 1;
    for (std::size_t site = 0; site < n_; ++site) {
      const std::size_t next_rank =
          (site + 1 == n_) ? 1 : out.cut_ranks[site];
      out.factorized_complex_scalars +=
          2 * previous_rank * next_rank;
      previous_rank = next_rank;
    }

    return out;
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

  static std::size_t validated_qubits(std::size_t n) {
    if (n == 0 || n > 63)
      throw std::invalid_argument("reduced representation supports 1..63 qubits");
    return n;
  }

  void check_cancelled() const {
    if (cancelled_ && cancelled_())
      throw std::runtime_error("reduced representation cancelled");
  }

  void poll_components(std::size_t visited) const {
    if ((visited & 255U) == 0) check_cancelled();
  }

  static std::size_t complex_matrix_rank(
      std::vector<std::complex<double>> matrix,
      std::size_t rows, std::size_t cols) {
    double scale = 0.0;
    for (const auto& v : matrix) scale = std::max(scale, std::abs(v));
    if (scale == 0.0) return 0;

    const double tolerance = scale * 1e-10;
    std::size_t rank = 0;

    for (std::size_t col = 0; col < cols && rank < rows; ++col) {
      std::size_t pivot = rank;
      double best = std::abs(matrix[pivot * cols + col]);
      for (std::size_t r = rank + 1; r < rows; ++r) {
        const double candidate = std::abs(matrix[r * cols + col]);
        if (candidate > best) {
          best = candidate;
          pivot = r;
        }
      }

      if (best <= tolerance) continue;

      if (pivot != rank) {
        for (std::size_t c = col; c < cols; ++c)
          std::swap(matrix[rank * cols + c], matrix[pivot * cols + c]);
      }

      const auto pivot_value = matrix[rank * cols + col];
      for (std::size_t r = rank + 1; r < rows; ++r) {
        const auto value = matrix[r * cols + col];
        if (std::abs(value) <= tolerance) continue;
        const auto factor = value / pivot_value;
        for (std::size_t c = col; c < cols; ++c)
          matrix[r * cols + c] -= factor * matrix[rank * cols + c];
      }

      ++rank;
    }

    return rank;
  }

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

  StabilizerBasis stabilizer_basis() const { return stabilizer_basis(base_); }

  StabilizerBasis stabilizer_basis(const CompactCore& state) const {
    StabilizerBasis basis;
    basis.rows.reserve(n_);

    for (std::size_t r = 0; r < n_; ++r)
      basis.rows.push_back(parse_generator(state.generator_string(r)));

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
    if (it == map.end()) {
      // Count every stored key, including exactly zero coefficients. Never
      // approximate-prune to fit the budget, and never insert beyond the cap.
      if (map.size() >= max_components_)
        throw std::runtime_error("reduced component limit reached");
      map.emplace(key, coefficient);
    } else
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
      case ExactExperimentOp::T: case ExactExperimentOp::TDagger:
        throw std::logic_error("T/T-dagger is not Clifford");
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
      case ExactExperimentOp::T: case ExactExperimentOp::TDagger:
        throw std::logic_error("T/T-dagger is not Clifford");
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

  static void update_base(CompactCore& state, const ExactExperimentGate& gate) {
    switch (gate.op) {
      case ExactExperimentOp::H: state.hadamard(gate.a); break;
      case ExactExperimentOp::S: state.phase_s(gate.a); break;
      case ExactExperimentOp::X: state.pauli_x(gate.a); break;
      case ExactExperimentOp::Z: state.pauli_z(gate.a); break;
      case ExactExperimentOp::CX: state.cnot(gate.a, gate.b); break;
      case ExactExperimentOp::CZ: state.controlled_z(gate.a, gate.b); break;
      case ExactExperimentOp::T: case ExactExperimentOp::TDagger:
        throw std::logic_error("T/T-dagger cannot update stabilizer base");
    }
  }

  void apply_clifford(const ExactExperimentGate& gate) {
    check_cancelled();
    // Prepare the entire update before committing: cancellation or a component
    // limit leaves the existing state and gate history untouched.
    CompactCore next_base = base_;
    update_base(next_base, gate);
    const auto basis = stabilizer_basis(next_base);

    ComponentMap next;
    next.reserve(components_.size());

    std::size_t visited = 0;
    for (const auto& entry : components_) {
      const Pauli p{entry.first.x, entry.first.z, 0};
      accumulate(next, conjugate(p, gate), entry.second, basis);
      poll_components(++visited);
    }

    check_cancelled();
    circuit_.push_back(gate);
    base_circuit_.push_back(gate);
    base_ = std::move(next_base);
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
      case ExactExperimentOp::TDagger:
        // DenseCore has T and S: T S^3 = T^dagger, with no global phase.
        dense.phase_t(gate.a);
        for (unsigned i = 0; i < 3; ++i) dense.phase_s(gate.a);
        break;
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
  std::size_t max_components_;
  std::function<bool()> cancelled_;
  std::size_t t_count_ = 0;
  CompactCore base_;
  ComponentMap components_;
  std::vector<ExactExperimentGate> circuit_;
  std::vector<ExactExperimentGate> base_circuit_;
};


inline ExactTExperimentResult run_magic_state_witness() {
  DenseCore magic(1);
  magic.hadamard(0);
  magic.phase_t(0);

  const auto& psi = magic.state();

  auto overlap_fidelity = [&](const std::vector<std::complex<double>>& phi) {
    std::complex<double> overlap{0.0, 0.0};
    for (std::size_t i = 0; i < phi.size(); ++i) {
      overlap += std::conj(phi[i]) *
                 std::complex<double>{psi[i].real(), psi[i].imag()};
    }
    return std::norm(overlap);
  };

  const double s = 1.0 / std::sqrt(2.0);
  const std::complex<double> I{0.0, 1.0};

  std::vector<std::vector<std::complex<double>>> stabilizers = {
      {{1.0, 0.0}, {0.0, 0.0}},
      {{0.0, 0.0}, {1.0, 0.0}},
      {{s, 0.0}, {s, 0.0}},
      {{s, 0.0}, {-s, 0.0}},
      {{s, 0.0}, I * s},
      {{s, 0.0}, -I * s},
  };

  double max_stabilizer_fidelity = 0.0;
  for (const auto& phi : stabilizers) {
    max_stabilizer_fidelity =
        std::max(max_stabilizer_fidelity, overlap_fidelity(phi));
  }

  const std::complex<double> a0{psi[0].real(), psi[0].imag()};
  const std::complex<double> a1{psi[1].real(), psi[1].imag()};
  const double x_expectation =
      2.0 * (std::conj(a0) * a1).real();
  const double y_expectation =
      2.0 * (std::conj(a0) * a1).imag();
  const double z_expectation =
      std::norm(a0) - std::norm(a1);

  const bool non_stabilizer = max_stabilizer_fidelity < 1.0 - 1e-6;
  const bool expected_geometry =
      std::abs(x_expectation - s) < 2e-5 &&
      std::abs(y_expectation - s) < 2e-5 &&
      std::abs(z_expectation) < 2e-5;

  std::ostringstream out;
  out << std::fixed << std::setprecision(9);
  out << "MAGIC STATE WITNESS\n";
  out << "circuit=|0> -> H -> T\n";
  out << "state=(|0> + exp(i*pi/4)|1>)/sqrt(2)\n";
  out << "bloch_x=" << x_expectation << "\n";
  out << "bloch_y=" << y_expectation << "\n";
  out << "bloch_z=" << z_expectation << "\n";
  out << "max_fidelity_to_any_1q_pure_stabilizer="
      << max_stabilizer_fidelity << "\n";
  out << "non_stabilizer=" << (non_stabilizer ? "PASS" : "FAIL") << "\n";
  out << "expected_magic_geometry="
      << (expected_geometry ? "PASS" : "FAIL") << "\n";
  out << "result="
      << ((non_stabilizer && expected_geometry) ? "PASS" : "FAIL") << "\n";

  ExactTExperimentResult result;
  result.all_passed = non_stabilizer && expected_geometry;
  result.report = out.str();
  return result;
}

inline ExactTExperimentResult run_reduced_growth_trace_variant(
    std::size_t qubits, std::size_t t_gates) {
  if (qubits < 4 || qubits > 10)
    throw std::invalid_argument("variant trace requires 4..10 qubits");
  if (t_gates > 24)
    throw std::invalid_argument("variant trace safety limit is 24 T gates");

  ReducedExactTRepresentation q(qubits);
  std::ostringstream out;
  out << "REDUCED T GROWTH TRACE VARIANT\n";
  out << "qubits=" << qubits << "\n";
  out << "t_gates=" << t_gates << "\n";

  std::size_t step = 0;
  std::size_t previous_components = q.component_count();
  std::size_t previous_rank =
      q.coefficient_structure_profile().max_cut_rank;
  std::size_t t_support_increases = 0;
  std::size_t non_t_support_increases = 0;
  std::size_t h_rank_increases = 0;
  std::size_t non_h_rank_increases = 0;

  auto emit = [&](const std::string& gate, bool is_t, bool is_h) {
    const auto profile = q.coefficient_structure_profile();
    const std::size_t components = q.component_count();

    if (components > previous_components) {
      if (is_t) ++t_support_increases;
      else ++non_t_support_increases;
    }
    if (profile.max_cut_rank > previous_rank) {
      if (is_h) ++h_rank_increases;
      else ++non_h_rank_increases;
    }

    out << "step=" << step
        << " gate=" << gate
        << " components=" << components
        << " active=" << profile.active_coefficients
        << " max_cut_rank=" << profile.max_cut_rank
        << " payload_bytes=" << q.logical_payload_bytes()
        << "\n";

    previous_components = components;
    previous_rank = profile.max_cut_rank;
    ++step;
  };

  emit("START", false, false);

  // Structurally different Clifford preparation:
  // local superpositions + CZ ring + alternating S.
  for (std::size_t qidx = 0; qidx < qubits; qidx += 2) {
    q.hadamard(qidx);
    emit("H" + std::to_string(qidx), false, true);
  }
  for (std::size_t qidx = 0; qidx < qubits; ++qidx) {
    const std::size_t next = (qidx + 1) % qubits;
    q.controlled_z(qidx, next);
    emit("CZ" + std::to_string(qidx) + "_" + std::to_string(next),
         false, false);
  }
  for (std::size_t qidx = 1; qidx < qubits; qidx += 2) {
    q.phase_s(qidx);
    emit("S" + std::to_string(qidx), false, false);
  }

  // Different T placement and mixing schedule from the original trace.
  for (std::size_t k = 0; k < t_gates; ++k) {
    const std::size_t a = (3 * k + 1) % qubits;
    const std::size_t hq = (a + 2 + (k & 1U)) % qubits;
    const std::size_t b = (a + 3) % qubits;
    const std::size_t czb = (a + 5) % qubits;

    q.phase_t(a);
    emit("T" + std::to_string(a), true, false);

    if ((k & 1U) == 0U) {
      q.controlled_z(a, czb);
      emit("CZ" + std::to_string(a) + "_" + std::to_string(czb),
           false, false);
    } else {
      q.cnot(a, b);
      emit("CX" + std::to_string(a) + "_" + std::to_string(b),
           false, false);
    }

    q.hadamard(hq);
    emit("H" + std::to_string(hq), false, true);

    if ((k % 3U) == 2U) {
      const std::size_t sq = (a + 1) % qubits;
      q.phase_s(sq);
      emit("S" + std::to_string(sq), false, false);
    }
  }

  const double fidelity = q.fidelity();
  const double norm = q.reduced_norm();
  const bool ok =
      std::abs(fidelity - 1.0) < 3e-5 &&
      std::abs(norm - 1.0) < 3e-5;

  out << std::fixed << std::setprecision(9);
  out << "support_increases_on_T=" << t_support_increases << "\n";
  out << "support_increases_on_non_T=" << non_t_support_increases << "\n";
  out << "rank_increases_on_H=" << h_rank_increases << "\n";
  out << "rank_increases_on_non_H=" << non_h_rank_increases << "\n";
  out << "final_components=" << q.component_count() << "\n";
  out << "fidelity=" << fidelity << "\n";
  out << "reduced_norm=" << norm << "\n";
  out << "result=" << (ok ? "PASS" : "FAIL") << "\n";

  ExactTExperimentResult result;
  result.all_passed = ok;
  result.report = out.str();
  return result;
}

inline ExactTExperimentResult run_reduced_growth_trace(
    std::size_t qubits, std::size_t t_gates) {
  if (qubits == 0 || qubits > 10)
    throw std::invalid_argument("trace validation limit is 1..10 qubits");
  if (t_gates > 32)
    throw std::invalid_argument("trace safety limit is 32 T gates");

  ReducedExactTRepresentation q(qubits);
  std::ostringstream out;
  out << "REDUCED T GROWTH TRACE\n";
  out << "qubits=" << qubits << "\n";
  out << "t_gates=" << t_gates << "\n";

  std::size_t step = 0;
  auto emit = [&](const std::string& gate) {
    const auto profile = q.coefficient_structure_profile();
    out << "step=" << step
        << " gate=" << gate
        << " components=" << q.component_count()
        << " active=" << profile.active_coefficients
        << " max_cut_rank=" << profile.max_cut_rank
        << " payload_bytes=" << q.logical_payload_bytes()
        << "\n";
    ++step;
  };

  emit("START");

  q.hadamard(0);
  emit("H0");

  for (std::size_t i = 1; i < qubits; ++i) {
    q.cnot(i - 1, i);
    emit("CX" + std::to_string(i - 1) + "_" + std::to_string(i));
    if ((i & 1U) == 0U) {
      q.phase_s(i);
      emit("S" + std::to_string(i));
    }
  }

  for (std::size_t k = 0; k < t_gates; ++k) {
    const std::size_t a = k % qubits;
    const std::size_t b = (a + 1) % qubits;

    q.phase_t(a);
    emit("T" + std::to_string(a));

    q.hadamard(a);
    emit("H" + std::to_string(a));

    if (qubits > 1) {
      q.cnot(a, b);
      emit("CX" + std::to_string(a) + "_" + std::to_string(b));
    }

    if ((k & 1U) != 0U) {
      q.phase_s(b);
      emit("S" + std::to_string(b));
    }
  }

  const double fidelity = q.fidelity();
  const double norm = q.reduced_norm();
  const bool ok =
      std::abs(fidelity - 1.0) < 3e-5 &&
      std::abs(norm - 1.0) < 3e-5;

  out << std::fixed << std::setprecision(9);
  out << "final_components=" << q.component_count() << "\n";
  out << "fidelity=" << fidelity << "\n";
  out << "reduced_norm=" << norm << "\n";
  out << "result=" << (ok ? "PASS" : "FAIL") << "\n";

  ExactTExperimentResult result;
  result.all_passed = ok;
  result.report = out.str();
  return result;
}

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
  const auto structure = q.coefficient_structure_profile();
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
  out << "active_coefficients=" << structure.active_coefficients << "\n";
  out << "full_coefficient_space=" << structure.full_coefficients << "\n";
  out << "coefficient_cut_ranks=";
  for (std::size_t i = 0; i < structure.cut_ranks.size(); ++i) {
    if (i) out << ",";
    out << structure.cut_ranks[i];
  }
  out << "\n";
  out << "max_cut_rank=" << structure.max_cut_rank << "\n";
  out << "factorized_complex_scalars="
      << structure.factorized_complex_scalars << "\n";
  out << "dense_coefficient_scalars=" << structure.full_coefficients << "\n";
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
