#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"

using mundo_x::CompactCore;
using mundo_x::DenseCore;

static double mib(std::size_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

static int hello() {
  DenseCore dense(1);
  dense.hadamard(0);
  dense.pauli_z(0);
  dense.hadamard(0);

  CompactCore compact(1);
  compact.hadamard(0);
  compact.pauli_z(0);
  compact.hadamard(0);

  const bool dense_ok = dense.probability(1) > 0.999999f;
  const bool compact_ok = compact.hello_is_one();

  std::cout << "HELLO_MUNDO_X dense=" << (dense_ok ? "PASS" : "FAIL")
            << " compact=" << (compact_ok ? "PASS" : "FAIL") << "\n";

  return (dense_ok && compact_ok) ? 0 : 1;
}

static int bench(std::size_t n, bool run_dense) {
  std::cout << std::fixed << std::setprecision(3);

  {
    const auto start = std::chrono::steady_clock::now();

    CompactCore q(n);
    q.hadamard(0);
    for (std::size_t i = 1; i < n; ++i) q.cnot(i - 1, i);

    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "COMPACT qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " ghz=" << (q.is_ghz_signature() ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  if (run_dense) {
    if (n >= 63) {
      std::cout << "DENSE skipped=dimension_overflow\n";
      return 0;
    }

    const auto start = std::chrono::steady_clock::now();

    DenseCore q(n);
    q.hadamard(0);
    for (std::size_t i = 1; i < n; ++i) q.cnot(i - 1, i);

    const auto end = std::chrono::steady_clock::now();

    const std::size_t last = (std::size_t{1} << n) - 1;
    const bool ok =
        std::abs(q.probability(0) - 0.5f) < 1e-5f &&
        std::abs(q.probability(last) - 0.5f) < 1e-5f;

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "DENSE qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " ghz=" << (ok ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr << "usage: mundo_x hello | bench <qubits> [--dense] | practice <qubits>\n";
    return 2;
  }

  const std::string cmd = argv[1];

  if (cmd == "hello") return hello();

  if (cmd == "bench") {
    if (argc < 3) return 2;

    const std::size_t n =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));

    bool dense = false;
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--dense") dense = true;
    }

    return bench(n, dense);
  }

  return 2;
}
