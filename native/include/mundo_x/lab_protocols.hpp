#pragma once

#include <cstddef>
#include <cstdint>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/lab_circuit.hpp"

namespace mundo_x {
namespace lab_protocol_detail {

struct Readout {
  std::size_t qubit;
  bool outcome;
  double probability;
  bool random;
};

// The structured experiment lets tests inspect the actual tableau and measured
// branches. No protocol decision is inferred from a JSON string.
struct Experiment {
  CompactCore state;
  std::mt19937_64 rng;
  std::ostringstream operations;
  std::vector<Readout> measurements;
  std::string title, explanation, expected, observed, syndrome;
  int corrected_qubit = -1;
  std::size_t oracle_calls = 0;
  bool deterministic = true;
  bool passed = false;

  Experiment(std::size_t qubits, std::uint64_t seed) : state(qubits), rng(seed) {
    operations.imbue(std::locale::classic());
  }

  void h(std::size_t q) { state.hadamard(q); operations << "h " << q << '\n'; }
  void x(std::size_t q) { state.pauli_x(q); operations << "x " << q << '\n'; }
  void z(std::size_t q) { state.pauli_z(q); operations << "z " << q << '\n'; }
  void cx(std::size_t a, std::size_t b) {
    state.cnot(a, b);
    operations << "cx " << a << ' ' << b << '\n';
  }
  bool measure(std::size_t q) {
    const auto result = state.measure_z(q, rng);
    operations << "measure " << q << '\n';
    measurements.push_back({q, result.outcome_one, result.probability, result.random});
    deterministic = deterministic && !result.random && result.probability == 1.0;
    return result.outcome_one;
  }
  void read_data(std::size_t count) {
    for (std::size_t q = 0; q < count; ++q)
      observed.push_back(measure(q) ? '1' : '0');
    passed = deterministic && observed == expected;
    for (std::size_t q = 0; q < count; ++q)
      passed = passed &&
          state.probability_one_z(q) == (expected[q] == '1' ? 1.0 : 0.0);
  }
};

inline std::string bits(int value, std::size_t width) {
  std::string output;
  for (std::size_t q = 0; q < width; ++q)
    output.push_back((value & (1 << (width - q - 1))) ? '1' : '0');
  return output;
}

inline Experiment execute(int protocol, int value, std::uint64_t seed) {
  if (protocol < 0 || protocol > 2)
    throw std::invalid_argument("lab protocol must be 0..2");
  const int maximum = protocol == 1 ? 15 : 3;
  if (value < 0 || value > maximum)
    throw std::invalid_argument("lab protocol value outside supported range");
  Experiment experiment(protocol == 0 ? 2 : 5, seed);

  if (protocol == 0) {
    experiment.title = "Codificacion superdensa";
    experiment.explanation =
        "Protocolo conocido: dos bits se codifican en una mitad de un par Bell "
        "compartido y se recuperan al decodificar. q0 es el bit mas significativo. "
        "Simulacion clasica en CPU con CompactCore; no se transmite hardware cuantico.";
    experiment.expected = bits(value, 2);
    experiment.h(0);
    experiment.cx(0, 1);
    if (value & 2) experiment.z(0);
    if (value & 1) experiment.x(0);
    experiment.cx(0, 1);
    experiment.h(0);
    experiment.read_data(2);
  } else if (protocol == 1) {
    experiment.title = "Bernstein-Vazirani";
    experiment.explanation =
        "Recupera un secreto de cuatro bits con una consulta logica al oraculo "
        "XOR, construido aqui con puertas CX. Los datos son q0..q3 y q4 es el "
        "ancilla |->. Es una simulacion clasica en CPU, no una demostracion "
        "de ventaja de hardware ni de un algoritmo nuevo.";
    experiment.expected = bits(value, 4);
    experiment.x(4);
    experiment.h(4);
    for (std::size_t q = 0; q < 4; ++q) experiment.h(q);
    // One logical XOR-oracle invocation; its native circuit has popcount(s) CXs.
    experiment.oracle_calls = 1;
    for (std::size_t q = 0; q < 4; ++q)
      if (value & (1 << (3 - q))) experiment.cx(q, 4);
    for (std::size_t q = 0; q < 4; ++q) experiment.h(q);
    experiment.read_data(4);
  } else {
    experiment.title = "Codigo de tres qubits contra bitflip";
    experiment.explanation =
        "Codigo de repeticion con entrada logica |1>: codifica, inyecta a lo "
        "sumo un error X, mide dos paridades y aplica la correccion indicada. "
        "Tras decodificar, los datos q0..q2 deben ser 100; q3 y q4 conservan "
        "el sindrome. No es correccion general de errores de fase o multiples errores.";
    experiment.expected = "100";
    experiment.x(0);
    experiment.cx(0, 1);
    experiment.cx(0, 2);
    if (value) experiment.x(static_cast<std::size_t>(value - 1));

    experiment.cx(0, 3);
    experiment.cx(1, 3);
    experiment.cx(1, 4);
    experiment.cx(2, 4);
    // These outcomes come from the actual tableau. The injected-error value
    // is deliberately not consulted when choosing the correction.
    const bool first = experiment.measure(3);
    const bool second = experiment.measure(4);
    experiment.syndrome.push_back(first ? '1' : '0');
    experiment.syndrome.push_back(second ? '1' : '0');
    if (first && second) experiment.corrected_qubit = 1;
    else if (first) experiment.corrected_qubit = 0;
    else if (second) experiment.corrected_qubit = 2;
    if (experiment.corrected_qubit >= 0)
      experiment.x(static_cast<std::size_t>(experiment.corrected_qubit));
    experiment.cx(0, 2);
    experiment.cx(0, 1);
    experiment.read_data(3);
  }
  return experiment;
}

}  // namespace lab_protocol_detail

// All state evolution and measurement use Mundo X's packed CompactCore.
// The canonical circuit result is replayed from the exact applied script,
// including the correction chosen from the measured syndrome. These protocols
// have deterministic data readouts, so replay preserves the measured branch.
inline std::string run_lab_protocol(int protocol, int value, std::size_t shots,
                                    std::uint64_t seed) {
  if (shots > 4096)
    throw std::invalid_argument("lab protocol shots must be 0..4096");
  auto experiment = lab_protocol_detail::execute(protocol, value, seed);
  const std::string script = experiment.operations.str();
  const std::string circuit = run_lab_circuit(
      experiment.state.qubits(), script, -1, shots, seed);
  std::ostringstream output;
  output.imbue(std::locale::classic());
  output << "{\"ok\":true,\"protocol\":" << protocol << ",\"value\":" << value
         << ",\"title\":";
  lab_detail::quoted(output, experiment.title);
  output << ",\"explanation\":";
  lab_detail::quoted(output, experiment.explanation);
  output << ",\"expected\":";
  lab_detail::quoted(output, experiment.expected);
  output << ",\"observed\":";
  lab_detail::quoted(output, experiment.observed);
  output << ",\"passed\":" << (experiment.passed ? "true" : "false")
         << ",\"script\":";
  lab_detail::quoted(output, script);
  if (protocol == 1) output << ",\"oracle_calls\":" << experiment.oracle_calls;
  if (protocol == 2) {
    output << ",\"logical_input\":\"1\",\"expected_data\":\"100\",\"syndrome\":";
    lab_detail::quoted(output, experiment.syndrome);
    output << ",\"corrected_qubit\":" << experiment.corrected_qubit;
  }
  output << ",\"engine\":\"CompactCore\",\"simulation\":\"classical_cpu\""
         << ",\"seed\":\"" << seed << "\",\"shots\":" << shots
         << ",\"result\":" << circuit << '}';
  return output.str();
}

}  // namespace mundo_x
