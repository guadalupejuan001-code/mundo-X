#include "../pc_machine.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;
using cosmic_pc::StepResult;
namespace arm64 = cosmic_pc::ether_arm64;

constexpr std::uint64_t code = 0xa000, irq_handler = 0xc000, idt = 0xd000;
constexpr std::uint64_t nodes = 0x10000, node_stride = 32;
constexpr unsigned node_count = 1024;
constexpr std::uint64_t stack = 0x19000, output = 0x80000;
constexpr unsigned ready_entry = 0x23;
constexpr unsigned ready_leaf = 0xe3;

// Recreates the observed MOV RSI,[RSI], CMP R15,[RSI+10h], CMP RSI,R14
// and conditional branches with controlled RAM and explicit exit paths.
constexpr std::array<std::uint8_t, 24> pointer_loop{
    0x48,0x8b,0x36, 0x4c,0x3b,0x7e,0x10, 0x72,0x0d, // MOV, CMP, JB to alternate exit.
  0x49,0x3b,0xf6, 0x75,0xf2,                      // CMP, JNE to MOV.
    0x48,0x89,0xf0, 0x48,0x89,0x03, 0xeb,0x02, 0x90,0x90}; // Record RSI, return to HLT.

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

void emit(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> bytes) {
  cpu.memory.write_block(address, bytes.begin(), bytes.size());
}

void boot(PcMachine& machine, bool translated) {
  machine.boot_long_mode_demo();
  machine.run(4000);
  auto& cpu = machine.cpu;
  require(cpu.is_long_mode() && cpu.halted && !cpu.fault, "long-mode fixture failed");
  cpu.halted = false;
  cpu.eip = code;
  cpu.regs[Cpu::ESP] = stack;
  cpu.enable_page_translation_cache();
  cpu.enable_ether_arm64_blocks(translated);
  machine.enable_boot_acceleration();
}

void initialize_loop(Cpu& cpu) {
  cpu.memory.write_block(code, pointer_loop.data(), pointer_loop.size());
  emit(cpu, code + pointer_loop.size(), {0x0f,0x31,0xf4}); // RDTSC; HLT.
  for (unsigned i = 0; i < node_count; ++i) {
    cpu.write64(nodes + i * node_stride, nodes + ((i + 1) % node_count) * node_stride);
    cpu.write64(nodes + i * node_stride + 16, i & 3);
  }
  cpu.write64(output, 0);
  cpu.regs[Cpu::ESI] = nodes;
  cpu.regs[Cpu::R14] = nodes + (node_count - 1) * node_stride;
  cpu.regs[Cpu::R15] = 4;
  cpu.regs[Cpu::EBX] = output;
  cpu.eflags = 2 | Cpu::IF;
}

void decoder_contract() {
  const auto first = arm64::decode(pointer_loop.data(), pointer_loop.size(), code);
  require(first.count == 3 && first.size == 9 && first.compare_width == 8 &&
          first.instructions[0].rhs.memory && first.instructions[0].rhs.reg == Cpu::ESI &&
          first.instructions[1].lhs.reg == Cpu::R15 && first.instructions[1].rhs.displacement == 16 &&
          first.instructions[2].condition == 3 && first.target == code + 22,
          "diagnostic MOV/CMP/JB encodings did not decode");
  const auto back = arm64::decode(pointer_loop.data() + 9, pointer_loop.size() - 9, code + 9);
  require(back.count == 2 && back.target == code &&
          back.instructions[0].rhs.reg == Cpu::R14 && back.instructions[1].condition == 1,
          "diagnostic register CMP/JNE encodings did not decode");
  const std::uint8_t unsupported[]{0x64,0x48,0x8b,0x36,0xf4};
  require(!arm64::decode(unsupported, sizeof(unsupported), code).count,
          "segment override must stay in the interpreter");
  const std::uint8_t write_then_read[]{0x89,0x03,0x8b,0x33};
  require(arm64::decode(write_then_read, sizeof(write_then_read), code).count == 1,
          "RAM writes must terminate the block");
  const std::uint8_t parity[]{0x49,0x3b,0xf6,0x7a,0xfb};
  require(arm64::decode(parity, sizeof(parity), code).count == 1,
          "parity branches must use x86 parity");
  const std::uint8_t near_back[]{0x48,0x3b,0xf6,0x0f,0x85,0xf7,0xff,0xff,0xff};
  require(arm64::decode(near_back, sizeof(near_back), code).target == code,
          "near relative branch must sign-extend its displacement");
}

void gate(PcMachine& machine, unsigned vector) {
  auto& cpu = machine.cpu;
  cpu.idtr = {0xfff, idt};
  cpu.write64(idt + vector * 16, irq_handler | (0x18ULL << 16) | (0x8eULL << 40));
  cpu.write64(idt + vector * 16 + 8, 0);
}

struct State {
  std::array<std::uint64_t, 16> regs;
  std::uint64_t rip, steps, cr2, stored, frame_rip, frame_error;
  std::uint32_t flags;
  bool halted;

  bool operator==(const State& other) const {
    return regs == other.regs && rip == other.rip && steps == other.steps &&
        cr2 == other.cr2 && stored == other.stored && frame_rip == other.frame_rip &&
        frame_error == other.frame_error && flags == other.flags && halted == other.halted;
  }
};

State capture(Cpu& cpu) {
  return {cpu.regs, cpu.eip, cpu.instructions, cpu.cr2, cpu.read64(output),
          cpu.read64(stack - 40), cpu.read64(stack - 48), cpu.eflags, cpu.halted};
}

struct Result {
  State state;
  Cpu::EtherArm64Stats stats;
  std::uint64_t elapsed_ns, guest_steps = 0, ticks = 0, irq_rip = 0, irq_step = 0;
};

Result execute_loop(bool translated, bool interrupt, PcMachine* reuse = nullptr) {
  if (!reuse) {
    PcMachine machine;
    boot(machine, translated);
    return execute_loop(translated, interrupt, &machine);
  }
  auto& machine = *reuse;
  auto& cpu = machine.cpu;
  cpu.eip = code;
  cpu.halted = false;
  initialize_loop(cpu);
  Result result{};
  const auto start_steps = cpu.instructions;
  constexpr unsigned deadline = 37;
  if (interrupt) {
    gate(machine, 0x40);
    emit(cpu, irq_handler, {0x48,0xff,0xc1,0x48,0xcf}); // INC RCX; IRETQ.
    machine.device_step_hook = [&] {
      if (++result.ticks != deadline) return;
      result.irq_rip = cpu.eip;
      result.irq_step = cpu.instructions - start_steps;
      require(cpu.raise_external_irq(0x40), "guest IRQ was not delivered at the boundary");
    };
    machine.device_batch_limit_hook = [&](unsigned budget) {
      return result.ticks < deadline
          ? std::min(budget, unsigned(deadline - result.ticks)) : budget;
    };
    machine.device_batch_advance_hook = [&](unsigned steps) { result.ticks += steps; };
  }
  const auto started = std::chrono::steady_clock::now();
  machine.run(6000);
  const auto elapsed = std::chrono::steady_clock::now() - started;
  if (interrupt) {
    machine.device_step_hook = {};
    machine.device_batch_limit_hook = {};
    machine.device_batch_advance_hook = {};
  }
  require(cpu.halted && !cpu.fault && cpu.read64(output) == cpu.regs[Cpu::R14],
          "pointer loop did not reach its checked HALT and RAM write");
  result.state = capture(cpu);
  result.guest_steps = cpu.instructions - start_steps;
  result.stats = cpu.ether_arm64_stats();
  result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
  if (interrupt) require(result.irq_step == deadline - 1, "device deadline shifted");
  return result;
}

void warm_block(Cpu& cpu, std::uint64_t address, unsigned steps) {
  for (unsigned attempt = 0; attempt < 4; ++attempt) {
    cpu.eip = address;
    if (cpu.try_ether_arm64_block(steps) == steps) return;
  }
  throw std::runtime_error("ARM64 block never became executable");
}

void page_fault(bool translated, bool store, bool initially_absent,
                State& state, Cpu::EtherArm64Stats& stats) {
  constexpr std::uint64_t address = 0x280000, leaf = 0x3008;
  PcMachine machine;
  boot(machine, translated);
  auto& cpu = machine.cpu;
  gate(machine, 14);
  emit(cpu, irq_handler, {0xf4}); // Observe the real guest #PF stack frame.
  cpu.write64(leaf, 0x200000 | ready_leaf);
  if (store) {
    emit(cpu, code, {0x48,0x8b,0xd0,0x48,0x89,0x13,0xf4}); // MOV RDX,RAX; MOV [RBX],RDX.
    cpu.regs[Cpu::EBX] = address;
  } else {
    emit(cpu, code, {0x48,0x8b,0xd0,0x48,0x8b,0x0b,0xf4}); // MOV RDX,RAX; MOV RCX,[RBX].
    cpu.regs[Cpu::EBX] = address;
  }
  cpu.regs[Cpu::EAX] = 0x76ff0123456789abULL;
  const auto steps = cpu.instructions;
  for (unsigned i = 0; i < 4; ++i) {
    cpu.eip = code;
    cpu.regs[Cpu::EDX] = 0;
    if (!cpu.try_ether_arm64_block(2)) {
      require(cpu.step() == StepResult::Executed && cpu.step() == StepResult::Executed,
              "warm-up before the mapping change failed");
    }
  }
  cpu.write64(leaf, initially_absent ? 0 : (0x200000 | 0xa1)); // Absent or read-only.
  cpu.regs[Cpu::EDX] = 0;
  cpu.eip = code;
  machine.run(10);
  require(cpu.halted && !cpu.fault && cpu.cr2 == address && cpu.instructions == steps + 11 &&
          cpu.regs[Cpu::EDX] == cpu.regs[Cpu::EAX] && cpu.read64(cpu.regs[Cpu::ESP] + 8) == code + 3 &&
          cpu.read64(cpu.regs[Cpu::ESP]) == (store ? 3U : 0U),
          "mapping change did not enter #PF at the precise load/store RIP");
  require(cpu.read64(address) == (store ? cpu.regs[Cpu::EAX] : 0),
          "faulting access changed guest RAM");
  state = capture(cpu);
  stats = cpu.ether_arm64_stats();
}

void guard_contract() {
  constexpr std::uint64_t linear = 0x200000, physical = 0x40000, remapped = 0x50000;
  PcMachine machine;
  boot(machine, true);
  auto& cpu = machine.cpu;
  cpu.write64(0x3008, 0x4000 | ready_entry);
  cpu.write64(0x4000, physical | ready_entry);
  emit(cpu, physical, {0x48,0x8b,0xd0,0x48,0x8b,0xca,0xf4}); // MOV RDX,RAX; MOV RCX,RDX.
  cpu.regs[Cpu::EAX] = 0xfedcba9876541230ULL;
  cpu.regs[Cpu::EBX] = 0x450034;
  warm_block(cpu, linear, 2);
  require(cpu.regs[Cpu::ECX] == cpu.regs[Cpu::EAX], "initial mapping computed the wrong value");
  cpu.write8(physical + 2, 0xd3); // Change the source via its physical alias.
  warm_block(cpu, linear, 2);
  require(cpu.regs[Cpu::ECX] == cpu.regs[Cpu::EBX], "cached code survived a physical alias write");
  cpu.write64(0x4000, remapped | ready_entry);
  emit(cpu, remapped, {0x8b,0xd0,0x48,0x8b,0xca,0xf4}); // MOV EDX,EAX clears high half.
  warm_block(cpu, linear, 2);
  require(cpu.regs[Cpu::ECX] == std::uint32_t(cpu.regs[Cpu::EAX]) &&
          cpu.ether_arm64_stats().invalidated >= 2, "mapping change reused the wrong translation");
  cpu.eip = linear;
  require(!cpu.try_ether_arm64_block(1), "a one-step budget allowed a two-instruction block");
  cpu.step_trace_hook = [](const auto&) {};
  require(!cpu.try_ether_arm64_block(2), "instruction tracer lost decoded x86 steps");
}

void flag_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  constexpr std::array<std::uint64_t, 8> values{
      0,1,0x10,0xff,0xffffffff,0x8000000000000000ULL,0x7fffffffffffffffULL,UINT64_MAX};
  for (unsigned cc = 0; cc < 16; ++cc) {
    if (cc == 10 || cc == 11) continue;
    for (const auto prefix : {0x48U, 0x40U}) {
      for (auto* m : {&native, &scalar})
        emit(m->cpu, code, {std::uint8_t(prefix),0x3b,0xc3,std::uint8_t(0x70 | cc),0x08});
      for (auto a : values) for (auto b : values) {
        for (auto* m : {&native, &scalar}) {
          m->cpu.eip = code;
          m->cpu.eflags = 2 | Cpu::IF | Cpu::PF | Cpu::AF;
          m->cpu.regs[Cpu::EAX] = a;
          m->cpu.regs[Cpu::EBX] = b;
        }
        warm_block(native.cpu, code, 2);
        require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed &&
                native.cpu.regs == scalar.cpu.regs && native.cpu.eflags == scalar.cpu.eflags &&
                native.cpu.eip == scalar.cpu.eip, "ARM64 branch or x86 flags diverged");
      }
    }
  }
}

void benchmark() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  const auto initial_regs = scalar.cpu.regs;
  const auto initial_steps = scalar.cpu.instructions;
  const auto measure = [&](PcMachine& machine, bool translated) {
    machine.cpu.regs = initial_regs;
    machine.cpu.instructions = initial_steps;
    return execute_loop(translated, false, &machine);
  };
  const auto first_scalar = measure(scalar, false);
  const auto first = measure(native, true);
  require(first.state == first_scalar.state && first.stats.translated > 0 && first.stats.hits > 0,
          "first pass did not exercise real ARM64 translation");
  // Blocks reached only at loop exit need multiple visits to translate.
  for (unsigned i = 0; i < 3; ++i) {
    const auto a = measure(native, true);
    const auto b = measure(scalar, false);
    require(a.state == b.state, "ARM64 warm-up changed the guest result");
  }
  const auto warm_translations = native.cpu.ether_arm64_stats().translated;
  std::array<std::uint64_t, 7> native_times{}, scalar_times{};
  for (unsigned i = 0; i < native_times.size(); ++i) {
    Result a{}, b{};
    if (i % 2) a = measure(native, true);
    b = measure(scalar, false);
    if (!(i % 2)) a = measure(native, true);
    require(a.state == b.state, "warm ARM64 run changed the guest result");
    require(a.stats.translated == warm_translations,
            "stable ARM64 blocks were recompiled after warm-up");
    native_times[i] = a.elapsed_ns;
    scalar_times[i] = b.elapsed_ns;
  }
  std::sort(native_times.begin(), native_times.end());
  std::sort(scalar_times.begin(), scalar_times.end());
  std::cout << "PRIME_C03_ARM64_BENCH {\"nodes\":" << node_count
            << ",\"steps\":" << first.guest_steps
            << ",\"scalar_cold_ns\":" << first_scalar.elapsed_ns
            << ",\"native_cold_ns\":" << first.elapsed_ns
            << ",\"scalar_warm_median_ns\":" << scalar_times[3]
            << ",\"native_warm_median_ns\":" << native_times[3]
            << ",\"translated_blocks\":" << first.stats.translated
            << ",\"native_steps\":" << first.stats.steps << "}\n";
}
}  // namespace

int main() {
  try {
    decoder_contract();
    const auto scalar = execute_loop(false, true);
    const auto native = execute_loop(true, true);
    require(native.state == scalar.state && native.ticks == scalar.ticks &&
            native.irq_rip == scalar.irq_rip && native.irq_step == scalar.irq_step,
            "translated boundary differs from the scalar IRQ boundary");
    if (!Cpu::ether_arm64_host()) {
      require(native.stats.steps == 0, "non-ARM64 host executed a native block");
      std::cout << "PRIME_C03_ARM64_FALLBACK_PASS decoder=1 irq=1\n";
      return 0;
    }
    require(native.stats.steps > 4000 && native.stats.hits > 0, "ARM64 loop did not execute");
    guard_contract();
    flag_contract();
    for (bool store : {false, true}) {
      State a{}, b{};
      Cpu::EtherArm64Stats sa{}, sb{};
      page_fault(true, store, !store, a, sa);
      page_fault(false, store, !store, b, sb);
      require(a == b && sa.data_fallbacks > 0 && sa.steps > 0,
              "translated path changed the page fault or skipped the fallback");
    }
    benchmark();
    std::cout << "PRIME_C03_ARM64_PASS flags=1 ram=1 aliases=1 remap=1 page_faults=2 irq=1\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PRIME_C03_ARM64_FAIL " << error.what() << '\n';
    return 1;
  }
}
