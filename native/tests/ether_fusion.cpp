#include "mundo_x/infinite_core.hpp"
#include "mundo_x/infinite_session.hpp"
#include "../../pc-android/native/quantum_device.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <memory>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

// Allocation failures occur only inside one operation under test. Diagnostics,
// snapshots and the independent oracle run after the fault has been disabled.
namespace fusion_allocation_fault {
bool enabled = false;
std::size_t remaining = 0;
#if defined(_MSC_VER)
__declspec(noinline)
#elif defined(__GNUC__) || defined(__clang__)
__attribute__((noinline))
#endif
void release(void* pointer) noexcept { std::free(pointer); }
}
void* operator new(std::size_t size) {
  if (fusion_allocation_fault::enabled) {
    if (fusion_allocation_fault::remaining == 0) throw std::bad_alloc();
    --fusion_allocation_fault::remaining;
  }
  if (void* value = std::malloc(size ? size : 1)) return value;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* p) noexcept { fusion_allocation_fault::release(p); }
void operator delete[](void* p) noexcept { fusion_allocation_fault::release(p); }
void operator delete(void* p, std::size_t) noexcept { fusion_allocation_fault::release(p); }
void operator delete[](void* p, std::size_t) noexcept { fusion_allocation_fault::release(p); }

namespace {
using Core = mundo_x::InfiniteCore;
using Session = mundo_x::InfiniteSession;
using Device = cosmic_pc::PcQuantumDevice;
using Term = Core::Term;
using Gate = Core::Gate;
using Complex = std::complex<double>;
constexpr double tolerance = 1e-10;
std::size_t checks = 0, comparisons = 0, dense_comparisons = 0, oracle_operations = 0;
std::size_t projections = 0, rollbacks = 0, allocation_failures = 0;
double maximum_error = 0, maximum_norm_error = 0;

void require(bool condition, const std::string& reason) {
  ++checks;
  if (!condition) throw std::runtime_error(reason);
}
void close(double actual, double expected, const std::string& reason) {
  ++comparisons;
  const double error = std::abs(actual - expected);
  maximum_error = std::max(maximum_error, error);
  require(std::isfinite(actual) && error < tolerance,
          reason + " (actual=" + std::to_string(actual) +
          ", expected=" + std::to_string(expected) + ")");
}
void close_dense(double actual, double expected, const std::string& reason) {
  ++dense_comparisons;
  close(actual, expected, reason);
}
template<class Function> void rejects(Function&& function, const std::string& reason) {
  bool rejected = false;
  try { function(); } catch (const std::exception&) { rejected = true; }
  require(rejected, reason);
}

// This reference evolves complex amplitudes using literal 2x2 matrices,
// controlled permutations/diagonal signs and projection. It never calls
// CompactCore, PauliObserver, EtherCore::pullback or stabilizer propagation.
class DenseOracle {
 public:
  explicit DenseOracle(std::size_t n, Core::Background background)
      : n_(n), state_(std::size_t{1} << n) {
    state_[0] = 1;
    if (background == Core::Background::BellPairs) {
      require(n % 2 == 0, "oracle Bell window must contain complete pairs");
      for (std::size_t q = 0; q < n; q += 2) { gate('H', q); gate('C', q, q + 1); }
    }
  }
  void gate(char op, std::size_t a, std::size_t b = 0) {
    ++oracle_operations;
    if (op == 'C' || op == 'D') {
      for (std::size_t i = 0; i < state_.size(); ++i) {
        if (((i >> a) & 1U) == 0) continue;
        if (op == 'C') {
          if (((i >> b) & 1U) == 0) std::swap(state_[i], state_[i ^ (std::size_t{1} << b)]);
        } else if ((i >> b) & 1U) state_[i] = -state_[i];
      }
      return;
    }
    const Complex zero{0, 0}, one{1, 0}, imag{0, 1};
    std::array<Complex, 4> matrix{};
    const double h = std::sqrt(0.5);
    switch (op) {
      case 'H': matrix = {h, h, h, -h}; break;
      case 'S': matrix = {one, zero, zero, imag}; break;
      case 'X': matrix = {zero, one, one, zero}; break;
      case 'Y': matrix = {zero, -imag, imag, zero}; break;
      case 'Z': matrix = {one, zero, zero, -one}; break;
      default: throw std::runtime_error("unsupported oracle matrix");
    }
    const auto mask = std::size_t{1} << a;
    for (std::size_t i = 0; i < state_.size(); ++i) if ((i & mask) == 0) {
      const auto j = i | mask;
      const auto u = state_[i], v = state_[j];
      state_[i] = matrix[0] * u + matrix[1] * v;
      state_[j] = matrix[2] * u + matrix[3] * v;
    }
  }
  void h_all() { for (std::size_t q = 0; q < n_; ++q) gate('H', q); }
  void cz_neighbors() { for (std::size_t q = 0; q + 1 < n_; ++q) gate('D', q, q + 1); }
  double norm() const {
    double value = 0;
    for (const auto& amplitude : state_) value += std::norm(amplitude);
    return value;
  }
  double probability_one(std::size_t q) const {
    double value = 0;
    for (std::size_t i = 0; i < state_.size(); ++i)
      if ((i >> q) & 1U) value += std::norm(state_[i]);
    return value;
  }
  double project(std::size_t q, bool one) {
    const double probability = one ? probability_one(q) : norm() - probability_one(q);
    require(probability > tolerance, "oracle requested impossible branch");
    const double scale = 1.0 / std::sqrt(probability);
    for (std::size_t i = 0; i < state_.size(); ++i) {
      if (bool((i >> q) & 1U) != one) state_[i] = 0;
      else state_[i] *= scale;
    }
    return probability;
  }
  double expectation(const std::vector<std::pair<std::size_t, char>>& terms,
                     int sign = 1) const {
    // Direct tensor-product matrix action: X flips; Z contributes (-1)^bit;
    // Y|0> = i|1>, Y|1> = -i|0>. No backwards circuit transformation occurs.
    std::size_t flips = 0, negative_on_one = 0;
    Complex phase{static_cast<double>(sign), 0};
    for (const auto& term : terms) {
      const auto bit = std::size_t{1} << term.first;
      if (term.second == 'X' || term.second == 'Y') flips |= bit;
      if (term.second == 'Z' || term.second == 'Y') negative_on_one |= bit;
      if (term.second == 'Y') phase *= Complex{0, 1};
    }
    Complex answer{};
    for (std::size_t i = 0; i < state_.size(); ++i) {
      auto bits = i & negative_on_one;
      bool negative = false;
      while (bits) { negative = !negative; bits &= bits - 1; }
      answer += std::conj(state_[i ^ flips]) * (negative ? -phase : phase) * state_[i];
    }
    require(std::abs(answer.imag()) < tolerance, "oracle expectation has imaginary residual");
    return answer.real();
  }
 private:
  std::size_t n_;
  std::vector<Complex> state_;
};

struct Domain {
  std::size_t count, anchor;
  std::vector<std::string> ids;
  explicit Domain(bool interior) : count(interior ? 14 : 12), anchor(interior ? 4 : 0) {
    std::string address = interior ? "1" + std::string(100, '0') : "0";
    for (std::size_t q = 0; q < count; ++q) {
      ids.push_back(address);
      bool carry = true;
      for (std::size_t i = address.size(); i-- > 0 && carry;) {
        if (address[i] == '9') address[i] = '0';
        else { ++address[i]; carry = false; }
      }
      if (carry) address.insert(address.begin(), '1');
    }
  }
};
void physical_gate(Core& core, DenseOracle& oracle, const Domain& domain,
                   char op, std::size_t a, std::size_t b = 0) {
  core.apply({Gate{op, domain.ids.at(a), (op == 'C' || op == 'D') ? domain.ids.at(b) : ""}});
  oracle.gate(op, a, b);
}
void global_h(Core& core, DenseOracle& oracle) { core.h_all(); oracle.h_all(); }
void global_cz(Core& core, DenseOracle& oracle) { core.cz_neighbors(); oracle.cz_neighbors(); }

void compare(Core& core, const DenseOracle& oracle, const Domain& domain,
             const std::string& context, bool complete = false) {
  const std::array<std::size_t, 4> sites{
      domain.anchor, domain.anchor + 1, domain.anchor + 3, domain.anchor + 4};
  const auto before = core.snapshot();
  const auto stats = core.stats();
  const auto norm_error = std::abs(oracle.norm() - 1);
  maximum_norm_error = std::max(maximum_norm_error, norm_error);
  require(norm_error < tolerance, context + ": oracle normalization drift");
  // Deterministic sample of all 4^4 tensor products, or the complete set.
  const std::size_t count = complete ? 256 : 64;
  static constexpr char axes[] = {'I', 'X', 'Y', 'Z'};
  for (std::size_t i = 0; i < count; ++i) {
    auto code = complete ? i : (i * 73 + 19) % 256;
    std::vector<Term> native;
    std::vector<std::pair<std::size_t, char>> dense;
    for (const auto q : sites) {
      const char axis = axes[code & 3U]; code >>= 2;
      if (axis == 'I') continue;
      native.push_back({domain.ids[q], axis}); dense.push_back({q, axis});
    }
    const int sign = i % 2 ? -1 : 1;
    const double expected = oracle.expectation(dense, sign);
    close_dense(core.expectation(native, sign), expected, context + ": tensor Pauli");
    if (complete) close_dense(core.expectation(native, -sign), -expected, context + ": reversed sign");
  }
  for (const auto q : sites)
    close_dense(core.probability_one(domain.ids[q]), oracle.probability_one(q), context + ": physical Born probability");
  require(core.snapshot() == before && core.stats() == stats,
          context + ": queries mutated frame, state, revision or RNG");
}

void mixed_dense_oracle() {
  // Each circuit contains only three CZ-neighbor layers. The reference
  // windows include the backward cones of all local gates, measurements and
  // checked observables, plus complete initial Bell pairs. The q0 window has
  // the actual left boundary. The interior window starts at even 10^100 and
  // all operations/queries lie on offsets 4..8, away from both artificial ends.
  for (const auto background : {Core::Background::Zero, Core::Background::BellPairs}) {
    for (const bool interior : {false, true}) {
      const Domain domain(interior);
      const auto a = domain.anchor;
      Core core(background); DenseOracle oracle(domain.count, background);
      const std::string label = std::string(interior ? "interior" : "q0") +
          (background == Core::Background::Zero ? "/zero" : "/bell");
      compare(core, oracle, domain, label + "/initial");
      global_h(core, oracle);
      physical_gate(core, oracle, domain, 'S', a);
      physical_gate(core, oracle, domain, 'X', a + 4);
      compare(core, oracle, domain, label + "/H-local");
      global_cz(core, oracle);
      compare(core, oracle, domain, label + "/CZ");
      physical_gate(core, oracle, domain, 'H', a + 1);
      physical_gate(core, oracle, domain, 'C', a, a + 4);
      physical_gate(core, oracle, domain, 'D', a + 1, a + 3);
      physical_gate(core, oracle, domain, 'Y', a + 2);
      compare(core, oracle, domain, label + "/local-CX-CZ-Y");
      global_h(core, oracle);
      physical_gate(core, oracle, domain, 'S', a + 4);
      physical_gate(core, oracle, domain, 'Z', a);
      physical_gate(core, oracle, domain, 'C', a + 3, a + 1);
      global_cz(core, oracle);
      compare(core, oracle, domain, label + "/second-frame");
      auto recovered = Core::from_snapshot(core.snapshot(), core.limits());
      compare(recovered, oracle, domain, label + "/restored-frame", true);
      core = std::move(recovered);

      for (const bool outcome : {false, true}) {
        Core branch = core; DenseOracle projected = oracle;
        const auto saved = branch.snapshot();
        const double one = projected.probability_one(a + 1);
        const double probability = outcome ? one : 1.0 - one;
        if (probability < tolerance) {
          rejects([&] { branch.force_measure_z(domain.ids[a + 1], outcome); },
                  label + ": impossible physical postselection accepted");
          require(branch.snapshot() == saved, "impossible branch changed the frame/state/RNG");
          ++rollbacks;
          continue;
        }
        close_dense(branch.force_measure_z(domain.ids[a + 1], outcome), probability,
                    label + ": forced physical branch probability");
        projected.project(a + 1, outcome); ++projections;
        compare(branch, projected, domain, label + "/postselection");
        physical_gate(branch, projected, domain, 'H', a + 2);
        global_h(branch, projected); global_cz(branch, projected);
        physical_gate(branch, projected, domain, 'D', a + 1, a + 4);
        physical_gate(branch, projected, domain, 'S', a);
        compare(branch, projected, domain, label + "/postselection-continued", true);
      }

      const std::uint64_t seed = interior ? 0x9b117ULL : 0x53741ULL;
      core.seed(seed); std::mt19937_64 rng(seed);
      const double one = oracle.probability_one(a + 4);
      const bool random = std::abs(one - 0.5) < tolerance;
      const bool expected = random ? (rng() & 1U) != 0 : one > 0.5;
      const auto result = core.measure_z(domain.ids[a + 4]);
      require(result.outcome_one == expected && result.random == random,
              label + ": random physical measurement/RNG differs");
      close_dense(result.probability, random ? 0.5 : 1.0, label + ": sample branch probability");
      oracle.project(a + 4, result.outcome_one); ++projections;
      compare(core, oracle, domain, label + "/sampled");
      const auto measured = core.snapshot();
      const auto again = core.measure_z(domain.ids[a + 4]);
      require(!again.random && again.outcome_one == result.outcome_one &&
              core.snapshot() == measured, "repeat physical measurement consumed RNG or changed state");
      global_h(core, oracle); global_cz(core, oracle);
      physical_gate(core, oracle, domain, 'Y', a + 1);
      compare(core, oracle, domain, label + "/sample-continued", true);
    }
  }
}

void boundary_correlations_and_cancellation() {
  Core cluster; cluster.h_all(); cluster.cz_neighbors();
  close(cluster.expectation({{"0", 'X'}, {"1", 'Z'}}), 1, "cluster K0");
  require(cluster.stats().active_qubits == 0 && cluster.ether_rule_count() == 2,
          "global cluster materialized a finite surrogate");
  cluster.apply({{'Z', "0000", ""}});
  close(cluster.expectation({{"0", 'X'}, {"1", 'Z'}}), -1,
        "local Z did not change global correlation across implicit neighbor");
  close(cluster.expectation({{"0", 'Z'}, {"1", 'X'}, {"2", 'Z'}}), 1,
        "local Z damaged adjacent cluster generator");
  require(cluster.ether_rule_count() == 2, "local gate replaced the global frame");
  cluster.apply({{'H', "0", ""}});
  close(cluster.expectation({{"0", 'Z'}, {"1", 'Z'}}), -1,
        "physical H lost cross-boundary anti-correlation");
  for (const bool one : {false, true}) {
    auto branch = cluster;
    close(branch.force_measure_z("0", one), 0.5, "cross-boundary postselection probability");
    close(branch.expectation({{"1", 'Z'}}), one ? 1 : -1,
          "measurement did not condition the physical neighboring site");
    close(branch.probability_one("1"), one ? 0 : 1, "conditional neighboring Born probability");
    close(branch.expectation({{"999", 'Z'}, {"1000", 'X'}, {"1001", 'Z'}}),
          1, "local projection changed a remote implicit cluster");
    ++projections;
  }
  Core cancelled; cancelled.h_all(); cancelled.apply({{'S', "0", ""}}); cancelled.h_all();
  require(cancelled.ether_rule_count() == 0, "equal adjacent frame rules did not cancel");
  close(cancelled.expectation({{"0", 'Y'}}), -1,
        "frame cancellation discarded or mis-signed the local correction");
  cancelled.h_all(); cancelled.cz_neighbors();
  close(cancelled.expectation({{"0", 'Y'}, {"1", 'Z'}}), 1,
        "new global frame ignored an existing signed finite correction");

  Core compact; compact.h_all(); compact.cz_neighbors();
  compact.apply({{'Z', "0", ""}, {'Z', "0", ""}});
  const auto frame = compact.ether_rule_count();
  require(compact.compact_background() > 0 && compact.stats().active_qubits == 0,
          "restored frame-relative background was not released");
  require(compact.ether_rule_count() == frame, "component compaction removed global frame");
  close(compact.expectation({{"0", 'X'}, {"1", 'Z'}}), 1,
        "component compaction changed the physical state");
}

void snapshots_rng_and_copies() {
  Core source; source.seed(782349); source.h_all(); source.cz_neighbors();
  source.apply({{'S', "0", ""}, {'Y', "3", ""}, {'C', "0", "3"}});
  const auto saved = source.snapshot();
  require(saved.rfind("MXI2:", 0) == 0, "nonempty Ether frame missing from snapshot version");
  auto copy = Core::from_snapshot(saved, source.limits());
  require(copy.snapshot() == saved && copy.ether_rule_count() == 2,
          "frame/current-state/RNG snapshot did not roundtrip exactly");
  std::mt19937_64 expected_rng(782349);
  for (unsigned i = 0; i < 32; ++i) {
    const auto address = std::to_string(100 + i * 8);
    const auto expected = (expected_rng() & 1U) != 0;
    const auto a = source.measure_z(address), b = copy.measure_z(address);
    require(a.random && b.random && a.probability == 0.5 && b.probability == 0.5 &&
                a.outcome_one == expected && b.outcome_one == expected,
            "restored global frame changed the portable RNG continuation");
    const auto current = copy.snapshot();
    const auto deterministic = copy.measure_z(address);
    require(!deterministic.random && deterministic.outcome_one == expected &&
                copy.snapshot() == current,
            "deterministic fused measurement advanced RNG");
    require(source.snapshot() == copy.snapshot(), "continued fused snapshots diverged");
    ++projections;
  }
  auto fork = source;
  const auto original = source.snapshot();
  fork.h_all(); fork.apply({{'X', "7", ""}});
  require(source.snapshot() == original && fork.snapshot() != original,
          "frame copy-on-write mutated another owner");

  Core forced; forced.seed(3737); forced.h_all(); forced.cz_neighbors();
  auto unforced = forced;
  constexpr std::size_t rng_start = 5 + 2 * 17;
  constexpr std::size_t rng_length = 2 * (8 + std::mt19937_64::state_size * 8);
  const auto original_rng = forced.snapshot().substr(rng_start, rng_length);
  close(forced.force_measure_z("0", true), 0.5, "forced RNG control probability");
  require(forced.snapshot().substr(rng_start, rng_length) == original_rng,
          "postselection changed serialized RNG state");
  const auto forced_next = forced.measure_z("100");
  const auto unforced_next = unforced.measure_z("100");
  require(forced_next.random && unforced_next.random &&
              forced_next.outcome_one == unforced_next.outcome_one,
          "postselection consumed a random output");

  Core legacy; legacy.apply({{'H', "0", ""}, {'S', "0", ""}});
  const auto old = legacy.snapshot();
  require(old.rfind("MXI1:", 0) == 0, "empty frame no longer uses compatible snapshot");
  auto imported = Core::from_snapshot(old, legacy.limits());
  require(imported.ether_rule_count() == 0, "legacy import invented a global frame");
  close(imported.expectation({{"0", 'Y'}}), 1, "legacy signed state import changed");
  imported.h_all(); imported.cz_neighbors();
  close(imported.expectation({{"0", 'Y'}, {"1", 'Z'}}), -1,
        "new Ether frame ignored imported legacy state");

  auto damaged = saved;
  // MXI2 extends the legacy background/revision/count/portable-MT fields with
  // a rule count and 0/1 rule tags. Corrupt the first rule, without changing
  // any tableau, so a decoder cannot silently discard an unknown frame.
  constexpr std::size_t rule_tag = 25 + std::mt19937_64::state_size * 8 + 8;
  damaged.at(5 + 2 * rule_tag) = '0'; damaged.at(6 + 2 * rule_tag) = '2';
  const auto before = source.snapshot();
  for (const auto& invalid : {damaged, saved.substr(0, saved.size() - 2), saved + "00"}) {
    rejects([&] { source.restore(invalid); }, "invalid fused snapshot accepted");
    require(source.snapshot() == before, "invalid snapshot partially replaced live frame/state/RNG");
    ++rollbacks;
  }
  Core::Limits restricted; restricted.max_ether_rules = 1;
  rejects([&] { Core::from_snapshot(saved, restricted); }, "import ignored receiving frame budget");
}

template<class Function>
void rollback(Core& core, Function&& operation, const std::string& label) {
  const auto saved = core.snapshot(); const auto stats = core.stats();
  rejects(operation, label + ": expected rejection");
  require(core.snapshot() == saved && core.stats() == stats,
          label + ": failure changed frame, component, revision or RNG");
  ++rollbacks;
}
void budgets_and_transactions() {
  Core::Limits rule_limit; rule_limit.max_ether_rules = 2;
  Core limited(Core::Background::Zero, rule_limit);
  limited.h_all(); limited.cz_neighbors();
  rollback(limited, [&] { limited.h_all(); }, "frame rule budget");
  limited.cz_neighbors(); limited.h_all();
  require(limited.ether_rule_count() == 0, "rule cancellation was rejected at capacity");

  Core::Limits cone; cone.max_ether_query_sites = 1;
  Core narrow(Core::Background::Zero, cone); narrow.h_all(); narrow.cz_neighbors();
  rollback(narrow, [&] { (void)narrow.expectation({{"2", 'X'}}); }, "expanded query cone budget");
  rollback(narrow, [&] { narrow.apply({{'H', "2", ""}}); }, "expanded local gate cone budget");
  close(narrow.expectation({{"2", 'Z'}}), 0, "bounded one-site pullback was rejected");

  for (unsigned budget = 0; budget < 3; ++budget) {
    Core::Limits limits;
    // An identity control on an implicit |0> neighbour remains symbolic.
    // This parity circuit now needs two sites; test below that actual need.
    if (budget == 0) limits.max_active_qubits = 1;
    if (budget == 1) limits.max_component_qubits = 1;
    if (budget == 2) limits.max_state_bytes = 33;
    Core small(Core::Background::Zero, limits);
    small.seed(811); small.h_all(); small.cz_neighbors(); small.h_all();
    close(small.probability_one("2"), 0.5, "query was incorrectly charged as materialization");
    rollback(small, [&] { (void)small.measure_z("2"); }, "measurement materialization budget");
    rollback(small, [&] { (void)small.force_measure_z("2", true); }, "postselection materialization budget");
  }
  {
    Core::Limits limits;
    limits.max_active_qubits = limits.max_component_qubits = 2;
    limits.max_state_bytes = 34;
    Core small(Core::Background::Zero, limits);
    small.h_all(); small.cz_neighbors(); small.h_all();
    close(small.force_measure_z("2", true), 0.5, "symbolic-neighbour projection probability");
    close(small.probability_one("2"), 1, "symbolic-neighbour projection result");
    require(small.stats().active_qubits == 2, "projection materialized identity neighbour");
  }
  Core::Limits digits; digits.max_address_digits = 1;
  Core end(Core::Background::Zero, digits); end.h_all(); end.cz_neighbors();
  rollback(end, [&] { (void)end.expectation({{"9", 'X'}}); }, "expanded successor digit budget");
  rollback(end, [&] { end.apply({{'H', "9", ""}}); }, "expanded gate successor digit budget");
  close(end.expectation({{"9", 'Z'}}), 0, "valid terminal address query was rejected");

  Core::Limits no_general_terms; no_general_terms.max_query_terms = 0;
  Core fixed(Core::Background::Zero, no_general_terms);
  fixed.h_all(); fixed.cz_neighbors(); fixed.h_all();
  close(fixed.probability_one("2"), 0.5, "fixed-size probability inherited general query budget");
  close(fixed.force_measure_z("2", false), 0.5, "fixed-size projection inherited general query budget");
  close(fixed.probability_one("2"), 0, "fixed-size projected probability wrong");

  Core::Limits partial; partial.max_active_qubits = 1;
  Core atomic(Core::Background::Zero, partial); atomic.h_all(); atomic.cz_neighbors();
  rollback(atomic, [&] { atomic.apply({{'Z', "0", ""}, {'H', "1", ""}}); },
           "partial finite batch under global frame");
  rollback(atomic, [&] { atomic.apply({{'Z', "0", ""}, {'T', "1", ""}}); },
           "unsupported trailing gate under global frame");
}

template<class Function>
void allocation_transaction(const Core& initial, Function&& operation, const std::string& label) {
  const auto saved = initial.snapshot();
  bool succeeded = false;
  for (std::size_t fail_at = 0; fail_at < 4096; ++fail_at) {
    auto candidate = initial;
    fusion_allocation_fault::remaining = fail_at;
    fusion_allocation_fault::enabled = true;
    bool failed = false;
    try { operation(candidate); }
    catch (const std::bad_alloc&) { failed = true; }
    catch (...) { fusion_allocation_fault::enabled = false; throw; }
    fusion_allocation_fault::enabled = false;
    require(initial.snapshot() == saved, label + ": shared source changed");
    if (failed) {
      require(candidate.snapshot() == saved, label + ": allocation failure partially committed");
      ++allocation_failures;
    } else { succeeded = true; break; }
  }
  require(succeeded, label + ": allocation fault sweep did not reach success");
}
void allocation_rollbacks() {
  Core initial; initial.seed(117); initial.h_all(); initial.cz_neighbors();
  const std::vector<Gate> batch{{'S', "0", ""}, {'C', "0", "3"}, {'Y', "2", ""}};
  allocation_transaction(initial, [&](Core& core) { core.apply(batch); }, "fused local batch");
  allocation_transaction(initial, [](Core& core) { core.h_all(); }, "global frame append");
  allocation_transaction(initial, [](Core& core) { (void)core.measure_z("0"); }, "fused random projection");
  allocation_transaction(initial, [](Core& core) { (void)core.force_measure_z("0", true); }, "fused postselection");
}

void device_send(Device& device, const std::string& command) {
  require(device.output(Device::control_port, 1, Device::clear_channel), "PC clear port rejected");
  for (const unsigned char c : command)
    require(device.output(Device::data_port, 1, c), "PC data port rejected");
  require(device.output(Device::control_port, 1, Device::submit_request), "PC submit port rejected");
}
std::string device_receive(Device& device) {
  std::string out;
  while (device.status() & Device::response_ready) {
    const auto value = device.input(Device::data_port, 1);
    require(value.has_value(), "PC response byte unavailable");
    out += static_cast<char>(*value);
    require(out.size() < 65536, "unexpected unbounded PC test response");
  }
  return out;
}
double session_expect(Session& session, const std::string& expression) {
  return session.query_batch({mundo_x::parse_observable(expression)}).values.at(0);
}
void session_and_pc_causality() {
  auto shared = std::make_shared<Session>();
  Device device(shared);
  shared->h_all(); shared->cz_neighbors();
  close(session_expect(*shared, "X:0 Z:1"), 1, "host global cluster unavailable to session");
  const auto epoch = shared->epoch();
  device_send(device, "z 0\nexpect X:0 Z:1\n");
  require(!(device.status() & Device::request_error), "guest local mutation under frame failed");
  const auto pending = device.remaining();
  close(session_expect(*shared, "X:0 Z:1"), -1,
        "guest local mutation did not reach host global correlation");
  require(shared->epoch() == epoch + 1 && shared->stats().ether_rules == 2,
          "guest transaction changed wrong owner/frame/epoch");
  require(device.remaining() == pending, "host query drained guest response");
  require(device_receive(device).find("\"value\":-1") != std::string::npos,
          "guest response did not derive from fused state");

  device_send(device, "h_all\nexpect Z:0 X:1\n");
  require(!(device.status() & Device::request_error), "guest global mutation failed");
  close(session_expect(*shared, "Z:0 X:1"), -1,
        "guest global mutation did not transform existing finite correction");
  require(shared->stats().ether_rules == 3, "guest global operation used a separate Ether owner");
  (void)device_receive(device);
  shared->h_all(); shared->apply({{'H', "0", ""}});
  device_send(device, "postselect 0 1\nexpect Z:1\n");
  require(!(device.status() & Device::request_error), "guest fused postselection failed");
  close(shared->probability_one("1"), 0, "guest projection did not condition host neighbor");
  require(device_receive(device).find("\"value\":1") != std::string::npos,
          "guest conditional correlation response wrong");

  const auto saved = shared->snapshot(); const auto serial = shared->epoch();
  device_send(device, "h_all\nmeasure 9\nx 8\nassert expect -1 I:0\n");
  require(device.status() & Device::request_error, "failing mixed guest program was accepted");
  require(shared->snapshot() == saved && shared->epoch() == serial,
          "failed guest transaction changed frame/state/RNG/epoch");
  require(device_receive(device).find("\"ok\":false") != std::string::npos,
          "failed guest transaction lacks protocol error");
  ++rollbacks;
  rejects([&] {
    shared->execute_prepared("h_all\nmeasure 17\n", [](const std::string&) -> std::string {
      throw std::bad_alloc();
    });
  }, "response allocation failure was swallowed");
  require(shared->snapshot() == saved && shared->epoch() == serial,
          "response allocation failure committed the Ether frame or projection");
  ++rollbacks;
  shared->cz_neighbors(); shared->restore(saved);
  require(shared->snapshot() == saved && shared->epoch() > serial,
          "session restore did not restore same fused state and invalidate observers");
  close(shared->probability_one("1"), 0, "restored host/guest correlation lost");
}
}  // namespace

int main() {
  try {
    mixed_dense_oracle();
    boundary_correlations_and_cancellation();
    snapshots_rng_and_copies();
    budgets_and_transactions();
    allocation_rollbacks();
    session_and_pc_causality();
    std::cout << "{\"suite\":\"ether_fusion\",\"ok\":true,\"checks\":" << checks
              << ",\"comparison_checks\":" << comparisons
              << ",\"independent_complex_oracle_comparisons\":" << dense_comparisons
              << ",\"dense_matrix_operations\":" << oracle_operations
              << ",\"max_error\":" << maximum_error
              << ",\"max_norm_error\":" << maximum_norm_error
              << ",\"verified_projection_cases\":" << projections
              << ",\"transaction_rollbacks\":" << rollbacks
              << ",\"allocation_failure_rollbacks\":" << allocation_failures
              << ",\"physical_left_boundary\":true,\"interior_address_digits\":101"
              << ",\"zero_and_bell_backgrounds\":true,\"session_and_pc_port_causality\":true}\n";
    return 0;
  } catch (const std::exception& error) {
    fusion_allocation_fault::enabled = false;
    std::cerr << "ether_fusion: " << error.what() << '\n'; return 1;
  }
}
