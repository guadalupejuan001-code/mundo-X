// Regression coverage for the 60-qubit application profile. The full-width
// tests use CompactCore / ReducedExactTRepresentation, never DenseCore(60).
// A six-qubit DenseCore64 oracle is embedded at non-contiguous physical indices
// so high bits, signed Paulis and non-Clifford coefficients have a manageable
// independent numerical check in addition to the fully connected GHZ60 cases.
#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/engine_session.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/graphics_core_bridge.h"
#include "mundo_x/pauli_observer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr std::size_t width = 60;
constexpr std::array<std::size_t, 6> active{{0, 1, 31, 32, 53, 59}};
// Gates/coefficients in the oracle use double precision. This is much tighter
// than a visual tolerance while allowing ordinary roundoff from long circuits.
constexpr double tolerance = 1e-10;
using Complex = std::complex<double>;
using State = std::vector<Complex>;
using Session = mundo_x::EngineSession;
using Gate = Session::Gate;
using Reduced = mundo_x::ReducedExactTRepresentation;
std::size_t checks = 0, oracle_comparisons = 0, measurement_branches = 0;
double maximum_oracle_error = 0.;

void require(bool condition, const std::string& context) {
  ++checks;
  if (!condition) throw std::runtime_error(context);
}

void near(double actual, double expected, const std::string& context) {
  require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance,
          context + ": actual=" + std::to_string(actual) +
          " expected=" + std::to_string(expected));
}

template<class Exception, class Action>
void rejects(Action action, const std::string& context) {
  bool caught = false;
  try { action(); } catch (const Exception&) { caught = true; }
  require(caught, context);
}

std::vector<std::string> generators(const mundo_x::CompactCore& core) {
  std::vector<std::string> rows;
  for (std::size_t row = 0; row < core.qubits(); ++row)
    rows.push_back(core.generator_string(row));
  return rows;
}

std::vector<std::string> generators(const Session& core) {
  std::vector<std::string> rows;
  for (std::size_t row = 0; row < core.qubits(); ++row)
    rows.push_back(core.generator(row));
  return rows;
}

std::string pauli(std::initializer_list<std::pair<std::size_t, char>> entries) {
  std::string text(width, 'I');
  for (const auto& entry : entries) text.at(entry.first) = entry.second;
  return text;
}

std::vector<Gate> ghz_gates() {
  std::vector<Gate> gates{{MX_GATE_H, 0, 0}};
  for (std::size_t q = 1; q < width; ++q)
    gates.push_back({MX_GATE_CX, q - 1, q});
  return gates;
}

template<class Core>
void make_ghz(Core& core) {
  core.hadamard(0);
  for (std::size_t q = 1; q < width; ++q) core.cnot(q - 1, q);
}

double compact_expectation(const mundo_x_bridge::PauliObserver& observer,
                           const std::string& text) {
  return observer.expectation(mundo_x_bridge::Pauli::parse(text, width));
}

// Direct character-by-character matrix action is intentionally independent of
// the production Pauli masks, GF(2) reduction and sparse-coset arithmetic.
double reference_expectation(const State& state, const std::string& text) {
  require(text.size() == active.size() + 1, "oracle Pauli width");
  Complex sum{};
  double norm = 0.;
  for (std::size_t source = 0; source < state.size(); ++source) {
    std::size_t target = source;
    Complex factor = text[0] == '-' ? -1. : 1.;
    for (std::size_t q = 0; q < active.size(); ++q) {
      const auto bit = std::size_t{1} << q;
      const bool one = (source & bit) != 0;
      switch (text[q + 1]) {
        case 'I': break;
        case 'X': target ^= bit; break;
        case 'Y': target ^= bit; factor *= Complex{0., one ? -1. : 1.}; break;
        case 'Z': if (one) factor = -factor; break;
        default: throw std::logic_error("oracle Pauli character");
      }
    }
    sum += std::conj(state[target]) * factor * state[source];
    norm += std::norm(state[source]);
  }
  require(norm > 0. && std::abs(sum.imag()) <= tolerance, "oracle norm/Hermiticity");
  return sum.real() / norm;
}

std::string embed(const std::string& local) {
  std::string full(width + 1, 'I');
  full[0] = local[0];
  for (std::size_t q = 0; q < active.size(); ++q)
    full[active[q] + 1] = local[q + 1];
  return full;
}

std::string local_pauli(std::size_t code, bool negative = false) {
  std::string result(active.size() + 1, 'I');
  result[0] = negative ? '-' : '+';
  for (std::size_t q = 0; q < active.size(); ++q)
    result[q + 1] = "IXYZ"[(code >> (2 * q)) & 3U];
  return result;
}

void compare_oracle(double actual, double expected, const std::string& context) {
  ++oracle_comparisons;
  maximum_oracle_error = std::max(maximum_oracle_error, std::abs(actual - expected));
  near(actual, expected, context);
}

void verify_embedded(const mundo_x::CompactCore& core, const State& state,
                     bool complete_pauli_basis) {
  const auto before = generators(core);
  const mundo_x_bridge::PauliObserver observer(core);
  // The other 54 sites are |0>. Each of the 60 independent signed generators
  // must stabilize this explicit product embedding, determining the full state.
  for (const auto& row : before) {
    std::string local(active.size() + 1, 'I');
    local[0] = row[0];
    bool zero = false;
    for (std::size_t q = 0; q < width; ++q) {
      const auto found = std::find(active.begin(), active.end(), q);
      if (found == active.end()) {
        if (row[q + 1] == 'X' || row[q + 1] == 'Y') zero = true;
      } else local[std::distance(active.begin(), found) + 1] = row[q + 1];
    }
    compare_oracle(zero ? 0. : reference_expectation(state, local), 1.,
                   "signed 60-site generator vs embedded oracle");
  }
  if (complete_pauli_basis) {
    const auto count = std::size_t{1} << (2 * active.size());
    for (std::size_t code = 0; code < count; ++code) {
      auto local = local_pauli(code);
      const double expected = reference_expectation(state, local);
      compare_oracle(compact_expectation(observer, embed(local)), expected,
                     "embedded complete positive Pauli basis");
      local[0] = '-';
      compare_oracle(compact_expectation(observer, embed(local)), -expected,
                     "embedded complete negative Pauli basis");
    }
  }
  require(generators(core) == before, "60-site observation mutated tableau");
}

template<class Core>
void clifford_gate(Core& core, std::uint64_t op, std::size_t a, std::size_t b) {
  switch (op) {
    case MX_GATE_H: core.hadamard(a); break;
    case MX_GATE_S: core.phase_s(a); break;
    case MX_GATE_X: core.pauli_x(a); break;
    case MX_GATE_Z: core.pauli_z(a); break;
    case MX_GATE_CX: core.cnot(a, b); break;
    case MX_GATE_CZ: core.controlled_z(a, b); break;
    case MX_GATE_Y: core.pauli_z(a); core.pauli_x(a); break;
    default: throw std::logic_error("test Clifford operation");
  }
}

State project(const State& input, std::size_t q, bool one) {
  State result = input;
  double mass = 0.;
  for (std::size_t i = 0; i < result.size(); ++i) {
    if (static_cast<bool>(i & (std::size_t{1} << q)) != one) result[i] = 0.;
    mass += std::norm(result[i]);
  }
  require(mass > tolerance, "oracle projected an impossible branch");
  for (auto& amplitude : result) amplitude /= std::sqrt(mass);
  return result;
}

void ghz_and_measurement() {
  mundo_x::CompactCore core(width);
  make_ghz(core);
  require(core.qubits() == width && core.is_ghz_signature(), "connected GHZ60 signature");
  require(core.state_bytes() == 1020, "60-qubit packed tableau logical payload");
  const std::string all_x(width, 'X');
  std::string two_y = all_x;
  two_y[0] = two_y[59] = 'Y';
  for (const bool negative : {false, true}) {
    if (negative) core.pauli_z(59);
    const mundo_x_bridge::PauliObserver observer(core);
    const double sign = negative ? -1. : 1.;
    near(compact_expectation(observer, all_x), sign, "GHZ60 X^60 phase witness");
    near(compact_expectation(observer, "-" + all_x), -sign, "GHZ60 signed X^60");
    near(compact_expectation(observer, two_y), -sign, "GHZ60 two-Y sign");
    near(compact_expectation(observer, pauli({{0, 'Z'}, {59, 'Z'}})), 1., "GHZ60 endpoint correlation");
    for (const auto q : {std::size_t{0}, std::size_t{31}, std::size_t{32}, std::size_t{53}, std::size_t{59}})
      near(core.probability_one_z(q), .5, "GHZ60 local Born probability");
  }
  for (const bool one : {false, true}) {
    auto branch = core;
    require(branch.measure_z_forced(59, one), "GHZ60 forced branch was not random");
    ++measurement_branches;
    for (std::size_t q = 0; q < width; ++q)
      near(branch.probability_one_z(q), one ? 1. : 0., "GHZ60 complete-register projection");
    const mundo_x_bridge::PauliObserver observer(branch);
    near(compact_expectation(observer, all_x), 0., "GHZ60 measurement removed X coherence");
    near(compact_expectation(observer, pauli({{0, 'Z'}, {59, 'Z'}})), 1., "GHZ60 measurement preserves ZZ correlation");
  }
  bool saw_zero = false, saw_one = false;
  for (std::uint64_t seed = 0; seed < 16; ++seed) {
    auto first = core, second = core;
    std::mt19937_64 rng(seed), same(seed), expected(seed);
    const bool predicted = (expected() & 1U) != 0;
    const auto a = first.measure_z(59, rng), b = second.measure_z(59, same);
    require(a.random && b.random && a.probability == .5 && b.probability == .5,
            "GHZ60 measurement branch metadata");
    require(a.outcome_one == predicted && a.outcome_one == b.outcome_one &&
            rng == expected && same == expected && generators(first) == generators(second),
            "GHZ60 seeded measurement reproducibility");
    saw_one |= a.outcome_one; saw_zero |= !a.outcome_one;
    const auto before = generators(first);
    for (const auto q : active) {
      const auto repeated = first.measure_z(q, rng);
      require(!repeated.random && repeated.probability == 1. && repeated.outcome_one == predicted,
              "GHZ60 deterministic correlated measurements");
    }
    require(rng == expected && generators(first) == before,
            "GHZ60 deterministic measurement changed state/RNG");
  }
  require(saw_zero && saw_one, "GHZ60 seeds did not cover both Born branches");

  const auto before = generators(core);
  std::mt19937_64 rng(60); const auto saved_rng = rng;
  auto invalid = [&](auto action) {
    rejects<std::out_of_range>(action, "q60 must be outside a 60-qubit register");
    require(generators(core) == before && rng == saved_rng, "invalid q60 mutated tableau/RNG");
  };
  invalid([&] { core.hadamard(60); }); invalid([&] { core.phase_s(60); });
  invalid([&] { core.pauli_x(60); }); invalid([&] { core.pauli_z(60); });
  invalid([&] { core.cnot(59, 60); }); invalid([&] { core.cnot(60, 59); });
  invalid([&] { core.controlled_z(59, 60); }); invalid([&] { core.controlled_z(60, 59); });
  invalid([&] { (void)core.probability_one_z(60); });
  invalid([&] { (void)core.measure_z(60, rng); });
  invalid([&] { (void)core.measure_z_forced(60, true); });
  invalid([&] { (void)core.generator_string(60); });
}

void embedded_clifford_oracle() {
  for (std::uint64_t trial = 0; trial < 4; ++trial) {
    mundo_x::CompactCore core(width);
    mundo_x::DenseCore64 reference(active.size());
    std::mt19937_64 rng(0x6031325359ULL + trial);
    // Guaranteed interactions across 31/32 and the JavaScript integer boundary
    // at 53 precede the seeded circuit; every mapped site participates.
    for (std::size_t q = 0; q < active.size(); ++q) {
      core.hadamard(active[q]); reference.hadamard(q);
      core.phase_s(active[q]); reference.phase_s(q);
      if (q) { core.cnot(active[q - 1], active[q]); reference.cnot(q - 1, q); }
    }
    for (std::size_t step = 0; step < 96; ++step) {
      const auto op = rng() % 7, a = rng() % active.size();
      const auto b = (a + 1 + rng() % (active.size() - 1)) % active.size();
      clifford_gate(core, op, active[a], active[b]);
      clifford_gate(reference, op, a, b);
      if (step % 24 == 23) verify_embedded(core, reference.state(), step == 95);
    }
    for (std::size_t local = 2; local < active.size(); ++local) {
      auto z = local_pauli(0); z[local + 1] = 'Z';
      const double p_one = (1. - reference_expectation(reference.state(), z)) / 2.;
      near(core.probability_one_z(active[local]), p_one, "embedded high-bit measurement probability");
      for (const bool one : {false, true}) {
        const double mass = one ? p_one : 1. - p_one;
        if (mass <= tolerance) continue;
        auto branch = core;
        const bool random = p_one > tolerance && p_one < 1. - tolerance;
        require(branch.measure_z_forced(active[local], one) == random,
                "embedded measurement random/deterministic classification");
        verify_embedded(branch, project(reference.state(), local, one), false);
        ++measurement_branches;
      }
    }
  }
}

double reduced_expectation(const Reduced& core, const std::string& text) {
  std::uint64_t x = 0, z = 0;
  unsigned phase = text[0] == '-' ? 2U : 0U;
  for (std::size_t q = 0; q < width; ++q) {
    const auto bit = std::uint64_t{1} << q;
    if (text[q + 1] == 'X' || text[q + 1] == 'Y') x |= bit;
    if (text[q + 1] == 'Z' || text[q + 1] == 'Y') z |= bit;
    if (text[q + 1] == 'Y') ++phase;
  }
  const auto observed = core.pauli_expectation(x, z, static_cast<std::uint8_t>(phase & 3U));
  require(std::abs(observed.imag()) <= tolerance, "Reduced60 Hermitian observable became complex");
  return observed.real() / core.sparse_norm();
}

void reduced_non_clifford() {
  Reduced ghz(width, 2);
  make_ghz(ghz);
  ghz.phase_t(59);
  require(ghz.component_count() == 2 && ghz.t_count() == 1, "GHZ60 T59 has two sparse cosets");
  const std::uint64_t all_x = (std::uint64_t{1} << width) - 1;
  const auto last = std::uint64_t{1} << 59;
  const double root_half = 1. / std::sqrt(2.);
  near(ghz.sparse_norm(), 1., "GHZ60 T norm");
  near(ghz.pauli_expectation(all_x, 0).real(), root_half, "GHZ60 T X^60 analytic cos(pi/4)");
  near(ghz.pauli_expectation(all_x, last, 1).real(), root_half, "GHZ60 T Y59 Xothers analytic sin(pi/4)");
  near(ghz.pauli_expectation(0, last | 1U).real(), 1., "GHZ60 T endpoint ZZ");
  near(ghz.z_expectation(59), 0., "GHZ60 T endpoint marginal");
  ghz.phase_t_dagger(59);
  near(ghz.pauli_expectation(all_x, 0).real(), 1., "GHZ60 T followed by inverse");
  near(ghz.pauli_expectation(all_x, last, 1).real(), 0., "GHZ60 inverse T cancels Y coherence");
  rejects<std::runtime_error>([&] { (void)ghz.dense_reference(); }, "Reduced60 dense-reference guard");
  rejects<std::runtime_error>([&] { (void)ghz.dense_from_reduced(); }, "Reduced60 dense-expansion guard");

  Reduced core(width, 64);
  mundo_x::DenseCore64 reference(active.size());
  std::mt19937_64 rng(0x60c11ff0ULL);
  for (std::size_t q = 0; q < active.size(); ++q) {
    core.hadamard(active[q]); reference.hadamard(q);
    core.phase_t(active[q]); reference.phase_t(q);
  }
  for (std::size_t step = 0; step < 72; ++step) {
    const auto a = rng() % active.size();
    const auto b = (a + 1 + rng() % (active.size() - 1)) % active.size();
    const auto op = rng() % 10;
    if (op < 7) {
      clifford_gate(core, op, active[a], active[b]);
      clifford_gate(reference, op, a, b);
    } else if (op == 7) {
      core.phase_t(active[a]); reference.phase_t(a);
    } else if (op == 8) {
      core.phase_t_dagger(active[a]); reference.phase(a, -std::acos(-1.) / 4.);
    } else {
      const double theta = (step & 1U) ? .37 : -.61;
      core.phase(active[a], theta); reference.phase(a, theta);
    }
  }
  near(core.sparse_norm(), 1., "Reduced60 mixed non-Clifford norm");
  require(core.component_count() <= 64, "embedded six-site sparse support exceeds 64");
  const auto payload = core.logical_payload_bytes(), components = core.component_count();
  const auto t_count = core.t_count();
  rejects<std::out_of_range>([&] { core.phase_t(60); }, "Reduced60 rejects T60");
  rejects<std::out_of_range>([&] { core.hadamard(60); }, "Reduced60 rejects H60");
  rejects<std::out_of_range>([&] { core.cnot(59, 60); }, "Reduced60 rejects CX59,60");
  rejects<std::out_of_range>([&] { core.phase(60, .37); }, "Reduced60 rejects phase60");
  rejects<std::out_of_range>([&] { (void)core.pauli_expectation(std::uint64_t{1} << 60, 0); },
                            "Reduced60 rejects observable bit60");
  require(core.logical_payload_bytes() == payload && core.component_count() == components &&
          core.t_count() == t_count, "Reduced60 rejected operations changed storage/history");
  for (std::size_t code = 0; code < (std::size_t{1} << (2 * active.size())); ++code) {
    const auto local = local_pauli(code, (code & 1U) != 0);
    compare_oracle(reduced_expectation(core, embed(local)),
                   reference_expectation(reference.state(), local),
                   "Reduced60 mixed non-Clifford complete Pauli basis after rejection");
  }
  for (std::size_t q = 0; q < width; ++q)
    if (std::find(active.begin(), active.end(), q) == active.end())
      near(core.z_expectation(q), 1., "Reduced60 idle site remains |0>");
}

struct Bridge {
  Bridge() : pointer(mx_create(MX_BACKEND_COMPACT, width)) {
    require(pointer != nullptr, std::string("create bridge60: ") + mx_last_error());
  }
  ~Bridge() { mx_destroy(pointer); }
  Bridge(const Bridge&) = delete;
  Bridge& operator=(const Bridge&) = delete;
  void* pointer;
};

std::vector<std::string> bridge_generators(void* pointer) {
  std::vector<std::string> result;
  for (std::size_t row = 0; row < width; ++row) {
    std::array<char, width + 2> text{};
    require(mx_generator(pointer, row, text.data(), text.size()) == 0,
            "bridge60 complete generator export");
    result.emplace_back(text.data());
  }
  return result;
}

void session_and_bridge_snapshot() {
  Session session(Session::Backend::Compact, width);
  session.apply(ghz_gates());
  session.apply({{MX_GATE_Z, 59, 0}});
  const std::vector<std::string> strings{std::string(width, 'X'),
      "-" + std::string(width, 'X'), pauli({{0, 'Z'}, {59, 'Z'}}), pauli({{59, 'Z'}})};
  const std::vector<double> expected{-1., 1., 1., 0.};
  require(session.query(strings) == expected, "session60 signed GHZ query");
  const auto saved = session.snapshot();
  const auto original_rows = generators(session);
  require(saved.qubits == width && saved.gates.size() == 61 && saved.revision == 2,
          "session60 snapshot metadata");
  require(session.state_bytes() == 1020 && session.observer_bytes() > 0 &&
          session.history_bytes() == 61 * sizeof(Gate), "session60 separate payload accounting");
  session.apply({{MX_GATE_X, 31, 0}, {MX_GATE_H, 59, 0}});
  const auto changed_epoch = session.epoch();
  session.restore(saved);
  require(session.query(strings) == expected && generators(session) == original_rows &&
          session.snapshot().gates == saved.gates && session.revision() == saved.revision &&
          session.epoch() == changed_epoch + 1, "session60 replay restoration");

  Bridge source, restored;
  std::vector<std::uint64_t> triples;
  for (const auto& gate : saved.gates) {
    triples.push_back(gate.operation); triples.push_back(gate.first); triples.push_back(gate.second);
  }
  require(mx_apply(source.pointer, triples.data(), saved.gates.size()) == 0, "bridge60 apply full-width batch");
  require(mx_backend(source.pointer) == MX_BACKEND_COMPACT && mx_qubits(source.pointer) == width,
          "bridge60 preserves selected backend/register");
  std::size_t count = 0; std::uint64_t revision = 0;
  require(mx_snapshot(source.pointer, nullptr, 0, &count, &revision) == 0 && count == 61,
          "bridge60 snapshot sizing");
  std::vector<std::uint64_t> exported(3 * count, 77);
  require(mx_snapshot(source.pointer, exported.data(), count - 1, &count, &revision) == -1 &&
          std::all_of(exported.begin(), exported.end(), [](std::uint64_t x) { return x == 77; }),
          "bridge60 short snapshot does not partially write");
  require(mx_snapshot(source.pointer, exported.data(), count, &count, &revision) == 0 &&
          exported == triples, "bridge60 snapshot retained high indices");
  require(mx_restore(restored.pointer, exported.data(), count, revision) == 0,
          "bridge60 restore accepted snapshot");
  std::vector<const char*> paulis;
  for (const auto& text : strings) paulis.push_back(text.c_str());
  std::vector<double> observed(paulis.size());
  require(mx_query(restored.pointer, paulis.data(), paulis.size(), observed.data()) == 0 &&
          observed == expected && bridge_generators(restored.pointer) == original_rows,
          "bridge60 restored signed full-width state");
  const auto before_epoch = mx_epoch(restored.pointer), before_rebuilds = mx_rebuilds(restored.pointer);
  const std::uint64_t bad[] = {MX_GATE_X, 31, 0, MX_GATE_CX, 59, 60};
  require(mx_apply(restored.pointer, bad, 2) == -1, "bridge60 rejects q60 in entire batch");
  require(mx_restore(restored.pointer, bad, 2, revision + 99) == -1, "bridge60 rejects invalid restore");
  require(mx_query(restored.pointer, paulis.data(), paulis.size(), observed.data()) == 0 &&
          observed == expected && bridge_generators(restored.pointer) == original_rows &&
          mx_revision(restored.pointer) == revision && mx_epoch(restored.pointer) == before_epoch &&
          mx_rebuilds(restored.pointer) == before_rebuilds && mx_history_count(restored.pointer) == count,
          "bridge60 rejection changed state, history, revision or observer");
}
}  // namespace

int main() {
  try {
    ghz_and_measurement();
    embedded_clifford_oracle();
    reduced_non_clifford();
    session_and_bridge_snapshot();
    std::cout.precision(17);
    std::cout << "PASS core_60 qubits=" << width << " oracle_qubits=" << active.size()
              << " physical_indices=0,1,31,32,53,59 checks=" << checks
              << " oracle_comparisons=" << oracle_comparisons
              << " measurement_branches=" << measurement_branches
              << " max_oracle_error=" << maximum_oracle_error
              << " tolerance=" << tolerance
              << " ghz60_phase=PASS ghz60_t=PASS snapshot_bridge=PASS q60_rejection=PASS\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL core_60 " << error.what() << '\n';
    return 1;
  }
}
