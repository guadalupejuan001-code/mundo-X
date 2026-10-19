// Reproducible native-host comparison of the same repository-owned core at
// 20, 60 and 1000 qubits. Timings are measurements, never pass/fail thresholds.
// Graphics queries retain every generator plus local X/Z: 3*n operators, so
// the 1000-qubit case really evaluates 3000 observables per query batch.
#include "mundo_x/engine_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Session = mundo_x::EngineSession;
using Gate = Session::Gate;
using Clock = std::chrono::steady_clock;
volatile double result_sink = 0.;

struct Options {
  std::string json_path;
  std::size_t repetitions = 11, warmup = 3, cached_batches = 4;
};

void require(bool condition, const char* context) {
  if (!condition) throw std::runtime_error(context);
}

std::string quote(const std::string& text) {
  std::ostringstream out; out << '"';
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (c == '\t') out << "\\t";
    else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0')
                         << static_cast<int>(c) << std::dec;
    else out << c;
  }
  out << '"'; return out.str();
}

Options parse_options(int argc, char** argv) {
  Options options;
  for (int i = 1; i < argc; ++i) {
    const std::string option = argv[i];
    if (++i == argc) throw std::invalid_argument("Missing option value");
    const std::string value = argv[i];
    if (option == "--json") options.json_path = value;
    else {
      if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::invalid_argument("Count options require nonnegative decimal integers");
      const auto count = std::stoull(value);
      if (count > 1000) throw std::invalid_argument("Benchmark count exceeds 1000");
      if (option == "--repetitions") options.repetitions = static_cast<std::size_t>(count);
      else if (option == "--warmup") options.warmup = static_cast<std::size_t>(count);
      else if (option == "--cached-batches") options.cached_batches = static_cast<std::size_t>(count);
      else throw std::invalid_argument("Unknown option: " + option);
    }
  }
  if (!options.repetitions || options.repetitions > 101 || options.warmup > 10 ||
      !options.cached_batches || options.cached_batches > 32)
    throw std::invalid_argument("Allowed counts: repetitions1..101, warmup0..10, cached-batches1..32");
  return options;
}

std::vector<Gate> circuit(std::size_t n, const std::string& family) {
  std::vector<Gate> gates;
  if (family == "ghz") {
    gates.push_back({MX_GATE_H, 0, 0});
    for (std::size_t q = 1; q < n; ++q) gates.push_back({MX_GATE_CX, q - 1, q});
  } else if (family == "graphics_graph") {
    // This is exactly graphics-desktop/engine.py: graph_program(n).
    for (std::size_t q = 0; q < n; ++q) gates.push_back({MX_GATE_H, q, 0});
    for (std::size_t q = 0; q + 1 < n; ++q) gates.push_back({MX_GATE_CZ, q, q + 1});
    for (std::size_t q = 0; q + 3 < n; q += 2) gates.push_back({MX_GATE_CZ, q, q + 3});
    for (std::size_t q = 1; q < n; q += 3) gates.push_back({MX_GATE_S, q, 0});
  } else throw std::invalid_argument("Unknown benchmark family");
  return gates;
}

std::vector<std::string> graphic_observables(const Session& core) {
  std::vector<std::string> words;
  const auto n = core.qubits();
  words.reserve(3 * n);
  for (std::size_t r = 0; r < n; ++r) words.push_back(core.generator(r));
  for (const char axis : {'X', 'Z'}) {
    for (std::size_t q = 0; q < n; ++q) {
      std::string local(n, 'I'); local[q] = axis; words.push_back(std::move(local));
    }
  }
  return words;
}

std::string circuit_hash(std::size_t n, const std::vector<Gate>& gates) {
  std::uint64_t hash = 14695981039346656037ULL;
  auto word = [&](std::uint64_t value) {
    for (unsigned i = 0; i < 8; ++i) { hash ^= (value >> (8 * i)) & 255U; hash *= 1099511628211ULL; }
  };
  word(n);
  for (const auto& gate : gates) { word(gate.operation); word(gate.first); word(gate.second); }
  std::ostringstream out; out << std::hex << std::setw(16) << std::setfill('0') << hash;
  return out.str();
}

double elapsed(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::milli>(end - start).count();
}

struct Sample {
  double initialization = 0., apply = 0., cold_query = 0., cached_query = 0.;
  double snapshot = 0., restore = 0., final_z = 0., refresh = 0.;
};

struct Result {
  std::size_t qubits = 0, gate_count = 0, observable_count = 0;
  std::size_t state_bytes = 0, observer_bytes = 0, history_bytes = 0;
  std::size_t observable_text_bytes = 0, query_result_bytes = 0;
  std::string family, hash;
  double prepare_observables = 0.;
  std::vector<Sample> samples;
};

Sample run(const Options& options, const std::vector<Gate>& gates,
           const std::vector<std::string>& words, Result& result) {
  Sample sample;
  auto start = Clock::now();
  Session core(Session::Backend::Compact, result.qubits);
  sample.initialization = elapsed(start, Clock::now());
  start = Clock::now(); core.apply(gates); sample.apply = elapsed(start, Clock::now());
  require(core.observer_bytes() == 0, "Observer exists before first query");

  start = Clock::now();
  auto values = core.query(words);
  sample.cold_query = elapsed(start, Clock::now());
  require(values.size() == words.size() && core.rebuilds() == 1, "Incomplete cold query");
  for (std::size_t q = 0; q < values.size(); ++q)
    require(values[q] == (q < result.qubits ? 1. : 0.), "Unexpected graph/GHZ starting observable");
  result.state_bytes = core.state_bytes();
  result.observer_bytes = core.observer_bytes();
  result.history_bytes = core.history_bytes();
  result.query_result_bytes = values.size() * sizeof(double);

  start = Clock::now();
  for (std::size_t i = 0; i < options.cached_batches; ++i) {
    const auto cached = core.query(words);
    result_sink += cached.front();
  }
  sample.cached_query = elapsed(start, Clock::now()) / static_cast<double>(options.cached_batches);
  require(core.rebuilds() == 1, "Cached query rebuilt its observer");

  start = Clock::now();
  const auto saved = core.snapshot();
  sample.snapshot = elapsed(start, Clock::now());
  require(saved.gates == gates && saved.qubits == result.qubits, "Snapshot differs from benchmark circuit");
  Session restored(Session::Backend::Compact, result.qubits);
  start = Clock::now(); restored.restore(saved); sample.restore = elapsed(start, Clock::now());
  require(restored.query(words) == values && restored.snapshot().gates == gates,
          "Restore changed query results or gate history");

  const std::vector<Gate> change{{MX_GATE_Z, result.qubits - 1, 0}};
  start = Clock::now(); core.apply(change); sample.final_z = elapsed(start, Clock::now());
  start = Clock::now();
  const auto changed = core.query(words);
  sample.refresh = elapsed(start, Clock::now());
  require(core.rebuilds() == 2, "Changed state did not invalidate observer");
  std::size_t changed_values = 0;
  for (std::size_t i = 0; i < changed.size(); ++i) {
    const auto offset = words[i].size() == result.qubits + 1 ? 1U : 0U;
    const char last = words[i][offset + result.qubits - 1];
    const double expected = (last == 'X' || last == 'Y') ? -values[i] : values[i];
    require(changed[i] == expected, "Last-qubit Z refresh lost a signed observable");
    changed_values += changed[i] != values[i];
  }
  require(changed_values > 0, "Highest-index gate did not change any graphic observable");
  return sample;
}

Result benchmark(const Options& options, std::size_t n, const std::string& family) {
  Result result; result.qubits = n; result.family = family;
  const auto gates = circuit(n, family);
  result.gate_count = gates.size(); result.hash = circuit_hash(n, gates);
  Session preparation(Session::Backend::Compact, n); preparation.apply(gates);
  const auto start = Clock::now();
  const auto words = graphic_observables(preparation);
  result.prepare_observables = elapsed(start, Clock::now());
  result.observable_count = words.size();
  for (const auto& word : words) result.observable_text_bytes += word.size();
  for (std::size_t i = 0; i < options.warmup; ++i) (void)run(options, gates, words, result);
  for (std::size_t i = 0; i < options.repetitions; ++i) result.samples.push_back(run(options, gates, words, result));
  return result;
}

std::string statistics(std::vector<double> values) {
  require(!values.empty(), "Empty benchmark statistics");
  std::sort(values.begin(), values.end());
  const auto size = values.size();
  const double median = size & 1U ? values[size / 2] : (values[size / 2 - 1] + values[size / 2]) * .5;
  const auto percentile = static_cast<std::size_t>(std::ceil(.95 * static_cast<double>(size))) - 1;
  std::ostringstream out; out << std::setprecision(10)
      << "{\"min\":" << values.front() << ",\"median\":" << median
      << ",\"p95_nearest_rank\":" << values[percentile] << ",\"max\":" << values.back() << '}';
  return out.str();
}

std::string cpu_description() {
  std::ifstream input("/proc/cpuinfo");
  std::string line;
  while (std::getline(input, line)) {
    if (line.compare(0, 10, "model name") == 0) {
      const auto colon = line.find(':');
      if (colon != std::string::npos) return line.substr(colon + 2);
    }
  }
  return "unavailable";
}

std::string report(const Options& options, const std::vector<Result>& results) {
  std::ostringstream out; out << std::setprecision(10);
  const auto now = std::time(nullptr);
  std::ostringstream date; date << std::put_time(std::gmtime(&now), "%Y-%m-%dT%H:%M:%SZ");
#if defined(__clang__)
  const std::string compiler = std::string("clang ") + __clang_version__;
#elif defined(__GNUC__)
  const std::string compiler = std::string("gcc ") + __VERSION__;
#elif defined(_MSC_VER)
  const std::string compiler = "msvc " + std::to_string(_MSC_VER);
#else
  const std::string compiler = "unknown";
#endif
  out << "{\n  \"schema\":\"cosmic-core-scale-benchmark-v1\",\n  \"timestamp_utc\":" << quote(date.str())
      << ",\n  \"execution_scope\":\"Native host CPU; no Android-device, GPU or frame-rate measurement\""
      << ",\n  \"representation\":\"repository CompactCore packed Clifford tableau\""
      << ",\n  \"dense_full_register_allocated\":false,\n  \"t_gate_support_measured\":false"
      << ",\n  \"compiler\":" << quote(compiler) << ",\n  \"cpu\":" << quote(cpu_description())
      << ",\n  \"hardware_threads_reported\":" << std::thread::hardware_concurrency()
      << ",\n  \"size_t_bytes\":" << sizeof(std::size_t)
      << ",\n  \"protocol\":{\"warmup_per_case\":" << options.warmup
      << ",\"measured_repetitions\":" << options.repetitions
      << ",\"cached_query_batches_per_sample\":" << options.cached_batches
      << ",\"rng\":\"none: fixed GHZ and graph_program gate sequences\"}"
      << ",\n  \"memory_scope\":\"Logical used payload only; state, observer, gate history, query strings and result values are separate. Excludes containers, spare capacity, allocator, temporary work and process/GPU RAM.\""
      << ",\n  \"timing_scope\":\"EngineSession operations include locks, input parsing and allocations inside the timed call. Cached queries reuse the observer index but evaluate every requested Pauli again. No performance thresholds.\""
      << ",\n  \"cases\":[\n";
  for (std::size_t index = 0; index < results.size(); ++index) {
    const auto& result = results[index];
    if (index) out << ",\n";
    out << "    {\"qubits\":" << result.qubits << ",\"family\":" << quote(result.family)
        << ",\"gate_count\":" << result.gate_count << ",\"circuit_fnv1a64\":" << quote(result.hash)
        << ",\"all_qubits_connected\":true,\"observables_per_batch\":" << result.observable_count
        << ",\"observable_definition\":\"n signed stabilizer generators, n local X, n local Z\""
        << ",\"initial_state_payload_bytes\":" << result.state_bytes
        << ",\"observer_index_payload_bytes\":" << result.observer_bytes
        << ",\"initial_gate_history_payload_bytes\":" << result.history_bytes
        << ",\"observable_utf8_payload_bytes\":" << result.observable_text_bytes
        << ",\"one_query_result_double_payload_bytes\":" << result.query_result_bytes
        << ",\"prepare_observable_strings_ms\":" << result.prepare_observables
        << ",\"verification\":\"PASS: initial expectations, exact history replay, full query equality, and signed response to Z on highest index\""
        << ",\"milliseconds\":{";
    using Metric = std::pair<const char*, double Sample::*>;
    const std::vector<Metric> metrics{{"initialization", &Sample::initialization},
        {"apply_initial_circuit", &Sample::apply}, {"first_observer_and_query", &Sample::cold_query},
        {"cached_full_query_batch", &Sample::cached_query}, {"snapshot_history_copy", &Sample::snapshot},
        {"restore_replay", &Sample::restore}, {"Z_on_highest_index", &Sample::final_z},
        {"observer_refresh_and_full_query", &Sample::refresh}};
    for (std::size_t metric = 0; metric < metrics.size(); ++metric) {
      if (metric) out << ',';
      std::vector<double> values;
      for (const auto& sample : result.samples) values.push_back(sample.*(metrics[metric].second));
      out << quote(metrics[metric].first) << ':' << statistics(values);
    }
    out << "}}";
  }
  out << "\n  ]\n}\n";
  return out.str();
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    std::vector<Result> results;
    for (const auto n : {std::size_t{20}, std::size_t{60}, std::size_t{1000}}) {
      for (const std::string family : {"ghz", "graphics_graph"}) {
        results.push_back(benchmark(options, n, family));
        std::cerr << "completed qubits=" << n << " family=" << family
                  << " observables=" << results.back().observable_count << '\n';
      }
    }
    const auto json = report(options, results);
    if (options.json_path.empty()) std::cout << json;
    else {
      std::ofstream file(options.json_path, std::ios::binary);
      if (!file) throw std::runtime_error("Cannot open JSON output file");
      file << json; file.close();
      if (!file) throw std::runtime_error("Cannot write JSON output file");
      std::cout << "Saved " << options.json_path << " cases=" << results.size() << '\n';
    }
  } catch (const std::exception& error) {
    std::cerr << "FAIL core_1000_benchmark " << error.what() << '\n'; return 1;
  }
}
