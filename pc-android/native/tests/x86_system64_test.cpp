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

constexpr std::uint32_t kEfer = 0xc0000080U;
constexpr std::uint32_t kApicBase = 0x1bU;
constexpr std::uint32_t kStar = 0xc0000081U;
constexpr std::uint32_t kLstar = 0xc0000082U;
constexpr std::uint32_t kCstar = 0xc0000083U;
constexpr std::uint32_t kFmask = 0xc0000084U;
constexpr std::uint32_t kGsBase = 0xc0000101U;
constexpr std::uint32_t kKernelGsBase = 0xc0000102U;
constexpr std::uint64_t kKernel = 0xa000, kUser = 0x11000, kStack = 0x1afe00;
constexpr std::uint64_t kUserGs = 0x18000, kKernelGs = 0x19000;
constexpr std::uint64_t kStarValue = (0x20ULL << 48) | (0x38ULL << 32);
constexpr std::uint64_t kNoncanonical = 0x0000800000000000ULL;
unsigned checks = 0;

void check(bool ok, const char* reason) {
  ++checks;
  if (!ok) throw std::runtime_error(reason);
}

void write(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> code) {
  cpu.memory.write_block(address, code.begin(), code.size());
}

void program(Cpu& cpu, std::initializer_list<std::uint8_t> code, std::uint64_t address = kKernel) {
  write(cpu, address, code);
  cpu.eip = address;
  cpu.halted = false;
  cpu.fault.reset();
}

void step(Cpu& cpu) {
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "unexpected halt");
}

void fault(Cpu& cpu, FaultCode code, const char* reason) {
  const auto regs = cpu.regs;
  const auto segments = cpu.segments;
  const auto flags = cpu.eflags;
  const auto ip = cpu.eip;
  check(cpu.step() == StepResult::Faulted && cpu.fault && cpu.fault->code == code, reason);
  check(cpu.regs == regs && cpu.segments == segments && cpu.eflags == flags && cpu.eip == ip,
        "fault did not preserve architectural state");
}

void boot(Cpu& cpu, std::string_view desired = "long64") {
  const auto disk = cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00, disk.data(), disk.size());
  cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
    if (vector != 0x15 || guest.reg16(Cpu::EAX) != 0x2401)
      guest.fail("unexpected BIOS interrupt");
    guest.a20_enabled = true;
    guest.set_reg8(4, 0);
    guest.set_flag(Cpu::CF, false);
    return true;
  };
  for (unsigned i = 0; i < 4000; ++i) {
    if (cpu.mode_name() == desired && cpu.segments[Cpu::SS] == 0x10) {
      cpu.idtr = {0, 0}; // Report the original fault directly in negative cases.
      if (desired == "long64") {
        cpu.write64(0x1000, 0x2007);
        cpu.write64(0x2000, 0x3007);
        cpu.write64(0x3000, 0x0087);
      }
      return;
    }
    step(cpu);
  }
  throw std::runtime_error("did not reach requested mode");
}

void write_msr(Cpu& cpu, std::uint32_t number, std::uint64_t value) {
  program(cpu, {0x0f, 0x30});
  cpu.regs[Cpu::ECX] = number;
  cpu.regs[Cpu::EDX] = value >> 32;
  cpu.regs[Cpu::EAX] = std::uint32_t(value);
  step(cpu);
}

std::uint64_t read_msr(Cpu& cpu, std::uint32_t number) {
  program(cpu, {0x0f, 0x32});
  cpu.regs[Cpu::ECX] = number;
  step(cpu);
  return (cpu.regs[Cpu::EDX] << 32) | cpu.regs[Cpu::EAX];
}

void configure(Cpu& cpu) {
  boot(cpu);
  write_msr(cpu, kStar, kStarValue);
  write_msr(cpu, kLstar, kKernel);
  write_msr(cpu, kCstar, kKernel + 0x100);
  write_msr(cpu, kFmask, Cpu::IF | Cpu::DF);
  write_msr(cpu, kEfer, cpu.efer | 1U);
  write_msr(cpu, kGsBase, kUserGs);
  write_msr(cpu, kKernelGsBase, kKernelGs);
  check(read_msr(cpu, kStar) == kStarValue && read_msr(cpu, kLstar) == kKernel &&
            read_msr(cpu, kCstar) == kKernel + 0x100 &&
            read_msr(cpu, kFmask) == (Cpu::IF | Cpu::DF) && (read_msr(cpu, kEfer) & 1U),
        "MSRs must round-trip through actual guest WRMSR/RDMSR");
}

void call_and_return() {
  Cpu cpu(4U << 20);
  configure(cpu);
  cpu.gdtr.limit = 0; // STAR uses fixed hidden attributes irrespective of descriptor availability.
  constexpr auto saved_flags = Cpu::IF | Cpu::DF | Cpu::CF | 2U;
  program(cpu, {0x48, 0x0f, 0x07}, kKernel + 0x200); // SYSRETQ to user.
  write(cpu, kUser, {0x0f, 0x05, 0x90});             // SYSCALL, NOP.
  write(cpu, kKernel, {
      0x0f, 0x01, 0xf8,                               // SWAPGS.
      0x65, 0x48, 0x8b, 0x04, 0x25, 0, 0, 0, 0,     // MOV RAX,GS:[0].
      0x0f, 0x01, 0xf8,                               // SWAPGS.
      0x48, 0x0f, 0x07});                             // SYSRETQ.
  constexpr auto marker = 0x97d35af41b2c5608ULL;
  cpu.write64(kKernelGs, marker);
  cpu.regs[Cpu::ECX] = kUser;
  cpu.regs[Cpu::ESP] = kStack;
  cpu.regs[Cpu::R11] = saved_flags | 0x30000U; // SYSRET clears RF and VM.
  step(cpu);
  check(cpu.cpl() == 3 && cpu.eip == kUser && cpu.segments[Cpu::CS] == 0x33 &&
            cpu.segments[Cpu::SS] == 0x2b && cpu.eflags == saved_flags,
        "SYSRETQ must restore RIP, flags and fixed ring-three selectors");

  step(cpu);
  check(cpu.cpl() == 0 && cpu.eip == kKernel && cpu.segments[Cpu::CS] == 0x38 &&
            cpu.segments[Cpu::SS] == 0x40, "SYSCALL must enter the STAR/LSTAR kernel");
  check(cpu.regs[Cpu::ECX] == kUser + 2 && cpu.regs[Cpu::R11] == saved_flags &&
            cpu.eflags == (Cpu::CF | 2U), "SYSCALL must save the caller and apply FMASK");
  check(cpu.regs[Cpu::ESP] == kStack, "SYSCALL must leave RSP with the caller's value");

  step(cpu); step(cpu);
  check(cpu.gs_base == kKernelGs && cpu.regs[Cpu::EAX] == marker,
        "kernel GS memory reference must use the base installed by SWAPGS");
  step(cpu);
  check(cpu.gs_base == kUserGs, "second SWAPGS must restore the original GS base");
  step(cpu);
  check(cpu.cpl() == 3 && cpu.eip == kUser + 2 && cpu.regs[Cpu::ESP] == kStack &&
            cpu.eflags == saved_flags, "system-call round trip must restore caller state");
  step(cpu);
  check(cpu.eip == kUser + 3, "caller must execute its next instruction");
}

void compatibility_and_invalid_modes() {
  Cpu cpu(4U << 20);
  configure(cpu);
  program(cpu, {0x0f, 0x07});
  write(cpu, kUser, {0x0f, 0x05, 0x0f, 0x07, 0x0f, 0x01, 0xf8});
  cpu.regs[Cpu::ECX] = 0xfedcba9800000000ULL | kUser;
  cpu.regs[Cpu::ESP] = 0x1234567800000000ULL | kStack;
  cpu.regs[Cpu::R11] = 2;
  step(cpu);
  check(std::string_view(cpu.mode_name()) == "compat32" && cpu.cpl() == 3 &&
            cpu.eip == kUser && cpu.segments[Cpu::CS] == 0x23 && cpu.segments[Cpu::SS] == 0x2b &&
            cpu.regs[Cpu::ESP] == (0x1234567800000000ULL | kStack),
        "SYSRET without REX.W must enter 32-bit compatibility without changing RSP");
  fault(cpu, FaultCode::InvalidInstruction, "compatibility SYSCALL must raise #UD");
  program(cpu, {0x0f, 0x07}, kUser + 2);
  fault(cpu, FaultCode::InvalidInstruction, "compatibility SYSRET must raise #UD");
  program(cpu, {0x0f, 0x01, 0xf8}, kUser + 4);
  fault(cpu, FaultCode::InvalidInstruction, "compatibility SWAPGS must raise #UD");
}

void privilege_canonical_reset() {
  Cpu cpu(4U << 20);
  configure(cpu);
  for (const auto number : {kLstar, kKernelGsBase}) {
    const auto old = read_msr(cpu, number);
    program(cpu, {0x0f, 0x30});
    cpu.regs[Cpu::ECX] = number;
    cpu.regs[Cpu::EDX] = kNoncanonical >> 32;
    cpu.regs[Cpu::EAX] = 0;
    fault(cpu, FaultCode::ProtectionFault, "noncanonical address MSR write must raise #GP");
    check(read_msr(cpu, number) == old, "faulting WRMSR must preserve prior value");
  }
  program(cpu, {0x48, 0x0f, 0x07});
  cpu.regs[Cpu::ECX] = kNoncanonical;
  fault(cpu, FaultCode::ProtectionFault, "SYSRETQ to noncanonical RCX must raise #GP");

  program(cpu, {0x48, 0x0f, 0x07});
  cpu.regs[Cpu::ECX] = kUser;
  cpu.regs[Cpu::R11] = 2;
  step(cpu);
  program(cpu, {0x0f, 0x01, 0xf8}, kUser);
  fault(cpu, FaultCode::ProtectionFault, "ring-three SWAPGS must raise #GP");
  check(cpu.gs_base == kUserGs, "rejected SWAPGS changed GS");
  program(cpu, {0x48, 0x0f, 0x07}, kUser);
  fault(cpu, FaultCode::ProtectionFault, "ring-three SYSRET must raise #GP");
  program(cpu, {0xf0, 0x0f, 0x05}, kUser);
  fault(cpu, FaultCode::InvalidInstruction, "LOCK SYSCALL must raise #UD");

  boot(cpu);
  check((read_msr(cpu, kEfer) & 1U) == 0 && read_msr(cpu, kKernelGsBase) == 0 &&
            read_msr(cpu, kLstar) == 0 && read_msr(cpu, kCstar) == 0 &&
            read_msr(cpu, kFmask) == 0 && read_msr(cpu, kStar) == 0,
        "CPU reset must clear system-call and kernel GS MSRs");
  program(cpu, {0x0f, 0x05});
  fault(cpu, FaultCode::InvalidInstruction, "disabled SYSCALL must raise #UD");
}

void apic_msr() {
  Cpu cpu(4U << 20);
  boot(cpu);
  constexpr std::uint64_t base = Cpu::kLocalApicAddress;
  constexpr std::uint64_t bsp = 1U << 8, enabled = 1U << 11;
  check(read_msr(cpu, kApicBase) == (base | bsp | enabled) && cpu.apic_global_enabled(),
        "the bootstrap CPU must reset with its local APIC at the board's MMIO base and enabled");
  write_msr(cpu, kApicBase, base);
  check(read_msr(cpu, kApicBase) == base && !cpu.apic_global_enabled(),
        "WRMSR must clear the writable BSP and global-enable bits");
  write_msr(cpu, kApicBase, base | enabled);
  check(read_msr(cpu, kApicBase) == (base | enabled) && cpu.apic_global_enabled(),
        "WRMSR must re-enable xAPIC without rewriting the BSP bit");
  for (const auto invalid : {base | enabled | (1U << 10), base | 1, base | (std::uint64_t{1} << 32)}) {
    program(cpu, {0x0f, 0x30});
    cpu.regs[Cpu::ECX] = kApicBase;
    cpu.regs[Cpu::EDX] = invalid >> 32;
    cpu.regs[Cpu::EAX] = std::uint32_t(invalid);
    fault(cpu, FaultCode::ProtectionFault, "the guest MSR must reject x2APIC and reserved bits");
    check(read_msr(cpu, kApicBase) == (base | enabled), "rejected WRMSR changed APIC state");
  }
  program(cpu, {0x0f, 0x30});
  cpu.regs[Cpu::ECX] = kApicBase;
  cpu.regs[Cpu::EAX] = 0xfee01800U;
  cpu.regs[Cpu::EDX] = 0;
  fault(cpu, FaultCode::UnsupportedState, "the virtual board does not relocate its APIC MMIO page");
  check(read_msr(cpu, kApicBase) == (base | enabled), "rejected APIC relocation changed the base");

  // A warm CPU reset restores the default even after software disables xAPIC.
  write_msr(cpu, kApicBase, base);
  configure(cpu);
  check(read_msr(cpu, kApicBase) == (base | bsp | enabled),
        "CPU reset did not restore xAPIC enable and bootstrap status");
  program(cpu, {0x48, 0x0f, 0x07});
  cpu.regs[Cpu::ECX] = kUser;
  cpu.regs[Cpu::R11] = 2;
  step(cpu);
  program(cpu, {0x0f, 0x32}, kUser);
  cpu.regs[Cpu::ECX] = kApicBase;
  fault(cpu, FaultCode::ProtectionFault, "APIC_BASE RDMSR requires CPL0");
  program(cpu, {0x0f, 0x30}, kUser);
  cpu.regs[Cpu::ECX] = kApicBase;
  fault(cpu, FaultCode::ProtectionFault, "APIC_BASE WRMSR requires CPL0");

  const auto apic_cpuid = [&] {
    program(cpu, {0x0f, 0xa2});
    cpu.regs[Cpu::EAX] = 1;
    step(cpu);
    check(!(cpu.regs[Cpu::ECX] & (1U << 21)), "x2APIC must remain unadvertised");
    return (cpu.regs[Cpu::EDX] & (1U << 9)) != 0;
  };
  boot(cpu);
  check(!apic_cpuid(), "CPU without a board must leave APIC unadvertised");
  cpu.set_local_apic_present(true);
  check(apic_cpuid(), "CPU must announce the attached and enabled xAPIC");
  write_msr(cpu, kApicBase, base | bsp);
  check(!apic_cpuid(), "globally disabled xAPIC must disappear from CPUID");
  boot(cpu);
  check(apic_cpuid(), "reset must preserve the board and restore global enable");
  cpu.set_local_apic_present(false);
  check(!apic_cpuid(), "CPUID must stop announcing the detached xAPIC");
}
} // namespace

int main() {
  try {
    call_and_return();
    compatibility_and_invalid_modes();
    privilege_canonical_reset();
    apic_msr();
    std::cout << "X86_SYSTEM64_PASS checks=" << checks
              << " syscall=1 sysret64=1 sysret32=1 swapgs=1 msr_roundtrip=1 apic_base=1\n";
  } catch (const std::exception& error) {
    std::cerr << "X86_SYSTEM64_FAIL " << error.what() << '\n';
    return 1;
  }
}
