#include <cmath>
#include <complex>
#include <cstddef>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mundo_x/dense_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/xasm.hpp"

namespace {

constexpr double kTolerance = 3e-5;

// Do not use assert(): these checks must also run in Release builds.
void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

mundo_x::XasmRunResult run(const std::string& program) {
  std::istringstream input(program);
  return mundo_x::run_xasm(input);
}

void expect_error(const std::string& program, const std::string& fragment) {
  try {
    run(program);
  } catch (const std::exception& error) {
    require(std::string(error.what()).find(fragment) != std::string::npos,
            "unexpected diagnostic: " + std::string(error.what()));
    return;
  }
  throw std::runtime_error("program should have failed: " + program);
}

void compare_state(const mundo_x::ReducedExactTRepresentation& reduced,
                   const mundo_x::DenseCore& reference,
                   const std::string& context) {
  const auto actual = reduced.dense_from_reduced();
  const auto& expected = reference.state();
  require(actual.size() == expected.size(), context + ": state size");
  double norm = 0.0;
  for (std::size_t i = 0; i < actual.size(); ++i) {
    const std::complex<double> expected_amplitude{
        expected[i].real(), expected[i].imag()};
    require(std::isfinite(actual[i].real()) &&
                std::isfinite(actual[i].imag()),
            context + ": non-finite amplitude");
    require(std::abs(actual[i] - expected_amplitude) < kTolerance,
            context + ": amplitude " + std::to_string(i) + " differs");
    norm += std::norm(actual[i]);
  }
  require(std::isfinite(norm) && std::abs(norm - 1.0) < kTolerance,
          context + ": state is not normalized");
}

void test_compact_compatibility() {
  const auto hello = run("# existing hello program\nqubits 1\nh 0\nz 0\n"
                         "h 0\nshow\nbytes\nassert one\n");
  require(hello.backend == "compact", "compact must remain the default");
  require(hello.qubits == 1 && hello.instructions == 3,
          "hello instruction count");
  require(hello.assertions_passed && hello.state_bytes == 17,
          "hello state and assertion");
  require(hello.output == "STATE\n-Z\nSTATE_BYTES 17\nASSERT one PASS\n",
          "legacy hello output changed");

  const auto ghz = run("backend compact\nqubits 4\nh 0\ncx 0 1\n"
                       "cx 1 2\ncx 2 3\nassert ghz\n");
  require(ghz.assertions_passed && ghz.instructions == 4,
          "explicit compact backend GHZ");
  const auto large = run("qubits 64\nh 0\ncx 0 63\nbytes\n");
  require(large.qubits == 64 && large.state_bytes > 0,
          "compact backend must retain support above 12 qubits");
  const auto cluster = run("qubits 2\nh 0\nh 1\ncz 0 1\nshow\n");
  require(cluster.output == "STATE\n+XZ\n+ZX\n",
          "compact CZ must prepare the two-qubit cluster generators");
}

void test_reduced_xasm() {
  const auto magic = run("backend reduced\nqubits 1\nh 0\nt 0\nshow\n"
                         "bytes\nassert fidelity\nassert norm\n");
  require(magic.backend == "reduced" && magic.qubits == 1,
          "reduced backend selection");
  require(magic.instructions == 2 && magic.t_gates == 1 &&
              magic.components == 2,
          "magic state counters");
  require(magic.state_bytes == 0 && magic.logical_payload_bytes > 0,
          "reduced memory must be reported as logical payload");
  require(magic.assertions_passed, "magic state validation");
  require(magic.output.find("STATE\nbackend=reduced\ncomponents=2\n"
                            "t_gates=1\nlogical_payload_bytes=") == 0,
          "reduced show must describe the representation");
  require(magic.output.find("LOGICAL_PAYLOAD_BYTES ") != std::string::npos &&
              magic.output.find("STATE_BYTES ") == std::string::npos,
          "reduced bytes label");
  require(magic.output.find("ASSERT fidelity PASS") != std::string::npos &&
              magic.output.find("ASSERT norm PASS") != std::string::npos,
          "reduced validation output");

  const auto mixed = run("backend reduced\nqubits 3\nh 0\ncx 0 1\n"
                         "t 1\ns 0\nx 2\nz 1\ncz 1 2\nh 1\n"
                         "t 0\ncx 2 0\nassert fidelity\nassert norm\n");
  require(mixed.assertions_passed && mixed.instructions == 10 &&
              mixed.t_gates == 2,
          "mixed Clifford+T program");
}

void test_t_identities() {
  const double half_sqrt = 1.0 / std::sqrt(2.0);
  mundo_x::ReducedExactTRepresentation magic(1);
  magic.hadamard(0);
  magic.phase_t(0);
  const auto magic_state = magic.dense_from_reduced();
  require(std::abs(magic_state[0] -
                   std::complex<double>{half_sqrt, 0.0}) < kTolerance &&
              std::abs(magic_state[1] -
                       std::complex<double>{0.5, 0.5}) < kTolerance,
          "H then T must preserve the relative pi/4 phase");

  mundo_x::ReducedExactTRepresentation twice(2);
  mundo_x::DenseCore s_reference(2);
  twice.hadamard(0);
  s_reference.hadamard(0);
  twice.cnot(0, 1);
  s_reference.cnot(0, 1);
  twice.phase_t(1);
  twice.phase_t(1);
  s_reference.phase_s(1);
  compare_state(twice, s_reference, "T squared equals S on an entangled state");

  mundo_x::ReducedExactTRepresentation eight(2);
  mundo_x::DenseCore identity_reference(2);
  eight.hadamard(0);
  identity_reference.hadamard(0);
  eight.cnot(0, 1);
  identity_reference.cnot(0, 1);
  for (std::size_t i = 0; i < 8; ++i) eight.phase_t(1);
  compare_state(eight, identity_reference,
                "T to the eighth equals identity on an entangled state");

  const auto xasm = run("backend reduced\nqubits 1\nh 0\nt 0\nt 0\n"
                        "assert fidelity\nassert norm\n");
  require(xasm.t_gates == 2 && xasm.assertions_passed,
          "XASM must apply both T instructions");
}

void test_seeded_circuits() {
  std::mt19937 random(0x4D554E44u);
  for (std::size_t qubits = 1; qubits <= 6; ++qubits) {
    for (std::size_t trial = 0; trial < 12; ++trial) {
      mundo_x::ReducedExactTRepresentation reduced(qubits);
      mundo_x::DenseCore reference(qubits);
      std::ostringstream program;
      program << "backend reduced\nqubits " << qubits << '\n';
      std::size_t t_gates = 0;
      for (std::size_t step = 0; step < 80; ++step) {
        const std::size_t q = random() % qubits;
        const std::size_t other =
            qubits > 1 ? (q + 1 + random() % (qubits - 1)) % qubits : q;
        const auto op = random() % (qubits > 1 ? 7 : 5);
        switch (op) {
          case 0:
            reduced.hadamard(q);
            reference.hadamard(q);
            program << "h " << q << '\n';
            break;
          case 1:
            reduced.phase_s(q);
            reference.phase_s(q);
            program << "s " << q << '\n';
            break;
          case 2:
            reduced.pauli_x(q);
            reference.pauli_x(q);
            program << "x " << q << '\n';
            break;
          case 3:
            reduced.pauli_z(q);
            reference.pauli_z(q);
            program << "z " << q << '\n';
            break;
          case 4:
            reduced.phase_t(q);
            reference.phase_t(q);
            program << "t " << q << '\n';
            ++t_gates;
            break;
          case 5:
            reduced.cnot(q, other);
            reference.cnot(q, other);
            program << "cx " << q << ' ' << other << '\n';
            break;
          case 6:
            reduced.controlled_z(q, other);
            reference.controlled_z(q, other);
            program << "cz " << q << ' ' << other << '\n';
            break;
        }
        if ((step + 1) % 10 == 0) {
          compare_state(reduced, reference,
                        "seeded circuit q=" + std::to_string(qubits) +
                            " trial=" + std::to_string(trial) +
                            " step=" + std::to_string(step + 1));
        }
      }
      program << "assert fidelity\nassert norm\n";
      const auto result = run(program.str());
      require(result.assertions_passed && result.instructions == 80 &&
                  result.t_gates == t_gates,
              "seeded XASM circuit validation");
    }
  }
}

void test_parser_errors() {
  const std::vector<std::pair<std::string, std::string>> cases{
      {"backend unknown\nqubits 1\n", "line 1:"},
      {"backend reduced\nbackend reduced\nqubits 1\n", "line 2:"},
      {"backend\nqubits 1\n", "line 1:"},
      {"qubits 1\nbackend reduced\n", "line 2:"},
      {"qubits 1\nqubits 1\n", "line 2:"},
      {"qubits -1\n", "line 1:"},
      {"qubits 0\n", "line 1:"},
      {"qubits 1 extra\n", "line 1:"},
      {"qubits 99999999999999999999999999999\n", "line 1:"},
      {"qubits 1\nh -1\n", "line 2:"},
      {"qubits 1\nh +0\n", "line 2:"},
      {"qubits 1\nh 0x0\n", "line 2:"},
      {"qubits 1\nh 0 extra\n", "line 2:"},
      {"qubits 1\nh 1\n", "line 2:"},
      {"qubits 1\nh\n", "line 2:"},
      {"qubits 2\ncx 0\n", "line 2:"},
      {"qubits 2\ncx 0 0\n", "line 2:"},
      {"qubits 2\ncz 1 1\n", "line 2:"},
      {"qubits 2\ncz 0 2\n", "line 2:"},
      {"qubits 1\nt 0\n", "line 2:"},
      {"qubits 1\nshow extra\n", "line 2:"},
      {"qubits 1\nbytes extra\n", "line 2:"},
      {"qubits 1\nassert one extra\n", "line 2:"},
      {"qubits 1\nassert fidelity\n", "line 2:"},
      {"qubits 1\nassert norm\n", "line 2:"},
      {"qubits 1\nassert unknown\n", "line 2:"},
      {"backend reduced\nqubits 1\nassert one\n", "line 3:"},
      {"backend reduced\nqubits 2\nassert ghz\n", "line 3:"},
      {"backend reduced extra\nqubits 1\n", "line 1:"},
      {"backend reduced\nqubits 64\n", "line 2:"},
      {"# comment\n\nh 0\n", "line 3:"},
      {"qubits 1\nunknown 0\n", "line 2:"},
      {"# no declaration\n", "qubits"},
  };
  for (const auto& example : cases) expect_error(example.first, example.second);

  const auto comments = run("backend compact # select engine\nqubits 1\r\n"
                            "  x 0 # prepare one\nassert one\n");
  require(comments.assertions_passed, "comments and CRLF support");
}

void test_limits_and_failed_assertions() {
  const auto max_qubits = run("backend reduced\nqubits 63\nbytes\n");
  require(max_qubits.qubits == 63 && max_qubits.components == 1,
          "63-qubit reduced limit must be inclusive");
  std::string program = "backend reduced\nqubits 1\n";
  for (std::size_t i = 0; i < 128; ++i) program += "t 0\n";
  const auto max_t = run(program + "assert fidelity\nassert norm\n");
  require(max_t.t_gates == 128 && max_t.instructions == 128 &&
              max_t.assertions_passed,
          "128-T limit must be inclusive");
  expect_error(program + "t 0\n", "line 131:");

  const auto failed = run("qubits 1\nassert one\nx 0\nassert one\n");
  require(!failed.assertions_passed,
          "a later passing assertion must not erase an earlier failure");
  require(failed.output == "ASSERT one FAIL\nASSERT one PASS\n",
          "assertion failure reporting");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests{
      {"compact compatibility", test_compact_compatibility},
      {"reduced XASM", test_reduced_xasm},
      {"T phase identities", test_t_identities},
      {"seeded Clifford+T circuits", test_seeded_circuits},
      {"parser diagnostics", test_parser_errors},
      {"limits and failed assertions", test_limits_and_failed_assertions},
  };
  std::size_t failures = 0;
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "PASS " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
    }
  }
  return failures == 0 ? 0 : 1;
}
