#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <limits>
#include <locale>
#include <mutex>
#include <new>
#include <sstream>
#include <string>

#include "mundo_x/compact_core.hpp"

namespace mundo_x {

// Blocking, single-job wrapper around the original CompactCore. The caller
// owns its worker thread. No tableau survives run(), and the budget measures
// logical tableau payload, not RSS, allocator overhead or transient strings.
class LabMemoryJob {
 public:
  std::string run(std::size_t budget_mib, std::size_t qubits, int family) {
    std::uint64_t token = 0;
    {
      std::lock_guard<std::mutex> lock(mutex_);
      auto idle = lifecycle_.load(std::memory_order_acquire);
      if (idle & 1U) return json_locked("error", "a job is already running");
      if (idle > std::numeric_limits<std::uint64_t>::max() - 2)
        return json_locked("error", "job sequence exhausted");
      token = idle + 1;
      if (!lifecycle_.compare_exchange_strong(idle, token, std::memory_order_acq_rel))
        return json_locked("error", "a job is already running");
      budget_mib_ = budget_mib;
      qubits_ = qubits;
      family_ = family;
      state_bytes_.store(0);
      completed_.store(0);
      total_.store(0);
      verified_.store(0);
      elapsed_.store(0);
      start_ = Clock::now();
      error_ = nullptr;
      status_.store(Status::Allocating);
    }
    Result result{Status::Error, "unexpected engine failure"};
    try {
      result = execute(budget_mib, qubits, family, token);
    } catch (const std::bad_alloc&) {
      result = {Status::Error, "CompactCore allocation failed"};
    } catch (const std::exception&) {
      result = {Status::Error, "CompactCore execution failed"};
    } catch (...) {
      result = {Status::Error, "unexpected engine failure"};
    }
    // execute() has returned/unwound: its CompactCore has already been freed.
    std::lock_guard<std::mutex> lock(mutex_);
    update_elapsed();
    error_ = result.error;
    status_.store(result.status);
    lifecycle_.store(token + 1, std::memory_order_release);
    return json_locked();
  }

  std::string progress_json() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return json_locked();
  }

  bool running() const { return (lifecycle_.load(std::memory_order_acquire) & 1U) != 0; }

  void cancel() {
    const auto token = lifecycle_.load(std::memory_order_acquire);
    if (!(token & 1U)) return;
    // Monotonic tokens prevent a delayed old cancellation from erasing a newer
    // request, and an idle cancellation never attaches to the next job.
    auto requested = cancelled_token_.load(std::memory_order_relaxed);
    while (requested < token &&
           !cancelled_token_.compare_exchange_weak(requested, token,
                                                   std::memory_order_release,
                                                   std::memory_order_relaxed)) {}
  }

 private:
  using Clock = std::chrono::steady_clock;
  enum class Status { Idle, Allocating, Evolving, Validating, Pass, Cancelled, Error };
  struct Result { Status status; const char* error; };

  static const char* name(Status status) {
    switch (status) {
      case Status::Idle: return "idle";
      case Status::Allocating: return "allocating";
      case Status::Evolving: return "evolving";
      case Status::Validating: return "validating";
      case Status::Pass: return "pass";
      case Status::Cancelled: return "cancelled";
      case Status::Error: return "error";
    }
    return "error";
  }

  static bool multiply(std::size_t a, std::size_t b, std::size_t& out) {
    if (b && a > std::numeric_limits<std::size_t>::max() / b) return false;
    out = a * b;
    return true;
  }

  bool cancelled(std::uint64_t token) const {
    return cancelled_token_.load(std::memory_order_acquire) == token;
  }

  void update_elapsed() {
    elapsed_.store(static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count()),
        std::memory_order_relaxed);
  }

  Result execute(std::size_t budget_mib, std::size_t n, int family,
                 std::uint64_t token) {
    if (budget_mib < 8 || budget_mib > 2048) return {Status::Error, "budget_mib must be 8..2048"};
    if (n < 2 || n > 100000) return {Status::Error, "qubits must be 2..100000"};
    if (family != 0 && family != 1) return {Status::Error, "family must be 0 (GHZ) or 1 (line cluster)"};
    std::size_t budget = 0, elements = 0, payload = 0;
    const auto words = n / 64 + (n % 64 != 0);
    if (!multiply(budget_mib, std::size_t{1024} * 1024, budget) ||
        !multiply(n, words, elements) || !multiply(elements, 2 * sizeof(std::uint64_t), payload) ||
        payload > std::numeric_limits<std::size_t>::max() - n)
      return {Status::Error, "tableau payload size overflow"};
    payload += n;
    state_bytes_.store(payload);
    total_.store(family == 0 ? n : 2 * n - 1);
    if (payload > budget) return {Status::Error, "tableau logical payload exceeds budget"};
    if (cancelled(token)) return {Status::Cancelled, nullptr};

    // Allocation/constructor is intentionally not interrupted halfway through.
    CompactCore core(n);
    if (core.state_bytes() != payload) return {Status::Error, "tableau payload estimate mismatch"};
    if (cancelled(token)) return {Status::Cancelled, nullptr};
    status_.store(Status::Evolving);
    const auto advance = [&] {
      completed_.fetch_add(1, std::memory_order_relaxed);
      update_elapsed();
    };
    if (family == 0) {
      if (cancelled(token)) return {Status::Cancelled, nullptr};
      core.hadamard(0); advance();
      for (std::size_t q = 1; q < n; ++q) {
        if (cancelled(token)) return {Status::Cancelled, nullptr};
        core.cnot(q - 1, q); advance();
      }
    } else {
      for (std::size_t q = 0; q < n; ++q) {
        if (cancelled(token)) return {Status::Cancelled, nullptr};
        core.hadamard(q); advance();
      }
      for (std::size_t q = 1; q < n; ++q) {
        if (cancelled(token)) return {Status::Cancelled, nullptr};
        core.controlled_z(q - 1, q); advance();
      }
    }
    if (cancelled(token)) return {Status::Cancelled, nullptr};
    status_.store(Status::Validating);
    const auto samples = std::min<std::size_t>(n, 17);
    for (std::size_t index = 0; index < samples; ++index) {
      if (cancelled(token)) return {Status::Cancelled, nullptr};
      const auto row = index * (n - 1) / (samples - 1);
      std::string expected(n + 1, 'I');
      expected[0] = '+';
      if (family == 0) {
        if (row == 0) std::fill(expected.begin() + 1, expected.end(), 'X');
        else { expected[row] = 'Z'; expected[row + 1] = 'Z'; }
      } else {
        expected[row + 1] = 'X';
        if (row > 0) expected[row] = 'Z';
        if (row + 1 < n) expected[row + 2] = 'Z';
      }
      if (core.generator_string(row) != expected)
        return {Status::Error, "sampled generator verification failed"};
      verified_.fetch_add(1, std::memory_order_relaxed);
      update_elapsed();
    }
    if (cancelled(token)) return {Status::Cancelled, nullptr};
    return {Status::Pass, nullptr};
  }

  std::string json_locked(const char* status_override = nullptr,
                          const char* error_override = nullptr) const {
    const bool active = running();
    const auto elapsed = active ? static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now() - start_).count())
        : elapsed_.load();
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "{\"status\":\"" << (status_override ? status_override : name(status_.load()))
        << "\",\"running\":" << (active ? "true" : "false")
        << ",\"qubits\":" << qubits_ << ",\"budget_mib\":" << budget_mib_
        << ",\"family\":" << family_ << ",\"state_bytes\":" << state_bytes_.load()
        << ",\"completed_gates\":" << completed_.load() << ",\"total_gates\":" << total_.load()
        << ",\"elapsed_ms\":" << elapsed << ",\"verified_rows\":" << verified_.load()
        << ",\"validation\":\"sampled_generators\"";
    const char* error = error_override ? error_override : error_;
    if (error) out << ",\"error\":\"" << error << '"';
    out << '}';
    return out.str();
  }

  mutable std::mutex mutex_;
  std::atomic<std::uint64_t> lifecycle_{0}, cancelled_token_{0}, elapsed_{0};
  std::atomic<std::size_t> state_bytes_{0}, completed_{0}, total_{0}, verified_{0};
  std::atomic<Status> status_{Status::Idle};
  std::size_t qubits_ = 0, budget_mib_ = 0;
  int family_ = 0;
  Clock::time_point start_ = Clock::now();
  const char* error_ = nullptr;
};

}  // namespace mundo_x
