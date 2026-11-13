#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace mundo_x::ether_next {
// This is an Ether value transformation, not a guest CPU or architectural state.
// The caller owns XMM/FP/exception state and grants a pure register-only block.
using Vector = std::array<std::uint8_t, 16>;
using Registers = std::array<Vector, 16>;
enum class Op : std::uint8_t {
  Input, Zero, Xor, And, Or, AndNot,
  Add8, Add16, Add32, Add64, Sub8, Sub16, Sub32, Sub64
};
struct Node {
  Op op;
  std::uint16_t a, b;
};
static_assert(sizeof(Node) <= 8, "Ether operation must remain compact");
struct Transform {
  // Inputs 0..15, zero 16; topologically ordered, structurally interned SSA.
  std::vector<Node> nodes;
  std::array<std::uint16_t, 16> outputs{};
  // Only reachable non-input nodes, in topological order. Zero counts as work.
  std::vector<std::uint16_t> schedule;
  std::size_t source_operations = 0;
};
struct Guard {
  bool long_mode = true;
  bool sse2_enabled = false;
  bool osfxsr = false;
  bool cr0_em = false, cr0_ts = false;
  bool single_step = false;
  bool instruction_observation = false;
  bool permits_pure_block() const {
    return long_mode && sse2_enabled && osfxsr && !cr0_em && !cr0_ts &&
           !single_step && !instruction_observation;
  }
};
// Returns nullptr for ANY unsupported/truncated byte; never consumes a prefix
// of a block. No guest memory access, exception delivery or RIP update here.
std::shared_ptr<const Transform> reduce_x86(const std::vector<std::uint8_t>& code);

class NativeCode {
 public:
  explicit NativeCode(const Transform& transform);
  ~NativeCode();
  NativeCode(const NativeCode&) = delete;
  NativeCode& operator=(const NativeCode&) = delete;
  bool available() const noexcept;
  bool execute(Registers& values) const;
  const std::vector<std::uint32_t>& words() const noexcept { return words_; }
  std::size_t vector_instructions() const noexcept { return vector_instructions_; }
  std::size_t loads() const noexcept { return loads_; }
  std::size_t stores() const noexcept { return stores_; }
 private:
  void* memory_ = nullptr;
  std::size_t memory_bytes_ = 0;
  std::vector<std::uint32_t> words_;
  std::size_t vector_instructions_ = 0, loads_ = 0, stores_ = 0;
};
// Bounded, exact-byte keyed transformation + native-code memory. There is no
// result cache: each hit reads the current inputs. Owner serializes access.
class Cache {
 public:
  explicit Cache(std::size_t capacity = 32) : capacity_(capacity) {}
  bool execute(const std::vector<std::uint8_t>& code, Registers& values, const Guard& guard);
  void invalidate() noexcept { entries_.clear(); }
  struct Stats { std::size_t hits = 0, misses = 0, fallbacks = 0; };
  Stats stats() const noexcept { return stats_; }
 private:
  struct Entry {
    std::vector<std::uint8_t> bytes;
    std::shared_ptr<const Transform> transform;
    std::shared_ptr<const NativeCode> native;
  };
  std::size_t capacity_;
  std::vector<Entry> entries_;
  Stats stats_;
};
} // namespace mundo_x::ether_next
