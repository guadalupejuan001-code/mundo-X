#pragma once

#include <cmath>
#include <cstddef>
#include <fstream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"

namespace mundo_x {

struct XasmRunResult {
  std::size_t qubits = 0;
  std::size_t state_bytes = 0;
  std::size_t instructions = 0;
  bool assertions_passed = true;
  std::string output;
  std::string backend = "compact";
  std::size_t logical_payload_bytes = 0;
  std::size_t components = 0;
  std::size_t t_gates = 0;
};

inline std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

inline XasmRunResult run_xasm(std::istream& input) {
  XasmRunResult result;
  std::unique_ptr<CompactCore> core;
  std::unique_ptr<ReducedExactTRepresentation> reduced;
  bool backend_declared = false;
  std::ostringstream out;

  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;

    const auto comment = line.find('#');
    if (comment != std::string::npos) line = line.substr(0, comment);
    line = trim(line);
    if (line.empty()) continue;

    std::istringstream in(line);
    std::string op;
    in >> op;

    try {
      const auto read_size = [&](const std::string& usage) {
        std::string token;
        if (!(in >> token) || token.empty())
          throw std::runtime_error("expected " + usage);
        std::size_t value = 0;
        for (const char digit : token) {
          if (digit < '0' || digit > '9')
            throw std::runtime_error("expected " + usage + " (unsigned decimal integer)");
          const auto part = static_cast<std::size_t>(digit - '0');
          if (value > (std::numeric_limits<std::size_t>::max() - part) / 10)
            throw std::runtime_error("integer out of range in " + usage);
          value = value * 10 + part;
        }
        return value;
      };
      const auto require_end = [&]() {
        std::string extra;
        if (in >> extra)
          throw std::runtime_error("unexpected argument '" + extra + "'");
      };

      if (op == "backend") {
        if (core || reduced)
          throw std::runtime_error("backend must be declared before qubits");
        if (backend_declared) throw std::runtime_error("backend already declared");
        std::string backend;
        if (!(in >> backend) || (backend != "compact" && backend != "reduced"))
          throw std::runtime_error("expected backend <compact|reduced>");
        require_end();
        result.backend = backend;
        backend_declared = true;
        continue;
      }

      if (op == "qubits") {
        if (core || reduced) throw std::runtime_error("qubits already declared");
        const auto n = read_size("qubits <n>");
        require_end();
        if (result.backend == "reduced") {
          if (n == 0 || n > 12)
            throw std::runtime_error("reduced XASM supports 1..12 qubits");
          reduced = std::make_unique<ReducedExactTRepresentation>(n);
        } else {
          if (n == 0) throw std::runtime_error("expected qubits <n> with n > 0");
          core = std::make_unique<CompactCore>(n);
        }
        result.qubits = n;
        continue;
      }

      if (!core && !reduced) throw std::runtime_error("declare qubits first");

      if (op == "h" || op == "x" || op == "z" || op == "s") {
        const auto q = read_size(op + " <q>");
        require_end();
        const auto apply = [&](auto& state) {
          if (op == "h") state.hadamard(q);
          else if (op == "x") state.pauli_x(q);
          else if (op == "z") state.pauli_z(q);
          else state.phase_s(q);
        };
        if (reduced) apply(*reduced);
        else apply(*core);
        ++result.instructions;
      } else if (op == "cx" || op == "cz") {
        const auto control = read_size(op + " <control> <target>");
        const auto target = read_size(op + " <control> <target>");
        require_end();
        const auto apply = [&](auto& state) {
          if (op == "cx") state.cnot(control, target);
          else state.controlled_z(control, target);
        };
        if (reduced) apply(*reduced);
        else apply(*core);
        ++result.instructions;
      } else if (op == "t") {
        const auto q = read_size("t <q>");
        require_end();
        if (!reduced) throw std::runtime_error("t requires backend reduced before qubits");
        if (reduced->t_count() >= 128)
          throw std::runtime_error("reduced XASM safety limit is 128 T gates");
        reduced->phase_t(q);
        ++result.instructions;
      } else if (op == "show") {
        require_end();
        out << "STATE\n";
        if (reduced) {
          out << "backend=reduced\n"
              << "components=" << reduced->component_count() << "\n"
              << "t_gates=" << reduced->t_count() << "\n"
              << "logical_payload_bytes=" << reduced->logical_payload_bytes() << "\n";
        } else {
          for (std::size_t r = 0; r < core->qubits(); ++r)
            out << core->generator_string(r) << "\n";
        }
      } else if (op == "bytes") {
        require_end();
        if (reduced) out << "LOGICAL_PAYLOAD_BYTES " << reduced->logical_payload_bytes() << "\n";
        else out << "STATE_BYTES " << core->state_bytes() << "\n";
      } else if (op == "assert") {
        std::string what;
        if (!(in >> what)) throw std::runtime_error("expected assert <ghz|one|fidelity|norm>");
        require_end();

        bool ok = false;
        if (what == "ghz" || what == "one") {
          if (!core) throw std::runtime_error("assert " + what + " requires backend compact");
          ok = what == "ghz" ? core->is_ghz_signature() : core->hello_is_one();
        } else if (what == "fidelity" || what == "norm") {
          if (!reduced) throw std::runtime_error("assert " + what + " requires backend reduced");
          const double value = what == "fidelity" ? reduced->fidelity() : reduced->reduced_norm();
          // DenseCore uses float amplitudes, matching the native experiment's tolerance.
          ok = std::isfinite(value) && std::abs(value - 1.0) <= 3e-5;
        } else {
          throw std::runtime_error("unknown assertion '" + what + "'");
        }

        out << "ASSERT " << what << " " << (ok ? "PASS" : "FAIL") << "\n";
        result.assertions_passed = result.assertions_passed && ok;
      } else {
        throw std::runtime_error("unknown instruction '" + op + "'");
      }
    } catch (const std::exception& e) {
      throw std::runtime_error("line " + std::to_string(line_number) + ": " + e.what());
    }
  }

  if (!core && !reduced) throw std::runtime_error("program did not declare qubits");

  if (reduced) {
    result.logical_payload_bytes = reduced->logical_payload_bytes();
    result.components = reduced->component_count();
    result.t_gates = reduced->t_count();
  } else {
    result.state_bytes = core->state_bytes();
  }
  result.output = out.str();
  return result;
}

inline XasmRunResult run_xasm_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open XASM program: " + path);
  return run_xasm(file);
}

}  // namespace mundo_x
