#include "../pc_machine.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;
using cosmic_pc::StepResult;
constexpr std::uint64_t entry = 0xa000;
using Clock = std::chrono::steady_clock;

void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

void boot(PcMachine& pc, bool paged) {
    if (paged) {
        pc.boot_long_mode_demo();
        pc.run(4000);
        require(pc.cpu.is_long_mode() && pc.cpu.halted && !pc.cpu.fault, "long-mode fixture did not boot");
        pc.cpu.enable_page_translation_cache();
    } else {
        pc.boot_demo();
    }
}

void prepare(PcMachine& pc, bool paged, std::initializer_list<std::uint8_t> program,
             unsigned reg, std::uint64_t value, std::uint64_t start = entry, unsigned seed_steps = 2) {
    boot(pc, paged);
    auto& cpu = pc.cpu;
    cpu.memory.write_block(start, program.begin(), program.size());
    cpu.eip = start;
    cpu.halted = false;
    cpu.regs[reg] = value;
    cpu.eflags = 2 | Cpu::CF | Cpu::DF | Cpu::OF;
    for (unsigned i = 0; i < seed_steps; ++i)
        require(cpu.step() == StepResult::Executed, "could not execute the scalar loop seed");
    require(cpu.eip == start && cpu.counter_loop_batch_candidate(), "expected a taken scalar JNZ seed");
}

void compare(bool paged, std::initializer_list<std::uint8_t> program, unsigned reg,
             std::uint64_t value, unsigned budget, unsigned expected, unsigned seed_steps = 2) {
    PcMachine scalar(4U << 20), batch(4U << 20);
    prepare(scalar, paged, program, reg, value, entry, seed_steps);
    prepare(batch, paged, program, reg, value, entry, seed_steps);
    const auto steps = batch.cpu.try_counter_loop_batch(budget);
    require(steps == expected, "batch retired a wrong iteration count");
    for (unsigned i = 0; i < steps; ++i)
        require(scalar.cpu.step() == StepResult::Executed, "scalar counter loop failed");
    const auto& a = scalar.cpu;
    const auto& b = batch.cpu;
    require(a.regs == b.regs && a.eflags == b.eflags && a.eip == b.eip &&
            a.instructions == b.instructions && a.clock_cycles() == b.clock_cycles() &&
            a.last_decoded_bytes() == b.last_decoded_bytes(), "counter batch changed x86 state");
    require(a.counter_loop_batch_candidate() == b.counter_loop_batch_candidate(),
            "counter loop was left in a different dispatch state");
}

void unsafe_spans() {
    PcMachine pc(4U << 20);
    prepare(pc, false, {0x49, 0x75, 0xfd}, Cpu::ECX, 30);
    auto& cpu = pc.cpu;
    const auto before = cpu.regs;
    const auto retired = cpu.instructions;
    cpu.write8(entry, 0x90); // Change the DEC after the taken branch.
    require(cpu.try_counter_loop_batch(100) == 0, "modified code was executed from a stale signature");
    cpu.write8(entry, 0x49);
    cpu.step_trace_hook = [](const Cpu::StepTrace&) {};
    require(cpu.try_counter_loop_batch(100) == 0, "counter loop hid an instruction from tracing");
    cpu.step_trace_hook = {};
    cpu.mmio_claim_hook = [](std::uint64_t address) { return address == entry + 1; };
    cpu.mmio_range_claim_hook = [](std::uint64_t address, std::size_t bytes) {
        return address <= entry + 1 && entry + 1 - address < bytes;
    };
    require(cpu.try_counter_loop_batch(100) == 0 && cpu.regs == before &&
            cpu.eip == entry && cpu.instructions == retired, "a denied batch altered guest state");

    PcMachine crossed(4U << 20);
    prepare(crossed, true, {0x48, 0xff, 0xc9, 0x75, 0xfb}, Cpu::ECX, 30, 0xbfff);
    require(crossed.cpu.try_counter_loop_batch(100) == 0, "page-crossing code must use the scalar fetch path");
}

void measure_large_loop(bool accumulate) {
    constexpr std::uint64_t budget = 369'000'000'000'000;
    const unsigned seed_steps = accumulate ? 3 : 2;
    const auto iterations = budget / seed_steps;
    PcMachine pc(4U << 20);
    if (accumulate)
        prepare(pc, true, {0x49,0x83,0xc0,7,0x49,0xff,0xc9,0x75,0xf7,0x0f,0x31},
                Cpu::R9, iterations, entry, seed_steps);
    else
        prepare(pc, true, {0x49,0xff,0xc9,0x75,0xfb,0x0f,0x31}, Cpu::R9, iterations);
    auto& cpu = pc.cpu;
    std::array<std::int64_t, 101> samples{};
    for (auto& elapsed : samples) {
        cpu.eip = entry;
        cpu.regs[Cpu::R8] = accumulate ? 0U - iterations * 7 : 7;
        cpu.regs[Cpu::R9] = iterations;
        cpu.eflags = 2 | Cpu::CF | Cpu::DF | Cpu::OF;
        const auto retired = cpu.instructions;
        const auto clock = cpu.clock_cycles();
        const auto start = Clock::now();
        for (unsigned i = 0; i < seed_steps; ++i)
            require(cpu.step() == StepResult::Executed, "could not seed the large loop");
        const auto batched = cpu.try_counter_loop_batch(budget - seed_steps);
        elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        require(batched == budget - seed_steps && cpu.regs[Cpu::R8] == (accumulate ? 0U : 7U) &&
                cpu.regs[Cpu::R9] == 0 && cpu.eflags == (2 | Cpu::CF | Cpu::DF | Cpu::PF | Cpu::ZF) &&
                cpu.eip == entry + (accumulate ? 9 : 5) && cpu.instructions - retired == budget &&
                cpu.clock_cycles() - clock == budget, "large loop changed registers, flags or instruction count");
        require(cpu.step() == StepResult::Executed &&
                (cpu.regs[Cpu::EAX] | (cpu.regs[Cpu::EDX] << 32)) ==
                    (clock + budget) * Cpu::kTscTicksPerCycle, "large loop changed the x86 clock");
    }
    std::sort(samples.begin(), samples.end());
    std::cout << (accumulate ? "ETHER_ACCUMULATOR_64BIT_BUDGET_PASS" : "ETHER_COUNTER_64BIT_BUDGET_PASS")
              << " equivalent_steps=" << budget << " repeats=" << samples.size()
              << " median_ns=" << samples[samples.size() / 2] << '\n';
}

void guarded_accumulator_dispatch() {
    PcMachine pc(4U << 20);
    prepare(pc, true, {0x49,0x83,0xc1,7,0x49,0xff,0xc9,0x75,0xf7},
            Cpu::R9, 23, entry, 3);
    const auto before = pc.cpu.regs;
    const auto retired = pc.cpu.instructions;
    require(!pc.cpu.try_counter_loop_batch(33) && pc.cpu.regs == before &&
            pc.cpu.instructions == retired, "an accumulator aliased its countdown register");

    prepare(pc, true, {0x49,0x83,0xc0,7,0x49,0xff,0xc9,0x75,0xf7},
            Cpu::R9, 23, entry, 3);
    pc.enable_boot_acceleration();
    unsigned ticks = 0;
    pc.device_step_hook = [&ticks] { ++ticks; };
    pc.device_batch_limit_hook = [](unsigned budget) { return std::min(budget, 13U); };
    pc.device_batch_advance_hook = [&ticks](unsigned extra) { ticks += extra; };
    const auto original = pc.cpu.regs[Cpu::R8];
    const auto initial = pc.cpu.instructions;
    pc.run(3 * 22);
    require(ticks == 3 * 22 && pc.cpu.instructions - initial == ticks &&
            pc.cpu.regs[Cpu::R8] == original + 22 * 7 && pc.cpu.regs[Cpu::R9] == 0 &&
            pc.boot_acceleration().counter_loop_steps > 0 && !pc.cpu.fault,
            "machine dispatch changed device ticks or skipped the accumulator");
}

void measure_mixed(bool native) {
    PcMachine pc(4U << 20);
    boot(pc, true);
    pc.enable_boot_acceleration();
    auto& cpu = pc.cpu;
    cpu.enable_ether_arm64_blocks(native);
    constexpr std::uint64_t data = 0x20000, stack = 0x2e000, output = 0x30000;
    constexpr std::uint64_t seed = 0xfedcba9876543210, multiplier = 0x1000101;
    constexpr unsigned rounds = 10'000'000, steps = 11 * rounds + 1;
    constexpr std::array<std::uint8_t, 34> program{
        0x8b,0x04,0x96,              // MOV EAX,[RSI+RDX*4]
        0x48,0x0f,0xaf,0xc3,         // IMUL RAX,RBX
        0x49,0x01,0xc0,              // ADD R8,RAX
        0x41,0x50,0x41,0x59,         // PUSH R8; POP R9
        0x49,0x31,0xc9,              // XOR R9,RCX
        0x48,0x83,0xc2,1,            // ADD RDX,1
        0x83,0xe2,63,               // AND EDX,63
        0x4c,0x89,0x0f,              // MOV [RDI],R9
        0x48,0x83,0xe9,1,            // SUB RCX,1
        0x75,0xdf,0xf4};             // JNZ; HLT
    cpu.memory.write_block(entry, program.data(), program.size());
    // Sum the 64-entry input cycle independently of the guest loop.
    std::uint64_t expected = seed;
    for (unsigned i = 0; i < 64; ++i) {
        const auto value = std::uint32_t((i + 1) * 0x12345U) ^ 0xc4e72ed9U;
        cpu.write32(data + 4 * i, value);
        expected += std::uint64_t(value) * multiplier * (rounds / 64 + (i < rounds % 64));
    }
    cpu.halted = false;
    cpu.eip = entry;
    cpu.eflags = 2;
    cpu.regs[Cpu::EBX] = multiplier;
    cpu.regs[Cpu::ESI] = data;
    cpu.regs[Cpu::EDI] = output;
    cpu.regs[Cpu::ESP] = stack;
    cpu.regs[Cpu::ECX] = rounds;
    cpu.regs[Cpu::EDX] = 0;
    cpu.regs[Cpu::R8] = seed;
    const auto retired = cpu.instructions;
    const auto start = Clock::now();
    for (auto remaining = steps; remaining;) {
        const auto slice = std::min(remaining, 50'000U);
        pc.run(slice);
        remaining -= slice;
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    const auto translated = cpu.ether_arm64_stats().steps;
    require(cpu.halted && !cpu.fault && cpu.instructions - retired == steps &&
            cpu.regs[Cpu::ECX] == 0 && cpu.regs[Cpu::EDX] == rounds % 64 &&
            cpu.regs[Cpu::ESP] == stack && cpu.regs[Cpu::R8] == expected &&
            cpu.eflags == (2 | Cpu::PF | Cpu::ZF) &&
            cpu.read64(output) == (expected ^ 1U) && cpu.read64(stack - 8) == expected,
            "mixed x86 run diverged from its independent register and memory calculation");
    const auto& accelerated = pc.boot_acceleration();
    require((translated != 0) == native && accelerated.rep_elements == 0 &&
            accelerated.shift_loop_steps == 0 && accelerated.scan_loop_steps == 0 &&
            accelerated.zero_loop_steps == 0 && accelerated.counter_loop_steps == 0,
            "mixed x86 run changed execution path or used a loop shortcut");
    std::cout << "ETHER_X86_MIXED_PASS mode=" << (native ? "arm64" : "interpreter")
              << " steps=" << steps << " native_steps=" << translated << " elapsed_ns=" << elapsed
              << " steps_per_s=" << double(steps) * 1e9 / double(elapsed) << '\n';
}
}

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string_view(argv[1]) == "--measure-mixed") {
            measure_mixed(false);
            if (Cpu::ether_arm64_host()) measure_mixed(true);
            return 0;
        }
        if (argc != 1) return 2;
        compare(false, {0x49, 0x75, 0xfd}, Cpu::ECX, 0x1122334455660021, 17, 16);
        compare(false, {0x49, 0x75, 0xfd}, Cpu::ECX, 0x1122334455660006, 20, 10);
        compare(false, {0x49, 0x75, 0xfd}, Cpu::ECX, 0x1122334455660000, 41, 40);
        compare(false, {0x66, 0xff, 0xc9, 0x75, 0xfb}, Cpu::ECX, 0x1122334400000010, 12, 12);
        compare(true, {0x66, 0x41, 0xff, 0xc9, 0x75, 0xfa}, Cpu::R9, 0x1122334455660008, 40, 14);
        compare(true, {0x41, 0x83, 0xe9, 1, 0x75, 0xfa}, Cpu::R9, 0x1122334400000008, 40, 14);
        compare(true, {0x49, 0xff, 0xc9, 0x75, 0xfb}, Cpu::R9, 0x8000000000000002, 4, 4);
        compare(true, {0x49,0x83,0xc0,0x80,0x49,0xff,0xc9,0x75,0xf7}, Cpu::R9, 30, 31, 30, 3);
        compare(true, {0x49,0x83,0xe8,0xff,0x49,0x83,0xe9,1,0x75,0xf6}, Cpu::R9, 5, 30, 12, 3);
        unsafe_spans();
        measure_large_loop(false);
        measure_large_loop(true);
        guarded_accumulator_dispatch();
        std::cout << "ETHER_COUNTER_LOOP_PASS widths=16,32,64 flags=1 paging=1 fallbacks=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ETHER_COUNTER_LOOP_FAIL " << error.what() << '\n';
        return 1;
    }
}
