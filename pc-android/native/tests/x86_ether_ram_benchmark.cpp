#include "pc_machine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;

constexpr std::uint64_t code = 0xa000, first = 0x80000, other = 0x81000;
constexpr unsigned iterations = 300'000, steps = 6 * iterations + 1;
constexpr std::array<std::uint8_t, 21> loop{
    0x4c,0x89,0xe1,        // MOV RCX,R12
    0x4a,0x29,0x04,0xc9,   // Observed SUB [RCX+R9*8],RAX
    0x4c,0x89,0xe9,        // MOV RCX,R13
    0x4e,0x29,0x1c,0xc9,   // Observed SUB [RCX+R9*8],R11
    0x48,0x83,0xea,0x01,   // SUB RDX,1
    0x75,0xec,0xf4};       // JNZ; HLT

void measure(bool same_word) {
  PcMachine machine;
  machine.boot_long_mode_demo();
  machine.run(4000);
  auto& cpu = machine.cpu;
  if (!cpu.is_long_mode() || !cpu.halted || cpu.fault)
    throw std::runtime_error("long-mode fixture failed");
  cpu.enable_page_translation_cache();
  cpu.enable_ether_arm64_blocks(true);
  machine.enable_boot_acceleration();
  cpu.memory.write_block(code, loop.data(), loop.size());

  constexpr std::uint64_t initial = 0x9745397ea123b111ULL;
  const auto second = same_word ? first : other;
  std::array<std::int64_t, 5> wall_ns{}, cpu_ns{};
  std::uint64_t blocks = 0, native_steps = 0;
  for (unsigned sample = 0; sample <= wall_ns.size(); ++sample) {
    cpu.halted = false;
    cpu.eip = code;
    cpu.eflags = 2 | Cpu::IF;
    cpu.regs[Cpu::R12] = first - 8;
    cpu.regs[Cpu::R13] = second - 8;
    cpu.regs[Cpu::R9] = 1;
    cpu.regs[Cpu::EAX] = 19;
    cpu.regs[Cpu::R11] = 7;
    cpu.regs[Cpu::EDX] = iterations;
    cpu.write64(first, initial);
    cpu.write64(other, initial);
    const auto retired = cpu.instructions;
    const auto before = cpu.ether_arm64_stats();
    const auto start = std::chrono::steady_clock::now();
    const auto cpu_start = std::clock();
    for (unsigned remaining = steps; remaining;) {
      const auto slice = std::min(remaining, 50'000U);
      machine.run(slice);
      remaining -= slice;
    }
    const auto cpu_end = std::clock();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    blocks = cpu.ether_arm64_stats().blocks - before.blocks;
    native_steps = cpu.ether_arm64_stats().steps - before.steps;
    const auto first_expected = initial - (same_word ? 26 : 19) * iterations;
    const auto other_expected = initial - (same_word ? 0 : 7) * iterations;
    if (!cpu.halted || cpu.fault || cpu.instructions - retired != steps ||
        cpu.read64(first) != first_expected || cpu.read64(other) != other_expected ||
        cpu.regs[Cpu::EDX] != 0 || cpu.eflags != (2 | Cpu::IF | Cpu::ZF | Cpu::PF) ||
        native_steps * 100 < steps * 99 || cpu_start == std::clock_t(-1) || cpu_end < cpu_start)
      throw std::runtime_error("RAM benchmark diverged or missed native execution");
    if (sample == 0) continue; // Warm the code cache before collecting samples.
    wall_ns[sample - 1] = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    cpu_ns[sample - 1] = std::int64_t(cpu_end - cpu_start) * 1'000'000'000 / CLOCKS_PER_SEC;
  }
  std::sort(wall_ns.begin(), wall_ns.end());
  std::sort(cpu_ns.begin(), cpu_ns.end());
  std::cout << "PRIME_ETHER_RAM_BENCH variant=" << (same_word ? "same" : "separate")
            << " steps=" << steps << " native_steps=" << native_steps << " blocks=" << blocks
            << " wall_median_ns=" << wall_ns[2] << " cpu_median_ns=" << cpu_ns[2] << '\n';
}
}  // namespace

int main() {
  if (!Cpu::ether_arm64_host()) return 1;
  try {
    measure(true);
    measure(false);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
