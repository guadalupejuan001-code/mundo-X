#pragma once

#include <algorithm>
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

inline constexpr unsigned kStackAccess = 16; // Packed alongside the RAM callback's width.
inline constexpr unsigned kSubtractAccess = 32;

// Decode only the register and ordinary-memory forms the ARM64 backend
// implements. The interpreter owns all other prefixes, operands and opcodes.
struct Operand {
  static constexpr unsigned kNoIndex = 16, kRipBase = 17;
  unsigned reg = 0;
  std::uint32_t displacement = 0;
  std::uint64_t immediate_value = 0;
  unsigned index = kNoIndex, scale_shift = 0;
  bool memory = false, immediate = false, stack = false;

  static Operand literal(std::uint64_t value) noexcept {
    Operand operand;
    operand.immediate = true;
    operand.immediate_value = value;
    return operand;
  }
};

struct Instruction {
  enum Kind {
    Unsupported, Move, MoveByte, MoveZeroExtend, MoveSignExtend, Lea, Nop, Compare, Test, Increment, Decrement,
    Add, Subtract, And, Or, Xor, ShiftLeft, ShiftRight, ShiftArithmetic, Conditional, Jump
  };
  Kind kind = Move;
  Operand lhs, rhs;
  unsigned width = 0, begin = 0, end = 0, opcode = 0, condition = 0;

  static bool logical(Kind kind) noexcept {
    return kind == Test || kind == And || kind == Or || kind == Xor;
  }
  static bool shift(Kind kind) noexcept {
    return kind == ShiftLeft || kind == ShiftRight || kind == ShiftArithmetic;
  }
  bool writes_ram() const noexcept {
    return lhs.memory && (kind == Move || kind == MoveByte || kind == Subtract);
  }
  bool writes_register() const noexcept {
    return !lhs.memory && kind != Nop && kind != Compare && kind != Test &&
        kind != Conditional && kind != Jump;
  }
};

struct Block {
  static constexpr unsigned kMaxBytes = 64, kMaxInstructions = 16;
  static constexpr unsigned kMaxWrites = 4;
  std::array<Instruction, kMaxInstructions> instructions{};
  unsigned count = 0, size = 0, flag_width = 0, flag_index = 0, writes = 0;
  Instruction::Kind flag_op = Instruction::Move;
  std::uint64_t target = 0;
  bool long_mode = true, repeats = false;

  bool stages_ram() const noexcept {
    return writes && (writes > 1 || !instructions[count - 1].writes_ram());
  }

  bool uses_arm_condition() const noexcept {
    if (!count || flag_width < 4 ||
        (flag_op != Instruction::Compare && flag_op != Instruction::Subtract)) return false;
    const auto& last = instructions[count - 1];
    return last.kind == Instruction::Conditional && last.condition < 16;
  }
};

inline bool fixed_loop_addresses(const Block& block) noexcept {
  unsigned fixed = 0xffffU;
  for (unsigned i = 0; i < block.count; ++i) {
    const auto& op = block.instructions[i];
    if (op.writes_register()) fixed &= ~(1U << op.lhs.reg);
  }
  const auto fixed_register = [&](unsigned reg) {
    return reg >= Operand::kNoIndex || (fixed & (1U << reg)) != 0;
  };
  const auto fixed_address = [&](const Operand& operand) {
    return !operand.memory || (fixed_register(operand.reg) && fixed_register(operand.index));
  };
  for (unsigned i = 0; i < block.count; ++i) {
    const auto& op = block.instructions[i];
    if (!fixed_address(op.lhs) || (op.kind != Instruction::Lea && !fixed_address(op.rhs)))
      return false;
    if (op.writes_register()) {
      const auto bit = 1U << op.lhs.reg;
      // A full-width move from an invariant input overwrites any
      // loop-carried value. A partial move still depends on the old register.
      if (op.kind == Instruction::Move && op.width >= 4 && !op.rhs.memory &&
          (op.rhs.immediate || fixed_register(op.rhs.reg)))
        fixed |= bit;
      else
        fixed &= ~bit;
    }
  }
  return true;
}

// x86 carry on subtraction is ARM's inverse carry. Parity conditions use
// the original interpreter, which also owns the final x86 flag calculation.
inline constexpr std::array<unsigned, 16> kArmCondition{
    6, 7, 3, 2, 0, 1, 9, 8, 4, 5, 16, 16, 11, 10, 13, 12};
inline constexpr std::array<Instruction::Kind, 8> kGroupOne{
    Instruction::Add, Instruction::Or, Instruction::Unsupported, Instruction::Unsupported,
    Instruction::And, Instruction::Subtract, Instruction::Xor, Instruction::Compare};

inline Block decode(const std::uint8_t* bytes, unsigned size, std::uint64_t rip, bool long_mode = true) {
  Block block;
  block.long_mode = long_mode;
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
    const bool word = opcode == 0x66;
    if (word) {
      if (pos == size) break;
      opcode = bytes[pos++];
    }
    if (long_mode && opcode >= 0x40 && opcode <= 0x4f) {
      if (pos == size) break;
      rex = opcode;
      opcode = bytes[pos++];
    }
    if (long_mode && word && opcode != 0x89 && opcode != 0x8b) break;
    if (opcode == 0x0f && pos < size &&
        ((bytes[pos] & 0xfeU) == 0xb6U || (long_mode && (bytes[pos] & 0xfeU) == 0xbeU)))
      opcode = 0x0f00U | bytes[pos++];
    const auto form = opcode & 7;
    const auto direct_alu = opcode <= 0x3d && form <= 5
        ? kGroupOne[opcode >> 3] : Instruction::Unsupported;
    const bool binary_alu = direct_alu != Instruction::Unsupported && form < 4;
    const bool zero_extend = opcode == 0x0fb6 || opcode == 0x0fb7;
    const bool sign_extend_move = opcode == 0x0fbe || opcode == 0x0fbf ||
        (long_mode && opcode == 0x63 && (rex & 8));
    const bool move_byte = opcode == 0x88 || opcode == 0x8a;
    const bool group_immediate = opcode == 0x80 || opcode == 0x81 || opcode == 0x83;
    const bool move_immediate = opcode == 0xc6 || opcode == 0xc7;
    const bool test_immediate = opcode == 0xf6 || opcode == 0xf7;
    const bool shift = opcode == 0xc1 || opcode == 0xd1 || opcode == 0xd3;
    const bool carry_preserved = !block.flag_width ||
        block.flag_op == Instruction::Increment || block.flag_op == Instruction::Decrement;
    const auto immediate = [&](unsigned width, bool sign) {
      std::uint64_t value = 0;
      for (unsigned i = 0; i < width; ++i) value |= std::uint64_t(bytes[pos++]) << (8 * i);
      if (sign && width < 8 && (value >> (8 * width - 1))) value |= UINT64_MAX << (8 * width);
      return Operand::literal(value);
    };
    Instruction op;
    op.begin = begin;
    op.opcode = opcode;
    op.width = (rex & 8) ? 8 : word ? 2 : 4;
    if (!long_mode && opcode >= 0x40 && opcode <= 0x4f) {
      if (!carry_preserved) break;
      op.lhs.reg = opcode & 7;
      op.kind = opcode < 0x48 ? Instruction::Increment : Instruction::Decrement;
    } else if (opcode >= 0xb0 && opcode <= 0xbf) {
      op.lhs.reg = (opcode & 7U) + ((rex & 1) ? 8 : 0);
      if (opcode < 0xb8) {
        if (!rex && op.lhs.reg >= 4) break;
        op.width = 1;
        op.kind = Instruction::MoveByte;
      }
      if (op.width > size - pos) break;
      op.rhs = immediate(op.width, false);
    } else if ((direct_alu != Instruction::Unsupported && form >= 4) || opcode == 0xa8 || opcode == 0xa9) {
      if (!(opcode & 1)) op.width = 1;
      const unsigned literal_width = op.width == 8 ? 4 : op.width;
      if (literal_width > size - pos) break;
      op.kind = direct_alu != Instruction::Unsupported ? direct_alu : Instruction::Test;
      op.rhs = immediate(literal_width, op.width == 8);
    } else if (opcode == 0x90 && !(rex & 1)) {
      op.kind = Instruction::Nop;
    } else if (opcode == 0x8b || opcode == 0x89 || opcode == 0x8d || opcode == 0x84 ||
        opcode == 0x85 || opcode == 0xff || zero_extend || sign_extend_move || move_byte || binary_alu ||
        group_immediate || move_immediate || test_immediate || shift) {
      if (pos == size) break;
      const auto modrm = bytes[pos++];
      const auto mod = modrm >> 6;
      const auto rm = modrm & 7U, group = (modrm >> 3) & 7U;
      const Operand reg{group + ((rex & 4) ? 8U : 0U)};
      Operand other{rm + ((rex & 1) ? 8U : 0U)};
      other.memory = mod != 3;
      if (other.memory) {
        unsigned base = rm;
        if (rm == 4) {
          if (pos == size) break;
          const auto sib = bytes[pos++];
          base = sib & 7U;
          const auto index = (sib >> 3) & 7U;
          if (index != 4 || (rex & 2)) {
            other.index = index + ((rex & 2) ? 8U : 0U);
            other.scale_shift = sib >> 6;
          }
        }
        const bool no_base = mod == 0 && base == 5;
        if (no_base) other.reg = long_mode && rm == 5 ? Operand::kRipBase : Operand::kNoIndex;
        else other.reg = base + ((rex & 1) ? 8U : 0U);
        other.stack = !long_mode && !no_base && (base == 4 || base == 5);
        const unsigned disp_bytes = mod == 1 ? 1 : (mod == 2 || no_base ? 4 : 0);
        if (disp_bytes > size - pos) break;
        for (unsigned i = 0; i < disp_bytes; ++i)
          other.displacement |= std::uint32_t(bytes[pos++]) << (8 * i);
        other.displacement = sign_extend(other.displacement, disp_bytes);
      }
      const bool reg_first = opcode == 0x8b || opcode == 0x8d ||
          zero_extend || sign_extend_move || opcode == 0x8a || (binary_alu && (opcode & 2));
      op.lhs = reg_first ? reg : other;
      op.rhs = reg_first ? other : reg;
      if (zero_extend || move_byte) {
        if (word && zero_extend) break;
        op.width = opcode == 0x0fb7 ? 2 : 1;
        // Without REX, byte register encodings 4 through 7 use the high byte.
        if (op.width == 1 && !rex && ((move_byte && reg.reg >= 4) ||
            (!other.memory && other.reg >= 4))) break;
        op.kind = move_byte ? Instruction::MoveByte : Instruction::MoveZeroExtend;
      } else if (sign_extend_move) {
        if (opcode == 0x0fbe && !rex && !other.memory && other.reg >= 4) break;
        op.kind = Instruction::MoveSignExtend;
      } else if (opcode == 0x8d) {
        if (!other.memory) break;
        op.kind = Instruction::Lea;
      } else if (opcode == 0xff) {
        // INC/DEC preserve carry. Retire its earlier producer first.
        if (other.memory || group > 1 || !carry_preserved) break;
        op.kind = group == 0 ? Instruction::Increment : Instruction::Decrement;
      } else if (group_immediate || move_immediate || test_immediate) {
        if (group_immediate) {
          op.kind = kGroupOne[group];
          if (op.kind == Instruction::Unsupported || (other.memory && op.kind != Instruction::Compare)) break;
        } else {
          if (group != 0) break;
          op.kind = test_immediate ? Instruction::Test
              : opcode == 0xc6 ? Instruction::MoveByte : Instruction::Move;
        }
        if (opcode == 0x80 || opcode == 0xc6 || opcode == 0xf6) {
          op.width = 1;
          if (!rex && !other.memory && other.reg >= 4) break;
        }
        const unsigned literal_width = opcode == 0x83 ? 1 : (op.width == 8 ? 4 : op.width);
        if (literal_width > size - pos) break;
        op.rhs = immediate(literal_width, opcode == 0x83 || op.width == 8);
      } else if (shift) {
        // A zero count can keep all incoming flags. Even on a nonzero count,
        // OF or AF can depend on their previous values.
        if (other.memory || op.width < 4 || group < 4 || block.flag_width) break;
        if (opcode == 0xc1 && pos == size) break;
        op.kind = group == 5 ? Instruction::ShiftRight
            : group == 7 ? Instruction::ShiftArithmetic : Instruction::ShiftLeft;
        op.rhs = opcode == 0xd3 ? Operand{1} : Operand::literal(opcode == 0xd1 ? 1 : bytes[pos++]);
      } else if (opcode == 0x84 || opcode == 0x85) {
        if (opcode == 0x84) {
          // Without REX, encodings 4 through 7 refer to AH, CH, DH and BH.
          if (!rex && (reg.reg >= 4 || (!other.memory && other.reg >= 4))) break;
          op.width = 1;
        }
        op.kind = Instruction::Test;
      } else if (binary_alu) {
        // Only the observed 64-bit SUB r/m64,r64 variant has a guarded write.
        const bool subtract_memory = opcode == 0x29 && op.width == 8;
        if (op.lhs.memory && direct_alu != Instruction::Compare && !subtract_memory) break;
        if (!(opcode & 1)) {
          op.width = 1;
          if (!rex && (reg.reg >= 4 || (!other.memory && other.reg >= 4))) break;
        }
        op.kind = direct_alu;
      }
    } else {
      if (rex || word) break;
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
      if (long_mode && !canonical(target)) break;
      block.target = long_mode ? target : std::uint32_t(target);
    }
    // Logical operations and shifts preserve AF. Retire an earlier producer of AF first.
    if (Instruction::logical(op.kind) && block.flag_width &&
        !Instruction::logical(block.flag_op) && !Instruction::shift(block.flag_op)) break;
    if (op.kind == Instruction::Compare || Instruction::logical(op.kind) || Instruction::shift(op.kind) ||
        op.kind == Instruction::Increment || op.kind == Instruction::Decrement ||
        op.kind == Instruction::Add || op.kind == Instruction::Subtract) {
      block.flag_width = op.width;
      block.flag_op = op.kind;
      block.flag_index = block.count;
    }
    if (op.lhs.memory && op.lhs.reg == Operand::kRipBase)
      op.lhs.immediate_value = rip + pos + signed_offset(op.lhs.displacement);
    if (op.rhs.memory && op.rhs.reg == Operand::kRipBase)
      op.rhs.immediate_value = rip + pos + signed_offset(op.rhs.displacement);
    op.end = pos;
    block.instructions[block.count++] = op;
    block.size = pos;
    if (op.writes_ram() && (++block.writes == Block::kMaxWrites || !long_mode)) break;
    if (op.kind == Instruction::Conditional || op.kind == Instruction::Jump) break;
  }
  block.repeats = long_mode && block.target == rip && block.uses_arm_condition() &&
      fixed_loop_addresses(block);
  return block;
}

#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
struct PendingRamWrites {
  struct Store {
    std::uint64_t physical, value;
    unsigned width;
  };
  std::array<Store, Block::kMaxWrites> stores;
  std::uint64_t code_first, code_end;
  unsigned count;

  bool stage(std::uint64_t physical, unsigned width, std::uint64_t value) noexcept {
    // A store to the executing bytes would change a later x86 fetch.
    if (physical < code_end && code_first < physical + width) return false;
    for (unsigned i = count; i > 0; --i) {
      const auto& store = stores[i - 1];
      if (store.physical != physical || store.width != width) continue;
      // The new store covers the older store completely. Place it after
      // intermediate partial aliases so loads still read the newest bytes.
      if (i < count) std::move(stores.begin() + i, stores.begin() + count, stores.begin() + i - 1);
      stores[count - 1] = {physical, value, width};
      return true;
    }
    if (count == stores.size()) return false;
    stores[count++] = {physical, value, width};
    return true;
  }

  template <class Read>
  std::uint64_t load(std::uint64_t address, unsigned width, Read&& read) const {
    const auto end = address + width;
    for (unsigned i = count; i > 0; --i) {
      const auto& store = stores[i - 1];
      const auto store_end = store.physical + store.width;
      if (store_end <= address || end <= store.physical) continue;
      if (address >= store.physical && end <= store_end)
        return (store.value >> (8 * (address - store.physical))) & mask(width);
      break;
    }
    auto result = read();
    for (unsigned i = 0; i < count; ++i) {
      const auto& store = stores[i];
      const auto first = std::max(address, store.physical);
      const auto last = std::min(end, store.physical + store.width);
      if (first >= last) continue;
      const auto bits = mask(static_cast<unsigned>(last - first));
      const auto shift = 8 * (first - address);
      const auto value = store.value >> (8 * (first - store.physical));
      result = (result & ~(bits << shift)) | ((value & bits) << shift);
    }
    return result;
  }

 private:
  static std::uint64_t mask(unsigned width) noexcept {
    return width == 8 ? UINT64_MAX : (std::uint64_t{1} << (width * 8)) - 1;
  }
};

struct Frame {
  using Read = unsigned (*)(Frame*, std::uint64_t, unsigned) noexcept;
  using Write = unsigned (*)(Frame*, std::uint64_t, unsigned, std::uint64_t) noexcept;
  std::array<std::uint64_t, 16> regs;
  std::uint64_t left = 0, right = 0, loaded = 0;
  Read read;
  Write write;
  void* cpu;
  PendingRamWrites* pending;
  unsigned loop_remaining = 1;
};

// The emitter is bounded by decoded instructions. Guest bytes are never
// mapped as host instructions. Address arithmetic uses guest registers,
// and all loads go through the original CPU's guarded RAM accessor.
class Emitter {
 public:
  explicit Emitter(const Block& block) : long_mode_(block.long_mode) {
    code_.reserve(96);
    emit(0xd503245f); // BTI c (a hint on hosts without BTI).
    emit(0xa9bf7bf3); // STP X19,X30,[SP,#-16]!
    emit(0xaa0003f3); // MOV X19,X0
    const auto loop_start = static_cast<std::int32_t>(code_.size());
    for (unsigned i = 0; i < block.count; ++i) {
      const auto& op = block.instructions[i];
      const bool capture_flags = block.flag_width && i == block.flag_index;
      if (op.writes_ram()) {
        const auto access = op.kind == Instruction::Subtract ? kSubtractAccess : 0;
        prepare_access(op.lhs, op.width, access);
        if (op.rhs.immediate) literal(3, op.rhs.immediate_value, op.width);
        else load(3, op.rhs.reg * 8, op.width);
        call(offsetof(Frame, write));
      } else if (op.kind == Instruction::Move || op.kind == Instruction::MoveByte ||
                 op.kind == Instruction::MoveZeroExtend) {
        read(op.rhs, op.width);
        const auto width = op.kind == Instruction::MoveZeroExtend || op.width >= 4 ? 8 : op.width;
        store(9, op.lhs.reg * 8, width);
      } else if (op.kind == Instruction::MoveSignExtend) {
        const auto source = op.opcode == 0x63 ? 4U : (op.opcode & 1U) ? 2U : 1U;
        read(op.rhs, source);
        const auto wide = op.width == 8 ? 0x80400000U : 0; // sf and N for SBFM X.
        emit(0x13000129U | wide | ((source * 8U - 1U) << 10)); // SXTB/SXTH/SXTW W/X9,W/X9
        store(9, op.lhs.reg * 8);
      } else if (op.kind == Instruction::Lea) {
        address(op.rhs, 9);
        if (op.width == 4) emit(0x2a0903e9); // MOV W9,W9 clears the upper half.
        store(9, op.lhs.reg * 8, op.width == 2 ? 2 : 8);
      } else if (op.kind == Instruction::Increment || op.kind == Instruction::Decrement ||
                 op.kind == Instruction::Add || op.kind == Instruction::Subtract || op.kind == Instruction::Xor ||
                 op.kind == Instruction::And || op.kind == Instruction::Or || Instruction::shift(op.kind)) {
        register_alu(op, capture_flags);
      } else if (op.kind == Instruction::Compare || op.kind == Instruction::Test) {
        // Preserve guarded reads even if later instructions overwrite these flags.
        if (capture_flags || op.lhs.memory) {
          read(op.lhs, op.width);
          if (capture_flags) store(9, offsetof(Frame, left));
        }
        if (capture_flags || op.rhs.memory) {
          read(op.rhs, op.width);
          if (capture_flags) store(9, offsetof(Frame, right));
        }
      }
    }
    const auto& last = block.instructions[block.count - 1];
    if (block.uses_arm_condition()) {
      load(9, offsetof(Frame, left), block.flag_width);
      load(10, offsetof(Frame, right), block.flag_width);
      emit(block.flag_width == 8 ? 0xeb0a013f : 0x6b0a013f); // CMP X/W9,X/W10
      emit(0x1a9f07e0U | ((last.condition ^ 1U) << 12)); // CSET W0,cc
      if (block.repeats) {
        load(11, offsetof(Frame, loop_remaining), 4);
        emit(0x5100056b); // SUB W11,W11,#1
        store(11, offsetof(Frame, loop_remaining), 4);
        emit(0x34000040); // CBZ W0,+8 (guest branch fell through)
        const auto back = loop_start - static_cast<std::int32_t>(code_.size());
        emit(0x3500000bU | ((static_cast<std::uint32_t>(back) & 0x7ffffU) << 5)); // CBNZ W11,loop_start
      }
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
  bool long_mode_;

  void register_alu(const Instruction& op, bool capture_flags) {
    read(op.lhs, op.width);
    if (capture_flags || op.rhs.memory) store(9, offsetof(Frame, left));
    const auto wide = op.width == 8 ? 0x80000000U : 0;
    const auto dest_width = op.width < 4 ? op.width : 8;
    const bool inc_dec = op.kind == Instruction::Increment || op.kind == Instruction::Decrement;
    const bool add_sub = op.kind == Instruction::Add || op.kind == Instruction::Subtract;
    const bool negative = op.rhs.immediate_value > INT64_MAX;
    const auto amount = inc_dec ? 1 : negative ? 0U - op.rhs.immediate_value : op.rhs.immediate_value;
    if (inc_dec || (add_sub && op.rhs.immediate && amount <= 4095)) {
      const bool subtract = (op.kind == Instruction::Decrement || op.kind == Instruction::Subtract) != negative;
      emit(0x11000129U | (subtract ? 0x40000000U : 0) | wide | (std::uint32_t(amount) << 10));
      store(9, op.lhs.reg * 8, dest_width);
      if (capture_flags && !inc_dec) {
        read(op.rhs, op.width);
        store(9, offsetof(Frame, right));
      }
      return;
    }
    if (!op.rhs.memory) emit(0xaa0903ea); // MOV X10,X9 (left).
    read(op.rhs, op.width);
    if (capture_flags) store(9, offsetof(Frame, right));
    if (op.rhs.memory) load(10, offsetof(Frame, left), op.width);
    std::uint32_t alu;
    switch (op.kind) {
      case Instruction::Subtract: alu = 0x4b090149U; break;
      case Instruction::And: alu = 0x0a090149U; break;
      case Instruction::Or: alu = 0x2a090149U; break;
      case Instruction::Xor: alu = 0x4a090149U; break;
      case Instruction::ShiftLeft: alu = 0x1ac92149U; break;
      case Instruction::ShiftRight: alu = 0x1ac92549U; break;
      case Instruction::ShiftArithmetic: alu = 0x1ac92949U; break;
      default: alu = 0x0b090149U; break; // ADD
    }
    emit(alu | wide); // Register operations use X/W10 and X/W9, with result in X/W9.
    store(9, op.lhs.reg * 8, dest_width);
  }

  void emit(std::uint32_t word) { code_.push_back(word); }
  void load(unsigned reg, unsigned offset, unsigned width = 8) {
    std::uint32_t load_op = 0x39400000U; // LDRB
    if (width == 8) load_op = 0xf9400000U;
    else if (width == 4) load_op = 0xb9400000U;
    else if (width == 2) load_op = 0x79400000U;
    emit(load_op | ((offset / width) << 10) | (19U << 5) | reg);
  }
  void store(unsigned reg, unsigned offset, unsigned width = 8) {
    const auto store_op = width == 1 ? 0x39000000U
        : width == 2 ? 0x79000000U : width == 4 ? 0xb9000000U : 0xf9000000U;
    emit(store_op | ((offset / width) << 10) | (19U << 5) | reg);
  }
  void address(const Operand& operand, unsigned reg) {
    if (operand.reg == Operand::kRipBase) {
      literal(reg, operand.immediate_value, 8);
      return;
    }
    if (operand.reg == Operand::kNoIndex) emit(0x52800000U | reg); // MOV Wd,#0
    else load(reg, operand.reg * 8);
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
    if (operand.index != Operand::kNoIndex) {
      load(10, operand.index * 8);
      emit(0x8b0a0000U | (operand.scale_shift << 10) | regs); // ADD Xd,Xd,X10,LSL #shift
    }
    if (!long_mode_) emit(0x2a0003e0U | (reg << 16) | reg); // MOV Wd,Wd wraps the 32-bit offset.
  }
  void prepare_access(const Operand& operand, unsigned width, unsigned access = 0) {
    emit(0xaa1303e0); // MOV X0,X19
    address(operand, 1);
    const auto spec = width | access | (operand.stack ? kStackAccess : 0);
    emit(0x52800002U | (spec << 5)); // MOV W2,#access
  }
  void call(unsigned offset) {
    load(9, offset);
    emit(0xd63f0120); // BLR X9
    failed_access_[exits_++] = code_.size();
    emit(0); // Patched to CBZ W0 on failure.
  }
  void literal(unsigned reg, std::uint64_t value, unsigned width) {
    const auto wide = width == 8 ? 0x80000000U : 0;
    if (width == 1) value = std::uint8_t(value);
    else if (width == 2) value = std::uint16_t(value);
    else if (width == 4) value = std::uint32_t(value);
    if ((value | 0xffffU) == (width == 8 ? UINT64_MAX : UINT32_MAX)) {
      emit(0x12800000U | wide | (std::uint32_t(~value & 0xffffU) << 5) | reg); // MOVN
      return;
    }
    emit(0x52800000U | wide | (std::uint32_t(value & 0xffffU) << 5) | reg); // MOVZ
    for (unsigned part = 1; part * 2 < width; ++part) {
      const auto half = std::uint32_t(value >> (part * 16)) & 0xffffU;
      if (half) emit(0x72800000U | wide | (part << 21) | (half << 5) | reg); // MOVK
    }
  }
  void read(const Operand& operand, unsigned width) {
    if (operand.immediate) {
      literal(9, operand.immediate_value, width);
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
    std::uint64_t code_version = 0;
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
      code_version = 0;
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
