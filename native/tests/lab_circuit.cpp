#include "mundo_x/lab_circuit.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
std::size_t field(const std::string& json, const std::string& name) {
  const auto key = '"' + name + "\":";
  const auto position = json.find(key);
  if (position == std::string::npos) throw std::runtime_error("missing JSON field " + name);
  return position + key.size();
}
double number(const std::string& json, const std::string& name) {
  return std::stod(json.substr(field(json, name)));
}
std::string array(const std::string& json, const std::string& name) {
  const auto begin = field(json, name);
  if (json.at(begin) != '[') throw std::runtime_error("expected JSON array " + name);
  return json.substr(begin + 1, json.find(']', begin) - begin - 1);
}
std::vector<double> numbers(const std::string& json, const std::string& name) {
  std::istringstream input(array(json, name));
  std::vector<double> values;
  if (input.peek() == std::char_traits<char>::eof()) return values;
  for (;;) {
    double value = 0.0;
    if (!(input >> value)) throw std::runtime_error("invalid numeric JSON array");
    values.push_back(value);
    char comma = 0;
    if (!(input >> comma)) break;
    if (comma != ',') throw std::runtime_error("invalid JSON array separator");
  }
  return values;
}
std::vector<std::string> generators(const std::string& json) {
  std::istringstream input(array(json, "generators"));
  std::vector<std::string> values;
  std::string value;
  while (input >> std::quoted(value)) {
    values.push_back(value);
    char comma = 0;
    if (!(input >> comma)) break;
    if (comma != ',') throw std::runtime_error("invalid generator separator");
  }
  return values;
}
std::vector<std::string> measurements(const std::string& json) {
  const auto content = array(json, "measurements");
  std::vector<std::string> values;
  for (auto begin = content.find('{'); begin != std::string::npos;) {
    const auto end = content.find('}', begin);
    if (end == std::string::npos) throw std::runtime_error("invalid measurement JSON");
    values.push_back(content.substr(begin, end - begin + 1));
    begin = content.find('{', end + 1);
  }
  return values;
}
std::string stable(const std::string& json) {
  const auto begin = field(json, "elapsed_ms");
  const auto end = json.find(',', begin);
  return json.substr(0, begin) + "0" + json.substr(end);
}
std::string run(std::size_t n, const std::string& script, int steps = -1,
                std::size_t shots = 0, std::uint64_t seed = 739) {
  const auto json = mundo_x::run_lab_circuit(n, script, steps, shots, seed);
  require(json.find("\"ok\":true") != std::string::npos &&
          json.find("\"engine\":\"CompactCore\"") != std::string::npos,
          "result identifies a successful original CompactCore run");
  require(number(json, "qubits") == n && generators(json).size() == n &&
          numbers(json, "prob_one").size() == n, "snapshot dimensions");
  require(number(json, "state_bytes") == 17 * n, "packed tableau storage for n<=8");
  require(std::isfinite(number(json, "elapsed_ms")) && number(json, "elapsed_ms") >= 0,
          "finite nonnegative elapsed time");
  const auto counts = numbers(json, "counts");
  require(counts.size() == (shots ? std::size_t{1} << n : 0), "histogram dimensions");
  require(std::accumulate(counts.begin(), counts.end(), 0.0) == shots, "histogram shot total");
  for (const auto count : counts)
    require(count >= 0 && count == std::floor(count), "histogram integer count");
  return json;
}
void rejects(std::size_t n, const std::string& script, int steps = -1,
             std::size_t shots = 0) {
  bool rejected = false;
  try { mundo_x::run_lab_circuit(n, script, steps, shots, 739); }
  catch (const std::exception&) { rejected = true; }
  require(rejected, "invalid lab request must throw");
}

void physical_circuits() {
  const auto interference = run(1, "h 0\nz 0\nh 0\n", -1, 128);
  require(numbers(interference, "prob_one") == std::vector<double>{1} &&
          numbers(interference, "counts") == std::vector<double>({0, 128}),
          "HZH interference produces one deterministically");
  require(generators(interference) == std::vector<std::string>{"-Z"}, "interference stabilizer");
  const auto phase = run(1, "h 0\ns 0\ns 0\nh 0\n", -1, 32);
  require(numbers(phase, "counts") == std::vector<double>({0, 32}), "S phases interfere");

  constexpr std::uint64_t seed = 739;
  const auto bell = run(2, "h 0\ncx 0 1\n", -1, 512, seed);
  const auto counts = numbers(bell, "counts");
  std::mt19937_64 rng(seed);
  int ones = 0;
  for (int shot = 0; shot < 512; ++shot) ones += int(rng() & 1U);
  require(counts == std::vector<double>({double(512 - ones), 0, 0, double(ones)}),
          "fresh Bell copies retain correlations and consume one random bit per shot");
  require(numbers(bell, "prob_one") == std::vector<double>({0.5, 0.5}), "Bell pre-sampling marginals");
  const auto no_shots = run(2, "h 0\ncx 0 1\n");
  require(generators(bell) == generators(no_shots) &&
          numbers(bell, "prob_one") == numbers(no_shots, "prob_one"),
          "histogram leaves the returned prefix snapshot unchanged");
  const auto graph = run(2, "h 0\nh 1\ncz 0 1\nh 1\n", -1, 512, seed);
  require(numbers(graph, "counts") == counts, "CZ graph converted to Bell correlations");
  require(numbers(run(2, "x 0\n", -1, 64), "counts") ==
          std::vector<double>({0, 0, 64, 0}), "q0 is leftmost histogram bit");
  require(numbers(run(2, "x 1\n", -1, 64), "counts") ==
          std::vector<double>({0, 64, 0, 0}), "q1 is rightmost histogram bit");
}

void prefixes_and_measurements() {
  const std::string script = "h 0\ncx 0 1\nmeasure 0\nx 1\n";
  const auto initial = run(2, script, 0, 16);
  require(number(initial, "step") == 0 && number(initial, "total_gates") == 4 &&
          numbers(initial, "counts") == std::vector<double>({16, 0, 0, 0}), "step zero is initial state");
  const auto one = run(2, script, 1, 32);
  require(numbers(one, "prob_one") == std::vector<double>({0.5, 0}) &&
          numbers(one, "counts")[1] == 0 && numbers(one, "counts")[3] == 0 &&
          measurements(one).empty(), "prefix excludes later gates and measurements");
  const auto two = run(2, script, 2);
  require(numbers(two, "prob_one") == std::vector<double>({0.5, 0.5}) &&
          measurements(two).empty(), "Bell prefix precedes mid-circuit measurement");
  const auto three = run(2, script, 3, 32);
  const auto measured = measurements(three);
  require(measured.size() == 1 && number(measured[0], "qubit") == 0 &&
          number(measured[0], "probability") == 0.5, "prefix measurement record");
  const auto outcome = number(measured[0], "outcome");
  require(numbers(three, "prob_one") == std::vector<double>({outcome, outcome}),
          "Bell measurement conditions both qubits");
  const auto all = run(2, script, -1, 32);
  require(number(all, "step") == 4 &&
          numbers(all, "prob_one") == std::vector<double>({outcome, 1 - outcome}),
          "post-measurement gate is executed");
  require(stable(all) == stable(run(2, script, 128, 32)), "oversized valid prefix ends at total gates");

  constexpr std::uint64_t seed = 851;
  const std::string random_again = "h 0\nmeasure 0\nh 0\n";
  const auto repeated = run(1, random_again, -1, 73, seed);
  std::mt19937_64 rng(seed);
  const auto prefix_bit = int(rng() & 1U);
  int ones = 0;
  for (int shot = 0; shot < 73; ++shot) ones += int(rng() & 1U);
  require(number(measurements(repeated)[0], "outcome") == prefix_bit &&
          numbers(repeated, "counts") == std::vector<double>({double(73 - ones), double(ones)}),
          "prefix measurement and histogram use one continuous RNG");
  require(stable(repeated) == stable(run(1, random_again, -1, 73, seed)), "seed reproduces snapshot and histogram");
  const auto twice = measurements(run(1, "h 0\nmeasure 0\nmeasure 0\n"));
  require(twice.size() == 2 && number(twice[0], "outcome") == number(twice[1], "outcome") &&
          number(twice[0], "probability") == 0.5 && number(twice[1], "probability") == 1,
          "repeated measurement is deterministic");
}

void input_limits() {
  run(2, "# comment\n\n  h 0 # phase preparation\r\ncx 0 1\r\n", -1, 8);
  for (const std::string invalid : {"h", "cx 0", "h 0 1", "cx 0 1 extra", "h -1", "h +0",
                                  "h 0x", "h 0.0", "h 2", "cx 0 0", "cz 1 1", "measure",
                                  "measure 1 extra", "H 0", "t 0", "h 184467440737095516160"})
    rejects(2, "x 0\n" + invalid + '\n', 0);
  rejects(0, ""); rejects(9, ""); rejects(1, "", -2); rejects(1, "", 129);
  rejects(1, "", -1, 4097);
  std::string limit;
  for (int i = 0; i < 128; ++i) limit += "x 0\n";
  require(number(run(1, limit), "total_gates") == 128, "128 operations accepted");
  rejects(1, limit + "x 0\n", 0);
  run(1, '#' + std::string(16 * 1024 - 1, 'x'));
  rejects(1, '#' + std::string(16 * 1024, 'x'));
  const auto maximum = run(8, "", -1, 4096, UINT64_MAX);
  require(numbers(maximum, "counts")[0] == 4096 &&
          number(maximum, "total_gates") == 0, "maximum qubits/shots and full-width seed");
}
}  // namespace

int main() {
  try {
    physical_circuits();
    prefixes_and_measurements();
    input_limits();
    std::cout << "PASS lab_circuit checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL lab_circuit " << error.what() << '\n';
    return 1;
  }
}
