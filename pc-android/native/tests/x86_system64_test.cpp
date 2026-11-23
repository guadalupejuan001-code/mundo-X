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

void single_step_system_calls() {
  Cpu cpu(4U << 20);
  configure(cpu);
  write_msr(cpu, kFmask, Cpu::TF);
  program(cpu, {0x48, 0x0f, 0x07}); // SYSRETQ.
  write(cpu, kUser, {0x9d, 0x0f, 0x05, 0x90}); // POPFQ; SYSCALL; NOP.
  cpu.write64(kStack, Cpu::TF | 2U);
  cpu.regs[Cpu::ECX] = kUser;
  cpu.regs[Cpu::ESP] = kStack;
  cpu.regs[Cpu::R11] = 2;
  step(cpu);
  step(cpu);
  check(cpu.flag(Cpu::TF) && cpu.eip == kUser + 1, "POPFQ must arm the following instruction");
  step(cpu);
  check(cpu.cpl() == 0 && !cpu.flag(Cpu::TF) && cpu.eip == kKernel &&
            cpu.regs[Cpu::R11] == (Cpu::TF | 2U), "SYSCALL must apply FMASK before debug delivery");
  const auto retired = cpu.instructions;
  check(cpu.step() == StepResult::Faulted && cpu.fault && cpu.fault->code == FaultCode::DebugException &&
            cpu.cpl() == 3 && cpu.eip == kUser + 3 && cpu.instructions == retired + 1,
        "SYSRETQ restoring TF must trap before executing its target");
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

void sysenter_registers() {
  constexpr std::uint32_t cs = 0x174, esp = 0x175, eip = 0x176;
  constexpr std::uint64_t kernel_stack = 0xfffff80160004000ULL;
  constexpr std::uint64_t kernel_entry = 0xfffff80110001000ULL;
  Cpu cpu(4U << 20);
  cpu.use_windows_x64_profile();
  boot(cpu);
  write_msr(cpu, cs, 0x123456789abc0010ULL);
  write_msr(cpu, esp, kernel_stack);
  write_msr(cpu, eip, kernel_entry);
  check(read_msr(cpu, cs) == 0x9abc0010U && read_msr(cpu, esp) == kernel_stack &&
            read_msr(cpu, eip) == kernel_entry, "SYSENTER MSRs must preserve the architecturally readable bits");

  for (auto number : {esp, eip}) {
    program(cpu, {0x0f, 0x30});
    cpu.regs[Cpu::ECX] = number;
    cpu.regs[Cpu::EAX] = std::uint32_t(kNoncanonical);
    cpu.regs[Cpu::EDX] = kNoncanonical >> 32;
    fault(cpu, FaultCode::ProtectionFault, "WRMSR must reject a noncanonical SYSENTER pointer");
    check(read_msr(cpu, number) == (number == esp ? kernel_stack : kernel_entry),
          "failed WRMSR changed the SYSENTER pointer");
  }

  // The real x64 boot writes zero to CS, EIP and ESP in this order.
  program(cpu, {0x33, 0xc0, 0x33, 0xd2,
                0xb9, 0x74, 0x01, 0, 0, 0x0f, 0x30,
                0xb9, 0x76, 0x01, 0, 0, 0x0f, 0x30,
                0xb9, 0x75, 0x01, 0, 0, 0x0f, 0x30});
  for (unsigned i = 0; i < 8; ++i) step(cpu);
  check(cpu.eip == kKernel + 25 && read_msr(cpu, cs) == 0 && read_msr(cpu, esp) == 0 &&
            read_msr(cpu, eip) == 0, "the kernel must be able to clear all three SYSENTER MSRs");

  program(cpu, {0x0f, 0xa2});
  cpu.regs[Cpu::EAX] = 1;
  step(cpu);
  check(!(cpu.regs[Cpu::EDX] & (1U << 11)), "register storage alone must not advertise SEP");
  write_msr(cpu, cs, 0x10);
  write_msr(cpu, esp, kernel_stack);
  write_msr(cpu, eip, kernel_entry);
  boot(cpu);
  check(read_msr(cpu, cs) == 0 && read_msr(cpu, esp) == 0 && read_msr(cpu, eip) == 0,
        "CPU reset must clear SYSENTER MSRs");
}

void tsc_adjust_register() {
  constexpr std::uint32_t tsc = 0x10, adjust = 0x3b;
  constexpr auto ticks_per_cycle = Cpu::kTscTicksPerCycle;
  constexpr auto negative_adjust = std::uint64_t{0} - 327;
  Cpu cpu(4U << 20);
  cpu.reset();
  const auto leaf7 = [&](unsigned subleaf) {
    program(cpu, {0x0f, 0xa2});
    cpu.regs[Cpu::EAX] = 7;
    cpu.regs[Cpu::ECX] = subleaf;
    step(cpu);
    return cpu.regs[Cpu::EBX];
  };
  check(leaf7(0) == 0, "generic CPU does not advertise IA32_TSC_ADJUST");
  program(cpu, {0x0f, 0x32});
  cpu.regs[Cpu::ECX] = adjust;
  fault(cpu, FaultCode::UnsupportedState, "generic CPU rejects the unsupported MSR");

  cpu.use_windows_x64_profile();
  cpu.reset();
  Cpu peer(cpu.memory);
  peer.use_windows_x64_profile();
  peer.reset();
  std::uint64_t cycles = 100'000;
  const auto raw_tsc = [&] { return cycles * ticks_per_cycle; };
  cpu.set_shared_clock(&cycles);
  peer.set_shared_clock(&cycles);
  unsigned clock_samples = 0;
  cpu.before_timestamp_read_hook = [&] { ++clock_samples; };
  const auto combined = [](const Cpu& value) { return value.regs[Cpu::EDX] << 32 | value.regs[Cpu::EAX]; };
  const auto rdtsc = [&](Cpu& value) {
    program(value, {0x0f, 0x31});
    step(value);
    return combined(value);
  };

  check(leaf7(0) == 2 && leaf7(1) == 0, "CPUID.07H.0 enables exactly the implemented TSC adjust bit");
  check(read_msr(cpu, adjust) == 0 && read_msr(peer, adjust) == 0, "Power-on TSC adjustment is zero on both CPUs");
  write_msr(cpu, adjust, negative_adjust);
  check(read_msr(cpu, adjust) == negative_adjust && clock_samples == 0,
        "TSC adjustment reads and writes retain all bits without sampling the raw clock");
  check(rdtsc(cpu) == raw_tsc() + negative_adjust && rdtsc(peer) == raw_tsc(),
        "Negative adjustment modifies the caller's TSC and leaves its peer unchanged");
  const auto absolute = raw_tsc() + 0x123456789ULL;
  write_msr(cpu, tsc, absolute);
  check(read_msr(cpu, adjust) == absolute - raw_tsc() && read_msr(cpu, tsc) == absolute && clock_samples == 3,
        "Writing the absolute TSC also changes the adjustment by the same delta");
  cycles += 80;
  write_msr(cpu, adjust, 73);
  check(rdtsc(cpu) == raw_tsc() + 73 && rdtsc(peer) == raw_tsc() && cycles == 100'080,
        "Replacing the adjustment applies its delta to the running counter without moving shared time");
  const auto before_startup = cpu.tsc_access_stats();
  check(before_startup.rdtsc == 2 && before_startup.read_tsc == 1 && before_startup.write_tsc == 1 &&
        before_startup.read_adjust == 3 && before_startup.write_adjust == 2, "Committed TSC operation counters");

  cpu.reset_for_startup_ipi(0, 0);
  cpu.reset_for_startup_ipi(0xa00, 0);
  write(cpu, kKernel, {0x0f, 0x32, 0x0f, 0x31});
  cpu.regs[Cpu::ECX] = adjust;
  step(cpu);
  check(combined(cpu) == 73, "INIT and SIPI preserve the relative TSC offset");
  step(cpu);
  check(combined(cpu) == raw_tsc() + 73 && cpu.tsc_access_stats().write_adjust == 2,
        "INIT and SIPI preserve shared clock, local offset and per-boot observation");
  cpu.reset();
  check(cpu.tsc_access_stats().rdtsc == 0 && cpu.tsc_access_stats().write_adjust == 0 &&
        read_msr(cpu, adjust) == 0 && rdtsc(cpu) == raw_tsc(),
        "CPU RESET clears adjustment and observations and leaves board-owned raw time to the board");

  Cpu local(4U << 20);
  local.use_windows_x64_profile();
  local.reset();
  write_msr(local, adjust, 311);
  local.advance_idle_clock(127);
  const auto local_tsc = local.clock_cycles() * ticks_per_cycle + 311;
  local.reset_for_startup_ipi(0, 0);
  check(rdtsc(local) == local_tsc && read_msr(local, adjust) == 311,
        "A standalone INIT preserves the raw clock without folding it into IA32_TSC_ADJUST");
  local.reset();
  check(rdtsc(local) == 0 && read_msr(local, adjust) == 0, "Standalone RESET clears raw TSC and adjustment");

  configure(cpu);
  write_msr(cpu, adjust, 137);
  program(cpu, {0x48, 0x0f, 0x07}); // SYSRETQ to CPL3.
  cpu.regs[Cpu::ECX] = kUser;
  cpu.regs[Cpu::R11] = 2;
  step(cpu);
  program(cpu, {0x0f, 0x30}, kUser);
  cpu.regs[Cpu::ECX] = adjust;
  cpu.regs[Cpu::EAX] = 3;
  cpu.regs[Cpu::EDX] = 0;
  fault(cpu, FaultCode::ProtectionFault, "Unprivileged WRMSR cannot change the TSC adjustment");
  program(cpu, {0x0f, 0x32}, kUser);
  cpu.regs[Cpu::ECX] = adjust;
  fault(cpu, FaultCode::ProtectionFault, "Unprivileged RDMSR cannot read the TSC adjustment");
  program(cpu, {0x0f, 0x31}, kUser);
  step(cpu);
  check(combined(cpu) == raw_tsc() + 137 && cpu.tsc_access_stats().write_adjust == 1 &&
        cpu.tsc_access_stats().read_adjust == 0, "Faulting MSR operations leave offset and counters intact");
  std::cout << "PRIME_TSC_ADJUST_PASS cpuid=1 shared_clock=1 signed=1 absolute=1 init=1 reset=1 privilege=1\n";
}

void cache_writeback() {
  Cpu cpu(4U << 20);
  constexpr auto data = 0x10000;
  constexpr auto marker = 0x78563412U;
  const auto verify = [&] {
    program(cpu, {0x0f, 0x09, 0x90});
    cpu.write32(data, marker);
    cpu.regs[Cpu::EAX] = 0x9abcdef0U;
    cpu.eflags |= Cpu::CF | Cpu::DF;
    const auto registers = cpu.regs;
    const auto flags = cpu.eflags;
    const auto count = cpu.instructions;
    step(cpu);
    check(cpu.eip == kKernel + 2 && cpu.instructions == count + 1 &&
              cpu.regs == registers && cpu.eflags == flags && cpu.read32(data) == marker,
          "WBINVD must retire once and preserve guest RAM, registers and flags");
    step(cpu);
    check(cpu.eip == kKernel + 3, "execution did not continue after WBINVD");
  };
  verify();
  configure(cpu);
  verify();

  program(cpu, {0xf0, 0x0f, 0x09});
  fault(cpu, FaultCode::InvalidInstruction, "LOCK WBINVD must raise #UD even in ring zero");
  program(cpu, {0x48, 0x0f, 0x07}); // SYSRETQ to ring three.
  cpu.regs[Cpu::ECX] = kUser;
  cpu.regs[Cpu::R11] = 2;
  step(cpu);
  program(cpu, {0x0f, 0x09}, kUser);
  fault(cpu, FaultCode::ProtectionFault, "ring-three WBINVD must raise #GP(0)");
  check(cpu.fault->error_code == 0, "WBINVD #GP must have error code zero");
}
} // namespace

int main() {
  try {
    call_and_return();
    single_step_system_calls();
    compatibility_and_invalid_modes();
    privilege_canonical_reset();
    apic_msr();
    sysenter_registers();
    tsc_adjust_register();
    cache_writeback();
    Cpu debug_cpu(4U << 20);
    boot(debug_cpu);
    program(debug_cpu, {0x41,0x0f,0x23,0xc0, 0x41,0x0f,0x21,0xc1, 0x0f,0x21,0xfa});
    debug_cpu.regs[Cpu::R8] = 0x112233448abcdef0ULL;
    step(debug_cpu); // MOV DR0, R8.
    step(debug_cpu); // MOV R9, DR0.
    step(debug_cpu); // MOV RDX, DR7.
    check(debug_cpu.regs[Cpu::R9] == debug_cpu.regs[Cpu::R8] &&
          debug_cpu.regs[Cpu::EDX] == 0x400, "MOV DR retained virtual debug-register state");
    std::cout << "X86_SYSTEM64_PASS checks=" << checks
              << " syscall=1 sysret64=1 sysret32=1 swapgs=1 msr_roundtrip=1 sysenter_msrs=1 apic_base=1 wbinvd=1\n";
  } catch (const std::exception& error) {
    std::cerr << "X86_SYSTEM64_FAIL " << error.what() << '\n';
    return 1;
  }
}
