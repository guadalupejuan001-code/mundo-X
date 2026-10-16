#pragma once

#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <vector>
#include "mundo_x/exact_t_experiment.hpp"

namespace mundo_x {

// Optional front end for circuits with arbitrary phases. Product states are
// kept as one two-amplitude vector per qubit. A genuinely entangling operation
// transfers that product state into ReducedExactTRepresentation. It never
// infers the answer from an adder or assumes particular register values.
//
// A small local rank-one residual is permitted to absorb floating point noise.
// This tolerance is explicit; the sum of accepted residuals is reported. It is
// not a rigorous bound on all floating point error. Transfer keeps the accepted
// product approximations and their residual report. Replay remains available
// explicitly when reconstruction from the original gates is desired.
class AdaptivePhaseCore {
 public:
  using Complex = std::complex<double>;
  using Pair = std::array<Complex, 2>;
  enum class Transition { Transfer, Replay };

  explicit AdaptivePhaseCore(std::size_t n, double product_tolerance = 1e-14,
                             std::size_t max_components = 65536,
                             Transition transition = Transition::Transfer)
      : n_(n), tolerance_(product_tolerance), max_components_(max_components),
        transition_(transition) {
    if (n == 0 || n > 63) throw std::invalid_argument("supports 1..63 qubits");
    if (!std::isfinite(tolerance_) || tolerance_ < 0. || tolerance_ > 1e-8)
      throw std::invalid_argument("invalid product tolerance");
    if (max_components_ == 0) throw std::invalid_argument("zero component limit");
    if (transition_ != Transition::Transfer && transition_ != Transition::Replay)
      throw std::invalid_argument("invalid transition policy");
    product_.assign(n, Pair{Complex{1., 0.}, Complex{0., 0.}});
  }

  std::size_t qubits() const { return n_; }
  bool is_product() const { return !reduced_; }
  double product_residual_sum() const { return residual_sum_; }
  std::size_t gate_count() const { return gate_count_; }
  std::size_t fallback_count() const { return reduced_ ? 1 : 0; }
  std::size_t history_gate_count() const { return history_.size(); }
  std::size_t replayed_gate_count() const { return replayed_gates_; }
  std::size_t transferred_qubit_count() const { return transferred_qubits_; }
  std::size_t component_count() const { return reduced_ ? reduced_->component_count() : 1; }
  std::size_t peak_component_count() const { return peak_components_; }
  std::size_t logical_payload_bytes() const {
    return product_.size() * sizeof(Pair) + history_.size() * sizeof(Gate) +
           (reduced_ ? reduced_->logical_payload_bytes() : 0);
  }

  void hadamard(std::size_t q) { single({'H', q, 0, 0.}); }
  void pauli_x(std::size_t q) { single({'X', q, 0, 0.}); }
  void pauli_z(std::size_t q) { single({'Z', q, 0, 0.}); }
  void phase_s(std::size_t q) { single({'S', q, 0, 0.}); }
  void phase_t(std::size_t q) { single({'T', q, 0, 0.}); }
  void phase(std::size_t q, double theta) {
    finite(theta); single({'P', q, 0, theta});
  }
  void cnot(std::size_t c, std::size_t t) { two({'C', c, t, 0.}); }
  void controlled_z(std::size_t c, std::size_t t) { two({'D', c, t, pi()}); }
  void controlled_phase(std::size_t c, std::size_t t, double theta) {
    finite(theta); two({'Q', c, t, theta});
  }

  double norm_squared() const {
    ready();
    if (reduced_) return reduced_->coefficient_norm_squared();
    double norm = 1.;
    for (const auto& p : product_) norm *= std::norm(p[0]) + std::norm(p[1]);
    return norm;
  }

  double pauli_expectation(char axis, std::size_t q) const {
    check(q);
    if (reduced_) return reduced_->pauli_expectation(axis, q);
    const auto& p = product_[q];
    const double norm = std::norm(p[0]) + std::norm(p[1]);
    if (!(norm > 0.)) throw std::runtime_error("zero state norm");
    if (axis == 'X') return 2. * (std::conj(p[0])*p[1]).real() / norm;
    if (axis == 'Y') return 2. * (std::conj(p[0])*p[1]).imag() / norm;
    if (axis == 'Z') return (std::norm(p[0])-std::norm(p[1])) / norm;
    throw std::invalid_argument("axis must be X, Y or Z");
  }

  // Full amplitudes are a small-circuit validation interface, not the large-N
  // output route. It has the same 23-qubit guard as the existing reduced core.
  std::vector<Complex> dense_state() const {
    ready();
    if (n_ >= 24) throw std::runtime_error("dense validation limit is 23 qubits");
    if (reduced_) return reduced_->dense_from_reduced();
    std::vector<Complex> out(std::size_t{1} << n_, {1., 0.});
    for (std::size_t i = 0; i < out.size(); ++i)
      for (std::size_t q = 0; q < n_; ++q) out[i] *= product_[q][(i >> q)&1U];
    return out;
  }

 private:
  struct Gate { char op; std::size_t a, b; double theta; };
  static double pi() { return std::acos(-1.); }
  static Complex rotation(double theta) { return {std::cos(theta), std::sin(theta)}; }
  static void finite(double theta) {
    if (!std::isfinite(theta)) throw std::invalid_argument("non-finite phase");
  }
  void check(std::size_t q) const {
    ready();
    if (q >= n_) throw std::out_of_range("qubit");
  }
  void ready() const {
    if (failed_) throw std::logic_error("simulation failed; construct a fresh state");
  }
  static void apply_reduced(ReducedExactTRepresentation& s, const Gate& g) {
    switch (g.op) {
      case 'H': s.hadamard(g.a); break;
      case 'X': s.pauli_x(g.a); break;
      case 'Z': s.pauli_z(g.a); break;
      case 'S': s.phase_s(g.a); break;
      case 'T': s.phase_t(g.a); break;
      case 'P': s.phase(g.a, g.theta); break;
      case 'C': s.cnot(g.a, g.b); break;
      case 'D': s.controlled_z(g.a, g.b); break;
      case 'Q': s.controlled_phase(g.a, g.b, g.theta); break;
      default: throw std::logic_error("unknown gate");
    }
  }
  void check_components(const ReducedExactTRepresentation& s) {
    peak_components_ = std::max(peak_components_, s.component_count());
    if (s.component_count() > max_components_) {
      failed_ = true;
      throw std::length_error("reduced component budget exceeded; no result available");
    }
  }
  void fallback(const Gate& current) {
    std::unique_ptr<ReducedExactTRepresentation> candidate;
    if (transition_ == Transition::Transfer) {
      candidate = std::make_unique<ReducedExactTRepresentation>(
          ReducedExactTRepresentation::from_product(product_, max_components_));
      check_components(*candidate);
      apply_reduced(*candidate, current);
      check_components(*candidate);
      transferred_qubits_ = n_;
    } else {
      candidate = std::make_unique<ReducedExactTRepresentation>(n_);
      for (const auto& g : history_) {
        apply_reduced(*candidate, g);
        check_components(*candidate);
      }
      replayed_gates_ = history_.size();
      residual_sum_ = 0.;
    }
    reduced_ = std::move(candidate);
    std::vector<Pair>().swap(product_);
    std::vector<Gate>().swap(history_);
  }
  void single(const Gate& g) {
    check(g.a);
    if (reduced_) {
      apply_reduced(*reduced_, g); check_components(*reduced_); ++gate_count_; return;
    }
    if (transition_ == Transition::Replay) history_.push_back(g);
    auto& p = product_[g.a];
    switch (g.op) {
      case 'H': {
        const auto a = p[0], b = p[1];
        const double s = 1./std::sqrt(2.);
        p = {(a+b)*s, (a-b)*s}; break;
      }
      case 'X': std::swap(p[0], p[1]); break;
      case 'Z': p[1] = -p[1]; break;
      case 'S': p[1] *= Complex{0., 1.}; break;
      case 'T': p[1] *= rotation(pi()/4.); break;
      case 'P': p[1] *= rotation(g.theta); break;
      default: throw std::logic_error("unknown single-qubit gate");
    }
    ++gate_count_;
  }
  void two(const Gate& g) {
    check(g.a); check(g.b);
    if (g.a == g.b) throw std::invalid_argument("same control and target");
    if (reduced_) {
      apply_reduced(*reduced_, g); check_components(*reduced_); ++gate_count_; return;
    }
    if (transition_ == Transition::Replay) history_.push_back(g);
    const auto& a = product_[g.a];
    const auto& b = product_[g.b];
    std::array<Complex, 4> v{a[0]*b[0], a[0]*b[1], a[1]*b[0], a[1]*b[1]};
    if (g.op == 'C') std::swap(v[2], v[3]);
    else v[3] *= (g.op == 'D' ? Complex{-1., 0.} : rotation(g.theta));

    const double row0 = std::norm(v[0]) + std::norm(v[1]);
    const double row1 = std::norm(v[2]) + std::norm(v[3]);
    const std::size_t pivot = row0 >= row1 ? 0 : 2;
    const double length = std::sqrt(std::max(row0, row1));
    if (!(length > 0.)) throw std::runtime_error("zero state norm");
    Pair target{v[pivot]/length, v[pivot+1]/length};
    Pair control{std::conj(target[0])*v[0] + std::conj(target[1])*v[1],
                 std::conj(target[0])*v[2] + std::conj(target[1])*v[3]};
    double error2 = 0.;
    for (std::size_t i = 0; i < 2; ++i)
      for (std::size_t j = 0; j < 2; ++j)
        error2 += std::norm(v[2*i+j]-control[i]*target[j]);
    const double relative_error = std::sqrt(error2/(row0+row1));
    if (relative_error <= tolerance_) {
      product_[g.a] = control; product_[g.b] = target;
      residual_sum_ += relative_error;
    } else {
      try { fallback(g); }
      catch (...) { failed_ = true; throw; }
    }
    ++gate_count_;
  }

  std::size_t n_;
  double tolerance_, residual_sum_ = 0.;
  bool failed_ = false;
  std::size_t max_components_, gate_count_ = 0, peak_components_ = 1;
  Transition transition_;
  std::size_t replayed_gates_ = 0, transferred_qubits_ = 0;
  std::vector<Pair> product_;
  std::vector<Gate> history_;
  std::unique_ptr<ReducedExactTRepresentation> reduced_;
};

}  // namespace mundo_x
