#include "../x86_cpu.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <string>
#include <vector>

using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

namespace {
std::uint64_t checks = 0;
void check(bool value, const std::string& message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
void load(Cpu& cpu, std::initializer_list<std::uint8_t> code, std::uint32_t ip = 0x1000) {
  cpu.reset(0, ip); cpu.set_reg16(Cpu::ESP, 0xf000);
  std::size_t at = ip;
  for (const auto value : code) cpu.memory.at(at++) = value;
}
void one(Cpu& cpu) {
  const auto result = cpu.step();
  check(result != StepResult::Faulted, cpu.fault ? cpu.fault->describe() : "step fault");
}
void run(Cpu& cpu, unsigned limit = 1000) {
  for (unsigned n = 0; n < limit; ++n) {
    const auto result = cpu.step();
    if (result == StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
    if (result == StepResult::Halted) return;
  }
  throw std::runtime_error("guest exceeded test budget");
}

void arithmetic_exhaustive() {
  Cpu cpu;
  const std::uint8_t operations[] = {0x04, 0x14, 0x2c, 0x1c, 0x3c};
  // Independent integer range/nibble/parity oracles; every 8-bit input pair,
  // both incoming carry values, ADD/ADC/SUB/SBB/CMP: 655,360 executions.
  for (const auto op : operations) {
    load(cpu, {op, 0});
    for (unsigned carry = 0; carry < 2; ++carry) {
      for (unsigned a = 0; a < 256; ++a) for (unsigned b = 0; b < 256; ++b) {
        cpu.eip = 0x1000;
        cpu.regs[Cpu::EAX] = 0x76543200U | a;
        cpu.eflags = 2 | Cpu::IF | Cpu::DF | (carry ? Cpu::CF : 0);
        cpu.memory[0x1001] = std::uint8_t(b);
        const auto result = cpu.step();
        if (result == StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
        const bool sub = op == 0x2c || op == 0x1c || op == 0x3c;
        const unsigned c = (op == 0x14 || op == 0x1c) ? carry : 0;
        const int full = sub ? int(a) - int(b) - int(c) : int(a) + int(b) + int(c);
        const unsigned byte = unsigned(full) & 255;
        const int sa = a < 128 ? int(a) : int(a) - 256;
        const int sb = b < 128 ? int(b) : int(b) - 256;
        const int signed_full = sub ? sa - sb - int(c) : sa + sb + int(c);
        unsigned ones = 0;
        for (unsigned bit = 0; bit < 8; ++bit) if (byte & (1U << bit)) ++ones;
        check(cpu.reg8(0) == (op == 0x3c ? a : byte), "8-bit ALU value");
        check((cpu.regs[Cpu::EAX] & 0xffffff00U) == 0x76543200U, "AL must preserve upper EAX");
        check(cpu.flag(Cpu::CF) == (sub ? full < 0 : full > 255), "8-bit ALU carry/borrow");
        check(cpu.flag(Cpu::OF) == (signed_full < -128 || signed_full > 127), "8-bit ALU signed overflow");
        check(cpu.flag(Cpu::AF) == (sub ? (a & 15) < (b & 15) + c : (a & 15) + (b & 15) + c > 15), "8-bit ALU auxiliary carry");
        check(cpu.flag(Cpu::ZF) == (byte == 0), "8-bit ALU zero");
        check(cpu.flag(Cpu::SF) == (byte >= 128), "8-bit ALU sign");
        check(cpu.flag(Cpu::PF) == (ones % 2 == 0), "8-bit ALU parity");
        check(cpu.flag(Cpu::IF) && cpu.flag(Cpu::DF), "ALU preserves non-arithmetic flags");
      }
    }
  }
}

void register_and_decode_tests() {
  Cpu cpu;
  load(cpu, {0x66, 0x66, 0xb8, 0x78, 0x56, 0x34, 0x12, 0xb4, 0xaa, 0xf4});
  run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x1234aa78U, "operand prefixes are presence, not XOR toggles; AH mapping");
  load(cpu, {0xb8, 0xff, 0x7f, 0x40, 0xf4});
  cpu.set_flag(Cpu::CF, true); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x8000 && cpu.flag(Cpu::OF) && cpu.flag(Cpu::CF), "INC preserves CF and computes OF");
  load(cpu, {0x66, 0xb8, 0xff, 0xff, 0xff, 0x7f, 0x66, 0x83, 0xc0, 0x01, 0xf4});
  run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x80000000U && cpu.flag(Cpu::OF) && !cpu.flag(Cpu::CF), "32-bit ALU boundary");
  load(cpu, {0xb0, 0x80, 0x98, 0x99, 0xf4}); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 0xff80 && cpu.reg16(Cpu::EDX) == 0xffff, "CBW CWD signed extension");
  load(cpu, {0x66, 0x98, 0x66, 0x99, 0xf4}); cpu.set_reg16(Cpu::EAX, 0x8000); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0xffff8000U && cpu.regs[Cpu::EDX] == 0xffffffffU, "CWDE CDQ signed extension");
  load(cpu, {0x0f, 0xb6, 0xc4, 0xf4}); cpu.set_reg16(Cpu::EAX, 0xabcd); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x00ab, "MOVZX AH to AX");
  load(cpu, {0x66, 0x0f, 0xbe, 0xc3, 0xf4}); cpu.set_reg8(Cpu::EBX, 0x80); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0xffffff80U, "MOVSX BL to EAX");
}

void memory_address_tests() {
  Cpu cpu;
  load(cpu, {0x8b, 0x42, 0xfe, 0xf4});
  cpu.segments[Cpu::SS] = 0x3000; cpu.segments[Cpu::DS] = 0x4000;
  cpu.set_reg16(Cpu::EBP, 0x10); cpu.set_reg16(Cpu::ESI, 6);
  cpu.write16(0x30014, 0xcafe); cpu.write16(0x40014, 0xdead); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 0xcafe, "ModRM16 BP+SI defaults SS, signed disp8");
  load(cpu, {0x67, 0x66, 0x8b, 0x44, 0x8b, 0x08, 0xf4});
  cpu.segments[Cpu::DS] = 0x1000; cpu.regs[Cpu::EBX] = 0x100; cpu.regs[Cpu::ECX] = 3;
  cpu.write32(0x10114, 0x76543210U); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x76543210U, "ModRM32 SIB scale/index/base and operand32");
  load(cpu, {0x26, 0x8a, 0x04, 0xf4});
  cpu.segments[Cpu::ES] = 0x2000; cpu.set_reg16(Cpu::ESI, 0x80); cpu.write8(0x20080, 0x5a); run(cpu);
  check(cpu.reg8(Cpu::EAX) == 0x5a, "segment override affects data operand");
  load(cpu, {0x8d, 0x02, 0xf4});
  cpu.set_reg16(Cpu::EBP, 0xfffe); cpu.set_reg16(Cpu::ESI, 5); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 3, "LEA16 wraps effective address");
  load(cpu, {0x67, 0x8f, 0x04, 0x24, 0xf4});
  cpu.regs[Cpu::ESP] = 0xf000; cpu.write16(0xf000, 0x2468); run(cpu);
  check(cpu.reg16(Cpu::ESP) == 0xf002 && cpu.read16(0xf002) == 0x2468, "POP [ESP] computes destination after SP increment");
  load(cpu, {0xc4, 0x06, 0x00, 0x02, 0xf4});
  cpu.write16(0x200, 0x3456); cpu.write16(0x202, 0x789a); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x3456 && cpu.segments[Cpu::ES] == 0x789a, "LES far pointer load");
  cpu.memory[0] = 0x12; cpu.memory[0x100000] = 0x34;
  check(cpu.read8(0x100000) == 0x12, "disabled A20 aliases bit20");
  cpu.a20_enabled = true;
  check(cpu.read8(0x100000) == 0x34, "enabled A20 exposes high memory");
}

void stack_control_tests() {
  Cpu cpu;
  load(cpu, {0xb8, 0x34, 0x12, 0x50, 0x31, 0xc0, 0x58, 0xf4}); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x1234 && cpu.reg16(Cpu::ESP) == 0xf000, "PUSH POP values and SP");
  load(cpu, {0xe8, 0x01, 0x00, 0xf4, 0x40, 0xc3}); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 1 && cpu.reg16(Cpu::ESP) == 0xf000, "near CALL RET");
  load(cpu, {0xb9, 0x03, 0x00, 0x31, 0xc0, 0x40, 0xe2, 0xfd, 0xf4}); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 3 && cpu.reg16(Cpu::ECX) == 0, "LOOP relative control flow");
  load(cpu, {0x9a, 0x00, 0x02, 0x10, 0x00, 0xf4});
  cpu.memory[0x300] = 0x40; cpu.memory[0x301] = 0xcb; run(cpu);
  check(cpu.reg16(Cpu::EAX) == 1 && cpu.segments[Cpu::CS] == 0 && cpu.reg16(Cpu::ESP) == 0xf000, "far CALL RETF stack order");
  load(cpu, {0xcd, 0x21, 0xf4});
  cpu.write16(0x84, 0x400); cpu.write16(0x86, 0x10);
  cpu.memory[0x500] = 0x40; cpu.memory[0x501] = 0xcf;
  cpu.set_reg16(Cpu::EAX, 4); cpu.eflags = 2 | Cpu::CF | Cpu::IF; run(cpu);
  check(cpu.reg16(Cpu::EAX) == 5 && cpu.eflags == (2 | Cpu::CF | Cpu::IF), "INT IVT and IRET restore flags");
  check(cpu.reg16(Cpu::ESP) == 0xf000 && cpu.segments[Cpu::CS] == 0, "IRET restores CS and stack");
  load(cpu, {0x66, 0x60, 0x66, 0x61, 0xf4});
  for (unsigned i = 0; i < 8; ++i) cpu.regs[i] = 0x12340000U + i * 0x101;
  cpu.regs[Cpu::ESP] = 0x8765f000U; const auto saved = cpu.regs; run(cpu);
  check(cpu.regs == saved, "PUSHAD POPAD preserve high registers and original ESP");
  load(cpu, {0x67, 0xe2, 0xfd}); cpu.regs[Cpu::ECX] = 0x10001; one(cpu);
  check(cpu.regs[Cpu::ECX] == 0x10000 && cpu.eip == 0x1000, "address override LOOP uses ECX");
  // Every Jcc condition, checked against a separately expressed truth table.
  for (unsigned flags = 0; flags < 32; ++flags) {
    const bool c = flags & 1, z = flags & 2, s = flags & 4, o = flags & 8, p = flags & 16;
    const bool expected[16] = {o, !o, c, !c, z, !z, c || z, !c && !z,
                              s, !s, p, !p, s != o, s == o, z || s != o, !z && s == o};
    for (unsigned cc = 0; cc < 16; ++cc) {
      load(cpu, {std::uint8_t(0x70 + cc), 0x02, 0x90, 0x90, 0xf4});
      cpu.eflags = 2 | (c ? Cpu::CF : 0) | (z ? Cpu::ZF : 0) | (s ? Cpu::SF : 0) | (o ? Cpu::OF : 0) | (p ? Cpu::PF : 0);
      one(cpu); check(cpu.eip == (expected[cc] ? 0x1004U : 0x1002U), "Jcc truth table");
    }
  }
}

void enter_leave_real_mode_tests() {
  Cpu cpu;
  constexpr auto flags = 2U | Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::IF | Cpu::DF | Cpu::OF;
  // The screenshot identifies C8 at this address, but does not expose its
  // immediates. Exercise a concrete nested frame there, not a claimed ISO boot.
  cpu.reset(0x2000, 0x056c);
  constexpr std::uint8_t regression[] = {0xc8, 0x10, 0x00, 0x02, 0xc9};
  cpu.memory.write_block(0x2056c, regression, sizeof(regression));
  cpu.regs[Cpu::ESP] = 0xabcd9000; cpu.regs[Cpu::EBP] = 0x12349100; cpu.eflags = flags;
  cpu.write16(0x90fe, 0xa55a); one(cpu);
  check(cpu.regs[Cpu::ESP] == 0xabcd8fea && cpu.regs[Cpu::EBP] == 0x12348ffe,
        "ISO-address ENTER creates a nested real16 frame and reserves locals");
  check(cpu.read16(0x8ffe) == 0x9100 && cpu.read16(0x8ffc) == 0xa55a && cpu.read16(0x8ffa) == 0x8ffe,
        "ENTER saves old BP, copies the enclosing frame and pushes its new frame pointer");
  check(cpu.eip == 0x0570 && cpu.segments[Cpu::CS] == 0x2000 && cpu.eflags == flags,
        "ENTER consumes imm16 plus imm8 and leaves flags unchanged");
  one(cpu);
  check(cpu.regs[Cpu::ESP] == 0xabcd9000 && cpu.regs[Cpu::EBP] == 0x12349100 && cpu.eflags == flags,
        "LEAVE releases the real16 frame and restores the original BP");

  // All 256 immediate encodings, both operand sizes. Nonzero SS and CS override
  // catch accidental DS/override accesses; 67 must not change the stack width.
  for (const unsigned bytes : {2U, 4U}) for (unsigned raw = 0; raw < 256; ++raw) {
    if (bytes == 2) load(cpu, {0x2e, 0x67, 0xc8, 0x23, 0x01, std::uint8_t(raw), 0x67, 0xc9});
    else load(cpu, {0x2e, 0x67, 0x66, 0xc8, 0x23, 0x01, std::uint8_t(raw), 0x67, 0x66, 0xc9});
    cpu.segments[Cpu::SS] = 0x2000; cpu.segments[Cpu::DS] = 0x3000;
    cpu.regs[Cpu::ESP] = 0xabcd9000; cpu.regs[Cpu::EBP] = 0x12347000; cpu.eflags = flags;
    const auto saved = cpu.regs;
    for (unsigned i = 1; i < 32; ++i) {
      if (bytes == 2) cpu.write16(0x27000 - i * bytes, std::uint16_t(0x5100 + i));
      else cpu.write32(0x27000 - i * bytes, 0xabc05100 + i);
    }
    const unsigned level = raw & 31U, slots = level == 0 ? 1 : level + 1;
    const auto frame = bytes == 2 ? 0x8ffeU : 0xabcd8ffcU;
    const auto expected_bp = bytes == 2 ? 0x12348ffeU : frame;
    const auto expected_sp = 0xabcd0000U | (0x9000U - slots * bytes - 0x123U);
    cpu.memory[0x29000] = 0x5a; cpu.memory[0x20000 + (expected_sp & 0xffffU)] = 0xa5;
    one(cpu);
    check(cpu.regs[Cpu::EBP] == expected_bp && cpu.regs[Cpu::ESP] == expected_sp,
          "ENTER masks the nesting level and separates operand size from the 16-bit stack address");
    check((bytes == 2 ? cpu.read16(0x29000 - bytes) : cpu.read32(0x29000 - bytes)) ==
          (bytes == 2 ? 0x7000U : 0x12347000U), "ENTER saves the original operand-sized frame pointer");
    for (unsigned i = 1; i < level; ++i)
      check((bytes == 2 ? cpu.read16(0x29000 - (i + 1) * bytes) : cpu.read32(0x29000 - (i + 1) * bytes)) ==
            (bytes == 2 ? 0x5100U + i : 0xabc05100U + i), "ENTER copies every enclosing frame in order");
    if (level != 0)
      check((bytes == 2 ? cpu.read16(0x29000 - slots * bytes) : cpu.read32(0x29000 - slots * bytes)) == frame,
            "ENTER appends the new frame pointer only at nonzero nesting levels");
    check(cpu.memory[0x29000] == 0x5a && cpu.memory[0x20000 + (expected_sp & 0xffffU)] == 0xa5 && cpu.eflags == flags,
          "ENTER does not overwrite locals or adjacent data, and never modifies flags");
    one(cpu);
    check(cpu.regs == saved && cpu.eflags == flags, "matching LEAVE restores registers after every real-mode ENTER level");
  }

  for (const unsigned allocation : {0U, 1U, 0x8000U, 0xffffU}) {
    load(cpu, {0xc8, std::uint8_t(allocation), std::uint8_t(allocation >> 8), 0x00, 0xc9});
    cpu.regs[Cpu::ESP] = 0xabcd9000; cpu.regs[Cpu::EBP] = 0x12348000;
    one(cpu);
    check(cpu.regs[Cpu::ESP] == (0xabcd0000U | ((0x8ffeU - allocation) & 0xffffU)),
          "ENTER uses unsigned imm16 allocation and wraps only the 16-bit stack pointer");
    one(cpu); check(cpu.regs[Cpu::ESP] == 0xabcd9000 && cpu.regs[Cpu::EBP] == 0x12348000,
                    "LEAVE recovers the frame after maximal or wrapped local allocation");
  }
  load(cpu, {0xc8, 0x02, 0x00, 0x00, 0xc9});
  cpu.regs[Cpu::ESP] = 0xabcd0000; cpu.regs[Cpu::EBP] = 0x12348000; one(cpu);
  check(cpu.regs[Cpu::ESP] == 0xabcdfffc && cpu.read16(0xfffe) == 0x8000, "ENTER push wraps SP from zero");
  one(cpu); check(cpu.regs[Cpu::ESP] == 0xabcd0000 && cpu.regs[Cpu::EBP] == 0x12348000,
                  "LEAVE pop wraps SP while preserving the high half of ESP");
  load(cpu, {0xc8, 0, 0, 2});
  cpu.segments[Cpu::SS] = 0x2000; cpu.regs[Cpu::EBP] = 0x12340000; cpu.set_reg16(Cpu::ESP, 0x8000);
  cpu.write16(0x2fffe, 0xbeef); one(cpu);
  check(cpu.read16(0x27ffc) == 0xbeef && cpu.regs[Cpu::EBP] == 0x12347ffe,
        "ENTER frame-chain address wraps at the stack address width, preserving original upper BP bits");
  load(cpu, {0xc8, 0, 0, 3}); cpu.set_reg16(Cpu::ESP, 0x9000); cpu.set_reg16(Cpu::EBP, 0x9000); one(cpu);
  check(cpu.read16(0x8ffe) == 0x9000 && cpu.read16(0x8ffc) == 0x9000 && cpu.read16(0x8ffa) == 0x9000 &&
        cpu.read16(0x8ff8) == 0x8ffe, "overlapping ENTER chain reads observe earlier pushes in the same instruction");
}

void protected_stack_fixture(Cpu& cpu, bool code32, bool stack32,
                             std::uint32_t limit = 0x7ffff, std::uint32_t base = 0x30000) {
  load(cpu, {0x66, 0xea, 0x00, 0x20, 0x00, 0x00, 0x08, 0x00});
  cpu.gdtr = {23, 0x600};
  cpu.write64(0x600, 0);
  cpu.write64(0x608, 0x00009b000000ffffULL | (code32 ? std::uint64_t{4} << 52 : 0));
  const auto stack = std::uint64_t(limit & 0xffff) | (std::uint64_t(base & 0xffffff) << 16) |
      (std::uint64_t(base & 0xff000000) << 32) | (std::uint64_t(limit & 0xf0000) << 32) |
      (std::uint64_t{0x93} << 40) | (stack32 ? std::uint64_t{4} << 52 : 0);
  cpu.write64(0x610, stack); cpu.cr0 |= 1; one(cpu);
  check(std::string(cpu.mode_name()) == (code32 ? "protected32" : "protected16"), "test loads the requested code descriptor");
  cpu.memory[0x2000] = 0x8e; cpu.memory[0x2001] = 0xd0; cpu.set_reg16(Cpu::EAX, 0x10); one(cpu);
  check(cpu.segments[Cpu::SS] == 0x10, "test loads an independent stack descriptor");
  cpu.eip = 0x2100;
}

void enter_leave_protected_mode_tests() {
  Cpu cpu;
  constexpr auto flags = 2U | Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::IF | Cpu::DF | Cpu::OF;
  for (const bool code32 : {false, true}) for (const bool stack32 : {false, true})
    for (const unsigned bytes : {2U, 4U}) for (const unsigned raw : {0U, 1U, 2U, 31U, 255U}) {
      protected_stack_fixture(cpu, code32, stack32);
      std::vector<std::uint8_t> code{0x2e, 0x67};
      if ((bytes == 4) != code32) code.push_back(0x66);
      code.insert(code.end(), {0xc8, 0x23, 0x01, std::uint8_t(raw), 0x2e, 0x67});
      if ((bytes == 4) != code32) code.push_back(0x66);
      code.push_back(0xc9); cpu.memory.write_block(0x2100, code.data(), code.size());
      cpu.regs[Cpu::ESP] = stack32 ? 0x39000 : 0xabcd9000;
      cpu.regs[Cpu::EBP] = stack32 ? 0x37000 : 0x12347000; cpu.eflags = flags;
      const auto saved = cpu.regs;
      const auto old_sp = stack32 ? 0x39000U : 0x9000U, old_bp = stack32 ? 0x37000U : 0x7000U;
      for (unsigned i = 1; i < 32; ++i) {
        if (bytes == 2) cpu.write16(0x30000 + old_bp - i * bytes, std::uint16_t(0x5200 + i));
        else cpu.write32(0x30000 + old_bp - i * bytes, 0xdef05200 + i);
      }
      const unsigned level = raw & 31, slots = level == 0 ? 1 : level + 1;
      const auto frame = bytes == 2 ? (old_sp - bytes) & 0xffffU
          : stack32 ? old_sp - bytes : 0xabcd0000U | (old_sp - bytes);
      const auto expected_bp = bytes == 2 ? (saved[Cpu::EBP] & ~std::uint64_t{0xffff}) | frame : frame;
      const auto expected_sp = stack32 ? old_sp - slots * bytes - 0x123
          : 0xabcd0000U | (old_sp - slots * bytes - 0x123);
      one(cpu);
      check(cpu.regs[Cpu::EBP] == expected_bp && cpu.regs[Cpu::ESP] == expected_sp && cpu.eflags == flags,
            "protected ENTER honors independent CS.D, SS.B, operand override and ignored address/segment prefixes");
      for (unsigned i = 1; i < level; ++i)
        check((bytes == 2 ? cpu.read16(0x30000 + old_sp - (i + 1) * bytes) : cpu.read32(0x30000 + old_sp - (i + 1) * bytes)) ==
              (bytes == 2 ? 0x5200U + i : 0xdef05200U + i), "protected ENTER reads the chain using SS.B and the SS base");
      one(cpu); check(cpu.regs == saved && cpu.eflags == flags, "protected LEAVE uses SS.B independently of operand size");
    }
  protected_stack_fixture(cpu, true, true);
  constexpr std::uint8_t borrow[] = {0x66, 0x67, 0xc8, 0x04, 0, 0, 0x66, 0x67, 0xc9};
  cpu.memory.write_block(0x2100, borrow, sizeof(borrow));
  cpu.regs[Cpu::ESP] = 0x10004; cpu.regs[Cpu::EBP] = 0x18000; one(cpu);
  check(cpu.regs[Cpu::ESP] == 0xfffe && cpu.regs[Cpu::EBP] == 0x10002,
        "16-bit ENTER on a 32-bit stack borrows across 64KiB in ESP, preserving upper EBP");
  one(cpu); check(cpu.regs[Cpu::ESP] == 0x10004 && cpu.regs[Cpu::EBP] == 0x18000,
                  "16-bit LEAVE still restores a full 32-bit stack pointer");
}

void enter_leave_fault_tests() {
  Cpu cpu;
  const auto rollback_check = [&](const std::array<std::uint64_t, 16>& saved, std::uint64_t ip,
                                  std::uint64_t retired, std::uint32_t flags) {
    check(cpu.regs == saved && cpu.eip == ip && cpu.instructions == retired && cpu.eflags == flags,
          "faulting ENTER/LEAVE restores every register, instruction count, flags and instruction start");
  };
  load(cpu, {0xc8, 0, 0}, 0xfffd); cpu.write16(0xeffe, 0xbeef);
  auto saved = cpu.regs;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit && cpu.fault->bytes.size() == 3,
        "ENTER fetches both complete immediates before it pushes anything");
  rollback_check(saved, 0xfffd, 0, 2); check(cpu.read16(0xeffe) == 0xbeef, "truncated ENTER did not write the stack");
  load(cpu, {0xc8, 0, 0, 1}); cpu.set_reg16(Cpu::ESP, 3); cpu.set_reg16(Cpu::EBP, 0x8000);
  cpu.write16(1, 0xbeef); saved = cpu.regs;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit,
        "ENTER faults when a later push straddles the stack segment limit");
  rollback_check(saved, 0x1000, 0, 2); check(cpu.read16(1) == 0xbeef, "a late ENTER push fault rolls back earlier successful pushes");
  load(cpu, {0x66, 0xc8, 0, 0, 2}); cpu.regs[Cpu::EBP] = 1; cpu.write32(0xeffc, 0xdeadbeef); saved = cpu.regs;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit,
        "operand32 ENTER detects a frame-chain read crossing a 16-bit stack segment");
  rollback_check(saved, 0x1000, 0, 2); check(cpu.read32(0xeffc) == 0xdeadbeef, "chain-read failure rolls back the saved EBP");
  load(cpu, {0xc8, 0xff, 0x7f, 0}); cpu.set_reg16(Cpu::ESP, 0x8000); cpu.write16(0x7ffe, 0xbeef); saved = cpu.regs;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit,
        "ENTER probes a full operand-width slot at final SP, including the segment end");
  rollback_check(saved, 0x1000, 0, 2); check(cpu.read16(0x7ffe) == 0xbeef, "final stack probe failure rolls back the initial push");
  protected_stack_fixture(cpu, true, false, 0x8000);
  constexpr std::uint8_t reserve[] = {0x66, 0xc8, 0x00, 0x80, 0};
  cpu.memory.write_block(0x2100, reserve, sizeof(reserve)); cpu.set_reg16(Cpu::ESP, 0x8000);
  cpu.write16(0x37ffe, 0xbeef); saved = cpu.regs; const auto retired = cpu.instructions;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit,
        "ENTER checks reserved locals against the loaded protected stack limit");
  rollback_check(saved, 0x2100, retired, 2); check(cpu.read16(0x37ffe) == 0xbeef, "protected allocation failure rolls back the saved frame");
  load(cpu, {0x67, 0xc9}); cpu.regs[Cpu::ESP] = 0xabcd8000; cpu.regs[Cpu::EBP] = 0x1234ffff; saved = cpu.regs;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit, "LEAVE validates its operand-sized stack read");
  rollback_check(saved, 0x1000, 0, 2);

  Cpu limited(0x20000, 8192);
  load(limited, {0xc8, 0, 0, 1}); limited.write16(0x3000, 0xbeef);
  limited.set_reg16(Cpu::ESP, 0x3002); limited.set_reg16(Cpu::EBP, 0x7000); const auto limited_saved = limited.regs;
  check(limited.step() == StepResult::Faulted && limited.fault->code == FaultCode::MemoryBudget,
        "ENTER respects the resident RAM limit during a later push");
  check(limited.regs == limited_saved && limited.read16(0x3000) == 0xbeef && limited.eip == 0x1000 && limited.instructions == 0,
        "resident-budget failure rolls back previous ENTER stores and registers");
}

void string_tests() {
  Cpu cpu;
  load(cpu, {0xf3, 0xa4, 0xf4});
  cpu.set_reg16(Cpu::ESI, 0x200); cpu.set_reg16(Cpu::EDI, 0x300); cpu.set_reg16(Cpu::ECX, 4);
  for (unsigned i = 0; i < 4; ++i) cpu.memory[0x200 + i] = std::uint8_t(11 + i);
  one(cpu);
  check(cpu.eip == 0x1000 && cpu.reg16(Cpu::ECX) == 3 && cpu.memory[0x300] == 11, "REP is bounded to one element per step");
  run(cpu);
  check(cpu.reg16(Cpu::ECX) == 0 && cpu.reg16(Cpu::ESI) == 0x204 && cpu.reg16(Cpu::EDI) == 0x304, "REP MOVSB counters");
  check(cpu.memory[0x303] == 14, "REP MOVSB data");
  load(cpu, {0xf2, 0xae, 0xf4});
  cpu.set_reg8(Cpu::EAX, 3); cpu.set_reg16(Cpu::EDI, 0x200); cpu.set_reg16(Cpu::ECX, 4);
  for (unsigned i = 0; i < 4; ++i) cpu.memory[0x200 + i] = std::uint8_t(i + 1);
  run(cpu);
  check(cpu.reg16(Cpu::ECX) == 1 && cpu.reg16(Cpu::EDI) == 0x203 && cpu.flag(Cpu::ZF), "REPNE SCAS stops on match");
  load(cpu, {0xf3, 0xab, 0xf4});
  cpu.set_flag(Cpu::DF, true); cpu.set_reg16(Cpu::EAX, 0xabcd); cpu.set_reg16(Cpu::EDI, 0x304); cpu.set_reg16(Cpu::ECX, 3);
  run(cpu); check(cpu.read16(0x300) == 0xabcd && cpu.reg16(Cpu::EDI) == 0x2fe, "DF reverses string progression");
  load(cpu, {0x67, 0x66, 0xf3, 0xa5, 0xf4});
  cpu.regs[Cpu::ESI] = 0x200; cpu.regs[Cpu::EDI] = 0x300; cpu.regs[Cpu::ECX] = 2;
  cpu.write32(0x200, 0x12345678); cpu.write32(0x204, 0xaabbccdd); run(cpu);
  check(cpu.read32(0x304) == 0xaabbccdd && cpu.regs[Cpu::ESI] == 0x208 && cpu.regs[Cpu::ECX] == 0, "REP MOVSD address32");
  load(cpu, {0xf3, 0xa4, 0xf4});
  cpu.set_reg16(Cpu::ESI, 0xffff); cpu.set_reg16(Cpu::EDI, 0xffff); cpu.set_reg16(Cpu::ECX, 0);
  run(cpu); check(cpu.reg16(Cpu::ESI) == 0xffff && cpu.reg16(Cpu::EDI) == 0xffff, "zero-count REP performs no memory operation");
}

void real_mode_control_boundaries() {
  Cpu cpu;
  // INT's real-mode IVT entry creates a 16-bit frame even with a 66 prefix;
  // an ordinary IRET in the handler must consume exactly that frame.
  load(cpu, {0x66, 0xcd, 0x21, 0xf4});
  cpu.write16(0x84, 0x400); cpu.write16(0x86, 0x10);
  cpu.memory[0x500] = 0x40; cpu.memory[0x501] = 0xcf;
  cpu.eflags = 2 | Cpu::CF | Cpu::IF;
  one(cpu);
  check(cpu.reg16(Cpu::ESP) == 0xeffa, "66 INT real-mode frame is still six bytes");
  check(cpu.read16(0xeffa) == 0x1003 && cpu.read16(0xeffc) == 0 && cpu.read16(0xeffe) == (2 | Cpu::CF | Cpu::IF), "INT frame contains IP CS FLAGS");
  run(cpu);
  check(cpu.reg16(Cpu::ESP) == 0xf000 && cpu.eflags == (2 | Cpu::CF | Cpu::IF), "IRET consumes prefixed-INT frame");

  // A 16-bit relative transfer truncates the target; a 32-bit target must
  // instead encounter the unchanged 64-KiB real-mode CS limit.
  load(cpu, {0xe2, 0x01}, 0xfffe); cpu.set_reg16(Cpu::ECX, 2); one(cpu);
  check(cpu.eip == 1 && cpu.reg16(Cpu::ECX) == 1, "LOOP16 truncates IP at 64KiB");
  load(cpu, {0x66, 0xe2, 0x01}, 0xfffd); cpu.set_reg16(Cpu::ECX, 2);
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit, "LOOP32 rejects target beyond CS limit");
  check(cpu.eip == 0xfffd && cpu.reg16(Cpu::ECX) == 2, "failed LOOP preserves IP and counter");
  load(cpu, {0x66, 0xe3, 0x01}, 0xfffd); cpu.set_reg16(Cpu::ECX, 0);
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit, "prefixed JCXZ uses operand size for target");

  load(cpu, {0x66, 0x9a, 0x00, 0x02, 0x00, 0x00, 0x10, 0x00, 0xf4});
  cpu.memory[0x300] = 0x40; cpu.memory[0x301] = 0x66; cpu.memory[0x302] = 0xcb;
  one(cpu);
  check(cpu.reg16(Cpu::ESP) == 0xeff8 && cpu.read32(0xeff8) == 0x1008 && cpu.read32(0xeffc) == 0, "far CALL32 stack slots and return address");
  run(cpu); check(cpu.reg16(Cpu::ESP) == 0xf000 && cpu.segments[Cpu::CS] == 0 && cpu.reg16(Cpu::EAX) == 1, "far RET32 restores real-mode caller");

  load(cpu, {0x9a, 0x00, 0x02, 0x10, 0x00});
  cpu.set_reg16(Cpu::ESP, 3); cpu.memory[1] = 0x5a; cpu.memory[2] = 0xa5;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit, "far call detects second push across stack limit");
  check(cpu.memory[1] == 0x5a && cpu.memory[2] == 0xa5 && cpu.reg16(Cpu::ESP) == 3 && cpu.eip == 0x1000, "failed multi-push instruction restores journaled first push");
}

void shifts_multiply_divide_tests() {
  Cpu cpu;
  load(cpu, {0xd0, 0xe0, 0xf4}); cpu.set_reg8(Cpu::EAX, 0x80); run(cpu);
  check(cpu.reg8(Cpu::EAX) == 0 && cpu.flag(Cpu::CF) && cpu.flag(Cpu::OF) && cpu.flag(Cpu::ZF), "SHL one-bit flags");
  load(cpu, {0xc0, 0xf8, 0x04, 0xf4}); cpu.set_reg8(Cpu::EAX, 0x81); run(cpu);
  check(cpu.reg8(Cpu::EAX) == 0xf8, "SAR arithmetic sign fill");
  load(cpu, {0xd0, 0xd0, 0xf4}); cpu.set_reg8(Cpu::EAX, 0x80); cpu.set_flag(Cpu::CF, true); run(cpu);
  check(cpu.reg8(Cpu::EAX) == 1 && cpu.flag(Cpu::CF) && cpu.flag(Cpu::OF), "RCL uses old carry");
  load(cpu, {0xc0, 0xc0, 0x08, 0xf4}); cpu.set_reg8(Cpu::EAX, 0x81); run(cpu);
  check(cpu.reg8(Cpu::EAX) == 0x81 && cpu.flag(Cpu::CF), "ROL8 count8 preserves value but updates CF");
  load(cpu, {0x66, 0xd3, 0xe0, 0xf4}); cpu.regs[Cpu::EAX] = 0x12345678; cpu.set_reg8(Cpu::ECX, 32);
  const auto before = cpu.eflags; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x12345678 && cpu.eflags == before, "masked zero shift count preserves flags");
  load(cpu, {0xf6, 0xe3, 0xf4}); cpu.set_reg8(Cpu::EAX, 200); cpu.set_reg8(Cpu::EBX, 3); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 600 && cpu.flag(Cpu::CF) && cpu.flag(Cpu::OF), "MUL byte high product");
  load(cpu, {0x6b, 0xc3, 0xfd, 0xf4}); cpu.set_reg16(Cpu::EBX, 123); run(cpu);
  check(cpu.reg16(Cpu::EAX) == std::uint16_t(-369) && !cpu.flag(Cpu::OF), "IMUL signed immediate");
  load(cpu, {0xf7, 0xf3, 0xf4}); cpu.set_reg16(Cpu::EAX, 1000); cpu.set_reg16(Cpu::EBX, 37); run(cpu);
  check(cpu.reg16(Cpu::EAX) == 27 && cpu.reg16(Cpu::EDX) == 1, "DIV unsigned quotient remainder");
  load(cpu, {0xf7, 0xfb, 0xf4}); cpu.set_reg16(Cpu::EAX, std::uint16_t(-1000)); cpu.set_reg16(Cpu::EDX, 0xffff); cpu.set_reg16(Cpu::EBX, 37); run(cpu);
  check(cpu.reg16(Cpu::EAX) == std::uint16_t(-27) && cpu.reg16(Cpu::EDX) == 0xffff, "IDIV truncates toward zero, remainder sign");
  load(cpu, {0x66, 0xf7, 0xf3, 0xf4}); cpu.regs[Cpu::EDX] = 1; cpu.regs[Cpu::EAX] = 0; cpu.regs[Cpu::EBX] = 3; run(cpu);
  check(cpu.regs[Cpu::EAX] == 1431655765U && cpu.regs[Cpu::EDX] == 1, "DIV32 uses full EDX:EAX");
}

void double_shift_tests() {
  Cpu cpu;
  constexpr std::uint32_t status = Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::OF;
  // Regression for the reported ISO stop. Reproduce its real-mode CS:IP and
  // 0F AD opcode with an explicit ModRM; this is not a full ISO boot test.
  cpu.reset(0x07c0, 0x039c);
  constexpr std::uint8_t iso_regression[] = {0x0f, 0xad, 0xd8};
  cpu.memory.write_block(0x7f9c, iso_regression, sizeof(iso_regression));
  cpu.set_reg16(Cpu::EAX, 0x1234); cpu.set_reg16(Cpu::EBX, 0x5678); cpu.set_reg8(Cpu::ECX, 4);
  one(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x8123 && cpu.reg16(Cpu::EBX) == 0x5678,
        "ISO regression SHRD AX,BX,CL executes and preserves BX");
  check(cpu.eip == 0x039f && cpu.segments[Cpu::CS] == 0x07c0 && cpu.instructions == 1,
        "ISO regression advances the real-mode instruction exactly once");

  // All four opcodes, real-mode operand-size override, both incoming flag
  // polarities, and a fixed arithmetic result independent of the decoder.
  for (const unsigned bits : {16U, 32U}) for (const bool left : {false, true}) {
    for (const bool immediate : {false, true}) for (const bool initial : {false, true}) {
      const auto op = std::uint8_t((left ? 0xa4 : 0xac) + (immediate ? 0 : 1));
      if (bits == 16) load(cpu, {0x0f, op, 0xd8, 4});
      else load(cpu, {0x66, 0x0f, op, 0xd8, 4});
      cpu.regs[Cpu::EAX] = bits == 16 ? 0x1122334455661234ULL : 0x1122334412345678ULL;
      cpu.regs[Cpu::EBX] = bits == 16 ? 0xaabbccddeeff5678ULL : 0xaabbccdd9abcdef0ULL;
      cpu.regs[Cpu::ECX] = 0xabcdef0012345604ULL;
      cpu.eflags = 2 | Cpu::IF | Cpu::DF | (initial ? status : 0);
      const auto saved = cpu.regs;
      one(cpu);
      const auto expected = bits == 16 ? (left ? 0x1122334455662345ULL : 0x1122334455668123ULL)
                                        : (left ? 0x23456789ULL : 0x01234567ULL);
      check(cpu.regs[Cpu::EAX] == expected, "SHLD/SHRD16/32 value and upper register bits");
      check(cpu.regs[Cpu::EBX] == saved[Cpu::EBX] && cpu.regs[Cpu::ECX] == saved[Cpu::ECX],
            "SHLD/SHRD preserves source and full count register");
      check(cpu.eip == 0x1000 + (bits == 32 ? 1U : 0U) + (immediate ? 4U : 3U),
            "SHLD/SHRD instruction length for imm8/CL");
      check(cpu.flag(Cpu::CF) == (left || bits == 32) && !cpu.flag(Cpu::ZF) && !cpu.flag(Cpu::PF) &&
            cpu.flag(Cpu::SF) == (bits == 16 && !left), "SHLD/SHRD defined status flags");
      check(cpu.flag(Cpu::AF) == initial && cpu.flag(Cpu::OF) == initial && cpu.flag(Cpu::IF) && cpu.flag(Cpu::DF),
            "SHLD/SHRD preserves undefined and control flags");
    }
  }

  for (const bool left : {false, true}) for (const bool immediate : {false, true}) {
    const auto op = std::uint8_t((left ? 0xa4 : 0xac) + (immediate ? 0 : 1));
    for (const std::uint8_t raw : {0, 32, 64, 128}) {
      load(cpu, {0x0f, op, 0xd8, raw});
      cpu.regs[Cpu::EAX] = 0x1234567812345678ULL; cpu.regs[Cpu::EBX] = UINT64_MAX;
      cpu.set_reg8(Cpu::ECX, raw); cpu.eflags = 2 | status | Cpu::IF | Cpu::DF;
      one(cpu);
      check(cpu.regs[Cpu::EAX] == 0x1234567812345678ULL && cpu.eflags == (2 | status | Cpu::IF | Cpu::DF),
            "masked zero SHLD/SHRD16 count preserves destination and all flags");
    }
    for (const std::uint8_t raw : {16, 48}) {
      load(cpu, {0x0f, op, 0xd8, raw});
      cpu.set_reg16(Cpu::EAX, 0x8001); cpu.set_reg16(Cpu::EBX, 0);
      cpu.set_reg8(Cpu::ECX, raw); cpu.eflags = 2 | status;
      one(cpu);
      check(cpu.reg16(Cpu::EAX) == 0 && cpu.flag(Cpu::CF) && cpu.flag(Cpu::PF) && cpu.flag(Cpu::ZF) &&
            !cpu.flag(Cpu::SF) && cpu.flag(Cpu::AF) && cpu.flag(Cpu::OF), "full-width SHLD/SHRD16 count16 flags");
    }
    // Intel does not define count17..31 for word operands. Exercise every raw
    // byte under sanitizers without treating any CPU's undefined flags as an
    // oracle. The emulator explicitly preserves those flags and upper bits.
    for (unsigned raw = 0; raw < 256; ++raw) if ((raw & 31) > 16) {
      load(cpu, {0x0f, op, 0xd8, std::uint8_t(raw)});
      cpu.regs[Cpu::EAX] = 0x123456789abc1234ULL; cpu.regs[Cpu::EBX] = 0x5678;
      cpu.set_reg8(Cpu::ECX, std::uint8_t(raw)); cpu.eflags = 2 | status;
      one(cpu);
      check((cpu.regs[Cpu::EAX] & ~0xffffULL) == 0x123456789abc0000ULL &&
            cpu.regs[Cpu::EBX] == 0x5678 && cpu.eflags == (2 | status),
            "undefined word-count policy remains bounded and preserves other state");
    }
    // Zero count is still a memory operand: a segment-limit fault must not
    // modify registers, flags, memory or the instruction pointer.
    for (const std::uint8_t raw : {0, 1}) {
      load(cpu, {0x0f, op, 0x1e, 0xff, 0xff, raw});
      cpu.set_reg8(Cpu::ECX, raw); cpu.regs[Cpu::EBX] = 0x5678; cpu.eflags = 2 | status;
      cpu.memory[0xffff] = 0xa5; const auto saved = cpu.regs;
      check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit,
            "SHLD/SHRD memory operand respects segment limit, including count0");
      check(cpu.regs == saved && cpu.eflags == (2 | status) && cpu.eip == 0x1000 && cpu.memory[0xffff] == 0xa5,
            "faulting SHLD/SHRD rolls back all architectural effects");
    }
  }

  struct FlagCase { bool left; std::uint16_t destination, source, result; bool carry, overflow; };
  const FlagCase flags[] = {{true, 0x8000, 0, 0, true, true}, {true, 0x1000, 0, 0x2000, false, false},
                            {false, 0x1234, 0xffff, 0x891a, false, true}, {false, 0x8001, 1, 0xc000, true, false}};
  for (const auto& f : flags) for (const bool initial : {false, true}) {
    load(cpu, {0x0f, std::uint8_t(f.left ? 0xa4 : 0xac), 0xd8, 1});
    cpu.set_reg16(Cpu::EAX, f.destination); cpu.set_reg16(Cpu::EBX, f.source);
    cpu.eflags = 2 | (initial ? status : 0); one(cpu);
    check(cpu.reg16(Cpu::EAX) == f.result && cpu.flag(Cpu::CF) == f.carry && cpu.flag(Cpu::OF) == f.overflow,
          "one-bit SHLD/SHRD computes CF and sign-change OF");
    check(cpu.flag(Cpu::AF) == initial, "one-bit SHLD/SHRD preserves undefined AF");
  }

  load(cpu, {0x26, 0x0f, 0xad, 0x5a, 0xfe}); // SHRD ES:[BP+SI-2],BX,CL
  cpu.segments[Cpu::ES] = 0x2000; cpu.segments[Cpu::SS] = 0x3000;
  cpu.set_reg16(Cpu::EBP, 0x10); cpu.set_reg16(Cpu::ESI, 6);
  cpu.set_reg16(Cpu::EBX, 0x5678); cpu.set_reg8(Cpu::ECX, 4);
  cpu.write16(0x20014, 0x1234); cpu.write16(0x30014, 0xabcd); one(cpu);
  check(cpu.read16(0x20014) == 0x8123 && cpu.read16(0x30014) == 0xabcd,
        "SHRD memory16 handles signed displacement and segment override");

  load(cpu, {0x67, 0x66, 0x0f, 0xa4, 0x5c, 0xbe, 0x08, 4}); // SHLD [ESI+EDI*4+8],EBX,4
  cpu.segments[Cpu::DS] = 0x1000; cpu.regs[Cpu::ESI] = 0x200; cpu.regs[Cpu::EDI] = 3;
  cpu.regs[Cpu::EBX] = 0x9abcdef0; cpu.write32(0x10214, 0x12345678);
  cpu.memory[0x10213] = 0xa5; cpu.memory[0x10218] = 0x5a; one(cpu);
  check(cpu.read32(0x10214) == 0x23456789 && cpu.memory[0x10213] == 0xa5 && cpu.memory[0x10218] == 0x5a,
        "SHLD memory32 SIB and address/operand overrides write exactly four bytes");

  load(cpu, {0x0f, 0xad, 0xd9}); cpu.set_reg16(Cpu::ECX, 0x1204); cpu.set_reg16(Cpu::EBX, 0x5678); one(cpu);
  check(cpu.reg16(Cpu::ECX) == 0x8120, "SHRD CX,BX,CL reads the original count before writing CX");
  load(cpu, {0x0f, 0xad, 0xc8}); cpu.set_reg16(Cpu::ECX, 0x5604); cpu.set_reg16(Cpu::EAX, 0x1234); one(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x4123 && cpu.reg16(Cpu::ECX) == 0x5604, "SHRD AX,CX,CL preserves aliased source/count");
  load(cpu, {0x0f, 0xad, 0xc0}); cpu.set_reg16(Cpu::EAX, 0x1234); cpu.set_reg8(Cpu::ECX, 4); one(cpu);
  check(cpu.reg16(Cpu::EAX) == 0x4123, "SHRD with identical source/destination reads original source");
  load(cpu, {0x0f, 0xac, 0xd8}, 0xfffd); cpu.set_reg16(Cpu::EAX, 0x1234); cpu.eflags = 2 | status;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit &&
        cpu.eip == 0xfffd && cpu.reg16(Cpu::EAX) == 0x1234 && cpu.eflags == (2 | status),
        "truncated SHRD immediate faults before any destination/flag change");
}

void signed_multiply_boundaries() {
  Cpu cpu;
  // A full signed 16/32-bit product fits in int64_t. This direct product is an
  // independent reference for the portable CPU implementation's range check.
  for (const unsigned bits : {16U, 32U}) {
    const std::int64_t maximum = (std::int64_t{1} << (bits - 1)) - 1;
    const std::int64_t minimum = -maximum - 1;
    const std::uint64_t value_mask = (std::uint64_t{1} << bits) - 1;
    const std::int64_t values[] = {minimum, minimum + 1, -257, -2, -1,
                                  0, 1, 2, 257, maximum};
    for (const auto a : values) for (const auto b : values) {
      const std::int64_t product = a * b;
      const bool overflow = product < minimum || product > maximum;
      const auto expected_low = std::uint64_t(product) & value_mask;
      const auto label = "IMUL" + std::to_string(bits) + " " +
                         std::to_string(a) + " * " + std::to_string(b);
      for (const bool initial_overflow : {false, true}) {
        // 0F AF C3: IMUL AX/EAX, BX/EBX, without the implicit high-product form.
        if (bits == 16) load(cpu, {0x0f, 0xaf, 0xc3});
        else load(cpu, {0x66, 0x0f, 0xaf, 0xc3});
        cpu.regs[Cpu::EAX] = 0x1122334400000000ULL | (std::uint64_t(a) & value_mask);
        cpu.regs[Cpu::EBX] = 0x8877665500000000ULL | (std::uint64_t(b) & value_mask);
        const auto old_destination = cpu.regs[Cpu::EAX];
        const auto source = cpu.regs[Cpu::EBX];
        cpu.eflags = 2 | Cpu::IF | Cpu::DF |
                     (initial_overflow ? Cpu::CF | Cpu::OF : 0);
        one(cpu);
        const auto expected = bits == 16 ? (old_destination & ~value_mask) | expected_low
                                         : expected_low;
        check(cpu.regs[Cpu::EAX] == expected, label + " result and upper register bits");
        check(cpu.regs[Cpu::EBX] == source, label + " preserves source");
        check(cpu.flag(Cpu::CF) == overflow && cpu.flag(Cpu::OF) == overflow,
              label + " CF/OF");
        check(cpu.flag(Cpu::IF) && cpu.flag(Cpu::DF), label + " preserves control flags");
      }
    }
  }
}

void hooks_and_fault_tests() {
  Cpu cpu;
  load(cpu, {0xcd, 0x10, 0xf4});
  bool called = false;
  cpu.interrupt_hook = [&](Cpu& c, std::uint8_t n) {
    called = n == 0x10 && c.eip == 0x1002; c.set_reg16(Cpu::EAX, 0xbeef); c.set_flag(Cpu::CF, false); return true;
  };
  run(cpu); check(called && cpu.reg16(Cpu::EAX) == 0xbeef && cpu.reg16(Cpu::ESP) == 0xf000, "host INT consumes without guest frame");
  cpu.interrupt_hook = nullptr;
  load(cpu, {0xba, 0xf8, 0x03, 0x66, 0xed, 0xee, 0xf4});
  unsigned in_width = 0; std::uint32_t output = 0;
  cpu.in_hook = [&](std::uint16_t port, unsigned w) -> std::optional<std::uint32_t> {
    check(port == 0x3f8, "IN DX port"); in_width = w; return 0x12345678;
  };
  cpu.out_hook = [&](std::uint16_t port, unsigned w, std::uint32_t value) {
    check(port == 0x3f8 && w == 1, "OUT DX byte width"); output = value; return true;
  };
  run(cpu); check(in_width == 4 && output == 0x78, "I/O hook widths and mask");
  load(cpu, {0xe4, 0x99}); cpu.in_hook = nullptr;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::UnsupportedPort && cpu.eip == 0x1000, "unknown port stops instead of fabricating value");
  load(cpu, {0x0f, 0x20, 0xc0, 0x66, 0x0d, 0x01, 0x00, 0x00, 0x80, 0x0f, 0x22, 0xc0}); one(cpu); one(cpu);
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::UnsupportedState, "legacy paging request faults explicitly");
  check(cpu.cr0 == 0x10 && cpu.eip == 0x1009 && cpu.regs[Cpu::EAX] == 0x80000011, "unsupported paging preserves CR0 and faulting IP");
  check(cpu.fault->opcode == 0x0f22 && cpu.fault->bytes.size() == 3 && !cpu.fault->describe().empty(), "fault includes opcode bytes and address");
  load(cpu, {0x0f, 0x0b});
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::InvalidInstruction && cpu.instructions == 0, "UD2 explicit invalid instruction");
  load(cpu, {0xa1, 0xff, 0xff}); cpu.regs[Cpu::EAX] = 0xdeadbeef;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit && cpu.regs[Cpu::EAX] == 0xdeadbeef, "multi-byte access cannot cross segment limit");
  load(cpu, {0x67, 0xa1, 0x00, 0x00, 0x01, 0x00});
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::SegmentLimit, "32-bit addressing retains real-mode segment limits");
  load(cpu, {0xf7, 0xf3}); cpu.set_reg16(Cpu::EAX, 42); cpu.set_reg16(Cpu::EBX, 0);
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::DivideError && cpu.reg16(Cpu::EAX) == 42, "divide error preserves dividend");
  load(cpu, {0x66, 0xf7, 0xfb});
  cpu.regs[Cpu::EDX] = 0x80000000U; cpu.regs[Cpu::EAX] = 0; cpu.regs[Cpu::EBX] = 0xffffffffU;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::DivideError, "IDIV INT64_MIN/-1 is checked before host division");
  load(cpu, {0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x66, 0x90});
  check(cpu.step() == StepResult::Faulted && cpu.fault->bytes.size() == 15, "15-byte decoder limit");
  load(cpu, {0xcd, 0x42});
  cpu.interrupt_hook = [](Cpu& c, std::uint8_t) {
    c.write8(0x222, 0xaa); c.set_reg16(Cpu::EAX, 123); c.fail("host service intentionally unavailable"); return true;
  };
  cpu.memory[0x222] = 0x11;
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::HostError, "host fail becomes structured fault");
  check(cpu.memory[0x222] == 0x11 && cpu.reg16(Cpu::EAX) == 0 && cpu.eip == 0x1000, "CPU-mediated writes and registers roll back on failed instruction");
}
}  // namespace

int main() {
  try {
    arithmetic_exhaustive(); register_and_decode_tests(); memory_address_tests();
    stack_control_tests(); enter_leave_real_mode_tests(); enter_leave_protected_mode_tests(); enter_leave_fault_tests();
    string_tests(); real_mode_control_boundaries();
    shifts_multiply_divide_tests(); double_shift_tests(); signed_multiply_boundaries(); hooks_and_fault_tests();
    std::cout << "PASS x86 CPU: " << checks << " checks, including 655360 exhaustive 8-bit ALU executions\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL x86 CPU after " << checks << " checks: " << e.what() << '\n';
    return 1;
  }
}
