#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

namespace {

constexpr std::uint64_t kCode = 0xa000;
constexpr std::uint64_t kDestination = 0x123456789abcdef0ULL;
constexpr std::uint32_t kFlags = 2 | Cpu::CF | Cpu::PF | Cpu::AF | Cpu::SF | Cpu::OF;
unsigned checks = 0;
unsigned hardware_cases = 0;

void require(bool ok, const char* reason) {
  ++checks;
  if (!ok) throw std::runtime_error(reason);
}

void program(Cpu& cpu, std::initializer_list<std::uint8_t> bytes) {
  cpu.eip = kCode;
  cpu.fault.reset();
  cpu.memory.write_block(kCode, bytes.begin(), bytes.size());
}

void execute(Cpu& cpu) {
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "expected a guest instruction");
}

void boot(Cpu& cpu, std::string_view mode) {
  cpu.reset();
  const auto disk = cosmic_pc::long_mode_boot_disk();
  cpu.memory.write_block(0x7c00, disk.data(), disk.size());
  cpu.interrupt_hook = [](Cpu& c, std::uint8_t vector) {
    if (vector != 0x15 || c.reg16(Cpu::EAX) != 0x2401) return false;
    c.a20_enabled = true;
    c.set_reg8(4, 0);
    c.set_flag(Cpu::CF, false);
    return true;
  };
  for (unsigned steps = 0; steps < 4096; ++steps) {
    if (cpu.mode_name() == mode && (mode != "protected32" || cpu.segments[Cpu::SS] == 0x10))
      return;
    execute(cpu);
  }
  throw std::runtime_error("guest failed to enter the mode under test");
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
template <typename Operand>
void compare_hardware(Operand source, bool reverse, unsigned index, bool expected_zero) {
  Operand result;
  std::uint8_t zero;
  if (reverse)
    __asm__ volatile("bsr %2, %0; setz %1" : "=r"(result), "=qm"(zero) : "r"(source) : "cc");
  else
    __asm__ volatile("bsf %2, %0; setz %1" : "=r"(result), "=qm"(zero) : "r"(source) : "cc");
  require(bool(zero) == expected_zero, "hardware ZF differs");
  if (!expected_zero) require(result == index, "hardware destination differs");
  ++hardware_cases;
}
#endif

void register_cases(Cpu& cpu, unsigned bits) {
  const bool long_mode = cpu.is_long_mode();
  const unsigned source_reg = long_mode ? Cpu::R10 : Cpu::EDX;
  const unsigned dest_reg = long_mode ? Cpu::R9 : Cpu::ECX;
  for (const auto opcode : {std::uint8_t{0xbc}, std::uint8_t{0xbd}}) {
    for (bool prefixed : {false, true}) {
      std::array<std::uint8_t, 6> code{};
      unsigned length = 0;
      if (prefixed) code[length++] = 0xf3;
      if (long_mode && bits == 16) code[length++] = 0x66;
      if (long_mode) code[length++] = bits == 64 ? 0x4d : 0x45;
      code[length++] = 0x0f;
      code[length++] = opcode;
      code[length++] = 0xca;
      cpu.memory.write_block(kCode, code.data(), length);

      for (unsigned bit = 0; bit <= bits; ++bit) {
        const bool zero = bit == bits;
        const auto source = zero ? 0 : std::uint64_t{1} << bit;
        const auto outside_width = bits == 64 ? 0 : UINT64_MAX << bits;
        cpu.eip = kCode;
        cpu.eflags = kFlags | (zero ? 0 : Cpu::ZF);
        cpu.regs[source_reg] = source | outside_width;
        cpu.regs[dest_reg] = kDestination;
        execute(cpu);
        const auto upper = bits == 16 ? kDestination & ~std::uint64_t{0xffff} : 0;
        require(cpu.regs[dest_reg] == (zero ? kDestination : upper | bit), "wrong register destination");
        require(cpu.regs[source_reg] == (source | outside_width), "register source changed");
        require(cpu.eflags == (kFlags | (zero ? Cpu::ZF : 0)), "wrong register flags");
        require(cpu.eip == kCode + length, "incorrect bit scan instruction length");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        if (!prefixed) {
          const bool reverse = opcode == 0xbd;
          if (bits == 16) compare_hardware(std::uint16_t(source), reverse, bit, zero);
          else if (bits == 32) compare_hardware(std::uint32_t(source), reverse, bit, zero);
          else compare_hardware(source, reverse, bit, zero);
        }
#endif
      }
    }
  }
}

void memory_cases(Cpu& cpu) {
  // Protected32 uses the same legacy encoding as the ISO. The ModRM here
  // selects a synthetic memory operand because the report ends at the opcode.
  cpu.write32(0xb000, 0x80000008U);
  program(cpu, {0x0f, 0xbc, 0x05, 0x00, 0xb0, 0x00, 0x00});
  execute(cpu);
  require(cpu.regs[Cpu::EAX] == 3 && !cpu.flag(Cpu::ZF), "BSF must select the lowest set bit");
  program(cpu, {0x0f, 0xbd, 0x05, 0x00, 0xb0, 0x00, 0x00});
  execute(cpu);
  require(cpu.regs[Cpu::EAX] == 31 && !cpu.flag(Cpu::ZF), "BSR must select the highest set bit");
  require(cpu.read32(0xb000) == 0x80000008U, "memory source changed");
}

void cpuid_aliases(Cpu& cpu) {
  program(cpu, {0x0f, 0xa2});
  cpu.regs[Cpu::EAX] = 7;
  cpu.regs[Cpu::ECX] = 0;
  execute(cpu);
  require((cpu.regs[Cpu::EBX] & (1U << 3)) == 0, "declaring BMI1 changes F3 BSF semantics");

  cpu.eip = kCode;
  cpu.regs[Cpu::EAX] = 0x80000001U;
  execute(cpu);
  require((cpu.regs[Cpu::ECX] & (1U << 5)) == 0, "declaring LZCNT changes F3 BSR semantics");
}

void faults(Cpu& cpu) {
  // The boot guest identity maps 2 MiB. Place the operand across that boundary.
  cpu.idtr = {0, 0};
  cpu.regs[Cpu::EBX] = 0x1ffffc;
  cpu.regs[Cpu::EAX] = kDestination;
  cpu.eflags = kFlags;
  const auto registers = cpu.regs;
  program(cpu, {0x48, 0x0f, 0xbc, 0x03});
  require(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::PageFault &&
          cpu.cr2 == 0x200000, "cross-page source read must raise a page fault");
  require(cpu.eip == kCode && cpu.eflags == kFlags && cpu.regs == registers,
          "source fault must preserve instruction restart state");

  program(cpu, {0xf0, 0x48, 0x0f, 0xbd, 0x03});
  cpu.cr2 = 0;
  require(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::InvalidInstruction &&
          cpu.cr2 == 0, "LOCK must raise an invalid-opcode fault before touching the source");
  require(cpu.eip == kCode && cpu.eflags == kFlags && cpu.regs == registers,
          "LOCK fault must preserve instruction restart state");
}

} // namespace

int main() {
  try {
    Cpu real, protected_cpu, long_cpu;
    real.reset();
    register_cases(real, 16);
    boot(protected_cpu, "protected32");
    register_cases(protected_cpu, 32);
    memory_cases(protected_cpu);
    boot(long_cpu, "long64");
    register_cases(long_cpu, 16);
    register_cases(long_cpu, 32);
    register_cases(long_cpu, 64);
    cpuid_aliases(long_cpu);
    faults(long_cpu);
    std::cout << "BSF/BSR: " << checks << " checks, " << hardware_cases
              << " native x86-64 comparisons\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "BSF/BSR: " << error.what() << '\n';
    return 1;
  }
}
