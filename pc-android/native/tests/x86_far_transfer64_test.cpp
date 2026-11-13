#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

constexpr std::uint64_t kCode = 0xa000;
constexpr std::uint64_t kTarget = 0xc000;
constexpr std::uint64_t kStack = 0x1a6de8;
constexpr std::uint64_t kPointer = 0x17000;
constexpr std::uint64_t kGdt = 0x18000;
constexpr std::uint64_t kHighBase = 0xffff800000000000ULL;
constexpr std::uint64_t kHighPhysical = 0x200000;
constexpr std::uint16_t kKernelCompat = 0x08;
constexpr std::uint16_t kKernelData = 0x10;
constexpr std::uint16_t kKernelCode = 0x18;
constexpr std::uint16_t kUserCode = 0x23;
constexpr std::uint16_t kUserData = 0x2b;
constexpr std::uint16_t kUserCompat = 0x33;
unsigned checks = 0;

void check(bool ok, const char* message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}

void step(Cpu& cpu) {
  if (cpu.step() == StepResult::Executed) return;
  throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "guest stopped unexpectedly");
}

void write(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> bytes) {
  cpu.memory.write_block(address, bytes.begin(), bytes.size());
}

void execute_at(Cpu& cpu, std::initializer_list<std::uint8_t> bytes) {
  write(cpu, kCode, bytes);
  cpu.eip = kCode;
  cpu.halted = false;
  cpu.fault.reset();
}

void boot_to(Cpu& cpu, std::string_view mode) {
  const auto disk = cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00, disk.data(), disk.size());
  cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
    if (vector != 0x15 || guest.reg16(Cpu::EAX) != 0x2401)
      guest.fail("unexpected BIOS call in far-transfer fixture");
    guest.a20_enabled = true;
    guest.set_reg8(4, 0);
    guest.set_flag(Cpu::CF, false);
    return true;
  };
  for (unsigned n = 0; n < 5000; ++n) {
    if (std::string_view(cpu.mode_name()) == mode && cpu.segments[Cpu::SS] == kKernelData) return;
    step(cpu);
  }
  throw std::runtime_error("guest did not reach the required start mode");
}

void tables(Cpu& cpu) {
  cpu.gdtr = {0x47, kGdt};
  cpu.idtr = {0, 0}; // Observe terminal exception state without consuming it in a handler.
  cpu.write64(kGdt + kKernelCompat, 0x00cf9b000000ffffULL);
  cpu.write64(kGdt + kKernelData, 0x00cf93000000ffffULL);
  cpu.write64(kGdt + kKernelCode, 0x00af9b000000ffffULL);
  cpu.write64(kGdt + (kUserCode & ~3U), 0x00affb000000ffffULL);
  cpu.write64(kGdt + (kUserData & ~3U), 0x00cff3000000ffffULL);
  cpu.write64(kGdt + (kUserCompat & ~3U), 0x00cffb000000ffffULL);
  cpu.write64(kGdt + 0x38, 0x00af1b000000ffffULL); // Absent kernel code.
  cpu.write64(kGdt + 0x40, 0x00cf73000000ffffULL); // Absent user stack.
  for (const auto address : {0x1000U, 0x2000U, 0x3000U})
    cpu.write64(address, cpu.read64(address) | 4);
  cpu.write64(0x1800, 0x4007); // PML4[256]: a high range with distinct physical pages.
  cpu.write64(0x4000, 0x5007);
  cpu.write64(0x5000, kHighPhysical | 0x87);
  cpu.regs[Cpu::ESP] = kStack;
}

void pointer(Cpu& cpu, unsigned width, std::uint64_t address, std::uint16_t selector) {
  if (width == 8) cpu.write64(kPointer, address);
  else if (width == 4) cpu.write32(kPointer, std::uint32_t(address));
  else cpu.write16(kPointer, std::uint16_t(address));
  cpu.write16(kPointer + width, selector);
}

void expect_fault(Cpu& cpu, FaultCode code, unsigned error_code, const char* reason) {
  const auto regs = cpu.regs;
  const auto segments = cpu.segments;
  const auto flags = cpu.eflags;
  const auto ip = cpu.eip;
  const auto instructions = cpu.instructions;
  if (cpu.step() != StepResult::Faulted || !cpu.fault ||
      cpu.fault->code != code || cpu.fault->error_code != error_code) {
    throw std::runtime_error(std::string(reason) + ": " +
                             (cpu.fault ? cpu.fault->describe() : "no matching exception"));
  }
  ++checks;
  check(cpu.regs == regs && cpu.segments == segments && cpu.eflags == flags &&
            cpu.eip == ip && cpu.fault->eip == ip && cpu.instructions == instructions,
        "faulting far transfer must preserve its restart state");
}

void observed_retfq() {
  constexpr std::uint64_t observed = 0x2a5a52cc;
  constexpr std::uint64_t fallback_cs = 0x123456789abc0000ULL | kKernelCode;
  constexpr auto target = kHighBase + kTarget;
  Cpu cpu(16U << 20);
  boot_to(cpu, "compat32");
  check(cpu.read8(cpu.eip) == 0xea, "guest must reach its compatibility-mode far jump");
  tables(cpu);
  cpu.write64(kGdt + kKernelData, 0x00af9b000000ffffULL);
  cpu.write64(0x3000 + 8 * (observed >> 21), 0x400083);
  cpu.write32(cpu.eip + 1, observed);
  cpu.write16(cpu.eip + 5, kKernelData);
  write(cpu, 0x400000 + (observed & 0x1fffff), {0x48, 0xcb, 0x0f, 0x0b});
  write(cpu, kHighPhysical + kTarget, {0xb8, 0x78, 0x56, 0x34, 0x12});
  step(cpu);
  check(cpu.is_long_mode() && cpu.cpl() == 0 && cpu.eip == observed &&
            cpu.segments[Cpu::CS] == 0x10,
        "original boot guest must reach the recorded long-mode CS:RIP");
  cpu.regs = {0x10, 0x2a616954, 0x2a5a52ce, 0x2a616940, kStack, 0x1a6ee0, 0, 0x2a616940,
              0, 0x2a622000, 0x2a5c375a, 0x2a47430e, 0x1a803c, 0, 1, 0x1a8d02};
  cpu.eflags = 0x200002;
  cpu.write64(kStack, target);
  cpu.write64(kStack + 8, fallback_cs);
  const auto steps = cpu.instructions;
  const auto flags = cpu.eflags;
  auto regs = cpu.regs;
  regs[Cpu::ESP] += 16;
  step(cpu);
  check(cpu.eip == target && cpu.segments[Cpu::CS] == kKernelCode &&
            cpu.regs == regs && cpu.eflags == flags && cpu.instructions == steps + 1,
        "RETFQ must pop full RIP and the low 16 bits of the CS slot");
  step(cpu);
  check(cpu.regs[Cpu::EAX] == 0x12345678U, "RETFQ must execute the real destination bytes");
}

void return_operand_widths() {
  Cpu cpu(16U << 20);
  boot_to(cpu, "long64");
  tables(cpu);
  execute_at(cpu, {0xcb});
  cpu.write32(kStack, kTarget);
  cpu.write32(kStack + 4, 0xfeed0018U);
  step(cpu);
  check(cpu.eip == kTarget && cpu.regs[Cpu::ESP] == kStack + 8,
        "default far return must consume two dwords in long64");

  execute_at(cpu, {0x66, 0xcb});
  cpu.regs[Cpu::ESP] = kHighBase + kStack;
  cpu.write16(kHighPhysical + kStack, kTarget);
  cpu.write16(kHighPhysical + kStack + 2, kKernelCode);
  step(cpu);
  check(cpu.eip == kTarget && cpu.regs[Cpu::ESP] == kHighBase + kStack + 4,
        "operand-size override must pop words using full RSP");

  execute_at(cpu, {0x48, 0xca, 0x20, 0x00});
  cpu.regs[Cpu::ESP] = kHighBase + kStack;
  cpu.write64(kHighPhysical + kStack, kTarget);
  cpu.write64(kHighPhysical + kStack + 8, kKernelCompat);
  step(cpu);
  check(std::string_view(cpu.mode_name()) == "compat32" && cpu.eip == kTarget &&
            cpu.regs[Cpu::ESP] == kStack + 16 + 0x20,
        "same-CPL RETFQ must read the high frame and adjust the resulting compatibility ESP");
}

void return_to_user(bool compat) {
  Cpu cpu(16U << 20);
  boot_to(cpu, "long64");
  tables(cpu);
  execute_at(cpu, {0x48, 0xca, 0x18, 0x00});
  constexpr auto adjustment = 0x18U;
  const auto destination = compat ? kTarget : kHighBase + kTarget;
  const auto user_stack = compat ? 0xe000UL : kHighBase + 0xe000;
  const auto user_cs = compat ? kUserCompat : kUserCode;
  cpu.write64(kStack, destination);
  cpu.write64(kStack + 8, 0xabcdef0000000000ULL | user_cs);
  cpu.write64(kStack + 16 + adjustment, user_stack);
  cpu.write64(kStack + 24 + adjustment, 0xfedcba0000000000ULL | kUserData);
  cpu.eflags = 0x202;
  step(cpu);
  check(cpu.cpl() == 3 && cpu.eip == destination && cpu.segments[Cpu::CS] == user_cs &&
            cpu.segments[Cpu::SS] == kUserData && cpu.regs[Cpu::ESP] == user_stack + adjustment &&
            cpu.eflags == 0x202,
        "outer RETFQ must restore CS:RIP, SS:RSP and adjust the user stack");
  check(cpu.segments[Cpu::DS] == 0 && cpu.segments[Cpu::ES] == 0,
        "outer return must invalidate inherited kernel data segments");
  write(cpu, (compat ? 0 : kHighPhysical) + kTarget, {0x90});
  step(cpu);
  check(cpu.eip == destination + 1, "user code after outer far return must execute");

  cpu.regs[Cpu::ESP] = 0xe000;
  cpu.write32(0xe000, kCode);
  cpu.write32(0xe004, kKernelCode);
  execute_at(cpu, {0xcb});
  expect_fault(cpu, FaultCode::ProtectionFault, kKernelCode, "far return must not elevate CPL3");
}

void indirect_calls_and_jumps() {
  Cpu cpu(16U << 20);
  boot_to(cpu, "long64");
  tables(cpu);

  execute_at(cpu, {0x48, 0xff, 0x1c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 8, kHighBase + kTarget, kKernelCode);
  write(cpu, kHighPhysical + kTarget, {0x48, 0xcb});
  step(cpu);
  check(cpu.eip == kHighBase + kTarget && cpu.regs[Cpu::ESP] == kStack - 16 &&
            cpu.read64(kStack - 16) == kCode + 8 && cpu.read64(kStack - 8) == kKernelCode,
        "64-bit far CALL must push two qword slots and select the full RIP");
  step(cpu);
  check(cpu.eip == kCode + 8 && cpu.regs[Cpu::ESP] == kStack,
        "RETFQ must complete a real far CALL round trip");

  execute_at(cpu, {0xff, 0x1c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 4, kTarget, kKernelCompat);
  write(cpu, kTarget, {0xcb});
  step(cpu);
  check(std::string_view(cpu.mode_name()) == "compat32" && cpu.eip == kTarget &&
            cpu.regs[Cpu::ESP] == kStack - 8 && cpu.read32(kStack - 8) == kCode + 7 &&
            cpu.read32(kStack - 4) == kKernelCode,
        "default far CALL must push dword slots and enter compatibility mode");
  step(cpu);
  check(cpu.is_long_mode() && cpu.eip == kCode + 7 && cpu.regs[Cpu::ESP] == kStack,
        "compatibility RETF must return to actual 64-bit code");

  execute_at(cpu, {0x66, 0xff, 0x1c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 2, kTarget, kKernelCode);
  write(cpu, kTarget, {0x66, 0xcb});
  step(cpu);
  check(cpu.is_long_mode() && cpu.eip == kTarget && cpu.regs[Cpu::ESP] == kStack - 4 &&
            cpu.read16(kStack - 4) == kCode + 8 && cpu.read16(kStack - 2) == kKernelCode,
        "16-bit far CALL must push two words");
  step(cpu);
  check(cpu.eip == kCode + 8 && cpu.regs[Cpu::ESP] == kStack,
        "16-bit far return must complete the matching far call");

  execute_at(cpu, {0x48, 0xff, 0x2c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 8, kHighBase + kTarget, kKernelCode);
  step(cpu);
  check(cpu.eip == kHighBase + kTarget && cpu.regs[Cpu::ESP] == kStack,
        "64-bit far JMP must leave the stack untouched");

  constexpr auto wide_compat_offset = 0x100000000ULL + kTarget;
  execute_at(cpu, {0x48, 0xff, 0x2c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 8, wide_compat_offset, kKernelCompat);
  expect_fault(cpu, FaultCode::ProtectionFault, 0, "far JMP to compatibility code must check the full offset");

  execute_at(cpu, {0x48, 0xff, 0x1c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 8, wide_compat_offset, kKernelCompat);
  step(cpu);
  check(std::string_view(cpu.mode_name()) == "compat32" && cpu.eip == kTarget &&
            cpu.regs[Cpu::ESP] == kStack - 16,
        "far CALL to compatibility code must truncate its target and keep its qword frame");
}

void faults_and_rollback() {
  Cpu cpu(16U << 20);
  boot_to(cpu, "long64");
  tables(cpu);
  for (const auto selector : {0x00U, 0x10U, 0x48U}) {
    execute_at(cpu, {0x48, 0xcb});
    cpu.write64(kStack, kTarget);
    cpu.write64(kStack + 8, selector);
    expect_fault(cpu, FaultCode::ProtectionFault, selector, "invalid RETFQ code selector");
  }
  execute_at(cpu, {0x48, 0xcb});
  cpu.write64(kStack, 0x0000800000000000ULL);
  cpu.write64(kStack + 8, kKernelCode);
  expect_fault(cpu, FaultCode::ProtectionFault, 0, "RETFQ target must be canonical");

  execute_at(cpu, {0x48, 0xcb});
  cpu.write64(kStack, kTarget);
  cpu.write64(kStack + 8, 0x38);
  expect_fault(cpu, FaultCode::SegmentNotPresent, 0x38, "RETFQ requires a present code segment");

  execute_at(cpu, {0x48, 0xcb});
  cpu.write64(kStack, kTarget);
  cpu.write64(kStack + 8, kUserCode);
  cpu.write64(kStack + 16, 0xe000);
  cpu.write64(kStack + 24, 0x43);
  expect_fault(cpu, FaultCode::StackFault, 0x40, "outer RETFQ requires a present stack segment");

  execute_at(cpu, {0x48, 0xcb});
  cpu.regs[Cpu::ESP] = 0x0000800000000000ULL;
  expect_fault(cpu, FaultCode::StackFault, 0, "RETFQ stack frame must be canonical");

  execute_at(cpu, {0x48, 0xcb});
  cpu.regs[Cpu::ESP] = 0x1ffff8;
  cpu.write64(0x1ffff8, kTarget);
  expect_fault(cpu, FaultCode::PageFault, 0, "RETFQ must fault when its CS slot is unmapped");
  check(cpu.cr2 == 0x200000, "lost the address of the absent RETFQ stack slot");
  cpu.regs[Cpu::ESP] = kStack;

  execute_at(cpu, {0xff, 0xd8});
  expect_fault(cpu, FaultCode::InvalidInstruction, 0, "far CALL requires memory");
  execute_at(cpu, {0xff, 0xe8});
  expect_fault(cpu, FaultCode::InvalidInstruction, 0, "far JMP requires memory");

  execute_at(cpu, {0x48, 0xff, 0x1c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 8, 0x0000800000000000ULL, kKernelCode);
  constexpr std::uint64_t marker = 0xaabbccddeeff0099ULL;
  cpu.write64(kStack - 8, marker);
  expect_fault(cpu, FaultCode::ProtectionFault, 0, "far CALL target must be canonical");
  check(cpu.read64(kStack - 8) == marker, "faulting far CALL modified the return stack");

  execute_at(cpu, {0x48, 0xff, 0x1c, 0x25, 0x00, 0x70, 0x01, 0x00});
  pointer(cpu, 8, kTarget, kKernelCode);
  cpu.write64(0x3008, 0x6003); // Make only the first far CALL push writable.
  cpu.write64(0x6008, 0x90003);
  cpu.write64(0x90000, marker);
  cpu.regs[Cpu::ESP] = 0x201008;
  expect_fault(cpu, FaultCode::PageFault, 2, "far CALL must fault on its second stack write");
  check(cpu.cr2 == 0x200ff8 && cpu.read64(0x90000) == marker,
        "far CALL fault must roll back the first stack write");
}
} // namespace

int main() {
  try {
    observed_retfq();
    return_operand_widths();
    return_to_user(false);
    return_to_user(true);
    indirect_calls_and_jumps();
    faults_and_rollback();
    std::cout << "X86_FAR_TRANSFER64_PASS checks=" << checks
              << " retfq=1 retf_widths=1 retf_imm16=1 compatibility=1 outer_cpl=1"
              << " far_call=1 far_jmp=1 stack_rollback=1\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "X86_FAR_TRANSFER64_FAIL " << error.what() << '\n';
    return 1;
  }
}
