// Examinations of the existing CompactCore and ReducedExactTRepresentation.
// This driver uses only the C++ standard library and the repository's cores.
// The Python standard-library supervisor applies OS resource limits externally.
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"

namespace {
using Clock = std::chrono::steady_clock;
using Complex = std::complex<double>;
constexpr double tolerance = 1e-9;
struct Gate { char op; std::size_t a, b = 0; };

double milliseconds(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}
std::size_t integer(const std::string& value) {
  std::size_t result = 0;
  const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
  if (value.empty() || parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size())
    throw std::invalid_argument("expected an unsigned decimal integer");
  return result;
}
std::string quoted(const std::string& value) {
  std::string result = "\"";
  for (const auto ch : value) {
    if (ch == '\\' || ch == '"') result += '\\';
    if (ch == '\n' || ch == '\r') result += ' ';
    else result += ch;
  }
  return result + '"';
}
void phase(const std::string& name, double elapsed = 0.0,
           std::size_t gate = 0, std::size_t components = 0) {
  std::cout << "{\"event\":\"progress\",\"phase\":" << quoted(name)
            << ",\"elapsed_ms\":" << elapsed << ",\"gates_completed\":" << gate
            << ",\"components\":" << components << "}\n" << std::flush;
}

template <typename Core> void apply(Core& core, const Gate& gate) {
  switch (gate.op) {
    case 'H': core.hadamard(gate.a); break;
    case 'S': core.phase_s(gate.a); break;
    case 'X': core.pauli_x(gate.a); break;
    case 'Z': core.pauli_z(gate.a); break;
    case 'C': core.cnot(gate.a, gate.b); break;
    case 'V': core.controlled_z(gate.a, gate.b); break;
    default: throw std::invalid_argument("unknown Clifford gate");
  }
}
void apply_reduced(mundo_x::ReducedExactTRepresentation& core, const Gate& gate) {
  if (gate.op == 'T') core.phase_t(gate.a);
  else apply(core, gate);
}
void apply_dense(mundo_x::DenseCore64& core, const Gate& gate) {
  if (gate.op == 'T') core.phase_t(gate.a);
  else apply(core, gate);
}

std::vector<Gate> random_circuit(std::size_t n, std::size_t depth,
                                 std::uint64_t seed, bool include_t) {
  std::mt19937_64 random(seed);
  const std::string options = include_t ? "HSXZTCV" : "HSXZCV";
  std::vector<Gate> result;
  for (std::size_t k = 0; k < depth; ++k) {
    char op = options[random() % options.size()];
    const auto a = random() % n;
    std::size_t b = 0;
    if (op == 'C' || op == 'V') {
      if (n == 1) op = 'H';
      else { b = random() % (n - 1); if (b >= a) ++b; }
    }
    result.push_back({op, a, b});
  }
  return result;
}

std::vector<Gate> reduced_circuit(const std::string& family, std::size_t n,
                                  std::uint64_t seed, std::size_t depth) {
  if (family == "random") return random_circuit(n, depth, seed, true);
  std::vector<Gate> gates;
  if (family == "boundary") {
    gates.push_back({'H', 0}); gates.push_back({'T', 0});
    if (n > 1) {
      gates.push_back({'H', n - 1}); gates.push_back({'T', n - 1});
      gates.push_back({'C', n - 1, 0});
    }
    return gates;
  }
  for (std::size_t q = 0; q < n; ++q) gates.push_back({'H', q});
  if (family == "product_ht" || family == "product_t8") {
    for (std::size_t q = 0; q < n; ++q)
      for (std::size_t t = 0; t < (family == "product_t8" ? 8u : 1u); ++t)
        gates.push_back({'T', q});
  } else if (family == "connected") {
    for (std::size_t k = 0; k < 4 * n; ++k) {
      const auto a = k % n, b = (a + 1) % n;
      gates.push_back({'T', a}); gates.push_back({'H', a});
      if (n > 1) gates.push_back({'C', a, b});
      if (k % 2) gates.push_back({'S', b});
    }
  } else throw std::invalid_argument("unknown Reduced family");
  return gates;
}

std::vector<std::size_t> selected_rows(std::size_t n) {
  std::vector<std::size_t> rows{0, n / 4, n / 2, n - 1};
  if (n > 1) { rows.push_back(1); rows.push_back(n - 2); }
  std::sort(rows.begin(), rows.end());
  rows.erase(std::unique(rows.begin(), rows.end()), rows.end());
  return rows;
}
bool verify_selected(const mundo_x::CompactCore& core, const std::string& family) {
  const auto n = core.qubits();
  for (const auto row : selected_rows(n)) {
    std::string expected(n + 1, 'I'); expected[0] = '+';
    if (family == "ghz") {
      if (row == 0) std::fill(expected.begin() + 1, expected.end(), 'X');
      else { expected[row] = 'Z'; expected[row + 1] = 'Z'; }
    } else if (family == "cluster_line") {
      expected[row + 1] = 'X';
      if (row > 0) expected[row] = 'Z';
      if (row + 1 < n) expected[row + 2] = 'Z';
    } else expected[row + 1] = 'Z';
    if (core.generator_string(row) != expected) return false;
  }
  return true;
}

int compact_case(const std::string& family, std::size_t n,
                 std::uint64_t seed, std::size_t depth) {
  if (n == 0 || n > 65536) throw std::invalid_argument("examination n outside 1..65536");
  std::vector<Gate> random;
  if (family == "random_inverse") random = random_circuit(n, depth, seed, false);
  else if (family != "ghz" && family != "cluster_line")
    throw std::invalid_argument("unknown Compact family");
  phase("construction_start");
  const auto initial = Clock::now();
  mundo_x::CompactCore core(n);
  const auto initialization = milliseconds(initial);
  std::cout << "{\"event\":\"initialized\",\"logical_payload_bytes\":"
            << core.state_bytes() << ",\"initialization_ms\":" << initialization << "}\n" << std::flush;
  const auto start = Clock::now();
  std::size_t gates = 0;
  if (family == "ghz") {
    core.hadamard(0); ++gates;
    for (std::size_t q = 1; q < n; ++q) {
      core.cnot(q - 1, q); ++gates;
      if ((q & 4095u) == 0) phase("evolution", milliseconds(start), gates);
    }
  } else if (family == "cluster_line") {
    for (std::size_t q = 0; q < n; ++q) { core.hadamard(q); ++gates; }
    for (std::size_t q = 1; q < n; ++q) { core.controlled_z(q - 1, q); ++gates; }
  } else {
    for (const auto& gate : random) { apply(core, gate); ++gates; }
    for (auto gate = random.rbegin(); gate != random.rend(); ++gate)
      for (unsigned k = 0; k < (gate->op == 'S' ? 3u : 1u); ++k) {
        apply(core, *gate); ++gates;
      }
  }
  const double evolution = milliseconds(start);
  std::cout << "{\"event\":\"evolution_complete\",\"evolution_ms\":" << evolution
            << ",\"gate_count\":" << gates << ",\"logical_payload_bytes\":"
            << core.state_bytes() << "}\n" << std::flush;
  const auto selected_start = Clock::now();
  bool passed = verify_selected(core, family);
  const auto selected_ms = milliseconds(selected_start);
  phase("selected_validation_complete", selected_ms, gates);
  const auto query_start = Clock::now();
  std::string validation;
  if (family == "ghz" && n > 1) {
    validation = "full_ghz_signature_and_selected_generators";
    passed = core.is_ghz_signature() && passed;
  } else if (family == "cluster_line") {
    validation = "full_cluster_signature_and_selected_generators";
    passed = core.is_cluster_signature(1, n) && passed;
  } else {
    validation = "all_identity_generators_after_inverse";
    for (std::size_t row = 0; row < n; ++row) {
      std::string expected(n + 1, 'I'); expected[0] = '+'; expected[row + 1] = 'Z';
      if (core.generator_string(row) != expected) passed = false;
    }
  }
  const auto query_ms = milliseconds(query_start);
  std::cout << "{\"event\":\"result\",\"status\":" << quoted(passed ? "pass" : "FAIL")
            << ",\"core\":\"CompactCore\",\"family\":" << quoted(family)
            << ",\"n\":" << n << ",\"seed\":" << seed << ",\"requested_depth\":" << depth
            << ",\"gate_count\":" << gates << ",\"initialization_ms\":" << initialization
            << ",\"evolution_ms\":" << evolution << ",\"selected_validation_ms\":" << selected_ms
            << ",\"full_validation_ms\":" << query_ms << ",\"validation_scope\":" << quoted(validation)
            << ",\"logical_payload_bytes\":" << core.state_bytes() << "}\n";
  return passed ? 0 : 1;
}

int reduced_case(const std::string& family, std::size_t n,
                 std::uint64_t seed, std::size_t depth, bool reconstruct) {
  phase("construction_start");
  const auto initial = Clock::now();
  mundo_x::ReducedExactTRepresentation core(n);
  const auto initialization = milliseconds(initial);
  const auto circuit = reduced_circuit(family, n, seed, depth);
  std::size_t gates = 0, peak_components = core.component_count();
  const auto start = Clock::now();
  auto last_progress = start;
  for (const auto& gate : circuit) {
    apply_reduced(core, gate); ++gates;
    peak_components = std::max(peak_components, core.component_count());
    if (gate.op == 'T' && milliseconds(last_progress) >= 1000.0) {
      phase("evolution", milliseconds(start), gates, core.component_count());
      last_progress = Clock::now();
    }
  }
  const auto evolution = milliseconds(start);
  std::cout << "{\"event\":\"evolution_complete\",\"evolution_ms\":" << evolution
            << ",\"components\":" << core.component_count()
            << ",\"peak_components\":" << peak_components
            << ",\"logical_payload_bytes\":" << core.logical_payload_bytes()
            << ",\"gate_count\":" << gates << "}\n" << std::flush;
  double reconstruction_ms = 0, reference_ms = 0, validation_ms = 0;
  double max_error = 0, l2_error = 0, norm_error = 0, analytic_error = 0;
  bool passed = true;
  std::string validation = "not_run_scale_case";
  if (reconstruct && n <= 10) {
    phase("reconstruction_start");
    auto stage = Clock::now();
    const auto actual = core.dense_from_reduced();
    reconstruction_ms = milliseconds(stage);
    phase("reconstruction_complete", reconstruction_ms);
    stage = Clock::now();
    mundo_x::DenseCore64 reference(n);
    for (const auto& gate : circuit) apply_dense(reference, gate);
    reference_ms = milliseconds(stage);
    stage = Clock::now();
    Complex overlap{};
    double actual_norm = 0.0;
    for (std::size_t i = 0; i < actual.size(); ++i) {
      overlap += std::conj(reference.state()[i]) * actual[i];
      actual_norm += std::norm(actual[i]);
    }
    const auto global_phase = std::abs(overlap) > 0 ? overlap / std::abs(overlap) : Complex{1, 0};
    for (std::size_t i = 0; i < actual.size(); ++i) {
      const auto error = actual[i] - global_phase * reference.state()[i];
      max_error = std::max(max_error, std::abs(error));
      l2_error += std::norm(error);
    }
    l2_error = std::sqrt(l2_error); norm_error = std::abs(actual_norm - 1.0);
    if (family == "product_t8") {
      const double amplitude = 1.0 / std::sqrt(static_cast<double>(actual.size()));
      for (const auto value : actual)
        analytic_error = std::max(analytic_error, std::abs(value - global_phase * amplitude));
    }
    passed = std::isfinite(l2_error) && max_error < tolerance && l2_error < tolerance &&
             norm_error < tolerance && analytic_error < tolerance;
    validation_ms = milliseconds(stage);
    validation = family == "product_t8" ? "DenseCore64_global_phase_aligned_and_analytic_T8_identity"
                                         : "DenseCore64_global_phase_aligned";
  }
  std::cout << "{\"event\":\"result\",\"status\":"
            << quoted(passed ? (validation == "not_run_scale_case" ? "completed" : "pass") : "FAIL")
            << ",\"core\":\"ReducedExactTRepresentation\",\"family\":" << quoted(family)
            << ",\"n\":" << n << ",\"seed\":" << seed << ",\"requested_depth\":" << depth
            << ",\"gate_count\":" << gates << ",\"initialization_ms\":" << initialization
            << ",\"evolution_ms\":" << evolution << ",\"reconstruction_ms\":" << reconstruction_ms
            << ",\"reference_ms\":" << reference_ms << ",\"validation_ms\":" << validation_ms
            << ",\"max_amplitude_error\":" << max_error << ",\"l2_error\":" << l2_error
            << ",\"norm_squared_error\":" << norm_error << ",\"analytic_T8_error\":" << analytic_error
            << ",\"components\":" << core.component_count() << ",\"peak_components\":" << peak_components
            << ",\"logical_payload_bytes\":" << core.logical_payload_bytes()
            << ",\"validation_scope\":" << quoted(validation)
            << ",\"reconstruction_guard_max_n\":10,\"tolerance\":" << tolerance << "}\n";
  return passed ? 0 : 1;
}

int sanity() {
  int failures = 0;
  for (const auto n : {2u, 63u, 64u, 65u, 129u}) {
    failures += compact_case("ghz", n, 20261018, 0);
    failures += compact_case("cluster_line", n, 20261018, 0);
    failures += compact_case("random_inverse", n, 20261018 + n, 256);
  }
  for (std::size_t n = 1; n <= 10; ++n) {
    failures += reduced_case("random", n, 20261018 + n, 128 + 16 * n, true);
    failures += reduced_case("product_t8", n, 20261018, 0, true);
    failures += reduced_case("connected", n, 20261018, 0, true);
  }
  for (const auto n : {std::size_t{0}, std::size_t{64}, std::numeric_limits<std::size_t>::max()}) {
    bool rejected = false;
    try { mundo_x::ReducedExactTRepresentation invalid(n); }
    catch (const std::invalid_argument&) { rejected = true; }
    if (!rejected) ++failures;
  }
  std::cout << "{\"event\":\"sanity_summary\",\"core_cases\":45,\"rejected_dimension_cases\":3,\"failures\":"
            << failures << "}\n";
  return failures == 0 ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  std::cout << std::setprecision(17);
  try {
    if (argc == 1 || std::string(argv[1]) == "sanity") return sanity();
    if (argc < 4) throw std::invalid_argument("usage: native_original_exams compact|reduced family n [seed] [depth] [validate]");
    const std::string core = argv[1], family = argv[2];
    const auto n = integer(argv[3]);
    const auto seed = argc > 4 ? integer(argv[4]) : 20261018;
    const auto depth = argc > 5 ? integer(argv[5]) : 512;
    if (core == "compact") return compact_case(family, n, seed, depth);
    if (core == "reduced") return reduced_case(family, n, seed, depth, argc > 6 && std::string(argv[6]) == "validate");
    throw std::invalid_argument("unknown engine");
  } catch (const std::bad_alloc& error) {
    std::cout << "{\"event\":\"error\",\"status\":\"allocation_failure\",\"message\":" << quoted(error.what()) << "}\n";
    return 3;
  } catch (const std::exception& error) {
    std::cout << "{\"event\":\"error\",\"status\":\"exception\",\"message\":" << quoted(error.what()) << "}\n";
    return 2;
  }
}
