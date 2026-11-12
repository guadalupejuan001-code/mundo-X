#pragma once

#include "mundo_x/infinite_session.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <fstream>
#include <functional>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace mundo_x {
namespace autonomous_c02_detail {

inline std::uint64_t available_host_bytes() {
#if defined(_WIN32)
  MEMORYSTATUSEX memory{};
  memory.dwLength = sizeof(memory);
  if (GlobalMemoryStatusEx(&memory)) return static_cast<std::uint64_t>(memory.ullAvailPhys);
#elif defined(__linux__)
  std::ifstream input("/proc/meminfo");
  std::string key, units;
  std::uint64_t kib = 0;
  while (input >> key >> kib >> units)
    if (key == "MemAvailable:" && kib <= std::numeric_limits<std::uint64_t>::max() / 1024)
      return kib * 1024;
#endif
  return 0;  // Unsupported probe: enforce configured hard caps only.
}

inline std::size_t memory_budget(std::size_t ceiling, std::uint64_t available,
                                 std::uint64_t host_reserve, unsigned share_percent) {
  if (!available) return ceiling;
  const auto headroom = available > host_reserve ? available - host_reserve : 0;
  const auto fraction = (headroom / 100) * share_percent +
                        ((headroom % 100) * share_percent) / 100;
  return static_cast<std::size_t>(std::min<std::uint64_t>(ceiling, fraction));
}

inline bool can_admit(std::size_t queued, std::size_t queued_limit,
                      std::size_t reserved, std::size_t bytes,
                      std::size_t effective_cap) {
  return queued < queued_limit && reserved <= effective_cap &&
         bytes <= effective_cap - reserved;
}

inline std::size_t pick_priority(const std::array<bool, 3>& pending,
                                 const std::array<std::uint64_t, 3>& age_us,
                                 std::size_t& cursor,
                                 std::uint64_t normal_age_us,
                                 std::uint64_t interactive_age_us) {
  if ((pending[1] && age_us[1] >= interactive_age_us) ||
      (pending[2] && age_us[2] >= interactive_age_us)) {
    if (pending[2] && age_us[2] >= interactive_age_us &&
        (!pending[1] || age_us[2] >= age_us[1] || age_us[1] < interactive_age_us))
      return 2;
    return 1;
  }
  static constexpr std::array<std::size_t, 7> slots{{0, 0, 0, 0, 1, 1, 2}};
  for (std::size_t checked = 0; checked < slots.size(); ++checked) {
    const auto requested = slots[cursor];
    cursor = (cursor + 1) % slots.size();
    if (requested == 1 && pending[2] && age_us[2] >= normal_age_us)
      return 2;  // Virtual promotion without moving the job or changing FIFO.
    if (pending[requested]) return requested;
  }
  throw std::logic_error("scheduler has no pending work");
}

}  // namespace autonomous_c02_detail

// C02 is an opt-in authority, not an alternative quantum interpreter or state.
// No mutable InfiniteSession is returned to callers. Every worker executes XI
// on a private restored copy; transactional writes publish only after an epoch
// compare-and-commit under the single authority lock.
class AutonomousCoreC02 {
 public:
  enum class Priority : std::size_t { Interactive = 0, Normal = 1, Background = 2 };
  enum class Mode { Transactional, Isolated };
  struct Options {
    std::size_t workers = 2;
    std::size_t max_queued = 1024;
    std::size_t max_program_bytes = 16u * 1024u * 1024u;
    std::size_t max_queue_bytes = 128u * 1024u * 1024u;
    std::uint64_t host_reserve_bytes = 384ull * 1024ull * 1024ull;
    unsigned admission_share_percent = 20;
    std::uint64_t aging_normal_us = 2000000;
    std::uint64_t aging_interactive_us = 5000000;
    std::size_t conflict_retries = 4;
  };
  struct Outcome {
    std::uint64_t id = 0, source_epoch = 0, epoch = 0, revision = 0;
    std::uint64_t wait_us = 0, execution_us = 0;
    std::size_t conflicts = 0, worker = 0;
    bool ok = false, state_changed = false, committed = false;
    bool isolated = false, conflict_exhausted = false;
    std::string output, error, private_snapshot;
  };
  struct Telemetry {
    std::uint64_t accepted = 0, completed = 0, failed = 0, rejected = 0;
    std::uint64_t committed = 0, isolated = 0, conflicts = 0, conflict_exhausted = 0;
    std::uint64_t last_available_bytes = 0;
    std::size_t last_effective_cap = 0;
    std::size_t queued = 0, active = 0, reserved_program_bytes = 0;
    std::size_t peak_queued = 0, peak_active = 0, peak_reserved_program_bytes = 0;
    bool stopped = false;
  };
  using MemoryProbe = std::function<std::uint64_t()>;
  // Diagnostic hook for deterministic conflict tests. Runs outside every
  // authority lock. Never receives the state and cannot change its revision.
  using BeforeCommit = std::function<void(std::uint64_t, std::size_t)>;

  AutonomousCoreC02() : AutonomousCoreC02(Options{}) {}
  explicit AutonomousCoreC02(Options options, MemoryProbe probe = {},
                             BeforeCommit before_commit = {},
                             InfiniteSession::Background background =
                                 InfiniteSession::Background::Zero,
                             InfiniteSession::Limits limits = InfiniteSession::Limits{})
      : options_(options),
        memory_probe_(probe ? std::move(probe) : MemoryProbe(autonomous_c02_detail::available_host_bytes)),
        before_commit_(std::move(before_commit)),
        authority_(background, limits) {
    if (options_.workers == 0 || options_.max_queued == 0 ||
        options_.max_program_bytes == 0 || options_.max_queue_bytes == 0 ||
        options_.max_program_bytes > options_.max_queue_bytes ||
        options_.max_program_bytes > infinite_program_detail::max_program_bytes ||
        options_.admission_share_percent == 0 || options_.admission_share_percent > 100 ||
        options_.aging_normal_us > options_.aging_interactive_us)
      throw std::invalid_argument("invalid C02 worker, aging or admission configuration");
    telemetry_.last_effective_cap = options_.max_queue_bytes;
    threads_.reserve(options_.workers);
    try {
      for (std::size_t i = 0; i < options_.workers; ++i)
        threads_.emplace_back([this, i] { work(i); });
    } catch (...) {
      shutdown();
      throw;
    }
  }
  AutonomousCoreC02(const AutonomousCoreC02&) = delete;
  AutonomousCoreC02& operator=(const AutonomousCoreC02&) = delete;
  ~AutonomousCoreC02() { shutdown(); }

  std::future<Outcome> submit(std::string program,
                              Priority priority = Priority::Normal,
                              Mode mode = Mode::Transactional,
                              bool capture_private_snapshot = false) {
    const auto level = static_cast<std::size_t>(priority);
    if (level >= queues_.size() ||
        (mode != Mode::Isolated && mode != Mode::Transactional) ||
        (capture_private_snapshot && mode != Mode::Isolated))
      throw std::invalid_argument("invalid priority, mode or private snapshot request");
    if (program.size() > options_.max_program_bytes) {
      rejected();
      throw std::length_error("C02 program exceeds configured per-job limit");
    }

    const auto available = memory_probe_();
    const auto budget = autonomous_c02_detail::memory_budget(
        options_.max_queue_bytes, available, options_.host_reserve_bytes,
        options_.admission_share_percent);
    auto job = std::make_shared<Job>();
    job->program = std::move(program);
    job->mode = mode;
    job->capture_private_snapshot = capture_private_snapshot;
    job->enqueued = Clock::now();
    auto future = job->promise.get_future();
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      if (stopping_) throw std::runtime_error("C02 scheduler is shutting down");
      telemetry_.last_available_bytes = available;
      telemetry_.last_effective_cap = budget;
      if (!autonomous_c02_detail::can_admit(queued_, options_.max_queued,
                                           reserved_program_bytes_,
                                           job->program.size(), budget)) {
        ++telemetry_.rejected;
        throw std::length_error("C02 queue or dynamic memory admission exhausted");
      }
      if (next_id_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("C02 request ID exhausted");
      job->id = ++next_id_;
      queues_[level].push_back(job);
      ++queued_;
      reserved_program_bytes_ += job->program.size();
      ++telemetry_.accepted;
      telemetry_.peak_queued = std::max(telemetry_.peak_queued, queued_);
      telemetry_.peak_reserved_program_bytes =
          std::max(telemetry_.peak_reserved_program_bytes, reserved_program_bytes_);
    }
    ready_.notify_one();
    return future;
  }

  // The snapshot belongs to the authority, not an individual worker.
  std::string snapshot() const {
    std::lock_guard<std::mutex> lock(authority_mutex_);
    return authority_.snapshot();
  }
  InfiniteSession::Metadata metadata() const {
    std::lock_guard<std::mutex> lock(authority_mutex_);
    return authority_.metadata();
  }
  Telemetry telemetry() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    auto value = telemetry_;
    value.queued = queued_;
    value.active = active_;
    value.reserved_program_bytes = reserved_program_bytes_;
    value.stopped = stopping_;
    return value;
  }
  const Options& options() const noexcept { return options_; }

  // Drain accepted work; no forceful XI cancellation or detached worker threads.
  void shutdown() noexcept {
    std::lock_guard<std::mutex> join_lock(join_mutex_);
    {
      std::lock_guard<std::mutex> lock(queue_mutex_);
      stopping_ = true;
    }
    ready_.notify_all();
    for (auto& thread : threads_) if (thread.joinable()) thread.join();
  }

 private:
  using Clock = std::chrono::steady_clock;
  struct Job {
    std::uint64_t id = 0;
    Clock::time_point enqueued;
    Mode mode = Mode::Transactional;
    bool capture_private_snapshot = false;
    std::string program;
    std::promise<Outcome> promise;
  };
  struct BaseSnapshot {
    std::string bytes;
    std::uint64_t epoch = 0;
  };

  static std::uint64_t micros(Clock::duration duration) {
    const auto value = std::chrono::duration_cast<std::chrono::microseconds>(duration).count();
    return value <= 0 ? 0 : static_cast<std::uint64_t>(value);
  }
  void rejected() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    ++telemetry_.rejected;
  }
  BaseSnapshot capture_base() const {
    std::lock_guard<std::mutex> lock(authority_mutex_);
    const auto meta = authority_.metadata();
    return {authority_.snapshot(), meta.epoch};
  }
  void record_conflict() {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    ++telemetry_.conflicts;
  }

  std::shared_ptr<Job> take_next() {
    std::array<bool, 3> pending{{false, false, false}};
    std::array<std::uint64_t, 3> ages{{0, 0, 0}};
    const auto now = Clock::now();
    for (std::size_t i = 0; i < queues_.size(); ++i) {
      pending[i] = !queues_[i].empty();
      if (pending[i]) ages[i] = micros(now - queues_[i].front()->enqueued);
    }
    const auto index = autonomous_c02_detail::pick_priority(
        pending, ages, slot_, options_.aging_normal_us, options_.aging_interactive_us);
    auto job = queues_[index].front();
    queues_[index].pop_front();
    --queued_;
    ++active_;
    telemetry_.peak_active = std::max(telemetry_.peak_active, active_);
    // Input reservation is held until completion, including running jobs.
    return job;
  }

  void execute_job(const Job& job, Outcome& result) {
    result.isolated = job.mode == Mode::Isolated;
    for (std::size_t attempt = 0;; ++attempt) {
      const auto base = capture_base();
      result.source_epoch = base.epoch;
      InfiniteSession local(InfiniteSession::Background::Zero, authority_.limits());
      local.restore(base.bytes);
      const auto before_epoch = local.epoch();
      const auto output = local.execute(job.program);
      const bool changed = local.epoch() != before_epoch;

      if (job.mode == Mode::Isolated) {
        result.output = output;
        result.ok = true;
        result.state_changed = changed;
        result.revision = local.stats().revision;
        result.epoch = base.epoch;
        if (job.capture_private_snapshot) result.private_snapshot = local.snapshot();
        return;
      }

      // Prepare everything that could fail before taking the commit lock.
      const std::string candidate = changed ? local.snapshot() : std::string();
      if (before_commit_) before_commit_(job.id, attempt);
      {
        std::lock_guard<std::mutex> lock(authority_mutex_);
        const auto current = authority_.metadata();
        if (current.epoch == base.epoch) {
          if (changed) authority_.restore(candidate);
          const auto committed = authority_.metadata();
          result.output = output;
          result.ok = true;
          result.state_changed = changed;
          result.committed = changed;
          result.epoch = committed.epoch;
          result.revision = committed.stats.revision;
          return;
        }
      }
      ++result.conflicts;
      record_conflict();
      if (attempt >= options_.conflict_retries) {
        result.conflict_exhausted = true;
        result.error = "C02 optimistic commit conflict: retry budget exhausted";
        const auto current = metadata();
        result.epoch = current.epoch;
        result.revision = current.stats.revision;
        return;  // Never overwrite a newer authoritative state.
      }
    }
  }

  void work(std::size_t index) {
    for (;;) {
      std::shared_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        ready_.wait(lock, [this] { return stopping_ || queued_ != 0; });
        if (stopping_ && queued_ == 0) return;
        job = take_next();
      }
      Outcome result;
      result.id = job->id;
      result.worker = index;
      const auto started = Clock::now();
      result.wait_us = micros(started - job->enqueued);
      try {
        execute_job(*job, result);
      } catch (const std::exception& error) {
        result.error = error.what();
      } catch (...) {
        result.error = "unknown XI C02 worker error";
      }
      result.execution_us = micros(Clock::now() - started);
      {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        --active_;
        reserved_program_bytes_ -= job->program.size();
        ++telemetry_.completed;
        if (!result.ok) ++telemetry_.failed;
        if (result.committed) ++telemetry_.committed;
        if (result.isolated && result.ok) ++telemetry_.isolated;
        if (result.conflict_exhausted) ++telemetry_.conflict_exhausted;
      }
      job->promise.set_value(std::move(result));
    }
  }

  const Options options_;
  MemoryProbe memory_probe_;
  BeforeCommit before_commit_;
  // Private authority: a client cannot bypass epoch checks through this API.
  mutable std::mutex authority_mutex_;
  InfiniteSession authority_;
  mutable std::mutex queue_mutex_, join_mutex_;
  std::condition_variable ready_;
  std::array<std::deque<std::shared_ptr<Job>>, 3> queues_;
  std::vector<std::thread> threads_;
  std::size_t queued_ = 0, active_ = 0, reserved_program_bytes_ = 0, slot_ = 0;
  std::uint64_t next_id_ = 0;
  bool stopping_ = false;
  Telemetry telemetry_;
};

}  // namespace mundo_x
