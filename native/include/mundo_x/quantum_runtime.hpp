#pragma once

#include "mundo_x/infinite_program.hpp"
#include "mundo_x/inverse_memory.hpp"

#include <string>
#include <utility>

namespace mundo_x {

// Quantum-only owner for Mundo X.
//
// It deliberately excludes PC, graphics, Solar and physics. InfiniteCore is
// the quantum authority; InverseMemory is the rule-first logical memory view.
// An XI program is applied to private candidates and both states commit
// together only when every command and every buffered output succeeds.
class QuantumRuntime {
 public:
  using Background = InfiniteCore::Background;
  using CoreLimits = InfiniteCore::Limits;
  using MemoryLimits = InverseMemory::Limits;

  QuantumRuntime()
      : QuantumRuntime(Background::Zero, CoreLimits{}, MemoryLimits{}) {}

  QuantumRuntime(Background background, CoreLimits core_limits,
                 MemoryLimits memory_limits)
      : core_(background, core_limits), memory_(0, memory_limits) {}

  const InfiniteCore& core() const noexcept { return core_; }
  const InverseMemory& memory() const noexcept { return memory_; }

  std::string execute(const std::string& program,
                      bool require_matching_background = false) {
    auto candidate_core = core_;
    auto candidate_memory = memory_;
    auto result = infinite_program_detail::run(
        candidate_core, &candidate_memory, program, require_matching_background);
    core_ = std::move(candidate_core);
    memory_ = std::move(candidate_memory);
    return std::move(result.output);
  }

  std::string core_snapshot() const { return core_.snapshot(); }
  std::string memory_snapshot() const { return memory_.snapshot(); }

  // Restore both authorities transactionally. A bad core or memory snapshot
  // leaves the current runtime untouched.
  void restore(const std::string& core_saved, const std::string& memory_saved) {
    auto candidate_core = InfiniteCore::from_snapshot(core_saved, core_.limits());
    auto candidate_memory = InverseMemory::from_snapshot(memory_saved, memory_.limits());
    core_ = std::move(candidate_core);
    memory_ = std::move(candidate_memory);
  }

 private:
  InfiniteCore core_;
  InverseMemory memory_;
};

}  // namespace mundo_x
