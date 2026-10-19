#pragma once

#include "mundo_x/infinite_address.hpp"
#include "mundo_x/pauli_observer.hpp"

#include <algorithm>
#include <array>
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
  // Non-owning batch view used by InfiniteSession. A batch shares temporary
  // PauliObserver preparation across all observables that touch the same
  // materialized component. Background-only terms still allocate nothing.
  struct QueryRef { const std::vector<Term>* terms; int sign = 1; };
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

  // Snapshot text is an ASCII MXI1: envelope around a little-endian binary
  // current state. This transport budget is separate from tableau payload.
  // Limits are deliberately not imported: the receiving owner sets them.
  static constexpr std::size_t max_snapshot_bytes = std::size_t{16} * 1024 * 1024;

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

  std::string snapshot() const {
    std::size_t bytes = 1 + 8 + 8 + 8 + PortableRng::word_count * 8;
    for (const auto& component : state_->components) {
      bytes = checked_add(bytes, 8, "snapshot dimensions overflow");
      for (const auto& id : component->ids) {
        bytes = checked_add(bytes, 8, "snapshot dimensions overflow");
        bytes = checked_add(bytes, id.size(), "snapshot dimensions overflow");
      }
      bytes = checked_add(bytes, component->core.state_bytes(),
                          "snapshot dimensions overflow");
    }
    if (bytes > (max_snapshot_bytes - 5) / 2)
      throw std::length_error("snapshot exceeds 16 MiB transport budget");
    HexWriter out(bytes);
    out.byte(background_ == Background::Zero ? 0 : 1);
    out.u64(state_->revision);
    out.u64(state_->components.size());
    out.u64(state_->rng.position);
    for (const auto word : state_->rng.words) out.u64(word);
    for (const auto& component : state_->components) {
      out.u64(component->ids.size());
      for (const auto& id : component->ids) {
        out.u64(id.size());
        for (const auto ch : id) out.byte(static_cast<std::uint8_t>(ch));
      }
      const auto packed = component->core.packed_state();
      for (const auto sign : packed.signs) out.byte(sign);
      for (const auto word : packed.x) out.u64(word);
      for (const auto word : packed.z) out.u64(word);
    }
    return std::move(out.text);
  }

  // Decoding checks sizes and the consumer's budgets before each allocation,
  // then validates signs, unused bits, commutation, rank, unique canonical
  // addresses, complete Bell pairs, and the complete portable RNG state.
  static InfiniteCore from_snapshot(const std::string& saved, const Limits& limits) {
    HexReader input(saved);
    const auto background = input.byte();
    if (background > 1) throw std::invalid_argument("invalid snapshot background");
    InfiniteCore result(background == 0 ? Background::Zero : Background::BellPairs, limits);
    auto& state = *result.state_;
    state.revision = input.u64();
    const auto count = input.size();
    if (count > limits.max_active_qubits)
      throw std::length_error("snapshot exceeds active qubit materialization budget");
    state.rng.position = input.size();
    for (auto& word : state.rng.words) word = input.u64();
    state.rng.validate();
    // Every component needs at least its count, one address length/digit,
    // and one 17-byte tableau. This check precedes metadata reservation.
    if (count > input.remaining() / 34)
      throw std::invalid_argument("truncated snapshot components");
    state.components.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
      const auto n = input.size();
      if (n == 0) throw std::invalid_argument("snapshot component is empty");
      const auto active = checked_add(state.locations.size(), n, "snapshot qubit count overflow");
      const auto payload = checked_add(state.tableau_bytes, tableau_bytes(n),
                                       "snapshot tableau size overflow");
      result.check_resources(active, n, payload);
      // Bounds below are independent of any advertised vector dimensions.
      input.require(checked_add(tableau_bytes(n), n * 8, "snapshot size overflow"));
      std::vector<std::string> ids;
      ids.reserve(n);
      for (std::size_t q = 0; q < n; ++q) {
        const auto digits = input.size();
        if (digits == 0 || digits > limits.max_address_digits)
          throw std::length_error("snapshot address digit budget exceeded");
        auto id = input.string(digits);
        if (result.canonical(id) != id)
          throw std::invalid_argument("snapshot address is not canonical");
        if (!state.locations.emplace(id, Location{index, q}).second)
          throw std::invalid_argument("snapshot contains duplicate addresses");
        state.address_bytes = checked_add(state.address_bytes, digits,
                                          "snapshot address text overflows");
        ids.push_back(std::move(id));
      }
      const auto words = n / 64 + (n % 64 != 0);
      input.require(tableau_bytes(n));
      CompactCore::PackedState packed;
      packed.qubits = n;
      packed.signs.resize(n);
      packed.x.resize(n * words);
      packed.z.resize(n * words);
      for (auto& sign : packed.signs) sign = input.byte();
      for (auto& word : packed.x) word = input.u64();
      for (auto& word : packed.z) word = input.u64();
      auto core = CompactCore::from_packed_state(packed);
      state.components.push_back(std::make_shared<Component>(std::move(ids), std::move(core)));
      state.tableau_bytes = payload;
    }
    if (input.remaining() != 0)
      throw std::invalid_argument("snapshot has trailing data");
    if (result.background_ == Background::BellPairs) {
      for (const auto& item : state.locations) {
        const auto partner = state.locations.find(infinite_detail::partner_address(item.first));
        if (partner == state.locations.end() ||
            partner->second.component != item.second.component)
          throw std::invalid_argument("snapshot splits an implicit Bell pair");
      }
    }
    return result;
  }

  void restore(const std::string& saved) {
    auto candidate = from_snapshot(saved, limits_);
    *this = std::move(candidate);
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
    std::map<std::size_t, std::unique_ptr<mundo_x_bridge::PauliObserver>> observers;
    return expectation_cached(terms, sign, observers);
  }

  std::vector<double> expectations(const std::vector<QueryRef>& queries) const {
    std::map<std::size_t, std::unique_ptr<mundo_x_bridge::PauliObserver>> observers;
    std::vector<double> values;
    values.reserve(queries.size());
    for (const auto& query : queries) {
      if (!query.terms) throw std::invalid_argument("null Pauli query");
      values.push_back(expectation_cached(*query.terms, query.sign, observers));
    }
    return values;
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
    const bool outcome_one = (candidate->rng() & 1U) != 0;
    component.core.measure_z_forced(location.qubit, outcome_one);
    const CompactCore::ZMeasurement outcome{outcome_one, 0.5, true};
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
  double expectation_cached(const std::vector<Term>& terms, int sign,
                            std::map<std::size_t, std::unique_ptr<mundo_x_bridge::PauliObserver>>& observers) const {
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
      auto found = observers.find(query.first);
      if (found == observers.end())
        found = observers.emplace(query.first, std::make_unique<mundo_x_bridge::PauliObserver>(
            state_->components[query.first]->core)).first;
      answer *= found->second->expectation(std::move(query.second));
      if (answer == 0.0) return 0.0;
    }
    return answer;
  }


  // Same MT19937-64 algorithm and seed/output sequence as std::mt19937_64.
  // Its explicit state makes snapshots portable between libstdc++ and libc++
  // without relying on their implementation-dependent stream representations.
  struct PortableRng {
    using Standard = std::mt19937_64;
    static constexpr std::size_t word_count = Standard::state_size;
    std::array<std::uint64_t, word_count> words{};
    std::size_t position = word_count;
    PortableRng() { seed(42); }
    void seed(std::uint64_t value) {
      words[0] = value;
      for (std::size_t i = 1; i < word_count; ++i)
        words[i] = Standard::initialization_multiplier *
            (words[i - 1] ^ (words[i - 1] >> (Standard::word_size - 2))) + i;
      position = word_count;
    }
    void validate() const {
      if (position > word_count)
        throw std::invalid_argument("snapshot RNG position is invalid");
      std::uint64_t any = 0;
      for (const auto word : words) any |= word;
      if (any == 0) throw std::invalid_argument("snapshot RNG state is zero");
    }
    std::uint64_t operator()() {
      if (position == word_count) {
        constexpr auto lower = (std::uint64_t{1} << Standard::mask_bits) - 1;
        for (std::size_t i = 0; i < word_count; ++i) {
          const auto joined = (words[i] & ~lower) |
                              (words[(i + 1) % word_count] & lower);
          words[i] = words[(i + Standard::shift_size) % word_count] ^
                     (joined >> 1) ^ ((joined & 1) ? Standard::xor_mask : 0);
        }
        position = 0;
      }
      auto value = words[position++];
      value ^= (value >> Standard::tempering_u) & Standard::tempering_d;
      value ^= (value << Standard::tempering_s) & Standard::tempering_b;
      value ^= (value << Standard::tempering_t) & Standard::tempering_c;
      value ^= value >> Standard::tempering_l;
      return value;
    }
  };

  struct HexWriter {
    std::string text = "MXI1:";
    explicit HexWriter(std::size_t bytes) { text.reserve(5 + 2 * bytes); }
    void byte(std::uint8_t value) {
      constexpr char digits[] = "0123456789abcdef";
      text.push_back(digits[value >> 4]); text.push_back(digits[value & 15]);
    }
    void u64(std::uint64_t value) {
      for (unsigned i = 0; i < 8; ++i) { byte(static_cast<std::uint8_t>(value)); value >>= 8; }
    }
  };

  struct HexReader {
    const std::string& text;
    std::size_t cursor = 5;
    explicit HexReader(const std::string& value) : text(value) {
      if (value.size() > max_snapshot_bytes)
        throw std::length_error("snapshot exceeds 16 MiB transport budget");
      if (value.size() < 5 || value.compare(0, 5, "MXI1:") != 0 || (value.size() - 5) % 2)
        throw std::invalid_argument("unsupported or malformed infinite snapshot");
    }
    std::size_t remaining() const { return (text.size() - cursor) / 2; }
    void require(std::size_t bytes) const {
      if (bytes > remaining()) throw std::invalid_argument("truncated infinite snapshot");
    }
    static unsigned nibble(char value) {
      if (value >= '0' && value <= '9') return static_cast<unsigned>(value - '0');
      if (value >= 'a' && value <= 'f') return static_cast<unsigned>(value - 'a' + 10);
      throw std::invalid_argument("snapshot contains invalid hexadecimal text");
    }
    std::uint8_t byte() {
      require(1);
      const auto value = static_cast<std::uint8_t>((nibble(text[cursor]) << 4) |
                                                  nibble(text[cursor + 1]));
      cursor += 2;
      return value;
    }
    std::uint64_t u64() {
      require(8);
      std::uint64_t value = 0;
      for (unsigned i = 0; i < 8; ++i) value |= std::uint64_t{byte()} << (8 * i);
      return value;
    }
    std::size_t size() {
      const auto value = u64();
      if (value > std::numeric_limits<std::size_t>::max())
        throw std::length_error("snapshot size exceeds this platform");
      return static_cast<std::size_t>(value);
    }
    std::string string(std::size_t bytes) {
      require(bytes);
      std::string value(bytes, '\0');
      for (auto& ch : value) ch = static_cast<char>(byte());
      return value;
    }
  };

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
    PortableRng rng;
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
