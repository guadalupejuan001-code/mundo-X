#pragma once

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <locale>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/compact_core.hpp"

namespace mundo_x {
namespace lab_detail {

struct Operation {
  enum class Kind { H, X, Z, S, CX, CZ, Measure } kind;
  std::size_t a;
  std::size_t b;
};

inline std::size_t index(const std::string& token, std::size_t qubits,
                         std::size_t line) {
  std::size_t value = 0;
  for (const char c : token)
    if (c < '0' || c > '9')
      throw std::invalid_argument("line " + std::to_string(line) +
                                  ": expected unsigned decimal qubit index");
  const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
  if (token.empty() || parsed.ec != std::errc{} ||
      parsed.ptr != token.data() + token.size() || value >= qubits)
    throw std::invalid_argument("line " + std::to_string(line) +
                                ": qubit index outside circuit");
  return value;
}

inline std::vector<Operation> parse(std::size_t qubits, const std::string& script) {
  std::vector<Operation> operations;
  std::istringstream input(script);
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    const auto comment = line.find('#');
    if (comment != std::string::npos) line.resize(comment);
    std::istringstream tokens(line);
    std::string name, a, b, extra;
    if (!(tokens >> name)) continue;
    Operation::Kind kind;
    bool binary = false;
    if (name == "h") kind = Operation::Kind::H;
    else if (name == "x") kind = Operation::Kind::X;
    else if (name == "z") kind = Operation::Kind::Z;
    else if (name == "s") kind = Operation::Kind::S;
    else if (name == "cx") { kind = Operation::Kind::CX; binary = true; }
    else if (name == "cz") { kind = Operation::Kind::CZ; binary = true; }
    else if (name == "measure") kind = Operation::Kind::Measure;
    else throw std::invalid_argument("line " + std::to_string(line_number) +
                                     ": unknown lab operation");
    if (!(tokens >> a) || (binary && !(tokens >> b)) || (tokens >> extra))
      throw std::invalid_argument("line " + std::to_string(line_number) +
                                  ": wrong operation argument count");
    const auto first = index(a, qubits, line_number);
    const auto second = binary ? index(b, qubits, line_number) : 0;
    if (binary && first == second)
      throw std::invalid_argument("line " + std::to_string(line_number) +
                                  ": two-qubit operation needs distinct qubits");
    if (operations.size() == 128)
      throw std::length_error("lab circuit exceeds 128 operations");
    operations.push_back({kind, first, second});
  }
  return operations;
}

inline void quoted(std::ostream& output, const std::string& value) {
  static constexpr char hex[] = "0123456789abcdef";
  output << '"';
  for (const unsigned char c : value) {
    if (c == '"' || c == '\\') output << '\\' << static_cast<char>(c);
    else if (c < 0x20)
      output << "\\u00" << hex[c >> 4] << hex[c & 15];
    else output << static_cast<char>(c);
  }
  output << '"';
}

}  // namespace lab_detail

// Small interactive Clifford laboratory using the original packed tableau.
// Parse and validate the entire script, including operations beyond a prefix.
// steps=-1 executes all operations; a nonnegative value executes at most that
// many. Histogram indices display q0 as the leftmost bit. Sampling copies the
// prefix state and uses one continuous seeded RNG, preserving its snapshot.
inline std::string run_lab_circuit(std::size_t qubits, const std::string& script,
                                   int steps, std::size_t shots,
                                   std::uint64_t seed) {
  const auto start = std::chrono::steady_clock::now();
  if (qubits < 1 || qubits > 8)
    throw std::invalid_argument("lab circuit supports 1..8 qubits");
  if (script.size() > 16 * 1024)
    throw std::length_error("lab script exceeds 16 KiB");
  if (steps < -1 || steps > 128)
    throw std::invalid_argument("lab steps must be -1 or 0..128");
  if (shots > 4096)
    throw std::invalid_argument("lab shots must be 0..4096");
  const auto operations = lab_detail::parse(qubits, script);
  const std::size_t executed = steps < 0 ? operations.size() :
      std::min(operations.size(), static_cast<std::size_t>(steps));
  CompactCore state(qubits);
  std::mt19937_64 rng(seed);
  struct Measurement {
    std::size_t qubit;
    bool outcome;
    double probability;
  };
  std::vector<Measurement> measurements;
  for (std::size_t i = 0; i < executed; ++i) {
    const auto& operation = operations[i];
    using Kind = lab_detail::Operation::Kind;
    switch (operation.kind) {
      case Kind::H: state.hadamard(operation.a); break;
      case Kind::X: state.pauli_x(operation.a); break;
      case Kind::Z: state.pauli_z(operation.a); break;
      case Kind::S: state.phase_s(operation.a); break;
      case Kind::CX: state.cnot(operation.a, operation.b); break;
      case Kind::CZ: state.controlled_z(operation.a, operation.b); break;
      case Kind::Measure: {
        const auto result = state.measure_z(operation.a, rng);
        measurements.push_back({operation.a, result.outcome_one, result.probability});
        break;
      }
    }
  }

  std::vector<double> probability_one;
  std::vector<std::string> generators;
  for (std::size_t q = 0; q < qubits; ++q) {
    probability_one.push_back(state.probability_one_z(q));
    generators.push_back(state.generator_string(q));
  }
  std::vector<int> counts(shots ? std::size_t{1} << qubits : 0, 0);
  for (std::size_t shot = 0; shot < shots; ++shot) {
    CompactCore sample(state);
    std::size_t index = 0;
    for (std::size_t q = 0; q < qubits; ++q)
      index = (index << 1) | std::size_t(sample.measure_z(q, rng).outcome_one);
    ++counts[index];
  }

  std::ostringstream output;
  output.imbue(std::locale::classic());
  output.imbue(std::locale::classic());
  output << std::setprecision(17)
         << "{\"ok\":true,\"qubits\":" << qubits << ",\"step\":" << executed
         << ",\"total_gates\":" << operations.size() << ",\"prob_one\":[";
  for (std::size_t i = 0; i < probability_one.size(); ++i) {
    if (i) output << ',';
    output << probability_one[i];
  }
  output << "],\"counts\":[";
  for (std::size_t i = 0; i < counts.size(); ++i) {
    if (i) output << ',';
    output << counts[i];
  }
  output << "],\"generators\":[";
  for (std::size_t i = 0; i < generators.size(); ++i) {
    if (i) output << ',';
    lab_detail::quoted(output, generators[i]);
  }
  output << "],\"state_bytes\":" << state.state_bytes() << ",\"measurements\":[";
  for (std::size_t i = 0; i < measurements.size(); ++i) {
    if (i) output << ',';
    const auto& measurement = measurements[i];
    output << "{\"qubit\":" << measurement.qubit
           << ",\"outcome\":" << (measurement.outcome ? 1 : 0)
           << ",\"probability\":" << measurement.probability << '}';
  }
  const auto elapsed = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  output << "],\"elapsed_ms\":" << elapsed << ",\"engine\":\"CompactCore\""
         << ",\"seed\":\"" << seed << "\",\"shots\":" << shots << '}';
  return output.str();
}

}  // namespace mundo_x
