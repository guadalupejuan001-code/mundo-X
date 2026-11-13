#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

namespace {
constexpr std::uint64_t code_address = 0xa000;
unsigned checks = 0;

void check(bool passed, const char* message) {
  ++checks;
  if (!passed) throw std::runtime_error(message);
}

void program(Cpu& cpu, std::initializer_list<std::uint8_t> code) {
  cpu.eip = code_address;
  cpu.halted = false;
  cpu.fault.reset();
  cpu.memory.write_block(code_address, code.begin(), code.size());
}

StepResult step(Cpu& cpu) {
  const auto result = cpu.step();
  if (result == StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
  return result;
}

void boot(Cpu& cpu, bool long64) {
  const auto sector = cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00, sector.data(), sector.size());
  cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
    if (vector != 0x15 || guest.reg16(Cpu::EAX) != 0x2401)
      guest.fail("unexpected BIOS service");
    guest.a20_enabled = true;
    guest.set_reg8(4, 0);
    guest.set_flag(Cpu::CF, false);
    return true;
  };
  for (unsigned n = 0; n < 12000; ++n) {
    const auto result = step(cpu);
    if (long64 && result == StepResult::Halted && cpu.is_long_mode()) return;
    if (!long64 && std::string_view(cpu.mode_name()) == "protected32" &&
        cpu.segments[Cpu::SS] == 0x10) return;
  }
  throw std::runtime_error("fixture did not boot into the requested CPU mode");
}

void expect_fault(Cpu& cpu, FaultCode expected, const char* message) {
  // With no IDT handler, observe the architecturally classified fault and
  // restart address directly.
  cpu.idtr = {0, 0};
  check(cpu.step() == StepResult::Faulted && cpu.fault && cpu.fault->code == expected, message);
  check(cpu.eip == code_address && cpu.fault->eip == code_address,
        "fault did not preserve the instruction start");
}

void protected32(Cpu& cpu) {
  // MOV DS,AX with a null selector, then INVLPG [EAX] targeting an MMIO window.
  cpu.regs[Cpu::EAX] = 0x01000000;
  unsigned target_claims = 0;
  cpu.mmio_claim_hook = [&](std::uint64_t address) {
    if (address != 0x01000000) return false;
    ++target_claims;
    return true;
  };
  program(cpu, {0x8e, 0xd8, 0x0f, 0x01, 0x38});
  step(cpu);
  check(cpu.segments[Cpu::DS] == 0, "fixture did not load a null DS");
  const auto saved_registers = cpu.regs;
  const auto saved_flags = cpu.eflags;
  step(cpu);
  check(cpu.eip == code_address + 5 && target_claims == 0,
        "INVLPG must retire without accessing its segment or MMIO operand");
  check(cpu.regs == saved_registers && cpu.eflags == saved_flags,
        "INVLPG changed a register or FLAGS");
  cpu.mmio_claim_hook = {};
}

void long64(Cpu& cpu) {
  constexpr std::uint64_t large_page = 0x200083;
  cpu.write64(0x3008, large_page);
  cpu.cr2 = 0x123456;
  program(cpu, {0x8d, 0x40, 0x00}); // Three-byte LEA, with no operand access.
  const auto before_lea = cpu.page_walks;
  step(cpu);
  const auto fetch_walks = cpu.page_walks - before_lea;
  for (const auto address : {0x200000ULL, 0x400000ULL, 0x800000000000ULL}) {
    cpu.regs[Cpu::EAX] = address;
    program(cpu, {0x0f, 0x01, 0x38}); // mapped, absent or noncanonical
    const auto before = cpu.page_walks;
    step(cpu);
    check(cpu.eip == code_address + 3 && cpu.page_walks == before + fetch_walks,
          "INVLPG must retire with only its instruction-fetch page walks");
    check(cpu.read64(0x3008) == large_page && cpu.cr2 == 0x123456,
          "INVLPG walked the target page or changed CR2");
  }
  // FS base makes this address noncanonical even though the SIB offset is
  // canonical. A valid instruction still consumes all prefixes and bytes.
  cpu.fs_base = 0x7ffffffff000ULL;
  cpu.regs[Cpu::EAX] = 0x1000;
  program(cpu, {0x64, 0x67, 0x0f, 0x01, 0x7c, 0x20, 0x7f});
  step(cpu);
  check(cpu.eip == code_address + 7, "FS/addr32 INVLPG did not retire");
}

void enter_cpl3(Cpu& cpu, bool long_mode) {
  constexpr std::uint64_t gdt = 0x14000;
  constexpr std::uint64_t frame = 0x6000;
  cpu.gdtr = {0x2f, gdt};
  cpu.write64(gdt + 0x20, long_mode ? 0x00affa000000ffffULL : 0x00cffa000000ffffULL);
  cpu.write64(gdt + 0x28, 0x00cff2000000ffffULL);
  if (long_mode) {
    for (const auto entry : {0x1000U, 0x2000U, 0x3000U})
      cpu.write64(entry, cpu.read64(entry) | 4); // guest code and stack are user accessible
    cpu.write64(frame, code_address);
    cpu.write64(frame + 8, 0x23);
    cpu.write64(frame + 16, 2);
    cpu.write64(frame + 24, 0x7000);
    cpu.write64(frame + 32, 0x2b);
    program(cpu, {0x48, 0xcf}); // IRETQ
  } else {
    cpu.write32(frame, code_address);
    cpu.write32(frame + 4, 0x23);
    cpu.write32(frame + 8, 2);
    cpu.write32(frame + 12, 0x7000);
    cpu.write32(frame + 16, 0x2b);
    program(cpu, {0xcf}); // IRETD
  }
  cpu.regs[Cpu::ESP] = frame;
  step(cpu);
  check(cpu.cpl() == 3 && cpu.eip == code_address, "guest IRET failed to enter CPL3");
}

void exercise(bool long_mode) {
  Cpu cpu(32U << 20);
  boot(cpu, long_mode);
  if (long_mode) long64(cpu);
  else protected32(cpu);

  program(cpu, {0x0f, 0x01, 0xff});
  expect_fault(cpu, FaultCode::InvalidInstruction, "reserved register encoding must raise #UD");
  program(cpu, {0xf0, 0x0f, 0x01, 0x38});
  expect_fault(cpu, FaultCode::InvalidInstruction, "LOCK INVLPG must raise #UD");

  enter_cpl3(cpu, long_mode);
  cpu.regs[Cpu::EAX] = long_mode ? 0x800000000000ULL : 0x01000000ULL;
  program(cpu, {0x0f, 0x01, 0x38});
  expect_fault(cpu, FaultCode::ProtectionFault, "CPL3 INVLPG must raise #GP(0)");
  check(cpu.fault->error_code == 0, "INVLPG #GP did not carry error code zero");
  program(cpu, {0xf0, 0x0f, 0x01, 0x38});
  expect_fault(cpu, FaultCode::InvalidInstruction, "LOCK encoding must raise #UD even at CPL3");
}
} // namespace

int main() {
  try {
    exercise(false);
    exercise(true);
    std::cout << "X86_INVLPG_PASS checks=" << checks << '\n';
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "X86_INVLPG_FAIL " << e.what() << '\n';
    return 1;
  }
}
