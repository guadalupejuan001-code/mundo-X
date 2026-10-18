// Standalone worker for tools/stress_factorized.py. No production DenseCore is
// used: the double statevector below is an independent implementation.
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/factorized_core.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>
#ifdef __linux__
#include <sched.h>
#endif

namespace {
using Complex = std::complex<double>;
using Clock = std::chrono::steady_clock;
struct Gate { char op; std::size_t a, b = 0; };
struct Options {
  std::string method = "factorized", family = "product_ht";
  std::size_t n = 4, max_block = 20, expected_factors = 0;
  std::uint64_t seed = 20261017;
  std::uint64_t memory_bytes = 1024ULL * 1024 * 1024;
  std::uint64_t amplitude_budget = 256ULL * 1024 * 1024;
  unsigned cpu_seconds = 15;
  bool expect_rejection = false;
};

std::string quote(const std::string& s) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (c == '\t') out << "\\t";
    else if (c < 32) out << "\\u" << std::hex << std::setw(4)
                         << std::setfill('0') << static_cast<int>(c) << std::dec;
    else out << c;
  }
  out << '"';
  return out.str();
}

double elapsed(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double, std::milli>(b - a).count();
}

std::uint64_t rss_bytes() {
  // Unlike ru_maxrss, VmHWM describes this executable after exec, not a Python
  // supervisor's pre-exec high water mark. A missing field is explicitly zero.
  std::ifstream status("/proc/self/status");
  std::string key;
  while (status >> key) {
    if (key == "VmHWM:") {
      std::uint64_t kib = 0;
      status >> kib;
      return kib * 1024;
    }
    status.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
  }
  return 0;
}

void progress(const char* phase, double init_ms, double evolution_ms) {
  std::cerr << std::setprecision(17) << "{\"phase\":" << quote(phase)
            << ",\"initialization_ms\":" << init_ms
            << ",\"evolution_ms\":" << evolution_ms << "}" << std::endl;
}

Options options(int argc, char** argv) {
  Options o;
  for (int i = 1; i < argc; ++i) {
    const std::string key = argv[i];
    if (++i == argc) throw std::invalid_argument("missing option value");
    const std::string value = argv[i];
    if (key == "--method") o.method = value;
    else if (key == "--family") o.family = value;
    else if (key == "--qubits") o.n = std::stoull(value);
    else if (key == "--max-block-qubits") o.max_block = std::stoull(value);
    else if (key == "--logical-amplitude-bytes") o.amplitude_budget = std::stoull(value);
    else if (key == "--memory-bytes") o.memory_bytes = std::stoull(value);
    else if (key == "--cpu-seconds") o.cpu_seconds = std::stoul(value);
    else if (key == "--expect-factor-count") o.expected_factors = std::stoull(value);
    else if (key == "--expect-budget-rejection") o.expect_rejection = value == "1";
    else if (key == "--seed") o.seed = std::stoull(value);
    else throw std::invalid_argument("unknown option: " + key);
  }
  if (!o.n || o.n > 10000000 || !o.max_block || o.max_block > 25 ||
      !o.amplitude_budget || o.amplitude_budget > 512ULL * 1024 * 1024 ||
      !o.memory_bytes || o.memory_bytes > 4ULL * 1024 * 1024 * 1024 ||
      !o.cpu_seconds || o.cpu_seconds > 15)
    throw std::invalid_argument("requested worker size or budget exceeds ceiling");
  return o;
}

int limits(const Options& o) {
  const rlimit address{static_cast<rlim_t>(o.memory_bytes), static_cast<rlim_t>(o.memory_bytes)};
  const rlimit cpu{static_cast<rlim_t>(o.cpu_seconds), static_cast<rlim_t>(o.cpu_seconds + 1)};
  const rlimit core{0, 0};
  if (setrlimit(RLIMIT_AS, &address) || setrlimit(RLIMIT_CPU, &cpu) || setrlimit(RLIMIT_CORE, &core))
    throw std::runtime_error("cannot install worker resource limits");
#ifdef __linux__
  cpu_set_t allowed, chosen;
  CPU_ZERO(&allowed);
  if (sched_getaffinity(0, sizeof(allowed), &allowed))
    throw std::runtime_error("cannot inspect CPU affinity");
  for (int i = 0; i < CPU_SETSIZE; ++i) {
    if (!CPU_ISSET(i, &allowed)) continue;
    CPU_ZERO(&chosen);
    CPU_SET(i, &chosen);
    if (sched_setaffinity(0, sizeof(chosen), &chosen))
      throw std::runtime_error("cannot restrict worker to one CPU");
    return i;
  }
  throw std::runtime_error("empty CPU affinity mask");
#else
  throw std::runtime_error("Linux /proc and CPU affinity are required by this worker");
#endif
}

std::vector<Gate> circuit(const Options& o) {
  std::vector<Gate> g;
  auto emit = [&](char op, std::size_t a, std::size_t b = 0) { g.push_back({op, a, b}); };
  if (o.family == "product_ht" || o.family == "product_t2") {
    for (std::size_t q = 0; q < o.n; ++q) emit('H', q);
    for (std::size_t q = 0; q < o.n; ++q) {
      emit('T', q);
      if (o.family == "product_t2") emit('T', q);
    }
  } else if (o.family == "bell_pairs") {
    for (std::size_t q = 0; q + 1 < o.n; q += 2) {
      emit('H', q); emit('C', q, q + 1);
    }
  } else if (o.family == "ghz") {
    emit('H', 0);
    for (std::size_t q = 1; q < o.n; ++q) emit('C', q - 1, q);
  } else if (o.family == "bridge_uncompute") {
    if (o.n < 4) throw std::invalid_argument("bridge requires at least4 qubits");
    emit('H', 0); emit('C', 0, 1); emit('H', 2); emit('C', 2, 3);
    emit('C', 1, 2); emit('C', 1, 2);
  } else throw std::invalid_argument("unknown circuit family");
  return g;
}

std::string circuit_hash(std::size_t n, const std::vector<Gate>& gates) {
  std::uint64_t h = 14695981039346656037ULL;
  auto word = [&](std::uint64_t value) {
    for (unsigned k = 0; k < 8; ++k) { h ^= (value >> (8 * k)) & 255; h *= 1099511628211ULL; }
  };
  word(n);
  for (const auto& g : gates) { word(static_cast<unsigned char>(g.op)); word(g.a); word(g.b); }
  std::ostringstream out;
  out << std::hex << std::setw(16) << std::setfill('0') << h;
  return out.str();
}

// Deliberately independent double-precision reference, not DenseCore.
class IndependentDense {
 public:
  explicit IndependentDense(std::size_t n) : n_(n) {
    if (!n || n >= std::numeric_limits<std::size_t>::digits)
      throw std::invalid_argument("independent dense shift outside addressable range");
    state.assign(std::size_t{1} << n, Complex{});
    state[0] = 1.0;
  }
  void apply(const Gate& g) {
    const std::size_t bit = std::size_t{1} << g.a;
    if (g.op == 'H') {
      const double scale = 1.0 / std::sqrt(2.0);
      for (std::size_t base = 0; base < state.size(); base += bit * 2)
        for (std::size_t j = 0; j < bit; ++j) {
          const Complex a = state[base + j], b = state[base + j + bit];
          state[base + j] = (a + b) * scale;
          state[base + j + bit] = (a - b) * scale;
        }
    } else if (g.op == 'T') {
      const double scale = 1.0 / std::sqrt(2.0);
      for (std::size_t base = 0; base < state.size(); base += bit * 2)
        for (std::size_t j = 0; j < bit; ++j) state[base + j + bit] *= Complex{scale, scale};
    } else if (g.op == 'C') {
      const std::size_t target = std::size_t{1} << g.b;
      for (std::size_t i = 0; i < state.size(); ++i)
        if ((i & bit) && !(i & target)) std::swap(state[i], state[i | target]);
    } else throw std::invalid_argument("unknown independent dense gate");
  }
  double probability_one(std::size_t q) const {
    const std::size_t bit = std::size_t{1} << q;
    double numerator = 0, denominator = 0;
    for (std::size_t i = 0; i < state.size(); ++i) {
      const double mass = std::norm(state[i]);
      denominator += mass;
      if (i & bit) numerator += mass;
    }
    return numerator / denominator;
  }
  double expectation_z(std::size_t q) const { return 1.0 - 2.0 * probability_one(q); }
  Complex amplitude(const std::vector<bool>& bits) const {
    if (bits.size() != n_) throw std::invalid_argument("independent dense basis width mismatch");
    std::size_t index = 0;
    for (std::size_t q = 0; q < n_; ++q) if (bits[q]) index |= std::size_t{1} << q;
    return state[index];
  }
  std::vector<Complex> state;
 private:
  std::size_t n_;
};

template <typename Engine> void apply(Engine& q, const Gate& g) {
  if (g.op == 'H') q.hadamard(g.a);
  else if (g.op == 'T') q.phase_t(g.a);
  else if (g.op == 'C') q.cnot(g.a, g.b);
  else throw std::invalid_argument("unknown benchmark gate");
}

const mundo_x::FactorizedCore& core(const mundo_x::FactorizedCore& q) { return q; }

std::unique_ptr<mundo_x::FactorizedCore> make_factorized(const Options& o) {
  return std::make_unique<mundo_x::FactorizedCore>(o.n, o.max_block, o.amplitude_budget);
}

struct Stage {
  std::string phase;
  std::size_t factors, largest, amplitudes, payload;
};
struct LogProbe { bool zero = true; double log_magnitude = 0, phase = 0; };
struct Result {
  std::string status = "ok", query_status = "not_run", validation = "not_run", reason;
  std::string payload_kind = "complex_double_amplitudes_only_excludes_all_metadata";
  std::string preservation_scope = "not_applicable";
  double init_ms = 0, evolution_ms = 0, refactor_ms = 0, query_ms = 0, max_query_error = 0;
  double max_log_error = 0, max_phase_error = 0;
  std::size_t executed = 0, amplitudes = 0, payload = 0, factors = 0, largest = 0, components = 0;
  bool factor_metrics = false, query_ran = false, budget_checked = false, state_preserved = false;
  bool manual_split_attempted = false, manual_split_success = false;
  std::vector<Stage> stages;
  std::vector<double> probabilities, expectations;
  Complex amplitude_zero{}, amplitude_one{};
  LogProbe log_zero, log_one;
};

void stage(Result& r, const mundo_x::FactorizedCore& q, const std::string& phase) {
  r.stages.push_back({phase, q.factor_count(), q.largest_factor_qubits(),
                     q.amplitude_count(), q.logical_amplitude_bytes()});
}

struct Signature {
  std::vector<Complex> amplitudes;
  std::vector<double> probabilities;
  std::vector<std::vector<std::size_t>> partition;
  std::size_t amplitude_count = 0;
  bool complete = false;
  bool operator==(const Signature& b) const {
    return amplitudes == b.amplitudes && probabilities == b.probabilities &&
           partition == b.partition && amplitude_count == b.amplitude_count;
  }
};

Signature signature(const mundo_x::FactorizedCore& q) {
  Signature result;
  result.partition = q.block_partition();
  result.amplitude_count = q.amplitude_count();
  if (q.qubits() <= 12) {
    result.amplitudes = q.dense_state(12);
    result.complete = true;
  } else {
    // Larger refusal probes inspect every one-qubit probability and two global
    // amplitudes. This is explicitly labelled a signature, not full equality.
    result.amplitudes.push_back(q.amplitude(std::vector<bool>(q.qubits(), false)));
    result.amplitudes.push_back(q.amplitude(std::vector<bool>(q.qubits(), true)));
    for (std::size_t i = 0; i < q.qubits(); ++i) result.probabilities.push_back(q.probability_one(i));
  }
  return result;
}

double expected_probability(const Options& o, std::size_t q) {
  if (o.family == "bell_pairs" && o.n % 2 && q == o.n - 1) return 0;
  if (o.family == "bridge_uncompute" && q >= 4) return 0;
  return 0.5;
}

std::pair<Complex, Complex> expected_amplitudes(const Options& o) {
  if (o.family == "ghz") {
    const double v = 1.0 / std::sqrt(2.0);
    return {Complex{v, 0}, Complex{v, 0}};
  }
  if (o.family == "bridge_uncompute") return {Complex{0.5, 0}, Complex{o.n == 4 ? 0.5 : 0.0, 0}};
  if (o.family == "bell_pairs") {
    const double v = std::pow(0.5, static_cast<double>(o.n / 2) / 2.0);
    return {Complex{v, 0}, Complex{o.n % 2 ? 0 : v, 0}};
  }
  const double v = std::pow(0.5, static_cast<double>(o.n) / 2.0);
  const double pi = std::acos(-1.0);
  const double angle = o.family == "product_t2" ? pi * static_cast<double>(o.n % 4) / 2.0
                                                : pi * static_cast<double>(o.n % 8) / 4.0;
  return {Complex{v, 0}, v * Complex{std::cos(angle), std::sin(angle)}};
}

LogProbe log_probe(const mundo_x::FactorizedCore& q, const std::vector<bool>& bits) {
  const auto result = q.log_amplitude(bits);
  return {result.zero, result.log_magnitude, result.phase};
}
LogProbe log_probe(const IndependentDense& q, const std::vector<bool>& bits) {
  const auto value = q.amplitude(bits);
  if (value == Complex{}) return {};
  return {false, std::log(std::abs(value)), std::arg(value)};
}

std::pair<LogProbe, LogProbe> expected_logs(const Options& o) {
  const double ln2 = std::log(2.0), pi = std::acos(-1.0);
  if (o.family == "ghz") return {{false, -ln2 / 2, 0}, {false, -ln2 / 2, 0}};
  if (o.family == "bridge_uncompute")
    return {{false, -ln2, 0}, {o.n != 4, -ln2, 0}};
  if (o.family == "bell_pairs") {
    const double value = -static_cast<double>(o.n / 2) * ln2 / 2;
    return {{false, value, 0}, {o.n % 2 != 0, value, 0}};
  }
  const double value = -static_cast<double>(o.n) * ln2 / 2;
  const double phase = o.family == "product_t2" ? static_cast<double>(o.n % 4) * pi / 2
                                               : static_cast<double>(o.n % 8) * pi / 4;
  return {{false, value, 0}, {false, value, phase}};
}

template <typename QueryEngine>
void queries(Result& r, const QueryEngine& q, const Options& o) {
  const std::vector<std::size_t> sites = {0, o.n / 2, o.n - 1};
  // Input preparation is outside the query timer and RSS still includes it.
  const std::vector<bool> zeros(o.n, false), ones(o.n, true);
  auto start = Clock::now();
  for (auto i : sites) r.probabilities.push_back(q.probability_one(i));
  for (auto i : sites) r.expectations.push_back(q.expectation_z(i));
  r.amplitude_zero = q.amplitude(zeros);
  r.amplitude_one = q.amplitude(ones);
  r.log_zero = log_probe(q, zeros);
  r.log_one = log_probe(q, ones);
  auto done = Clock::now();
  r.query_ms = elapsed(start, done);
  r.query_ran = true;
  r.query_status = "three_local_probabilities_three_Z_expectations_two_raw_and_log_basis_amplitudes";
  for (std::size_t j = 0; j < sites.size(); ++j) {
    const double p = expected_probability(o, sites[j]);
    r.max_query_error = std::max(r.max_query_error, std::abs(r.probabilities[j] - p));
    r.max_query_error = std::max(r.max_query_error, std::abs(r.expectations[j] - (1.0 - 2.0 * p)));
  }
  const auto expected = expected_amplitudes(o);
  r.max_query_error = std::max(r.max_query_error, std::abs(r.amplitude_zero - expected.first));
  r.max_query_error = std::max(r.max_query_error, std::abs(r.amplitude_one - expected.second));
  const auto expected_log = expected_logs(o);
  bool zero_flags_match = true;
  bool finite = std::all_of(r.probabilities.begin(), r.probabilities.end(), [](double value) { return std::isfinite(value); }) &&
                std::all_of(r.expectations.begin(), r.expectations.end(), [](double value) { return std::isfinite(value); }) &&
                std::isfinite(r.amplitude_zero.real()) && std::isfinite(r.amplitude_zero.imag()) &&
                std::isfinite(r.amplitude_one.real()) && std::isfinite(r.amplitude_one.imag());
  auto check_log = [&](const LogProbe& actual, const LogProbe& reference) {
    if (actual.zero != reference.zero) { zero_flags_match = false; return; }
    if (reference.zero) return;
    if (!std::isfinite(actual.log_magnitude) || !std::isfinite(actual.phase)) { finite = false; return; }
    r.max_log_error = std::max(r.max_log_error, std::abs(actual.log_magnitude - reference.log_magnitude));
    r.max_phase_error = std::max(r.max_phase_error, std::abs(std::remainder(actual.phase - reference.phase, 2 * std::acos(-1.0))));
  };
  check_log(r.log_zero, expected_log.first);
  check_log(r.log_one, expected_log.second);
  if (!finite || !std::isfinite(r.max_query_error) || r.max_query_error > 1e-9 || !zero_flags_match ||
      !std::isfinite(r.max_log_error) || r.max_log_error > 1e-8 ||
      !std::isfinite(r.max_phase_error) || r.max_phase_error > 1e-8) r.status = "correctness_failure";
  r.validation = "analytic_family_selected_local_queries_and_two_amplitudes_not_full_state";
}

void manual_refactor(Result& r, mundo_x::FactorizedCore& q, const Options& o) {
  if (o.family != "bridge_uncompute") return;
  r.manual_split_attempted = true;
  const auto start = Clock::now();
  r.manual_split_success = q.try_split({0, 1});
  const auto done = Clock::now();
  r.refactor_ms = elapsed(start, done);
  r.evolution_ms += r.refactor_ms;
  stage(r, q, "after_explicit_known_cut_split");
}

template <typename Factory>
Result run_factor(const Options& o, const std::vector<Gate>& g, Factory factory) {
  Result r;
  auto start = Clock::now();
  auto owner = factory(o);
  auto ready = Clock::now();
  auto& q = *owner;
  r.init_ms = elapsed(start, ready);
  progress("initialization_complete", r.init_ms, 0);
  stage(r, core(q), "initialized");
  bool rejected = false;
  // Signature collection is restricted to small refusal probes and kept out
  // of the evolution timer. Product100000 never reconstructs or scans a state.
  if (o.family == "product_ht" || o.family == "product_t2" || o.family == "bell_pairs") {
    // Batch the many constant-sized gates: do not add a clock call per gate
    // when measuring the linear100000-qubit structured families.
    start = Clock::now();
    for (const auto& gate : g) apply(q, gate);
    r.evolution_ms = elapsed(start, Clock::now());
    r.executed = g.size();
  } else for (const auto& gate : g) {
    const bool refusal_candidate =
        (o.family == "ghz" && gate.op == 'C' && gate.b >= o.max_block) ||
        (o.family == "bridge_uncompute" && r.executed == 4);
    Signature before;
    if (refusal_candidate) before = signature(core(q));
    start = Clock::now();
    try {
      apply(q, gate);
    } catch (const std::length_error& e) {
      const auto done = Clock::now();
      r.evolution_ms += elapsed(start, done);
      rejected = true;
      r.reason = e.what();
      r.budget_checked = refusal_candidate;
      if (refusal_candidate) {
        const auto after = signature(core(q));
        r.state_preserved = before == after;
        r.preservation_scope = before.complete ? "all_amplitudes_exact_and_block_partition"
                                              : "all_one_qubit_probabilities_two_amplitudes_and_block_partition";
      }
      r.status = o.expect_rejection && r.budget_checked && r.state_preserved
                     ? "expected_budget_rejection" : "unexpected_budget_rejection";
      r.validation = "rejected_gate_before_after_state_preservation";
      stage(r, core(q), "budget_rejected");
      break;
    }
    r.evolution_ms += elapsed(start, Clock::now());
    ++r.executed;
    if (o.family == "bridge_uncompute") {
      if (r.executed == 4) stage(r, core(q), "two_independent_Bell_pairs");
      else if (r.executed == 5) stage(r, core(q), "bridge_applied");
      else if (r.executed == 6) stage(r, core(q), "bridge_undone_before_manual_split");
    }
  }
  if (!rejected) {
    manual_refactor(r, q, o);
    if (o.expect_rejection) {
      r.status = "correctness_failure";
      r.reason = "expected budget rejection did not occur";
    }
  }
  progress("evolution_complete", r.init_ms, r.evolution_ms);
  r.factor_metrics = true;
  r.factors = core(q).factor_count();
  r.largest = core(q).largest_factor_qubits();
  r.amplitudes = core(q).amplitude_count();
  r.payload = core(q).logical_amplitude_bytes();
  if (!rejected && o.expected_factors && r.factors != o.expected_factors) {
    r.status = "correctness_failure";
    r.reason = "expected factor count was not recovered";
  }
  if (!rejected) queries(r, core(q), o);
  else r.query_status = "rejection_preservation_queries_outside_evolution_timer";
  progress("queries_complete", r.init_ms, r.evolution_ms);
  return r;
}

Result run_reduced(const Options& o, const std::vector<Gate>& g) {
  Result r;
  auto start = Clock::now();
  mundo_x::ReducedExactTRepresentation q(o.n);
  auto ready = Clock::now();
  for (const auto& gate : g) apply(q, gate);
  auto done = Clock::now();
  r.init_ms = elapsed(start, ready);
  r.evolution_ms = elapsed(ready, done);
  progress("evolution_complete", r.init_ms, r.evolution_ms);
  r.executed = g.size();
  r.components = q.component_count();
  r.amplitudes = q.component_count();
  r.payload = q.component_count() * sizeof(Complex);
  r.payload_kind = "complex_double_coefficients_only_excludes_keys_tableau_circuit_hashmap_and_allocator";
  r.query_status = "not_run_no_native_local_query_api_no_dense_reconstruction";
  r.validation = "not_run_timing_worker_covered_by_separate_correctness_suite";
  return r;
}

Result run_dense(const Options& o, const std::vector<Gate>& g) {
  Result r;
  auto start = Clock::now();
  IndependentDense q(o.n);
  auto ready = Clock::now();
  for (const auto& gate : g) q.apply(gate);
  auto done = Clock::now();
  r.init_ms = elapsed(start, ready);
  r.evolution_ms = elapsed(ready, done);
  progress("evolution_complete", r.init_ms, r.evolution_ms);
  r.executed = g.size();
  r.amplitudes = q.state.size();
  r.payload = q.state.size() * sizeof(Complex);
  queries(r, q, o);
  return r;
}

void numbers(std::ostream& out, const std::vector<double>& xs) {
  out << '[';
  for (std::size_t i = 0; i < xs.size(); ++i) { if (i) out << ','; out << xs[i]; }
  out << ']';
}

void output(const Options& o, const std::vector<Gate>& g, const Result& r, int cpu) {
  auto optional_number = [&](double value) {
    if (r.query_ran) std::cout << value; else std::cout << "null";
  };
  auto underflow = [](const LogProbe& value) {
    // Repeated multiplication can stick at denorm_min instead of reaching0.
    // A finite log below the smallest normal double flags that loss of range
    // even when the raw complex value is a nonzero subnormal artifact.
    return !value.zero && value.log_magnitude < std::log(std::numeric_limits<double>::min());
  };
  std::cout << std::setprecision(17)
            << "{\"status\":" << quote(r.status) << ",\"method\":" << quote(o.method)
            << ",\"family\":" << quote(o.family) << ",\"qubits\":" << o.n
            << ",\"seed\":" << o.seed << ",\"gate_count\":" << g.size()
            << ",\"executed_gate_count\":" << r.executed
            << ",\"circuit_fnv1a64\":" << quote(circuit_hash(o.n, g))
            << ",\"initialization_ms\":" << r.init_ms << ",\"evolution_ms\":" << r.evolution_ms
            << ",\"refactorization_ms\":" << r.refactor_ms << ",\"query_ms\":";
  if (r.query_ran) std::cout << r.query_ms; else std::cout << "null";
  std::cout << ",\"query_status\":" << quote(r.query_status) << ",\"max_query_error\":";
  optional_number(r.max_query_error);
  std::cout << ",\"max_log_magnitude_error\":";
  optional_number(r.max_log_error);
  std::cout << ",\"max_log_phase_error\":";
  optional_number(r.max_phase_error);
  std::cout << ",\"amplitude_count\":" << r.amplitudes
            << ",\"logical_amplitude_bytes\":" << r.payload
            << ",\"amplitude_payload_kind\":" << quote(r.payload_kind)
            << ",\"metadata_payload_bytes\":null,\"factor_count\":";
  if (r.factor_metrics) std::cout << r.factors; else std::cout << "null";
  std::cout << ",\"largest_factor_qubits\":";
  if (r.factor_metrics) std::cout << r.largest; else std::cout << "null";
  std::cout << ",\"component_count\":";
  if (o.method == "reduced") std::cout << r.components; else std::cout << "null";
  std::cout << ",\"validation\":" << quote(r.validation)
            << ",\"peak_rss_bytes\":" << rss_bytes()
            << ",\"peak_rss_source\":\"linux_proc_self_VmHWM\""
            << ",\"worker_address_space_limit_bytes\":" << o.memory_bytes
            << ",\"logical_amplitude_budget_bytes\":" << o.amplitude_budget
            << ",\"max_block_qubits\":" << o.max_block
            << ",\"allowed_cpu_count\":1,\"selected_cpu\":" << cpu
            << ",\"dense_materialization_in_evolution_timer\":false"
            << ",\"evolution_includes_requested_refactorization\":true"
            << ",\"manual_split_attempted\":" << (r.manual_split_attempted ? "true" : "false")
            << ",\"manual_split_success\":" << (r.manual_split_success ? "true" : "false")
            << ",\"budget_state_preserved\":";
  if (r.budget_checked) std::cout << (r.state_preserved ? "true" : "false"); else std::cout << "null";
  std::cout << ",\"budget_preservation_scope\":" << quote(r.preservation_scope)
            << ",\"reason\":" << quote(r.reason)
            << ",\"query_sites\":[0," << o.n / 2 << ',' << o.n - 1 << "]"
            << ",\"probabilities\":";
  numbers(std::cout, r.probabilities);
  std::cout << ",\"expectations_z\":";
  numbers(std::cout, r.expectations);
  std::cout << ",\"basis_amplitudes\":";
  if (r.query_ran)
    std::cout << "[[" << r.amplitude_zero.real() << ',' << r.amplitude_zero.imag()
              << "],[" << r.amplitude_one.real() << ',' << r.amplitude_one.imag() << "]]";
  else std::cout << "null";
  std::cout << ",\"raw_zero_amplitude_underflow\":";
  if (r.query_ran) std::cout << (underflow(r.log_zero) ? "true" : "false"); else std::cout << "null";
  std::cout << ",\"raw_one_amplitude_underflow\":";
  if (r.query_ran) std::cout << (underflow(r.log_one) ? "true" : "false"); else std::cout << "null";
  std::cout << ",\"raw_underflow_meaning\":\"log_magnitude_below_smallest_normal_double_raw_may_be_zero_or_subnormal\""
            << ",\"log_basis_amplitudes\":";
  auto print_log = [](const LogProbe& value) {
    std::cout << "{\"zero\":" << (value.zero ? "true" : "false") << ",\"log_magnitude\":";
    if (value.zero) std::cout << "null"; else std::cout << value.log_magnitude;
    std::cout << ",\"phase\":";
    if (value.zero) std::cout << "null"; else std::cout << value.phase;
    std::cout << '}';
  };
  if (r.query_ran) {
    std::cout << '[';
    print_log(r.log_zero);
    std::cout << ',';
    print_log(r.log_one);
    std::cout << ']';
  } else std::cout << "null";
  std::cout << ",\"stages\":[";
  for (std::size_t i = 0; i < r.stages.size(); ++i) {
    if (i) std::cout << ',';
    const auto& s = r.stages[i];
    std::cout << "{\"phase\":" << quote(s.phase) << ",\"factor_count\":" << s.factors
              << ",\"largest_factor_qubits\":" << s.largest << ",\"amplitude_count\":" << s.amplitudes
              << ",\"logical_amplitude_bytes\":" << s.payload << '}';
  }
  std::cout << "]}\n";
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const auto o = options(argc, argv);
    const int cpu = limits(o);
    const auto gates = circuit(o);
    Result r;
    if (o.method == "factorized") r = run_factor(o, gates, make_factorized);
    else if (o.method == "reduced") r = run_reduced(o, gates);
    else if (o.method == "dense") r = run_dense(o, gates);
    else throw std::invalid_argument("unknown method");
    output(o, gates, r, cpu);
    return r.status == "ok" || r.status == "expected_budget_rejection" ? 0 : 2;
  } catch (const std::bad_alloc& e) {
    std::cout << "{\"status\":\"allocation_failure\",\"reason\":" << quote(e.what())
              << ",\"peak_rss_bytes\":" << rss_bytes() << "}\n";
    return 3;
  } catch (const std::invalid_argument& e) {
    std::cout << "{\"status\":\"invalid_argument\",\"reason\":" << quote(e.what())
              << ",\"peak_rss_bytes\":" << rss_bytes() << "}\n";
    return 4;
  } catch (const std::exception& e) {
    std::cout << "{\"status\":\"runtime_failure\",\"reason\":" << quote(e.what())
              << ",\"peak_rss_bytes\":" << rss_bytes() << "}\n";
    return 4;
  }
}
