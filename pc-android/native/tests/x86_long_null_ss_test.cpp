#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

constexpr std::uint64_t kCode = 0xa000;
constexpr std::uint64_t kObservedRip = 0x489138;
constexpr std::uint64_t kHighAlias = 0xffff800000000000ULL;
constexpr std::uint64_t kStack = 0x1a6ff0;
constexpr std::uint64_t kHandler = 0x16000;
constexpr std::uint8_t kVector = 0x40;
unsigned checks = 0;

void check(bool ok, const char* reason) {
  ++checks;
  if (!ok) throw std::runtime_error(reason);
}

void step(Cpu& cpu) {
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "unexpected guest stop");
}

void write(Cpu& cpu, std::uint64_t at, std::initializer_list<std::uint8_t> code) {
  cpu.memory.write_block(at, code.begin(), code.size());
}

void program(Cpu& cpu, std::initializer_list<std::uint8_t> code, std::uint64_t at = kCode) {
  write(cpu, at, code);
  cpu.eip = at;
  cpu.halted = false;
  cpu.fault.reset();
}

void boot_to(Cpu& cpu, std::string_view mode) {
  const auto disk = cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00, disk.data(), disk.size());
  cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
    if (vector != 0x15 || guest.reg16(Cpu::EAX) != 0x2401)
      guest.fail("unexpected BIOS call in null-SS fixture");
    guest.a20_enabled = true;
    guest.set_reg8(4, 0);
    guest.set_flag(Cpu::CF, false);
    return true;
  };
  for (unsigned n = 0; n < 5000; ++n) {
    if (std::string_view(cpu.mode_name()) == mode && cpu.segments[Cpu::SS] == 0x10) return;
    step(cpu);
  }
  throw std::runtime_error("guest did not reach requested CPU mode");
}

void install_irq(Cpu& cpu) {
  constexpr std::uint64_t idt = 0x18000;
  constexpr auto offset = kVector * 16U;
  const auto entry = (kHandler & 0xffffU) |
      (std::uint64_t(cpu.segments[Cpu::CS]) << 16) | (0x8eULL << 40) |
      ((kHandler & 0xffff0000ULL) << 32);
  cpu.idtr = {offset + 15, idt};
  cpu.write64(idt + offset, entry);
  cpu.write64(idt + offset + 8, kHandler >> 32);
  write(cpu, kHandler, {0x48, 0xcf}); // IRETQ.
}

void observed_mov_ss() {
  Cpu cpu(8U << 20);
  boot_to(cpu, "compat32");
  check(cpu.read8(cpu.eip) == 0xea, "guest did not reach its compatibility-mode far jump");
  cpu.write64(0x3010, 0x400083); // Identity-map the observed 64-bit instruction address.
  cpu.write64(0x1800, 0x2003);   // Map a canonical high alias for the stack.
  cpu.write32(cpu.eip + 1, kObservedRip);
  write(cpu, kObservedRip, {
      0x66, 0x8e, 0xd0, // MOV SS,AX.
      0x53,             // PUSH RBX.
      0x59,             // POP RCX.
      0x8c, 0xd0        // MOV EAX,SS.
  });
  step(cpu);
  check(cpu.is_long_mode() && cpu.cpl() == 0 && cpu.eip == kObservedRip &&
            cpu.segments[Cpu::SS] == 0x10,
        "guest far jump failed to enter the observed long-mode path");

  install_irq(cpu);
  constexpr auto original_rsp = kHighAlias + kStack;
  constexpr std::uint64_t value = 0xfedcba9876543210ULL;
  cpu.regs[Cpu::ESP] = original_rsp;
  cpu.regs[Cpu::EAX] = 0xfedcba9876540000ULL;
  cpu.regs[Cpu::EBX] = value;
  cpu.eflags = 0x200046U | Cpu::IF;
  const auto before = cpu.regs;
  const auto flags = cpu.eflags;
  const auto retired = cpu.instructions;
  step(cpu);
  check(cpu.eip == kObservedRip + 3 && cpu.instructions == retired + 1 &&
            cpu.segments[Cpu::SS] == 0 && cpu.regs == before && cpu.eflags == flags,
        "observed MOV accepted neither its null selector nor its unchanged architectural state");
  check(!cpu.raise_external_irq(kVector) && !cpu.fault && cpu.eip == kObservedRip + 3,
        "MOV SS must defer a pending IRQ at the immediately following boundary");
  step(cpu);
  constexpr auto pushed_rsp = original_rsp - 8;
  check(cpu.regs[Cpu::ESP] == pushed_rsp && cpu.read64(kStack - 8) == value,
        "PUSH with a null SS must use all 64 stack-address bits");

  check(cpu.raise_external_irq(kVector) && cpu.eip == kHandler && !cpu.flag(Cpu::IF),
        "the pending IRQ must enter its real guest gate after one instruction");
  const auto frame = (pushed_rsp & ~std::uint64_t{15}) - 40;
  const auto physical = frame - kHighAlias;
  check(cpu.regs[Cpu::ESP] == frame && cpu.read64(physical) == kObservedRip + 4 &&
            cpu.read64(physical + 24) == pushed_rsp && cpu.read64(physical + 32) == 0,
        "64-bit IRQ frame must save RIP and null SS with the full interrupted RSP");
  step(cpu);
  check(cpu.eip == kObservedRip + 4 && cpu.regs[Cpu::ESP] == pushed_rsp &&
            cpu.segments[Cpu::SS] == 0 && cpu.eflags == flags,
        "IRETQ must restore the interrupted null stack and flags");
  step(cpu);
  check(cpu.regs[Cpu::ECX] == value && cpu.regs[Cpu::ESP] == original_rsp,
        "POP after IRETQ must recover the original guest stack value");
  step(cpu);
  check(cpu.regs[Cpu::EAX] == 0, "reading SS must return the null selector");
}

void expect_null_rejected(Cpu& cpu, std::uint16_t selector) {
  cpu.idtr = {0, 0}; // Observe the rejected operation before any guest handler.
  program(cpu, {0x66, 0x8e, 0xd0});
  cpu.set_reg16(Cpu::EAX, selector);
  const auto before = cpu.regs;
  const auto flags = cpu.eflags;
  const auto old_ss = cpu.segments[Cpu::SS];
  const auto retired = cpu.instructions;
  check(cpu.step() == StepResult::Faulted && cpu.fault &&
            cpu.fault->code == FaultCode::ProtectionFault && cpu.fault->error_code == 0,
        "invalid null SS selector or mode must raise #GP(0)");
  check(cpu.eip == kCode && cpu.fault->eip == kCode && cpu.regs == before &&
            cpu.eflags == flags && cpu.segments[Cpu::SS] == old_ss && cpu.instructions == retired,
        "invalid MOV SS must leave architectural registers and IP intact");
}

void rejected_selectors() {
  for (const auto* mode : {"protected32", "compat32", "long64"}) {
    Cpu cpu(8U << 20);
    boot_to(cpu, mode);
    if (!cpu.is_long_mode()) expect_null_rejected(cpu, 0);
    else for (const auto selector : {1, 2, 3}) expect_null_rejected(cpu, selector);
  }

  Cpu cpu(8U << 20);
  boot_to(cpu, "long64");
  constexpr std::uint64_t gdt = 0x14000;
  cpu.gdtr = {0x2f, gdt};
  cpu.write64(gdt + 0x18, 0x00af9b000000ffffULL);
  cpu.write64(gdt + 0x20, 0x00affa000000ffffULL);
  cpu.write64(gdt + 0x28, 0x00cff2000000ffffULL);
  for (const auto address : {0x1000U, 0x2000U, 0x3000U})
    cpu.write64(address, cpu.read64(address) | 4);
  constexpr std::uint64_t frame = 0x6000;
  cpu.write64(frame, kCode + 0x100);
  cpu.write64(frame + 8, 0x23);
  cpu.write64(frame + 16, 2);
  cpu.write64(frame + 24, 0x7000);
  cpu.write64(frame + 32, 0x2b);
  program(cpu, {0x48, 0xcf}); // IRETQ to a real guest CPL3 context.
  cpu.regs[Cpu::ESP] = frame;
  step(cpu);
  check(cpu.is_long_mode() && cpu.cpl() == 3 && cpu.segments[Cpu::SS] == 0x2b,
        "guest IRETQ failed to enter CPL3");
  for (const auto selector : {0, 1, 2, 3}) expect_null_rejected(cpu, selector);
}

void lss_load() {
  Cpu cpu(8U << 20);
  boot_to(cpu, "long64");
  install_irq(cpu);
  constexpr std::uint64_t pointer = 0x17000;
  constexpr auto stack = 0x190800;
  cpu.write64(pointer, stack);
  cpu.write16(pointer + 8, 0);
  cpu.set_flag(Cpu::IF, true);
  program(cpu, {0x48, 0x0f, 0xb2, 0x24, 0x25, 0x00, 0x70, 0x01, 0x00}); // LSS RSP,[17000h].
  step(cpu);
  check(cpu.eip == kCode + 9 && cpu.segments[Cpu::SS] == 0 && cpu.regs[Cpu::ESP] == stack,
        "LSS must atomically load the null selector and full stack pointer");
  check(cpu.raise_external_irq(kVector) && cpu.eip == kHandler && cpu.read64(stack - 40) == kCode + 9,
        "LSS must leave interrupts deliverable immediately on the new stack");
  step(cpu);
  check(cpu.eip == kCode + 9 && cpu.regs[Cpu::ESP] == stack && cpu.segments[Cpu::SS] == 0,
        "IRETQ from an IRQ following LSS must restore the null stack");
}
} // namespace

int main() {
  try {
    observed_mov_ss();
    rejected_selectors();
    lss_load();
    std::cout << "X86_LONG_NULL_SS_PASS checks=" << checks
              << " compat32_to_long64=1 mov_ss=1 lss=1 irq_shadow=1 iretq=1\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "X86_LONG_NULL_SS_FAIL " << error.what() << '\n';
    return 1;
  }
}
