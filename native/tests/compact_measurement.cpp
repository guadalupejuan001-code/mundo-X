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

#include "mundo_x/compact_core.hpp"

namespace {
using Complex = std::complex<double>;
using State = std::vector<Complex>;
using Matrix = std::array<std::array<Complex, 2>, 2>;
std::size_t checks = 0, measurements = 0, random_measurements = 0;
constexpr double tolerance = 2e-11;

void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

// Test-only numerical oracle. Single-qubit gates are generic 2x2 products;
// controlled gates are direct permutations/phases, not tableau updates.
struct Reference {
  explicit Reference(std::size_t n) : state(std::size_t{1} << n) { state[0] = 1.; }
  State state;
  void single(std::size_t q, const Matrix& matrix) {
    const auto bit = std::size_t{1} << q;
    for (std::size_t i = 0; i < state.size(); ++i) {
      if (i & bit) continue;
      const std::array<Complex, 2> input{{state[i], state[i | bit]}};
      std::array<Complex, 2> output{};
      for (std::size_t a = 0; a < 2; ++a)
        for (std::size_t b = 0; b < 2; ++b) output[a] += matrix[a][b] * input[b];
      state[i] = output[0]; state[i | bit] = output[1];
    }
  }
  void apply(char op, std::size_t a, std::size_t b = 0) {
    const Complex zero{0, 0}, one{1, 0};
    const double s = 1 / std::sqrt(2.);
    if (op == 'H') single(a, Matrix{{{{s, s}}, {{s, -s}}}});
    else if (op == 'S') single(a, Matrix{{{{one, zero}}, {{zero, Complex{0, 1}}}}});
    else if (op == 'X') single(a, Matrix{{{{zero, one}}, {{one, zero}}}});
    else if (op == 'Z') single(a, Matrix{{{{one, zero}}, {{zero, Complex{-1, 0}}}}});
    else {
      State output(state.size());
      const auto abit = std::size_t{1} << a, bbit = std::size_t{1} << b;
      for (std::size_t i = 0; i < state.size(); ++i) {
        const auto j = op == 'C' && (i & abit) ? i ^ bbit : i;
        output[j] += op == 'V' && (i & abit) && (i & bbit) ? -state[i] : state[i];
      }
      state = std::move(output);
    }
  }
  double probability(std::size_t q) const {
    double total = 0, one = 0;
    for (std::size_t i = 0; i < state.size(); ++i) {
      const double mass = std::norm(state[i]);
      total += mass;
      if (i & (std::size_t{1} << q)) one += mass;
    }
    return one / total;
  }
  void project(std::size_t q, bool outcome) {
    double mass = 0;
    for (std::size_t i = 0; i < state.size(); ++i) {
      if (static_cast<bool>(i & (std::size_t{1} << q)) != outcome) state[i] = 0;
      mass += std::norm(state[i]);
    }
    require(mass > 0, "oracle projected an impossible branch");
    for (auto& amplitude : state) amplitude /= std::sqrt(mass);
  }
};

void gate(mundo_x::CompactCore& core, Reference& ref, char op,
          std::size_t a, std::size_t b = 0) {
  if (op == 'H') core.hadamard(a);
  else if (op == 'S') core.phase_s(a);
  else if (op == 'X') core.pauli_x(a);
  else if (op == 'Z') core.pauli_z(a);
  else if (op == 'C') core.cnot(a, b);
  else if (op == 'V') core.controlled_z(a, b);
  else throw std::logic_error("invalid test gate");
  ref.apply(op, a, b);
}

std::vector<std::string> generators(const mundo_x::CompactCore& core) {
  std::vector<std::string> rows;
  for (std::size_t row = 0; row < core.qubits(); ++row) rows.push_back(core.generator_string(row));
  return rows;
}

// Applying each signed Pauli to the reference state checks full stabilizer
// information, including Y phases, rather than just the measured marginals.
void verify(const mundo_x::CompactCore& core, const Reference& ref) {
  const auto n = core.qubits();
  std::vector<std::uint64_t> binary_rows;
  State projected = ref.state;
  double reference_norm = 0;
  for (const auto amplitude : ref.state) reference_norm += std::norm(amplitude);
  for (std::size_t q = 0; q < n; ++q) {
    const double actual = core.probability_one_z(q);
    require(actual == 0 || actual == .5 || actual == 1, "Born query outside exact Clifford values");
    require(std::abs(actual - ref.probability(q)) < tolerance, "independent Z probability mismatch");
  }
  for (const auto& row : generators(core)) {
    require(row.size() == n + 1 && (row[0] == '+' || row[0] == '-'), "invalid generator");
    std::uint64_t bits = 0;
    State acted(projected.size());
    for (std::size_t i = 0; i < projected.size(); ++i) {
      std::size_t target = i;
      Complex phase = row[0] == '-' ? -1. : 1.;
      for (std::size_t q = 0; q < n; ++q) {
        const bool one = (i & (std::size_t{1} << q)) != 0;
        const char pauli = row[q + 1];
        if (pauli == 'X' || pauli == 'Y') target ^= std::size_t{1} << q;
        if (pauli == 'Z' && one) phase = -phase;
        if (pauli == 'Y') phase *= one ? Complex{0, -1} : Complex{0, 1};
      }
      acted[target] += phase * projected[i];
    }
    for (std::size_t i = 0; i < projected.size(); ++i) projected[i] = (projected[i] + acted[i]) * .5;
    for (std::size_t q = 0; q < n; ++q) {
      if (row[q + 1] == 'X' || row[q + 1] == 'Y') bits |= std::uint64_t{1} << q;
      if (row[q + 1] == 'Z' || row[q + 1] == 'Y') bits |= std::uint64_t{1} << (q + n);
    }
    binary_rows.push_back(bits);
  }
  // Independent generators make the stabilizer projector rank one. Its overlap
  // with the oracle is therefore the state fidelity, up to global phase.
  std::size_t rank = 0;
  for (std::size_t bit = 0; bit < 2 * n; ++bit) {
    std::size_t pivot = rank;
    while (pivot < n && !(binary_rows[pivot] & (std::uint64_t{1} << bit))) ++pivot;
    if (pivot == n) continue;
    std::swap(binary_rows[pivot], binary_rows[rank]);
    for (std::size_t r = rank + 1; r < n; ++r)
      if (binary_rows[r] & (std::uint64_t{1} << bit)) binary_rows[r] ^= binary_rows[rank];
    ++rank;
  }
  require(rank == n, "measurement lost independent generators");
  Complex overlap{};
  for (std::size_t i = 0; i < projected.size(); ++i) overlap += std::conj(ref.state[i]) * projected[i];
  require(std::abs(overlap / reference_norm - Complex{1, 0}) < tolerance, "independent stabilizer-projector fidelity mismatch");
}

bool measure(mundo_x::CompactCore& core, Reference& ref, std::size_t q,
             std::mt19937_64& rng) {
  verify(core, ref);
  const double p = ref.probability(q);
  const bool random = p > tolerance && p < 1 - tolerance;
  require(!random || std::abs(p - .5) < tolerance, "oracle is not a Clifford state");
  auto expected_rng = rng;
  const bool expected = random ? (expected_rng() & 1U) != 0 : p > .5;
  const auto before = generators(core);
  const auto result = core.measure_z(q, rng);
  ++measurements;
  if (random) ++random_measurements;
  require(result.outcome_one == expected, "seeded Born outcome mismatch");
  require(result.probability == (random ? .5 : 1.), "selected-branch probability mismatch");
  require(result.random == random, "random/deterministic classification mismatch");
  require(rng == expected_rng, "measurement consumed an incorrect number of RNG words");
  if (!random) require(generators(core) == before, "deterministic measurement mutated tableau");
  ref.project(q, expected);
  verify(core, ref);
  const auto repeated_before = generators(core);
  const auto repeated_rng = rng;
  const auto repeated = core.measure_z(q, rng);
  ++measurements;
  require(repeated.outcome_one == expected && !repeated.random && repeated.probability == 1., "repeated measurement is not deterministic");
  require(rng == repeated_rng && generators(core) == repeated_before, "repeated deterministic measurement changed RNG/tableau");
  return expected;
}

void named_cases() {
  std::mt19937_64 rng(0x424f524eULL);
  {
    mundo_x::CompactCore core(1); Reference ref(1);
    gate(core, ref, 'H', 0); gate(core, ref, 'Z', 0); gate(core, ref, 'H', 0);
    require(measure(core, ref, 0, rng), "HZH interference did not produce one");
  }
  {
    mundo_x::CompactCore core(2); Reference ref(2);
    gate(core, ref, 'H', 0); gate(core, ref, 'C', 0, 1);
    const bool first = measure(core, ref, 0, rng);
    require(measure(core, ref, 1, rng) == first, "Bell outcomes are not correlated");
  }
  for (const bool one : {false, true}) {
    mundo_x::CompactCore core(2); Reference ref(2);
    if (one) gate(core, ref, 'X', 1);
    gate(core, ref, 'C', 0, 1); gate(core, ref, 'H', 0); gate(core, ref, 'S', 0);
    // Generators are YI and +/-YZ: Z1 requires their signed product.
    const std::string explicit_z = one ? "-IZ" : "+IZ";
    const auto rows = generators(core);
    require(std::find(rows.begin(), rows.end(), explicit_z) == rows.end(), "combined-generator fixture became trivial");
    require(measure(core, ref, 1, rng) == one, "signed Y-generator product gave wrong deterministic result");
    measure(core, ref, 0, rng);
  }
  {
    mundo_x::CompactCore first(2), second(2); Reference a(2), b(2);
    std::mt19937_64 ra(73), rb(73);
    for (std::size_t q = 0; q < 2; ++q) {
      gate(first, a, 'H', q); gate(second, b, 'H', q);
      gate(first, a, 'S', q); gate(second, b, 'S', q);
    }
    for (std::size_t q = 0; q < 2; ++q)
      require(measure(first, a, q, ra) == measure(second, b, q, rb), "equal seeds are not reproducible");
    require(ra == rb && generators(first) == generators(second), "reproducible runs diverged");
    const auto rows = generators(first); const auto saved_rng = ra;
    bool rejected_query = false, rejected_measurement = false;
    try { (void)first.probability_one_z(2); } catch (const std::out_of_range&) { rejected_query = true; }
    try { (void)first.measure_z(2, ra); } catch (const std::out_of_range&) { rejected_measurement = true; }
    require(rejected_query && rejected_measurement, "invalid index was not rejected");
    require(ra == saved_rng && generators(first) == rows, "rejected index mutated RNG/tableau");
  }
}

void random_cases() {
  const std::string operations = "HSXZCV";
  for (std::size_t n = 1; n <= 6; ++n) {
    for (std::size_t trial = 0; trial < 5; ++trial) {
      mundo_x::CompactCore core(n); Reference ref(n);
      std::mt19937_64 circuit_rng(0x4d454153555245ULL ^ (n * 31 + trial));
      std::mt19937_64 measurement_rng(0x424f524e5aULL ^ trial);
      for (std::size_t round = 0; round < 8; ++round) {
        for (std::size_t step = 0; step < (round == 0 ? 48U : 3U); ++step) {
          char op = operations[circuit_rng() % operations.size()];
          const auto a = circuit_rng() % n;
          auto b = circuit_rng() % n;
          if (n == 1 && (op == 'C' || op == 'V')) op = 'H';
          if (n > 1 && b == a) b = (a + 1) % n;
          gate(core, ref, op, a, b);
        }
        measure(core, ref, circuit_rng() % n, measurement_rng);
      }
    }
  }
}
}  // namespace

int main() {
  try {
    named_cases(); random_cases();
    require(random_measurements > 0 && random_measurements < measurements, "measurement paths were not exercised");
    std::cout << "PASS compact_measurement checks=" << checks << " measurements=" << measurements
              << " random=" << random_measurements << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL compact_measurement " << error.what() << '\n';
    return 1;
  }
}
