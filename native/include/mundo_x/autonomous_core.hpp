#pragma once

#include "mundo_x/infinite_session.hpp"

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>

// C01: opt-in, host-independent execution policy around the canonical session.
// It never makes its own quantum state, changes InfiniteCore limits or interprets XI.
// One scheduler per shared InfiniteSession owner is required for global fairness.
namespace mundo_x {

class AutonomousCore {
 public:
  enum class Priority : std::size_t { Interactive = 0, Normal = 1, Background = 2 };
  struct Limits {
    std::size_t max_queued = 64;
    std::size_t max_program_bytes = 1024 * 1024;
    std::size_t max_queued_bytes = 4 * 1024 * 1024;
  };
  struct Outcome {
    std::uint64_t id = 0;
    bool ok = false;
    std::string output;
    std::string error;
    std::uint64_t revision = 0;
    std::uint64_t epoch = 0;
    std::uint64_t wait_us = 0;
    std::uint64_t execution_us = 0;
  };
  struct Telemetry {
    std::uint64_t accepted = 0, completed = 0, failed = 0, rejected = 0;
    std::uint64_t ewma_execution_us = 0;
    std::size_t queued = 0, queued_bytes = 0, peak_queued = 0;
    bool executing = false, stopped = false;
    std::uint64_t last_revision = 0, last_epoch = 0;
  };

  explicit AutonomousCore(std::shared_ptr<InfiniteSession> session)
      : AutonomousCore(std::move(session), Limits{}) {}

  AutonomousCore(std::shared_ptr<InfiniteSession> session, Limits limits)
      : session_(std::move(session)), limits_(limits) {
    if (!session_) throw std::invalid_argument("AutonomousCore requires one existing InfiniteSession");
    if (!limits_.max_queued || !limits_.max_program_bytes ||
        !limits_.max_queued_bytes || limits_.max_program_bytes > limits_.max_queued_bytes ||
        limits_.max_program_bytes > infinite_program_detail::max_program_bytes)
      throw std::invalid_argument("invalid autonomous scheduler transport limits");
    worker_ = std::thread([this] { run(); });
  }
  AutonomousCore(const AutonomousCore&) = delete;
  AutonomousCore& operator=(const AutonomousCore&) = delete;
  ~AutonomousCore() { shutdown(); }

  std::future<Outcome> submit(std::string program, Priority priority = Priority::Normal) {
    const auto level = static_cast<std::size_t>(priority);
    if (level >= queues_.size()) throw std::invalid_argument("invalid priority");
    if (program.size() > limits_.max_program_bytes)
      throw std::length_error("autonomous job exceeds per-job transport budget");

    auto job = std::make_shared<Job>();
    job->program = std::move(program);
    job->enqueued = Clock::now();
    auto future = job->promise.get_future();
    {
      std::lock_guard<std::mutex> lock(mutex_);
      if (stopping_) throw std::runtime_error("autonomous scheduler is stopping");
      if (queued_ >= limits_.max_queued ||
          job->program.size() > limits_.max_queued_bytes - queued_bytes_) {
        ++telemetry_.rejected;
        throw std::length_error("autonomous queue budget exhausted");
      }
      if (next_id_ == std::numeric_limits<std::uint64_t>::max())
        throw std::overflow_error("autonomous request id exhausted");
      job->id = ++next_id_;
      queues_[level].push_back(job);
      ++queued_;
      queued_bytes_ += job->program.size();
      ++telemetry_.accepted;
      if (queued_ > telemetry_.peak_queued) telemetry_.peak_queued = queued_;
    }
    wake_.notify_one();
    return future;
  }

  Telemetry telemetry() const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto result = telemetry_;
    result.queued = queued_;
    result.queued_bytes = queued_bytes_;
    result.executing = executing_;
    result.stopped = stopping_;
    return result;
  }

  // Drain all already accepted jobs; reject new ones. No cancellation of XI
  // transactions or interruption of a running native operation is attempted.
  void shutdown() noexcept {
    std::lock_guard<std::mutex> join_lock(join_mutex_);
    {
      std::lock_guard<std::mutex> lock(mutex_);
      stopping_ = true;
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
  }

  std::shared_ptr<InfiniteSession> session() const noexcept { return session_; }

 private:
  using Clock = std::chrono::steady_clock;
  struct Job {
    std::uint64_t id = 0;
    std::string program;
    Clock::time_point enqueued;
    std::promise<Outcome> promise;
  };
  static std::uint64_t micros(Clock::duration duration) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(duration).count());
  }
  bool has_work() const noexcept { return queued_ != 0; }

  std::shared_ptr<Job> take_next() {
    // Weighted round-robin: 4 interactive, 2 normal, 1 background slots.
    // Empty levels are skipped; under continuous contention none can starve.
    static constexpr std::array<std::size_t, 7> slots{{0, 0, 0, 0, 1, 1, 2}};
    for (std::size_t checked = 0; checked < slots.size(); ++checked) {
      const auto index = slots[slot_];
      slot_ = (slot_ + 1) % slots.size();
      if (queues_[index].empty()) continue;
      auto job = queues_[index].front();
      queues_[index].pop_front();
      --queued_;
      queued_bytes_ -= job->program.size();
      executing_ = true;
      return job;
    }
    throw std::logic_error("scheduler queue accounting mismatch");
  }

  void run() {
    for (;;) {
      std::shared_ptr<Job> job;
      {
        std::unique_lock<std::mutex> lock(mutex_);
        wake_.wait(lock, [this] { return stopping_ || has_work(); });
        if (stopping_ && !has_work()) return;
        job = take_next();
      }
      Outcome outcome;
      outcome.id = job->id;
      const auto began = Clock::now();
      outcome.wait_us = micros(began - job->enqueued);
      try {
        outcome.output = session_->execute(job->program);
        outcome.ok = true;
      } catch (const std::exception& error) {
        outcome.error = error.what();
      } catch (...) {
        outcome.error = "unknown XI execution error";
      }
      outcome.execution_us = micros(Clock::now() - began);
      const auto metadata = session_->metadata();
      outcome.revision = metadata.stats.revision;
      outcome.epoch = metadata.epoch;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        executing_ = false;
        ++telemetry_.completed;
        if (!outcome.ok) ++telemetry_.failed;
        telemetry_.last_revision = outcome.revision;
        telemetry_.last_epoch = outcome.epoch;
        // One inexpensive smoothing filter; recorded timing is not a speedup claim.
        telemetry_.ewma_execution_us = telemetry_.completed == 1 ? outcome.execution_us :
            (telemetry_.ewma_execution_us / 8) * 7 + outcome.execution_us / 8;
      }
      job->promise.set_value(std::move(outcome));
    }
  }

  std::shared_ptr<InfiniteSession> session_;
  const Limits limits_;
  mutable std::mutex mutex_;
  std::mutex join_mutex_;
  std::condition_variable wake_;
  std::array<std::deque<std::shared_ptr<Job>>, 3> queues_;
  std::uint64_t next_id_ = 0;
  std::size_t queued_ = 0, queued_bytes_ = 0, slot_ = 0;
  bool executing_ = false, stopping_ = false;
  Telemetry telemetry_;
  std::thread worker_;
};

}  // namespace mundo_x
