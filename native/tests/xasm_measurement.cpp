#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/xasm.hpp"

namespace {
int checks = 0, failures = 0;
void expect(bool ok, const char* name) {
  ++checks;
  failures += !ok;
  if (!ok) std::cerr << "FAIL " << name << '\n';
}
mundo_x::XasmRunResult run(const std::string& program) {
  std::istringstream input(program);
  return mundo_x::run_xasm(input);
}
void rejects(const std::string& program, const char* name) {
  bool rejected = false;
  try { run(program); }
  catch (const std::exception&) { rejected = true; }
  expect(rejected, name);
}
}  // namespace

int main() {
  const std::string bell =
      "qubits 2\nseed 2026\nh 0\ncx 0 1\nassert ghz\n"
      "probz 0\nprobz 1\nmeasure 0\nprobz 1\nmeasure 1\n";
  const auto result = run(bell);
  expect(result.assertions_passed && result.instructions == 4, "Bell program executes");
  expect(run(bell).output == result.output, "same seed reproduces entire output");
  std::istringstream output(result.output);
  std::string line;
  int outcome0 = -1, outcome1 = -1, prior_half = 0;
  double conditional_p1 = -1;
  bool first_random = false, second_deterministic = false;
  while (std::getline(output, line)) {
    if (line == "PROB_Z q=0 p0=0.5 p1=0.5" ||
        line == "PROB_Z q=1 p0=0.5 p1=0.5") ++prior_half;
    if (line.find("MEASURE_Z q=0 ") == 0) {
      outcome0 = line.find("outcome=1") != std::string::npos ? 1 : 0;
      first_random = line.find("probability=0.5 random=1") != std::string::npos;
    }
    if (line.find("PROB_Z q=1 ") == 0 && outcome0 >= 0)
      conditional_p1 = line.find("p1=1") != std::string::npos ? 1 : 0;
    if (line.find("MEASURE_Z q=1 ") == 0) {
      outcome1 = line.find("outcome=1") != std::string::npos ? 1 : 0;
      second_deterministic = line.find("probability=1 random=0") != std::string::npos;
    }
  }
  expect(prior_half == 2, "Bell marginals are both half before measurement");
  expect(outcome0 >= 0 && outcome0 == outcome1 && conditional_p1 == outcome0 &&
         first_random && second_deterministic, "Bell conditional probability and measured outcomes agree");

  const auto interference = run(
      "qubits 1\nh 0\nprobz 0\nz 0\nprobz 0\nh 0\nprobz 0\nassert one\nmeasure 0\n");
  expect(interference.assertions_passed &&
         interference.output.find("PROB_Z q=0 p0=0 p1=1") != std::string::npos &&
         interference.output.find("MEASURE_Z q=0 outcome=1 probability=1 random=0") != std::string::npos,
         "phase interference becomes deterministic measurement");
  expect(run("qubits 1\nseed 18446744073709551615\nmeasure 0\n").instructions == 1,
         "full unsigned 64-bit seed supported");
  expect(run("qubits 1\nprobz 0 # read without collapse\nmeasure 0\n").instructions == 1,
         "comments work and probability query is not a state operation");
  rejects("qubits -1\n", "negative allocation rejected");
  rejects("qubits 1\nseed -1\n", "negative seed rejected");
  rejects("qubits 1\nseed 18446744073709551616\n", "overflowing seed rejected");
  rejects("qubits 1\nmeasure -1\n", "negative measurement index rejected");
  rejects("qubits 1\nprobz 1\n", "out-of-range probability query rejected");
  rejects("qubits 1\nmeasure 0 extra\n", "extra measurement arguments rejected");
  rejects("qubits 1\nh 0x\n", "partial numeric token rejected");
  rejects("qubits 1\nseed\n", "missing seed rejected");
  rejects("measure 0\n", "measurement requires state declaration");
  std::cout << "checks=" << checks << " failures=" << failures << '\n';
  return failures ? 1 : 0;
}
