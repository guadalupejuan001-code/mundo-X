#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <limits>
#include <locale>
#include <stdexcept>
#include <string>
#include <thread>

#include "mundo_x/lab_memory.hpp"

namespace {
std::size_t checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
bool has_status(const std::string& json, const char* status) {
  return json.find(std::string("\"status\":\"") + status + '"') != std::string::npos;
}
std::size_t number(const std::string& json, const char* field) {
  const std::string marker = std::string("\"") + field + "\":";
  const auto start = json.find(marker);
  require(start != std::string::npos, "JSON field absent");
  return static_cast<std::size_t>(std::stoull(json.substr(start + marker.size())));
}
void small_cases() {
  mundo_x::LabMemoryJob job;
  require(has_status(job.progress_json(), "idle") && !job.running(), "initial status is not idle");
  job.cancel();  // An idle request must not attach to the next job.
  for (const auto n : {std::size_t{32}, std::size_t{65}}) {
    for (const int family : {0, 1}) {
      const auto result = job.run(8, n, family);
      require(has_status(result, "pass") && !job.running(), "small memory job failed");
      const auto payload = 2 * n * (n / 64 + (n % 64 != 0)) * sizeof(std::uint64_t) + n;
      require(number(result, "state_bytes") == payload, "logical payload is not exact");
      require(number(result, "budget_mib") == 8, "budget was not preserved");
      require(number(result, "completed_gates") == (family == 0 ? n : 2 * n - 1), "gate count mismatch");
      require(number(result, "completed_gates") == number(result, "total_gates"), "job incomplete");
      require(number(result, "verified_rows") == 17, "sampled validation count mismatch");
      require(result == job.progress_json(), "final summary was not retained");
    }
  }
  const auto too_large = job.run(8, 6000, 0);
  require(has_status(too_large, "error") && number(too_large, "state_bytes") > 8 * 1024 * 1024, "over-budget tableau was accepted");
  require(number(too_large, "completed_gates") == 0 && number(too_large, "verified_rows") == 0, "preflight performed work");
  require(has_status(job.run(2048, 100000, 0), "error"), "largest over-budget tableau accepted");
  require(has_status(job.run(8, std::numeric_limits<std::size_t>::max(), 0), "error"), "overflow-size qubits accepted");
  require(has_status(job.run(std::numeric_limits<std::size_t>::max(), 32, 0), "error"), "overflow-size budget accepted");
  require(has_status(job.run(7, 32, 0), "error") && has_status(job.run(2049, 32, 0), "error"), "budget bounds ignored");
  require(has_status(job.run(8, 1, 0), "error") && has_status(job.run(8, 32, 2), "error"), "qubit/family bounds ignored");
  job.cancel();
  require(has_status(job.run(8, 2, 1), "pass"), "failed/idle cancellation leaked to next job");
}
void concurrent_case() {
  mundo_x::LabMemoryJob job;
  std::string result;
  std::thread worker([&] { result = job.run(8, 4096, 1); });
  try {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    std::string progress;
    do {
      progress = job.progress_json();
      if (has_status(progress, "evolving")) break;
      std::this_thread::yield();
    } while (std::chrono::steady_clock::now() < deadline);
    require(has_status(progress, "evolving") && job.running(), "worker did not enter cancellable evolution");
    const auto duplicate = job.run(8, 32, 0);
    require(has_status(duplicate, "error"), "a second simultaneous job was accepted");
    require(number(job.progress_json(), "qubits") == 4096, "duplicate job overwrote active metadata");
    job.cancel();
    worker.join();
    require(has_status(result, "cancelled") && !job.running(), "cooperative cancellation failed");
    require(number(result, "completed_gates") < number(result, "total_gates"), "cancellation did not stop evolution");
    require(result == job.progress_json(), "cancelled summary not retained");
    job.cancel();
    require(has_status(job.run(8, 32, 0), "pass"), "cancellation attached to subsequent job");
  } catch (...) {
    job.cancel();
    if (worker.joinable()) worker.join();
    throw;
  }
}
void locale_case() {
  struct GroupedNumbers : std::numpunct<char> {
    char do_thousands_sep() const override { return '_'; }
    std::string do_grouping() const override { return "\3"; }
  };
  const auto previous = std::locale();
  std::locale::global(std::locale(previous, new GroupedNumbers));
  try {
    mundo_x::LabMemoryJob job;
    const auto result = job.run(8, 65, 0);
    require(number(result, "state_bytes") == 2145, "global locale corrupted JSON numbers");
    std::locale::global(previous);
  } catch (...) {
    std::locale::global(previous);
    throw;
  }
}
}  // namespace
int main() {
  try {
    small_cases(); locale_case(); concurrent_case();
    std::cout << "PASS lab_memory checks=" << checks << '\n';
  } catch (const std::exception& error) {
    std::cerr << "FAIL lab_memory " << error.what() << '\n';
    return 1;
  }
}
