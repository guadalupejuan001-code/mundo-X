// Native-only diversity exam. The numerical reference uses generic long-double
// 2x2 matrix products, not the production gate implementation or an SDK.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <unordered_set>
#include <vector>

#ifdef __linux__
#include <sched.h>
#include <sys/resource.h>
#endif

#include "mundo_x/factorized_core.hpp"

namespace {
using Clock = std::chrono::steady_clock;
using Complex = std::complex<long double>;
using Pair = std::array<Complex, 2>;
using Matrix = std::array<std::array<Complex, 2>, 2>;
constexpr long double kLocalTolerance = 3e-11L;
constexpr long double kGlobalTolerance = 1e-7L;
constexpr long double kLegacyGlobalTolerance = 1e-8L;
constexpr long double kAlternativeMinimumMass = 1e-4L;
constexpr std::size_t kSignatureSamples = 4096;

struct Sum {
  long double value = 0, compensation = 0;
  void add(long double term) {
    const auto corrected = term - compensation;
    const auto next = value + corrected;
    compensation = (next - value) - corrected;
    value = next;
  }
};

std::uint64_t mix(std::uint64_t value) {
  value += 0x9e3779b97f4a7c15ULL;
  value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
  return value ^ (value >> 31U);
}

std::uint64_t generator(std::uint64_t seed, std::size_t qubit) {
  return mix(seed ^ mix(static_cast<std::uint64_t>(qubit)));
}

std::size_t next_gate(std::uint64_t& state) {
  state = mix(state);
  return static_cast<std::size_t>(state % 5);
}

std::array<Matrix, 5> matrices() {
  const long double scale = 1 / std::sqrt(2.0L);
  const Complex zero{0, 0}, one{1, 0}, minus{-1, 0};
  const Complex phase{scale, scale};
  return {{Matrix{{{{scale, scale}}, {{scale, -scale}}}},
           Matrix{{{{one, zero}}, {{zero, Complex{0, 1}}}}},
           Matrix{{{{one, zero}}, {{zero, phase}}}},
           Matrix{{{{zero, one}}, {{one, zero}}}},
           Matrix{{{{one, zero}}, {{zero, minus}}}}}};
}

Pair matrix_product(const Matrix& matrix, const Pair& state) {
  Pair result{};
  for (std::size_t row = 0; row < 2; ++row)
    for (std::size_t column = 0; column < 2; ++column)
      result[row] += matrix[row][column] * state[column];
  return result;
}

void production_gate(mundo_x::FactorizedCore& core, std::size_t operation,
                     std::size_t qubit) {
  switch (operation) {
    case 0: core.hadamard(qubit); break;
    case 1: core.phase_s(qubit); break;
    case 2: core.phase_t(qubit); break;
    case 3: core.pauli_x(qubit); break;
    case 4: core.pauli_z(qubit); break;
    default: throw std::logic_error("invalid generated gate");
  }
}

double elapsed(Clock::time_point start) {
  return std::chrono::duration<double, std::milli>(Clock::now() - start).count();
}

std::size_t real_peak_rss() {
#ifdef __linux__
  std::ifstream stream("/proc/self/status");
  std::string line;
  while (std::getline(stream, line)) {
    if (line.compare(0, 6, "VmHWM:") == 0) {
      std::istringstream fields(line.substr(6));
      std::size_t kib = 0;
      fields >> kib;
      return kib * 1024;
    }
  }
#endif
  return 0;
}

void limits(std::size_t bytes, unsigned cpu_seconds) {
#ifdef __linux__
  const rlimit memory{static_cast<rlim_t>(bytes), static_cast<rlim_t>(bytes)};
  const rlimit cpu{cpu_seconds, cpu_seconds};
  const rlimit no_core{0, 0};
  if (setrlimit(RLIMIT_AS, &memory) != 0 || setrlimit(RLIMIT_CPU, &cpu) != 0 ||
      setrlimit(RLIMIT_CORE, &no_core) != 0)
    throw std::runtime_error("could not apply process limits");
  cpu_set_t allowed;
  CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof(allowed), &allowed) != 0)
    throw std::runtime_error("could not read CPU affinity");
  int chosen = -1;
  for (int cpu_id = 0; cpu_id < CPU_SETSIZE; ++cpu_id)
    if (CPU_ISSET(cpu_id, &allowed)) { chosen = cpu_id; break; }
  cpu_set_t one;
  CPU_ZERO(&one);
  if (chosen < 0) throw std::runtime_error("no allowed CPU");
  CPU_SET(chosen, &one);
  if (sched_setaffinity(0, sizeof(one), &one) != 0)
    throw std::runtime_error("could not apply one-CPU affinity");
#else
  (void)bytes;
  (void)cpu_seconds;
#endif
}

struct Query {
  std::vector<bool> bits;
  Sum magnitude, phase;
  mundo_x::FactorizedCore::LogAmplitude actual{};
  long double magnitude_error = 0, phase_error = 0;
  bool passed = false, legacy_passed = false;
  explicit Query(std::size_t n) : bits(n) {}
};

void progress(const std::string& phase, Clock::time_point start) {
  std::cerr << "{\"phase\":\"" << phase << "\",\"elapsed_ms\":"
            << elapsed(start) << "}\n" << std::flush;
}

int examine(std::size_t n, std::size_t depth, std::uint64_t seed) {
  const auto total_start = Clock::now();
  auto phase_start = Clock::now();
  mundo_x::FactorizedCore core(n);
  const double initialization_ms = elapsed(phase_start);
  progress("initialized", total_start);
  phase_start = Clock::now();
  for (std::size_t q = 0; q < n; ++q) {
    auto rng = generator(seed, q);
    for (std::size_t step = 0; step < depth; ++step)
      production_gate(core, next_gate(rng), q);
  }
  const double evolution_ms = elapsed(phase_start);
  progress("evolved", total_start);

  phase_start = Clock::now();
  const auto operators = matrices();
  std::array<Query, 2> queries{{Query(n), Query(n)}};
  const std::size_t samples = std::min(n, kSignatureSamples);
  std::unordered_set<std::uint64_t> signatures;
  signatures.reserve(samples);
  std::uint64_t signature_digest = 1469598103934665603ULL;
  std::size_t next_sample = 0, flipped_bits = 0, protected_bits = 0;
  std::size_t local_failures = 0, worst_qubit = 0;
  long double max_local_error = 0, worst_reference = 0, worst_actual = 0;
  long double min_selected_mass = 1, max_reference_norm_error = 0;
  for (std::size_t q = 0; q < n; ++q) {
    Pair state{{Complex{1, 0}, Complex{0, 0}}};
    auto rng = generator(seed, q);
    std::uint64_t hash = 1469598103934665603ULL;
    for (std::size_t step = 0; step < depth; ++step) {
      const auto operation = next_gate(rng);
      state = matrix_product(operators[operation], state);
      hash = (hash ^ (operation + 1)) * 1099511628211ULL;
    }
    if (next_sample < samples && q == next_sample * n / samples) {
      signatures.insert(hash);
      signature_digest = (signature_digest ^ hash) * 1099511628211ULL;
      ++next_sample;
    }
    const long double norm0 = std::norm(state[0]), norm1 = std::norm(state[1]);
    const long double norm = norm0 + norm1;
    if (!(norm > 0) || !std::isfinite(norm))
      throw std::runtime_error("invalid independent reference norm");
    max_reference_norm_error = std::max(max_reference_norm_error, std::abs(norm - 1));
    const long double probability = norm1 / norm;
    const long double actual = core.probability_one(q);
    const long double error = std::abs(actual - probability);
    if (!std::isfinite(error) || error > kLocalTolerance) ++local_failures;
    if (error > max_local_error) {
      max_local_error = error;
      worst_qubit = q;
      worst_reference = probability;
      worst_actual = actual;
    }
    const bool maximum = norm1 > norm0;
    const bool supported_alternative = std::min(norm0, norm1) / norm >= kAlternativeMinimumMass;
    queries[0].bits[q] = maximum;
    queries[1].bits[q] = supported_alternative ? !maximum : maximum;
    if (supported_alternative) ++flipped_bits;
    else ++protected_bits;
    for (auto& query : queries) {
      const auto coefficient = state[query.bits[q] ? 1 : 0];
      const auto mass = std::norm(coefficient) / norm;
      min_selected_mass = std::min(min_selected_mass, mass);
      if (!(mass > 0)) throw std::runtime_error("selected unsupported reference coefficient");
      query.magnitude.add(std::log(std::abs(coefficient)));
      query.phase.add(std::arg(coefficient));
    }
  }
  const double validation_ms = elapsed(phase_start);
  progress("all_local_probabilities_validated", total_start);
  phase_start = Clock::now();
  for (auto& query : queries) query.actual = core.log_amplitude(query.bits);
  const double global_queries_ms = elapsed(phase_start);
  const long double two_pi = 2 * std::acos(-1.0L);
  bool global_pass = true, global_legacy_pass = true;
  for (auto& query : queries) {
    query.magnitude_error = std::abs(static_cast<long double>(query.actual.log_magnitude) - query.magnitude.value);
    query.phase_error = std::abs(std::remainder(static_cast<long double>(query.actual.phase) - query.phase.value, two_pi));
    const bool finite = std::isfinite(query.magnitude_error) && std::isfinite(query.phase_error);
    query.passed = !query.actual.zero && finite && query.magnitude_error <= kGlobalTolerance && query.phase_error <= kGlobalTolerance;
    query.legacy_passed = !query.actual.zero && finite && query.magnitude_error <= kLegacyGlobalTolerance && query.phase_error <= kLegacyGlobalTolerance;
    global_pass = global_pass && query.passed;
    global_legacy_pass = global_legacy_pass && query.legacy_passed;
  }
  const bool diversity_pass = signatures.size() * 100 >= samples * 95;
  const bool structure_pass = core.factor_count() == n && core.largest_factor_qubits() == 1 && core.amplitude_count() == n * 2;
  const bool passed = local_failures == 0 && global_pass && diversity_pass && structure_pass;
  progress("global_queries_validated", total_start);

  std::cout << std::setprecision(std::numeric_limits<long double>::max_digits10)
            << "{\"status\":\"" << (passed ? "ok" : "correctness_failure")
            << "\",\"family\":\"independent_random_clifford_t_products\",\"n\":" << n
            << ",\"depth\":" << depth << ",\"seed\":" << seed
            << ",\"production_gates\":" << n * depth << ",\"reference_gates\":" << n * depth
            << ",\"reference\":\"generic complex<long double> 2x2 matrix-vector multiplication\""
            << ",\"long_double_mantissa_bits\":" << std::numeric_limits<long double>::digits
            << ",\"thresholds\":{\"local_probability\":3e-11,\"log_magnitude_and_phase\":1e-7,\"legacy_log_magnitude_and_phase\":1e-8,\"global_threshold_is_heuristic\":true}"
            << ",\"all_local_probabilities_checked\":" << n << ",\"local_failures\":" << local_failures
            << ",\"max_local_probability_abs_error\":" << max_local_error
            << ",\"worst_local_qubit\":" << worst_qubit << ",\"worst_local_reference\":" << worst_reference
            << ",\"worst_local_actual\":" << worst_actual
            << ",\"max_reference_norm_abs_error\":" << max_reference_norm_error
            << ",\"alternative_query_flipped_bits\":" << flipped_bits
            << ",\"alternative_query_protected_near_zero_bits\":" << protected_bits
            << ",\"minimum_selected_reference_probability\":" << min_selected_mass
            << ",\"query_selection_changes_state\":false,\"alternative_minimum_probability\":1e-4"
            << ",\"signature_samples\":" << samples << ",\"distinct_sampled_gate_trace_hashes\":" << signatures.size()
            << ",\"gate_trace_signature64\":\"" << std::hex << signature_digest << std::dec << "\""
            << ",\"gate_trace_diversity_pass\":" << (diversity_pass ? "true" : "false")
            << ",\"gate_trace_diversity_implies_unique_physical_states\":false"
            << ",\"factor_count\":" << core.factor_count() << ",\"largest_factor_qubits\":" << core.largest_factor_qubits()
            << ",\"amplitude_count\":" << core.amplitude_count() << ",\"logical_amplitude_bytes\":" << core.logical_amplitude_bytes()
            << ",\"initialization_ms\":" << initialization_ms << ",\"evolution_ms\":" << evolution_ms
            << ",\"reference_validation_and_query_selection_ms\":" << validation_ms
            << ",\"global_queries_ms\":" << global_queries_ms << ",\"total_ms\":" << elapsed(total_start)
            << ",\"peak_rss_bytes_self_proc\":" << real_peak_rss()
            << ",\"rss_method\":\"fresh worker /proc/self/status VmHWM; not inherited getrusage\""
            << ",\"legacy_global_tolerance_pass\":" << (global_legacy_pass ? "true" : "false")
            << ",\"global_queries\":[";
  for (std::size_t index = 0; index < queries.size(); ++index) {
    const auto& query = queries[index];
    if (index) std::cout << ',';
    std::cout << "{\"name\":\"" << (index == 0 ? "maximum_coefficient" : "supported_alternative")
              << "\",\"reference_log_magnitude\":" << query.magnitude.value
              << ",\"reference_phase\":" << std::remainder(query.phase.value, two_pi)
              << ",\"actual_zero\":" << (query.actual.zero ? "true" : "false")
              << ",\"actual_log_magnitude\":" << query.actual.log_magnitude
              << ",\"actual_phase\":" << query.actual.phase
              << ",\"log_magnitude_abs_error\":" << query.magnitude_error
              << ",\"phase_circular_abs_error\":" << query.phase_error
              << ",\"pass\":" << (query.passed ? "true" : "false")
              << ",\"legacy_1e8_pass\":" << (query.legacy_passed ? "true" : "false") << '}';
  }
  std::cout << "]}\n";
  return passed ? 0 : 1;
}
}  // namespace

int main(int argc, char** argv) {
  std::size_t n = 128, depth = 32, bytes = std::size_t{2} * 1024 * 1024 * 1024;
  std::uint64_t seed = 0x4d554e444f585f44ULL;
  unsigned cpu_seconds = 45;
  try {
    for (int index = 1; index < argc; index += 2) {
      if (index + 1 >= argc) throw std::invalid_argument("missing option value");
      const std::string option = argv[index];
      const auto value = std::stoull(argv[index + 1], nullptr, 0);
      if (option == "--n") n = value;
      else if (option == "--depth") depth = value;
      else if (option == "--seed") seed = value;
      else if (option == "--max-bytes") bytes = value;
      else if (option == "--cpu-seconds") cpu_seconds = static_cast<unsigned>(value);
      else throw std::invalid_argument("unknown option");
    }
    if (n == 0 || n > 1000000 || (depth != 32 && depth != 64) ||
        bytes < 64 * 1024 * 1024 || bytes > std::size_t{2} * 1024 * 1024 * 1024 ||
        cpu_seconds == 0 || cpu_seconds > 45)
      throw std::invalid_argument("exam bounds: n 1..1000000, depth 32 or 64, memory <=2GiB, CPU <=45s");
    limits(bytes, cpu_seconds);
    return examine(n, depth, seed);
  } catch (const std::exception& error) {
    std::cerr << "native diversity exam: " << error.what() << '\n';
    std::cout << "{\"status\":\"runtime_failure\"}\n";
    return 2;
  }
}
