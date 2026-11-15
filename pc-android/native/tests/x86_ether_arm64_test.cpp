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

// The first three instructions use encodings from the boot profile.
constexpr std::array<std::uint8_t, 20> indexed_loop{
    0x4c,0x8b,0x4c,0xd4,0xf8, // MOV R9,[RSP+RDX*8-8]
    0x0f,0xb7,0x44,0x24,0x58, // MOVZX EAX,word [RSP+58h]
    0x48,0x83,0xc0,0x08,      // ADD RAX,8
    0x83,0xe9,0x01,           // SUB ECX,1
    0x75,0xed,0xf4};          // JNZ to MOV; HLT

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
  require(first.count == 3 && first.size == 9 && first.flag_width == 8 &&
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
  const auto parity_block = arm64::decode(parity, sizeof(parity), code);
  require(parity_block.count == 2 && !parity_block.uses_arm_condition(),
          "parity branches must use the canonical x86 flags");
  const std::uint8_t near_back[]{0x48,0x3b,0xf6,0x0f,0x85,0xf7,0xff,0xff,0xff};
  require(arm64::decode(near_back, sizeof(near_back), code).target == code,
          "near relative branch must sign-extend its displacement");
  const std::uint8_t immediate[]{0x48,0x83,0x38,0xff,0x0f,0x85,0x04,0,0,0};
  const auto cmp = arm64::decode(immediate, sizeof(immediate), code);
  require(cmp.count == 2 && cmp.flag_width == 8 && cmp.instructions[0].lhs.memory &&
          cmp.instructions[0].rhs.immediate && cmp.instructions[0].rhs.immediate_value == -1,
          "CMP [RAX],imm8 lacks a sign-extended literal");
  const std::uint8_t byte_test[]{0x45,0x84,0xc9,0x7a,0x08};
  const auto test = arm64::decode(byte_test, sizeof(byte_test), code);
  require(test.count == 2 && test.flag_op == arm64::Instruction::Test && test.flag_width == 1 &&
          test.instructions[0].lhs.reg == Cpu::R9 && test.instructions[0].rhs.reg == Cpu::R9,
          "TEST R9B,R9B must decode with parity and REX registers");
  const std::uint8_t legacy_byte[]{0x84,0xe4,0x74,0x08};
  require(!arm64::decode(legacy_byte, sizeof(legacy_byte), code).count,
          "TEST AH must use the interpreter's legacy byte-register semantics");
  const std::uint8_t cmp_test[]{0x83,0xf8,0x01,0x85,0xc0,0x75,0x08};
  require(arm64::decode(cmp_test, sizeof(cmp_test), code).count == 1,
          "A later TEST must preserve AF established by the earlier CMP");
  const std::uint8_t scan_prefix[]{0x44,0x8d,0x40,0x01,0x48,0xff,0xc2,0x80,0x3a,0xe8,0x75,0xf4};
  const auto scan = arm64::decode(scan_prefix, sizeof(scan_prefix), code);
  require(scan.count == 4 && scan.target == code &&
          scan.instructions[0].kind == arm64::Instruction::Lea &&
          scan.instructions[0].lhs.reg == Cpu::R8 && scan.instructions[0].rhs.displacement == 1 &&
          scan.instructions[1].kind == arm64::Instruction::Increment && scan.flag_width == 1 &&
          scan.instructions[2].rhs.immediate_value == 0xe8 && !scan.uses_arm_condition(),
          "diagnostic LEA/INC/CMP byte/JNE sequence did not decode");
  const std::uint8_t cmp_inc[]{0x83,0xf8,0x01,0x48,0xff,0xc2};
  require(arm64::decode(cmp_inc, sizeof(cmp_inc), code).count == 1,
          "INC must preserve carry established by the preceding CMP");
  const std::uint8_t mov_branch[]{0x8b,0xc1,0x74,0x02};
  require(arm64::decode(mov_branch, sizeof(mov_branch), code).count == 2,
          "a branch using incoming flags should extend a MOV block");
  const auto indexed = arm64::decode(indexed_loop.data(), indexed_loop.size(), code);
  const auto& address = indexed.instructions[0].rhs;
  require(indexed.count == 5 && indexed.target == code &&
          address.reg == Cpu::ESP && address.index == Cpu::EDX &&
          address.displacement == 0xfffffff8U && address.scale_shift == 3 &&
          indexed.instructions[1].width == 2 && indexed.instructions[1].rhs.index == arm64::Operand::kNoIndex &&
          indexed.instructions[2].kind == arm64::Instruction::Add &&
          indexed.flag_op == arm64::Instruction::Subtract,
          "indexed MOV, MOVZX and immediate arithmetic did not form a single block");
  const std::uint8_t rex_index[]{0x4b,0x8b,0x44,0xa0,0xf0,0x75,0x02};
  const auto extended = arm64::decode(rex_index, sizeof(rex_index), code);
  require(extended.count == 2 && extended.instructions[0].rhs.reg == Cpu::R8 &&
          extended.instructions[0].rhs.index == Cpu::R12, "REX.X must retain an R12 index");
  const std::uint8_t high_byte[]{0x0f,0xb6,0xc4};
  require(!arm64::decode(high_byte, sizeof(high_byte), code).count,
          "MOVZX AH must use legacy byte-register semantics");
  const std::uint8_t add_inc[]{0x48,0x83,0xc0,0x08,0x48,0xff,0xc2};
  require(arm64::decode(add_inc, sizeof(add_inc), code).count == 1,
          "INC must use the carry produced by ADD");
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

void immediate_arithmetic_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  for (const bool wide : {false, true}) {
    for (const std::uint8_t modrm : {0xc0, 0xe8}) {
      for (const std::uint8_t imm : {0x08, 0x80}) {
        for (const auto initial : {0ULL, 0x7fffffffULL, 0x80000000ULL,
                                  0x7fffffffffffffffULL, 0x8000000000000000ULL, ~0ULL}) {
          for (auto* machine : {&native, &scalar}) {
            auto& cpu = machine->cpu;
            if (wide) emit(cpu, code, {0x48,0x83,modrm,imm,0x72,0x04});
            else emit(cpu, code, {0x83,modrm,imm,0x72,0x04});
            cpu.eip = code;
            cpu.eflags = 2 | Cpu::IF | Cpu::AF | Cpu::CF;
            cpu.regs[Cpu::EAX] = initial;
          }
          warm_block(native.cpu, code, 2);
          require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed &&
                  capture(native.cpu) == capture(scalar.cpu),
                  "immediate arithmetic changed register state, flags or the carry branch");
        }
      }
    }
  }
}

void indexed_loop_benchmark() {
  constexpr unsigned iterations = 4096;
  const auto run = [](bool translated) {
    PcMachine machine;
    boot(machine, translated);
    auto& cpu = machine.cpu;
    cpu.memory.write_block(code, indexed_loop.data(), indexed_loop.size());
    cpu.write64(stack + 8, 0xf0e1d2c3b4a59687ULL);
    cpu.write64(stack + 0x58, 0xdeadbeef5566fffaULL);
    cpu.regs[Cpu::EDX] = 2;
    cpu.regs[Cpu::ECX] = iterations;
    const auto initial_steps = cpu.instructions;
    const auto start = std::chrono::steady_clock::now();
    machine.run(iterations * 5 + 1);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    require(cpu.halted && !cpu.fault && cpu.regs[Cpu::EAX] == 0x10002 &&
            cpu.regs[Cpu::R9] == 0xf0e1d2c3b4a59687ULL && !cpu.regs[Cpu::ECX],
            "indexed loop produced an incorrect guest result");
    Result result{};
    result.state = capture(cpu);
    result.stats = cpu.ether_arm64_stats();
    result.guest_steps = cpu.instructions - initial_steps;
    result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return result;
  };
  std::array<std::uint64_t, 7> native_times{}, scalar_times{};
  for (unsigned i = 0; i < native_times.size(); ++i) {
    const auto first = run(i % 2 == 0);
    const auto second = run(i % 2 != 0);
    const auto& native = i % 2 == 0 ? first : second;
    const auto& scalar = i % 2 == 0 ? second : first;
    require(native.state == scalar.state && native.guest_steps == iterations * 5 + 1 &&
            native.stats.steps * 100 >= native.guest_steps * 99,
            "indexed loop diverged from scalar execution or missed the native path");
    native_times[i] = native.elapsed_ns;
    scalar_times[i] = scalar.elapsed_ns;
  }
  std::sort(native_times.begin(), native_times.end());
  std::sort(scalar_times.begin(), scalar_times.end());
  std::cout << "ETHER_ARM64_INDEXED_LOOP steps=" << iterations * 5 + 1
            << " native_median_ns=" << native_times[3]
            << " scalar_median_ns=" << scalar_times[3] << '\n';
}

void cache_collision_contract() {
  PcMachine native(4 * 1024 * 1024), scalar(4 * 1024 * 1024);
  boot(native, true);
  boot(scalar, false);
  // Fill every way of one set before touching cold or unsupported RIPs.
  constexpr std::array<std::uint64_t, 4> blocks{0xa140, 0xa350, 0xb560, 0xb770};
  constexpr std::array<std::uint64_t, 4> probes{0xc980, 0xcb90, 0xdda0, 0xdfb0};
  for (auto* machine : {&native, &scalar}) {
    for (const auto address : blocks)
      emit(machine->cpu, address, {0x8b, 0xc1, 0x8b, 0xd0, 0xf4}); // MOV EAX,ECX; MOV EDX,EAX.
    for (const auto address : probes) emit(machine->cpu, address, {0x90, 0xf4});
  }
  for (const auto address : blocks) {
    warm_block(native.cpu, address, 2);
    scalar.cpu.eip = address;
    require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed,
            "scalar warm-up failed");
  }
  const auto warm = native.cpu.ether_arm64_stats();
  constexpr unsigned rounds = 1024;
  for (unsigned round = 0; round < rounds; ++round) {
    const auto probe = probes[round % probes.size()];
    native.cpu.eip = scalar.cpu.eip = probe;
    require(!native.cpu.try_ether_arm64_block(2), "untranslated probe became native");
    require(native.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed,
            "untranslated probe changed scalar execution");
    const auto address = blocks[round % blocks.size()];
    native.cpu.eip = scalar.cpu.eip = address;
    native.cpu.regs[Cpu::ECX] = scalar.cpu.regs[Cpu::ECX] = 0xfeed00000000ULL | round;
    require(native.cpu.try_ether_arm64_block(2) == 2, "colliding guest RIP displaced native code");
    require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed &&
            native.cpu.eip == scalar.cpu.eip && native.cpu.regs == scalar.cpu.regs &&
            native.cpu.eflags == scalar.cpu.eflags && native.cpu.instructions == scalar.cpu.instructions,
            "cached block changed guest state");
  }
  const auto result = native.cpu.ether_arm64_stats();
  require(warm.translated == blocks.size() && result.translated == warm.translated &&
          result.hits - warm.hits == rounds && result.compiled_evictions == warm.compiled_evictions,
          "cold or unsupported addresses evicted a warm block");
  std::cout << "PRIME_C03_ARM64_CACHE_PASS translations=" << result.translated
            << " warm_hits=" << result.hits - warm.hits << '\n';
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
  const auto initial = cpu.ether_arm64_stats();
  warm_block(cpu, linear, 2);
  const auto stable = cpu.ether_arm64_stats();
  require(stable.code_version_hits == initial.code_version_hits + 1 &&
          stable.code_bytes_read == initial.code_bytes_read, "warm block reread validated code");
  cpu.write8(physical + 128, 0x5a); // Same page, outside the translated block.
  warm_block(cpu, linear, 2);
  const auto rechecked = cpu.ether_arm64_stats();
  require(rechecked.code_byte_checks == stable.code_byte_checks + 1 &&
          rechecked.translated == stable.translated, "same-page write lost exact revalidation");
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

void observed_flags_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  constexpr std::array<std::uint64_t, 8> values{
      0, 1, 3, 0x80, 0x100, 0x80000000ULL, 0x8000000000000000ULL, UINT64_MAX};
  for (unsigned cc = 0; cc < 16; ++cc) {
    for (unsigned variant = 0; variant < 9; ++variant) {
      const auto branch = std::uint8_t(0x70 | cc);
      for (auto* machine : {&native, &scalar}) {
        auto& cpu = machine->cpu;
        switch (variant) {
        case 0: emit(cpu, code, {0x45,0x84,0xc9,branch,0x08}); break; // TEST R9B,R9B
        case 1: emit(cpu, code, {0x48,0x85,0xc9,branch,0x08}); break; // TEST RCX,RCX
        case 2: emit(cpu, code, {0x44,0x84,0x08,branch,0x08}); break; // TEST [RAX],R9B
        case 3: emit(cpu, code, {0x48,0x83,0x38,0xff,branch,0x08}); break; // CMP [RAX],-1
        case 4: emit(cpu, code, {0x83,0xf9,0x01,branch,0x08}); break; // CMP ECX,1
        case 5: emit(cpu, code, {0x80,0x38,0xe8,branch,0x08}); break; // CMP BYTE [RAX],E8
        case 6: emit(cpu, code, {0x48,0xff,0xc1,branch,0x08}); break; // INC RCX
        case 7: emit(cpu, code, {0xff,0xc9,branch,0x08}); break; // DEC ECX
        case 8: emit(cpu, code, {0x44,0x8d,0x88,0x00,0x80,0xff,0xff,branch,0x08}); break;
        }
      }
      for (const auto value : values) {
        for (auto* machine : {&native, &scalar}) {
          auto& cpu = machine->cpu;
          cpu.eip = code;
          cpu.eflags = 2 | Cpu::IF | Cpu::AF | Cpu::OF | (value & 1 ? Cpu::CF : 0);
          cpu.regs[Cpu::EAX] = nodes;
          cpu.regs[Cpu::ECX] = cpu.regs[Cpu::R9] = value;
          cpu.write64(nodes, value);
        }
        warm_block(native.cpu, code, 2);
        require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed &&
                native.cpu.eip == scalar.cpu.eip && native.cpu.eflags == scalar.cpu.eflags &&
                native.cpu.regs == scalar.cpu.regs, "translated flags or branch differ from scalar x86");
      }
    }
  }
  for (auto* machine : {&native, &scalar}) {
    auto& cpu = machine->cpu;
    emit(cpu, code, {0x83,0xf8,0x01,0x85,0xc0,0x75,0x08});
    cpu.eip = code;
    cpu.eflags = 2;
    cpu.regs[Cpu::EAX] = 0x10;
  }
  require(!native.cpu.try_ether_arm64_block(3) && native.cpu.step() == StepResult::Executed &&
          scalar.cpu.step() == StepResult::Executed, "CMP before TEST must retire its own flags");
  warm_block(native.cpu, code + 3, 2);
  require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed &&
          native.cpu.eip == scalar.cpu.eip && native.cpu.eflags == scalar.cpu.eflags &&
          native.cpu.flag(Cpu::AF), "Translated TEST lost AF established by CMP");
}

void scan_forms_contract() {
  // Use the byte comparison and addressing forms seen in the boot traces,
  // with a distinct loop body to exercise general translation.
  constexpr unsigned count = 4096;
  const auto run = [](bool translated, bool specialized) {
    PcMachine machine;
    boot(machine, translated);
    auto& cpu = machine.cpu;
    if (specialized) {
      emit(cpu, code, {0x44,0x8d,0x40,0x01,0x48,0xff,0xc2,0x41,0x8b,0xc0,
                      0x41,0x89,0x81,0xd8,0x2e,0,0,0x80,0x3a,0xe8,0x75,0xea,0xf4});
      cpu.regs[Cpu::R9] = output - 0x2ed8;
      machine.enable_boot_acceleration(translated);
    } else {
      emit(cpu, code, {0x44,0x8d,0x40,0x01,0x48,0xff,0xc2,0x41,0x8b,0xc0,
                      0x80,0x3a,0xe8,0x75,0xf1,0xf4});
    }
    std::array<std::uint8_t, count> bytes{};
    bytes.back() = 0xe8;
    cpu.memory.write_block(nodes, bytes.data(), bytes.size());
    cpu.regs[Cpu::EDX] = nodes - 1;
    cpu.regs[Cpu::EAX] = 0xdeadbeeffffffff0ULL;
    const auto initial = cpu.instructions;
    machine.run(count * 6 + 32);
    require(cpu.halted && !cpu.fault && cpu.regs[Cpu::EDX] == nodes + count - 1 &&
            cpu.regs[Cpu::EAX] == std::uint32_t(0xfffffff0U + count),
            "byte scan failed to stop at its first match");
    if (specialized) {
      require(cpu.read64(output) == cpu.regs[Cpu::EAX], "original byte scan changed the output");
      if (translated) require(machine.boot_acceleration().scan_loop_steps > 0,
                              "native chaining bypassed the faster specialized byte scanner");
    }
    Result result{};
    result.state = capture(cpu);
    result.stats = cpu.ether_arm64_stats();
    result.guest_steps = cpu.instructions - initial;
    return result;
  };
  const auto scalar = run(false, false);
  const auto native = run(true, false);
  require(native.state == scalar.state, "translated scan changed the guest state");
  require(native.stats.steps * 100 >= native.guest_steps * 99 &&
          native.stats.translated == 1, "diagnostic scan lost its warm native block");
  require(run(true, true).state == run(false, true).state,
          "original byte scan diverged from the interpreter");
  std::cout << "ETHER_ARM64_SCAN_FORMS steps=" << native.guest_steps
            << " native_steps=" << native.stats.steps << '\n';
}

void benchmark() {
  PcMachine native, scalar, rereading;
  boot(native, true);
  boot(scalar, false);
  boot(rereading, true);
  rereading.cpu.memory.enable_code_page_versions(false);
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
    const auto c = measure(rereading, true);
    require(a.state == b.state && a.state == c.state, "ARM64 warm-up changed the guest result");
  }
  const auto native_warm = native.cpu.ether_arm64_stats();
  const auto rereading_warm = rereading.cpu.ether_arm64_stats();
  std::array<std::uint64_t, 7> native_times{}, scalar_times{}, rereading_times{};
  for (unsigned i = 0; i < native_times.size(); ++i) {
    Result a{}, c{};
    if (i % 2) {
      a = measure(native, true);
      c = measure(rereading, true);
    } else {
      c = measure(rereading, true);
      a = measure(native, true);
    }
    const auto b = measure(scalar, false);
    require(a.state == b.state && a.state == c.state, "warm ARM64 run changed the guest result");
    require(a.stats.translated == native_warm.translated &&
            c.stats.translated == rereading_warm.translated,
            "stable ARM64 blocks were recompiled after warm-up");
    native_times[i] = a.elapsed_ns;
    scalar_times[i] = b.elapsed_ns;
    rereading_times[i] = c.elapsed_ns;
  }
  const auto native_bytes = native.cpu.ether_arm64_stats().code_bytes_read - native_warm.code_bytes_read;
  const auto rereading_bytes = rereading.cpu.ether_arm64_stats().code_bytes_read - rereading_warm.code_bytes_read;
  require(rereading_bytes > 0 && native_bytes < rereading_bytes / 10 &&
          native.cpu.ether_arm64_stats().code_version_hits > native_warm.code_version_hits &&
          rereading.cpu.ether_arm64_stats().code_version_hits == 0,
          "code page versions did not replace repeated byte reads");
  std::sort(native_times.begin(), native_times.end());
  std::sort(scalar_times.begin(), scalar_times.end());
  std::sort(rereading_times.begin(), rereading_times.end());
  std::cout << "PRIME_C03_ARM64_BENCH {\"nodes\":" << node_count
            << ",\"steps\":" << first.guest_steps
            << ",\"scalar_cold_ns\":" << first_scalar.elapsed_ns
            << ",\"native_cold_ns\":" << first.elapsed_ns
            << ",\"scalar_warm_median_ns\":" << scalar_times[3]
            << ",\"native_warm_median_ns\":" << native_times[3]
            << ",\"rereading_warm_median_ns\":" << rereading_times[3]
            << ",\"native_warm_code_bytes\":" << native_bytes
            << ",\"rereading_warm_code_bytes\":" << rereading_bytes
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
    cache_collision_contract();
    flag_contract();
    observed_flags_contract();
    scan_forms_contract();
    immediate_arithmetic_contract();
    indexed_loop_benchmark();
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
