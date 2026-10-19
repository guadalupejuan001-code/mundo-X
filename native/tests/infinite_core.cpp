#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <new>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mundo_x/dense_core.hpp"
#include "mundo_x/infinite_core.hpp"

// Fail ordinary C++ allocations at every successive point of one transaction.
// The fault is disabled before inspecting the outcome or building diagnostics.
namespace allocation_fault {
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
  if (allocation_fault::enabled) {
    if (allocation_fault::remaining == 0) throw std::bad_alloc();
    --allocation_fault::remaining;
  }
  if (void* result = std::malloc(size ? size : 1)) return result;
  throw std::bad_alloc();
}
void* operator new[](std::size_t size) { return ::operator new(size); }
void operator delete(void* pointer) noexcept { allocation_fault::release(pointer); }
void operator delete[](void* pointer) noexcept { allocation_fault::release(pointer); }
void operator delete(void* pointer, std::size_t) noexcept { allocation_fault::release(pointer); }
void operator delete[](void* pointer, std::size_t) noexcept { allocation_fault::release(pointer); }

namespace {
using Core = mundo_x::InfiniteCore;
using Gate = Core::Gate;
using Term = Core::Term;
using Background = Core::Background;
using Complex = std::complex<double>;
using State = std::vector<Complex>;
constexpr double tolerance = 1e-10;
std::size_t checks = 0, oracle_comparisons = 0, gates_checked = 0;
std::size_t measurements_checked = 0, allocation_failures = 0;

void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, const std::string& message) {
  ++oracle_comparisons;
  require(std::isfinite(actual) && std::abs(actual - expected) < tolerance,
          message + " (actual=" + std::to_string(actual) +
          ", expected=" + std::to_string(expected) + ")");
}
bool same_stats(const Core::Stats& a, const Core::Stats& b) {
  return a.active_qubits == b.active_qubits && a.components == b.components &&
         a.largest_component == b.largest_component &&
         a.tableau_bytes == b.tableau_bytes && a.address_bytes == b.address_bytes &&
         a.revision == b.revision;
}
std::vector<std::string> addresses(std::size_t n, Background background) {
  const std::string big = "1" + std::string(100, '0');
  const std::vector<std::string> zero{
      "0", "1000000000", "18446744073709551616", big, "1", "71"};
  const std::vector<std::string> bell{
      "0", "1", "1000000000", "1000000001", big,
      "1" + std::string(99, '0') + "1"};
  const auto& pool = background == Background::Zero ? zero : bell;
  return {pool.begin(), pool.begin() + n};
}
std::vector<Term> terms_for(const std::vector<std::string>& ids,
                            const std::vector<char>& pauli) {
  std::vector<Term> terms;
  for (std::size_t q = 0; q < ids.size(); ++q)
    if (pauli[q] != 'I') terms.push_back(Term{ids[q], pauli[q]});
  return terms;
}
std::vector<char> decode_pauli(std::size_t n, std::uint64_t code) {
  static constexpr char axes[] = {'I', 'X', 'Y', 'Z'};
  std::vector<char> pauli(n);
  for (auto& axis : pauli) { axis = axes[code & 3U]; code >>= 2U; }
  return pauli;
}

// Independent action of Hermitian Pauli matrices on a dense state. In
// particular, Y|0> = i|1> and Y|1> = -i|0>; no tableau phase rules are used.
double dense_expectation(const State& state, const std::vector<char>& pauli) {
  Complex value{};
  double norm = 0;
  for (std::size_t i = 0; i < state.size(); ++i) {
    auto target = i;
    Complex phase{1., 0.};
    norm += std::norm(state[i]);
    for (std::size_t q = 0; q < pauli.size(); ++q) {
      const bool one = ((i >> q) & 1U) != 0;
      if (pauli[q] == 'X' || pauli[q] == 'Y') target ^= std::size_t{1} << q;
      if (pauli[q] == 'Z' && one) phase = -phase;
      if (pauli[q] == 'Y') phase *= one ? Complex{0, -1} : Complex{0, 1};
    }
    value += std::conj(state[target]) * phase * state[i];
  }
  require(norm > 0 && std::abs(value.imag()) < tolerance,
          "dense oracle produced an invalid Hermitian expectation");
  return value.real() / norm;
}
double dense_probability(const State& state, std::size_t q) {
  double one = 0, norm = 0;
  for (std::size_t i = 0; i < state.size(); ++i) {
    const double mass = std::norm(state[i]);
    norm += mass;
    if ((i >> q) & 1U) one += mass;
  }
  return one / norm;
}
void project(State& state, std::size_t q, bool one) {
  double mass = 0;
  for (std::size_t i = 0; i < state.size(); ++i) {
    if (static_cast<bool>((i >> q) & 1U) != one) state[i] = 0;
    mass += std::norm(state[i]);
  }
  require(mass > tolerance, "reference projection selected an impossible branch");
  for (auto& amplitude : state) amplitude /= std::sqrt(mass);
}
void dense_gate(mundo_x::DenseCore64& dense, char op, std::size_t a,
                std::size_t b = 0) {
  if (op == 'H') dense.hadamard(a);
  else if (op == 'S') dense.phase_s(a);
  else if (op == 'X') dense.pauli_x(a);
  else if (op == 'Z') dense.pauli_z(a);
  else if (op == 'Y') {
    // Y = S X S^-1, evaluated as ordinary dense gates.
    for (int k = 0; k < 3; ++k) dense.phase_s(a);
    dense.pauli_x(a); dense.phase_s(a);
  } else if (op == 'C') dense.cnot(a, b);
  else if (op == 'D') dense.controlled_z(a, b);
  else throw std::logic_error("invalid dense test gate");
}
void state_gate(State& state, char op, std::size_t a, std::size_t b = 0) {
  if (op == 'C' || op == 'D') {
    State output(state.size());
    for (std::size_t i = 0; i < state.size(); ++i) {
      const bool both = ((i >> a) & 1U) && ((i >> b) & 1U);
      const auto target = op == 'C' && ((i >> a) & 1U)
                              ? i ^ (std::size_t{1} << b) : i;
      output[target] += op == 'D' && both ? -state[i] : state[i];
    }
    state = std::move(output);
    return;
  }
  std::array<std::array<Complex, 2>, 2> matrix{};
  const double h = 1. / std::sqrt(2.);
  if (op == 'H') matrix = {{{h, h}, {h, -h}}};
  else if (op == 'S') matrix = {{{1., 0.}, {0., Complex{0., 1.}}}};
  else if (op == 'X') matrix = {{{0., 1.}, {1., 0.}}};
  else if (op == 'Y') matrix = {{{0., Complex{0., -1.}}, {Complex{0., 1.}, 0.}}};
  else if (op == 'Z') matrix = {{{1., 0.}, {0., -1.}}};
  else throw std::logic_error("invalid state-vector test gate");
  const auto bit = std::size_t{1} << a;
  for (std::size_t i = 0; i < state.size(); ++i) {
    if (i & bit) continue;
    const std::array<Complex, 2> input{state[i], state[i | bit]};
    std::array<Complex, 2> output{};
    for (std::size_t r = 0; r < 2; ++r)
      for (std::size_t c = 0; c < 2; ++c) output[r] += matrix[r][c] * input[c];
    state[i] = output[0]; state[i | bit] = output[1];
  }
}
void prepare_background(mundo_x::DenseCore64& dense, Background background) {
  if (background == Background::BellPairs)
    for (std::size_t q = 0; q < dense.qubits(); q += 2) {
      dense.hadamard(q); dense.cnot(q, q + 1);
    }
}
void verify(const Core& core, const State& dense,
            const std::vector<std::string>& ids, std::uint64_t salt = 0) {
  const auto before = core.stats();
  for (std::size_t q = 0; q < ids.size(); ++q)
    close(core.probability_one(ids[q]), dense_probability(dense, q),
          "independent local Born probability mismatch");
  const std::size_t total = std::size_t{1} << (2 * ids.size());
  const std::size_t samples = ids.size() <= 4 ? total : 96;
  std::mt19937_64 rng(0x5041554c495f494eULL ^ salt);
  for (std::size_t i = 0; i < samples; ++i) {
    const auto code = samples == total ? i : rng() % total;
    const auto pauli = decode_pauli(ids.size(), code);
    const auto terms = terms_for(ids, pauli);
    const double expected = dense_expectation(dense, pauli);
    close(core.expectation(terms), expected, "independent Pauli expectation mismatch");
    close(core.expectation(terms, -1), -expected, "signed Pauli expectation mismatch");
  }
  require(same_stats(before, core.stats()), "read-only oracle queries mutated core statistics");
}
std::vector<double> fingerprint(const Core& core, const std::vector<std::string>& ids) {
  std::vector<double> values;
  const auto total = std::size_t{1} << (2 * ids.size());
  for (std::size_t code = 0; code < total; ++code)
    values.push_back(core.expectation(terms_for(ids, decode_pauli(ids.size(), code))));
  return values;
}
void verify_future_randomness(Core actual, Core expected, const std::string& id) {
  for (int iteration = 0; iteration < 12; ++iteration) {
    actual.apply({Gate{'H', id, ""}});
    expected.apply({Gate{'H', id, ""}});
    const auto a = actual.measure_z(id), b = expected.measure_z(id);
    require(a.outcome_one == b.outcome_one && a.probability == b.probability &&
            a.random == b.random, "a rejected operation consumed measurement randomness");
  }
}
template <class Function>
void rejects_without_changes(Core& core, const std::vector<std::string>& ids,
                              Function operation, const std::string& label,
                              bool check_rng = true) {
  const Core original = core;
  const auto before = core.stats();
  const auto state_before = fingerprint(core, ids);
  bool rejected = false;
  try { operation(); } catch (const std::exception&) { rejected = true; }
  require(rejected, label + " did not reject");
  require(same_stats(before, core.stats()), label + " changed resource accounting or revision");
  require(fingerprint(core, ids) == state_before, label + " changed the represented state");
  if (check_rng && !ids.empty()) verify_future_randomness(core, original, ids[0]);
}

void implicit_backgrounds_and_addresses() {
  const std::vector<std::string> ids{
      "0", "1", "1000000000", "18446744073709551616",
      "1" + std::string(100, '0')};
  for (const auto background : {Background::Zero, Background::BellPairs}) {
    Core core(background);
    const auto before = core.stats();
    require(before.active_qubits == 0 && before.components == 0 &&
            before.tableau_bytes == 0 && before.address_bytes == 0,
            "rule-only background allocated explicit qubits");
    close(core.expectation({}), 1., "empty identity");
    close(core.expectation({}, -1), -1., "negative identity");
    for (const auto& id : ids) {
      close(core.probability_one(id), background == Background::Zero ? 0. : .5,
            "unmaterialized Born probability");
      close(core.expectation({Term{id, 'I'}}), 1., "addressed identity");
      close(core.expectation({Term{id, 'X'}}), 0., "background single X");
      close(core.expectation({Term{id, 'Y'}}), 0., "background single Y");
      close(core.expectation({Term{id, 'Z'}}), background == Background::Zero ? 1. : 0.,
            "background single Z");
    }
    require(same_stats(before, core.stats()), "implicit queries materialized a range of addresses");
  }
  Core bell(Background::BellPairs);
  for (const auto& even : std::vector<std::string>{"0", "1000000000", "18446744073709551616",
                                                  "1" + std::string(100, '0')}) {
    std::string odd = even;
    ++odd.back();
    for (const char axis : {'X', 'Y', 'Z'})
      close(bell.expectation({Term{even, axis}, Term{odd, axis}}), axis == 'Y' ? -1. : 1.,
            "implicit Bell pair correlation or Y sign");
    close(bell.expectation({Term{even, 'X'}, Term{odd, 'Z'}}), 0., "mixed Bell Pauli");
  }
  close(bell.expectation({Term{"0", 'X'}, Term{"1000000001", 'X'}}), 0.,
        "independent implicit pairs were treated as correlated");
  require(bell.stats().active_qubits == 0, "Bell correlation queries allocated pairs");

  for (const auto& id : ids) {
    Core zero(Background::Zero);
    zero.apply({Gate{'X', id, ""}});
    close(zero.probability_one(id), 1., "gate at arbitrary decimal address");
    require(zero.stats().active_qubits == 1 && zero.stats().components == 1 &&
            zero.stats().tableau_bytes == 17,
            "address magnitude allocated intervening qubits");
    require(zero.stats().address_bytes <= id.size() + 128,
            "address accounting grows by numeric range instead of string length");
  }
  Core alias(Background::Zero);
  alias.apply({Gate{'X', "0000001", ""}});
  close(alias.probability_one("1"), 1., "leading zero address alias");
  alias.apply({Gate{'X', "1", ""}});
  close(alias.probability_one("0001"), 0., "canonical aliases refer to different qubits");
  require(alias.stats().active_qubits == 1, "alias materialized a second qubit");
}

void named_gates_and_measurements() {
  const std::string far = "1" + std::string(100, '0');
  for (bool one : {false, true}) {
    Core core(Background::Zero);
    core.apply({Gate{'H', "0", ""}, Gate{'C', "0", far}});
    close(core.expectation({Term{"0", 'X'}, Term{far, 'X'}}), 1., "long-range Bell XX");
    close(core.expectation({Term{"0", 'Y'}, Term{far, 'Y'}}), -1., "long-range Bell YY");
    close(core.force_measure_z("0", one), .5, "forced Bell selected-branch probability");
    ++measurements_checked;
    close(core.probability_one(far), one ? 1. : 0., "forced Bell branch lost distant correlation");
    close(core.expectation({Term{"0", 'X'}, Term{far, 'X'}}), 0., "measurement did not project Bell coherence");
    close(core.force_measure_z(far, one), 1., "repeated deterministic forced measurement");
    rejects_without_changes(core, {"0", far}, [&] { core.force_measure_z(far, !one); },
                            "impossible forced branch");
  }
  for (bool one : {false, true}) {
    Core core(Background::BellPairs);
    close(core.force_measure_z("18446744073709551616", one), .5,
          "implicit Bell measurement probability");
    close(core.probability_one("18446744073709551617"), one ? 1. : 0.,
          "implicit Bell partner after measurement");
    require(core.stats().active_qubits == 2, "Bell measurement did not materialize only its pair");
  }
  Core plus_y(Background::Zero);
  plus_y.apply({Gate{'H', "0", ""}, Gate{'S', "0", ""}});
  close(plus_y.expectation({Term{"0", 'Y'}}), 1., "H then S failed to prepare positive Y");
  plus_y.apply({Gate{'Z', "0", ""}});
  close(plus_y.expectation({Term{"0", 'Y'}}), -1., "Pauli Z lost the Y sign");
  plus_y.apply({Gate{'Y', "0", ""}});
  close(plus_y.expectation({Term{"0", 'Y'}}), -1., "Y eigenstate gate changed its observable sign");

  Core actual(Background::Zero), control(Background::Zero);
  actual.seed(0x5354455053454544ULL); control.seed(0x5354455053454544ULL);
  const auto seeded = actual.stats(); actual.seed(0x5354455053454544ULL);
  require(same_stats(seeded, actual.stats()), "seed changed revision or state storage");
  bool seen_zero = false, seen_one = false;
  for (int i = 0; i < 64; ++i) {
    const auto before = actual.stats();
    const auto deterministic = actual.measure_z(far);
    require(!deterministic.outcome_one && !deterministic.random && deterministic.probability == 1.,
            "implicit zero measurement is not deterministic");
    require(same_stats(before, actual.stats()), "deterministic implicit measurement materialized state");
    actual.apply({Gate{'H', "0", ""}}); control.apply({Gate{'H', "0", ""}});
    (void)actual.expectation({Term{"0", 'Y'}});
    const auto a = actual.measure_z("0"), b = control.measure_z("0");
    require(a.random && b.random && a.probability == .5 &&
            a.outcome_one == b.outcome_one, "read-only/deterministic operations changed seeded outcomes");
    seen_zero |= !a.outcome_one; seen_one |= a.outcome_one;
    measurements_checked += 2;
  }
  require(seen_zero && seen_one, "fixed seeded sequence did not cover both measurement branches");
}

void random_dense_oracles() {
  std::mt19937_64 rng(0x494e46494e495445ULL);
  static constexpr char ops[] = {'H', 'S', 'X', 'Y', 'Z', 'C', 'D'};
  for (const auto background : {Background::Zero, Background::BellPairs})
    for (std::size_t n = 1; n <= 6; ++n) {
      if (background == Background::BellPairs && (n & 1U)) continue;
      const auto ids = addresses(n, background);
      for (std::size_t circuit = 0; circuit < 10; ++circuit) {
        Core core(background); mundo_x::DenseCore64 dense(n);
        prepare_background(dense, background);
        verify(core, dense.state(), ids, circuit);
        for (std::size_t step = 0; step < 36; ++step) {
          const char op = ops[rng() % (n > 1 ? 7 : 5)];
          const auto a = rng() % n;
          auto b = n > 1 ? rng() % (n - 1) : 0;
          if (n > 1 && b >= a) ++b;
          core.apply({Gate{op, ids[a], op == 'C' || op == 'D' ? ids[b] : ""}});
          dense_gate(dense, op, a, b);
          ++gates_checked;
          if ((step + 1) % 6 == 0) verify(core, dense.state(), ids, circuit * 37 + step);
        }
      }
    }
}

void mixed_unitary_and_projection_oracle() {
  std::mt19937_64 rng(0x50524f4a454354ULL);
  static constexpr char ops[] = {'H', 'S', 'X', 'Y', 'Z', 'C', 'D'};
  for (const auto background : {Background::Zero, Background::BellPairs})
    for (const std::size_t n : {2U, 4U, 6U}) {
      const auto ids = addresses(n, background);
      Core core(background); core.seed(8731 + n);
      mundo_x::DenseCore64 dense(n); prepare_background(dense, background);
      State reference = dense.state();
      for (std::size_t step = 0; step < 48; ++step) {
        if (step % 4 != 3) {
          const char op = ops[rng() % 7]; const auto a = rng() % n;
          auto b = rng() % (n - 1); if (b >= a) ++b;
          core.apply({Gate{op, ids[a], op == 'C' || op == 'D' ? ids[b] : ""}});
          state_gate(reference, op, a, b); ++gates_checked;
        } else {
          const auto q = rng() % n;
          const double probability = dense_probability(reference, q);
          const auto measured = core.measure_z(ids[q]); ++measurements_checked;
          const double expected = measured.outcome_one ? probability : 1. - probability;
          close(measured.probability, expected, "mixed circuit Born branch probability");
          require(measured.random == (probability > tolerance && probability < 1. - tolerance),
                  "mixed circuit deterministic/random classification");
          project(reference, q, measured.outcome_one);
        }
        if (step % 4 == 3) verify(core, reference, ids, step);
      }
    }
}

void invalid_operations_and_limits() {
  Core core(Background::Zero); core.seed(713);
  core.apply({Gate{'H', "0", ""}, Gate{'S', "1", ""}});
  const std::vector<std::string> ids{"0", "1"};
  for (const auto& batch : std::vector<std::vector<Gate>>{
      {{'X', "0", ""}, {'T', "1", ""}},
      {{'X', "0", ""}, {'H', "-1", ""}},
      {{'X', "0", ""}, {'C', "1", "0001"}},
      {{'X', "0", ""}, {'D', "0", "0"}},
      {{'X', "0", ""}, {'H', "1", "2"}},
      {{'X', "0", ""}, {'C', "1", ""}}})
    rejects_without_changes(core, ids, [&] { core.apply(batch); }, "invalid atomic gate batch");
  for (const auto& invalid : std::vector<std::string>{"", "-1", "+1", "1.0", " 1", "1 ",
                                                       "1e100", std::string("1\0", 2)}) {
    rejects_without_changes(core, ids, [&] { core.probability_one(invalid); }, "invalid probability address");
    rejects_without_changes(core, ids, [&] { core.measure_z(invalid); }, "invalid measurement address");
  }
  rejects_without_changes(core, ids, [&] { core.expectation({Term{"1", 'X'}, Term{"001", 'Z'}}); },
                          "duplicate canonical query address");
  rejects_without_changes(core, ids, [&] { core.expectation({Term{"1", 'I'}, Term{"1", 'X'}}); },
                          "duplicate identity query address");
  rejects_without_changes(core, ids, [&] { core.expectation({Term{"1", 'A'}}); }, "invalid Pauli axis");
  for (int sign : {0, 2, -2})
    rejects_without_changes(core, ids, [&] { core.expectation({}, sign); }, "invalid observable sign");
  const auto empty_before = core.stats(); core.apply({});
  require(same_stats(empty_before, core.stats()), "empty batch advanced revision");

  Core::Limits limits;
  limits.max_active_qubits = 2;
  Core active(Background::Zero, limits);
  active.apply({Gate{'H', "0", ""}, Gate{'H', "1", ""}});
  rejects_without_changes(active, ids, [&] { active.apply({Gate{'X', "0", ""}, Gate{'H', "2", ""}}); },
                          "active qubit budget");
  limits = Core::Limits{}; limits.max_component_qubits = 2;
  Core component(Background::Zero, limits);
  component.apply({Gate{'H', "0", ""}, Gate{'X', "1", ""}, Gate{'H', "2", ""}, Gate{'C', "0", "1"}});
  rejects_without_changes(component, {"0", "1", "2"}, [&] { component.apply({Gate{'Z', "0", ""}, Gate{'D', "1", "2"}}); },
                          "component qubit budget");
  limits = Core::Limits{}; limits.max_state_bytes = 33;
  Core bytes(Background::Zero, limits); bytes.apply({Gate{'H', "0", ""}});
  rejects_without_changes(bytes, {"0"}, [&] { bytes.apply({Gate{'X', "0", ""}, Gate{'H', "1", ""}}); },
                          "state byte budget");
  Core bell_bytes(Background::BellPairs, limits);
  rejects_without_changes(bell_bytes, {"0", "1"}, [&] { bell_bytes.measure_z("0"); },
                          "implicit pair measurement over byte budget", false);
  limits = Core::Limits{}; limits.max_address_digits = 3;
  Core digits(Background::Zero, limits);
  close(digits.probability_one("999"), 0., "maximum accepted address length");
  for (const auto& invalid : {std::string("1000"), std::string("0001")})
    rejects_without_changes(digits, {"0"}, [&] { digits.apply({Gate{'X', "0", ""}, Gate{'H', invalid, ""}}); },
                            "raw address length budget");
  limits = Core::Limits{}; limits.max_query_terms = 1;
  Core terms(Background::Zero, limits);
  rejects_without_changes(terms, {"0"}, [&] { terms.expectation({Term{"0", 'I'}, Term{"1", 'I'}}); },
                          "query term budget");
  limits = Core::Limits{};
  limits.max_active_qubits = limits.max_component_qubits = limits.max_state_bytes = 0;
  for (const auto background : {Background::Zero, Background::BellPairs}) {
    Core readonly(background, limits);
    const auto before = readonly.stats();
    close(readonly.expectation({Term{"0", 'Z'}, Term{"1", 'Z'}}), 1., "zero-budget background query");
    readonly.apply({});
    require(readonly.compact_background() == 0 && same_stats(before, readonly.stats()),
            "zero-budget background needed explicit storage");
    rejects_without_changes(readonly, {"0", "1"}, [&] { readonly.apply({Gate{'H', "0", ""}}); },
                            "zero active-storage budget", false);
  }
}

void exact_compaction_and_copy_isolation() {
  const std::string far = "1" + std::string(100, '0');
  for (const auto background : {Background::Zero, Background::BellPairs}) {
    Core core(background);
    const auto original = core;
    const std::vector<Gate> forward{{'H', "0", ""}, {'S', far, ""}, {'C', "0", far},
                                     {'Y', "1", ""}, {'D', far, "1"}};
    core.apply(forward);
    require(original.stats().active_qubits == 0, "copy-on-write mutation modified original core");
    close(original.probability_one("0"), background == Background::Zero ? 0. : .5,
          "copied background changed when child mutated");
    std::vector<Gate> inverse;
    for (auto it = forward.rbegin(); it != forward.rend(); ++it) {
      inverse.push_back(*it);
      if (it->op == 'S') { inverse.push_back(*it); inverse.push_back(*it); }
    }
    core.apply(inverse);
    const auto count = core.stats().active_qubits;
    require(count > 0, "compaction fixture has no materialized state");
    require(core.compact_background() == count, "inverse state was not returned completely to its rule");
    const auto after = core.stats();
    require(after.active_qubits == 0 && after.components == 0 && after.tableau_bytes == 0 &&
            after.address_bytes == 0, "compaction retained explicit buffers");
    require(core.compact_background() == 0 && same_stats(after, core.stats()),
            "idempotent compaction changed revision");
  }
  Core zero(Background::Zero); zero.apply({Gate{'H', "0", ""}});
  require(zero.compact_background() == 0, "compaction discarded a plus state as zero");
  close(zero.expectation({Term{"0", 'X'}}), 1., "failed compaction changed plus coherence");
  Core bell(Background::BellPairs); bell.apply({Gate{'Z', "0", ""}});
  require(bell.compact_background() == 0, "compaction discarded a changed Bell phase");
  close(bell.expectation({Term{"0", 'X'}, Term{"1", 'X'}}), -1., "Bell phase after rejected compaction");
  bell.apply({Gate{'Z', "0", ""}});
  require(bell.compact_background() == 2, "restored Bell pair did not return to the rule");
}

void allocation_failure_transactions() {
  Core original(Background::Zero); original.seed(8921);
  original.apply({Gate{'H', "0", ""}, Gate{'H', "1000000000", ""}});
  const std::vector<std::string> ids{"0", "1000000000", "3"};
  const auto before = original.stats();
  const auto state_before = fingerprint(original, ids);
  const std::vector<Gate> batch{{'Y', "0", ""}, {'C', "0", "1000000000"},
                               {'H', "3", ""}, {'D', "3", "0"}, {'S', "3", ""}};
  Core expected = original; expected.apply(batch);
  bool completed = false;
  for (std::size_t fail_at = 0; fail_at < 10000; ++fail_at) {
    Core candidate = original;
    allocation_fault::remaining = fail_at;
    allocation_fault::enabled = true;
    bool failed = false;
    try { candidate.apply(batch); }
    catch (const std::bad_alloc&) { failed = true; }
    catch (...) { allocation_fault::enabled = false; throw; }
    allocation_fault::enabled = false;
    if (!failed) {
      require(fingerprint(candidate, ids) == fingerprint(expected, ids),
              "successful transaction changed after allocation fault injection");
      completed = true; break;
    }
    ++allocation_failures;
    require(same_stats(before, candidate.stats()), "allocation failure changed stats or revision");
    require(fingerprint(candidate, ids) == state_before, "allocation failure committed a partial batch");
    verify_future_randomness(candidate, original, "0");
  }
  require(completed && allocation_failures > 0, "allocation sweep did not reach both failure and success");

  // An implicit random measurement must also preserve the seed if materializing
  // its Bell pair fails before commit.
  Core bell(Background::BellPairs); bell.seed(933);
  const auto bell_before = bell.stats();
  completed = false;
  for (std::size_t fail_at = 0; fail_at < 10000; ++fail_at) {
    Core candidate = bell;
    allocation_fault::remaining = fail_at; allocation_fault::enabled = true;
    bool failed = false;
    try { (void)candidate.measure_z("1000000000"); }
    catch (const std::bad_alloc&) { failed = true; }
    catch (...) { allocation_fault::enabled = false; throw; }
    allocation_fault::enabled = false;
    if (!failed) { completed = true; break; }
    ++allocation_failures;
    require(same_stats(bell_before, candidate.stats()), "failed measurement committed explicit pair or revision");
    verify_future_randomness(candidate, bell, "1000000000");
  }
  require(completed, "allocation sweep did not complete implicit measurement");
}
}  // namespace

int main() {
  bool ok = true;
  try {
    implicit_backgrounds_and_addresses();
    named_gates_and_measurements();
    random_dense_oracles();
    mixed_unitary_and_projection_oracle();
    invalid_operations_and_limits();
    exact_compaction_and_copy_isolation();
    allocation_failure_transactions();
  } catch (const std::exception& error) {
    allocation_fault::enabled = false;
    std::cerr << "infinite_core: " << error.what() << '\n';
    ok = false;
  }
  std::cout << "{\"ok\":" << (ok ? "true" : "false")
            << ",\"checks\":" << checks
            << ",\"oracle_comparisons\":" << oracle_comparisons
            << ",\"gates\":" << gates_checked
            << ",\"measurements\":" << measurements_checked
            << ",\"allocation_failures\":" << allocation_failures << "}\n";
  return ok ? 0 : 1;
}
