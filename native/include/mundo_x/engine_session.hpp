#pragma once

#include "mundo_x/dense_core.hpp"
#include "mundo_x/graphics_core_bridge.h"
#include "mundo_x/pauli_observer.hpp"
#include <complex>
#include <memory>
#include <mutex>

namespace mundo_x {

// State ownership for the existing nucleus + graphics integration. Observers
// borrow this session and call query(); they do not initialize, replace or
// measure its core. No external simulator or dense fallback is selected.
class EngineSession {
 public:
  enum class Backend : int {
    Compact = MX_BACKEND_COMPACT,
    DenseReference = MX_BACKEND_DENSE_REFERENCE,
    ReducedT = MX_BACKEND_REDUCED_T,
    Factorized = MX_BACKEND_FACTORIZED
  };

  struct Gate {
    std::uint64_t operation = 0, first = 0, second = 0;
    bool operator==(const Gate& other) const {
      return operation == other.operation && first == other.first &&
             second == other.second;
    }
  };

  // Portable logical snapshot. Every gate is replayed from |0...0>. Renderer
  // observables belong to the project, so importing a mutated core must not
  // redefine them from the core's new stabilizer generators.
  struct Snapshot {
    std::uint32_t version = 1;
    Backend backend = Backend::Compact;
    std::size_t qubits = 0;
    std::uint64_t revision = 0;
    std::vector<Gate> gates;
  };

  explicit EngineSession(Backend backend, std::size_t qubits)
      : backend_(backend), n_(qubits) {
    switch (backend_) {
      case Backend::Compact: compact_ = std::make_unique<CompactCore>(n_); break;
      case Backend::DenseReference: dense_ = std::make_unique<DenseCore64>(n_); break;
      case Backend::ReducedT:
      case Backend::Factorized:
        throw std::invalid_argument("This core is not connected to the graphics session ABI");
      default: throw std::invalid_argument("Unknown backend");
    }
  }

  EngineSession(const EngineSession&) = delete;
  EngineSession& operator=(const EngineSession&) = delete;

  static const char* backend_name(Backend backend) {
    switch (backend) {
      case Backend::Compact: return "compact-clifford";
      case Backend::DenseReference: return "dense-reference";
      case Backend::ReducedT: return "reduced-exact-t";
      case Backend::Factorized: return "factorized";
      default: throw std::invalid_argument("Unknown backend");
    }
  }

  static std::uint64_t capabilities(Backend backend) {
    (void)backend_name(backend); // Reject unrecognized ids rather than inventing one.
    const std::uint64_t shared = MX_CAP_SESSION | MX_CAP_READ_ONLY_PAULI |
                                 MX_CAP_GATE_HISTORY;
    if (backend == Backend::Compact) return shared | MX_CAP_STABILIZER_GENERATORS;
    if (backend == Backend::DenseReference) return shared | MX_CAP_DENSE_REFERENCE;
    return 0; // Other original cores are explicit; this ABI never substitutes them.
  }

  static std::uint64_t gate_mask(Backend backend) {
    return capabilities(backend) & MX_CAP_SESSION ? 0x7fULL : 0;
  }

  Backend backend() const { return backend_; }
  std::size_t qubits() const { return n_; }

  static std::vector<Gate> gates_from_triples(const std::uint64_t* triples,
                                             std::size_t count) {
    if (count && !triples) throw std::invalid_argument("Null gate batch");
    if (count > std::numeric_limits<std::size_t>::max() / 3)
      throw std::length_error("Gate batch dimensions overflow");
    std::vector<Gate> gates;
    if (count > gates.max_size()) throw std::length_error("Gate history too large");
    gates.reserve(count);
    for (std::size_t i = 0; i < count; ++i)
      gates.push_back({triples[3*i], triples[3*i+1], triples[3*i+2]});
    return gates;
  }

  // Entire input and all possible history allocation are handled before the
  // first mutation. The validated Compact/Dense Clifford primitives perform no
  // allocations and cannot fail partway through a batch. Invalid calls leave
  // core, history, revision, epoch and the reusable observer intact.
  void apply(const std::vector<Gate>& gates) {
    std::lock_guard<std::mutex> guard(mutex_);
    validate(gates);
    if (gates.empty()) return;
    require_incrementable(revision_, "Revision overflow");
    require_incrementable(epoch_, "Observation epoch overflow");
    reserve_history(gates.size());
    for (auto operation : gates) {
      if (operation.operation != MX_GATE_CX && operation.operation != MX_GATE_CZ)
        operation.second = 0;
      if (compact_) apply_gate(*compact_, operation);
      else apply_gate(*dense_, operation);
      history_.push_back(operation); // Reserved, trivially copyable, no allocation.
    }
    observer_.reset();
    ++revision_;
    ++epoch_;
  }

  std::vector<double> query(const std::vector<std::string>& strings) const {
    std::lock_guard<std::mutex> guard(mutex_);
    std::vector<mundo_x_bridge::Pauli> paulis;
    paulis.reserve(strings.size());
    for (const auto& text : strings)
      paulis.push_back(mundo_x_bridge::Pauli::parse(text, n_));
    std::vector<double> values(strings.size());
    if (paulis.empty()) return values;
    std::unique_ptr<mundo_x_bridge::PauliObserver> candidate;
    if (compact_ && !observer_) {
      require_incrementable(rebuilds_, "Observer rebuild counter overflow");
      candidate = std::make_unique<mundo_x_bridge::PauliObserver>(*compact_);
    }
    const auto* observer = candidate ? candidate.get() : observer_.get();
    for (std::size_t i = 0; i < paulis.size(); ++i)
      values[i] = compact_ ? observer->expectation(std::move(paulis[i]))
                           : dense_expectation(*dense_, paulis[i]);
    if (candidate) {
      observer_ = std::move(candidate);
      ++rebuilds_;
    }
    return values;
  }

  std::string generator(std::size_t row) const {
    std::lock_guard<std::mutex> guard(mutex_);
    if (!compact_) throw std::invalid_argument("Generator requires CompactCore");
    return compact_->generator_string(row);
  }

  Snapshot snapshot() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return {1, backend_, n_, revision_, history_};
  }

  // An in-place import is an explicit writer operation. Reconstruction happens
  // in isolation and commits once; no partial replay can affect a renderer.
  void restore(const Snapshot& saved) {
    if (saved.version != 1) throw std::invalid_argument("Unsupported core snapshot version");
    if (saved.backend != backend_ || saved.qubits != n_)
      throw std::invalid_argument("Snapshot backend or qubit count differs from session");
    EngineSession candidate(backend_, n_);
    candidate.apply(saved.gates);
    std::lock_guard<std::mutex> guard(mutex_);
    require_incrementable(epoch_, "Observation epoch overflow");
    compact_.swap(candidate.compact_);
    dense_.swap(candidate.dense_);
    history_.swap(candidate.history_);
    revision_ = saved.revision;
    observer_.reset();
    ++epoch_;
  }

  std::uint64_t revision() const {
    std::lock_guard<std::mutex> guard(mutex_); return revision_;
  }
  std::uint64_t epoch() const {
    std::lock_guard<std::mutex> guard(mutex_); return epoch_;
  }
  std::uint64_t rebuilds() const {
    std::lock_guard<std::mutex> guard(mutex_); return rebuilds_;
  }
  std::size_t state_bytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return compact_ ? compact_->state_bytes() : dense_->state_bytes();
  }
  std::size_t observer_bytes() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return observer_ ? observer_->payload_bytes() : 0;
  }
  std::size_t history_count() const {
    std::lock_guard<std::mutex> guard(mutex_); return history_.size();
  }
  std::size_t history_bytes() const {
    std::lock_guard<std::mutex> guard(mutex_); return history_.size() * sizeof(Gate);
  }

 private:
  static void require_incrementable(std::uint64_t value, const char* message) {
    if (value == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error(message);
  }

  void validate(const std::vector<Gate>& gates) const {
    for (const auto& gate : gates) {
      const bool pair = gate.operation == MX_GATE_CX || gate.operation == MX_GATE_CZ;
      if (gate.operation > MX_GATE_Y || gate.first >= n_ ||
          (pair && (gate.second >= n_ || gate.first == gate.second)))
        throw std::invalid_argument("Invalid/unsupported gate in batch");
    }
  }

  void reserve_history(std::size_t extra) {
    if (extra > history_.max_size() - history_.size())
      throw std::length_error("Gate history exceeds addressable capacity");
    const auto required = history_.size() + extra;
    if (required <= history_.capacity()) return;
    const auto capacity = history_.capacity();
    const auto growth = std::min(history_.max_size() - capacity,
                                 std::max<std::size_t>(capacity / 2, 16));
    history_.reserve(std::max(required, capacity + growth));
  }

  template<class Core>
  static void apply_gate(Core& core, const Gate& gate) {
    const auto a = static_cast<std::size_t>(gate.first);
    const auto b = static_cast<std::size_t>(gate.second);
    switch (gate.operation) {
      case MX_GATE_H: core.hadamard(a); break;
      case MX_GATE_S: core.phase_s(a); break;
      case MX_GATE_X: core.pauli_x(a); break;
      case MX_GATE_Z: core.pauli_z(a); break;
      case MX_GATE_CX: core.cnot(a,b); break;
      case MX_GATE_CZ: core.controlled_z(a,b); break;
      // Y = i X Z. The omitted global phase has no observable effect.
      case MX_GATE_Y: core.pauli_z(a); core.pauli_x(a); break;
      default: throw std::logic_error("Unvalidated gate");
    }
  }

  static double dense_expectation(const DenseCore64& core,
                                   const mundo_x_bridge::Pauli& p) {
    const auto& state = core.state();
    const auto xm = p.x[0], zm = p.z[0];
    const std::complex<double> phases[] = {{1,0},{0,1},{-1,0},{0,-1}};
    const auto factor = phases[p.phase];
    std::complex<double> sum{0,0};
    for (std::size_t i = 0; i < state.size(); ++i) {
      const double sign = mundo_x_bridge::parity64(i & zm) ? -1.0 : 1.0;
      sum += std::conj(state[i ^ xm]) * (factor * sign) * state[i];
    }
    if (std::abs(sum.imag()) > 1e-8) throw std::logic_error("Imaginary expectation");
    return sum.real();
  }

  const Backend backend_;
  const std::size_t n_;
  mutable std::mutex mutex_;
  std::uint64_t revision_ = 0, epoch_ = 0;
  mutable std::uint64_t rebuilds_ = 0;
  std::unique_ptr<CompactCore> compact_;
  std::unique_ptr<DenseCore64> dense_;
  std::vector<Gate> history_;
  mutable std::unique_ptr<mundo_x_bridge::PauliObserver> observer_;
};

} // namespace mundo_x
