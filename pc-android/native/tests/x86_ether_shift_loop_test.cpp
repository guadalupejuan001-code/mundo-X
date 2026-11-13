#include "../x86_cpu.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>

using cosmic_pc::Cpu;
using cosmic_pc::StepResult;

namespace {

constexpr std::uint16_t kLoop = 0x8123;
constexpr std::uint16_t kAfterLoop = kLoop + 6;

void require(bool result, const char* reason) {
  if (!result) throw std::runtime_error(reason);
}

void expect_step(Cpu& cpu) {
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "expected executing guest");
}

void expect_equal(const Cpu& scalar, const Cpu& batched) {
  require(scalar.instructions == batched.instructions && scalar.eip == batched.eip &&
          scalar.segments == batched.segments && scalar.regs == batched.regs &&
          scalar.eflags == batched.eflags && scalar.last_decoded_bytes() == batched.last_decoded_bytes(),
          "scalar and batched guest state differ");
}

void seed(Cpu& cpu, const bool left, const std::uint16_t cx, const std::uint32_t pair,
          const std::uint32_t flags, const bool protected_mode = true,
          const bool other_registers = false) {
  cpu.reset(0, protected_mode ? 0x7c00 : kLoop);
  if (protected_mode) {
    const std::array<std::uint8_t, 13> enter{
        0x0f,0x20,0xc0, 0x0c,0x01, 0x0f,0x22,0xc0,
        0xea,std::uint8_t(kLoop),std::uint8_t(kLoop >> 8),0x08,0x00};
    cpu.gdtr = {0x0f, 0x1000};
    cpu.write64(0x1008, 0x00009a000000ffffULL);
    cpu.memory.write_block(0x7c00, enter.data(), enter.size());
    for (unsigned i = 0; i < 4; ++i) expect_step(cpu);
    require(std::string_view(cpu.mode_name()) == "protected16", "missing protected16 fixture");
  }
  const unsigned low = other_registers ? Cpu::EBX : Cpu::EAX;
  const unsigned high = other_registers ? Cpu::EDI : Cpu::EDX;
  const auto shift = std::uint8_t(left ? 0xe0U | low : 0xe8U | high);
  const auto rotate = std::uint8_t(left ? 0xd0U | high : 0xd8U | low);
  const std::array<std::uint8_t, 6> code{0xd1, shift, 0xd1, rotate, 0xe2, 0xfa};
  cpu.memory.write_block(kLoop, code.data(), code.size());
  cpu.regs[low] = 0x123456789abc0000ULL | std::uint16_t(pair);
  cpu.regs[high] = 0x87654321cdef0000ULL | std::uint16_t(pair >> 16);
  cpu.regs[Cpu::ECX] = 0xfedcba9876540000ULL | cx;
  cpu.eflags = flags | 2;
}

void compare_slices() {
  Cpu scalar, batched;
  unsigned checks = 0, batches = 0;
  std::uint32_t state = 0x89abcdef;
  for (bool left : {false, true}) {
    for (unsigned i = 0; i < 180; ++i) {
      state = 1664525U * state + 1013904223U;
      const auto count = std::uint16_t(1 + i % 48);
      const auto flags = (state & (Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::IF | Cpu::DF | Cpu::OF));
      seed(scalar, left, count, state, flags, true, i % 2);
      seed(batched, left, count, state, flags, true, i % 2);
      while (scalar.eip != kAfterLoop) {
        const unsigned budget = 1 + (i + checks) % 108;
        const auto complete = batched.try_shift_loop_batch(budget);
        require(complete <= budget && complete % 3 == 0, "invalid batch grant");
        if (!complete) expect_step(batched);
        for (unsigned step = 0; step < (complete ? complete : 1); ++step) expect_step(scalar);
        expect_equal(scalar, batched);
        ++checks;
        if (complete) ++batches;
      }
    }
  }
  require(batches > 300, "expected both profile motifs and remapped registers to batch");
  std::cout << "Protected16 shift pairs: " << checks << " exact slice comparisons, " << batches
            << " batched slices\n";
}

#if defined(__x86_64__) && defined(__linux__) && !defined(__ANDROID__) && \
    (defined(__GNUC__) || defined(__clang__))
void check_native_x86() {
  constexpr auto defined_flags = Cpu::CF | Cpu::OF | Cpu::SF | Cpu::ZF | Cpu::PF;
  Cpu cpu;
  std::uint32_t pair = 0x81234567;
  unsigned checks = 0;
  for (bool left : {false, true}) {
    for (std::uint16_t count = 1; count <= 40; ++count) {
      for (unsigned i = 0; i < 12; ++i) {
        pair = 1664525U * pair + 1013904223U;
        auto low = std::uint16_t(pair);
        auto high = std::uint16_t(pair >> 16);
        auto counter = std::uint64_t(count);
        std::uint64_t flags = 0;
        // Capture flags directly after the final rotate and LOOP. Preserve the
        // System V red zone below RSP when PUSHFQ records the hardware flags.
#define NATIVE_SHIFT_LOOP(shift, rotate)                                      \
        __asm__ __volatile__("subq $128, %%rsp\n\t"                           \
                             "1: " shift "\n\t" rotate "\n\tloop 1b\n\t"      \
                             "pushfq\n\tpopq %[flags]\n\taddq $128, %%rsp"   \
                             : [low] "+&r" (low), [high] "+&r" (high),       \
                               "+&c" (counter), [flags] "=&r" (flags)        \
                             : : "cc", "memory")
        if (left) NATIVE_SHIFT_LOOP("shlw %w[low]", "rclw %w[high]");
        else NATIVE_SHIFT_LOOP("shrw %w[high]", "rcrw %w[low]");
#undef NATIVE_SHIFT_LOOP
        seed(cpu, left, count, pair, 0);
        for (unsigned step = 0; step < 3; ++step) expect_step(cpu);
        if (count > 1)
          require(cpu.try_shift_loop_batch(3 * count) == 3U * (count - 1), "native oracle must batch");
        require(cpu.reg16(Cpu::EAX) == low && cpu.reg16(Cpu::EDX) == high &&
                (cpu.eflags & defined_flags) == (flags & defined_flags) && cpu.reg16(Cpu::ECX) == counter,
                "hardware x86 shift pair result or defined flags differ");
        ++checks;
      }
    }
  }
  std::cout << "Native x86 shift pairs: " << checks << " result and defined-flags comparisons\n";
}
#endif

void benchmark_wrap() {
  using Clock = std::chrono::steady_clock;
  Cpu scalar, batched;
  std::chrono::nanoseconds scalar_time{}, batched_time{};
  for (bool left : {false, true}) {
    seed(scalar, left, 0, 0xffff8001U, Cpu::IF | Cpu::AF);
    seed(batched, left, 0, 0xffff8001U, Cpu::IF | Cpu::AF);
    auto start = Clock::now();
    while (scalar.eip != kAfterLoop) expect_step(scalar);
    scalar_time += Clock::now() - start;
    start = Clock::now();
    for (unsigned i = 0; i < 3; ++i) expect_step(batched);
    require(batched.reg16(Cpu::ECX) == 65535, "LOOP must wrap an initial CX of zero");
    require(batched.try_shift_loop_batch(200000) == 65535 * 3, "bulk wrap retirement");
    batched_time += Clock::now() - start;
    expect_equal(scalar, batched);
    require(batched.instructions == 4 + 65536 * 3, "logical steps must include both shifts and LOOP");
  }
  const auto micros = [](auto time) { return std::chrono::duration<double, std::micro>(time).count(); };
  std::cout << "Protected16 profile instruction pairs, 393216 guest steps: scalar "
            << micros(scalar_time) << " us, batched " << micros(batched_time) << " us\n";
}

void reject_changed_or_observed_code() {
  Cpu cpu;
  seed(cpu, true, 26, 0x8000cafe, Cpu::AF);
  for (unsigned i = 0; i < 3; ++i) expect_step(cpu);
  require(cpu.shift_loop_batch_candidate(), "taken scalar LOOP must seed batching");
  const auto before = cpu.instructions;
  const auto expect_fallback = [&] {
    require(cpu.try_shift_loop_batch(31) == 0 && cpu.instructions == before && cpu.eip == kLoop,
            "failed guard must preserve instruction state");
  };
  cpu.memory[kLoop + 3] = 0xd0; // RCL AX would make both operands alias.
  expect_fallback();
  cpu.memory[kLoop + 3] = 0xd1; // RCL CX would alter LOOP's count.
  expect_fallback();
  cpu.memory[kLoop + 3] = 0xd2;
  cpu.memory[kLoop + 5] = 0xf9;
  expect_fallback();
  cpu.memory[kLoop + 5] = 0xfa;
  cpu.step_trace_hook = [](const auto&) {};
  expect_fallback();
  cpu.step_trace_hook = {};
  cpu.mmio_claim_hook = [](std::uint64_t addr) { return addr == kLoop + 3; };
  expect_fallback();
  cpu.mmio_range_claim_hook = [](std::uint64_t addr, std::size_t bytes) {
    return addr <= kLoop + 3 && kLoop + 3 - addr < bytes;
  };
  expect_fallback();
  cpu.mmio_claim_hook = {};
  cpu.mmio_range_claim_hook = {};
  require(cpu.try_shift_loop_batch(2) == 0 && cpu.instructions == before, "two-step deadline");
  require(cpu.try_shift_loop_batch(31) == 30, "valid shift loop after guard rejection");
}

void irq_between_shift_and_rotate() {
  Cpu scalar, batched;
  for (auto* cpu : {&scalar, &batched}) {
    seed(*cpu, false, 19, 0x800004c3, Cpu::IF, false);
    cpu->set_reg16(Cpu::ESP, 0xa000);
    cpu->write16(0x100, 0x9000); // IVT vector 0x40.
    cpu->write16(0x102, 0);
    cpu->write8(0x9000, 0xcf); // IRET.
    for (unsigned i = 0; i < 3; ++i) expect_step(*cpu);
  }
  constexpr unsigned steps_to_irq = 19;
  const auto completed = batched.try_shift_loop_batch(steps_to_irq);
  require(completed == 18 && batched.try_shift_loop_batch(1) == 0, "IRQ deadline must split the loop");
  expect_step(batched);
  for (unsigned i = 0; i < steps_to_irq; ++i) expect_step(scalar);
  expect_equal(scalar, batched);
  for (auto* cpu : {&scalar, &batched}) {
    require(cpu->eip == kLoop + 2 && cpu->raise_external_irq(0x40), "IRQ did not land at RCR");
    require(cpu->read16(0x9ffa) == kLoop + 2, "wrong interrupt return IP");
    expect_step(*cpu);
    require(cpu->eip == kLoop + 2, "IRET did not resume the interrupted iteration");
    expect_step(*cpu);
    expect_step(*cpu);
  }
  expect_equal(scalar, batched);
}

} // namespace

int main() {
  try {
    compare_slices();
#if defined(__x86_64__) && defined(__linux__) && !defined(__ANDROID__) && \
    (defined(__GNUC__) || defined(__clang__))
    check_native_x86();
#endif
    benchmark_wrap();
    reject_changed_or_observed_code();
    irq_between_shift_and_rotate();
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
