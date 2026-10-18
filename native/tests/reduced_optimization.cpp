#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/exact_t_experiment.hpp"

namespace {
using Complex = std::complex<double>;
using Op = mundo_x::ExactExperimentOp;

// Independent double-precision statevector. In particular, CZ is applied
// directly rather than through the production H/CX/H implementation.
class Reference {
 public:
  explicit Reference(std::size_t n) : state(std::size_t{1} << n) {
    state[0] = 1.0;
  }

  void apply(Op op, std::size_t a, std::size_t b = 0) {
    const auto bit = std::size_t{1} << a;
    const double s = 1.0 / std::sqrt(2.0);
    for (std::size_t i = 0; i < state.size(); ++i) {
      const bool one = (i & bit) != 0;
      switch (op) {
        case Op::H:
          if (!one) {
            const auto lo = state[i];
            const auto hi = state[i | bit];
            state[i] = (lo + hi) * s;
            state[i | bit] = (lo - hi) * s;
          }
          break;
        case Op::X:
          if (!one) std::swap(state[i], state[i | bit]);
          break;
        case Op::Z:
          if (one) state[i] = -state[i];
          break;
        case Op::S:
          if (one) state[i] *= Complex{0.0, 1.0};
          break;
        case Op::T:
          if (one) state[i] *= Complex{s, s};
          break;
        case Op::CX:
          if (one && !(i & (std::size_t{1} << b)))
            std::swap(state[i], state[i | (std::size_t{1} << b)]);
          break;
        case Op::CZ:
          if (one && (i & (std::size_t{1} << b))) state[i] = -state[i];
          break;
      }
    }
  }

  std::vector<Complex> state;
};

void apply(mundo_x::ReducedExactTRepresentation& q, Op op,
           std::size_t a, std::size_t b = 0) {
  switch (op) {
    case Op::H: q.hadamard(a); break;
    case Op::S: q.phase_s(a); break;
    case Op::X: q.pauli_x(a); break;
    case Op::Z: q.pauli_z(a); break;
    case Op::T: q.phase_t(a); break;
    case Op::CX: q.cnot(a, b); break;
    case Op::CZ: q.controlled_z(a, b); break;
  }
}

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

void compare(const mundo_x::ReducedExactTRepresentation& q,
             const Reference& ref, std::size_t step,
             double tolerance = 5e-6) {
  const auto actual = q.dense_from_reduced();
  double error = 0.0;
  for (std::size_t i = 0; i < actual.size(); ++i)
    error += std::norm(actual[i] - ref.state[i]);
  require(std::sqrt(error) < tolerance,
          "independent reference mismatch at step " + std::to_string(step));
  require(q.component_count() <= actual.size(), "quotient dimension exceeded");
}

std::uint64_t random_word(std::uint64_t& seed) {
  seed ^= seed << 13U;
  seed ^= seed >> 7U;
  seed ^= seed << 17U;
  return seed;
}

void correctness() {
  std::size_t checked = 0;
  for (std::size_t n = 1; n <= 6; ++n) {
    for (std::size_t trial = 0; trial < 12; ++trial) {
      mundo_x::ReducedExactTRepresentation q(n);
      Reference ref(n);
      std::uint64_t seed = 0x9E3779B185EBCA87ULL ^ (n * 31 + trial);
      std::size_t t_count = 0;
      for (std::size_t step = 0; step < 96; ++step) {
        auto op = static_cast<Op>(random_word(seed) % 7);
        const std::size_t a = random_word(seed) % n;
        std::size_t b = random_word(seed) % n;
        if (n == 1 && (op == Op::CX || op == Op::CZ)) op = Op::T;
        if (n > 1 && a == b) b = (a + 1) % n;
        apply(q, op, a, b);
        ref.apply(op, a, b);
        if (op == Op::T) ++t_count;
        require(q.t_count() == t_count, "T count changed");
        compare(q, ref, step);
        ++checked;
      }
    }
  }

  // Repeated diagonal rotations, immediate reads, and inverse rotations test
  // interference rather than just basis-state probabilities.
  for (std::size_t n = 1; n <= 6; ++n) {
    mundo_x::ReducedExactTRepresentation q(n);
    Reference ref(n);
    for (std::size_t a = 0; a < n; ++a) {
      apply(q, Op::H, a);
      ref.apply(Op::H, a);
      for (std::size_t k = 0; k < 8; ++k) {
        apply(q, Op::T, a);
        ref.apply(Op::T, a);
        compare(q, ref, k);
        ++checked;
      }
    }
  }

  // A long, noncommuting sequence checks that both reconstruction and the
  // production validation oracle retain double precision after many gates.
  mundo_x::ReducedExactTRepresentation long_run(1);
  Reference long_ref(1);
  for (std::size_t k = 0; k < 5000; ++k) {
    for (const auto op : {Op::H, Op::T}) {
      apply(long_run, op, 0);
      long_ref.apply(op, 0);
    }
  }
  compare(long_run, long_ref, 10000, 1e-10);
  const auto long_oracle = long_run.dense_reference();
  double oracle_error = 0.0;
  for (std::size_t i = 0; i < long_oracle.size(); ++i)
    oracle_error += std::norm(long_oracle[i] - long_ref.state[i]);
  require(std::sqrt(oracle_error) < 1e-10, "long double oracle mismatch");
  ++checked;

  // The packed representation must also handle bit 62 without a dense oracle.
  mundo_x::ReducedExactTRepresentation wide(63);
  wide.hadamard(62);
  wide.phase_t(62);
  wide.controlled_z(62, 0);
  wide.cnot(62, 61);
  wide.phase_s(62);
  wide.pauli_x(61);
  wide.pauli_z(62);
  require(wide.component_count() == 2 && wide.t_count() == 1,
          "high-bit sparse evolution failed");
  std::cout << "PASS checked_states=" << checked << " high_bit=62\n";
}

void benchmark(std::size_t n, std::size_t t_gates, std::size_t repetitions) {
  require(n >= 1 && n <= 18 && t_gates <= 512 && repetitions <= 32,
          "benchmark safety limit");
  for (std::size_t rep = 0; rep < repetitions; ++rep) {
    const auto start = std::chrono::steady_clock::now();
    mundo_x::ReducedExactTRepresentation q(n);
    q.hadamard(0);
    for (std::size_t i = 1; i < n; ++i) {
      q.cnot(i - 1, i);
      if ((i & 1U) == 0U) q.phase_s(i);
    }
    for (std::size_t k = 0; k < t_gates; ++k) {
      const auto a = k % n;
      const auto b = (a + 1) % n;
      q.phase_t(a);
      q.hadamard(a);
      if (n > 1) q.cnot(a, b);
      if (k & 1U) q.phase_s(b);
    }
    const auto end = std::chrono::steady_clock::now();
    std::cout << "qubits=" << n << " T=" << t_gates
              << " components=" << q.component_count()
              << " logical_bytes=" << q.logical_payload_bytes()
              << " evolution_ms="
              << std::chrono::duration<double, std::milli>(end - start).count()
              << '\n';
  }
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc > 1 && std::string(argv[1]) == "bench") {
      benchmark(argc > 2 ? std::stoull(argv[2]) : 12,
                argc > 3 ? std::stoull(argv[3]) : 64,
                argc > 4 ? std::stoull(argv[4]) : 3);
    } else {
      correctness();
    }
  } catch (const std::exception& e) {
    std::cerr << "FAIL " << e.what() << '\n';
    return EXIT_FAILURE;
  }
}
