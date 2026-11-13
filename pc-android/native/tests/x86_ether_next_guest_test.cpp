#include "../pc_machine.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;
using cosmic_pc::StepResult;
using mundo_x::ether_next::Registers;

constexpr std::uint64_t entry = 0xa000;
constexpr std::uint64_t data = 0xb000;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

void boot(PcMachine& machine, bool accelerated) {
  machine.cpu.enable_ether_next(accelerated);
  machine.boot_long_mode_demo();
  machine.run(12000);
  auto& cpu = machine.cpu;
  if (cpu.fault) throw std::runtime_error(cpu.fault->describe());
  require(cpu.is_long_mode() && cpu.halted, "synthetic guest must boot into long mode");
  cpu.cr4 |= 0x200U;
  cpu.eflags = 2U | Cpu::CF | Cpu::ZF | Cpu::IF;
  cpu.mxcsr = 0x9fc0U;
  for (unsigned reg = 0; reg < cpu.xmm.size(); ++reg)
    for (unsigned byte = 0; byte < cpu.xmm[reg].size(); ++byte)
      cpu.xmm[reg][byte] = static_cast<std::uint8_t>(reg * 37 + byte * 31 + 1);
  for (unsigned byte = 0; byte < 16; ++byte)
    cpu.write8(data + byte, static_cast<std::uint8_t>(0x80U + byte));
}

void install(Cpu& cpu, const std::vector<std::uint8_t>& code) {
  cpu.memory.write_block(entry, code.data(), code.size());
  cpu.eip = entry;
  cpu.halted = false;
  cpu.fault.reset();
}

void compare_state(const Cpu& baseline, const Cpu& optimized) {
  require(baseline.xmm == optimized.xmm, "guest XMM state changed under Ether");
  require(baseline.regs == optimized.regs && baseline.segments == optimized.segments,
          "guest registers changed under Ether");
  require(baseline.eip == optimized.eip && baseline.instructions == optimized.instructions,
          "guest retirement changed under Ether");
  require(baseline.eflags == optimized.eflags && baseline.mxcsr == optimized.mxcsr,
          "guest flags or MXCSR changed under Ether");
  require(baseline.x87 == optimized.x87 && baseline.fpu_status == optimized.fpu_status,
          "guest x87 state changed under Ether");
  require(baseline.halted == optimized.halted, "guest stop state changed under Ether");
}

void run(PcMachine& baseline, PcMachine& optimized, const std::vector<std::uint8_t>& code,
         unsigned instruction_count) {
  install(baseline.cpu, code);
  install(optimized.cpu, code);
  unsigned baseline_ticks = 0, optimized_ticks = 0;
  baseline.device_step_hook = [&] { ++baseline_ticks; };
  optimized.device_step_hook = [&] { ++optimized_ticks; };
  for (unsigned step = 0; step < instruction_count; ++step) {
    baseline.run(1);
    optimized.run(1);
    require(!baseline.cpu.fault && !optimized.cpu.fault, "unexpected guest instruction fault");
    compare_state(baseline.cpu, optimized.cpu);
  }
  baseline.device_step_hook = {};
  optimized.device_step_hook = {};
  require(baseline_ticks == instruction_count && optimized_ticks == instruction_count,
          "Ether skipped a device boundary");
}
} // namespace

int main() {
  try {
    PcMachine baseline(16 * 1024 * 1024);
    PcMachine optimized(16 * 1024 * 1024);
    boot(baseline, false);
    boot(optimized, true);
    constexpr unsigned supported = 7;
    const std::vector<std::uint8_t> code{
      0x66,0x45,0x0f,0x6f,0xfa,     // MOVDQA XMM15,XMM10
      0x66,0x45,0x0f,0xfe,0xf9,     // PADDD XMM15,XMM9
      0x66,0x45,0x0f,0xfa,0xf9,     // PSUBD XMM15,XMM9
      0x66,0x45,0x0f,0xef,0xff,     // PXOR XMM15,XMM15
      0xf3,0x44,0x0f,0x7f,0xf8,     // MOVDQU XMM0,XMM15
      0x66,0x0f,0xdf,0xc1,          // PANDN XMM0,XMM1
      0x66,0x0f,0xeb,0xc2,          // POR XMM0,XMM2
      0x66,0x0f,0xfc,0x04,0x25,0x00,0xb0,0x00,0x00, // PADDB XMM0,[b000h]
      0xf4,
    };
    run(baseline, optimized, code, supported + 2);
    // Reuse bytes and cache, but vary input registers before the next call.
    baseline.cpu.xmm[1].fill(0x63);
    optimized.cpu.xmm[1].fill(0x63);
    run(baseline, optimized, code, supported + 2);
    auto stats = optimized.cpu.ether_next_stats();
    if (Cpu::ether_next_native_host()) {
      require(stats.native_instructions == supported * 2, "ARM64 did not execute the eligible instructions");
      require(stats.cache.misses == supported && stats.cache.hits == supported,
              "exact-byte translation cache did not reuse the second execution");
    } else {
      require(stats.native_instructions == 0 && stats.cache.misses == 0,
              "non-ARM64 host attempted to compile an instruction");
    }

    // The same RIP with changed bytes must execute a different instruction.
    run(baseline, optimized, {0x66,0x0f,0xdb,0xc1,0xf4}, 2);
    const auto rewritten = optimized.cpu.ether_next_stats();
    require(rewritten.native_instructions == stats.native_instructions +
                (Cpu::ether_next_native_host() ? 1 : 0),
            "rewritten instruction was not independently eligible");
    require(rewritten.cache.misses == stats.cache.misses +
                (Cpu::ether_next_native_host() ? 1 : 0),
            "rewritten instruction did not require its own exact-byte lookup");

    unsigned traces = 0;
    optimized.cpu.step_trace_hook = [&](const Cpu::StepTrace&) { ++traces; };
    run(baseline, optimized, code, supported + 2);
    require(traces == supported + 2 &&
                optimized.cpu.ether_next_stats().native_instructions == rewritten.native_instructions,
            "observed instruction used the native path or lost a trace");
    optimized.cpu.step_trace_hook = {};

    // Disabling OSFXSR must raise the original exception before Ether is called.
    const std::vector<std::uint8_t> pxor{0x66,0x0f,0xef,0xc0};
    for (Cpu* cpu : {&baseline.cpu, &optimized.cpu}) {
      install(*cpu, pxor);
      cpu->cr4 &= ~0x200U;
      const Registers input = cpu->xmm;
      require(cpu->step() == StepResult::Faulted && cpu->fault &&
                  cpu->fault->code == cosmic_pc::FaultCode::InvalidInstruction &&
                  cpu->xmm == input && cpu->eip == entry,
              "disabled OSFXSR did not preserve the interpreter's fault and state");
      cpu->cr4 |= 0x200U;
    }
    require(optimized.cpu.ether_next_stats().native_instructions == rewritten.native_instructions,
            "faulting SSE2 instruction used Ether");

    // Host optimization does not change any advertised guest CPU capability.
    baseline.cpu.regs[Cpu::EAX] = optimized.cpu.regs[Cpu::EAX] = 1;
    run(baseline, optimized, {0x0f,0xa2,0xf4}, 2);
    require(optimized.cpu.regs[Cpu::EDX] == baseline.cpu.regs[Cpu::EDX] &&
                (optimized.cpu.regs[Cpu::EDX] & (1U << 26)),
            "Ether Next changed the canonical verified SSE2 profile");
    stats = optimized.cpu.ether_next_stats();
    std::cout << "REVERSE_WINDOWS_ETHER_NEXT_GUEST_PASS native_host=" << Cpu::ether_next_native_host()
              << " native_instructions=" << stats.native_instructions
              << " cache_hits=" << stats.cache.hits << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "REVERSE_WINDOWS_ETHER_NEXT_GUEST_FAIL " << error.what() << '\n';
    return 1;
  }
}
