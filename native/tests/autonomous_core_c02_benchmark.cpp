#include "mundo_x/autonomous_core_c02.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using C02 = mundo_x::AutonomousCoreC02;
using Clock = std::chrono::steady_clock;
struct Trial {
  unsigned workers = 0, jobs = 0;
  double elapsed_ms = 0, throughput_jobs_s = 0;
  std::uint64_t wait_p50_us = 0, wait_p95_us = 0;
  std::uint64_t execution_p50_us = 0, execution_p95_us = 0;
  std::size_t peak_active = 0, peak_input_bytes = 0;
  std::uint64_t available_bytes = 0;
  std::size_t effective_input_cap = 0;
  std::size_t conflicts = 0, rejected = 0;
};
std::uint64_t quantile(std::vector<std::uint64_t> values, unsigned numerator) {
  if (values.empty()) return 0;
  std::sort(values.begin(), values.end());
  const auto rank = (numerator * values.size() + 99) / 100;
  return values[std::min(values.size() - 1, rank > 0 ? rank - 1 : 0)];
}
Trial run(unsigned workers, unsigned jobs, const std::string& program) {
  C02::Options options;
  options.workers = workers;
  C02 manager(options);  // Uses the real host available-memory probe.
  const auto original = manager.snapshot();
  std::vector<std::future<C02::Outcome>> futures;
  futures.reserve(jobs);
  const auto start = Clock::now();
  for (unsigned n = 0; n < jobs; ++n)
    futures.push_back(manager.submit(program, C02::Priority::Normal,
                                     C02::Mode::Isolated));
  std::string reference_response;
  std::vector<std::uint64_t> wait, exec;
  wait.reserve(jobs);
  exec.reserve(jobs);
  for (auto& future : futures) {
    const auto result = future.get();
    if (!result.ok || !result.isolated || result.committed)
      throw std::runtime_error("isolated workload failed or touched the central state");
    if (reference_response.empty()) reference_response = result.output;
    if (result.output != reference_response)
      throw std::runtime_error("parallel XI worker responses differ");
    wait.push_back(result.wait_us);
    exec.push_back(result.execution_us);
  }
  const auto end = Clock::now();
  if (original != manager.snapshot())
    throw std::runtime_error("isolated XI benchmark corrupted central state");
  const auto metrics = manager.telemetry();
  if (metrics.completed != jobs || metrics.reserved_program_bytes != 0)
    throw std::runtime_error("scheduler leaked accepted jobs");
  const double elapsed = std::chrono::duration<double, std::milli>(end - start).count();
  Trial result;
  result.workers = workers;
  result.jobs = jobs;
  result.elapsed_ms = elapsed;
  result.throughput_jobs_s = 1000.0 * jobs / std::max(0.001, elapsed);
  result.wait_p50_us = quantile(wait, 50);
  result.wait_p95_us = quantile(wait, 95);
  result.execution_p50_us = quantile(exec, 50);
  result.execution_p95_us = quantile(exec, 95);
  result.peak_active = metrics.peak_active;
  result.peak_input_bytes = metrics.peak_reserved_program_bytes;
  result.available_bytes = metrics.last_available_bytes;
  result.effective_input_cap = metrics.last_effective_cap;
  result.conflicts = metrics.conflicts;
  result.rejected = metrics.rejected;
  return result;
}
void print(const Trial& t) {
  std::cout << "{\"workers\":" << t.workers
            << ",\"jobs\":" << t.jobs
            << ",\"elapsed_ms\":" << std::fixed << std::setprecision(3) << t.elapsed_ms
            << ",\"jobs_per_second\":" << t.throughput_jobs_s
            << ",\"wait_p50_us\":" << t.wait_p50_us
            << ",\"wait_p95_us\":" << t.wait_p95_us
            << ",\"execution_p50_us\":" << t.execution_p50_us
            << ",\"execution_p95_us\":" << t.execution_p95_us
            << ",\"peak_running_workers\":" << t.peak_active
            << ",\"peak_reserved_program_bytes\":" << t.peak_input_bytes
            << ",\"host_available_bytes_at_last_admission\":" << t.available_bytes
            << ",\"last_dynamic_input_cap_bytes\":" << t.effective_input_cap
            << ",\"conflicts\":" << t.conflicts
            << ",\"admission_rejections\":" << t.rejected << '}';
}
}  // namespace

int main() {
  try {
    constexpr unsigned jobs = 32;
    constexpr unsigned repetitions = 2;
    std::string xi;
    for (unsigned i = 0; i < 256; ++i) xi += "h 0\n";
    xi += "expect Z:0\n";
    std::cout << "{\"schema\":\"InfiniteCore-C02-benchmark-v1\","
                 "\"workload\":\"256 Clifford H gates plus one Pauli read per independent task\","
                 "\"semantic_mode\":\"isolated copies; no authority mutation\","
                 "\"run_note\":\"host timings, not quantum hardware advantage\","
                 "\"hardware_concurrency_hint\":" << std::thread::hardware_concurrency()
              << ",\"trials\":[";
    bool first = true;
    for (unsigned workers : {1u, 2u, 4u}) {
      for (unsigned trial = 0; trial < repetitions; ++trial) {
        const auto measurement = run(workers, jobs, xi);
        if (!first) std::cout << ',';
        print(measurement);
        first = false;
      }
    }
    std::cout << "]}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "InfiniteCore C02 benchmark FAILED: " << error.what() << '\n';
    return 1;
  }
}
