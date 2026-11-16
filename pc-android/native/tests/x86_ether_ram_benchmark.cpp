#include "pc_machine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <iostream>
#include <stdexcept>
#include <string_view>

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

void measure_mixed(bool counted) {
  constexpr unsigned rounds = 100'000, guest_steps = 11 * rounds + 1;
  constexpr std::uint64_t stack = 0x90000, word_base = other + 0x18;
  constexpr std::uint64_t initial = 0x9745397ea123b111ULL;
  constexpr std::uint64_t addend = 0x01234567, multiplier = 0x2431;
  constexpr std::array<std::uint8_t, 34> body{
      0x48,0x81,0xc3,0x67,0x45,0x23,0x01, // ADD RBX,0x01234567
      0x4a,0x29,0x04,0xc9,                 // Observed SUB [RCX+R9*8],RAX
      0x4e,0x29,0x1c,0xc9,                 // Observed SUB [RCX+R9*8],R11
      0x66,0x89,0x48,0xe8,                 // Observed MOV [RAX-0x18],CX
      0xe8,0x68,0,0,0,                     // CALL code+0x80
      0x83,0xea,1,                         // SUB EDX,1
      0x0f,0x85,0xdf,0xff,0xff,0xff,       // JNZ back to the loop
      0xf4};
  constexpr std::array<std::uint8_t, 10> callee{
      0x53,0x4c,0x69,0xf3,0x31,0x24,0,0,0x5b,0xc3}; // PUSH RBX; IMUL R14,RBX,0x2431; POP RBX; RET

  PcMachine machine;
  machine.boot_long_mode_demo();
  machine.run(4000);
  auto& cpu = machine.cpu;
  if (!cpu.is_long_mode() || !cpu.halted || cpu.fault)
    throw std::runtime_error("scalar long-mode fixture failed");
  cpu.enable_page_translation_cache();
  cpu.enable_ether_arm64_blocks(false);
  machine.enable_boot_acceleration(counted);
  cpu.memory.write_block(code, body.data(), body.size());
  cpu.memory.write_block(code + 0x80, callee.data(), callee.size());

  std::array<std::int64_t, 5> wall_ns{}, cpu_ns{};
  std::uint64_t counter_steps = 0, counter_batches = 0;
  for (unsigned sample = 0; sample <= wall_ns.size(); ++sample) {
    cpu.halted = false;
    cpu.eip = code;
    cpu.eflags = 2 | Cpu::IF;
    cpu.regs[Cpu::EAX] = word_base;
    cpu.regs[Cpu::EBX] = 5;
    cpu.regs[Cpu::ECX] = first - 8;
    cpu.regs[Cpu::EDX] = rounds;
    cpu.regs[Cpu::ESP] = stack;
    cpu.regs[Cpu::R9] = 1;
    cpu.regs[Cpu::R11] = 7;
    cpu.write64(first, initial);
    cpu.write64(other, 0);
    const auto retired = cpu.instructions;
    const auto before = machine.boot_acceleration();
    const auto start = std::chrono::steady_clock::now();
    const auto cpu_start = std::clock();
    for (unsigned remaining = guest_steps; remaining;) {
      const auto slice = std::min(remaining, 50'000U);
      machine.run(slice);
      remaining -= slice;
    }
    const auto cpu_end = std::clock();
    const auto elapsed = std::chrono::steady_clock::now() - start;
    counter_steps = machine.boot_acceleration().counter_loop_steps - before.counter_loop_steps;
    counter_batches = machine.boot_acceleration().counter_loop_batches - before.counter_loop_batches;
    const auto expected_rbx = 5 + addend * rounds;
    if (!cpu.halted || cpu.fault || cpu.instructions - retired != guest_steps ||
        cpu.eip != code + body.size() || cpu.ether_arm64_stats().steps != 0 || cpu.regs[Cpu::EDX] != 0 ||
        cpu.regs[Cpu::EBX] != expected_rbx || cpu.regs[Cpu::R14] != expected_rbx * multiplier ||
        cpu.regs[Cpu::ESP] != stack || cpu.read64(first) != initial - (word_base + 7) * rounds ||
        cpu.read16(other) != std::uint16_t(first - 8) ||
        cpu.read64(stack - 16) != expected_rbx || cpu.read64(stack - 8) != code + 0x18 ||
        cpu.eflags != (2 | Cpu::IF | Cpu::ZF | Cpu::PF) || cpu_start == std::clock_t(-1) || cpu_end < cpu_start)
      throw std::runtime_error("mixed x86 interpreter benchmark changed the guest result");
    if (sample == 0) continue;
    wall_ns[sample - 1] = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    cpu_ns[sample - 1] = std::int64_t(cpu_end - cpu_start) * 1'000'000'000 / CLOCKS_PER_SEC;
  }
  std::sort(wall_ns.begin(), wall_ns.end());
  std::sort(cpu_ns.begin(), cpu_ns.end());
  std::cout << (counted ? "PRIME_X86_COUNTED_BENCH" : "PRIME_X86_SCALAR_BENCH") << " steps=" << guest_steps
            << " wall_median_ns=" << wall_ns[2] << " cpu_median_ns=" << cpu_ns[2]
            << " counter_steps=" << counter_steps << " counter_batches=" << counter_batches << '\n';
}
}  // namespace

int main(int argc, char** argv) {
  const bool scalar_only = argc == 2 && std::string_view(argv[1]) == "--scalar";
  const bool counted_only = argc == 2 && std::string_view(argv[1]) == "--counted";
  if (!scalar_only && !counted_only && !Cpu::ether_arm64_host()) return 1;
  try {
    if (!scalar_only && !counted_only) {
      measure(true);
      measure(false);
    }
    if (!counted_only) measure_mixed(false);
    if (!scalar_only) measure_mixed(true);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
