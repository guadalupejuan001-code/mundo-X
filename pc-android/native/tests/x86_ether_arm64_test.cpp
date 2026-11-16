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

constexpr std::array<std::uint8_t, 7> byte_read_loop{
    0x44,0x8a,0x5c,0x11,0xff, // Observed MOV R11B,[RCX+RDX-1].
    0xeb,0xf9};               // Repeat within the machine's step budget.

constexpr std::uint32_t protected_code = 0x459a1f, ds_base = 0x10000, ss_base = 0x20000;
constexpr std::array<std::uint8_t, 17> protected_loop{
    0x66,0x8b,0x07,       // MOV AX,[EDI], observed earlier in the same protected32 stage.
    0x89,0x7d,0xf8,       // MOV [EBP-8],EDI, observed at 0020:00459a22.
    0x31,0xd6,0x03,0xd8, // XOR ESI,EDX; ADD EBX,EAX.
    0x83,0xc7,0x02,0x49, // ADD EDI,2; DEC ECX.
    0x75,0xf0,0xf4};      // JNZ; HLT.

constexpr std::array<std::uint8_t, 25> byte_copy_loop{
    0x42,0x8a,0x0c,0x1a,0x44,0x03,0xf3,0x43,0x88,0x0c,0x1f, // Observed byte load, ADD and byte store.
    0x03,0xd3,0x4c,0x03,0xfb,0x44,0x2b,0xd3,0x45,0x85,0xd2, // Observed pointer/counter updates.
    0x75,0xe8,0xf4};                                           // JNZ; HLT.

constexpr std::array<std::uint8_t, 16> signed_load_loop{
    0x8b,0xc1,                             // MOV EAX,ECX
    0x45,0x0f,0xbf,0x44,0x41,0x1c,         // Observed MOVSX R8D, word [R9+RAX*2+1Ch]
    0x49,0x63,0xc0,                        // Observed MOVSXD RAX,R8D
    0xff,0xc9,0x75,0xf1,0xf4};             // DEC ECX; JNZ; HLT

constexpr std::array<std::uint8_t, 15> memory_subtractions{
    0x48,0x89,0xdf,0x4e,0x29,0x1c,0xc9,    // MOV RDI,RBX; observed SUB [RCX+R9*8],R11
    0x4c,0x89,0xca,0x4a,0x29,0x04,0xc9,    // MOV RDX,R9; observed SUB [RCX+R9*8],RAX
    0xf4};

constexpr std::array<std::uint8_t, 16> repeated_alias_loop{
    0x4c,0x29,0x1f,       // SUB [RDI],R11
    0x88,0x4f,0x03,       // MOV [RDI+3],CL (overlaps the subtraction)
    0x4c,0x8b,0x17,       // MOV R10,[RDI]
    0x48,0x83,0xea,0x01,  // SUB RDX,1
    0x75,0xf1,0xf4};      // JNZ; HLT

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

void boot_protected(PcMachine& machine, bool translated) {
  machine.boot_demo();
  auto& cpu = machine.cpu;
  cpu.reset(0, 0x1000);
  cpu.a20_enabled = true;
  cpu.gdtr = {0x37, 0x3000};
  cpu.write64(0x3020, 0x00cf9a000000ffffULL);
  cpu.write64(0x3028, 0x00cf92010000ffffULL); // DS starts at 0x10000.
  cpu.write64(0x3030, 0x00cf92020000ffffULL); // SS starts at 0x20000.
  emit(cpu, 0x1000, {0xb8,0x11,0,0x0f,0x22,0xc0,0x66,0xea,0,0x20,0,0,0x20,0});
  emit(cpu, 0x2000, {0x66,0xb8,0x28,0,0x8e,0xd8,0x66,0xb8,0x30,0,0x8e,0xd0,0x90});
  for (unsigned i = 0; i < 8; ++i)
    require(cpu.step() == StepResult::Executed, "could not enter unpaged protected32");
  require(std::string(cpu.mode_name()) == "protected32" && cpu.segments[Cpu::CS] == 0x20,
          "protected32 fixture selected the wrong code descriptor");
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
  const auto forwarded = arm64::decode(write_then_read, sizeof(write_then_read), code);
  require(forwarded.count == 2 && forwarded.stages_ram() &&
          arm64::decode(write_then_read, sizeof(write_then_read), code, false).count == 1,
          "long64 must stage the RAM store before the following load");
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
          cmp.instructions[0].rhs.immediate && cmp.instructions[0].rhs.immediate_value == UINT64_MAX,
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
  require(indexed.count == 5 && indexed.target == code && indexed.repeats &&
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
  const auto byte_read = arm64::decode(byte_read_loop.data(), byte_read_loop.size(), code);
  const auto& mov = byte_read.instructions[0];
  require(byte_read.count == 2 && byte_read.target == code && byte_read.flag_width == 0 &&
          mov.kind == arm64::Instruction::MoveByte && mov.width == 1 && mov.lhs.reg == Cpu::R11 &&
          mov.rhs.memory && mov.rhs.reg == Cpu::ECX && mov.rhs.index == Cpu::EDX &&
          mov.rhs.displacement == 0xffffffffU, "observed indexed byte load did not form a block");
  const std::uint8_t legacy_load[]{0x8a,0x24,0x24};
  require(!arm64::decode(legacy_load, sizeof(legacy_load), code).count,
          "MOV AH,[RSP] must preserve the legacy high-byte path");
  const std::uint8_t add_byte[]{0x48,0x83,0xc0,0x08,0x44,0x8a,0x5c,0x11,0xff};
  const auto add_then_read = arm64::decode(add_byte, sizeof(add_byte), code);
  require(add_then_read.count == 2 && add_then_read.flag_op == arm64::Instruction::Add,
          "byte load replaced the flags produced by the preceding ADD");
  const auto protected_block = arm64::decode(
      protected_loop.data(), protected_loop.size(), protected_code, false);
  require(protected_block.count == 2 && protected_block.instructions[0].width == 2 &&
          !protected_block.instructions[0].rhs.stack && protected_block.instructions[1].lhs.stack,
          "protected32 MOV AX and stack store lost their original widths or segments");
  const std::uint8_t legacy_back[]{0x49,0x75,0xfd};
  const auto legacy = arm64::decode(legacy_back, sizeof(legacy_back), code, false);
  require(legacy.count == 2 && legacy.target == code &&
          legacy.instructions[0].kind == arm64::Instruction::Decrement &&
          legacy.instructions[0].lhs.reg == Cpu::ECX, "legacy DEC was decoded as REX");
  const std::uint8_t word_cmp[]{0x66,0x3b,0xc1,0x7c,0x02};
  require(!arm64::decode(word_cmp, sizeof(word_cmp), code, false).uses_arm_condition(),
          "word comparison must use the x86 sign bit");
  const auto copy = arm64::decode(byte_copy_loop.data(), byte_copy_loop.size(), code);
  require(copy.count == 6 && copy.stages_ram() &&
          copy.instructions[1].kind == arm64::Instruction::Add &&
          copy.instructions[2].kind == arm64::Instruction::MoveByte &&
          copy.instructions[2].lhs.memory && copy.instructions[2].lhs.index == Cpu::R11,
          "observed long64 byte copy lost its indexed store or pointer updates");
  const std::uint8_t reference[]{0x48,0xf7,0x05,0xf5,0xff,0xff,0xff,0,0,0,0x80,0x75,2};
  const auto rip = arm64::decode(reference, sizeof(reference), code);
  require(rip.count == 2 && rip.instructions[0].lhs.reg == arm64::Operand::kRipBase &&
          rip.instructions[0].lhs.immediate_value == code &&
          rip.instructions[0].rhs.immediate_value == 0xffffffff80000000ULL,
          "RIP-relative TEST must use the end of the immediate and sign-extend it");
  const std::uint8_t prior_flags[]{0x83,0xf8,1,0xd3,0xe0,0x75,2};
  require(arm64::decode(prior_flags, sizeof(prior_flags), code).count == 1,
          "a zero-count shift must preserve the preceding guest flags");
  const std::uint8_t carry_input[]{0x11,0xc0};
  require(!arm64::decode(carry_input, sizeof(carry_input), code).count,
          "ADC needs the interpreter's carry dependence");
  const auto signed_loop = arm64::decode(signed_load_loop.data(), signed_load_loop.size(), code);
  require(signed_loop.count == 5 && signed_loop.target == code &&
          signed_loop.instructions[1].kind == arm64::Instruction::MoveSignExtend &&
          signed_loop.instructions[1].width == 4 && signed_loop.instructions[1].lhs.reg == Cpu::R8 &&
          signed_loop.instructions[1].rhs.reg == Cpu::R9 && signed_loop.instructions[1].rhs.scale_shift == 1 &&
          signed_loop.instructions[2].kind == arm64::Instruction::MoveSignExtend &&
          signed_loop.instructions[2].width == 8, "signed boot loads formed the wrong block");
  const std::uint8_t sign_ah[]{0x0f,0xbe,0xc4};
  require(!arm64::decode(sign_ah, sizeof(sign_ah), code).count,
          "MOVSX AH must retain legacy high-byte semantics");
  const auto rmw = arm64::decode(memory_subtractions.data(), memory_subtractions.size(), code);
  require(rmw.count == 4 && rmw.size == 14 && rmw.writes == 2 && rmw.stages_ram() &&
          rmw.flag_op == arm64::Instruction::Subtract &&
          rmw.instructions[1].lhs.memory && rmw.instructions[1].lhs.index == Cpu::R9 &&
          rmw.instructions[1].rhs.reg == Cpu::R11 &&
          rmw.instructions[3].rhs.reg == Cpu::EAX, "observed subtractions must share a block");
  const std::uint8_t locked_sub[]{0xf0,0x4e,0x29,0x1c,0xc9};
  require(!arm64::decode(locked_sub, sizeof(locked_sub), code).count,
          "LOCK SUB must execute through the CPU's atomic memory path");
  const auto repeated = arm64::decode(repeated_alias_loop.data(), repeated_alias_loop.size(), code);
  require(repeated.count == 5 && repeated.writes == 2 && repeated.repeats,
          "fixed-address RAM loop missed its native backedge");
  const std::uint8_t moving_store[]{0x4c,0x29,0x1f,0x48,0x83,0xc7,8,0x48,0x83,0xea,1,0x75,0xf3};
  const auto moving = arm64::decode(moving_store, sizeof(moving_store), code);
  require(moving.count == 4 && moving.target == code && !moving.repeats,
          "a moving destination must remain a one-round RAM block");
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

void signed_load_contract() {
  constexpr unsigned iterations = 20'000, steps = 5 * iterations + 1;
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  for (auto* machine : {&native, &scalar}) {
    auto& cpu = machine->cpu;
    cpu.memory.write_block(code, signed_load_loop.data(), signed_load_loop.size());
    for (unsigned i = 1; i <= iterations; ++i)
      cpu.write16(nodes + 0x1c + 2 * i, std::uint16_t(i * 17U + 0x80e0U));
  }
  const auto run = [&](PcMachine& machine) {
    auto& cpu = machine.cpu;
    cpu.eip = code;
    cpu.halted = false;
    cpu.eflags = 2 | Cpu::CF | Cpu::IF;
    cpu.regs[Cpu::ECX] = iterations;
    cpu.regs[Cpu::R9] = nodes;
    cpu.regs[Cpu::R8] = UINT64_MAX;
    const auto retired = cpu.instructions;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned remaining = steps; remaining;) {
      const auto slice = std::min(remaining, 50'000U);
      machine.run(slice);
      remaining -= slice;
    }
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    require(cpu.halted && !cpu.fault && cpu.instructions - retired == steps &&
            cpu.regs[Cpu::R8] == 0xffff80f1ULL && cpu.regs[Cpu::EAX] == 0xffffffffffff80f1ULL &&
            cpu.eflags == (2 | Cpu::CF | Cpu::IF | Cpu::ZF | Cpu::PF),
            "sign extension changed width, flags or guest instructions");
    return ns;
  };
  std::array<std::int64_t, 5> native_ns{}, scalar_ns{};
  for (unsigned i = 0; i < native_ns.size(); ++i) {
    if (i & 1U) {
      native_ns[i] = run(native);
      scalar_ns[i] = run(scalar);
    } else {
      scalar_ns[i] = run(scalar);
      native_ns[i] = run(native);
    }
    require(capture(native.cpu) == capture(scalar.cpu), "signed loop diverged from scalar state");
  }
  const auto covered = native.cpu.ether_arm64_stats().steps;
  require(covered * 100 >= steps * native_ns.size() * 99 &&
          scalar.cpu.ether_arm64_stats().steps == 0, "signed loop missed native execution");
  std::sort(native_ns.begin(), native_ns.end());
  std::sort(scalar_ns.begin(), scalar_ns.end());
  std::cout << "PRIME_ETHER_SIGNED_BOOT_PASS steps=" << steps << " native_steps=" << covered
            << " repeats=" << native_ns.size() << " native_median_ns=" << native_ns[2]
            << " scalar_median_ns=" << scalar_ns[2] << '\n';
  for (auto* machine : {&native, &scalar}) {
    auto& cpu = machine->cpu;
    emit(cpu, code, {0x48,0x0f,0xbe,0xc2,0x90,0xf4}); // MOVSX RAX,DL; NOP.
    cpu.halted = false;
    cpu.eip = code;
    cpu.regs[Cpu::EDX] = 0x80;
  }
  warm_block(native.cpu, code, 2);
  require(scalar.cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed &&
          capture(native.cpu) == capture(scalar.cpu) && native.cpu.regs[Cpu::EAX] == 0xffffffffffffff80ULL,
          "MOVSX r64,r8 changed signed value or CPU state");
}

void memory_subtraction_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  constexpr auto high = 0x8000000000000000ULL;
  constexpr auto boundary = output + 0xffc;
  for (auto* machine : {&native, &scalar}) {
    auto& cpu = machine->cpu;
    cpu.memory.write_block(code, memory_subtractions.data(), memory_subtractions.size());
    cpu.regs[Cpu::ECX] = output - 8;
    cpu.regs[Cpu::R9] = 1;
    cpu.regs[Cpu::R11] = 1;
    cpu.regs[Cpu::EAX] = UINT64_MAX;
    cpu.write64(output, high);
    cpu.write64(boundary, high);
  }
  warm_block(native.cpu, code, 4);
  for (unsigned i = 0; i < 4; ++i)
    require(scalar.cpu.step() == StepResult::Executed, "scalar indexed subtraction failed");
  const auto& stats = native.cpu.ether_arm64_stats();
  require(capture(native.cpu) == capture(scalar.cpu) && native.cpu.read64(output) == high &&
          native.cpu.flag(Cpu::CF) && native.cpu.flag(Cpu::OF) &&
          stats.staged_ram_blocks == 1 && stats.staged_ram_steps == 4 &&
          stats.coalesced_ram_writes == 1, "combined indexed subtractions changed x86 state");

  for (auto* machine : {&native, &scalar}) {
    machine->cpu.eip = code;
    machine->cpu.regs[Cpu::ECX] = boundary - 8;
  }
  const auto before = capture(native.cpu);
  const auto fallbacks = stats.data_fallbacks;
  require(!native.cpu.try_ether_arm64_block(4) && stats.data_fallbacks == fallbacks + 1 &&
          capture(native.cpu) == before &&
          native.cpu.read64(boundary) == high, "cross-page subtraction altered state before fallback");
  for (auto* machine : {&native, &scalar})
    require(machine->cpu.step() == StepResult::Executed && machine->cpu.step() == StepResult::Executed &&
            machine->cpu.read64(boundary) == high - 1, "scalar cross-page subtraction failed");
  require(capture(native.cpu) == capture(scalar.cpu), "subtraction fallback changed guest state");
  std::cout << "PRIME_ETHER_RMW_BOOT_PASS variants=2 cross_page=1\n";
}

void staged_ram_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  const auto alias_code = code + 0x40, blocked = code + 0x80, modified = code + 0xc0;
  for (auto* cpu : {&native.cpu, &scalar.cpu}) {
    // Write 8 bytes, overwrite the middle byte, then read through both stores.
    emit(*cpu, alias_code, {0x48,0x89,0x07,0x88,0x4f,0x03,0x4c,0x8b,0x17,0xf4});
    // The second store crosses a page and requires the scalar path.
    emit(*cpu, blocked, {0x48,0x89,0x07,0x48,0x89,0x06,0xf4});
    // Write HLT over the immediately following NOP.
    emit(*cpu, modified, {0x88,0x07,0x90,0xf4});
    cpu->regs[Cpu::EAX] = 0x1122334455667788ULL;
    cpu->regs[Cpu::ECX] = 0xab;
    cpu->regs[Cpu::EDI] = output;
    cpu->regs[Cpu::ESI] = output + 0x1ffc;
    cpu->write64(output, 0);
  }
  warm_block(native.cpu, alias_code, 3);
  scalar.cpu.eip = alias_code;
  for (unsigned i = 0; i < 3; ++i)
    require(scalar.cpu.step() == StepResult::Executed, "scalar forwarding fixture failed");
  require(capture(native.cpu) == capture(scalar.cpu) &&
          native.cpu.read64(output) == 0x11223344ab667788ULL &&
          native.cpu.regs[Cpu::R10] == native.cpu.read64(output),
          "overlapping staged writes changed the load or the final RAM bytes");

  auto& cpu = native.cpu;
  cpu.eip = blocked;
  const auto before = capture(cpu);
  const auto fallbacks = cpu.ether_arm64_stats().data_fallbacks;
  for (unsigned i = 0; i < 4; ++i)
    require(!cpu.try_ether_arm64_block(2) && capture(cpu) == before &&
            cpu.read64(output + 0x1ffc) == 0, "rejected second store committed the first store");
  require(cpu.ether_arm64_stats().data_fallbacks > fallbacks &&
          cpu.step() == StepResult::Executed && cpu.read64(output) == cpu.regs[Cpu::EAX],
          "scalar fallback did not execute the initial store");

  cpu.eip = modified;
  cpu.regs[Cpu::EDI] = modified + 2;
  cpu.regs[Cpu::EAX] = 0xf4;
  const auto code_fallbacks = cpu.ether_arm64_stats().data_fallbacks;
  for (unsigned i = 0; i < 4; ++i)
    require(!cpu.try_ether_arm64_block(2) && cpu.read8(modified + 2) == 0x90,
            "staged block ran across a change to its executing bytes");
  require(cpu.ether_arm64_stats().data_fallbacks > code_fallbacks &&
          cpu.step() == StepResult::Executed && cpu.step() == StepResult::Halted,
          "scalar fallback did not fetch the modified instruction");
  std::cout << "PRIME_ETHER_STAGED_RAM_PASS overlap=1 rollback=1 code_guard=1\n";
}

void repeated_loop_contract() {
  struct Run {
    State terminal;
    std::uint64_t ticks = 0, irq_rip = 0, irq_steps = 0, irq_word = 0;
    Cpu::EtherArm64Stats stats;
  };
  const auto execute = [](bool translated) {
    PcMachine machine;
    boot(machine, translated);
    auto& cpu = machine.cpu;
    cpu.memory.write_block(code, repeated_alias_loop.data(), repeated_alias_loop.size());
    cpu.regs[Cpu::EDI] = output;
    cpu.regs[Cpu::R11] = 0x12345678;
    cpu.regs[Cpu::R13] = 0;
    cpu.regs[Cpu::ECX] = 0xab;
    cpu.regs[Cpu::EDX] = 40;
    cpu.eflags = 2 | Cpu::IF;
    cpu.write64(output, 0x1122334455667788ULL);

    Run run{};
    const auto start_steps = cpu.instructions;
    constexpr unsigned deadline = 62; // Between instructions inside a loop round.
    gate(machine, 0x40);
    emit(cpu, irq_handler, {0x49,0xff,0xc5,0x48,0xcf}); // INC R13; IRETQ.
    machine.device_step_hook = [&] {
      if (++run.ticks != deadline) return;
      run.irq_rip = cpu.eip;
      run.irq_steps = cpu.instructions - start_steps;
      run.irq_word = cpu.read64(output);
      require(cpu.raise_external_irq(0x40), "repeated block lost its IRQ");
    };
    machine.device_batch_limit_hook = [&](unsigned budget) {
      return run.ticks < deadline ? std::min(budget, unsigned(deadline - run.ticks)) : budget;
    };
    machine.device_batch_advance_hook = [&](unsigned steps) { run.ticks += steps; };
    machine.run(351);
    require(cpu.halted && !cpu.fault && cpu.regs[Cpu::EDX] == 0 &&
            cpu.regs[Cpu::R13] == 1 && cpu.instructions - start_steps == 203,
            "repeated block failed to retire the exact guest loop and IRQ");
    run.terminal = capture(cpu);
    run.stats = cpu.ether_arm64_stats();
    return run;
  };

  const auto native = execute(true);
  const auto scalar = execute(false);
  require(native.terminal == scalar.terminal && native.ticks == scalar.ticks &&
          native.irq_rip == scalar.irq_rip && native.irq_word == scalar.irq_word &&
          native.irq_steps == scalar.irq_steps && native.irq_steps == 61,
          "native rounds crossed an IRQ boundary or changed overlapping RAM writes");
  require(native.stats.repeated_loop_dispatches >= 2 && native.stats.repeated_loop_steps >= 150 &&
          native.stats.coalesced_ram_writes >= 50 && scalar.stats.repeated_loop_steps == 0,
          "fixed-address loop did not amortize dispatch and RAM writes");
  std::cout << "PRIME_ETHER_REPEATED_LOOP_PASS steps=" << native.stats.repeated_loop_steps
            << " dispatches=" << native.stats.repeated_loop_dispatches << " irq_step=" << native.irq_steps << '\n';
}

void register_shift_contract() {
  struct Case {
    unsigned width;
    std::uint8_t opcode, modrm, count;
    std::uint64_t input, result;
    std::uint32_t arithmetic_flags;
  };
  constexpr auto baseline_flags = 2 | Cpu::IF | Cpu::AF;
  constexpr std::array<Case, 7> cases{{
      {4,0xd1,0xe0,1,0xdeadbeef80000001,2,Cpu::CF | Cpu::OF},
      {8,0xc1,0xf0,63,0x8000000000000001,0x8000000000000000,Cpu::SF | Cpu::PF | Cpu::OF},
      {4,0xd3,0xe8,1,0xdeadbeef80000001,0x40000000,Cpu::CF | Cpu::OF | Cpu::PF},
      {8,0xd3,0xf8,1,0x8000000000000001,0xc000000000000000,Cpu::CF | Cpu::SF | Cpu::PF},
      {4,0xc1,0xf8,32,0xdeadbeef80000001,0x80000001,Cpu::OF | Cpu::ZF},
      {8,0xd3,0xe8,64,0x8000000000000001,0x8000000000000001,Cpu::OF | Cpu::ZF},
      {4,0xd3,0xe8,255,0xdeadbeef00000001,0,Cpu::OF | Cpu::ZF | Cpu::PF},
  }};
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  for (const auto& item : cases) {
    for (auto* machine : {&native, &scalar}) {
      auto& cpu = machine->cpu;
      std::vector<std::uint8_t> program{0x8b,0xd2}; // MOV EDX,EDX leaves the flags alone.
      if (item.width == 8) program.push_back(0x48);
      program.insert(program.end(), {item.opcode, item.modrm});
      if (item.opcode == 0xc1) program.push_back(item.count);
      program.push_back(0xf4);
      cpu.memory.write_block(code, program.data(), program.size());
      cpu.eip = code;
      cpu.regs[Cpu::EAX] = item.input;
      cpu.regs[Cpu::ECX] = 0xfedcba9876543200ULL | item.count;
      cpu.eflags = baseline_flags | Cpu::OF | Cpu::ZF;
      if (machine == &native && Cpu::ether_arm64_host()) warm_block(cpu, code, 2);
      else require(cpu.step() == StepResult::Executed && cpu.step() == StepResult::Executed,
                   "scalar shift fixture faulted");
      require(cpu.regs[Cpu::EAX] == item.result && cpu.eflags == (baseline_flags | item.arithmetic_flags),
              "register shift disagreed with its independently calculated result and flags");
    }
    require(capture(native.cpu) == capture(scalar.cpu),
            "shift results, flags or retirement differed between paths");
  }
}

void integer_execution_contract() {
  const std::array<std::uint8_t, 37> program{
      0xb8,0x78,0x56,0x34,0x12,              // MOV EAX,12345678h
      0xd3,0xe0,                              // SHL EAX,CL
      0x81,0xe0,0xfc,0xff,0xff,0x7f,          // AND EAX,7FFFFFFCh
      0x09,0xd0,                              // OR EAX,EDX
      0x35,0xb7,0x18,0x67,0x21,              // XOR EAX,216718B7h
      0x8b,0xf0,                              // MOV ESI,EAX
      0x81,0xc1,0x01,0,0,0,                 // ADD ECX,1
      0x81,0xf9,0x20,0x4e,0,0,              // CMP ECX,20000
      0x75,0xdc,0xf4};                        // JNZ; HLT
  constexpr unsigned iterations = 20'000, steps = 9 * iterations + 1;
  for (const bool long_mode : {false, true}) {
    PcMachine native, scalar;
    const auto entry = long_mode ? code : protected_code;
    const auto init = [&](PcMachine& machine, bool translated) {
      if (long_mode) boot(machine, translated);
      else boot_protected(machine, translated);
      machine.cpu.memory.write_block(entry, program.data(), program.size());
    };
    init(native, true);
    init(scalar, false);
    const auto run = [&](PcMachine& machine) {
      auto& cpu = machine.cpu;
      cpu.eip = entry;
      cpu.halted = false;
      cpu.eflags = 2 | Cpu::AF | Cpu::OF;
      cpu.regs[Cpu::ECX] = 0;
      cpu.regs[Cpu::EDX] = 3;
      const auto initial_steps = cpu.instructions;
      const auto start = std::chrono::steady_clock::now();
      for (unsigned remaining = steps; remaining;) {
        const auto slice = std::min(remaining, 50'000U);
        machine.run(slice);
        remaining -= slice;
      }
      const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
      const auto last = std::uint32_t(0x12345678U << ((iterations - 1) & 31));
      require(!cpu.fault && cpu.halted && cpu.instructions - initial_steps == steps &&
              cpu.regs[Cpu::ECX] == iterations &&
              cpu.regs[Cpu::ESI] == (((last & 0x7ffffffcU) | 3) ^ 0x216718b7U),
              "integer fixture did not complete the expected guest work");
      return ns;
    };
    std::array<std::int64_t, 5> native_ns{}, scalar_ns{};
    for (unsigned i = 0; i < native_ns.size(); ++i) {
      if (i & 1) {
        native_ns[i] = run(native);
        scalar_ns[i] = run(scalar);
      } else {
        scalar_ns[i] = run(scalar);
        native_ns[i] = run(native);
      }
      require(capture(native.cpu) == capture(scalar.cpu),
              "integer stream changed registers, flags or instruction retirement");
    }
    const auto coverage = native.cpu.ether_arm64_stats().steps;
    require(coverage * 100 >= steps * native_ns.size() * 99 &&
            coverage == native.boot_acceleration().arm64_block_steps &&
            scalar.cpu.ether_arm64_stats().steps == 0,
            "integer fixture did not execute its blocks through the normal Éter path");
    std::sort(native_ns.begin(), native_ns.end());
    std::sort(scalar_ns.begin(), scalar_ns.end());
    std::cout << "PRIME_ETHER_INTEGER_PASS mode=" << (long_mode ? "long64" : "protected32")
              << " steps=" << steps << " total_native_steps=" << coverage << " repeats=" << native_ns.size()
              << " native_median_ns=" << native_ns[2] << " scalar_median_ns=" << scalar_ns[2] << '\n';
  }
}

void rip_relative_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  const auto data = code + 0x2000;
  for (auto* machine : {&native, &scalar}) {
    emit(machine->cpu, code, {
        0x49,0xb8,0xef,0xcd,0xab,0x89,0x67,0x45,0x23,0x81,  // MOVABS R8
        0x48,0xc7,0x05,0xeb,0x1f,0,0,0,0,0,0x80,           // MOV qword [RIP+1FEBh],80000000h
        0x4c,0x8b,0x1d,0xe4,0x1f,0,0,                      // MOV R11,[RIP+1FE4h]
        0x48,0xf7,0x05,0xd9,0x1f,0,0,0,0,0,0x80,          // TEST qword [RIP+1FD9h],80000000h
        0x75,1,0x90,0xf4});                                 // JNZ skips NOP; HLT
  }
  for (unsigned pass = 0; pass < 5; ++pass) {
    for (auto* machine : {&native, &scalar}) {
      auto& cpu = machine->cpu;
      cpu.eip = code;
      cpu.halted = false;
      cpu.eflags = 2 | Cpu::AF;
      cpu.write64(data, 0);
      const auto initial = cpu.instructions;
      machine->run(6);
      require(cpu.halted && !cpu.fault && cpu.instructions - initial == 6 &&
              cpu.regs[Cpu::R8] == 0x8123456789abcdefULL &&
              cpu.regs[Cpu::R11] == 0xffffffff80000000ULL &&
              cpu.read64(data) == 0xffffffff80000000ULL, "RIP-relative literal or memory state diverged");
    }
    require(capture(native.cpu) == capture(scalar.cpu), "RIP-relative native path changed guest state");
  }
  require(native.cpu.ether_arm64_stats().steps >= 12, "RIP-relative fixture stayed in the interpreter");
  std::cout << "PRIME_ETHER_RIP_RELATIVE_PASS\n";
}

void protected32_contract() {
  PcMachine native, scalar;
  boot_protected(native, true);
  boot_protected(scalar, false);
  constexpr unsigned count = 20'000, steps = count * 7 + 1;
  for (auto* machine : {&native, &scalar}) {
    auto& cpu = machine->cpu;
    cpu.memory.write_block(protected_code, protected_loop.data(), protected_loop.size());
    for (unsigned i = 0; i < count; ++i) cpu.write16(ds_base + nodes + 2 * i, std::uint16_t(i ^ 0x652e));
    cpu.write32(stack, 0x12345678); // The stack store must use SS, not physical EBP.
  }
  const auto measure = [&](PcMachine& machine) {
    auto& cpu = machine.cpu;
    cpu.halted = false;
    cpu.eip = protected_code;
    cpu.eflags = 2 | Cpu::CF | Cpu::AF;
    cpu.regs[Cpu::ECX] = count;
    cpu.regs[Cpu::EAX] = 0x0123456789abcdefULL;
    cpu.regs[Cpu::EDX] = 0x13579bdfULL;
    cpu.regs[Cpu::ESI] = 0x2468ace0ULL;
    cpu.regs[Cpu::EDI] = 0xdeadbeef00000000ULL | nodes;
    cpu.regs[Cpu::EBP] = stack + 8;
    cpu.regs[Cpu::EBX] = 0xd23f6ae7;
    const auto initial = cpu.instructions;
    const auto start = std::chrono::steady_clock::now();
    for (unsigned remaining = steps; remaining;) {
      const auto slice = std::min(remaining, 50'000U);
      machine.run(slice);
      remaining -= slice;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    require(!cpu.fault && cpu.halted && cpu.instructions - initial == steps &&
            cpu.regs[Cpu::EAX] == (0x0123456789ab0000ULL | ((count - 1) ^ 0x652e)) &&
            cpu.read32(ss_base + stack) == nodes + 2 * (count - 1) &&
            cpu.read32(stack) == 0x12345678, "protected32 loop changed counts, AX or SS");
    return elapsed;
  };
  std::array<std::int64_t, 5> native_ns{}, scalar_ns{};
  for (unsigned i = 0; i < native_ns.size(); ++i) {
    if (i & 1) {
      native_ns[i] = measure(native);
      scalar_ns[i] = measure(scalar);
    } else {
      scalar_ns[i] = measure(scalar);
      native_ns[i] = measure(native);
    }
    require(capture(native.cpu) == capture(scalar.cpu),
            "protected32 native execution diverged from scalar state");
  }
  const auto native_steps = native.cpu.ether_arm64_stats().protected32_steps;
  require(native_steps * 100 >= steps * native_ns.size() * 99 &&
          native.cpu.ether_arm64_stats().steps == native_steps &&
          scalar.cpu.ether_arm64_stats().steps == 0, "protected32 missed its native path");
  auto& a = native.cpu;
  auto& b = scalar.cpu;
  const auto guard = protected_code + 0x100;
  for (auto* cpu : {&a, &b}) {
    emit(*cpu, guard, {0x8b,0xc1,0x66,0x8b,0x07}); // Stage EAX, then cross a page to load AX.
    cpu->halted = false;
    cpu->eip = guard;
    cpu->regs[Cpu::EDI] = 0x17fff;
    cpu->write16(ds_base + 0x17fff, 0xfe90);
  }
  const auto before = capture(a);
  for (unsigned i = 0; i < 4; ++i)
    require(!a.try_ether_arm64_block(2) && capture(a) == before,
            "a rejected protected32 load committed staged register state");
  require(a.ether_arm64_stats().data_fallbacks > 0, "protected32 data guard was never exercised");
  for (auto* cpu : {&a, &b})
    require(cpu->step() == StepResult::Executed && cpu->step() == StepResult::Executed,
            "scalar cross-page load failed");
  require(capture(a) == capture(b) && a.reg16(Cpu::EAX) == 0xfe90,
          "protected32 data fallback changed the guest result");
  std::sort(native_ns.begin(), native_ns.end());
  std::sort(scalar_ns.begin(), scalar_ns.end());
  std::cout << "PRIME_ARM64_PROTECTED32_PASS steps=" << steps << " native_steps=" << native_steps
            << " repeats=" << native_ns.size() << " native_median_ns=" << native_ns[2]
            << " scalar_median_ns=" << scalar_ns[2] << '\n';
}

void byte_copy_contract() {
  PcMachine native, scalar;
  boot(native, true);
  boot(scalar, false);
  constexpr unsigned count = 512;
  std::array<std::uint8_t, count> source{}, actual{};
  for (unsigned i = 0; i < count; ++i) source[i] = std::uint8_t(0xdfU + 131U * i);
  for (auto* machine : {&native, &scalar}) {
    auto& cpu = machine->cpu;
    cpu.memory.write_block(code, byte_copy_loop.data(), byte_copy_loop.size());
    cpu.memory.write_block(nodes, source.data(), source.size());
    cpu.regs[Cpu::ECX] = 0xdeadbeef12345678ULL;
    cpu.regs[Cpu::EDX] = nodes;
    cpu.regs[Cpu::EBX] = 1;
    cpu.regs[Cpu::R10] = count;
    cpu.regs[Cpu::R11] = 0;
    cpu.regs[Cpu::R14] = 0x12345678fffffff0ULL;
    cpu.regs[Cpu::R15] = output;
    machine->run(count * 8 + 1);
    cpu.memory.read_block(output, actual.data(), actual.size());
    require(!cpu.fault && cpu.halted && source == actual &&
            cpu.regs[Cpu::R14] == 496 && cpu.regs[Cpu::R15] == output + count &&
            cpu.regs[Cpu::ECX] == (0xdeadbeef12345600ULL | source.back()),
            "long64 observed indexed byte copy changed memory, width or address");
  }
  require(capture(native.cpu) == capture(scalar.cpu) &&
          native.cpu.ether_arm64_stats().steps >= count * 7,
          "long64 native byte copy did not match the scalar path");
  std::cout << "PRIME_ARM64_BYTE_COPY_PASS bytes=" << count << '\n';
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

void byte_read_benchmark() {
  constexpr unsigned steps = 40000;
  const auto run = [](bool translated) {
    PcMachine machine;
    boot(machine, translated);
    auto& cpu = machine.cpu;
    cpu.memory.write_block(code, byte_read_loop.data(), byte_read_loop.size());
    cpu.write64(nodes + 8, 0x78ab1234fedc00a6ULL);
    cpu.regs[Cpu::ECX] = nodes;
    cpu.regs[Cpu::EDX] = 9;
    cpu.regs[Cpu::R11] = 0xfedcba9876543210ULL;
    const auto flags = 2 | Cpu::CF | Cpu::AF | Cpu::OF | Cpu::PF;
    cpu.eflags = flags;
    const auto before = cpu.instructions;
    const auto start = std::chrono::steady_clock::now();
    machine.run(steps);
    const auto elapsed = std::chrono::steady_clock::now() - start;
    require(!cpu.fault && cpu.eip == code && cpu.instructions - before == steps &&
            cpu.regs[Cpu::R11] == 0xfedcba98765432a6ULL && cpu.eflags == flags &&
            cpu.read64(nodes + 8) == 0x78ab1234fedc00a6ULL,
            "byte load changed the upper register bits, flags, RAM or guest step count");
    Result result{};
    result.state = capture(cpu);
    result.stats = cpu.ether_arm64_stats();
    result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count();
    return result;
  };
  std::array<std::uint64_t, 7> native_times{}, scalar_times{};
  for (unsigned i = 0; i < native_times.size(); ++i) {
    const bool native_first = i % 2 == 0;
    const auto first = run(native_first);
    const auto second = run(!native_first);
    const auto& native = native_first ? first : second;
    const auto& scalar = native_first ? second : first;
    require(native.state == scalar.state && native.stats.steps * 100 >= steps * 99,
            "byte read loop missed native execution or diverged from the interpreter");
    native_times[i] = native.elapsed_ns;
    scalar_times[i] = scalar.elapsed_ns;
  }
  std::sort(native_times.begin(), native_times.end());
  std::sort(scalar_times.begin(), scalar_times.end());
  std::cout << "ETHER_ARM64_BYTE_READ_LOOP steps=" << steps
            << " native_median_ns=" << native_times[3] << " scalar_median_ns=" << scalar_times[3] << '\n';
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
  const auto instruction_page_reuses = cpu.instruction_page_reuses;
  warm_block(cpu, linear, 2);
  const auto stable = cpu.ether_arm64_stats();
  require(stable.code_version_hits == initial.code_version_hits + 1 &&
          stable.code_bytes_read == initial.code_bytes_read, "warm block reread validated code");
  require(cpu.instruction_page_reuses == instruction_page_reuses + 1,
          "warm native block missed its guarded instruction mapping");
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
    register_shift_contract();
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
    integer_execution_contract();
    rip_relative_contract();
    signed_load_contract();
    memory_subtraction_contract();
    staged_ram_contract();
    repeated_loop_contract();
    protected32_contract();
    byte_copy_contract();
    guard_contract();
    cache_collision_contract();
    flag_contract();
    observed_flags_contract();
    scan_forms_contract();
    immediate_arithmetic_contract();
    indexed_loop_benchmark();
    byte_read_benchmark();
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
