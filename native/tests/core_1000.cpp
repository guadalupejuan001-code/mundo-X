// Full-register Clifford validation for the 1,000-qubit application profile.
// No 2^1000 statevector is allocated. Analytic GHZ1000 witnesses cover all
// connected sites; a separate eight-site numerical oracle checks multiword
// gates and signed Pauli phases at non-contiguous physical indices.
#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/engine_session.hpp"
#include "mundo_x/graphics_core_bridge.h"
#include "mundo_x/pauli_observer.hpp"

#include <algorithm>
#include <array>
#include <bitset>
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
constexpr std::size_t width = 1000;
constexpr std::array<std::size_t, 8> active{{0, 31, 32, 63, 64, 511, 512, 999}};
constexpr double tolerance = 1e-10;
using Complex = std::complex<double>;
using State = std::vector<Complex>;
using Session = mundo_x::EngineSession;
using Gate = Session::Gate;
std::size_t checks = 0, comparisons = 0, branches = 0;
double maximum_error = 0.;

void require(bool condition, const std::string& context) {
  ++checks;
  if (!condition) throw std::runtime_error(context);
}

void near(double actual, double expected, const std::string& context) {
  require(std::isfinite(actual) && std::abs(actual - expected) <= tolerance, context);
}

void compare(double actual, double expected, const std::string& context) {
  ++comparisons;
  maximum_error = std::max(maximum_error, std::abs(actual - expected));
  near(actual, expected, context);
}

template<class Action>
void rejects(Action action) {
  bool caught = false;
  try { action(); } catch (const std::out_of_range&) { caught = true; }
  require(caught, "q1000 is outside a 1000-qubit register");
}

std::vector<std::string> generators(const mundo_x::CompactCore& core) {
  std::vector<std::string> rows;
  rows.reserve(width);
  for (std::size_t row = 0; row < width; ++row) rows.push_back(core.generator_string(row));
  return rows;
}

std::string single(std::size_t q, char axis = 'Z') {
  std::string word(width, 'I'); word.at(q) = axis; return word;
}

double expectation(const mundo_x_bridge::PauliObserver& observer, const std::string& word) {
  return observer.expectation(mundo_x_bridge::Pauli::parse(word, width));
}

std::vector<Gate> ghz_gates() {
  std::vector<Gate> result{{MX_GATE_H, 0, 0}};
  for (std::size_t q = 1; q < width; ++q) result.push_back({MX_GATE_CX, q - 1, q});
  return result;
}

void verify_ghz(const mundo_x::CompactCore& core, bool negative) {
  const auto before = generators(core);
  const mundo_x_bridge::PauliObserver observer(core);
  std::string all_x(width, 'X'), yy = all_x;
  yy[63] = yy[999] = 'Y';
  near(expectation(observer, all_x), negative ? -1. : 1., "GHZ1000 global X phase");
  near(expectation(observer, "-" + all_x), negative ? 1. : -1., "GHZ1000 signed global X");
  near(expectation(observer, yy), negative ? 1. : -1., "GHZ1000 multiword Y signs");
  // These 999 correlations plus global X specify the entire GHZ stabilizer
  // state, including all intermediate sites, rather than just two endpoints.
  for (std::size_t q = 1; q < width; ++q) {
    auto zz = single(q); zz[0] = 'Z';
    near(expectation(observer, zz), 1., "GHZ1000 every site correlates with q0");
  }
  for (const auto q : active)
    near(core.probability_one_z(q), .5, "GHZ1000 boundary-bit Born probability");
  require(generators(core) == before, "GHZ1000 observation changed state");
}

void full_register_and_measurement() {
  mundo_x::CompactCore core(width);
  core.hadamard(0);
  for (std::size_t q = 1; q < width; ++q) core.cnot(q - 1, q);
  require(core.qubits() == width && core.is_ghz_signature(), "connected GHZ1000 signature");
  require(core.state_bytes() == 257000, "GHZ1000 tableau uses 16 packed words per row");
  verify_ghz(core, false);
  core.pauli_z(999);
  verify_ghz(core, true);

  for (const bool one : {false, true}) {
    auto branch = core;
    require(branch.measure_z_forced(999, one), "GHZ1000 forced random branch");
    ++branches;
    const mundo_x_bridge::PauliObserver observer(branch);
    for (std::size_t q = 0; q < width; ++q)
      near(expectation(observer, single(q)), one ? -1. : 1., "GHZ1000 all projected bits agree");
    near(expectation(observer, std::string(width, 'X')), 0., "GHZ1000 projection removes coherence");
    for (const auto q : active)
      near(branch.probability_one_z(q), one ? 1. : 0., "GHZ1000 projected boundary-bit probability");
  }
  bool saw_zero = false, saw_one = false;
  for (std::uint64_t seed = 0; seed < 4; ++seed) {
    auto first = core, second = core;
    std::mt19937_64 rng(seed), same(seed), predicted(seed);
    const bool wanted = (predicted() & 1U) != 0;
    const auto a = first.measure_z(999, rng), b = second.measure_z(999, same);
    require(a.random && b.random && a.probability == .5 && b.probability == .5 &&
            a.outcome_one == wanted && b.outcome_one == wanted && rng == same && rng == predicted &&
            generators(first) == generators(second), "GHZ1000 reproducible seeded collapse");
    saw_zero |= !wanted; saw_one |= wanted;
    const auto repeated = first.measure_z(999, rng);
    require(!repeated.random && repeated.probability == 1. && repeated.outcome_one == wanted &&
            rng == predicted, "GHZ1000 repeated measurement preserves RNG");
  }
  require(saw_zero && saw_one, "GHZ1000 seeded branches cover both outcomes");
  const auto before = generators(core);
  std::mt19937_64 rng(1000); const auto saved_rng = rng;
  rejects([&] { core.hadamard(1000); }); rejects([&] { core.phase_s(1000); });
  rejects([&] { core.pauli_x(1000); }); rejects([&] { core.pauli_z(1000); });
  rejects([&] { core.cnot(999, 1000); }); rejects([&] { core.cnot(1000, 999); });
  rejects([&] { core.controlled_z(999, 1000); }); rejects([&] { core.controlled_z(1000, 999); });
  rejects([&] { (void)core.probability_one_z(1000); });
  rejects([&] { (void)core.measure_z(1000, rng); });
  rejects([&] { (void)core.measure_z_forced(1000, true); });
  require(generators(core) == before && rng == saved_rng, "invalid q1000 changed state/RNG");
}

// Direct 2x2 Pauli action on the user's DenseCore64 amplitudes. No production
// packed-Pauli parser, stabilizer elimination or observer calculates this sum.
double reference_expectation(const State& state, const std::string& local) {
  Complex sum{};
  double norm = 0.;
  for (std::size_t source = 0; source < state.size(); ++source) {
    auto target = source;
    Complex phase = local[0] == '-' ? -1. : 1.;
    for (std::size_t q = 0; q < active.size(); ++q) {
      const auto bit = std::size_t{1} << q;
      const bool one = (source & bit) != 0;
      switch (local[q + 1]) {
        case 'I': break;
        case 'X': target ^= bit; break;
        case 'Y': target ^= bit; phase *= Complex{0., one ? -1. : 1.}; break;
        case 'Z': if (one) phase = -phase; break;
        default: throw std::logic_error("oracle Pauli character");
      }
    }
    sum += std::conj(state[target]) * phase * state[source];
    norm += std::norm(state[source]);
  }
  require(norm > 0. && std::abs(sum.imag()) <= tolerance, "oracle norm/Hermiticity");
  return sum.real() / norm;
}

std::string embed(const std::string& local) {
  std::string full(width + 1, 'I'); full[0] = local[0];
  for (std::size_t q = 0; q < active.size(); ++q) full[active[q] + 1] = local[q + 1];
  return full;
}

void verify_embedded(const mundo_x::CompactCore& core, const State& state, std::uint64_t seed) {
  const auto rows = generators(core);
  std::array<int, width> mapped{}; mapped.fill(-1);
  for (std::size_t q = 0; q < active.size(); ++q) mapped[active[q]] = static_cast<int>(q);
  std::vector<std::bitset<2 * width>> labels(width);
  for (std::size_t r = 0; r < width; ++r) {
    const auto& row = rows[r];
    std::string local(active.size() + 1, 'I'); local[0] = row[0];
    bool zero = false;
    for (std::size_t q = 0; q < width; ++q) {
      const char p = row[q + 1];
      if (p == 'X' || p == 'Y') labels[r].set(q);
      if (p == 'Z' || p == 'Y') labels[r].set(width + q);
      if (mapped[q] >= 0) local[static_cast<std::size_t>(mapped[q]) + 1] = p;
      else if (p == 'X' || p == 'Y') zero = true;
    }
    compare(zero ? 0. : reference_expectation(state, local), 1.,
            "1000-site signed generator vs eight-site oracle with 992 idle |0> sites");
  }
  // Independent bitset rank check ensures the generator assertions identify a
  // unique full 1000-site state; this is polynomial storage, not a statevector.
  std::size_t rank = 0;
  for (std::size_t c = 0; c < 2 * width && rank < width; ++c) {
    std::size_t pivot = rank;
    while (pivot < width && !labels[pivot][c]) ++pivot;
    if (pivot == width) continue;
    std::swap(labels[rank], labels[pivot]);
    for (std::size_t r = rank + 1; r < width; ++r)
      if (labels[r][c]) labels[r] ^= labels[rank];
    ++rank;
  }
  require(rank == width, "1000-site stabilizers lost independent rank");
  const mundo_x_bridge::PauliObserver observer(core);
  std::mt19937_64 rng(seed);
  for (std::size_t query = 0; query < 256; ++query) {
    std::string local(active.size() + 1, 'I'); local[0] = query & 1U ? '-' : '+';
    const auto code = rng();
    for (std::size_t q = 0; q < active.size(); ++q) local[q + 1] = "IXYZ"[(code >> (2 * q)) & 3U];
    compare(expectation(observer, embed(local)), reference_expectation(state, local),
            "1000-site multiword Pauli vs independent oracle");
  }
  require(generators(core) == rows, "1000-site oracle observation mutated state");
}

template<class Core>
void gate(Core& core, std::uint64_t op, std::size_t a, std::size_t b) {
  switch (op) {
    case MX_GATE_H: core.hadamard(a); break;
    case MX_GATE_S: core.phase_s(a); break;
    case MX_GATE_X: core.pauli_x(a); break;
    case MX_GATE_Z: core.pauli_z(a); break;
    case MX_GATE_CX: core.cnot(a, b); break;
    case MX_GATE_CZ: core.controlled_z(a, b); break;
    case MX_GATE_Y: core.pauli_z(a); core.pauli_x(a); break;
    default: throw std::logic_error("test gate");
  }
}

void multiword_oracle() {
  for (std::uint64_t trial = 0; trial < 3; ++trial) {
    mundo_x::CompactCore core(width);
    mundo_x::DenseCore64 reference(active.size());
    // Seeded mixing plus guaranteed cross-word CZ/CX gates reaches all of the
    // selected 31/32, 63/64, 511/512 and 999 boundaries.
    for (std::size_t q = 0; q < active.size(); ++q) {
      core.hadamard(active[q]); reference.hadamard(q);
      core.phase_s(active[q]); reference.phase_s(q);
      if (q) { core.controlled_z(active[q - 1], active[q]); reference.controlled_z(q - 1, q); }
    }
    std::mt19937_64 rng(0x100031326364ULL + trial);
    for (std::size_t step = 0; step < 96; ++step) {
      const auto op = rng() % 7, a = rng() % active.size();
      const auto b = (a + 1 + rng() % (active.size() - 1)) % active.size();
      gate(core, op, active[a], active[b]); gate(reference, op, a, b);
    }
    verify_embedded(core, reference.state(), 1000 + trial);
    for (const bool one : {false, true}) {
      std::string local(active.size() + 1, 'I'); local[0] = '+'; local.back() = 'Z';
      const double p_one = (1. - reference_expectation(reference.state(), local)) / 2.;
      const double probability = one ? p_one : 1. - p_one;
      if (probability <= tolerance) continue;
      auto branch = core;
      const bool random = p_one > tolerance && p_one < 1. - tolerance;
      require(branch.measure_z_forced(999, one) == random, "multiword oracle branch classification");
      State projected = reference.state(); double mass = 0.;
      for (std::size_t i = 0; i < projected.size(); ++i) {
        if (static_cast<bool>(i & (std::size_t{1} << (active.size() - 1))) != one) projected[i] = 0.;
        mass += std::norm(projected[i]);
      }
      require(mass > tolerance, "multiword oracle impossible projection");
      for (auto& amplitude : projected) amplitude /= std::sqrt(mass);
      verify_embedded(branch, projected, 2000 + trial + (one ? 99 : 0));
      ++branches;
    }
  }
}

struct Handle {
  Handle() : pointer(mx_create(MX_BACKEND_COMPACT, width)) { require(pointer != nullptr, "create bridge1000"); }
  ~Handle() { mx_destroy(pointer); }
  Handle(const Handle&) = delete;
  Handle& operator=(const Handle&) = delete;
  void* pointer;
};

std::vector<std::string> bridge_rows(void* pointer) {
  std::vector<std::string> rows;
  for (std::size_t q = 0; q < width; ++q) {
    std::array<char, width + 2> row{};
    require(mx_generator(pointer, q, row.data(), row.size()) == 0, "bridge1000 generator export");
    rows.emplace_back(row.data());
  }
  return rows;
}

void session_bridge_and_restore() {
  Session original(Session::Backend::Compact, width);
  original.apply(ghz_gates()); original.apply({{MX_GATE_Z, 999, 0}});
  const auto saved = original.snapshot();
  Session restored(Session::Backend::Compact, width); restored.restore(saved);
  auto endpoint_zz = single(999); endpoint_zz[0] = 'Z';
  const std::vector<std::string> paulis{std::string(width, 'X'), "-" + std::string(width, 'X'),
                                      endpoint_zz, single(999)};
  const std::vector<double> expected{-1., 1., 1., 0.};
  require(restored.query(paulis) == expected && restored.snapshot().gates == saved.gates &&
          restored.snapshot().qubits == width && restored.revision() == saved.revision,
          "EngineSession1000 full-width replay");
  for (std::size_t q = 0; q < width; ++q)
    require(original.generator(q) == restored.generator(q), "EngineSession1000 restored generator");

  Handle source, target;
  std::vector<std::uint64_t> triples;
  for (const auto& g : saved.gates) { triples.push_back(g.operation); triples.push_back(g.first); triples.push_back(g.second); }
  require(mx_apply(source.pointer, triples.data(), saved.gates.size()) == 0 &&
          mx_backend(source.pointer) == MX_BACKEND_COMPACT && mx_qubits(source.pointer) == width,
          "C ABI selects CompactCore1000");
  std::size_t count = 0; std::uint64_t revision = 0;
  require(mx_snapshot(source.pointer, nullptr, 0, &count, &revision) == 0 && count == 1001,
          "C ABI1000 snapshot metadata");
  std::vector<std::uint64_t> exported(3 * count, 77);
  require(mx_snapshot(source.pointer, exported.data(), count - 1, &count, &revision) == -1 &&
          std::all_of(exported.begin(), exported.end(), [](std::uint64_t x) { return x == 77; }),
          "C ABI1000 short snapshot does not partially write");
  require(mx_snapshot(source.pointer, exported.data(), count, &count, &revision) == 0 &&
          exported == triples && mx_restore(target.pointer, exported.data(), count, revision) == 0,
          "C ABI1000 snapshot/restore preserves q999");
  const auto original_rows = bridge_rows(source.pointer);
  require(bridge_rows(target.pointer) == original_rows, "C ABI1000 restored all packed words");
  std::vector<const char*> words; for (const auto& word : paulis) words.push_back(word.c_str());
  std::vector<double> values(words.size());
  require(mx_query(target.pointer, words.data(), words.size(), values.data()) == 0 && values == expected,
          "C ABI1000 restored signed expectations");
  require(mx_state_bytes(target.pointer) == 257000 &&
          mx_observer_bytes(target.pointer) == width * (32 * sizeof(std::uint64_t) + sizeof(unsigned) + sizeof(std::size_t)) &&
          mx_history_bytes(target.pointer) == count * sizeof(Gate),
          "C ABI1000 separate logical payload accounting");
  const auto epoch = mx_epoch(target.pointer), rebuilds = mx_rebuilds(target.pointer);
  const std::uint64_t bad[] = {MX_GATE_X, 999, 0, MX_GATE_CX, 999, 1000};
  require(mx_apply(target.pointer, bad, 2) == -1 &&
          mx_restore(target.pointer, bad, 2, revision + 3) == -1, "C ABI1000 rejects q1000 batch/restore");
  require(mx_query(target.pointer, words.data(), words.size(), values.data()) == 0 && values == expected &&
          bridge_rows(target.pointer) == original_rows && mx_revision(target.pointer) == revision &&
          mx_epoch(target.pointer) == epoch && mx_rebuilds(target.pointer) == rebuilds &&
          mx_history_count(target.pointer) == count, "C ABI1000 rejection preserves full session");
}
}  // namespace

int main() {
  try {
    full_register_and_measurement(); multiword_oracle(); session_bridge_and_restore();
    std::cout.precision(17);
    std::cout << "PASS core_1000 qubits=" << width << " oracle_qubits=" << active.size()
              << " physical_indices=0,31,32,63,64,511,512,999 checks=" << checks
              << " oracle_comparisons=" << comparisons << " measurement_branches=" << branches
              << " max_oracle_error=" << maximum_error << " tolerance=" << tolerance
              << " ghz1000_phase=PASS snapshot_bridge=PASS q1000_rejection=PASS"
              << " dense1000_allocated=false backend=compact-clifford\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL core_1000 " << error.what() << '\n'; return 1;
  }
}
