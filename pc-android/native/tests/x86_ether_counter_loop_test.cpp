#include "../pc_machine.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

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

void measure_rotate_xor_loop() {
    constexpr std::uint64_t rounds = (1ULL << 40) + 11;
    constexpr unsigned steps_per_round = 4;
    constexpr std::uint64_t initial = 0x92ba8401381c0fffULL;
    PcMachine pc(4U << 20);
    prepare(pc, true, {0x49,0xc1,0xc0,17, 0x49,0x83,0xf0,0xf3, 0x49,0xff,0xc9, 0x75,0xf3},
            Cpu::R9, rounds, entry, steps_per_round);
    auto& cpu = pc.cpu;
    // A 64-bit rotation by 17 visits every bit in 64 rounds; applying
    // those same XOR masks twice cancels them. Check a nonzero tail.
    auto expected = initial;
    for (unsigned i = 0; i < rounds % 128; ++i)
        expected = ((expected << 17) | (expected >> 47)) ^ 0xfffffffffffffff3ULL;
    std::array<std::int64_t, 101> samples{};
    for (auto& ns : samples) {
        cpu.eip = entry;
        cpu.eflags = 2 | Cpu::DF | Cpu::CF;
        cpu.regs[Cpu::R8] = initial;
        cpu.regs[Cpu::R9] = rounds;
        const auto before = cpu.instructions;
        const auto start = Clock::now();
        for (unsigned i = 0; i < steps_per_round; ++i)
            require(cpu.step() == StepResult::Executed, "rotate/XOR seed failed");
        const auto completed = cpu.try_counter_loop_batch((rounds - 1) * steps_per_round);
        ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
        require(completed == (rounds - 1) * steps_per_round && cpu.instructions - before == rounds * steps_per_round &&
                    cpu.regs[Cpu::R8] == expected && cpu.regs[Cpu::R9] == 0 && cpu.eip == entry + 13 &&
                    cpu.eflags == (2 | Cpu::DF | Cpu::ZF | Cpu::PF), "rotate/XOR composition changed guest state");
    }
    std::sort(samples.begin(), samples.end());
    std::cout << "ETHER_ROTATE_XOR_PASS equivalent_steps=" << rounds * steps_per_round
              << " median_ns=" << samples[samples.size() / 2] << '\n';
}

void timed_rotate_xor_slice() {
    constexpr unsigned steps = 2'000'000, rounds = 800'000;
    PcMachine pc(4U << 20);
    prepare(pc, true, {0x49,0xc1,0xc0,17, 0x49,0x83,0xf0,0xf3, 0x49,0xff,0xc9, 0x75,0xf3},
            Cpu::R9, rounds, entry, 4);
    pc.enable_boot_acceleration();
    const auto before = pc.cpu.instructions;
    pc.run(steps);
    require(pc.cpu.instructions - before == PcMachine::kDefaultRunSteps, "legacy run budget changed");
    const auto timed_start = pc.cpu.instructions;
    pc.run(steps, PcMachine::RunClock::now() - std::chrono::milliseconds(1));
    require(pc.cpu.instructions == timed_start, "expired host slice executed guest instructions");
    pc.run(steps, PcMachine::RunClock::now() + std::chrono::seconds(1));
    require(pc.cpu.instructions - timed_start == steps && pc.cpu.eip == entry &&
                pc.cpu.regs[Cpu::R9] == rounds - 1 - (steps + PcMachine::kDefaultRunSteps) / 4 &&
                pc.boot_acceleration().rotate_xor_loop_steps == steps + PcMachine::kDefaultRunSteps,
            "timed slice changed the guest instruction budget or countdown");
    std::cout << "ETHER_TIMED_ROTATE_XOR_PASS steps=" << steps << '\n';
}

void affine_ram_rotations() {
    constexpr std::array<std::uint8_t, 24> program{
        0x4d,0x33,0x02, 0x4c,0x03,0xd6, 0x49,0xd3,0xc0, // XOR R8,[R10]; ADD R10,RSI; ROL R8,CL
        0x4d,0x33,0x02, 0x4c,0x03,0xd6, 0x49,0xd3,0xc0,
        0x48,0x2b,0xc7, 0x75,0xe9, 0xf4};              // SUB RAX,RDI; JNZ; HLT
    constexpr std::uint64_t data = 0x18000, decrement = 7;
    constexpr unsigned steps_per_round = 8;
    const auto prepare_ram = [&](PcMachine& pc, std::uint64_t initial, std::uint64_t stride,
                                 unsigned count, unsigned rounds, bool right) {
        boot(pc, true);
        auto& cpu = pc.cpu;
        cpu.memory.enable_code_page_versions(true);
        auto bytes = program;
        if (right) bytes[17] = 0xc8;
        cpu.memory.write_block(entry, bytes.data(), bytes.size());
        for (unsigned offset = 0; offset < 8192; offset += 8) {
            const auto mask = (0x9e3779b97f4a7c15ULL * (offset + 1)) ^ (offset >> 3);
            cpu.write64(data + offset, mask);
        }
        cpu.halted = false;
        cpu.eip = entry;
        cpu.eflags = 2 | Cpu::DF | Cpu::CF | Cpu::OF;
        cpu.regs[Cpu::EAX] = rounds * decrement;
        cpu.regs[Cpu::ECX] = count;
        cpu.regs[Cpu::EDI] = decrement;
        cpu.regs[Cpu::ESI] = stride;
        cpu.regs[Cpu::R8] = 0xfeeab001d1376363ULL;
        cpu.regs[Cpu::R10] = initial;
        for (unsigned i = 0; i < steps_per_round; ++i)
            require(cpu.step() == StepResult::Executed, "could not seed the RAM rotation loop");
        require(cpu.affine_ram_rotate_loop_candidate(), "expected a taken branch over a runtime rotation");
    };
    const auto finish_scalar = [&](PcMachine& scalar, const PcMachine& batch, unsigned steps) {
        for (unsigned i = 0; i < steps; ++i)
            require(scalar.cpu.step() == StepResult::Executed, "scalar RAM rotation failed");
        require(scalar.cpu.regs == batch.cpu.regs && scalar.cpu.eflags == batch.cpu.eflags &&
                    scalar.cpu.eip == batch.cpu.eip && scalar.cpu.instructions == batch.cpu.instructions &&
                    scalar.cpu.clock_cycles() == batch.cpu.clock_cycles() &&
                    scalar.cpu.last_decoded_bytes() == batch.cpu.last_decoded_bytes(),
                "composing RAM-derived masks changed architectural state");
    };
    unsigned cases = 0;
    for (auto count : {0U, 1U, 17U, 63U, 64U, 255U}) for (bool right : {false, true}) {
        const auto stride = right ? std::uint64_t(-8) : std::uint64_t(8);
        const auto initial = right ? data + 4096 : data;
        PcMachine scalar(4U << 20), batch(4U << 20);
        prepare_ram(scalar, initial, stride, count, 41, right);
        prepare_ram(batch, initial, stride, count, 41, right);
        require(!batch.cpu.try_affine_ram_rotate_loop(15), "a partial round was eligible");
        const auto first = batch.cpu.try_affine_ram_rotate_loop(47);
        require(first == 40, "RAM rotation did not obey the device step budget");
        finish_scalar(scalar, batch, first);
        // A later slice must use the new RAM word and the changed rotation.
        const auto changed = batch.cpu.regs[Cpu::R10];
        for (auto* pc : {&scalar, &batch}) {
            pc->cpu.write64(changed, 0xdef01024abd66601ULL);
            pc->cpu.write8(entry + 17, right ? 0xc0 : 0xc8);
        }
        const auto rest = batch.cpu.try_affine_ram_rotate_loop(1000);
        require(rest == 35 * steps_per_round && batch.cpu.eip == entry + 23,
                "RAM rotation did not stop when the x86 counter became zero");
        finish_scalar(scalar, batch, rest);
        ++cases;
    }

    PcMachine scalar(4U << 20), batch(4U << 20);
    for (auto* pc : {&scalar, &batch})
        prepare_ram(*pc, data + 4096 - 64, 8, 13, 30, false);
    constexpr auto mmio = data + 4096;
    unsigned device_reads = 0;
    batch.cpu.mmio_claim_hook = [](std::uint64_t address) { return address == mmio; };
    batch.cpu.mmio_range_claim_hook = [](std::uint64_t address, std::size_t bytes) {
        return address <= mmio && mmio - address < bytes;
    };
    batch.cpu.mmio_read_hook = [&](auto) -> std::optional<std::uint8_t> {
        ++device_reads;
        return 0xa1;
    };
    const auto whole = batch.cpu.try_affine_ram_rotate_loop(100);
    require(whole == 24 && device_reads == 0 && batch.cpu.regs[Cpu::R10] == mmio,
            "a RAM rotation read an MMIO register or retired a partial round");
    finish_scalar(scalar, batch, whole);
    const auto before = batch.cpu.regs;
    require(!batch.cpu.try_affine_ram_rotate_loop(100) && batch.cpu.regs == before &&
                batch.cpu.step() == StepResult::Executed && device_reads == 1,
            "the fallback did not leave the exact MMIO load for the interpreter");

    PcMachine routed(4U << 20);
    prepare_ram(routed, data, 8, 9, 24, false);
    unsigned ticks = 0;
    routed.enable_boot_acceleration();
    routed.device_step_hook = [&] { ++ticks; };
    routed.device_batch_limit_hook = [](unsigned budget) { return std::min(budget, 24U); };
    routed.device_batch_advance_hook = [&](unsigned extra) { ticks += extra; };
    routed.run(23 * steps_per_round);
    require(routed.cpu.regs[Cpu::EAX] == 0 && !routed.cpu.fault &&
                ticks == 23 * steps_per_round && routed.boot_acceleration().affine_ram_rotate_batches > 1,
            "RAM rotation did not integrate with the device clock and ordinary boot dispatch");
    std::cout << "ETHER_AFFINE_RAM_ROTATE_PASS cases=" << cases
              << " changing_ram=1 mmio=1 device_clock=1\n";
}

void sse_copy_loops() {
    // These are the two instruction streams sampled during the x64 ISO boot.
    constexpr std::uint8_t ordinary[]{
        0x0f,0x10,0x0c,0x11, 0x0f,0x10,0x54,0x11,0x10, 0x0f,0x10,0x5c,0x11,0x20,
        0x0f,0x10,0x64,0x11,0x30, 0x0f,0x29,0x41,0xf0, 0x48,0x83,0xc1,0x40, 0x49,0xff,0xc9,
        0x0f,0x29,0x49,0xc0, 0x0f,0x29,0x51,0xd0, 0x0f,0x29,0x59,0xe0, 0x0f,0x28,0xc4, 0x75,0xd1,
        0x0f,0x29,0x41,0xf0};
    constexpr std::uint8_t streaming[]{
        0x0f,0x10,0x0c,0x11, 0x0f,0x10,0x54,0x11,0x10, 0x0f,0x10,0x5c,0x11,0x20,
        0x0f,0x10,0x64,0x11,0x30, 0x0f,0x2b,0x41,0xf0, 0x48,0x83,0xc1,0x40, 0x0f,0x18,0x44,0x11,0x40,
        0x49,0xff,0xc9, 0x0f,0x2b,0x49,0xc0, 0x0f,0x2b,0x51,0xd0, 0x0f,0x2b,0x59,0xe0,
        0x0f,0x28,0xc4, 0x75,0xcc, 0x0f,0x2b,0x41,0xf0};
    constexpr std::uint64_t source = 0x2a005, destination = 0x2c000;
    constexpr unsigned rounds = 26;
    for (bool non_temporal : {false, true}) {
        const unsigned instructions = non_temporal ? 13 : 12;
        PcMachine scalar(4U << 20), fast(4U << 20);
        for (auto* pc : {&scalar, &fast}) {
            boot(*pc, true);
            auto& cpu = pc->cpu;
            cpu.cr0 &= ~0xcU;
            cpu.cr4 |= 0x200U;
            cpu.memory.write_block(entry, non_temporal ? streaming : ordinary,
                                   non_temporal ? sizeof(streaming) : sizeof(ordinary));
            for (unsigned i = 0; i < rounds * 64; ++i) cpu.write8(source + i, std::uint8_t(i ^ (i >> 4)));
            for (unsigned i = 0; i < 16; ++i) cpu.xmm[0][i] = std::uint8_t(0xf0 + i);
            cpu.eip = entry;
            cpu.halted = false;
            cpu.eflags = 2 | Cpu::CF | Cpu::DF;
            cpu.regs[Cpu::ECX] = destination + 16;
            cpu.regs[Cpu::EDX] = source - cpu.regs[Cpu::ECX];
            cpu.regs[Cpu::R9] = rounds;
            for (unsigned i = 0; i < instructions; ++i)
                require(cpu.step() == StepResult::Executed, "could not seed the SSE copy loop");
            require(cpu.counter_loop_batch_candidate() && cpu.eip == entry, "SSE loop branch did not return to its entry");
        }
        auto& cpu = fast.cpu;
        const auto registers = cpu.regs;
        const auto xmm = cpu.xmm;
        const auto retired = cpu.instructions;
        const auto denied = [&] {
            require(cpu.try_counter_loop_batch(10 * instructions) == 0 && cpu.instructions == retired &&
                    cpu.xmm == xmm && cpu.eip == entry && !cpu.fault, "SSE copy guard changed guest state");
        };
        cpu.write8(entry + 1, 0x11);
        denied();
        cpu.write8(entry + 1, 0x10);
        cpu.regs[Cpu::EDX] = -16ULL; // Same physical source and destination.
        denied();
        cpu.regs[Cpu::EDX] = registers[Cpu::EDX];
        cpu.regs[Cpu::ECX] = destination + 0xfd0; // Only one complete round before the page boundary.
        denied();
        cpu.regs[Cpu::ECX] = registers[Cpu::ECX];
        cpu.cr0 |= 8U;
        denied();
        cpu.cr0 &= ~8U;

        const auto compare = [&] {
            const auto& expected = scalar.cpu;
            require(cpu.regs == expected.regs && cpu.eflags == expected.eflags && cpu.xmm == expected.xmm &&
                    cpu.eip == expected.eip && cpu.clock_cycles() == expected.clock_cycles() &&
                    cpu.last_decoded_bytes() == expected.last_decoded_bytes(), "SSE batch changed x86 state");
            for (unsigned i = 0; i < rounds * 64 + 16; ++i)
                require(cpu.read8(destination + i) == expected.read8(destination + i), "SSE batch changed RAM");
        };
        const unsigned limit = 8 * instructions;
        fast.enable_boot_acceleration();
        unsigned ticks = 0;
        fast.device_step_hook = [&] { ++ticks; };
        fast.device_batch_limit_hook = [limit](unsigned budget) { return std::min(budget, limit); };
        fast.device_batch_advance_hook = [&](unsigned extra) { ticks += extra; };
        fast.run(limit);
        scalar.run(limit);
        compare();
        require(ticks == limit && fast.boot_acceleration().counter_loop_steps == limit,
                "SSE copy bypassed the production device budget or batch dispatch");

        const unsigned tail_steps = (rounds - 9) * instructions;
        require(cpu.try_counter_loop_batch(tail_steps + instructions - 1) == tail_steps,
                "SSE copy did not retire exactly the remaining rounds");
        scalar.run(tail_steps);
        compare();
        require(cpu.step() == StepResult::Executed && scalar.cpu.step() == StepResult::Executed,
                "SSE tail could not store the final vector");
        compare();
    }
    std::cout << "ETHER_SSE_COPY_LOOP_PASS variants=2 paging=1 state=1 device_bound=1\n";
}

constexpr std::uint8_t kCachedSseFill[]{
        0x0f,0x29,0x01, 0x0f,0x29,0x41,0x10, 0x48,0x81,0xc1,0x80,0x00,0x00,0x00,
        0x0f,0x29,0x41,0xa0, 0x0f,0x29,0x41,0xb0, 0x49,0xff,0xc9,
        0x0f,0x29,0x41,0xc0, 0x0f,0x29,0x41,0xd0, 0x0f,0x29,0x41,0xe0,
        0x0f,0x29,0x41,0xf0, 0x75,0xd5};

void sse_fill_loop() {
    const auto& code = kCachedSseFill;
    constexpr unsigned rounds = 9, steps_per_round = 11, vector_bytes = 16, round_bytes = 128;
    constexpr std::uint64_t destination = 0x2ce80, page_boundary = 0x2d000;
    PcMachine scalar(4U << 20), fast(4U << 20);
    for (auto* pc : {&scalar, &fast}) {
        boot(*pc, true);
        auto& cpu = pc->cpu;
        cpu.write8(destination - 1, 0x32);
        cpu.write8(destination + rounds * round_bytes, 0x25);
        cpu.memory.write_block(entry, code, sizeof(code));
        cpu.cr0 &= ~0xcU;
        cpu.cr4 |= 0x200U;
        cpu.eip = entry;
        cpu.halted = false;
        cpu.eflags = 2 | Cpu::CF | Cpu::DF | Cpu::OF;
        cpu.regs[Cpu::ECX] = destination;
        cpu.regs[Cpu::R9] = rounds;
        for (unsigned i = 0; i < vector_bytes; ++i) cpu.xmm[0][i] = std::uint8_t(0xd0 + i);
        for (unsigned i = 0; i < steps_per_round; ++i)
            require(cpu.step() == StepResult::Executed, "could not seed the SSE fill loop");
        require(cpu.eip == entry, "SSE fill backedge did not return to its entry");
    }

    auto& cpu = fast.cpu;
    const auto retired = cpu.instructions;
    const auto original_destination = cpu.regs[Cpu::ECX];
    const auto denied = [&] {
        const auto regs = cpu.regs;
        const auto flags = cpu.eflags;
        require(cpu.try_counter_loop_batch(3 * steps_per_round) == 0 && cpu.instructions == retired &&
                    cpu.regs == regs && cpu.eflags == flags && cpu.eip == entry && !cpu.fault &&
                    cpu.read64(original_destination) == 0,
                "rejected SSE fill changed guest state");
    };
    cpu.cr0 |= 8U;
    denied();
    cpu.cr0 &= ~8U;
    cpu.regs[Cpu::ECX] = original_destination + 1;
    denied();
    cpu.regs[Cpu::ECX] = page_boundary - round_bytes;
    denied();
    cpu.regs[Cpu::ECX] = entry;
    denied();
    require(cpu.read8(entry) == code[0], "SSE fill overwrote its executable bytes");
    cpu.regs[Cpu::ECX] = original_destination;
    cpu.write8(entry + 2, 0x09); // The edited first MOVAPS would use XMM1.
    denied();
    cpu.write8(entry + 2, code[2]);
    require(!cpu.try_counter_loop_batch(2 * steps_per_round - 1), "SSE fill exceeded its grant");

    fast.enable_boot_acceleration();
    constexpr unsigned device_limit = 3 * steps_per_round;
    unsigned ticks = 0;
    fast.device_step_hook = [&] { ++ticks; };
    fast.device_batch_limit_hook = [device_limit](unsigned budget) { return std::min(budget, device_limit); };
    fast.device_batch_advance_hook = [&](unsigned extra) {
        require(extra < device_limit, "SSE fill crossed a device observation boundary");
        ticks += extra;
    };
    constexpr unsigned steps = (rounds - 1) * steps_per_round;
    fast.run(steps);
    scalar.run(steps);
    const auto& expected = scalar.cpu;
    require(ticks == steps && cpu.eip == entry + sizeof(code) && cpu.regs == expected.regs &&
                cpu.eflags == expected.eflags && cpu.xmm == expected.xmm && !cpu.fault &&
                cpu.clock_cycles() == expected.clock_cycles() && cpu.last_decoded_bytes() == expected.last_decoded_bytes() &&
                fast.boot_acceleration().counter_loop_steps == steps,
            "SSE fill changed architectural state or device accounting");
    for (unsigned i = 0; i < rounds * round_bytes; ++i)
        require(cpu.read8(destination + i) == cpu.xmm[0][i % vector_bytes] &&
                    cpu.read8(destination + i) == expected.read8(destination + i), "SSE fill changed RAM");
    require(cpu.read8(destination - 1) == 0x32 && cpu.read8(destination + rounds * round_bytes) == 0x25,
            "SSE fill overwrote memory outside its destination");
    std::cout << "ETHER_SSE_FILL_LOOP_PASS architectural_state=1 page_boundary=1 edited_code=1 device_bound=1\n";
}

// A controlled streaming fill with the sampled stores and branch offsets.
constexpr std::uint8_t kMovntdqFill[]{
    0x66,0x0f,0xe7,0x01,      0x66,0x0f,0xe7,0x41,0x10, 0x66,0x0f,0xe7,0x41,0x20, 0x66,0x0f,0xe7,0x41,0x30,
    0x48,0x03,0xc8,           0x66,0x0f,0xe7,0x41,0xc0, 0x66,0x0f,0xe7,0x41,0xd0,
    0x48,0xff,0xca,           0x66,0x0f,0xe7,0x41,0xe0, 0x66,0x0f,0xe7,0x41,0xf0, 0x75,0xd1, 0xf4};
constexpr unsigned kMovntdqRoundSteps = 11;
constexpr std::uint64_t kMovntdqDestination = 0x2c000;

constexpr std::array<unsigned, 8> kStreamSources{2,1,0,3,4,5,6,7};

void prepare_movntdq(PcMachine& pc, unsigned rounds, std::uint64_t destination, bool mixed = false) {
    boot(pc, true);
    auto& cpu = pc.cpu;
    cpu.eip = entry;
    cpu.halted = false;
    cpu.cr4 |= 0x200U;
    cpu.cr0 &= ~0xcU;
    cpu.eflags = 2 | Cpu::CF | Cpu::DF;
    cpu.regs[Cpu::ECX] = destination;
    cpu.regs[Cpu::EAX] = 128;
    cpu.regs[Cpu::EDX] = rounds;
    for (unsigned i = 0; i < 16; ++i) cpu.xmm[0][i] = std::uint8_t(0xa0U + i);
    cpu.memory.write_block(entry, kMovntdqFill, sizeof(kMovntdqFill));
    if (mixed) {
        constexpr std::array<unsigned, 8> operands{3,7,12,17,25,30,38,43};
        for (unsigned n = 0; n < operands.size(); ++n) {
            const auto source = kStreamSources[n];
            for (unsigned i = 0; i < 16; ++i) cpu.xmm[source][i] = std::uint8_t(16 * source + i);
            cpu.write8(entry + operands[n], kMovntdqFill[operands[n]] | (source << 3));
        }
    }
}

void sse_zero_fill_pages() {
    constexpr auto page_size = cosmic_pc::MemoryFabric::kPageSize;
    constexpr std::uint64_t page = 0x2d000;
    constexpr unsigned round_bytes = 128, rounds = 3, steps = rounds * kMovntdqRoundSteps;
    constexpr std::uint8_t zero_xmm[]{0x66,0x0f,0xef,0xc0}; // PXOR XMM0,XMM0.
    std::array<std::uint8_t, page_size> prior;
    for (unsigned i = 0; i < prior.size(); ++i) prior[i] = std::uint8_t(1 + i % 251);

    for (const bool streaming : {false, true}) {
        for (const bool materialized : {false, true}) {
            PcMachine fast(4U << 20), scalar(4U << 20);
            const auto destination = materialized ? page + 2 * round_bytes : page;
            for (auto* machine : {&fast, &scalar}) {
                prepare_movntdq(*machine, rounds + 1, destination - round_bytes);
                auto& cpu = machine->cpu;
                if (!streaming) {
                    cpu.memory.write_block(entry, kCachedSseFill, sizeof(kCachedSseFill));
                    cpu.regs[Cpu::R9] = rounds + 1;
                }
                cpu.memory.write_block(entry - sizeof(zero_xmm), zero_xmm, sizeof(zero_xmm));
                cpu.eip = entry - sizeof(zero_xmm);
                require(cpu.step() == StepResult::Executed && cpu.eip == entry &&
                            std::all_of(cpu.xmm[0].begin(), cpu.xmm[0].end(), [](auto byte) { return byte == 0; }),
                        "the SSE zero fill did not execute PXOR");
                for (unsigned i = 0; i < kMovntdqRoundSteps; ++i)
                    require(cpu.step() == StepResult::Executed, "could not seed the SSE zero fill backedge");
                require(cpu.eip == entry && cpu.regs[Cpu::ECX] == destination &&
                            cpu.counter_loop_batch_candidate(),
                        "the SSE zero fill did not reach its measured destination");
                if (materialized) cpu.memory.write_block(page, prior.data(), prior.size());
                require(bool(cpu.memory.resident_ram_span(page, page_size)) == materialized,
                        "the SSE zero fill fixture started in the wrong RAM state");
            }

            auto& cpu = fast.cpu;
            const auto& expected = scalar.cpu;
            const auto prior_memory = cpu.memory.metrics();
            unsigned ticks = 0;
            fast.enable_boot_acceleration();
            fast.device_step_hook = [&] { ++ticks; };
            fast.device_batch_limit_hook = [steps](unsigned budget) { return std::min(budget, steps); };
            fast.device_batch_advance_hook = [&](unsigned extra) {
                require(extra < steps, "SSE zero fill crossed the device grant");
                ticks += extra;
            };
            fast.run(steps);
            scalar.run(steps);
            require(ticks == steps && fast.boot_acceleration().counter_loop_steps == steps &&
                        fast.boot_acceleration().streaming_sse_fill_loop_steps == (streaming ? steps : 0) &&
                        cpu.sse_zero_fill_bytes() == rounds * round_bytes && !expected.sse_zero_fill_bytes(),
                    "the SSE zero fill did not retire through its expected batch path");
            require(cpu.regs == expected.regs && cpu.eflags == expected.eflags && cpu.xmm == expected.xmm &&
                        cpu.eip == expected.eip && cpu.clock_cycles() == expected.clock_cycles() &&
                        cpu.last_decoded_bytes() == expected.last_decoded_bytes() && !cpu.fault && !expected.fault,
                    "the SSE zero fill changed x86 state");
            const auto following_memory = cpu.memory.metrics();
            require(following_memory.allocated_pages == prior_memory.allocated_pages &&
                        following_memory.page_state_vectors == prior_memory.page_state_vectors &&
                        bool(cpu.memory.resident_ram_span(page, page_size)) == materialized,
                    "the SSE zero fill changed page allocation or materialized an untouched page");

            std::array<std::uint8_t, page_size> actual, reference;
            cpu.memory.read_block(page, actual.data(), actual.size());
            expected.memory.read_block(page, reference.data(), reference.size());
            require(actual == reference, "the SSE zero fill disagreed with the scalar page");
            for (unsigned i = 0; i < actual.size(); ++i) {
                const auto address = page + i;
                const bool cleared = address >= destination && address < destination + rounds * round_bytes;
                require(actual[i] == (materialized && !cleared ? prior[i] : 0),
                        "the SSE zero fill left data in range or modified a neighbor");
            }
        }
    }
    std::cout << "ETHER_SSE_ZERO_FILL_PASS streaming=1 cached=1 untouched=1 neighbors=1 flags=1\n";
}

void streaming_sse_fill_rounds() {
    constexpr unsigned rounds = 9, round_bytes = 128;
    constexpr std::uint64_t destination = 0x2ce80, page_boundary = 0x2d000;
    constexpr unsigned body_bytes = sizeof(kMovntdqFill) - 1;
    constexpr std::uint8_t tail[]{0x0f,0xae,0xf8,0xf4}; // SFENCE; HLT
    const auto seed = [&](PcMachine& pc, unsigned count, std::uint64_t start) {
        prepare_movntdq(pc, count, start);
        auto& cpu = pc.cpu;
        cpu.memory.write_block(entry + body_bytes, tail, sizeof(tail));
        for (unsigned i = 0; i < kMovntdqRoundSteps; ++i)
            require(cpu.step() == StepResult::Executed, "could not seed streaming SSE fill loop");
        require(cpu.eip == entry && cpu.streaming_sse_fill_loop_batch_candidate(),
                "streaming SSE fill did not observe exact backedge");
    };

    PcMachine scalar(4U << 20), fast(4U << 20);
    for (auto* machine : {&scalar, &fast}) {
        seed(*machine, rounds, destination);
        machine->cpu.write8(destination - 1, 0x23);
        machine->cpu.write8(destination + rounds * round_bytes, 0x86);
    }
    auto& cpu = fast.cpu;
    const auto& expected = scalar.cpu;
    const auto retired = cpu.instructions;
    const auto cursor = cpu.regs[Cpu::ECX];
    const auto denied = [&] {
        const auto registers = cpu.regs;
        const auto flags = cpu.eflags;
        const auto xmm = cpu.xmm;
        require(cpu.try_counter_loop_batch(4 * kMovntdqRoundSteps) == 0 &&
                    cpu.instructions == retired && cpu.regs == registers && cpu.xmm == xmm &&
                    cpu.eflags == flags && cpu.eip == entry && !cpu.fault && cpu.read64(cursor) == 0,
                "rejected streaming SSE fill changed CPU or memory");
    };
    cpu.regs[Cpu::EAX] = 0x100000080ULL;
    denied();
    cpu.regs[Cpu::EAX] = round_bytes;
    cpu.cr0 |= 8U;
    denied();
    cpu.cr0 &= ~8U;
    cpu.regs[Cpu::ECX] = cursor + 1;
    denied();
    cpu.regs[Cpu::ECX] = page_boundary - round_bytes;
    denied();
    cpu.regs[Cpu::ECX] = entry;
    denied();
    require(cpu.read8(entry) == kMovntdqFill[0], "streaming SSE fill overwrote its code");
    cpu.regs[Cpu::ECX] = cursor;
    cpu.write8(entry + 3, 0x09); // First MOVNTDQ now uses XMM1.
    denied();
    cpu.write8(entry + 3, kMovntdqFill[3]);

    unsigned ticks = 0, device_limit = 4 * kMovntdqRoundSteps;
    fast.enable_boot_acceleration();
    fast.device_step_hook = [&] { ++ticks; };
    fast.device_batch_limit_hook = [&](unsigned grant) { return std::min(grant, device_limit); };
    fast.device_batch_advance_hook = [&](unsigned extra) {
        require(extra < device_limit, "streaming SSE fill crossed its device boundary");
        ticks += extra;
    };
    const auto compare = [&] {
        require(cpu.regs == expected.regs && cpu.eflags == expected.eflags && cpu.xmm == expected.xmm &&
                    cpu.eip == expected.eip && cpu.clock_cycles() == expected.clock_cycles() &&
                    cpu.last_decoded_bytes() == expected.last_decoded_bytes() && !cpu.fault,
                "streaming SSE fill changed x86 state");
        std::array<std::uint8_t, rounds * round_bytes + 2> actual, reference;
        cpu.memory.read_block(destination - 1, actual.data(), actual.size());
        expected.memory.read_block(destination - 1, reference.data(), reference.size());
        require(actual == reference && actual.front() == 0x23 && actual.back() == 0x86,
                "streaming SSE fill changed RAM or outside guard bytes");
    };
    const auto run = [&](unsigned steps) {
        auto before = cpu.instructions;
        const auto previous_ticks = ticks;
        fast.run(steps);
        scalar.run(steps);
        require(cpu.instructions - before == steps && ticks - previous_ticks == steps,
                "streaming SSE fill changed device ticks or retirement");
        compare();
    };
    run(2 * kMovntdqRoundSteps);
    require(cpu.regs[Cpu::ECX] == page_boundary && fast.boot_acceleration().streaming_sse_fill_loop_steps == 22,
            "streaming SSE fill missed the first page grant");
    device_limit = 19;
    run(19);
    require(cpu.eip != entry && fast.boot_acceleration().streaming_sse_fill_loop_steps == 22,
            "streaming SSE fill did not fall back inside a device grant");
    run(3);
    require(cpu.eip == entry, "streaming SSE fill did not resume at its backedge");
    device_limit = 4 * kMovntdqRoundSteps;
    run(4 * kMovntdqRoundSteps);
    require(cpu.eip == entry + body_bytes && cpu.regs[Cpu::EDX] == 0 &&
                fast.boot_acceleration().streaming_sse_fill_loop_steps == 66,
            "streaming SSE fill skipped its SFENCE or retired the wrong number of rounds");
    run(1); // SFENCE is the ordinary next instruction, after the loop stores.
    require(cpu.eip == entry + body_bytes + 3 && cpu.last_decoded_bytes()[2] == 0xf8,
            "streaming SSE fill changed the final SFENCE");

    PcMachine boundary(4U << 20);
    constexpr std::uint64_t end = 0x200000;
    seed(boundary, 6, end - 160);
    auto& edge = boundary.cpu;
    edge.write64(0x3008, 0); // Next writable page is absent.
    const auto edge_retired = edge.instructions;
    require(edge.try_counter_loop_batch(44) == 0 && edge.instructions == edge_retired &&
                edge.eip == entry && edge.read64(end - 32) == 0,
            "streaming SSE fill bypassed a page fault or partially wrote a rejected round");
    require(edge.step() == StepResult::Executed && edge.step() == StepResult::Executed &&
                edge.read64(end - 32) != 0 && edge.read64(end - 16) != 0,
            "scalar streaming fill lost the valid stores before the page fault");
    require(edge.step() == StepResult::Faulted && edge.fault && edge.fault->eip == entry + 9 &&
                edge.cr2 == end && edge.read64(end) == 0,
            "streaming SSE fill faulted at the wrong instruction or address");
    std::cout << "ETHER_STREAMING_SSE_FILL_PASS full_batch_steps=66 mid_grant=1 partial_page_fault=1 sfence=1\n";
}

void streaming_sse_triplet_rounds() {
    constexpr unsigned rounds = 8, round_steps = 6, round_bytes = 48;
    constexpr std::uint64_t page_boundary = 0x2d000;
    constexpr auto destination = page_boundary - 3 * round_bytes;
    constexpr std::uint8_t guest[]{
        0x66,0x0f,0xe7,0x11, 0x66,0x0f,0xe7,0x49,0x10, 0x66,0x0f,0xe7,0x41,0x20,
        0x48,0x83,0xc1,0x30, 0x49,0x3b,0xc8, 0x75,0xe9, 0x0f,0xae,0xf8,0xf4};
    constexpr unsigned body_bytes = sizeof(guest) - 4;
    const auto seed = [&](PcMachine& pc, unsigned count, std::uint64_t start) {
        prepare_movntdq(pc, count, start);
        auto& cpu = pc.cpu;
        cpu.memory.write_block(entry, guest, sizeof(guest));
        cpu.regs[Cpu::R8] = start + count * round_bytes;
        for (unsigned lane = 0; lane < 3; ++lane)
            for (unsigned byte = 0; byte < 16; ++byte)
                cpu.xmm[lane][byte] = std::uint8_t(0x20 + lane * 0x30 + byte);
        for (unsigned i = 0; i < round_steps; ++i)
            require(cpu.step() == StepResult::Executed, "could not seed the streaming SSE triplet");
        require(cpu.eip == entry && cpu.streaming_sse_fill_loop_batch_candidate(),
                "streaming SSE triplet did not recognize the taken backedge");
    };

    PcMachine fast(4U << 20), scalar(4U << 20);
    for (auto* pc : {&fast, &scalar}) {
        seed(*pc, rounds, destination);
        pc->cpu.write8(destination - 1, 0x23);
        pc->cpu.write8(destination + rounds * round_bytes, 0x86);
    }
    auto& cpu = fast.cpu;
    const auto& expected = scalar.cpu;
    const auto retired_before = cpu.instructions;
    const auto untouched = cpu.regs[Cpu::ECX];
    const auto rejected = [&](unsigned budget) {
        const auto registers = cpu.regs;
        const auto flags = cpu.eflags;
        require(!cpu.try_counter_loop_batch(budget) && cpu.instructions == retired_before &&
                    cpu.regs == registers && cpu.eflags == flags && cpu.eip == entry && !cpu.fault &&
                    cpu.read64(untouched) == 0,
                "a rejected streaming SSE triplet changed architectural state or memory");
    };
    rejected(2 * round_steps - 1);
    ++cpu.regs[Cpu::R8];
    rejected(2 * round_steps);
    --cpu.regs[Cpu::R8];
    cpu.cr0 |= 8U;
    rejected(2 * round_steps);
    cpu.cr0 &= ~8U;
    cpu.write8(entry + 3, 0x09); // Change XMM2 to XMM1 without changing the backedge.
    rejected(2 * round_steps);
    cpu.write8(entry + 3, guest[3]);

    unsigned ticks = 0, device_grant = 4 * round_steps;
    fast.enable_boot_acceleration();
    fast.device_step_hook = [&] { ++ticks; };
    fast.device_batch_limit_hook = [&](unsigned grant) { return std::min(grant, device_grant); };
    fast.device_batch_advance_hook = [&](unsigned extra) {
        require(extra < device_grant, "streaming SSE triplet crossed its device grant");
        ticks += extra;
    };
    const auto run = [&](unsigned steps) {
        const auto prior_steps = cpu.instructions;
        const auto prior_ticks = ticks;
        fast.run(steps);
        scalar.run(steps);
        require(cpu.instructions - prior_steps == steps && ticks - prior_ticks == steps &&
                    cpu.clock_cycles() == expected.clock_cycles() && cpu.eip == expected.eip &&
                    cpu.regs == expected.regs && cpu.eflags == expected.eflags && cpu.xmm == expected.xmm &&
                    cpu.last_decoded_bytes() == expected.last_decoded_bytes() && !cpu.fault && !expected.fault,
                "streaming SSE triplet changed x86 state or device accounting");
        std::array<std::uint8_t, rounds * round_bytes + 2> actual, oracle;
        cpu.memory.read_block(destination - 1, actual.data(), actual.size());
        expected.memory.read_block(destination - 1, oracle.data(), oracle.size());
        require(actual == oracle && actual.front() == 0x23 && actual.back() == 0x86,
                "streaming SSE triplet changed destination or guard bytes");
    };
    run(2 * round_steps);
    require(cpu.regs[Cpu::ECX] == page_boundary &&
                fast.boot_acceleration().streaming_sse_fill_loop_steps == 2 * round_steps,
            "streaming SSE triplet missed two complete rounds at the end of a page");
    device_grant = 7;
    run(7);
    require(cpu.eip == entry + 4 && fast.boot_acceleration().streaming_sse_fill_loop_steps == 2 * round_steps,
            "streaming SSE triplet failed to fall back inside a short device grant");
    run(5);
    require(cpu.eip == entry, "streaming SSE triplet did not resume on the next backedge");
    device_grant = 4 * round_steps;
    run(3 * round_steps);
    require(cpu.eip == entry + body_bytes && cpu.regs[Cpu::ECX] == cpu.regs[Cpu::R8] &&
                cpu.flag(Cpu::ZF) && fast.boot_acceleration().streaming_sse_fill_loop_steps == 5 * round_steps,
            "streaming SSE triplet consumed the tail or retired the wrong rounds");
    for (unsigned i = 0; i < rounds * round_bytes; ++i)
        require(cpu.read8(destination + i) == cpu.xmm[2 - (i % round_bytes) / 16][i % 16],
                "streaming SSE triplet wrote the wrong XMM lane");
    run(1); // SFENCE remains the next ordinary instruction.
    require(cpu.eip == entry + body_bytes + 3, "streaming SSE triplet skipped SFENCE");

    PcMachine boundary(4U << 20);
    constexpr std::uint64_t end = 0x200000;
    seed(boundary, 6, end - round_bytes - 32);
    auto& edge = boundary.cpu;
    edge.write64(0x3008, 0); // Third store of the next triplet encounters an absent mapping.
    const auto edge_steps = edge.instructions;
    require(!edge.try_counter_loop_batch(4 * round_steps) && edge.instructions == edge_steps &&
                edge.eip == entry && edge.read64(end - 32) == 0,
            "streaming SSE triplet committed a rejected cross-page round");
    require(edge.step() == StepResult::Executed && edge.step() == StepResult::Executed &&
                edge.read64(end - 32) != 0 && edge.read64(end - 16) != 0 &&
                edge.step() == StepResult::Faulted && edge.fault && edge.fault->eip == entry + 9 &&
                edge.cr2 == end && edge.read64(end) == 0,
            "streaming SSE triplet did not preserve the precise cross-page fault and prior stores");
    std::cout << "ETHER_STREAMING_SSE_TRIPLET_PASS full_batch_steps=30 lanes=3 device_bound=1 data_fault=1\n";
}

void movntdq_store_batches() {
    PcMachine scalar(4U << 20), fast(4U << 20);
    for (auto* pc : {&scalar, &fast}) {
        prepare_movntdq(*pc, 8, kMovntdqDestination);
        for (unsigned i = 0; i < kMovntdqRoundSteps; ++i)
            require(pc->cpu.step() == StepResult::Executed, "could not seed MOVNTDQ loop");
        require(pc->cpu.eip == entry && pc->cpu.movntdq_store_batch_candidate(),
                "MOVNTDQ entry missed the observed backedge");
    }
    fast.enable_boot_acceleration();
    unsigned ticks = 0;
    fast.device_step_hook = [&] { ++ticks; };
    fast.device_batch_limit_hook = [](unsigned budget) { return std::min(budget, 3U); };
    fast.device_batch_advance_hook = [&](unsigned extra) {
        require(extra <= 2, "MOVNTDQ crossed a device grant");
        ticks += extra;
    };
    constexpr unsigned steps = 5 * kMovntdqRoundSteps;
    fast.run(steps);
    scalar.run(steps);
    const auto& expected = scalar.cpu;
    const auto& actual = fast.cpu;
    require(ticks == steps && fast.boot_acceleration().movntdq_store_batches == 15 &&
            fast.boot_acceleration().movntdq_store_steps == 35 &&
            actual.regs == expected.regs && actual.eflags == expected.eflags && actual.eip == expected.eip &&
            actual.instructions == expected.instructions && actual.xmm == expected.xmm &&
            actual.last_decoded_bytes() == expected.last_decoded_bytes(),
            "MOVNTDQ changed x86 state or device accounting");
    std::array<std::uint8_t, 6U * 128U> left, right;
    actual.memory.read_block(kMovntdqDestination, left.data(), left.size());
    expected.memory.read_block(kMovntdqDestination, right.data(), right.size());
    require(left == right && left[17] == 0xa1, "MOVNTDQ changed guest RAM bytes");
    // Ensure the unretired data remains untouched on all three guards.
    auto& cpu = fast.cpu;
    const auto retired = cpu.instructions, destination = cpu.regs[Cpu::ECX];
    cpu.cr0 |= 8U;
    require(!cpu.try_movntdq_store_batch(4), "MOVNTDQ bypassed CR0.TS");
    cpu.cr0 &= ~8U;
    cpu.regs[Cpu::ECX] = destination + 1;
    require(!cpu.try_movntdq_store_batch(4), "MOVNTDQ bypassed 16-byte alignment");
    cpu.regs[Cpu::ECX] = entry;
    require(!cpu.try_movntdq_store_batch(4) && cpu.read32(entry) == 0x01e70f66U,
            "MOVNTDQ speculatively changed its own instruction bytes");
    require(cpu.instructions == retired && cpu.eip == entry && cpu.read64(destination) == 0,
            "rejected MOVNTDQ changed RAM or retired instructions");

    PcMachine boundary(4U << 20);
    constexpr std::uint64_t end_of_mapping = 0x200000;
    prepare_movntdq(boundary, 1, end_of_mapping - 32);
    auto& edge = boundary.cpu;
    edge.write64(0x3008, 0); // Missing leaf immediately after the two safe stores.
    require(edge.step() == StepResult::Executed && edge.eip == entry + 4,
            "could not seed the boundary store");
    require(!edge.try_movntdq_store_batch(4) && edge.read64(end_of_mapping - 16) == 0 &&
            edge.read64(end_of_mapping) == 0, "MOVNTDQ wrote across an unchecked mapping boundary");
    require(edge.step() == StepResult::Executed && edge.eip == entry + 9 &&
            edge.read64(end_of_mapping - 16) != 0, "scalar fallback lost the last valid store");
    require(edge.step() == StepResult::Faulted && edge.fault && edge.fault->eip == entry + 9 &&
            edge.cr2 == end_of_mapping && edge.read64(end_of_mapping) == 0,
            "MOVNTDQ did not fault at the first inaccessible address");
    std::cout << "ETHER_MOVNTDQ_STORE_PASS batches=15 stores=35 device_grant=3 page_fault_index=2\n";
}

void mixed_movntdq_registers() {
    // The first three stores match the sampled Windows sequence. Also cover XMM8/15 and R9.
    constexpr std::uint8_t code[]{
        0x66,0x0f,0xe7,0x41,0xf0,
        0x66,0x0f,0xe7,0x11,      0x66,0x0f,0xe7,0x49,0x10,
        0x66,0x0f,0xe7,0x41,0x20, 0x66,0x44,0x0f,0xe7,0x79,0x30,
        0x66,0x44,0x0f,0xe7,0x41,0x40, 0x66,0x0f,0xe7,0x59,0x50,
        0x66,0x0f,0xe7,0x61,0x60, 0x66,0x0f,0xe7,0x69,0x70,
        0x66,0x41,0x0f,0xe7,0x31, 0x66,0x45,0x0f,0xe7,0x79,0x10};
    PcMachine fast(4U << 20), scalar(4U << 20);
    for (auto* pc : {&fast, &scalar}) {
        prepare_movntdq(*pc, 1, kMovntdqDestination);
        auto& cpu = pc->cpu;
        cpu.regs[Cpu::R9] = kMovntdqDestination + 128;
        for (unsigned reg = 0; reg < cpu.xmm.size(); ++reg)
            for (unsigned b = 0; b < 16; ++b) cpu.xmm[reg][b] = std::uint8_t(reg * 16 + b);
        cpu.memory.write_block(entry, code, sizeof(code));
        require(cpu.step() == StepResult::Executed, "mixed MOVNTDQ seed failed");
    }
    require(fast.cpu.try_movntdq_store_batch(9) == 8 && fast.cpu.try_movntdq_store_batch(8) == 2,
            "streaming stores lost an independent XMM lane or crossed a base change");
    for (unsigned i = 0; i < 10; ++i)
        require(scalar.cpu.step() == StepResult::Executed, "mixed MOVNTDQ scalar reference failed");
    std::array<std::uint8_t, 160> actual, expected;
    fast.cpu.memory.read_block(kMovntdqDestination, actual.data(), actual.size());
    scalar.cpu.memory.read_block(kMovntdqDestination, expected.data(), expected.size());
    require(actual == expected && actual[0] == 32 && actual[16] == 16 && actual[32] == 0 &&
            actual[48] == 240 && actual[144] == 240 && fast.cpu.instructions == scalar.cpu.instructions &&
            fast.cpu.eip == scalar.cpu.eip && fast.cpu.eflags == scalar.cpu.eflags &&
            fast.cpu.regs == scalar.cpu.regs && fast.cpu.xmm == scalar.cpu.xmm &&
            fast.cpu.last_decoded_bytes() == scalar.cpu.last_decoded_bytes(), "mixed MOVNTDQ diverged");
    std::cout << "ETHER_MOVNTDQ_MIXED_PASS contiguous_stores=8 xmm15=1 page_safe=1\n";
}

void wide_movntdq_batch() {
    constexpr unsigned stores = 35, encoding_bytes = 8, vector_bytes = 16;
    constexpr std::uint64_t mapping_end = 0x200000;
    std::array<std::uint8_t, stores * encoding_bytes + 1> code{};
    for (unsigned i = 0; i < stores; ++i) {
        const auto displacement = i * vector_bytes;
        const std::array<std::uint8_t, encoding_bytes> instruction{
            0x66, 0x0f, 0xe7, std::uint8_t(0x81 | ((i % 8) << 3)),
            std::uint8_t(displacement), std::uint8_t(displacement >> 8), 0, 0};
        std::copy(instruction.begin(), instruction.end(), code.begin() + i * encoding_bytes);
    }
    code.back() = 0xf4;
    for (const auto destination : {kMovntdqDestination, mapping_end - 17 * vector_bytes}) {
        PcMachine fast(4U << 20), scalar(4U << 20);
        const bool fault_after_batch = destination != kMovntdqDestination;
        for (auto* pc : {&fast, &scalar}) {
            prepare_movntdq(*pc, 1, destination);
            auto& cpu = pc->cpu;
            for (unsigned reg = 0; reg < 8; ++reg)
                for (unsigned b = 0; b < vector_bytes; ++b)
                    cpu.xmm[reg][b] = std::uint8_t(reg * vector_bytes + b);
            cpu.memory.write_block(entry, code.data(), code.size());
            if (fault_after_batch) cpu.write64(0x3008, 0);
            require(cpu.step() == StepResult::Executed, "wide MOVNTDQ seed failed");
        }
        const unsigned expected_stores = fault_after_batch ? 16 : 32;
        require(fast.cpu.try_movntdq_store_batch(stores - 1) == expected_stores,
                "wide MOVNTDQ batch must stop at the store limit or destination page");
        for (unsigned n = 0; n < expected_stores; ++n)
            require(scalar.cpu.step() == StepResult::Executed, "wide MOVNTDQ scalar reference failed");
        const auto copied_bytes = (expected_stores + 1) * vector_bytes;
        std::array<std::uint8_t, stores * vector_bytes> actual{}, expected{};
        fast.cpu.memory.read_block(destination, actual.data(), copied_bytes);
        scalar.cpu.memory.read_block(destination, expected.data(), copied_bytes);
        require(actual == expected && fast.cpu.regs == scalar.cpu.regs && fast.cpu.xmm == scalar.cpu.xmm &&
                fast.cpu.eip == scalar.cpu.eip && fast.cpu.instructions == scalar.cpu.instructions &&
                fast.cpu.eflags == scalar.cpu.eflags &&
                fast.cpu.last_decoded_bytes() == scalar.cpu.last_decoded_bytes(), "wide MOVNTDQ state diverged");
        if (fault_after_batch)
            require(!fast.cpu.try_movntdq_store_batch(stores) && fast.cpu.step() == StepResult::Faulted &&
                    fast.cpu.cr2 == mapping_end && fast.cpu.fault && fast.cpu.fault->eip == entry + 17 * encoding_bytes,
                    "wide MOVNTDQ did not preserve the exact first fault beyond the committed page");
    }
    std::cout << "ETHER_MOVNTDQ_WIDE_PASS max_stores=32 checked_page=1\n";
}

constexpr std::array<unsigned, 8> kDisp32Sources{2,1,8,15,4,9,6,7};

std::uint64_t prepare_disp32_movntdq(PcMachine& pc, unsigned rounds, std::uint64_t destination,
                                     std::int32_t displacement, std::uint64_t ip = entry) {
    prepare_movntdq(pc, rounds, destination - std::uint64_t(displacement));
    std::vector<std::uint8_t> program;
    for (unsigned i = 0; i < kDisp32Sources.size(); ++i) {
        const unsigned reg = kDisp32Sources[i];
        for (unsigned j = 0; j < 16; ++j) pc.cpu.xmm[reg][j] = std::uint8_t(reg * 16 + j);
        program.push_back(0x66);
        if (reg >= 8) program.push_back(0x44);
        program.insert(program.end(), {0x0f, 0xe7, std::uint8_t(0x81 | ((reg & 7) << 3))});
        const auto offset = std::uint32_t(displacement) + i * 16;
        for (unsigned shift = 0; shift < 32; shift += 8)
            program.push_back(std::uint8_t(offset >> shift));
    }
    program.insert(program.end(), {0x48,0x03,0xc8, 0x48,0xff,0xca, 0x75});
    program.push_back(std::uint8_t(-(int(program.size()) + 1)));
    program.push_back(0xf4);
    pc.cpu.memory.write_block(ip, program.data(), program.size());
    pc.cpu.eip = ip;
    return ip + program.size() - 1;
}

void disp32_movntdq_registers() {
    for (const std::int32_t displacement : {-0x12340, 0x13450}) {
        PcMachine fast(4U << 20), scalar(4U << 20);
        for (auto* pc : {&fast, &scalar}) {
            prepare_disp32_movntdq(*pc, 1, kMovntdqDestination, displacement);
            require(pc->cpu.step() == StepResult::Executed, "disp32 seed failed");
        }
        require(!fast.cpu.try_movntdq_store_batch(1), "disp32 exceeded its single-step grant");
        for (unsigned grant : {2U, 5U}) {
            require(fast.cpu.try_movntdq_store_batch(grant) == grant, "disp32 batch missed its grant");
            for (unsigned n = 0; n < grant; ++n)
                require(scalar.cpu.step() == StepResult::Executed, "disp32 scalar reference failed");
            std::array<std::uint8_t, 160> actual, expected;
            fast.cpu.memory.read_block(kMovntdqDestination - 16, actual.data(), actual.size());
            scalar.cpu.memory.read_block(kMovntdqDestination - 16, expected.data(), expected.size());
            require(actual == expected && fast.cpu.regs == scalar.cpu.regs && fast.cpu.xmm == scalar.cpu.xmm &&
                    fast.cpu.eip == scalar.cpu.eip && fast.cpu.instructions == scalar.cpu.instructions &&
                    fast.cpu.eflags == scalar.cpu.eflags &&
                    fast.cpu.last_decoded_bytes() == scalar.cpu.last_decoded_bytes(), "disp32 state diverged");
        }
    }

    PcMachine code_edge(4U << 20);
    prepare_disp32_movntdq(code_edge, 1, kMovntdqDestination, -0x800, entry + 4096 - 22);
    require(code_edge.cpu.step() == StepResult::Executed && !code_edge.cpu.try_movntdq_store_batch(7) &&
            code_edge.cpu.read64(kMovntdqDestination + 16) == 0,
            "disp32 consumed an incomplete displacement at a code-page boundary");

    PcMachine data_edge(4U << 20);
    constexpr std::uint64_t end = 0x200000;
    prepare_disp32_movntdq(data_edge, 1, end - 32, 0x1000);
    auto& cpu = data_edge.cpu;
    cpu.write64(0x3008, 0);
    require(cpu.step() == StepResult::Executed && !cpu.try_movntdq_store_batch(7) &&
            cpu.read64(end - 16) == 0 && cpu.step() == StepResult::Executed &&
            cpu.step() == StepResult::Faulted && cpu.fault && cpu.fault->eip == entry + 16 && cpu.cr2 == end,
            "disp32 lost the scalar fault boundary");
    std::cout << "ETHER_MOVNTDQ_DISP32_PASS signed=1 grants=2,5 code_page=1 data_fault=1\n";
}

enum class StorePattern { Repeated, Mixed, Disp32 };

void measure_movntdq(StorePattern pattern = StorePattern::Repeated) {
    constexpr unsigned rounds = 4000, steps = rounds * kMovntdqRoundSteps;
    std::array<long long, 7> scalar, batched;
    std::uint64_t grouped_stores = 0;
    for (unsigned rep = 0; rep < scalar.size(); ++rep) {
        for (bool accelerated : {bool(rep % 2), !bool(rep % 2)}) {
            PcMachine pc(4U << 20);
            constexpr std::int32_t displacement = 0x300;
            auto end_ip = entry + sizeof(kMovntdqFill) - 1;
            auto final_base = kMovntdqDestination + rounds * 128;
            if (pattern == StorePattern::Disp32) {
                end_ip = prepare_disp32_movntdq(pc, rounds, kMovntdqDestination, displacement);
                final_base -= displacement;
            } else {
                prepare_movntdq(pc, rounds, kMovntdqDestination, pattern == StorePattern::Mixed);
            }
            pc.enable_boot_acceleration(accelerated);
            const auto retired = pc.cpu.instructions;
            const auto start = Clock::now();
            pc.run(steps);
            const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
            require(!pc.cpu.fault && pc.cpu.instructions - retired == steps &&
                    pc.cpu.regs[Cpu::EDX] == 0 && pc.cpu.regs[Cpu::ECX] == final_base && pc.cpu.eip == end_ip,
                    "MOVNTDQ benchmark changed execution");
            require(pattern != StorePattern::Repeated || (pc.boot_acceleration().movntdq_store_steps != 0) == accelerated,
                    "MOVNTDQ benchmark missed its execution path");
            if (accelerated) grouped_stores = pc.boot_acceleration().movntdq_store_steps;
            if (pattern != StorePattern::Repeated) {
                const auto& sources = pattern == StorePattern::Disp32 ? kDisp32Sources : kStreamSources;
                std::array<std::uint8_t, 128> row;
                for (unsigned n = 0; n < rounds; ++n) {
                    pc.cpu.memory.read_block(kMovntdqDestination + n * row.size(), row.data(), row.size());
                    for (unsigned i = 0; i < row.size(); ++i)
                        require(row[i] == 16 * sources[i / 16] + i % 16, "mixed stream data changed");
                }
            }
            (accelerated ? batched : scalar)[rep] = elapsed;
        }
    }
    std::sort(scalar.begin(), scalar.end());
    std::sort(batched.begin(), batched.end());
    const auto label = pattern == StorePattern::Disp32 ? "ETHER_MOVNTDQ_DISP32_BENCH" :
                       pattern == StorePattern::Mixed ? "ETHER_MOVNTDQ_MIXED_BENCH" : "ETHER_MOVNTDQ_BENCH";
    std::cout << label
              << " steps=" << steps << " scalar_median_ns=" << scalar[3]
              << " batched_median_ns=" << batched[3];
    if (pattern == StorePattern::Disp32) std::cout << " grouped_stores=" << grouped_stores;
    std::cout << '\n';
}

constexpr std::uint8_t kMultiplyAdd[]{
        0x4b,0x8b,0x04,0x08, 0x49,0xf7,0xe2, 0x49,0x03,0xc3, 0x48,0x83,0xd2,0,
        0x4a,0x01,0x04,0x09, 0x48,0x83,0xd2,0, 0x49,0x83,0xc1,8, 0x4c,0x8b,0xda, 0x75,0xe1};
constexpr std::uint8_t kMultiplySubtract[]{
        0x4b,0x8b,0x04,0xc8, 0x4c,0x8b,0xda, 0x49,0xf7,0xe2, 0x4a,0x29,0x04,0xc9,
        0x48,0x83,0xd2,0, 0x4e,0x29,0x1c,0xc9, 0x48,0x83,0xd2,0, 0x49,0xff,0xc1, 0x75,0xe1};
static_assert(sizeof(kMultiplyAdd) == sizeof(kMultiplySubtract));
constexpr unsigned kWideStepsPerRound = 9;

void prepare_wide_multiply(PcMachine& pc, bool subtract, std::uint64_t source,
                           std::uint64_t target, unsigned rounds) {
    constexpr std::array<std::uint64_t, 5> input{0, 1, UINT64_MAX, UINT64_MAX - 1, 0x0123456789abcdefULL};
    boot(pc, true);
    auto& cpu = pc.cpu;
    cpu.memory.write_block(entry, subtract ? kMultiplySubtract : kMultiplyAdd, sizeof(kMultiplyAdd));
    for (unsigned i = 0; i < rounds; ++i) {
        cpu.write64(source + 8 * i, input[i % input.size()]);
        if (target != source) cpu.write64(target + 8 * i, input[(i + 2) % input.size()]);
    }
    cpu.eip = entry;
    cpu.halted = false;
    cpu.eflags = 2 | Cpu::CF | Cpu::DF | Cpu::OF;
    cpu.regs[Cpu::ECX] = target + 8 * rounds;
    cpu.regs[Cpu::R8] = source + 8 * rounds;
    cpu.regs[Cpu::R9] = 0ULL - rounds * (subtract ? 1 : 8);
    cpu.regs[Cpu::R10] = UINT64_MAX;
    cpu.regs[Cpu::EDX] = UINT64_MAX;
    cpu.regs[Cpu::R11] = UINT64_MAX;
}

void wide_multiply_loops() {
    constexpr std::uint64_t source = 0x2a005;
    constexpr unsigned rounds = 38, steps_per_round = kWideStepsPerRound;
    for (bool subtract : {false, true}) {
        for (const auto target : {source, std::uint64_t{0x2c007}}) {
            PcMachine scalar(4U << 20), fast(4U << 20);
            const auto prepare = [&](PcMachine& pc) { prepare_wide_multiply(pc, subtract, source, target, rounds); };
            prepare(scalar);
            prepare(fast);
            for (auto* pc : {&scalar, &fast}) {
                for (unsigned i = 0; i < steps_per_round; ++i)
                    require(pc->cpu.step() == StepResult::Executed, "wide multiply seed did not execute");
                require(pc->cpu.wide_multiply_loop_batch_candidate(), "wide multiply branch did not form a candidate");
            }
            auto& cpu = fast.cpu;
            const auto before = cpu.regs;
            const auto initial_steps = cpu.instructions;
            const auto flags = cpu.eflags;
            const auto must_fall_back = [&](unsigned limit) {
                const auto registers = cpu.regs;
                require(cpu.try_counter_loop_batch(limit) == 0 && cpu.instructions == initial_steps &&
                        cpu.regs == registers && cpu.eip == entry && cpu.eflags == flags && !cpu.fault,
                        "wide multiply guard changed guest state");
            };
            must_fall_back(2 * steps_per_round - 1);
            cpu.write8(entry + 4, 0x90);
            must_fall_back(200);
            cpu.write8(entry + 4, (subtract ? kMultiplySubtract : kMultiplyAdd)[4]);
            cpu.regs[Cpu::ECX] = cpu.regs[Cpu::R8] + 8;
            must_fall_back(200); // A store would overwrite the next input word.
            cpu.regs[Cpu::ECX] = 0x30000 + 8 * (rounds - 2);
            must_fall_back(200); // Exactly one word before the page boundary.
            cpu.regs = before;
            require(cpu.read64(target + 8) == scalar.cpu.read64(target + 8), "wide multiply fallback wrote RAM");

            const auto compare = [&] {
                const auto& reference = scalar.cpu;
                require(cpu.regs == reference.regs && cpu.eip == reference.eip && cpu.eflags == reference.eflags &&
                        cpu.instructions == reference.instructions && cpu.clock_cycles() == reference.clock_cycles() &&
                        cpu.last_decoded_bytes() == reference.last_decoded_bytes(), "wide multiply changed x86 state");
                for (unsigned i = 0; i < rounds; ++i)
                    require(cpu.read64(target + i * 8) == scalar.cpu.read64(target + i * 8), "wide multiply changed RAM");
            };
            constexpr unsigned partial = 8 * steps_per_round, tail = (rounds - 9) * steps_per_round;
            unsigned ticks = 0;
            fast.enable_boot_acceleration();
            fast.device_step_hook = [&] { ++ticks; };
            fast.device_batch_limit_hook = [partial](unsigned budget) { return std::min(budget, partial); };
            fast.device_batch_advance_hook = [&](unsigned extra) { ticks += extra; };
            fast.run(partial);
            scalar.run(partial);
            compare();
            require(ticks == partial && fast.boot_acceleration().wide_multiply_loop_steps == partial,
                    "wide multiply did not take the production path within the device budget");
            require(cpu.try_counter_loop_batch(tail + steps_per_round - 1) == tail,
                    "wide multiply did not terminate on the original branch");
            scalar.run(tail);
            compare();

            fast.device_step_hook = {};
            fast.device_batch_limit_hook = {};
            fast.device_batch_advance_hook = {};
            fast.enable_boot_acceleration(false);
            prepare(scalar);
            prepare(fast);
            fast.enable_boot_acceleration();
            cpu.enable_ether_arm64_blocks();
            const auto previous = fast.boot_acceleration().wide_multiply_loop_steps;
            fast.run(rounds * steps_per_round);
            scalar.run(rounds * steps_per_round);
            compare();
            require(fast.boot_acceleration().wide_multiply_loop_steps > previous,
                    "the native chain hid the wide multiply loop backedge");
        }
    }
    std::cout << "ETHER_WIDE_MULTIPLY_LOOP_PASS variants=2 paging=1 aliases=1 carries=1 device_bound=1\n";
}

void measure_wide_multiply() {
    constexpr std::uint64_t source = 0x2a000, target = 0x2c000;
    for (const auto rounds : {32U, 480U}) {
        for (bool subtract : {false, true}) {
            const unsigned steps = rounds * kWideStepsPerRound;
            PcMachine scalar(4U << 20), fast(4U << 20);
            std::array<std::int64_t, 51> scalar_ns{}, fast_ns{};
            const auto measure = [&](PcMachine& pc, bool batch) {
                pc.enable_boot_acceleration(false);
                pc.cpu.enable_ether_arm64_blocks(false);
                prepare_wide_multiply(pc, subtract, source, target, rounds);
                pc.enable_boot_acceleration(batch);
                pc.cpu.enable_ether_arm64_blocks(batch);
                const auto retired = pc.cpu.instructions;
                const auto start = Clock::now();
                pc.run(steps);
                const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start);
                require(!pc.cpu.fault && pc.cpu.instructions - retired == steps && pc.cpu.regs[Cpu::R9] == 0,
                        "wide multiply benchmark did not finish the guest loop");
                return elapsed.count();
            };
            for (unsigned i = 0; i < scalar_ns.size(); ++i) {
                if (i % 2) {
                    fast_ns[i] = measure(fast, true);
                    scalar_ns[i] = measure(scalar, false);
                } else {
                    scalar_ns[i] = measure(scalar, false);
                    fast_ns[i] = measure(fast, true);
                }
                require(fast.cpu.regs == scalar.cpu.regs && fast.cpu.eflags == scalar.cpu.eflags &&
                            fast.cpu.eip == scalar.cpu.eip && fast.boot_acceleration().wide_multiply_loop_steps != 0,
                        "wide multiply benchmark changed guest state or missed the batch path");
                for (unsigned i = 0; i < rounds; ++i)
                    require(fast.cpu.read64(target + i * 8) == scalar.cpu.read64(target + i * 8),
                            "wide multiply benchmark changed guest output");
            }
            std::sort(scalar_ns.begin(), scalar_ns.end());
            std::sort(fast_ns.begin(), fast_ns.end());
            const auto scalar_median = scalar_ns[scalar_ns.size() / 2], fast_median = fast_ns[fast_ns.size() / 2];
            std::cout << "ETHER_WIDE_MULTIPLY_RATE mode=" << (subtract ? "subtract" : "add")
                      << " words=" << rounds << " runs=" << scalar_ns.size() << " steps=" << steps
                      << " scalar_ns=" << scalar_median << " batch_ns=" << fast_median
                      << " scalar_steps_per_s=" << double(steps) * 1e9 / double(scalar_median)
                      << " batch_steps_per_s=" << double(steps) * 1e9 / double(fast_median) << '\n';
        }
    }
}

void counted_calls() {
    constexpr std::uint64_t high = 0xffff800000000000ULL, start = high + entry;
    constexpr std::uint64_t callee = entry + 0x80, data = 0x2a000, word = 0x2c000, stack = 0x30000;
    constexpr unsigned steps_per_round = 11, rounds = 29, device_deadline = 36;
    constexpr std::array<std::uint8_t, 26> loop{
        0x49,0x83,0xc4,0xfd,       // ADD R12,-3
        0x4c,0x29,0x17,            // SUB [RDI],R10
        0x4c,0x29,0x1e,            // SUB [RSI],R11
        0x66,0x89,0x48,0xe8,       // MOV [RAX-0x18],CX
        0xe8,0x6d,0,0,0,          // CALL callee
        0x49,0x83,0xe9,1,          // SUB R9,1
        0x75,0xe7,0xcc};           // JNZ start; INT3
    constexpr std::array<std::uint8_t, 9> leaf{
        0x41,0x54,                // PUSH R12
        0x4d,0x6b,0xc1,0xfb,      // IMUL R8,R9,-5
        0x41,0x5c,0xc3};           // POP R12; RET
    PcMachine scalar(4U << 20), batch(4U << 20);
    for (auto* pc : {&scalar, &batch}) {
        boot(*pc, true);
        auto& cpu = pc->cpu;
        const auto pml4 = cpu.cr3 & ~std::uint64_t{0xfff};
        cpu.write64(pml4 + 256 * 8, cpu.read64(pml4) & ~0x20ULL);
        cpu.memory.write_block(entry, loop.data(), loop.size());
        cpu.memory.write_block(callee, leaf.data(), leaf.size());
        cpu.idtr = {0xfff, 0xd000}; // Empty IDT to expose the recent kernel calls.
        cpu.halted = false;
        cpu.eip = start;
        cpu.eflags = 2 | Cpu::DF;
        cpu.regs[Cpu::EAX] = word + 0x18;
        cpu.regs[Cpu::ECX] = 0x123456789abc5678ULL;
        cpu.regs[Cpu::ESI] = high + data;
        cpu.regs[Cpu::EDI] = data;
        cpu.regs[Cpu::ESP] = stack;
        cpu.regs[Cpu::R9] = rounds;
        cpu.regs[Cpu::R10] = 7;
        cpu.regs[Cpu::R11] = 11;
        cpu.regs[Cpu::R12] = 1;
        cpu.write64(data, 0x3141592653589793ULL);
        cpu.write64(word, 0x1122334455667788ULL);
        for (unsigned i = 0; i < steps_per_round; ++i)
            require(cpu.step() == StepResult::Executed, "could not seed the counted CALL loop");
        require(cpu.eip == start && cpu.counter_loop_batch_candidate(), "expected a counted CALL loop");
    }

    const auto check_state = [&] {
        const auto& a = scalar.cpu;
        const auto& b = batch.cpu;
        require(!a.fault && !b.fault && a.regs == b.regs && a.eflags == b.eflags && a.eip == b.eip &&
                a.instructions == b.instructions && a.clock_cycles() == b.clock_cycles() &&
                a.last_decoded_bytes() == b.last_decoded_bytes(), "counted CALL loop changed x86 state");
        for (const auto address : {data, word, stack - 16, stack - 8})
            require(a.read64(address) == b.read64(address), "counted CALL loop changed guest RAM");
    };

    auto& cpu = batch.cpu;
    batch.enable_boot_acceleration();
    unsigned ticks = 0;
    decltype(cpu.regs) observed_regs{};
    std::uint64_t observed_ip = 0;
    batch.device_step_hook = [&] {
        if (ticks == device_deadline) {
            observed_regs = cpu.regs;
            observed_ip = cpu.eip;
        }
        ++ticks;
    };
    batch.device_batch_limit_hook = [&](unsigned limit) {
        return ticks < device_deadline ? std::min(limit, device_deadline - ticks + 1) : 1U;
    };
    batch.device_batch_advance_hook = [&](unsigned extra) { ticks += extra; };

    // Finish the partially executed round to re-enter the same cached loop.
    constexpr unsigned tail = steps_per_round - device_deadline % steps_per_round;
    batch.run(device_deadline + tail);
    scalar.run(device_deadline);
    require(observed_regs == scalar.cpu.regs && observed_ip == scalar.cpu.eip &&
            batch.boot_acceleration().counter_loop_steps == 33, "counted CALL loop skipped a device deadline");
    scalar.run(tail);
    check_state();
    require(ticks == device_deadline + tail && cpu.eip == start, "wrong clock at the cache test entry");

    auto retired = cpu.instructions;
    cpu.write8(callee + leaf.size() - 1, 0x90);
    require(!cpu.try_counter_loop_batch(100) && cpu.instructions == retired,
            "counted CALL loop ignored a modified cached callee");
    cpu.write8(callee + leaf.size() - 1, leaf.back());
    check_state();
    const auto original_output = cpu.regs[Cpu::EDI];
    cpu.regs[Cpu::EDI] = stack - 16;
    require(!cpu.try_counter_loop_batch(100) && cpu.instructions == retired,
            "counted CALL loop accepted a stack alias");
    cpu.regs[Cpu::EDI] = original_output;
    check_state();

    const auto budget = (rounds - 1) * steps_per_round - device_deadline - tail;
    require(cpu.try_counter_loop_batch(budget + 1) == budget, "counted CALL loop consumed the next opcode");
    scalar.run(budget);
    check_state();
    require(cpu.eip == start + loop.size() - 1 && cpu.regs[Cpu::R9] == 0,
            "counted CALL loop did not exit on zero");

    require(scalar.cpu.step() == StepResult::Faulted && cpu.step() == StepResult::Faulted &&
            scalar.cpu.fault && cpu.fault && scalar.cpu.fault->interrupt_origin && cpu.fault->interrupt_origin,
            "expected the post-loop INT3 to report kernel CALL evidence");
    const auto& a = *scalar.cpu.fault->interrupt_origin;
    const auto& b = *cpu.fault->interrupt_origin;
    require(a.call_count == a.kRecentCalls && a.call_count == b.call_count, "wrong number of recent CALLs");
    for (unsigned i = 0; i < a.call_count; ++i) {
        const auto& lhs = a.recent_calls[i];
        const auto& rhs = b.recent_calls[i];
        require(lhs.from == rhs.from && lhs.target == rhs.target && lhs.return_ip == rhs.return_ip &&
                lhs.rsp == rhs.rsp && lhs.argument_registers == rhs.argument_registers,
                "counted CALL loop changed the recorded kernel CALL");
    }
    std::cout << "ETHER_COUNTED_CALL_LOOP_PASS device_deadline=1 self_modifying_code=1 aliases=1 kernel_calls=1\n";
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
        if (argc == 2 && std::string_view(argv[1]) == "--measure-wide-multiply") {
            measure_wide_multiply();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--measure-movntdq") {
            measure_movntdq();
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--measure-mixed-movntdq") {
            measure_movntdq(StorePattern::Mixed);
            return 0;
        }
        if (argc == 2 && std::string_view(argv[1]) == "--measure-disp32-movntdq") {
            measure_movntdq(StorePattern::Disp32);
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
        compare(true, {0x49,0xc1,0xc0,17,0x49,0x83,0xf0,0xf3,0x49,0xff,0xc9,0x75,0xf3},
                Cpu::R9, 25, 47, 44, 4);
        compare(true, {0x49,0x83,0xf0,0xf3,0x49,0xd1,0xc8,0x49,0xff,0xc9,0x75,0xf4},
                Cpu::R9, 17, 99, 64, 4);
        compare(true, {0x49,0x81,0xf0,0x26,0x0d,0x04,0x81,0x49,0xc1,0xc8,13,0x49,0x83,0xe9,1,0x75,0xef},
                Cpu::R9, 21, 89, 80, 4);
        unsafe_spans();
        measure_large_loop(false);
        measure_large_loop(true);
        measure_rotate_xor_loop();
        timed_rotate_xor_slice();
        affine_ram_rotations();
        guarded_accumulator_dispatch();
        counted_calls();
        sse_copy_loops();
        sse_fill_loop();
        sse_zero_fill_pages();
        streaming_sse_fill_rounds();
        streaming_sse_triplet_rounds();
        movntdq_store_batches();
        mixed_movntdq_registers();
        wide_movntdq_batch();
        disp32_movntdq_registers();
        wide_multiply_loops();
        std::cout << "ETHER_COUNTER_LOOP_PASS widths=16,32,64 flags=1 paging=1 fallbacks=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ETHER_COUNTER_LOOP_FAIL " << error.what() << '\n';
        return 1;
    }
}
