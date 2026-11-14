#pragma once

#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>

namespace cosmic_pc {

enum class BootTimePart : std::size_t {
  Total, Device, BatchDispatch, BatchExecute, Scalar, Decode, Ram, GuestIo, Compaction, Run, Count
};

class BootTiming {
 public:
  using Clock = std::chrono::steady_clock;
  using Duration = Clock::duration;
  static constexpr unsigned kWindowIterations = 16, kMinGap = 3072, kMaxGap = 7167;

  struct Counters {
    std::array<Duration, static_cast<std::size_t>(BootTimePart::Count)> times{};
    std::uint64_t windows = 0, iterations = 0, scalar_steps = 0, batched_steps = 0;

    std::uint64_t ns(BootTimePart part) const noexcept {
      return std::chrono::duration_cast<std::chrono::nanoseconds>(
          times[static_cast<std::size_t>(part)]).count();
    }

    Counters since(const Counters& before) const noexcept {
      Counters delta = *this;
      for (std::size_t i = 0; i < times.size(); ++i) delta.times[i] -= before.times[i];
      delta.windows -= before.windows;
      delta.iterations -= before.iterations;
      delta.scalar_steps -= before.scalar_steps;
      delta.batched_steps -= before.batched_steps;
      return delta;
    }

    void write_json(std::ostream& out) const;
  };

  class Scope {
   public:
    Scope(BootTiming* timing, BootTimePart part) noexcept
        : timing_(timing), part_(part), start_(timing ? Clock::now() : Clock::time_point{}) {}
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() { if (timing_) timing_->record(part_, Clock::now() - start_); }

   private:
    BootTiming* timing_;
    BootTimePart part_;
    Clock::time_point start_;
  };

  // Sample short windows of consecutive machine iterations. Detailed
  // timing stays inside a window, with a fresh interval drawn after it.
  BootTiming* next_iteration() noexcept {
    if (!remaining_) {
      if (--until_window_) return nullptr;
      remaining_ = kWindowIterations;
      seed_ ^= seed_ << 13;
      seed_ ^= seed_ >> 17;
      seed_ ^= seed_ << 5;
      until_window_ = 1 + kMinGap + seed_ % (kMaxGap - kMinGap + 1);
      ++counters_.windows;
    }
    --remaining_;
    ++counters_.iterations;
    return this;
  }

  void record(BootTimePart part, Duration elapsed) noexcept {
    counters_.times[static_cast<std::size_t>(part)] += elapsed;
  }

  const Counters& counters() const noexcept { return counters_; }

  void add_steps(std::uint64_t scalar, unsigned batched) noexcept {
    counters_.scalar_steps += scalar;
    counters_.batched_steps += batched;
  }
  void reset() noexcept {
    counters_ = {};
    seed_ = kSeed;
    until_window_ = 1;
    remaining_ = 0;
  }

  void write_json(std::ostream& out) const { counters_.write_json(out); }

 private:
  static constexpr std::uint32_t kSeed = 0xcb2c3141;
  Counters counters_;
  unsigned until_window_ = 1, remaining_ = 0;
  std::uint32_t seed_ = kSeed;
};

inline void BootTiming::Counters::write_json(std::ostream& out) const {
    const auto total = ns(BootTimePart::Total);
    const auto devices = ns(BootTimePart::Device);
  const auto batch_dispatch = ns(BootTimePart::BatchDispatch);
  const auto batch_execute = ns(BootTimePart::BatchExecute);
    const auto scalar = ns(BootTimePart::Scalar);
    const auto decode = ns(BootTimePart::Decode);
    const auto ram = ns(BootTimePart::Ram);
    const auto io = ns(BootTimePart::GuestIo);
    // Instruction fetch/decode, other RAM accesses and guest I/O are timed
  // within scalar execution. Device polling and batch attempts are disjoint
    // from it. Compaction and run time use clocks outside the sample windows.
    out << "{\"window_iterations\":" << kWindowIterations << ",\"min_gap_iterations\":" << kMinGap
        << ",\"max_gap_iterations\":" << kMaxGap
        << ",\"sampled_windows\":" << windows << ",\"sampled_iterations\":" << iterations
        << ",\"sampled_scalar_steps\":" << scalar_steps
        << ",\"sampled_batched_steps\":" << batched_steps
        << ",\"sampled_iteration_ns\":" << total
        << ",\"sampled_device_ns\":" << devices
      << ",\"sampled_batch_dispatch_ns\":" << batch_dispatch
      << ",\"sampled_batch_execute_ns\":" << batch_execute
        << ",\"sampled_scalar_ns\":" << scalar
        << ",\"sampled_scalar_fetch_decode_ns\":" << decode
        << ",\"sampled_scalar_other_ram_ns\":" << ram
        << ",\"sampled_scalar_guest_io_ns\":" << io
        << ",\"sampled_scalar_cpu_other_ns\":"
        << (scalar >= decode + ram + io ? scalar - decode - ram - io : 0)
      << ",\"sampled_loop_other_ns\":"
      << (total >= devices + batch_dispatch + batch_execute + scalar
              ? total - devices - batch_dispatch - batch_execute - scalar : 0)
        << ",\"compaction_ns\":" << ns(BootTimePart::Compaction)
        << ",\"run_ns\":" << ns(BootTimePart::Run) << '}';
}

// One active category per sampled scalar step. An outer scope owns nested
// work, so BIOS or MMIO callbacks cannot also be charged as backing RAM,
// and a paging walk includes the RAM reads it performs.
class BootScalarTiming {
 public:
  class Scope {
   public:
    Scope(BootScalarTiming* timing, BootTimePart part) noexcept
        : timing_(timing && !timing->active_ ? timing : nullptr), part_(part),
          start_(timing_ ? BootTiming::Clock::now() : BootTiming::Clock::time_point{}) {
      if (timing_) timing_->active_ = true;
    }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;
    ~Scope() {
      if (timing_) {
        timing_->owner_.record(part_, BootTiming::Clock::now() - start_);
        timing_->active_ = false;
      }
    }

   private:
    BootScalarTiming* timing_;
    BootTimePart part_;
    BootTiming::Clock::time_point start_;
  };

  explicit BootScalarTiming(BootTiming& owner) noexcept : owner_(owner) {}

 private:
  BootTiming& owner_;
  bool active_ = false;
};

}  // namespace cosmic_pc
