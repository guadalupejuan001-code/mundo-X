#include "mundo_x/lab_protocols.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks = 0;
void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class Function> void invalid(Function function, const std::string& message) {
  bool rejected = false;
  try { function(); } catch (const std::invalid_argument&) { rejected = true; }
  require(rejected, message);
}

std::vector<std::size_t> sample(const mundo_x::CompactCore& state,
                               std::size_t shots, std::uint64_t seed) {
  std::vector<std::size_t> histogram(std::size_t{1} << state.qubits(), 0);
  std::mt19937_64 rng(seed);
  for (std::size_t shot = 0; shot < shots; ++shot) {
    mundo_x::CompactCore copy(state);
    std::size_t index = 0;
    for (std::size_t q = 0; q < state.qubits(); ++q)
      index = (index << 1) | std::size_t(copy.measure_z(q, rng).outcome_one);
    ++histogram[index];
  }
  return histogram;
}

void superdense() {
  for (int message = 0; message < 4; ++message) {
    auto run = mundo_x::lab_protocol_detail::execute(0, message, 190);
    require(run.passed, "superdense readout failed");
    require(run.observed == mundo_x::lab_protocol_detail::bits(message, 2),
            "superdense bit order must be q0 MSB, q1 LSB");
    require(run.measurements.size() == 2, "superdense must measure two decoded data bits");
    const auto histogram = sample(run.state, 256, 32);
    require(histogram[message] == 256, "superdense histogram not deterministic");
    const auto script = run.operations.str();
    require(script.find("h 0\ncx 0 1\n") == 0, "superdense must prepare a Bell pair");
    require((script.find("z 0\n") != std::string::npos) == bool(message & 2),
            "superdense Z encoding incorrect");
    require((script.find("x 0\n") != std::string::npos) == bool(message & 1),
            "superdense X encoding incorrect");
    const auto gates = mundo_x::lab_detail::parse(2, script);
    const std::size_t encoded_bits = std::size_t(bool(message & 2)) +
                                     std::size_t(bool(message & 1));
    require(gates.size() == 6 + encoded_bits,
            "superdense gate and measurement count incorrect");
    const auto json = mundo_x::run_lab_protocol(0, message, 128, 190);
    require(json.find("\"passed\":true") != std::string::npos, "superdense public result failed");
    require(json.find("\"result\":{\"ok\":true") != std::string::npos,
            "protocol must contain the canonical circuit result");
  }
}

void bernstein_vazirani() {
  for (int secret = 0; secret < 16; ++secret) {
    auto run = mundo_x::lab_protocol_detail::execute(1, secret, 200 + secret);
    require(run.passed, "Bernstein-Vazirani readout failed");
    require(run.oracle_calls == 1, "BV must invoke one logical XOR oracle");
    require(run.observed == mundo_x::lab_protocol_detail::bits(secret, 4),
            "BV secret bit order incorrect");
    require(run.state.probability_one_z(4) == 0.5, "BV ancilla must retain unbiased Z readout");
    const auto gates = mundo_x::lab_detail::parse(5, run.operations.str());
    std::size_t secret_ones = 0, oracle_cx = 0;
    for (int q = 0; q < 4; ++q) if (secret & (1 << q)) ++secret_ones;
    for (const auto& gate : gates)
      if (gate.kind == mundo_x::lab_detail::Operation::Kind::CX) {
        ++oracle_cx;
        require(gate.a < 4 && gate.b == 4, "BV XOR oracle must target the ancilla");
      }
    require(oracle_cx == secret_ones && gates.size() == 14 + secret_ones,
            "BV logical oracle decomposition or operation count incorrect");
    const auto histogram = sample(run.state, 512, 210 + secret);
    const auto first = static_cast<std::size_t>(secret) << 1;
    require(histogram[first] + histogram[first + 1] == 512,
            "BV sampled data differs from the secret");
    require(std::abs(static_cast<double>(histogram[first]) - 256.0) < 96.0,
            "BV ancilla sampling is inconsistent with its half probability");
    for (std::size_t i = 0; i < histogram.size(); ++i)
      if (i != first && i != first + 1)
        require(histogram[i] == 0, "BV histogram leaked into another secret");
    const auto json = mundo_x::run_lab_protocol(1, secret, 64, 200 + secret);
    require(json.find("\"oracle_calls\":1") != std::string::npos, "BV public oracle count missing");
    require(json.find("\"simulation\":\"classical_cpu\"") != std::string::npos,
            "BV must identify classical CPU simulation");
  }
}

void bitflip() {
  const std::string expected_syndromes[] = {"00", "10", "11", "01"};
  for (int error = 0; error < 4; ++error) {
    auto run = mundo_x::lab_protocol_detail::execute(2, error, 300);
    require(run.passed && run.observed == "100", "bitflip corrected data must be100");
    require(run.syndrome == expected_syndromes[error], "bitflip measured syndrome incorrect");
    require(run.corrected_qubit == error - 1, "bitflip correction target incorrect");
    require(run.measurements.size() == 5, "bitflip needs two parity and three data measurements");
    require(run.measurements[0].qubit == 3 && run.measurements[1].qubit == 4,
            "bitflip must obtain parity before data readout");
    require(mundo_x::lab_detail::parse(5, run.operations.str()).size() ==
                std::size_t(error ? 16 : 14),
            "bitflip gate and measurement count incorrect");
    for (const auto& m : run.measurements)
      require(!m.random && m.probability == 1.0, "bitflip readout must be deterministic");
    const auto histogram = sample(run.state, 256, 301);
    const std::size_t syndrome = static_cast<std::size_t>(run.syndrome[0] == '1') * 2 +
                                 static_cast<std::size_t>(run.syndrome[1] == '1');
    require(histogram[16 + syndrome] == 256, "bitflip full state must preserve syndrome ancillas");

    // With |1>, an omitted correction is observable for all three injected Xs.
    // Checking every decoded data bit also catches dirty decoding ancillas.
    if (error) {
      mundo_x::CompactCore uncorrected(3);
      uncorrected.pauli_x(0); uncorrected.cnot(0, 1); uncorrected.cnot(0, 2);
      uncorrected.pauli_x(static_cast<std::size_t>(error - 1));
      uncorrected.cnot(0, 2); uncorrected.cnot(0, 1);
      require(uncorrected.probability_one_z(0) != 1.0 ||
              uncorrected.probability_one_z(1) != 0.0 ||
              uncorrected.probability_one_z(2) != 0.0,
              "bitflip negative control should fail without correction");
    }
    const auto json = mundo_x::run_lab_protocol(2, error, 64, 300);
    require(json.find("\"logical_input\":\"1\"") != std::string::npos,
            "bitflip must disclose the demonstrated logical input");
    require(json.find("\"expected_data\":\"100\"") != std::string::npos,
            "bitflip must distinguish data from syndrome ancillas");
  }
}

void boundaries() {
  invalid([] { mundo_x::run_lab_protocol(-1, 0, 1, 0); }, "negative protocol accepted");
  invalid([] { mundo_x::run_lab_protocol(3, 0, 1, 0); }, "unknown protocol accepted");
  for (int protocol = 0; protocol < 3; ++protocol) {
    invalid([=] { mundo_x::run_lab_protocol(protocol, -1, 1, 0); }, "negative value accepted");
    invalid([=] { mundo_x::run_lab_protocol(protocol, protocol == 1 ? 16 : 4, 1, 0); },
            "oversized value accepted");
    invalid([=] { mundo_x::run_lab_protocol(protocol, 0, 4097, 0); }, "oversized shots accepted");
  }
  const auto no_shots = mundo_x::run_lab_protocol(0, 3, 0, 0);
  require(no_shots.find("\"counts\":[]") != std::string::npos, "zero shots should omit histogram");
  require(mundo_x::run_lab_protocol(1, 9, 4096, 0).find("\"passed\":true") != std::string::npos,
          "maximum allowed shots rejected");
}
}

int main() {
  try {
    superdense(); bernstein_vazirani(); bitflip(); boundaries();
    std::cout << "PASS " << checks << " lab protocol checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return 1;
  }
}
