#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/xasm.hpp"
#include "mundo_x/teleport.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/mxlang.hpp"

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


static int t_experiment(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result = mundo_x::run_exact_t_experiment(qubits, t_gates);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << result.report;
    std::cout << std::fixed << std::setprecision(3)
              << "wall_time_ms=" << ms << "\n";
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "T EXPERIMENT ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int t_reduced_experiment(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result =
        mundo_x::run_reduced_exact_t_experiment(qubits, t_gates);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << result.report;
    std::cout << std::fixed << std::setprecision(3)
              << "wall_time_ms=" << ms << "\n";
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "REDUCED T EXPERIMENT ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int t_growth_trace(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto result =
        mundo_x::run_reduced_growth_trace(qubits, t_gates);
    std::cout << result.report;
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "T TRACE ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int magic_witness() {
  try {
    const auto result = mundo_x::run_magic_state_witness();
    std::cout << result.report;
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "MAGIC WITNESS ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int t_growth_trace_variant(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto result =
        mundo_x::run_reduced_growth_trace_variant(qubits, t_gates);
    std::cout << result.report;
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "T TRACE VARIANT ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int teleport() {
  const auto result = mundo_x::run_compact_teleportation();
  std::cout << result.report;
  return result.all_passed ? 0 : 1;
}

static void print_program_result(const mundo_x::XasmRunResult& result,
                                const std::string& kind,
                                const std::string& path, double ms) {
  std::cout << kind << " program=" << path << "\n";
  std::cout << "qubits=" << result.qubits
            << " instructions=" << result.instructions;
  if (result.backend == "reduced") {
    std::cout << " backend=reduced"
              << " components=" << result.components
              << " t_gates=" << result.t_gates
              << " logical_payload_bytes=" << result.logical_payload_bytes;
  } else {
    std::cout << " state_bytes=" << result.state_bytes;
  }
  std::cout << " ms=" << std::fixed << std::setprecision(3) << ms << "\n";
  std::cout << result.output;
}

static int run_program(const std::string& path) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result = mundo_x::run_xasm_file(path);
    const auto end = std::chrono::steady_clock::now();

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    print_program_result(result, "XASM", path, ms);

    return result.assertions_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "XASM ERROR: " << e.what() << "\n";
    return 1;
  }
}

static std::string mx_diagnostic(const std::string& path,
                                 const std::string& message) {
  if (message.compare(0, 5, "line ") == 0)
    return path + ":" + message.substr(5);
  return path + ": " + message;
}

static int compile_mx_program(const std::string& path) {
  try {
    const auto program = mundo_x::compile_mx_file(path);
    // Keep stdout suitable for redirection to a plain XASM file.
    std::cout << program.xasm;
    if (!std::cout) throw std::runtime_error("cannot write compiled XASM");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "MX ERROR " << mx_diagnostic(path, e.what()) << "\n";
    return 1;
  }
}

static int run_mx_program(const std::string& path) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto program = mundo_x::compile_mx_file(path);
    std::istringstream input(program.xasm);
    mundo_x::XasmRunResult result;
    try {
      result = mundo_x::run_xasm(input);
    } catch (const std::exception& e) {
      const std::string message = e.what();
      if (message.compare(0, 5, "line ") == 0) {
        std::istringstream location(message.substr(5));
        std::size_t generated_line = 0;
        char colon = '\0';
        if ((location >> generated_line >> colon) && colon == ':' &&
            generated_line > 0 && generated_line <= program.source_map.size()) {
          const auto& origin = program.source_map[generated_line - 1];
          std::string detail;
          std::getline(location, detail);
          throw std::runtime_error("line " + std::to_string(origin.line) +
                                   ":" + std::to_string(origin.column) +
                                   ":" + detail);
        }
      }
      throw;
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    print_program_result(result, "MX", path, ms);
    return result.assertions_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "MX ERROR " << mx_diagnostic(path, e.what()) << "\n";
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
        << "usage: mundo_x hello | teleport | magic-witness | "
           "t-experiment <qubits> <t_gates> | "
           "t-reduced-experiment <qubits> <t_gates> | "
           "t-trace <qubits> <t_gates> | "
           "t-trace-variant <qubits> <t_gates> | "
           "bench <qubits> [--dense] | "
           "cluster <rows> <cols> [--dense] | practice <qubits> | "
           "run <program.x> | compile <program.mx> | run-mx <program.mx>\n";
    return 2;
  }

  const std::string cmd = argv[1];

  if (cmd == "compile" || cmd == "run-mx") {
    if (argc != 3) return 2;
    return cmd == "compile" ? compile_mx_program(argv[2]) : run_mx_program(argv[2]);
  }

  if (cmd == "hello") return hello();
  if (cmd == "teleport") return teleport();
  if (cmd == "magic-witness") return magic_witness();

  if (cmd == "t-experiment") {
    if (argc < 4) return 2;
    const std::size_t n =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    const std::size_t t_gates =
        static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
    return t_experiment(n, t_gates);
  }

  if (cmd == "t-reduced-experiment") {
    if (argc < 4) return 2;
    const std::size_t n =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    const std::size_t t_gates =
        static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
    return t_reduced_experiment(n, t_gates);
  }

  if (cmd == "t-trace") {
    if (argc < 4) return 2;
    const std::size_t n =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    const std::size_t t_gates =
        static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
    return t_growth_trace(n, t_gates);
  }

  if (cmd == "t-trace-variant") {
    if (argc < 4) return 2;
    const std::size_t n =
        static_cast<std::size_t>(std::strtoull(argv[2], nullptr, 10));
    const std::size_t t_gates =
        static_cast<std::size_t>(std::strtoull(argv[3], nullptr, 10));
    return t_growth_trace_variant(n, t_gates);
  }

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
