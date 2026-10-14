#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/xasm.hpp"

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


static int cluster(std::size_t rows, std::size_t cols, bool run_dense) {
  if (rows == 0 || cols == 0) return 2;
  const std::size_t n = rows * cols;

  std::cout << std::fixed << std::setprecision(3);

  {
    const auto start = std::chrono::steady_clock::now();

    CompactCore q(n);
    for (std::size_t v = 0; v < n; ++v) q.hadamard(v);

    for (std::size_t r = 0; r < rows; ++r) {
      for (std::size_t col = 0; col < cols; ++col) {
        const std::size_t v = r * cols + col;
        if (col + 1 < cols) q.controlled_z(v, v + 1);
        if (r + 1 < rows) q.controlled_z(v, v + cols);
      }
    }

    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "CLUSTER_COMPACT rows=" << rows
              << " cols=" << cols
              << " qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " signature="
              << (q.is_cluster_signature(rows, cols) ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  if (run_dense) {
    if (n > 20) {
      std::cout << "CLUSTER_DENSE skipped=safety_limit_20_qubits\n";
      return 0;
    }

    const auto start = std::chrono::steady_clock::now();

    DenseCore q(n);
    for (std::size_t v = 0; v < n; ++v) q.hadamard(v);

    for (std::size_t r = 0; r < rows; ++r) {
      for (std::size_t col = 0; col < cols; ++col) {
        const std::size_t v = r * cols + col;
        if (col + 1 < cols) q.controlled_z(v, v + 1);
        if (r + 1 < rows) q.controlled_z(v, v + cols);
      }
    }

    const bool ok = q.cluster_is_valid(rows, cols);

    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "CLUSTER_DENSE rows=" << rows
              << " cols=" << cols
              << " qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " signature=" << (ok ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  return 0;
}

static int run_program(const std::string& path) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result = mundo_x::run_xasm_file(path);
    const auto end = std::chrono::steady_clock::now();

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "XASM program=" << path << "\n";
    std::cout << "qubits=" << result.qubits
              << " instructions=" << result.instructions
              << " state_bytes=" << result.state_bytes
              << " ms=" << std::fixed << std::setprecision(3) << ms << "\n";
    std::cout << result.output;

    return result.assertions_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "XASM ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int practice(std::size_t n) {
  CompactCore q(n);

  std::cout << "MUNDO X PRACTICE\n";
  std::cout << "commands: h q | x q | z q | s q | cx c t | show | bytes | reset | quit\n";

  std::string line;

  while (true) {
    std::cout << "mx> " << std::flush;
    if (!std::getline(std::cin, line)) break;

    std::istringstream in(line);
    std::string op;
    in >> op;

    try {
      if (op.empty()) continue;
      if (op == "quit" || op == "exit") break;

      if (op == "h") {
        std::size_t a;
        in >> a;
        q.hadamard(a);
      } else if (op == "x") {
        std::size_t a;
        in >> a;
        q.pauli_x(a);
      } else if (op == "z") {
        std::size_t a;
        in >> a;
        q.pauli_z(a);
      } else if (op == "s") {
        std::size_t a;
        in >> a;
        q.phase_s(a);
      } else if (op == "cx") {
        std::size_t c, t;
        in >> c >> t;
        q.cnot(c, t);
      } else if (op == "show") {
        for (std::size_t r = 0; r < q.qubits(); ++r) {
          std::cout << q.generator_string(r) << "\n";
        }
      } else if (op == "bytes") {
        std::cout << "state_bytes=" << q.state_bytes() << "\n";
      } else if (op == "reset") {
        q = CompactCore(n);
        std::cout << "reset\n";
      } else {
        std::cout << "unknown command\n";
      }
    } catch (const std::exception& e) {
      std::cout << "error: " << e.what() << "\n";
    }
  }

  return 0;
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::cerr
        << "usage: mundo_x hello | bench <qubits> [--dense] | "
           "cluster <rows> <cols> [--dense] | practice <qubits> | "
           "run <program.x>\n";
    return 2;
  }

  const std::string cmd = argv[1];

  if (cmd == "hello") return hello();

  if (cmd == "cluster") {
    if (argc < 4) return 2;
    const std::size_t rows =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    const std::size_t cols =
        static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));

    bool dense = false;
    for (int i = 4; i < argc; ++i) {
      if (std::string(argv[i]) == "--dense") dense = true;
    }

    return cluster(rows, cols, dense);
  }

  if (cmd == "run") {
    if (argc < 3) return 2;
    return run_program(argv[2]);
  }

  if (cmd == "practice") {
    if (argc < 3) return 2;
    const std::size_t n =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    return practice(n);
  }

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
