#include <array>
#include <cmath>
#include <complex>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>
#include "mundo_x/exact_t_experiment.hpp"

namespace {
using Complex = std::complex<double>;
using Reduced = mundo_x::ReducedExactTRepresentation;
std::size_t checks = 0;
void require(bool ok, const std::string& message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
void close(Complex actual, Complex expected, const std::string& name, double tolerance = 2e-10) {
  require(std::isfinite(actual.real()) && std::isfinite(actual.imag()) && std::abs(actual - expected) <= tolerance,
          name + ": error=" + std::to_string(std::abs(actual - expected)));
}
template<class Fn> void throws_message(Fn fn, const std::string& message) {
  std::string actual;
  try { fn(); } catch (const std::exception& e) { actual = e.what(); }
  require(actual == message, "expected exception: " + message + "; got: " + actual);
}

// Independent complex<double> state-vector oracle. It uses neither DenseCore
// (which stores float) nor the stabilizer/coset helpers under test.
struct Oracle {
  unsigned n;
  std::vector<Complex> state;
  explicit Oracle(unsigned qubits) : n(qubits), state(std::size_t{1} << n) { state[0] = 1; }
  void unary(unsigned q, Complex a, Complex b, Complex c, Complex d) {
    const auto mask = std::size_t{1} << q;
    for (std::size_t i = 0; i < state.size(); ++i) if (!(i & mask)) {
      const auto before0 = state[i], before1 = state[i | mask];
      state[i] = a * before0 + b * before1;
      state[i | mask] = c * before0 + d * before1;
    }
  }
  void gate(unsigned op, unsigned a, unsigned b) {
    const double h = std::sqrt(0.5);
    switch (op) {
      case 0: unary(a, h, h, h, -h); break;
      case 1: unary(a, 1, 0, 0, Complex{0, 1}); break;
      case 2: unary(a, 0, 1, 1, 0); break;
      case 3: unary(a, 1, 0, 0, -1); break;
      case 4:
        for (std::size_t i = 0; i < state.size(); ++i)
          if (((i >> a) & 1U) && !((i >> b) & 1U)) std::swap(state[i], state[i | (std::size_t{1} << b)]);
        break;
      case 5:
        for (std::size_t i = 0; i < state.size(); ++i)
          if (((i >> a) & 1U) && ((i >> b) & 1U)) state[i] = -state[i];
        break;
      case 6: unary(a, 1, 0, 0, std::polar(1.0, std::acos(-1.0) / 4)); break;
      case 7: unary(a, 1, 0, 0, std::polar(1.0, -std::acos(-1.0) / 4)); break;
      default: throw std::logic_error("oracle gate");
    }
  }
  double norm() const { double out = 0; for (const auto& a : state) out += std::norm(a); return out; }
  Complex expectation(std::uint64_t x, std::uint64_t z, unsigned phase) const {
    const std::array<Complex, 4> phase_values{{{1,0},{0,1},{-1,0},{0,-1}}};
    Complex out{0,0};
    for (std::size_t source = 0; source < state.size(); ++source) {
      auto target = source;
      Complex amplitude = state[source] * phase_values[phase % 4];
      for (unsigned q = 0; q < n; ++q) {
        if (((z >> q) & 1U) && ((source >> q) & 1U)) amplitude = -amplitude;
        if ((x >> q) & 1U) target ^= std::size_t{1} << q;
      }
      out += std::conj(state[target]) * amplitude;
    }
    return out;
  }
};
void apply(Reduced& q, unsigned op, unsigned a, unsigned b) {
  switch (op) {
    case 0: q.hadamard(a); break;
    case 1: q.phase_s(a); break;
    case 2: q.pauli_x(a); break;
    case 3: q.pauli_z(a); break;
    case 4: q.cnot(a, b); break;
    case 5: q.controlled_z(a, b); break;
    case 6: q.phase_t(a); break;
    case 7: q.phase_t_dagger(a); break;
    default: throw std::logic_error("test gate");
  }
}
void analytic_tests() {
  const double h = std::sqrt(0.5);
  Reduced magic(1);
  magic.hadamard(0); magic.phase_t(0);
  close(magic.sparse_norm(), 1, "magic norm");
  close(magic.pauli_expectation(1,0), h, "magic X");
  close(magic.pauli_expectation(1,1,1), h, "magic Y=iXZ");
  close(magic.pauli_expectation(1,1), Complex{0,-h}, "magic anti-Hermitian XZ");
  close(magic.z_expectation(0), 0, "magic Z");
  magic.phase_t_dagger(0);
  close(magic.pauli_expectation(1,0), 1, "T then dagger identity");
  close(magic.pauli_expectation(1,1,1), 0, "T then dagger Y");
  require(magic.t_count() == 2, "T and dagger count as non-Clifford gates");
  require(magic.component_count() == 2, "inverse does not approximate-prune zero or tiny coefficients");
  Reduced bell(2);
  bell.hadamard(0); bell.cnot(0,1); bell.phase_t(0);
  close(bell.pauli_expectation(3,0), h, "Bell XX");
  close(bell.pauli_expectation(3,3,2), -h, "Bell YY");
  close(bell.pauli_expectation(3,1,1), h, "Bell YX");
  close(bell.pauli_expectation(3,2,1), h, "Bell XY");
  close(bell.pauli_expectation(0,3), 1, "Bell ZZ");
  for (unsigned q = 0; q < 2; ++q) close(bell.z_expectation(q), 0, "Bell local Z");
  Reduced high(63, 16);
  high.hadamard(62); high.phase_t_dagger(62);
  close(high.sparse_norm(), 1, "63-qubit sparse norm");
  close(high.pauli_expectation(std::uint64_t{1} << 62, std::uint64_t{1} << 62, 1), -h, "63-qubit sparse Y");
  close(high.z_expectation(61), 1, "untouched high-register Z");
  throws_message([&]{ high.dense_reference(); }, "dense validation safety limit is 23 qubits");
  throws_message([&]{ high.pauli_expectation(std::uint64_t{1} << 63,0); }, "Pauli mask outside qubit register");
  throws_message([&]{ high.z_expectation(63); }, "qubit");
}
void random_tests() {
  std::mt19937 rng(0x51a29);
  for (unsigned n = 1; n <= 5; ++n) for (unsigned trial = 0; trial < 5; ++trial) {
    Reduced reduced(n);
    Oracle oracle(n);
    const auto mask = (std::uint64_t{1} << n) - 1;
    for (unsigned step = 0; step < 48; ++step) {
      unsigned op = rng() % 8;
      const unsigned a = rng() % n, b = n == 1 ? 0 : (a + 1 + rng() % (n-1)) % n;
      if (n == 1 && (op == 4 || op == 5)) op = 7;
      apply(reduced, op, a, b); oracle.gate(op, a, b);
      close(reduced.sparse_norm(), oracle.norm(), "random norm");
      close(reduced.pauli_expectation(0,0), oracle.norm(), "identity is unnormalized norm");
      for (unsigned q = 0; q < n; ++q)
        close(reduced.z_expectation(q), oracle.expectation(0,std::uint64_t{1} << q,0), "random local Z");
      // Every pair of masks, including anti-Hermitian strings; vary phase so
      // both explicit Y and arbitrary i phases are checked independently.
      for (std::uint64_t x = 0; x <= mask; ++x) for (std::uint64_t z = 0; z <= mask; ++z) {
        const auto phase = static_cast<std::uint8_t>((x + 3*z + step) & 3U);
        close(reduced.pauli_expectation(x,z,phase), oracle.expectation(x,z,phase), "random all-Pauli expectation");
      }
    }
    const auto reference = reduced.dense_reference(), replay = reduced.dense_from_reduced();
    for (std::size_t i = 0; i < oracle.state.size(); ++i) {
      close(reference[i], oracle.state[i], "T-dagger gate-history replay", 5e-6);
      close(replay[i], oracle.state[i], "reduced dense replay", 5e-6);
    }
    close(reduced.reduced_norm(), reduced.sparse_norm(), "legacy dense norm agrees", 1e-5);
  }
}
void limits_and_cancellation() {
  throws_message([]{ Reduced invalid(0); }, "reduced representation supports 1..63 qubits");
  throws_message([]{ Reduced invalid(std::numeric_limits<std::size_t>::max()); }, "reduced representation supports 1..63 qubits");
  throws_message([]{ Reduced invalid(1, 0); }, "max_components must be positive");
  throws_message([]{ Reduced cancelled(1, 2, []{ return true; }); }, "reduced representation cancelled");
  Reduced guarded(2, 1);
  guarded.phase_t(0);  // Both branches merge, so a cap of one is sufficient.
  guarded.hadamard(0);
  const auto before = guarded.dense_reference();
  throws_message([&]{ guarded.phase_t(0); }, "reduced component limit reached");
  throws_message([&]{ guarded.phase_t_dagger(0); }, "reduced component limit reached");
  require(guarded.component_count() == 1 && guarded.t_count() == 1, "budget failure cannot commit keys/history");
  require(guarded.dense_reference() == before, "budget failure preserves dense history");
  close(guarded.pauli_expectation(1,0), 1, "budget failure preserves state");
  int remaining = -1;
  std::size_t calls = 0;
  Reduced q(10, 2048, [&]{ ++calls; return remaining >= 0 && remaining-- == 0; });
  for (unsigned i = 0; i < 10; ++i) { q.hadamard(i); q.phase_t(i); }
  require(q.component_count() == 1024, "adversarial product state contains 1024 stored keys");
  const auto previous = q.dense_from_reduced(), history = q.dense_reference();
  for (unsigned action = 0; action < 4; ++action) {
    remaining = 1; calls = 0;
    throws_message([&]{
      if (action == 0) q.pauli_x(3);
      else if (action == 1) q.phase_t_dagger(4);
      else if (action == 2) q.sparse_norm();
      else q.pauli_expectation(7,11,3);
    }, "reduced representation cancelled");
    require(calls == 2, "cancellation polled inside component traversal");
    remaining = -1;
    require(q.dense_from_reduced() == previous && q.dense_reference() == history, "mid-operation cancellation preserves state/history");
  }
  remaining = 0;
  throws_message([&]{ q.phase_s(0); }, "reduced representation cancelled");
  remaining = -1;
  close(q.sparse_norm(), 1, "cancelled operation leaves norm intact");
}
}  // namespace
int main() {
  try {
    analytic_tests(); random_tests(); limits_and_cancellation();
    std::cout << "PASS " << checks << " sparse observable, independent double oracle, limit and cancellation checks\n";
  } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
