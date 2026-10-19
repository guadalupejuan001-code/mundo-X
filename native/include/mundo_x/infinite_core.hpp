#pragma once

#include "mundo_x/infinite_address.hpp"
#include "mundo_x/pauli_observer.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// A rule-defined register with finite Clifford modifications. Addresses are
// decimal strings, not a fixed-width integer domain. Untouched sites follow
// |0> or independent Bell pairs (2k,2k+1), and finite Pauli queries on that
// background are evaluated analytically without allocating those sites.
//
// Each materialized component owns the user's CompactCore. Tensor products
// join current states directly; no growing gate history, statevector, external
// simulator, branch pruning, or numerical tolerance is used. "Exact" refers
// to signed stabilizer algebra, which represents a pure state up to global
// phase. This is not a backend for arbitrary non-Clifford or infinite-support
// operations, and it does not claim infinite physical execution resources.
class InfiniteCore {
 public:
  enum class Background { Zero, BellPairs };

  struct Limits {
    std::size_t max_active_qubits = 4096;
    std::size_t max_component_qubits = 1024;
    // Sum of committed CompactCore::state_bytes() payloads. Not process RAM:
    // indices, strings, allocator/capacity overhead, observer scratch, and
    // transactional copies and tensor temporaries are excluded.
    std::size_t max_state_bytes = std::size_t{64} * 1024 * 1024;
    std::size_t max_address_digits = 4096;
    std::size_t max_query_terms = 4096;
  };

  // C=CX, D=CZ. H/S/X/Y/Z are single-qubit operations with an empty second
  // address. T and arbitrary phase operations are deliberately unsupported.
  struct Gate { char op; std::string first, second; };
  struct Term { std::string address; char axis; };
  struct Stats {
    std::size_t active_qubits = 0;
    std::size_t components = 0;
    std::size_t largest_component = 0;
    std::size_t tableau_bytes = 0;
    // Logical canonical address text counted once per active id, excluding
    // the separate copies held by component/index containers and their RAM.
    std::size_t address_bytes = 0;
    std::uint64_t revision = 0;

    bool operator==(const Stats& other) const {
      return active_qubits == other.active_qubits &&
             components == other.components &&
             largest_component == other.largest_component &&
             tableau_bytes == other.tableau_bytes &&
             address_bytes == other.address_bytes && revision == other.revision;
    }
    bool operator!=(const Stats& other) const { return !(*this == other); }
  };

  explicit InfiniteCore(Background background = Background::Zero)
      : InfiniteCore(background, Limits{}) {}

  InfiniteCore(Background background, const Limits& limits)
      : background_(background), limits_(limits) {
    if (background != Background::Zero && background != Background::BellPairs)
      throw std::invalid_argument("unknown infinite-register background");
    if (limits.max_address_digits == 0)
      throw std::invalid_argument("address digit budget must be positive");
    // Zero materialization/query budgets are allowed: the background still
    // has a definition, and supported fixed-size queries can still read it.
    state_ = std::make_shared<State>();
  }

  // Copies share immutable committed states. Writers detach their metadata
  // and only the components they mutate; independent copies retain their RNG.
  InfiniteCore(const InfiniteCore&) = default;
  InfiniteCore& operator=(const InfiniteCore&) = default;
  InfiniteCore(InfiniteCore&&) noexcept = default;
  InfiniteCore& operator=(InfiniteCore&&) noexcept = default;

  Background background() const { return background_; }
  const Limits& limits() const { return limits_; }

  Stats stats() const {
    Stats out;
    out.active_qubits = state_->locations.size();
    out.components = state_->components.size();
    for (const auto& component : state_->components)
      out.largest_component = std::max(out.largest_component, component->ids.size());
    out.tableau_bytes = state_->tableau_bytes;
    out.address_bytes = state_->address_bytes;
    out.revision = state_->revision;
    return out;
  }

  // A whole batch commits once. Any bad syntax, unsupported operation,
  // impossible allocation, or exceeded budget discards all candidate changes.
  // Successful batches do not retain the input or a replay history.
  void apply(const std::vector<Gate>& gates) {
    if (gates.empty()) return;
    const auto normalized = normalize_gates(gates);
    require_next_revision();
    auto candidate = std::make_shared<State>(*state_);
    for (const auto& gate : normalized) apply_gate(*candidate, gate);
    ++candidate->revision;
    state_.swap(candidate);
  }

  // Finite Hermitian Pauli products only. Duplicate canonical addresses are
  // rejected rather than silently interpreting noncommuting X/Y products.
  // All input is validated before an early zero result is returned. Queries
  // create only temporary observer data and never materialize the background.
  double expectation(const std::vector<Term>& terms, int sign = 1) const {
    if (sign != 1 && sign != -1)
      throw std::invalid_argument("Pauli sign must be +1 or -1");
    if (terms.size() > limits_.max_query_terms)
      throw std::length_error("Pauli query term budget exceeded");
    TermMap normalized;
    for (const auto& term : terms) {
      if (term.axis != 'I' && term.axis != 'X' && term.axis != 'Y' && term.axis != 'Z')
        throw std::invalid_argument("Pauli axis must be I, X, Y or Z");
      auto address = canonical(term.address);
      if (!normalized.emplace(std::move(address), term.axis).second)
        throw std::invalid_argument("duplicate canonical address in Pauli query");
    }

    std::map<std::size_t, mundo_x_bridge::Pauli> active;
    double answer = static_cast<double>(sign);
    for (const auto& term : normalized) {
      const auto& address = term.first;
      const char axis = term.second;
      if (axis == 'I') continue;
      const auto found = state_->locations.find(address);
      if (found != state_->locations.end()) {
        const auto location = found->second;
        auto query = active.find(location.component);
        if (query == active.end())
          query = active.emplace(location.component,
              identity_pauli(state_->components[location.component]->ids.size())).first;
        set_axis(query->second, location.qubit, axis);
      } else if (background_ == Background::Zero) {
        if (axis != 'Z') return 0.0;
      } else {
        const auto partner = infinite_detail::partner_address(address);
        const auto other = normalized.find(partner);
        // A Bell pair has zero single-site X/Y/Z expectations. Its only
        // nonidentity Pauli correlations are XX=+1, YY=-1 and ZZ=+1.
        if (other == normalized.end() || other->second != axis) return 0.0;
        if (!infinite_detail::is_odd(address) && axis == 'Y') answer = -answer;
      }
    }
    for (auto& query : active) {
      const mundo_x_bridge::PauliObserver observer(state_->components[query.first]->core);
      answer *= observer.expectation(std::move(query.second));
      if (answer == 0.0) return 0.0;
    }
    return answer;
  }

  // Fixed-size Z queries do not need a sparse-term allocation or the general
  // max_query_terms budget. The address budget still applies.
  double probability_one(const std::string& address) const {
    return probability_one(*state_, canonical(address));
  }

  CompactCore::ZMeasurement measure_z(const std::string& address) {
    const auto id = canonical(address);
    const double probability = probability_one(*state_, id);
    if (probability != 0.5)
      return {probability == 1.0, 1.0, false};
    require_next_revision();
    auto candidate = std::make_shared<State>(*state_); // Includes an RNG copy.
    const auto location = materialize(*candidate, id);
    auto& component = mutable_component(*candidate, location.component);
    const auto outcome = component.core.measure_z(location.qubit, candidate->rng);
    ++candidate->revision;
    state_.swap(candidate); // RNG is consumed only by a committed projection.
    return outcome;
  }

  // Return the requested branch's probability before projection. An impossible
  // branch throws, deterministic possible branches are no-ops, and no forced
  // measurement consumes RNG output.
  double force_measure_z(const std::string& address, bool outcome_one) {
    const auto id = canonical(address);
    const double one = probability_one(*state_, id);
    const double probability = outcome_one ? one : 1.0 - one;
    if (probability == 0.0)
      throw std::invalid_argument("requested Z measurement outcome is impossible");
    if (probability == 1.0) return 1.0;
    require_next_revision();
    auto candidate = std::make_shared<State>(*state_);
    const auto location = materialize(*candidate, id);
    mutable_component(*candidate, location.component).core.measure_z_forced(
        location.qubit, outcome_one);
    ++candidate->revision;
    state_.swap(candidate);
    return probability;
  }

  void seed(std::uint64_t value) {
    auto candidate = std::make_shared<State>(*state_);
    candidate->rng.seed(value);
    state_.swap(candidate); // Seeding is not a quantum-state revision.
  }

  // Release complete components only when their state equals the selected
  // background exactly (all independent background generators have +1
  // expectation). No approximate split or reset is performed. Return the
  // number of released qubits; physical observables and RNG remain unchanged.
  std::size_t compact_background() {
    std::vector<bool> release(state_->components.size(), false);
    std::size_t released = 0;
    for (std::size_t i = 0; i < state_->components.size(); ++i) {
      if (matches_background(*state_, i)) {
        release[i] = true;
        released += state_->components[i]->ids.size();
      }
    }
    if (released == 0) return 0;
    require_next_revision();
    auto candidate = std::make_shared<State>(*state_);
    for (std::size_t i = release.size(); i-- > 0;) {
      if (!release[i]) continue;
      const auto& component = candidate->components[i];
      candidate->tableau_bytes -= component->core.state_bytes();
      for (const auto& id : component->ids) {
        candidate->address_bytes -= id.size();
        candidate->locations.erase(id);
      }
      erase_slot(*candidate, i);
    }
    ++candidate->revision;
    state_.swap(candidate);
    return released;
  }

 private:
  struct Location { std::size_t component, qubit; };
  struct Component {
    std::vector<std::string> ids;
    CompactCore core;
    Component(std::vector<std::string> addresses, CompactCore state)
        : ids(std::move(addresses)), core(std::move(state)) {}
  };
  using LocationMap = std::map<std::string, Location, infinite_detail::AddressLess>;
  using TermMap = std::map<std::string, char, infinite_detail::AddressLess>;
  struct State {
    std::vector<std::shared_ptr<Component>> components;
    LocationMap locations;
    std::size_t tableau_bytes = 0, address_bytes = 0;
    std::uint64_t revision = 0;
    std::mt19937_64 rng{42};
  };

  static std::size_t checked_add(std::size_t a, std::size_t b, const char* message) {
    if (b > std::numeric_limits<std::size_t>::max() - a)
      throw std::length_error(message);
    return a + b;
  }

  // Mirrors CompactCore's packed logical payload formula, without allocating
  // any tableau. Arithmetic and budgets are checked before materialization or
  // tensor construction; the core also applies its own vector-capacity guard.
  static std::size_t tableau_bytes(std::size_t qubits) {
    if (qubits == 0) return 0;
    const auto words = qubits / 64 + (qubits % 64 != 0);
    const auto maximum = std::numeric_limits<std::size_t>::max();
    const auto entries = (maximum - qubits) / (2 * sizeof(std::uint64_t));
    if (qubits > entries / words)
      throw std::length_error("tableau dimensions exceed addressable storage");
    return qubits * words * (2 * sizeof(std::uint64_t)) + qubits;
  }

  void check_resources(std::size_t active, std::size_t component,
                       std::size_t bytes) const {
    if (active > limits_.max_active_qubits)
      throw std::length_error("active qubit materialization budget exceeded");
    if (component > limits_.max_component_qubits)
      throw std::length_error("component qubit budget exceeded");
    if (bytes > limits_.max_state_bytes)
      throw std::length_error("tableau payload byte budget exceeded");
  }

  void require_next_revision() const {
    if (state_->revision == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("infinite-register revision overflow");
  }

  std::string canonical(const std::string& address) const {
    return infinite_detail::canonical_address(address, limits_.max_address_digits);
  }

  std::vector<Gate> normalize_gates(const std::vector<Gate>& gates) const {
    std::vector<Gate> normalized;
    normalized.reserve(gates.size());
    for (const auto& gate : gates) {
      const bool pair = gate.op == 'C' || gate.op == 'D';
      if (!pair && gate.op != 'H' && gate.op != 'S' && gate.op != 'X' &&
          gate.op != 'Y' && gate.op != 'Z')
        throw std::invalid_argument("unsupported gate: only H/S/X/Y/Z/CX/CZ");
      auto first = canonical(gate.first);
      std::string second;
      if (pair) {
        second = canonical(gate.second);
        if (first == second)
          throw std::invalid_argument("two-qubit gate requires distinct addresses");
      } else if (!gate.second.empty()) {
        throw std::invalid_argument("single-qubit gate has a second address");
      }
      normalized.push_back({gate.op, std::move(first), std::move(second)});
    }
    return normalized;
  }

  static mundo_x_bridge::Pauli identity_pauli(std::size_t qubits) {
    mundo_x_bridge::Pauli out;
    const auto words = qubits / 64 + (qubits % 64 != 0);
    out.x.assign(words, 0);
    out.z.assign(words, 0);
    return out;
  }

  static void set_axis(mundo_x_bridge::Pauli& p, std::size_t q, char axis) {
    const auto word = q / 64;
    const auto bit = std::uint64_t{1} << (q % 64);
    if (axis == 'X' || axis == 'Y') p.x[word] |= bit;
    if (axis == 'Z' || axis == 'Y') p.z[word] |= bit;
    if (axis == 'Y') p.phase = (p.phase + 1U) & 3U;
  }

  double probability_one(const State& state, const std::string& id) const {
    const auto found = state.locations.find(id);
    if (found == state.locations.end())
      return background_ == Background::Zero ? 0.0 : 0.5;
    const auto location = found->second;
    return state.components[location.component]->core.probability_one_z(location.qubit);
  }

  Location materialize(State& state, const std::string& id) const {
    const auto existing = state.locations.find(id);
    if (existing != state.locations.end()) return existing->second;
    const std::size_t count = background_ == Background::Zero ? 1 : 2;
    const auto projected_active = checked_add(state.locations.size(), count,
                                              "active qubit count overflows");
    const auto projected_bytes = checked_add(state.tableau_bytes, tableau_bytes(count),
                                             "tableau payload byte count overflows");
    check_resources(projected_active, count, projected_bytes);

    std::vector<std::string> ids;
    if (count == 1) ids.push_back(id);
    else {
      auto partner = infinite_detail::partner_address(id);
      if (state.locations.find(partner) != state.locations.end())
        throw std::logic_error("partially materialized Bell background pair");
      if (infinite_detail::is_odd(id)) ids = {std::move(partner), id};
      else ids = {id, std::move(partner)};
    }
    auto projected_addresses = state.address_bytes;
    for (const auto& address : ids)
      projected_addresses = checked_add(projected_addresses, address.size(),
                                         "address payload byte count overflows");
    CompactCore core(count);
    if (count == 2) { core.hadamard(0); core.cnot(0, 1); }
    auto component = std::make_shared<Component>(std::move(ids), std::move(core));
    const auto slot = state.components.size();
    for (std::size_t q = 0; q < component->ids.size(); ++q)
      state.locations.emplace(component->ids[q], Location{slot, q});
    state.components.push_back(std::move(component));
    state.tableau_bytes = projected_bytes;
    state.address_bytes = projected_addresses;
    return state.locations.at(id);
  }

  static Component& mutable_component(State& state, std::size_t slot) {
    auto& component = state.components[slot];
    if (!component.unique()) component = std::make_shared<Component>(*component);
    return *component;
  }

  // Slot ids are finite storage indices, unrelated to decimal qubit addresses.
  // Reuse the last slot on removal so an operation history cannot accumulate
  // holes, tombstones, or ever-growing ids in the component container.
  static void erase_slot(State& state, std::size_t slot) {
    const auto last = state.components.size() - 1;
    if (slot != last) {
      state.components[slot] = std::move(state.components[last]);
      const auto& ids = state.components[slot]->ids;
      for (std::size_t q = 0; q < ids.size(); ++q)
        state.locations.at(ids[q]) = {slot, q};
    }
    state.components.pop_back();
  }

  void join(State& state, const std::string& first, const std::string& second) const {
    const auto a = state.locations.at(first), b = state.locations.at(second);
    if (a.component == b.component) return;
    const auto& left = state.components[a.component];
    const auto& right = state.components[b.component];
    const auto width = checked_add(left->ids.size(), right->ids.size(),
                                   "merged component qubit count overflows");
    const auto unchanged_bytes = state.tableau_bytes - left->core.state_bytes() -
                                 right->core.state_bytes();
    const auto projected_bytes = checked_add(unchanged_bytes, tableau_bytes(width),
                                             "merged tableau payload overflows");
    check_resources(state.locations.size(), width, projected_bytes);
    std::vector<std::string> ids;
    ids.reserve(width);
    ids.insert(ids.end(), left->ids.begin(), left->ids.end());
    ids.insert(ids.end(), right->ids.begin(), right->ids.end());
    auto joined = std::make_shared<Component>(std::move(ids),
        CompactCore::tensor_product(left->core, right->core));
    state.components[a.component] = std::move(joined);
    const auto& joined_ids = state.components[a.component]->ids;
    for (std::size_t q = 0; q < joined_ids.size(); ++q)
      state.locations.at(joined_ids[q]) = {a.component, q};
    erase_slot(state, b.component);
    state.tableau_bytes = projected_bytes;
  }

  void apply_gate(State& state, const Gate& gate) const {
    materialize(state, gate.first);
    if (gate.op == 'C' || gate.op == 'D') {
      materialize(state, gate.second);
      join(state, gate.first, gate.second);
    }
    const auto first = state.locations.at(gate.first);
    auto& core = mutable_component(state, first.component).core;
    switch (gate.op) {
      case 'H': core.hadamard(first.qubit); break;
      case 'S': core.phase_s(first.qubit); break;
      case 'X': core.pauli_x(first.qubit); break;
      case 'Y': core.pauli_z(first.qubit); core.pauli_x(first.qubit); break;
      case 'Z': core.pauli_z(first.qubit); break;
      case 'C': core.cnot(first.qubit, state.locations.at(gate.second).qubit); break;
      case 'D': core.controlled_z(first.qubit, state.locations.at(gate.second).qubit); break;
      default: throw std::logic_error("unvalidated gate");
    }
  }

  bool matches_background(const State& state, std::size_t slot) const {
    const auto& component = *state.components[slot];
    const mundo_x_bridge::PauliObserver observer(component.core);
    for (std::size_t q = 0; q < component.ids.size(); ++q) {
      if (background_ == Background::Zero) {
        auto z = identity_pauli(component.ids.size());
        set_axis(z, q, 'Z');
        if (observer.expectation(std::move(z)) != 1.0) return false;
      } else {
        if (infinite_detail::is_odd(component.ids[q])) continue;
        const auto partner = infinite_detail::partner_address(component.ids[q]);
        const auto other = state.locations.find(partner);
        if (other == state.locations.end() || other->second.component != slot)
          throw std::logic_error("Bell pair crossed a component boundary");
        for (const char axis : {'X', 'Z'}) {
          auto p = identity_pauli(component.ids.size());
          set_axis(p, q, axis);
          set_axis(p, other->second.qubit, axis);
          if (observer.expectation(std::move(p)) != 1.0) return false;
        }
      }
    }
    return true;
  }

  Background background_;
  Limits limits_;
  std::shared_ptr<State> state_;
};

} // namespace mundo_x
