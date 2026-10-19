#pragma once

#include "mundo_x/infinite_program.hpp"

#include <mutex>
#include <type_traits>

namespace mundo_x {

// A single persistent owner shared by hosts, emulated guest devices and
// renderers. Observations return values and version metadata under one lock.
// No query or snapshot measures or materializes the rule-defined background.
class InfiniteSession {
 public:
  using Background = InfiniteCore::Background;
  using Limits = InfiniteCore::Limits;
  using Gate = InfiniteCore::Gate;
  using Term = InfiniteCore::Term;
  using Stats = InfiniteCore::Stats;
  using Observable = InfiniteObservable;
  struct Observation {
    std::vector<double> values;
    Stats stats;
    std::uint64_t revision = 0, epoch = 0;
  };
  struct Metadata { Stats stats; Background background; std::uint64_t epoch; };

  explicit InfiniteSession(Background background = Background::Zero)
      : InfiniteSession(background, Limits{}) {}
  InfiniteSession(Background background, const Limits& limits)
      : limits_(limits), core_(background, limits) {}
  InfiniteSession(const InfiniteSession&) = delete;
  InfiniteSession& operator=(const InfiniteSession&) = delete;

  const Limits& limits() const { return limits_; }
  Background background() const {
    std::lock_guard<std::mutex> lock(mutex_); return core_.background();
  }
  Stats stats() const {
    std::lock_guard<std::mutex> lock(mutex_); return core_.stats();
  }
  std::uint64_t epoch() const {
    std::lock_guard<std::mutex> lock(mutex_); return epoch_;
  }
  Metadata metadata() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {core_.stats(), core_.background(), epoch_};
  }

  void apply(const std::vector<Gate>& gates) {
    validate_gate_transport(gates);
    std::lock_guard<std::mutex> lock(mutex_);
    if (gates.empty()) return;
    require_epoch();
    core_.apply(gates);
    ++epoch_;
  }

  Observation query_batch(const std::vector<Observable>& observables) const {
    validate_query_transport(observables);
    std::lock_guard<std::mutex> lock(mutex_);
    Observation result;
    result.values.reserve(observables.size());
    InfiniteCore::ObserverCache observers;
    for (const auto& observable : observables)
      result.values.push_back(
          core_.expectation_cached(observable.terms, observable.sign, observers));
    result.stats = core_.stats();
    result.revision = result.stats.revision;
    result.epoch = epoch_;
    return result;
  }

  double probability_one(const std::string& address) const {
    std::lock_guard<std::mutex> lock(mutex_); return core_.probability_one(address);
  }

  CompactCore::ZMeasurement measure_z(const std::string& address) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto candidate = core_;
    const auto result = candidate.measure_z(address);
    commit_if_changed(std::move(candidate), false);
    return result;
  }

  double force_measure_z(const std::string& address, bool outcome_one) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto candidate = core_;
    const auto result = candidate.force_measure_z(address, outcome_one);
    commit_if_changed(std::move(candidate), false);
    return result;
  }

  void seed(std::uint64_t value) {
    std::lock_guard<std::mutex> lock(mutex_);
    require_epoch();
    core_.seed(value);
    ++epoch_; // Quantum revision is unchanged; persisted RNG did change.
  }

  std::size_t compact_background() {
    std::lock_guard<std::mutex> lock(mutex_);
    auto candidate = core_;
    const auto count = candidate.compact_background();
    commit_if_changed(std::move(candidate), false);
    return count;
  }

  void reset(Background background) {
    InfiniteCore candidate(background, limits_);
    std::lock_guard<std::mutex> lock(mutex_);
    require_epoch();
    core_ = std::move(candidate);
    ++epoch_;
  }

  std::string snapshot() const {
    std::lock_guard<std::mutex> lock(mutex_); return core_.snapshot();
  }

  void restore(const std::string& saved) {
    auto candidate = InfiniteCore::from_snapshot(saved, limits_);
    std::lock_guard<std::mutex> lock(mutex_);
    require_epoch();
    core_ = std::move(candidate);
    ++epoch_; // Always invalidates observers, even for an equal revision.
  }

  std::string execute(const std::string& program) {
    return execute_prepared(program, [](const std::string& output) { return output; });
  }

  // The C transport allocates its owned response inside this transaction.
  // If response allocation fails, state/RNG/epoch stay unchanged as well.
  // prepare must not reenter this session, and its returned type must move
  // without throwing. It should only prepare an owned response, not publish it.
  template<class Prepare>
  auto execute_prepared(const std::string& program, Prepare prepare,
                        bool require_matching_background = false)
      -> typename std::invoke_result<Prepare, const std::string&>::type {
    using Output = typename std::invoke_result<Prepare, const std::string&>::type;
    static_assert(std::is_nothrow_move_constructible<Output>::value,
                  "Prepared output must have a nonthrowing move constructor");
    std::lock_guard<std::mutex> lock(mutex_);
    auto candidate = core_;
    auto result = infinite_program_detail::run(candidate, program, require_matching_background);
    const bool changed = result.explicit_mutation ||
                         candidate.stats().revision != core_.stats().revision;
    if (changed) require_epoch();
    auto output = prepare(result.output); // Last potentially failing step.
    if (changed) { core_ = std::move(candidate); ++epoch_; }
    return output;
  }

 private:
  static void add_transport_bytes(std::size_t& total, std::size_t extra) {
    if (extra > infinite_program_detail::max_batch_address_bytes - total)
      throw std::length_error("request address text exceeds 16 MiB");
    total += extra;
  }

  static void validate_gate_transport(const std::vector<Gate>& gates) {
    if (gates.size() > infinite_program_detail::max_batch_gates)
      throw std::length_error("batch exceeds 65536 gates");
    std::size_t bytes = 0;
    for (const auto& gate : gates) {
      add_transport_bytes(bytes, gate.first.size());
      add_transport_bytes(bytes, gate.second.size());
    }
  }

  static void validate_query_transport(const std::vector<Observable>& observables) {
    if (observables.size() > infinite_program_detail::max_observables)
      throw std::length_error("batch exceeds 4096 observables");
    std::size_t terms = 0, bytes = 0;
    for (const auto& observable : observables) {
      if (observable.terms.size() > infinite_program_detail::max_batch_query_terms - terms)
        throw std::length_error("batch exceeds 65536 Pauli terms");
      terms += observable.terms.size();
      for (const auto& term : observable.terms) add_transport_bytes(bytes, term.address.size());
    }
  }

  void require_epoch() const {
    if (epoch_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("infinite-session observation epoch overflow");
  }
  void commit_if_changed(InfiniteCore candidate, bool explicit_mutation) {
    if (!explicit_mutation && candidate.stats().revision == core_.stats().revision) return;
    require_epoch();
    core_ = std::move(candidate);
    ++epoch_;
  }

  const Limits limits_;
  mutable std::mutex mutex_;
  InfiniteCore core_;
  std::uint64_t epoch_ = 0;
};

}  // namespace mundo_x
