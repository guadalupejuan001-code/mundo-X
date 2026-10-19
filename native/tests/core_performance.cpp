#include "mundo_x/pauli_observer.hpp"
#include <iostream>
#include <random>

namespace {
std::size_t checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

std::vector<std::string> generators(const mundo_x::CompactCore& core) {
  std::vector<std::string> result;
  for (std::size_t r = 0; r < core.qubits(); ++r)
    result.push_back(core.generator_string(r));
  return result;
}

void random_gate(mundo_x::CompactCore& core, std::mt19937_64& rng) {
  const auto n = core.qubits(), a = rng() % n;
  const auto b = n == 1 ? a : (a + 1 + rng() % (n - 1)) % n;
  switch (rng() % (n == 1 ? 4 : 6)) {
    case 0: core.hadamard(a); break;
    case 1: core.phase_s(a); break;
    case 2: core.pauli_x(a); break;
    case 3: core.pauli_z(a); break;
    case 4: core.cnot(a, b); break;
    case 5: core.controlled_z(a, b); break;
  }
}

void packed_export() {
  for (const std::size_t n : {1, 2, 20, 63, 64, 65, 127, 128, 129, 256}) {
    mundo_x::CompactCore core(n);
    std::mt19937_64 rng(0x5041434b4544ULL ^ n);
    for (unsigned round = 0; round < 8; ++round) {
      for (std::size_t step = 0; step < 4 * n; ++step) random_gate(core, rng);
      if (round & 1U) core.measure_z(rng() % n, rng);
      const auto before = generators(core);
      const mundo_x_bridge::PauliObserver observer(core);
      for (std::size_t r = 0; r < n; ++r) {
        const auto text = mundo_x_bridge::Pauli::parse(before[r], n);
        const auto packed = core.generator_packed(r);
        require(packed.x == text.x && packed.z == text.z && packed.phase == text.phase,
                "Packed export differs from signed ASCII generator");
        require(observer.expectation(text) == 1., "Stabilizer generator expectation changed");
      }
      require(generators(core) == before, "Export/observation mutated the core");
    }
    bool rejected = false;
    try { (void)core.generator_packed(n); }
    catch (const std::out_of_range&) { rejected = true; }
    require(rejected, "Packed export accepted an invalid row");
  }
}

// H/CX/H uses the unchanged single-qubit and CNOT primitives as an independent
// decomposition oracle for the new single-pass CZ. Compare every signed row,
// including partial words and control/target in the same or different words.
void direct_cz() {
  for (const std::size_t n : {2, 20, 63, 64, 65, 127, 128, 129, 256}) {
    mundo_x::CompactCore direct(n);
    std::mt19937_64 rng(0x435a444952454354ULL ^ n);
    for (std::size_t step = 0; step < 16 * n; ++step) random_gate(direct, rng);
    auto decomposed = direct;
    for (unsigned step = 0; step < 64; ++step) {
      const auto a = rng() % n, b = (a + 1 + rng() % (n - 1)) % n;
      direct.controlled_z(a, b);
      decomposed.hadamard(b); decomposed.cnot(a, b); decomposed.hadamard(b);
      for (std::size_t r = 0; r < n; ++r)
        require(direct.generator_string(r) == decomposed.generator_string(r),
                "CZ differs from H/CX/H");
      if ((step & 7U) == 0) {
        const auto q = rng() % n;
        direct.phase_s(q); decomposed.phase_s(q);
        direct.hadamard(q); decomposed.hadamard(q);
      }
    }
    const auto before = generators(direct);
    for (const auto& pair : {std::pair<std::size_t, std::size_t>{0, 0}, {0, n}, {n, 0}}) {
      bool rejected = false;
      try { direct.controlled_z(pair.first, pair.second); }
      catch (const std::exception&) { rejected = true; }
      require(rejected && generators(direct) == before, "Invalid CZ changed the state");
    }
  }
}

// Dense classical bit evolution supplies the expected computational-basis
// value. Noncanonical Z generators force elimination across word boundaries.
void basis_elimination() {
  for (const std::size_t n : {63, 64, 65, 127, 128, 129, 256}) {
    mundo_x::CompactCore core(n);
    std::vector<bool> bits(n);
    std::mt19937_64 rng(0x4241534953ULL ^ n);
    for (std::size_t q = 0; q < n; ++q)
      if ((bits[q] = (rng() & 1U) != 0)) core.pauli_x(q);
    for (std::size_t step = 0; step < 8 * n; ++step) {
      const auto a = rng() % n, b = (a + 1 + rng() % (n - 1)) % n;
      core.cnot(a, b); bits[b] = bits[b] != bits[a];
    }
    const auto before = generators(core);
    for (std::size_t q = 0; q < n; ++q)
      require(core.probability_one_z(q) == (bits[q] ? 1. : 0.),
              "Packed deterministic reduction changed basis probability");
    require(generators(core) == before, "Deterministic query mutated generators");
    core.canonicalize_computational_basis();
    for (std::size_t q = 0; q < n; ++q)
      require(core.computational_basis_bit(q) == bits[q],
              "Packed canonicalization changed basis value");
  }
}

// Clifford gates on the other qubits preserve the chosen target's Z value,
// while making its implicit stabilizer use X/Y generators and signed products.
void implicit_signed_z() {
  for (const std::size_t n : {2, 20, 63, 64, 65, 129, 256}) {
    for (const bool one : {false, true}) {
      mundo_x::CompactCore core(n);
      const auto target = n - 1;
      if (one) core.pauli_x(target);
      core.cnot(0, target);
      core.hadamard(0); core.phase_s(0);
      std::mt19937_64 rng(0x5349474e4544ULL ^ n);
      for (std::size_t i = 0; i < 4 * n; ++i) {
        const auto a = rng() % target;
        core.hadamard(a); core.phase_s(a);
        if (target > 1) core.cnot(a, (a + 1) % target);
      }
      require(core.probability_one_z(target) == (one ? 1. : 0.),
              "Implicit multiword signed Z reduction failed");
      const auto before = generators(core);
      const auto saved_rng = rng;
      const auto result = core.measure_z(target, rng);
      require(!result.random && result.outcome_one == one && rng == saved_rng &&
                  generators(core) == before,
              "Implicit deterministic measurement changed state/RNG");
    }
  }
}
} // namespace

int main() {
  try {
    packed_export(); direct_cz(); basis_elimination(); implicit_signed_z();
    require(mundo_x::CompactCore(130).state_bytes() == 6370,
            "Optimization changed compact state payload");
    std::cout << "PASS core_performance exact_checks=" << checks
              << " word_boundaries=63,64,65,127,128,129,256"
              << " packed_export=PASS direct_cz=PASS signed_elimination=PASS\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL core_performance " << error.what() << '\n';
    return 1;
  }
}
