#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#if defined(__aarch64__) && defined(__linux__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MUNDO_X_ETHER_ARM64_BLOCKS 1
#include <sys/mman.h>
#include <cstring>
#include <vector>
#endif

namespace cosmic_pc::ether_arm64 {

// Decode only the register and ordinary-memory forms the ARM64 backend
// implements. The interpreter owns all other prefixes, operands and opcodes.
struct Operand {
  unsigned reg = 0;
  std::uint32_t displacement = 0;
  bool memory = false;
};

struct Instruction {
  enum Kind { Move, Compare, Conditional, Jump };
  Kind kind = Move;
  Operand lhs, rhs;
  unsigned width = 0, begin = 0, end = 0, opcode = 0, condition = 0;
};

struct Block {
  static constexpr unsigned kMaxBytes = 64, kMaxInstructions = 8;
  std::array<Instruction, kMaxInstructions> instructions{};
  unsigned count = 0, size = 0, compare_width = 0;
  std::uint64_t target = 0;
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
    if (opcode == 0x8b || opcode == 0x89 || opcode == 0x3b || opcode == 0x39) {
      if (pos == size) break;
      const auto modrm = bytes[pos++];
      const auto mod = modrm >> 6;
      const auto rm = modrm & 7U;
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
      const bool reg_first = opcode == 0x8b || opcode == 0x3b;
      op.lhs = reg_first ? reg : other;
      op.rhs = reg_first ? other : reg;
      op.kind = opcode == 0x8b || opcode == 0x89 ? Instruction::Move : Instruction::Compare;
      if (op.kind == Instruction::Compare) block.compare_width = op.width;
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
        if (!block.compare_width || op.condition == 16) break;
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
        address(op.lhs, op.width);
        load(3, op.rhs.reg * 8, op.width);
        call(offsetof(Frame, write));
      } else if (op.kind == Instruction::Move) {
        read(op.rhs, op.width);
        store(9, op.lhs.reg * 8);
      } else if (op.kind == Instruction::Compare) {
        read(op.lhs, op.width);
        store(9, offsetof(Frame, left));
        read(op.rhs, op.width);
        store(9, offsetof(Frame, right));
      }
    }
    const auto& last = block.instructions[block.count - 1];
    if (last.kind == Instruction::Conditional) {
      load(9, offsetof(Frame, left), block.compare_width);
      load(10, offsetof(Frame, right), block.compare_width);
      emit(block.compare_width == 8 ? 0xeb0a013f : 0x6b0a013f); // CMP X/W9,X/W10
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
    emit((width == 8 ? 0xf9400000U : 0xb9400000U) | ((offset / width) << 10) | (19U << 5) | reg);
  }
  void store(unsigned reg, unsigned offset) {
    emit(0xf9000000U | ((offset / 8) << 10) | (19U << 5) | reg);
  }
  void address(const Operand& operand, unsigned width) {
    emit(0xaa1303e0); // MOV X0,X19
    load(1, operand.reg * 8);
    const auto disp = operand.displacement;
    if (disp && disp <= 4095) {
      emit(0x91000021U | (disp << 10)); // ADD X1,X1,#disp
    } else if (disp >= 0xfffff001U) {
      emit(0xd1000021U | ((0U - disp) << 10)); // SUB X1,X1,#-disp
    } else if (disp) {
      emit(0x5280000aU | ((disp & 0xffff) << 5)); // MOVZ W10,low
      emit(0x72a0000aU | ((disp >> 16) << 5)); // MOVK W10,high,LSL #16
      emit(0x8b2ac021); // ADD X1,X1,W10,SXTW
    }
    emit(0x52800002U | (width << 5)); // MOV W2,#width
  }
  void call(unsigned offset) {
    load(9, offset);
    emit(0xd63f0120); // BLR X9
    failed_access_[exits_++] = code_.size();
    emit(0); // Patched to CBZ W0 on failure.
  }
  void read(const Operand& operand, unsigned width) {
    if (!operand.memory) {
      load(9, operand.reg * 8, width);
      return;
    }
    address(operand, width);
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
  }

 private:
  void* mapping_ = nullptr;
  std::size_t bytes_ = 0;
};

struct Cache {
  struct Entry {
    std::uint64_t rip = 0, physical = 0, cr3 = 0;
    unsigned cpl = 0, checked_bytes = 0, visits = 0;
    std::array<std::uint8_t, Block::kMaxBytes> bytes{};
    Block block;
    NativeCode code;

    void invalidate(std::uint64_t address) noexcept {
      code.clear();
      rip = address;
      checked_bytes = 0;
      visits = 0;
    }
  };
  static constexpr std::size_t kEntries = 32;
  std::array<Entry, kEntries> entries;
  bool host_rejected = false;

  Entry& entry(std::uint64_t rip) noexcept {
    return entries[(rip ^ (rip >> 5) ^ (rip >> 12)) & (kEntries - 1)];
  }
  void clear() noexcept {
    for (auto& item : entries) item.invalidate(0);
  }
};
#endif

}  // namespace cosmic_pc::ether_arm64
