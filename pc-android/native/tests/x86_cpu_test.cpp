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
  load(cpu, {0x0f, 0x20, 0xc0, 0x0c, 0x01, 0x0f, 0x22, 0xc0}); one(cpu); one(cpu);
  check(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::UnsupportedState, "protected-mode request faults");
  check(cpu.cr0 == 0x10 && cpu.eip == 0x1005 && cpu.regs[Cpu::EAX] == 0x11, "PM request preserves CR0 and faulting IP");
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
    stack_control_tests(); string_tests(); real_mode_control_boundaries();
    shifts_multiply_divide_tests(); hooks_and_fault_tests();
    std::cout << "PASS x86 CPU: " << checks << " checks, including 655360 exhaustive 8-bit ALU executions\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "FAIL x86 CPU after " << checks << " checks: " << e.what() << '\n';
    return 1;
  }
}
