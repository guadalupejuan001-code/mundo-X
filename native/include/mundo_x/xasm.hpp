#pragma once

#include <cstddef>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"

namespace mundo_x {

struct XasmRunResult {
  std::size_t qubits = 0;
  std::size_t state_bytes = 0;
  std::size_t instructions = 0;
  bool assertions_passed = true;
  std::string output;
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

    if (op == "qubits") {
      if (core) throw std::runtime_error("line " + std::to_string(line_number) + ": qubits already declared");
      std::size_t n = 0;
      if (!(in >> n) || n == 0) {
        throw std::runtime_error("line " + std::to_string(line_number) + ": expected qubits <n>");
      }
      core = std::make_unique<CompactCore>(n);
      result.qubits = n;
      continue;
    }

    if (!core) {
      throw std::runtime_error("line " + std::to_string(line_number) + ": declare qubits first");
    }

    if (op == "h") {
      std::size_t q;
      if (!(in >> q)) throw std::runtime_error("line " + std::to_string(line_number) + ": expected h <q>");
      core->hadamard(q);
      ++result.instructions;
    } else if (op == "x") {
      std::size_t q;
      if (!(in >> q)) throw std::runtime_error("line " + std::to_string(line_number) + ": expected x <q>");
      core->pauli_x(q);
      ++result.instructions;
    } else if (op == "z") {
      std::size_t q;
      if (!(in >> q)) throw std::runtime_error("line " + std::to_string(line_number) + ": expected z <q>");
      core->pauli_z(q);
      ++result.instructions;
    } else if (op == "s") {
      std::size_t q;
      if (!(in >> q)) throw std::runtime_error("line " + std::to_string(line_number) + ": expected s <q>");
      core->phase_s(q);
      ++result.instructions;
    } else if (op == "cx") {
      std::size_t c, t;
      if (!(in >> c >> t)) throw std::runtime_error("line " + std::to_string(line_number) + ": expected cx <control> <target>");
      core->cnot(c, t);
      ++result.instructions;
    } else if (op == "show") {
      out << "STATE\n";
      for (std::size_t r = 0; r < core->qubits(); ++r) {
        out << core->generator_string(r) << "\n";
      }
    } else if (op == "bytes") {
      out << "STATE_BYTES " << core->state_bytes() << "\n";
    } else if (op == "assert") {
      std::string what;
      in >> what;

      bool ok = false;
      if (what == "ghz") {
        ok = core->is_ghz_signature();
      } else if (what == "one") {
        ok = core->hello_is_one();
      } else {
        throw std::runtime_error("line " + std::to_string(line_number) + ": unknown assertion");
      }

      out << "ASSERT " << what << " " << (ok ? "PASS" : "FAIL") << "\n";
      result.assertions_passed = result.assertions_passed && ok;
    } else {
      throw std::runtime_error("line " + std::to_string(line_number) + ": unknown instruction '" + op + "'");
    }
  }

  if (!core) throw std::runtime_error("program did not declare qubits");

  result.state_bytes = core->state_bytes();
  result.output = out.str();
  return result;
}

inline XasmRunResult run_xasm_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open XASM program: " + path);
  return run_xasm(file);
}

}  // namespace mundo_x
