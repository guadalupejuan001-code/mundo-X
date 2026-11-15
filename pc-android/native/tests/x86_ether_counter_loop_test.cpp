#include "../pc_machine.hpp"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;
using cosmic_pc::StepResult;
constexpr std::uint64_t entry = 0xa000;

void require(bool result, const char* message) {
    if (!result) throw std::runtime_error(message);
}

void prepare(PcMachine& pc, bool paged, std::initializer_list<std::uint8_t> program,
             unsigned reg, std::uint64_t value, std::uint64_t start = entry) {
    if (paged) {
        pc.boot_long_mode_demo();
        pc.run(4000);
        require(pc.cpu.is_long_mode() && pc.cpu.halted && !pc.cpu.fault, "long-mode fixture did not boot");
        pc.cpu.enable_page_translation_cache();
    } else {
        pc.boot_demo();
    }
    auto& cpu = pc.cpu;
    cpu.memory.write_block(start, program.begin(), program.size());
    cpu.eip = start;
    cpu.halted = false;
    cpu.regs[reg] = value;
    cpu.eflags = 2 | Cpu::CF | Cpu::DF | Cpu::OF;
    require(cpu.step() == StepResult::Executed && cpu.step() == StepResult::Executed &&
            cpu.eip == start && cpu.counter_loop_batch_candidate(), "expected a taken scalar JNZ seed");
}

void compare(bool paged, std::initializer_list<std::uint8_t> program, unsigned reg,
             std::uint64_t value, unsigned budget, unsigned expected) {
    PcMachine scalar(4U << 20), batch(4U << 20);
    prepare(scalar, paged, program, reg, value);
    prepare(batch, paged, program, reg, value);
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

void long_counter() {
    constexpr std::uint64_t budget = 8'000'000'000'000;
    PcMachine pc(4U << 20);
    prepare(pc, true, {0x49, 0xff, 0xc9, 0x75, 0xfb, 0x0f, 0x31}, Cpu::R9, budget / 2 + 1);
    auto& cpu = pc.cpu;
    const auto retired = cpu.instructions;
    const auto clock = cpu.clock_cycles();
    require(cpu.try_counter_loop_batch(budget) == budget && cpu.regs[Cpu::R9] == 0 &&
            cpu.eflags == (2 | Cpu::CF | Cpu::DF | Cpu::PF | Cpu::ZF) && cpu.eip == entry + 5 &&
            cpu.instructions == retired + budget && cpu.clock_cycles() == clock + budget,
            "the 64-bit equivalent counter or final state was truncated");
    require(cpu.step() == StepResult::Executed &&
            (cpu.regs[Cpu::EAX] | (cpu.regs[Cpu::EDX] << 32)) == (clock + budget) * Cpu::kTscTicksPerCycle,
            "RDTSC did not include the equivalent instructions");
    std::cout << "ETHER_COUNTER_64BIT_BUDGET_PASS equivalent_steps=" << budget << '\n';
}
}

int main() {
    try {
        compare(false, {0x49, 0x75, 0xfd}, Cpu::ECX, 0x1122334455660021, 17, 16);
        compare(false, {0x49, 0x75, 0xfd}, Cpu::ECX, 0x1122334455660006, 20, 10);
        compare(false, {0x49, 0x75, 0xfd}, Cpu::ECX, 0x1122334455660000, 41, 40);
        compare(false, {0x66, 0xff, 0xc9, 0x75, 0xfb}, Cpu::ECX, 0x1122334400000010, 12, 12);
        compare(true, {0x66, 0x41, 0xff, 0xc9, 0x75, 0xfa}, Cpu::R9, 0x1122334455660008, 40, 14);
        compare(true, {0x41, 0x83, 0xe9, 1, 0x75, 0xfa}, Cpu::R9, 0x1122334400000008, 40, 14);
        compare(true, {0x49, 0xff, 0xc9, 0x75, 0xfb}, Cpu::R9, 0x8000000000000002, 4, 4);
        unsafe_spans();
        long_counter();
        std::cout << "ETHER_COUNTER_LOOP_PASS widths=16,32,64 flags=1 paging=1 fallbacks=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ETHER_COUNTER_LOOP_FAIL " << error.what() << '\n';
        return 1;
    }
}
