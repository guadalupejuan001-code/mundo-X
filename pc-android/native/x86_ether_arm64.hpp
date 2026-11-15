#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(__aarch64__) && defined(__linux__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MUNDO_X_ETHER_ARM64_BLOCKS 1
#include <sys/mman.h>
#include <cstring>
#include <utility>
#include <vector>
#endif

namespace cosmic_pc::ether_arm64 {

// Decode only the register and ordinary-memory forms the ARM64 backend
// implements. The interpreter owns all other prefixes, operands and opcodes.
struct Operand {
  unsigned reg = 0;
  std::uint32_t displacement = 0;
  bool memory = false;
  bool immediate = false;
  int immediate_value = 0;
};

struct Instruction {
  enum Kind { Move, Lea, Compare, Test, Increment, Decrement, Conditional, Jump };
  Kind kind = Move;
  Operand lhs, rhs;
  unsigned width = 0, begin = 0, end = 0, opcode = 0, condition = 0;
};

struct Block {
  static constexpr unsigned kMaxBytes = 64, kMaxInstructions = 16;
  std::array<Instruction, kMaxInstructions> instructions{};
  unsigned count = 0, size = 0, flag_width = 0;
  Instruction::Kind flag_op = Instruction::Move;
  std::uint64_t target = 0;

  bool uses_arm_condition() const noexcept {
    if (!count || flag_op != Instruction::Compare || flag_width <= 1) return false;
    const auto& last = instructions[count - 1];
    return last.kind == Instruction::Conditional && last.condition < 16;
  }
};

// x86 carry on subtraction is ARM's inverse carry. Parity conditions use
// the original interpreter, which also owns the final x86 flag calculation.
inline constexpr std::array<unsigned, 16> kArmCondition{
    6, 7, 3, 2, 0, 1, 9, 8, 4, 5, 16, 16, 11, 10, 13, 12};

inline Block decode(const std::uint8_t* bytes, unsigned size, std::uint64_t rip) {
  Block block;
  const auto sign_extend = [](std::uint32_t value, unsigned width) {
    return width == 1 && (value & 0x80) ? value | 0xffffff00U : value;
  };
  const auto signed_offset = [](std::uint32_t value) -> std::uint64_t {
    return (value >> 31) ? (0xffffffff00000000ULL | value) : value;
  };
  const auto canonical = [](std::uint64_t address) {
    return address >> 47 == 0 || address >> 47 == 0x1ffff;
  };
  while (block.count < Block::kMaxInstructions && block.size < size) {
    unsigned pos = block.size;
    const unsigned begin = pos;
    unsigned opcode = bytes[pos++], rex = 0;
    if (opcode >= 0x40 && opcode <= 0x4f) {
      if (pos == size) break;
      rex = opcode;
      opcode = bytes[pos++];
    }
    Instruction op;
    op.begin = begin;
    op.opcode = opcode;
    op.width = (rex & 8) ? 8 : 4;
    if (opcode == 0x8b || opcode == 0x89 || opcode == 0x8d || opcode == 0x3b ||
        opcode == 0x39 || opcode == 0x80 || opcode == 0x83 || opcode == 0x84 ||
        opcode == 0x85 || opcode == 0xff) {
      if (pos == size) break;
      const auto modrm = bytes[pos++];
      const auto mod = modrm >> 6;
      const auto rm = modrm & 7U, group = (modrm >> 3) & 7U;
      const Operand reg{((modrm >> 3) & 7U) + ((rex & 4) ? 8U : 0U), 0, false};
      Operand other{rm + ((rex & 1) ? 8U : 0U), 0, mod != 3};
      if (other.memory) {
        if (rm == 4 || (!mod && rm == 5)) break;
        const unsigned disp_bytes = mod == 1 ? 1 : mod == 2 ? 4 : 0;
        if (disp_bytes > size - pos) break;
        for (unsigned i = 0; i < disp_bytes; ++i)
          other.displacement |= std::uint32_t(bytes[pos++]) << (8 * i);
        other.displacement = sign_extend(other.displacement, disp_bytes);
      }
      const bool reg_first = opcode == 0x8b || opcode == 0x8d || opcode == 0x3b;
      op.lhs = reg_first ? reg : other;
      op.rhs = reg_first ? other : reg;
      if (opcode == 0x8d) {
        if (!other.memory) break;
        op.kind = Instruction::Lea;
      } else if (opcode == 0xff) {
        // INC/DEC preserve carry. Retire a preceding CMP or TEST that
        // defines this flag before allowing INC/DEC to use it.
        if (other.memory || group > 1 || block.flag_op == Instruction::Compare ||
            block.flag_op == Instruction::Test) break;
        op.kind = group == 0 ? Instruction::Increment : Instruction::Decrement;
      } else if (opcode == 0x80 || opcode == 0x83) {
        if (group != 7 || pos == size) break;
        const unsigned value = bytes[pos++];
        if (opcode == 0x80) {
          if (!rex && !other.memory && other.reg >= 4) break;
          op.width = 1;
        }
        op.rhs = {0, 0, false, true, int(value) - (op.width > 1 && value >= 128 ? 256 : 0)};
        op.kind = Instruction::Compare;
      } else if (opcode == 0x84 || opcode == 0x85) {
        if (opcode == 0x84) {
          // Without REX, encodings 4 through 7 refer to AH, CH, DH and BH.
          if (!rex && (reg.reg >= 4 || (!other.memory && other.reg >= 4))) break;
          op.width = 1;
        }
        op.kind = Instruction::Test;
      } else {
        op.kind = opcode == 0x8b || opcode == 0x89 ? Instruction::Move : Instruction::Compare;
      }
      // TEST preserves AF. Retire any earlier producer of AF first.
      if (op.kind == Instruction::Test && block.flag_width && block.flag_op != Instruction::Test) break;
      if (op.kind != Instruction::Move && op.kind != Instruction::Lea) {
        block.flag_width = op.width;
        block.flag_op = op.kind;
      }
    } else {
      if (rex) break;
      unsigned disp_bytes = 1;
      if (opcode == 0x0f && pos < size && bytes[pos] >= 0x80 && bytes[pos] < 0x90) {
        opcode = 0x0f00U | bytes[pos++];
        op.opcode = opcode;
        disp_bytes = 4;
      }
      const bool conditional = (opcode >= 0x70 && opcode <= 0x7f) ||
          (opcode >= 0x0f80 && opcode <= 0x0f8f);
      if (conditional) {
        op.condition = kArmCondition[opcode & 15];
        op.kind = Instruction::Conditional;
      } else if (opcode == 0xeb || opcode == 0xe9) {
        if (opcode == 0xe9) disp_bytes = 4;
        op.kind = Instruction::Jump;
      } else {
        break;
      }
      if (disp_bytes > size - pos) break;
      std::uint32_t displacement = 0;
      for (unsigned i = 0; i < disp_bytes; ++i)
        displacement |= std::uint32_t(bytes[pos++]) << (8 * i);
      const auto target = rip + pos + signed_offset(sign_extend(displacement, disp_bytes));
      if (!canonical(target)) break;
      block.target = target;
    }
    op.end = pos;
    block.instructions[block.count++] = op;
    block.size = pos;
    // Commit a store last. Revalidate code in the next block, including
    // writes through a physical alias.
    if (op.kind == Instruction::Conditional || op.kind == Instruction::Jump ||
        (op.kind == Instruction::Move && op.lhs.memory)) break;
  }
  return block;
}

#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
struct Frame {
  using Read = unsigned (*)(Frame*, std::uint64_t, unsigned) noexcept;
  using Write = unsigned (*)(Frame*, std::uint64_t, unsigned, std::uint64_t) noexcept;
  std::array<std::uint64_t, 16> regs;
  std::uint64_t left = 0, right = 0, loaded = 0;
  Read read;
  Write write;
  void* cpu;
};

// The emitter is bounded by decoded instructions. Guest bytes are never
// mapped as host instructions. Address arithmetic uses guest registers,
// and all loads go through the original CPU's guarded RAM accessor.
class Emitter {
 public:
  explicit Emitter(const Block& block) {
    code_.reserve(96);
    emit(0xd503245f); // BTI c (a hint on hosts without BTI).
    emit(0xa9bf7bf3); // STP X19,X30,[SP,#-16]!
    emit(0xaa0003f3); // MOV X19,X0
    for (unsigned i = 0; i < block.count; ++i) {
      const auto& op = block.instructions[i];
      if (op.kind == Instruction::Move && op.lhs.memory) {
        prepare_access(op.lhs, op.width);
        load(3, op.rhs.reg * 8, op.width);
        call(offsetof(Frame, write));
      } else if (op.kind == Instruction::Move) {
        read(op.rhs, op.width);
        store(9, op.lhs.reg * 8);
      } else if (op.kind == Instruction::Lea) {
        address(op.rhs, 9);
        if (op.width == 4) emit(0x2a0903e9); // MOV W9,W9 clears the upper half.
        store(9, op.lhs.reg * 8);
      } else if (op.kind == Instruction::Increment || op.kind == Instruction::Decrement) {
        read(op.lhs, op.width);
        store(9, offsetof(Frame, left));
        const auto subtract = op.kind == Instruction::Decrement ? 0x40000000U : 0;
        const auto wide = op.width == 8 ? 0x80000000U : 0;
        emit(0x11000529U | subtract | wide); // ADD/SUB X/W9,X/W9,#1
        store(9, op.lhs.reg * 8);
      } else if (op.kind == Instruction::Compare || op.kind == Instruction::Test) {
        read(op.lhs, op.width);
        store(9, offsetof(Frame, left));
        read(op.rhs, op.width);
        store(9, offsetof(Frame, right));
      }
    }
    const auto& last = block.instructions[block.count - 1];
    if (block.uses_arm_condition()) {
      load(9, offsetof(Frame, left), block.flag_width);
      load(10, offsetof(Frame, right), block.flag_width);
      emit(block.flag_width == 8 ? 0xeb0a013f : 0x6b0a013f); // CMP X/W9,X/W10
      emit(0x1a9f07e0U | ((last.condition ^ 1U) << 12)); // CSET W0,cc
      emit(0x11000400); // ADD W0,W0,#1
    } else {
      emit(last.kind == Instruction::Jump ? 0x52800040 : 0x52800020); // MOV W0,#2/#1
    }
    const auto epilogue = code_.size();
    emit(0xa8c17bf3); // LDP X19,X30,[SP],#16
    emit(0xd65f03c0); // RET
    for (unsigned i = 0; i < exits_; ++i) {
      const auto pos = failed_access_[i];
      code_[pos] = 0x34000000U | (std::uint32_t(epilogue - pos) << 5); // CBZ W0
    }
  }

  const std::vector<std::uint32_t>& words() const noexcept { return code_; }

 private:
  std::vector<std::uint32_t> code_;
  std::array<std::size_t, Block::kMaxInstructions> failed_access_{};
  unsigned exits_ = 0;

  void emit(std::uint32_t word) { code_.push_back(word); }
  void load(unsigned reg, unsigned offset, unsigned width = 8) {
    const auto load_op = width == 8 ? 0xf9400000U : width == 4 ? 0xb9400000U : 0x39400000U;
    emit(load_op | ((offset / width) << 10) | (19U << 5) | reg);
  }
  void store(unsigned reg, unsigned offset) {
    emit(0xf9000000U | ((offset / 8) << 10) | (19U << 5) | reg);
  }
  void address(const Operand& operand, unsigned reg) {
    load(reg, operand.reg * 8);
    const auto regs = (reg << 5) | reg;
    const auto disp = operand.displacement;
    if (disp && disp <= 4095) {
      emit(0x91000000U | (disp << 10) | regs); // ADD Xd,Xd,#disp
    } else if (disp >= 0xfffff001U) {
      emit(0xd1000000U | ((0U - disp) << 10) | regs); // SUB Xd,Xd,#-disp
    } else if (disp) {
      emit(0x5280000aU | ((disp & 0xffff) << 5)); // MOVZ W10,low
      emit(0x72a0000aU | ((disp >> 16) << 5)); // MOVK W10,high,LSL #16
      emit(0x8b2ac000U | regs); // ADD Xd,Xd,W10,SXTW
    }
  }
  void prepare_access(const Operand& operand, unsigned width) {
    emit(0xaa1303e0); // MOV X0,X19
    address(operand, 1);
    emit(0x52800002U | (width << 5)); // MOV W2,#width
  }
  void call(unsigned offset) {
    load(9, offset);
    emit(0xd63f0120); // BLR X9
    failed_access_[exits_++] = code_.size();
    emit(0); // Patched to CBZ W0 on failure.
  }
  void read(const Operand& operand, unsigned width) {
    if (operand.immediate) {
      if (operand.immediate_value < 0) {
        const auto movn = width == 8 ? 0x92800009U : 0x12800009U;
        emit(movn | (std::uint32_t(~operand.immediate_value) << 5));
      } else {
        emit(0x52800009U | (std::uint32_t(operand.immediate_value) << 5)); // MOVZ W9,imm
      }
      return;
    }
    if (!operand.memory) {
      load(9, operand.reg * 8, width);
      return;
    }
    prepare_access(operand, width);
    call(offsetof(Frame, read));
    load(9, offsetof(Frame, loaded), width);
  }
};

class NativeCode {
 public:
  using Run = unsigned (*)(Frame*);
  NativeCode() = default;
  NativeCode(const NativeCode&) = delete;
  NativeCode& operator=(const NativeCode&) = delete;
  NativeCode& operator=(NativeCode&& other) noexcept {
    std::swap(mapping_, other.mapping_);
    std::swap(bytes_, other.bytes_);
    return *this;
  }
  ~NativeCode() { clear(); }

  bool build(const Block& block) {
    const Emitter emitter(block);
    const auto& words = emitter.words();
    const auto bytes = words.size() * sizeof(words.front());
    void* mapping = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return false;
    std::memcpy(mapping, words.data(), bytes);
    auto* start = static_cast<char*>(mapping);
    __builtin___clear_cache(start, start + bytes);
    if (mprotect(mapping, bytes, PROT_READ | PROT_EXEC) != 0) {
      munmap(mapping, bytes);
      return false;
    }
    clear();
    mapping_ = mapping;
    bytes_ = bytes;
    return true;
  }
  explicit operator bool() const noexcept { return mapping_ != nullptr; }
  unsigned run(Frame& frame) const { return reinterpret_cast<Run>(mapping_)(&frame); }
  void clear() noexcept {
    if (mapping_) munmap(mapping_, bytes_);
    mapping_ = nullptr;
    bytes_ = 0;
  }

 private:
  void* mapping_ = nullptr;
  std::size_t bytes_ = 0;
};

struct Cache {
  struct Entry {
    std::uint64_t rip = 0, physical = 0, cr3 = 0;
    std::uint64_t last_used = 0;
    unsigned cpl = 0, checked_bytes = 0, visits = 0;
    bool occupied = false;
    std::array<std::uint8_t, Block::kMaxBytes> bytes{};
    Block block;
    NativeCode code;

    void invalidate(std::uint64_t address) noexcept {
      clear();
      rip = address;
      occupied = true;
    }

    void clear() noexcept {
      code.clear();
      checked_bytes = 0;
      visits = 0;
      occupied = false;
    }
  };
  static constexpr std::size_t kSets = 128;
  static constexpr std::size_t kWays = 4;
  struct Set {
    std::array<Entry, kWays> entries;
    std::array<Entry, kWays> probes;
    std::uint64_t clock = 0;
    std::size_t next_probe = 0;
  };
  struct Lookup { Entry& entry; bool pending; };
  std::array<Set, kSets> sets;
  bool host_rejected = false;

  static std::size_t set_index(std::uint64_t rip) noexcept {
    return (rip ^ (rip >> 5) ^ (rip >> 12)) & (kSets - 1);
  }

  Lookup lookup(std::uint64_t rip) noexcept {
    auto& set = sets[set_index(rip)];
    for (auto& item : set.entries) {
      if (item.occupied && item.rip == rip) {
        item.last_used = ++set.clock;
        return {item, false};
      }
    }
    for (auto& item : set.probes) {
      if (item.occupied && item.rip == rip) return {item, true};
    }
    auto& probe = set.probes[set.next_probe];
    set.next_probe = (set.next_probe + 1) % kWays;
    probe.invalidate(rip);
    return {probe, true};
  }

  Entry& admit(Entry& probe, std::uint64_t& compiled_evictions) noexcept {
    auto& set = sets[set_index(probe.rip)];
    auto* victim = &set.entries.front();
    // Only compiled blocks can replace native code. Prefer a vacant way
    // and then the least recently used translation.
    for (auto& item : set.entries) {
      if (!item.code) { victim = &item; break; }
      if (item.last_used < victim->last_used) victim = &item;
    }
    if (victim->code) ++compiled_evictions;
    *victim = std::move(probe);
    victim->last_used = ++set.clock;
    probe.clear();
    return *victim;
  }
  void clear() noexcept {
    for (auto& set : sets) {
      for (auto& item : set.entries) item.clear();
      for (auto& probe : set.probes) probe.clear();
      set.next_probe = 0;
      set.clock = 0;
    }
  }
};
#endif

}  // namespace cosmic_pc::ether_arm64
