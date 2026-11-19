#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

#if defined(__aarch64__) && defined(__linux__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MUNDO_X_ETHER_ARM64_BLOCKS 1
#include <sys/mman.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <new>
#include <optional>
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
  bool memory = false, immediate = false, stack = false, high_byte = false;

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
    Add, Subtract, And, Or, Xor, ShiftLeft, ShiftRight, ShiftArithmetic,
    RotateLeft, RotateRight, SignedMultiply, ConditionalMove, Conditional, Jump
  };
  Kind kind = Move;
  Operand lhs, rhs;
  std::uint64_t multiplier = 0;
  unsigned width = 0, begin = 0, end = 0, opcode = 0, condition = 0;

  static bool logical(Kind kind) noexcept {
    return kind == Test || kind == And || kind == Or || kind == Xor;
  }
  static bool shift(Kind kind) noexcept {
    return kind == ShiftLeft || kind == ShiftRight || kind == ShiftArithmetic;
  }
  static bool rotate(Kind kind) noexcept {
    return kind == RotateLeft || kind == RotateRight;
  }
  static bool sets_auxiliary(Kind kind) noexcept {
    return kind == Add || kind == Subtract || kind == Compare ||
        kind == Increment || kind == Decrement;
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
  static constexpr unsigned kNoFlagSource = kMaxInstructions;
  std::array<Instruction, kMaxInstructions> instructions{};
  unsigned count = 0, size = 0, flag_width = 0, flag_index = 0, writes = 0;
  unsigned auxiliary_index = kNoFlagSource, rotate_carry_index = kNoFlagSource;
  unsigned decrement_carry_index = kNoFlagSource;
  unsigned rotate_overflow_index = kNoFlagSource, rotates = 0;
  unsigned variable_rotates = 0;
  unsigned last_wide_rotate_offset = kMaxBytes, last_variable_rotate_offset = kMaxBytes;
  Instruction::Kind flag_op = Instruction::Move;
  std::uint64_t target = 0;
  bool long_mode = true, repeats = false, register_only = true;

  // A single interpreted instruction costs less than replacing a warm block
  // and setting up an executable mapping for the same amount of guest work.
  bool can_translate() const noexcept { return count > 1; }

  bool stages_ram() const noexcept {
    return writes && (writes > 1 || !instructions[count - 1].writes_ram());
  }

  bool uses_arm_condition() const noexcept {
    if (!count || flag_width < 4 || rotate_carry_index != kNoFlagSource) return false;
    const auto& last = instructions[count - 1];
    if (last.kind != Instruction::Conditional || last.condition >= 16) return false;
    return flag_op == Instruction::Compare || flag_op == Instruction::Subtract ||
        (flag_op == Instruction::Decrement && (last.opcode & 15) == 5);
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
  unsigned carry_boundary = 0;
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
        ((bytes[pos] & 0xfeU) == 0xb6U || (bytes[pos] & 0xfeU) == 0xbeU ||
         (bytes[pos] & 0xf0U) == 0x40U || bytes[pos] == 0xaf))
      opcode = 0x0f00U | bytes[pos++];
    const auto form = opcode & 7;
    const auto direct_alu = opcode <= 0x3d && form <= 5
        ? kGroupOne[opcode >> 3] : Instruction::Unsupported;
    const bool binary_alu = direct_alu != Instruction::Unsupported && form < 4;
    const bool zero_extend = opcode == 0x0fb6 || opcode == 0x0fb7;
    const bool sign_extend_move = opcode == 0x0fbe || opcode == 0x0fbf ||
        (long_mode && opcode == 0x63 && (rex & 8));
    const bool move_byte = opcode == 0x88 || opcode == 0x8a;
    const bool conditional_move = (opcode & 0xfff0U) == 0x0f40U;
    const bool group_immediate = opcode == 0x80 || opcode == 0x81 || opcode == 0x83;
    const bool move_immediate = opcode == 0xc6 || opcode == 0xc7;
    const bool test_immediate = opcode == 0xf6 || opcode == 0xf7;
    const bool multiply_immediate = opcode == 0x69 || opcode == 0x6b;
    const bool multiply = opcode == 0x0faf || multiply_immediate;
    const bool shift = opcode == 0xc1 || opcode == 0xd1 || opcode == 0xd3;
    const bool carry_preserved = block.rotate_carry_index == Block::kNoFlagSource &&
        (!block.flag_width || block.flag_op == Instruction::Increment || block.flag_op == Instruction::Decrement);
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
        group_immediate || move_immediate || test_immediate || shift || multiply || conditional_move) {
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
          zero_extend || sign_extend_move || opcode == 0x8a || multiply || conditional_move ||
          (binary_alu && (opcode & 2));
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
        if (op.width < 4) break;
        if (opcode == 0x0fbe && !rex && !other.memory && other.reg >= 4) {
          op.rhs.reg -= 4;
          op.rhs.high_byte = true;
        }
        op.kind = Instruction::MoveSignExtend;
      } else if (conditional_move) {
        // Memory sources must be evaluated even when the condition is false.
        if (other.memory || op.width < 4) break;
        op.kind = Instruction::ConditionalMove;
      } else if (opcode == 0x8d) {
        if (!other.memory) break;
        op.kind = Instruction::Lea;
      } else if (opcode == 0xff) {
        if (other.memory || group > 1) break;
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
      } else if (multiply) {
        // IMUL changes CF and OF while preserving the other arithmetic flags.
        if (op.width < 4 || block.flag_width) break;
        op.kind = Instruction::SignedMultiply;
        if (multiply_immediate) {
          const unsigned literal_width = opcode == 0x6b ? 1 : 4;
          if (literal_width > size - pos) break;
          op.multiplier = immediate(literal_width, true).immediate_value;
        }
      } else if (shift) {
        if (other.memory || op.width < 4) break;
        if (opcode == 0xc1 && pos == size) break;
        if (group <= 1) {
          // A runtime zero count inherits CF and OF. Keep any earlier
          // outstanding rotate flags in the preceding block.
          if (opcode == 0xd3 && block.rotate_carry_index != Block::kNoFlagSource) break;
          op.kind = group == 0 ? Instruction::RotateLeft : Instruction::RotateRight;
          if (opcode == 0xd3) op.rhs = Operand{1}; // CL
          else {
            const auto count = opcode == 0xd1 ? 1U : unsigned(bytes[pos++]);
            op.rhs = Operand::literal(count & (op.width * 8 - 1));
          }
        } else {
          // Shifts can keep incoming OF, and variable zero counts can keep all flags.
          if (group < 4 || block.flag_width || block.rotate_carry_index != Block::kNoFlagSource) break;
          op.kind = group == 5 ? Instruction::ShiftRight
              : group == 7 ? Instruction::ShiftArithmetic : Instruction::ShiftLeft;
          op.rhs = opcode == 0xd3 ? Operand{1} : Operand::literal(opcode == 0xd1 ? 1 : bytes[pos++]);
        }
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
    // A guarded memory SUB uses the primary flag slots in its write callback.
    // Commit it before a subsequent logical operation that inherits its AF.
    if (Instruction::logical(op.kind) && block.auxiliary_index != Block::kNoFlagSource &&
        block.instructions[block.auxiliary_index].writes_ram()) break;
    const bool inc_dec = op.kind == Instruction::Increment || op.kind == Instruction::Decrement;
    if (op.kind == Instruction::Compare || Instruction::logical(op.kind) || Instruction::shift(op.kind) || inc_dec ||
        op.kind == Instruction::Add || op.kind == Instruction::Subtract ||
        op.kind == Instruction::SignedMultiply) {
      // INC/DEC can stay with an earlier carry producer if a later
      // operation in this block replaces the carry before it is observed.
      if (!inc_dec) carry_boundary = 0;
      else if (!carry_preserved) {
        // DEC sets ZF for JNZ but preserves the body's carry. Capture that
        // carry in the final native round so the caller can observe it on exit.
        const bool jnz = (size - pos >= 2 && bytes[pos] == 0x75) ||
            (size - pos >= 6 && bytes[pos] == 0x0f && bytes[pos + 1] == 0x85);
        const bool from_rotate = block.rotate_carry_index != Block::kNoFlagSource;
        const bool can_capture = from_rotate ? block.instructions[block.rotate_carry_index].rhs.immediate
            : Instruction::logical(block.flag_op) || block.flag_op == Instruction::Add ||
              block.flag_op == Instruction::Subtract || block.flag_op == Instruction::Compare;
        if (op.kind == Instruction::Decrement && op.width >= 4 && jnz &&
            block.register_only && !carry_boundary && can_capture) {
          block.decrement_carry_index = from_rotate ? block.rotate_carry_index : block.flag_index;
        } else if (!long_mode) {
          break;
        } else if (!carry_boundary) {
          carry_boundary = begin;
        }
      }
      block.flag_width = op.width;
      block.flag_op = op.kind;
      block.flag_index = block.count;
      block.rotate_carry_index = block.rotate_overflow_index = Block::kNoFlagSource;
      if (Instruction::sets_auxiliary(op.kind)) block.auxiliary_index = block.count;
    } else if (Instruction::rotate(op.kind)) {
      ++block.rotates;
      const auto count = op.rhs.immediate_value;
      if (!op.rhs.immediate) {
        ++block.variable_rotates;
        block.rotate_carry_index = block.rotate_overflow_index = block.count;
        if (op.width == 8) block.last_variable_rotate_offset = op.begin;
      } else {
        if (count) block.rotate_carry_index = block.count;
        if (count && op.width == 8) block.last_wide_rotate_offset = op.begin;
        // Match the interpreter's retained OF for counts greater than one.
        if (count == 1) block.rotate_overflow_index = block.count;
      }
    }
    if (op.lhs.memory && op.lhs.reg == Operand::kRipBase)
      op.lhs.immediate_value = rip + pos + signed_offset(op.lhs.displacement);
    if (op.rhs.memory && op.rhs.reg == Operand::kRipBase)
      op.rhs.immediate_value = rip + pos + signed_offset(op.rhs.displacement);
    op.end = pos;
    block.instructions[block.count++] = op;
    block.register_only = block.register_only && !op.lhs.memory &&
        (!op.rhs.memory || op.kind == Instruction::Lea);
    block.size = pos;
    if (op.writes_ram() && (++block.writes == Block::kMaxWrites || !long_mode)) break;
    if (op.kind == Instruction::Conditional || op.kind == Instruction::Jump ||
        op.kind == Instruction::SignedMultiply || op.kind == Instruction::ConditionalMove) break;
  }
  if (carry_boundary) return decode(bytes, carry_boundary, rip, long_mode);
  // A read-only round can validate a new address with the guarded loader.
  // Repeated stores still require fixed addresses for the pending writes.
  // Legacy execution can repeat only when no segment or memory is accessed.
  block.repeats = (long_mode || block.register_only) && block.target == rip && block.uses_arm_condition() &&
      (!block.writes || fixed_loop_addresses(block));
  return block;
}

// An affine register transition F has linear differences modulo its register width.
// If its third difference is zero for the current state, every subsequent
// difference of that order is zero too. Run the final round normally so its
// branch and all x86 flags can be retired by either execution path.
struct RegisterStateVector {
  static constexpr unsigned kMinRounds = 32;
  static constexpr unsigned kRegisters = 16;
  using Registers = std::array<std::uint64_t, kRegisters>;

  unsigned counter = kRegisters;
  unsigned written_registers = 0;
  unsigned rotate_left = 0, rotate_right = 0;
  unsigned compare_register = kRegisters;

  static RegisterStateVector build(const Block& block) noexcept {
    if (!block.repeats || !block.register_only || block.count < 2) return {};
    const auto& branch = block.instructions[block.count - 1];
    const auto& last = block.instructions[block.count - 2];
    if (block.long_mode && block.count == 4 && branch.kind == Instruction::Conditional &&
        (branch.opcode & 15) == 15 && block.flag_index == 2) { // x86 JG
      const auto& difference = block.instructions[0];
      const auto& increment = block.instructions[1];
      if (difference.kind == Instruction::Subtract && difference.width == 8 &&
          !difference.rhs.immediate && difference.lhs.reg < kRegisters &&
          difference.rhs.reg < kRegisters && difference.lhs.reg != difference.rhs.reg &&
          increment.kind == Instruction::Increment && increment.width == 8 &&
          increment.lhs.reg == difference.rhs.reg &&
          last.kind == Instruction::Compare && last.width == 8 && !last.rhs.immediate &&
          last.lhs.reg == difference.lhs.reg && last.rhs.reg == difference.rhs.reg)
        return {difference.rhs.reg, 0, 0, 0, difference.lhs.reg};
      return {};
    }
    const bool countdown = last.kind == Instruction::Decrement || (last.kind == Instruction::Subtract &&
        last.rhs.immediate && last.rhs.immediate_value == 1);
    if (branch.kind != Instruction::Conditional || (branch.opcode & 15) != 5 || // x86 JNZ
        block.flag_index != block.count - 2 || !countdown ||
        (last.width != 4 && (!block.long_mode || last.width != 8)) ||
        last.lhs.reg >= kRegisters)
      return {};

    const unsigned body_width = block.long_mode ? 8 : 4;
    unsigned written = 0, read = 0, left = 0, right = 0;
    for (unsigned i = 0; i + 2 < block.count; ++i) {
      const auto& op = block.instructions[i];
      if (op.kind == Instruction::Nop) continue;
      if (op.width != body_width || op.lhs.reg >= kRegisters || op.lhs.reg == last.lhs.reg ||
          op.rhs.memory) return {};
      const auto destination = 1U << op.lhs.reg;
      if (Instruction::rotate(op.kind)) {
        // CL may change only through the loop's final DEC-equivalent SUB.
        constexpr unsigned rcx = 1;
        if (!block.long_mode || last.lhs.reg != rcx || op.rhs.immediate ||
            op.rhs.reg != rcx || ((left | right) & destination)) return {};
        (op.kind == Instruction::RotateLeft ? left : right) |= destination;
        continue;
      }
      written |= destination;
      if (op.kind == Instruction::Increment || op.kind == Instruction::Decrement) continue;
      if (op.kind != Instruction::Move && op.kind != Instruction::Add &&
          op.kind != Instruction::Subtract) return {};
      if (!op.rhs.immediate) {
        if (op.rhs.reg >= kRegisters || (op.rhs.reg == last.lhs.reg && op.width != last.width)) return {};
        read |= 1U << op.rhs.reg;
      }
    }
    // A rotated value must not participate in the affine register recurrence.
    if ((left | right) & (read | written)) return {};
    return {last.lhs.reg, written, left, right};
  }

  unsigned project_tail(const Block& block, const std::uint64_t* initial,
                        unsigned limit, Registers& tail,
                        unsigned minimum_rounds = kMinRounds) const noexcept {
    if (counter == kRegisters || limit < minimum_rounds) return 0;
    const bool narrow = block.instructions[block.count - 2].width == 4;
    const auto before = narrow ? std::uint64_t(std::uint32_t(initial[counter])) : initial[counter];
    // A zero counter wraps; its next zero is beyond an unsigned run budget.
    const auto rounds = before && before < limit ? unsigned(before) : limit;
    if (rounds < minimum_rounds) return 0;

    Registers once;
    std::copy_n(initial, kRegisters, once.begin());
    body(block, once);
    auto twice = once;
    body(block, twice);
    auto thrice = twice;
    body(block, thrice);

    const auto skipped = std::uint64_t(rounds - 1);
    const auto pairs = skipped * (skipped - 1) / 2;
    const std::uint64_t mask = block.long_mode ? UINT64_MAX : UINT32_MAX;
    for (unsigned i = 0; i < kRegisters; ++i) {
      if (!(written_registers & (1U << i))) {
        tail[i] = initial[i];
        continue;
      }
      const auto first = once[i] - initial[i];
      const auto second = twice[i] - once[i];
      const auto delta = second - first;
      if (((thrice[i] - twice[i] - second - delta) & mask) != 0) return 0;
      tail[i] = (initial[i] + skipped * first + pairs * delta) & mask;
    }
    if (rotate_left || rotate_right) {
      // The skipped CL counts form a descending sequence. Reducing the sum
      // modulo 64 also handles wrap in ECX/RCX and masking CL to six bits.
      const auto amount = unsigned((skipped * before - pairs) & 63U);
      if (amount) {
        for (unsigned i = 0; i < kRegisters; ++i) {
          const auto bit = 1U << i;
          const auto value = initial[i];
          if (rotate_left & bit) tail[i] = (value << amount) | (value >> (64 - amount));
          if (rotate_right & bit) tail[i] = (value >> amount) | (value << (64 - amount));
        }
      }
    }
    tail[counter] = before - skipped;
    if (narrow) tail[counter] = std::uint32_t(tail[counter]);
    return rounds;
  }

  unsigned project_compare(const std::uint64_t* initial, unsigned limit,
                           std::uint64_t& lhs, std::uint64_t& rhs) const noexcept {
    // This four-instruction loop can project within the 32-step SMP quantum.
    constexpr unsigned min_rounds = 4;
    if (limit < min_rounds) return 0;
    const auto value = initial[compare_register], start = initial[counter];
    if (value > INT64_MAX || start > INT64_MAX) return 0;
    limit = unsigned(std::min<std::uint64_t>(limit, INT64_MAX - start));
    if (limit < min_rounds) return 0;

    // After n rounds, JG is taken iff value > (n+1)*start + n*(n+1)/2.
    // Compare before multiplying by start so even large signed values fit.
    const auto continues = [&](std::uint64_t n) {
      const auto sum = n * (n + 1) / 2;
      return sum < value && start <= (value - sum - 1) / (n + 1);
    };
    unsigned rounds = limit;
    if (!continues(limit)) {
      if (!continues(min_rounds - 1)) return 0;
      unsigned first = min_rounds;
      while (first < rounds) {
        const auto middle = first + (rounds - first) / 2;
        if (continues(middle)) first = middle + 1;
        else rounds = middle;
      }
    }
    const auto n = std::uint64_t(rounds);
    // The previous comparison was true. The final subtraction cannot
    // underflow, so both operands of the final signed CMP are nonnegative.
    lhs = value - n * start - n * (n - 1) / 2;
    rhs = start + n;
    return rounds;
  }

 private:
  static void body(const Block& block, Registers& registers) noexcept {
    for (unsigned i = 0; i + 2 < block.count; ++i) {
      const auto& op = block.instructions[i];
      if (op.kind == Instruction::Nop || Instruction::rotate(op.kind)) continue;
      auto& left = registers[op.lhs.reg];
      if (op.kind == Instruction::Increment) {
        ++left;
        continue;
      }
      if (op.kind == Instruction::Decrement) {
        --left;
        continue;
      }
      const auto right = op.rhs.immediate ? op.rhs.immediate_value : registers[op.rhs.reg];
      switch (op.kind) {
        case Instruction::Move: left = right; break;
        case Instruction::Add: left += right; break;
        case Instruction::Subtract: left -= right; break;
        default: break;
      }
    }
    const auto& countdown = block.instructions[block.count - 2];
    auto& count = registers[countdown.lhs.reg];
    --count;
    if (countdown.width == 4) count = std::uint32_t(count);
  }
};

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

inline constexpr unsigned kResidentReadPageBytes = 4096;

struct ResidentReadPage {
  const std::uint8_t* bytes = nullptr;
  std::uint64_t linear = 0;
};

struct ResidentReadPages {
  ResidentReadPage recent, previous;
};

struct Frame {
  using Read = unsigned (*)(Frame*, std::uint64_t, unsigned) noexcept;
  using Write = unsigned (*)(Frame*, std::uint64_t, unsigned, std::uint64_t) noexcept;
  std::array<std::uint64_t, 16> regs;
  std::uint64_t left = 0, right = 0, loaded = 0;
  std::uint64_t carry_left, carry_right;
  std::uint64_t auxiliary_left, auxiliary_right, rotate_carry_result, rotate_overflow_result;
  std::uint64_t conditional_source;
  Read read;
  Write write;
  void* cpu;
  PendingRamWrites* pending;
  unsigned loop_remaining = 1;
  unsigned rotate_count;
  std::uint64_t* guest_regs;
  ResidentReadPages resident;
  unsigned resident_read_hits = 0, ram_read_callbacks = 0;
  unsigned state_vector_rounds = 0;
  bool reuse_resident_reads = false;
};

// The emitter is bounded by decoded instructions. Guest bytes are never
// mapped as host instructions. Only the guarded RAM accessor can supply
// a resident page, and generated loads check the address stays inside it.
class Emitter {
 public:
  explicit Emitter(const Block& block)
      : long_mode_(block.long_mode), register_only_(block.register_only),
        inline_ram_reads_(block.long_mode && !block.writes && !block.register_only) {
    code_.reserve(96);
    emit(0xd503245f); // BTI c (a hint on hosts without BTI).
    if (register_only_) {
      // Leaf blocks never call a memory helper. X12 holds the frame and X13
      // holds the guest register file without saving any host registers.
      emit(0xaa0003ec); // MOV X12,X0
      load(13, offsetof(Frame, guest_regs));
    } else {
      emit(0xa9bf7bf3); // STP X19,X30,[SP,#-16]!
      emit(0xaa0003f3); // MOV X19,X0
    }
    const auto loop_start = static_cast<std::int32_t>(code_.size());
    for (unsigned i = 0; i < block.count; ++i) {
      const auto& op = block.instructions[i];
      const bool capture_flags = block.flag_width && i == block.flag_index;
      const unsigned flag_slot = capture_flags ? offsetof(Frame, left)
          : i == block.auxiliary_index ? offsetof(Frame, auxiliary_left)
          : i == block.decrement_carry_index && !Instruction::logical(op.kind) ? offsetof(Frame, carry_left) : 0;
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
      } else if (op.kind == Instruction::ConditionalMove) {
        // The condition is evaluated after the x86 flags have been committed.
        read(op.rhs, op.width);
        store(9, offsetof(Frame, conditional_source));
      } else if (op.kind == Instruction::SignedMultiply) {
        // The last operation commits through the CPU's exact signed
        // multiplication, including its partial flag update.
        const bool immediate = op.opcode != 0x0faf;
        read(immediate ? op.rhs : op.lhs, op.width);
        store(9, offsetof(Frame, left));
        if (immediate) literal(9, op.multiplier, op.width);
        else read(op.rhs, op.width);
        store(9, offsetof(Frame, right));
      } else if (Instruction::rotate(op.kind)) {
        read(op.lhs, op.width);
        const auto count = unsigned(op.rhs.immediate_value);
        if (!op.rhs.immediate) {
          load(10, op.rhs.reg * 8, 4);
          if (i == block.rotate_carry_index || i == block.rotate_overflow_index)
            store(10, offsetof(Frame, rotate_count), 4);
          if (op.kind == Instruction::RotateLeft) emit(0x4b0a03eaU); // NEG W10,W10
          const auto rorv = op.width == 8 ? 0x9ac02c00U : 0x1ac02c00U;
          emit(rorv | (10U << 16) | (9U << 5) | 9U); // RORV X/W9,X/W9,X/W10
        } else if (count) {
          const auto right = op.kind == Instruction::RotateLeft ? op.width * 8 - count : count;
          const auto extr = op.width == 8 ? 0x93c00000U : 0x13800000U;
          emit(extr | (9U << 16) | (right << 10) | (9U << 5) | 9U); // ROR X/W9,X/W9,#right
        }
        store(9, op.lhs.reg * 8);
        if (i == block.decrement_carry_index) store(9, offsetof(Frame, carry_left));
        else if (i == block.rotate_carry_index) store(9, offsetof(Frame, rotate_carry_result));
        else if (i == block.rotate_overflow_index) store(9, offsetof(Frame, rotate_overflow_result));
      } else if (op.kind == Instruction::Increment || op.kind == Instruction::Decrement ||
                 op.kind == Instruction::Add || op.kind == Instruction::Subtract || op.kind == Instruction::Xor ||
                 op.kind == Instruction::And || op.kind == Instruction::Or || Instruction::shift(op.kind)) {
        register_alu(op, flag_slot);
      } else if (op.kind == Instruction::Compare || op.kind == Instruction::Test) {
        // Preserve guarded reads even if later instructions overwrite these flags.
        if (flag_slot || op.lhs.memory) {
          read(op.lhs, op.width);
          if (flag_slot) store(9, flag_slot);
        }
        if (flag_slot || op.rhs.memory) {
          read(op.rhs, op.width);
          if (flag_slot) store(9, flag_slot + sizeof(std::uint64_t));
        }
      }
    }
    const auto& last = block.instructions[block.count - 1];
    if (block.uses_arm_condition()) {
      load(9, offsetof(Frame, left), block.flag_width);
      if (block.flag_op == Instruction::Decrement) {
        emit(block.flag_width == 8 ? 0xf100053f : 0x7100053f); // CMP X/W9,#1 before DEC.
      } else {
        load(10, offsetof(Frame, right), block.flag_width);
        emit(block.flag_width == 8 ? 0xeb0a013f : 0x6b0a013f); // CMP X/W9,X/W10
      }
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
    if (!register_only_) emit(0xa8c17bf3); // LDP X19,X30,[SP],#16
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
  bool long_mode_, register_only_, inline_ram_reads_;

  void register_alu(const Instruction& op, unsigned flag_slot) {
    read(op.lhs, op.width);
    const unsigned left_slot = flag_slot ? flag_slot : offsetof(Frame, left);
    if (flag_slot || op.rhs.memory) store(9, left_slot);
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
      if (flag_slot && !inc_dec) {
        read(op.rhs, op.width);
        store(9, flag_slot + sizeof(std::uint64_t));
      }
      return;
    }
    if (!op.rhs.memory) emit(0xaa0903ea); // MOV X10,X9 (left).
    read(op.rhs, op.width);
    if (flag_slot) store(9, flag_slot + sizeof(std::uint64_t));
    if (op.rhs.memory) load(10, left_slot, op.width);
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
  unsigned base_register(unsigned offset) const noexcept {
    if (!register_only_) return 19;
    return offset < offsetof(Frame, left) ? 13 : 12;
  }
  void load(unsigned reg, unsigned offset, unsigned width = 8) {
    std::uint32_t load_op = 0x39400000U; // LDRB
    if (width == 8) load_op = 0xf9400000U;
    else if (width == 4) load_op = 0xb9400000U;
    else if (width == 2) load_op = 0x79400000U;
    emit(load_op | ((offset / width) << 10) | (base_register(offset) << 5) | reg);
  }
  void store(unsigned reg, unsigned offset, unsigned width = 8) {
    const auto store_op = width == 1 ? 0x39000000U
        : width == 2 ? 0x79000000U : width == 4 ? 0xb9000000U : 0xf9000000U;
    emit(store_op | ((offset / width) << 10) | (base_register(offset) << 5) | reg);
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
    address(operand, 1);
    prepare_callback(width | access | (operand.stack ? kStackAccess : 0));
  }
  void prepare_callback(unsigned spec) {
    emit(0xaa1303e0); // MOV X0,X19
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
      load(9, operand.reg * 8 + unsigned(operand.high_byte), width);
      return;
    }
    address(operand, 1);
    if (inline_ram_reads_) {
      read_resident_or_callback(width, operand.stack);
      return;
    }
    prepare_callback(width | (operand.stack ? kStackAccess : 0));
    call(offsetof(Frame, read));
    load(9, offsetof(Frame, loaded), width);
  }

  void read_resident_or_callback(unsigned width, bool stack) {
    const auto guard_page = [&](unsigned slot) {
      const auto page = offsetof(Frame, resident) + slot;
      load(9, page + offsetof(ResidentReadPage, bytes));
      const auto no_page = code_.size();
      emit(0); // CBZ X9,miss
      load(10, page + offsetof(ResidentReadPage, linear));
      emit(0xcb0a002b); // SUB X11,X1,X10
      emit(0xf100017fU | ((kResidentReadPageBytes - width) << 10)); // CMP X11,#last
      const auto outside = code_.size();
      emit(0); // B.HI miss
      return std::array{no_page, outside};
    };
    const auto set_miss_target = [&](const auto& checks, std::size_t target) {
      code_[checks[0]] = 0xb4000009U | (std::uint32_t(target - checks[0]) << 5);
      code_[checks[1]] = 0x54000008U | (std::uint32_t(target - checks[1]) << 5);
    };
    const auto recent_checks = guard_page(offsetof(ResidentReadPages, recent));
    const auto direct = code_.size();
    const auto load_op = width == 1 ? 0x38606800U : width == 2 ? 0x78606800U :
                         width == 4 ? 0xb8606800U : 0xf8606800U;
    emit(load_op | (11U << 16) | (9U << 5) | 9U); // LDR{B/H/W/X} 9,[X9,X11]
    constexpr auto hits = offsetof(Frame, resident_read_hits);
    load(10, hits, 4);
    emit(0x1100054a); // ADD W10,W10,#1
    store(10, hits, 4);
    const auto done = code_.size();
    emit(0); // B done
    set_miss_target(recent_checks, code_.size());
    const auto previous_checks = guard_page(offsetof(ResidentReadPages, previous));
    emit(0x14000000U | (std::uint32_t(direct - code_.size()) & 0x03ffffffU)); // B direct
    set_miss_target(previous_checks, code_.size());
    prepare_callback(width | (stack ? kStackAccess : 0));
    call(offsetof(Frame, read));
    load(9, offsetof(Frame, loaded), width);
    code_[done] = 0x14000000U | std::uint32_t(code_.size() - done);
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
    std::swap(capacity_, other.capacity_);
    return *this;
  }
  ~NativeCode() { clear(); }

  bool build(const Block& block) {
    const Emitter emitter(block);
    const auto& words = emitter.words();
    const auto bytes = words.size() * sizeof(words.front());
    static const auto page_size = static_cast<std::size_t>(getpagesize());
    const auto capacity = (bytes + page_size - 1) / page_size * page_size;
    // Cache replacement owns this slot exclusively. Keep its existing
    // allocation when it fits, with separate writable and executable phases.
    if (mapping_ && capacity <= capacity_ &&
        mprotect(mapping_, capacity_, PROT_READ | PROT_WRITE) == 0) {
      if (publish(mapping_, capacity_, words.data(), bytes)) return true;
      clear();
    }
    void* mapping = mmap(nullptr, capacity, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) return false;
    if (!publish(mapping, capacity, words.data(), bytes)) {
      munmap(mapping, capacity);
      return false;
    }
    clear();
    mapping_ = mapping;
    capacity_ = capacity;
    return true;
  }
  explicit operator bool() const noexcept { return mapping_ != nullptr; }
  unsigned run(Frame& frame) const { return reinterpret_cast<Run>(mapping_)(&frame); }
  void clear() noexcept {
    if (mapping_) munmap(mapping_, capacity_);
    mapping_ = nullptr;
    capacity_ = 0;
  }

 private:
  static bool publish(void* mapping, std::size_t capacity,
                      const std::uint32_t* code, std::size_t bytes) noexcept {
    std::memcpy(mapping, code, bytes);
    auto* start = static_cast<char*>(mapping);
    __builtin___clear_cache(start, start + bytes);
    return mprotect(mapping, capacity, PROT_READ | PROT_EXEC) == 0;
  }

  void* mapping_ = nullptr;
  std::size_t capacity_ = 0;
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
    RegisterStateVector state_vector;

    unsigned run_state_vector(Frame& frame, unsigned limit) const noexcept {
      auto* const registers = frame.guest_regs;
      if (state_vector.compare_register != RegisterStateVector::kRegisters) {
        const auto rounds = state_vector.project_compare(registers, limit, frame.left, frame.right);
        if (!rounds) return 0;
        // Use the final CMP operands in normal x86 retirement. This block
        // updates only these two registers and the flags overwritten by CMP.
        registers[state_vector.compare_register] = frame.left;
        registers[state_vector.counter] = frame.right;
        frame.loop_remaining = limit - rounds;
        frame.state_vector_rounds += rounds - 1;
        return frame.left > frame.right ? 2U : 1U;
      }

      RegisterStateVector::Registers tail;
      const auto rounds = state_vector.project_tail(block, registers, limit, tail);
      if (!rounds) return 0;

      const auto previous_remaining = frame.loop_remaining;
      frame.guest_regs = tail.data();
      frame.loop_remaining = 1;
      const auto outcome = code.run(frame);
      frame.guest_regs = registers;
      const auto expected = tail[state_vector.counter] ? 2U : 1U;
      if (outcome != expected || frame.loop_remaining) {
        frame.loop_remaining = previous_remaining;
        return 0;
      }
      std::copy(tail.begin(), tail.end(), registers);
      frame.loop_remaining = limit - rounds;
      frame.state_vector_rounds += rounds - 1;
      return outcome;
    }

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
  static constexpr std::uint64_t kLogicalBytes = std::uint64_t{1} << 48;
  static constexpr std::size_t kSets = 256;
  static constexpr std::size_t kWays = 4;
  struct Set {
    std::array<Entry, kWays> entries;
    std::array<Entry, kWays> probes;
    std::uint64_t clock = 0;
    std::size_t next_probe = 0;
  };
  struct Lookup { Entry& entry; bool pending; std::uint64_t reuse_distance; };
  explicit Cache(std::uint64_t logical_bytes) noexcept : logical_bytes_(logical_bytes) {}
  bool host_rejected = false;

  std::uint64_t logical_bytes() const noexcept { return logical_bytes_; }
  unsigned allocated_sets() const noexcept {
    return static_cast<unsigned>(std::count_if(sets_.begin(), sets_.end(),
        [](const auto& set) { return set != nullptr; }));
  }
  const Set* find_set(std::uint64_t rip) const noexcept { return sets_[set_index(rip)].get(); }

  std::optional<Lookup> lookup(std::uint64_t rip) noexcept {
    auto& storage = sets_[set_index(rip)];
    if (!storage) {
      storage.reset(new (std::nothrow) Set{});
      if (!storage) return std::nullopt;
    }
    auto& set = *storage;
    const auto now = ++set.clock;
    for (auto& item : set.entries) {
      if (item.occupied && item.rip == rip) {
        item.last_used = now;
        return Lookup{item, false, 0};
      }
    }
    for (auto& item : set.probes) {
      if (item.occupied && item.rip == rip) {
        const auto distance = now - item.last_used;
        item.last_used = now;
        return Lookup{item, true, distance};
      }
    }
    auto& probe = set.probes[set.next_probe];
    set.next_probe = (set.next_probe + 1) % kWays;
    probe.invalidate(rip);
    probe.last_used = now;
    return Lookup{probe, true, 0};
  }

  Entry* admission_slot(const Lookup& lookup) noexcept {
    auto& set = *sets_[set_index(lookup.entry.rip)];
    auto* victim = &set.entries.front();
    for (auto& item : set.entries) {
      if (!item.code) return &item;
      if (item.last_used < victim->last_used) victim = &item;
    }
    // Admit a colliding block only when its observed reuse is at least as
    // frequent as a resident block's. As the old block ages, the new one can enter.
    return lookup.reuse_distance <= set.clock - victim->last_used ? victim : nullptr;
  }

  void admit(Entry& probe, Entry& slot) noexcept {
    auto& set = *sets_[set_index(probe.rip)];
    slot = std::move(probe);
    slot.last_used = set.clock;
    probe.clear();
  }
  void clear() noexcept {
    for (auto& set : sets_) set.reset();
  }

 private:
  std::size_t set_index(std::uint64_t rip) const noexcept {
    // Remove the sign extension of a canonical x86-64 linear address.
    const auto offset = rip & (logical_bytes_ - 1);
    return (offset ^ (offset >> 5) ^ (offset >> 12)) & (kSets - 1);
  }

  std::array<std::unique_ptr<Set>, kSets> sets_{};
  const std::uint64_t logical_bytes_;
};
#endif

}  // namespace cosmic_pc::ether_arm64
