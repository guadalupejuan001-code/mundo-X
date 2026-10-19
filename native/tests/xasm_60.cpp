#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/xasm.hpp"

namespace {
int checks = 0, failures = 0;

void expect(bool ok, const std::string& name) {
  ++checks;
  if (!ok) {
    ++failures;
    std::cerr << "FAIL " << name << '\n';
  }
}

mundo_x::XasmRunResult run(const std::string& program) {
  std::istringstream input(program);
  return mundo_x::run_xasm(input);
}

void rejects(const std::string& program, const std::string& message,
             const std::string& name) {
  try {
    run(program);
    expect(false, name + " was accepted");
  } catch (const std::exception& error) {
    expect(std::string(error.what()).find(message) != std::string::npos,
           name + ": " + error.what());
  }
}

std::string ghz_program(std::size_t qubits, const std::string& backend) {
  std::ostringstream program;
  program << "backend " << backend << "\nqubits " << qubits << "\nh 0\n";
  for (std::size_t q = 1; q < qubits; ++q)
    program << "cx " << q - 1 << ' ' << q << '\n';
  return program.str();
}

std::string zz(std::size_t qubits, std::size_t first, std::size_t second) {
  std::string pauli(qubits, 'I');
  pauli[first] = pauli[second] = 'Z';
  return pauli;
}

std::vector<double> expectation_values(const std::string& text) {
  std::vector<double> values;
  std::istringstream lines(text);
  std::string line;
  while (std::getline(lines, line)) {
    if (line.find("EXPECT pauli=") != 0) continue;
    const auto start = line.find(" value=");
    if (start == std::string::npos) throw std::runtime_error("missing expectation value");
    values.push_back(std::stod(line.substr(start + 7)));
  }
  return values;
}

void compact_sixty() {
  const auto prepared = ghz_program(60, "compact");
  const std::string all_x(60, 'X');
  std::string yy_x = all_x;
  yy_x[0] = yy_x[59] = 'Y';
  std::string zi(60, 'I'); zi[59] = 'Z';
  std::string queries = "assert ghz\nexpect " + all_x + "\n" +
      "assert expectation +" + all_x + " +1\n" +
      "assert expectation -" + all_x + " -1\n" +
      "assert expectation " + yy_x + " -1\n" +
      "assert expectation " + zi + " 0\n";
  for (const std::size_t q : {0u, 31u, 32u, 52u, 53u, 58u})
    queries += "assert expectation " + zz(60, q, 59) + " 1\n";
  queries += "assert ghz\n";
  const auto observed = run(prepared + queries);
  expect(observed.qubits == 60 && observed.instructions == 60 &&
         observed.state_bytes == 1020 && observed.assertions_passed,
         "Compact GHZ60 signed and high-bit expectations preserve the state");
  const auto values = expectation_values(observed.output);
  expect(values.size() == 1 && values[0] == 1.0, "Compact expectation output");

  const std::string measurements = "seed 2026\nmeasure 59\nmeasure 0\n";
  const auto plain = run(prepared + measurements);
  const auto queried = run(prepared + queries + measurements);
  const auto start = queried.output.find("MEASURE_Z");
  expect(start != std::string::npos && queried.output.substr(start) == plain.output &&
         queried.instructions == plain.instructions,
         "expectations consume no measurement randomness and do not collapse GHZ60");
}

void compact_thousand() {
  const std::string all_x(1000, 'X');
  std::string yy_x = all_x; yy_x[0] = yy_x[999] = 'Y';
  std::string zi(1000, 'I'); zi[999] = 'Z';
  const auto result = run(ghz_program(1000, "compact") +
      "assert ghz\nexpect " + all_x + "\n" +
      "assert expectation " + all_x + " 1\n" +
      "assert expectation " + yy_x + " -1\n" +
      "assert expectation " + zz(1000, 63, 999) + " 1\n" +
      "assert expectation " + zz(1000, 64, 999) + " 1\n" +
      "assert expectation " + zi + " 0\n" +
      "z 999\nexpect " + all_x + "\n" +
      "assert expectation " + all_x + " -1\n" +
      "cz 0 999\nassert expectation " + all_x + " 1\n" +
      "cx 0 999\nassert expectation " + zi + " 1\n");
  expect(result.qubits == 1000 && result.state_bytes == 257000 &&
         result.instructions == 1003 && result.assertions_passed,
         "Compact GHZ1000, multiword signed Pauli and q999 operate without dense export");
  const auto values = expectation_values(result.output);
  expect(values == std::vector<double>({1.0, -1.0}),
         "cached multiword observer refreshes after a high-qubit gate");
}

void observer_invalidation() {
  const auto result = run(
      "qubits 1\nexpect Z\nh 0\nexpect X\ns 0\nexpect Y\n"
      "z 0\nexpect Y\nx 0\nexpect Y\nmeasure 0\nexpect Z\n");
  const auto values = expectation_values(result.output);
  expect(values.size() == 6 && values[0] == 1 && values[1] == 1 &&
         values[2] == 1 && values[3] == -1 && values[4] == 1,
         "H, S, X and Z invalidate the read-only observer");
  const bool one = result.output.find("MEASURE_Z q=0 outcome=1") != std::string::npos;
  expect(values.size() == 6 && values[5] == (one ? -1.0 : 1.0),
         "measurement invalidates the read-only observer");
}

void reduced_sixty() {
  const auto prepared = ghz_program(60, "reduced") + "t 59\n";
  const std::string all_x(60, 'X');
  std::string y_last = all_x; y_last[59] = 'Y';
  const std::string identity(60, 'I');
  const auto result = run(prepared +
      "assert norm\nexpect " + all_x + "\nexpect " + y_last + "\n" +
      "expect -" + y_last + "\n" +
      "assert expectation " + all_x + " 0.70710678118654752440\n" +
      "assert expectation " + y_last + " 7.0710678118654752440e-1\n" +
      "assert expectation " + zz(60, 0, 59) + " 1\n" +
      "assert expectation " + zz(60, 53, 59) + " 1\n" +
      "assert expectation -" + identity + " -1\n");
  expect(result.backend == "reduced" && result.qubits == 60 &&
         result.components == 2 && result.t_gates == 1 &&
         result.instructions == 61 && result.assertions_passed,
         "Reduced GHZ60 plus T has the analytical phase and correlations");
  const auto values = expectation_values(result.output);
  const double expected = 1.0 / std::sqrt(2.0);
  expect(values.size() == 3 && std::abs(values[0] - expected) < 1e-14 &&
         std::abs(values[1] - expected) < 1e-14 &&
         std::abs(values[2] + expected) < 1e-14,
         "Reduced expectations retain precision and the optional Pauli sign");
  expect(result.output.find("ASSERT norm PASS\n") != std::string::npos,
         "norm keeps the established output and uses sparse coefficients at 60");

  std::string inverse = prepared;
  for (int i = 0; i < 7; ++i) inverse += "t 59\n";
  inverse += "assert norm\nassert expectation " + all_x + " 1\n" +
             "assert expectation " + y_last + " 0\n";
  expect(run(inverse).assertions_passed,
         "eight T gates restore GHZ60 phase through interference");
  const auto wrong = run(prepared + "assert expectation " + all_x + " 1\n");
  expect(!wrong.assertions_passed &&
         wrong.output.find("ASSERT expectation FAIL") != std::string::npos,
         "incorrect analytical expectation fails instead of merely reporting a value");
}

void limits_and_parser() {
  const auto small = run("backend reduced\nqubits 12\nassert fidelity\nassert norm\n");
  expect(small.assertions_passed &&
         small.output == "ASSERT fidelity PASS\nASSERT norm PASS\n",
         "small dense fidelity and legacy assertion output remain compatible");
  expect(run("backend reduced\nqubits 13\nassert norm\n").assertions_passed,
         "Reduced XASM permits registers beyond the old 12-qubit validation limit");
  expect(run("backend reduced\nqubits 63\nh 62\nt 62\nassert norm\n").assertions_passed,
         "Reduced highest supported bit is usable through XASM");
  rejects("backend reduced\nqubits 60\nassert fidelity\n", "line 3: assert fidelity uses a dense reference limited to 12 qubits",
          "fidelity60 fails before any dense reconstruction");
  rejects("backend reduced\nqubits 64\n", "line 2: reduced XASM supports 1..63 qubits",
          "Reduced64 remains unsupported explicitly");
  rejects("backend reduced\nqubits 1000\n", "line 2: reduced XASM supports 1..63 qubits",
          "Compact1000 support is never attributed to Reduced");
  rejects("backend reduced\nqubits 60\nt 60\n", "line 3:", "out-of-range Reduced gate");

  std::string maximum_t = "backend reduced\nqubits 60\n";
  for (int i = 0; i < 128; ++i) maximum_t += "t 59\n";
  expect(run(maximum_t + "assert norm\n").t_gates == 128,
         "128 T gate allowance remains usable at 60");
  rejects(maximum_t + "t 59\n", "128 T gates", "129th T is rejected without exponential test allocation");

  for (const std::string token : {"nan", "NaN", "inf", "-inf", "1e309", "1e", "1junk", "0x1p0", "--1", "1,0"})
    rejects("qubits 1\nassert expectation Z " + token + "\n", "line 2:", "invalid expectation value " + token);
  for (const std::string instruction : {"expect", "expect Z extra", "expect ZZ", "expect +", "expect ++Z", "expect z",
                                      "assert expectation", "assert expectation Z", "assert expectation Z 1 extra"})
    rejects("qubits 1\n" + instruction + "\n", "line 2:", "invalid expectation syntax " + instruction);
  rejects("expect Z\n", "line 1: declare qubits first", "expect needs a declared state");
  expect(run("qubits 1\nassert expectation +Z +1e0 # comment\n").assertions_passed,
         "signed Pauli, scientific notation and comments parse completely");
  expect(run("qubits 1\nassert expectation Z 1.00000000005\n").assertions_passed &&
         !run("qubits 1\nassert expectation Z 1.0000000002\n").assertions_passed,
         "expectation tolerance is absolute 1e-10");
}
}  // namespace

int main() {
  try {
    compact_sixty();
    compact_thousand();
    observer_invalidation();
    reduced_sixty();
    limits_and_parser();
  } catch (const std::exception& error) {
    expect(false, std::string("unexpected exception: ") + error.what());
  }
  std::cout << "checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
