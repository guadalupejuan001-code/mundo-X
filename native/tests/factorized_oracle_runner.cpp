// Text protocol runner for the independent NumPy oracle. This file does not
// contain a second implementation of the production evolution algorithm.
#include <algorithm>
#include <complex>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/factorized_core.hpp"

namespace {
void gate(mundo_x::FactorizedCore& core, const std::string& op,
          std::size_t a, std::size_t b) {
  if (op == "H") core.hadamard(a);
  else if (op == "S") core.phase_s(a);
  else if (op == "T") core.phase_t(a);
  else if (op == "X") core.pauli_x(a);
  else if (op == "Z") core.pauli_z(a);
  else if (op == "CX") core.cnot(a, b);
  else if (op == "CZ") core.controlled_z(a, b);
  else throw std::invalid_argument("unknown gate: " + op);
}

void complex_json(std::complex<double> value) {
  std::cout << '[' << value.real() << ',' << value.imag() << ']';
}

void log_snapshot(const mundo_x::FactorizedCore& core, const std::string& id,
                  const std::string& label, const std::string& pattern) {
  std::vector<bool> bits(core.qubits());
  for (std::size_t q = 0; q < bits.size(); ++q)
    bits[q] = pattern == "one" || (pattern == "third" && q % 3 == 0);
  const auto result = core.log_amplitude(bits);
  std::cout << "{\"case\":\"" << id << "\",\"label\":\"" << label
            << "\",\"log_query\":true,\"qubits\":" << core.qubits()
            << ",\"log_zero\":" << (result.zero ? "true" : "false")
            << ",\"log_magnitude\":";
  if (result.zero) std::cout << "null";
  else std::cout << result.log_magnitude;
  std::cout << ",\"phase\":" << result.phase << ",\"raw_amplitude\":";
  complex_json(core.amplitude(bits));
  std::cout << ",\"factors\":" << core.factor_count()
            << ",\"amplitudes\":" << core.amplitude_count()
            << ",\"logical_bytes\":" << core.logical_amplitude_bytes()
            << ",\"probability_one\":[";
  const std::vector<std::size_t> queried{0, core.qubits()/2, core.qubits()-1};
  for (std::size_t index = 0; index < queried.size(); ++index) {
    if (index) std::cout << ',';
    std::cout << core.probability_one(queried[index]);
  }
  std::cout << "]}\n";
}

void snapshot(const mundo_x::FactorizedCore& core, const std::string& id,
              const std::string& label, int rejected, int split) {
  const auto state = core.dense_state(12);
  std::cout << "{\"case\":\"" << id << "\",\"label\":\"" << label
            << "\",\"rejected\":" << rejected << ",\"split\":" << split
            << ",\"factors\":" << core.factor_count()
            << ",\"largest\":" << core.largest_factor_qubits()
            << ",\"amplitudes\":" << core.amplitude_count()
            << ",\"logical_bytes\":" << core.logical_amplitude_bytes()
            << ",\"partition\":[";
  const auto partition = core.block_partition();
  for (std::size_t i = 0; i < partition.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << '[';
    for (std::size_t j = 0; j < partition[i].size(); ++j) {
      if (j) std::cout << ',';
      std::cout << partition[i][j];
    }
    std::cout << ']';
  }
  std::cout << "],\"factor_qubits\":[";
  for (std::size_t q = 0; q < core.qubits(); ++q) {
    if (q) std::cout << ',';
    auto members = core.factor_qubits(q);
    std::sort(members.begin(), members.end());
    std::cout << '[';
    for (std::size_t j = 0; j < members.size(); ++j) {
      if (j) std::cout << ',';
      std::cout << members[j];
    }
    std::cout << ']';
  }
  std::cout << "],\"factor_qubit_counts\":[";
  for (std::size_t q = 0; q < core.qubits(); ++q) {
    if (q) std::cout << ',';
    std::cout << core.factor_qubit_count(q);
  }
  std::cout << "],\"same_factor\":[";
  for (std::size_t a = 0; a < core.qubits(); ++a) {
    for (std::size_t b = 0; b < core.qubits(); ++b) {
      if (a || b) std::cout << ',';
      std::cout << (core.same_factor(a, b) ? "true" : "false");
    }
  }
  std::cout << "],\"state\":[";
  for (std::size_t i = 0; i < state.size(); ++i) {
    if (i) std::cout << ',';
    complex_json(state[i]);
  }
  std::cout << "],\"probability_one\":[";
  for (std::size_t q = 0; q < core.qubits(); ++q) {
    if (q) std::cout << ',';
    std::cout << core.probability_one(q);
  }
  std::cout << "],\"expectation_z\":[";
  for (std::size_t q = 0; q < core.qubits(); ++q) {
    if (q) std::cout << ',';
    std::cout << core.expectation_z(q);
  }
  std::cout << "],\"queried_amplitudes\":[";
  const std::vector<std::size_t> indexes{0, 1, state.size() / 3,
                                         state.size() - 1};
  for (std::size_t k = 0; k < indexes.size(); ++k) {
    if (k) std::cout << ',';
    std::vector<bool> bits(core.qubits());
    for (std::size_t q = 0; q < core.qubits(); ++q)
      bits[q] = (indexes[k] >> q) & 1U;
    complex_json(core.amplitude(bits));
  }
  std::cout << "]}\n";
}

void invalid_query(const mundo_x::FactorizedCore& core,
                   const std::string& kind, std::size_t argument) {
  if (kind == "AMPLITUDE") (void)core.amplitude(std::vector<bool>(argument));
  else if (kind == "LOG") (void)core.log_amplitude(std::vector<bool>(argument));
  else if (kind == "PROB") (void)core.probability_one(argument);
  else if (kind == "EXPECTZ") (void)core.expectation_z(argument);
  else if (kind == "FACTOR") (void)core.factor_qubits(argument);
  else if (kind == "SAME") (void)core.same_factor(0, argument);
  else throw std::invalid_argument("unknown query kind");
}
int run_protocol() {
  std::cout << std::setprecision(17);
  std::string line;
  try {
    while (std::getline(std::cin, line)) {
      if (line.empty()) continue;
      std::istringstream header(line);
      std::string marker, id;
      std::size_t n = 0, max_block = 0;
      header >> marker >> id >> n >> max_block;
      if (marker != "CASE" || !header || n > 100000)
        throw std::invalid_argument("expected CASE id n max_block, n <= 100000");
      mundo_x::FactorizedCore core(n, max_block);
      int rejected = -1;
      int split = -1;
      while (std::getline(std::cin, line) && line != "END") {
        std::istringstream command(line);
        std::string op;
        std::size_t a = 0, b = 0;
        command >> op;
        if (op == "SNAP") {
          std::string label;
          command >> label;
          snapshot(core, id, label, rejected, split);
          rejected = split = -1;
        } else if (op == "LOG") {
          std::string label, pattern;
          command >> label >> pattern;
          log_snapshot(core, id, label, pattern);
        } else if (op == "ALLHT") {
          for (std::size_t q = 0; q < n; ++q) {
            core.hadamard(q);
            core.phase_t(q);
          }
        } else if (op == "TRY") {
          command >> a;
          split = core.try_factorize(a) ? 1 : 0;
        } else if (op == "TRYSET") {
          std::size_t count = 0;
          command >> count;
          std::vector<std::size_t> left(count);
          for (auto& qubit : left) command >> qubit;
          split = core.try_split(left) ? 1 : 0;
        } else if (op == "REJECT") {
          command >> op >> a >> b;
          rejected = 0;
          try {
            gate(core, op, a, b);
          } catch (const std::exception&) {
            rejected = 1;
          }
        } else if (op == "REJECT_QUERY") {
          command >> op >> a;
          rejected = 0;
          try {
            invalid_query(core, op, a);
          } catch (const std::exception&) {
            rejected = 1;
          }
        } else {
          command >> a >> b;
          gate(core, op, a, b);
        }
      }
    }
  } catch (const std::exception& error) {
    std::cerr << "factorized oracle runner: " << error.what() << '\n';
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
}  // namespace

int main(int argc, char**) {
  if (argc != 1) {
    std::cerr << "factorized oracle runner accepts input on stdin and no arguments\n";
    return EXIT_FAILURE;
  }
  return run_protocol();
}
