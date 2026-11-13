#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"
#include <array>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <vector>

using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::MemoryFabric;
using cosmic_pc::StepResult;

namespace {

void require(bool result, const char* reason) {
  if (!result) throw std::runtime_error(reason);
}

void emit(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> code) {
  cpu.memory.write_block(address, code.begin(), code.size());
}

void expect_step(Cpu& cpu) {
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "expected executing guest");
}

void copy_fixture(Cpu& cpu, bool backward) {
  cpu.reset(0, 0x7c00);
  emit(cpu, 0x7c00, {0xf3,0xa5,0xf4});
  for (unsigned i = 0; i < 160; ++i) cpu.write8(0x2fe0 + i, std::uint8_t(i * 11));
  cpu.regs[Cpu::ESI] = 0x100002fe0 + (backward ? 158 : 0);
  cpu.regs[Cpu::EDI] = 0x200004fe4 + (backward ? 158 : 0);
  cpu.regs[Cpu::ECX] = 0x300000050;
  cpu.set_flag(Cpu::DF, backward);
  cpu.set_flag(Cpu::CF, true);
}

unsigned run_rep(Cpu& cpu, unsigned slice) {
  unsigned batches = 0;
  for (;;) {
    if (const auto count = cpu.try_rep_batch(slice)) {
      require(count <= slice && cpu.last_rep_batch_bytes() >= count, "batch budget/accounting");
      ++batches;
      continue;
    }
    const auto result = cpu.step();
    if (result == StepResult::Halted) return batches;
    if (result == StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
  }
}

void real_copy() {
  for (const bool backward : {false, true}) {
    Cpu scalar, batched;
    copy_fixture(scalar, backward);
    copy_fixture(batched, backward);
    expect_step(batched);
    require(batched.rep_batch_candidate() && batched.last_decoded_bytes().size() == 2,
            "ordinary REP must seed the batch");
    const auto first = batched.try_rep_batch(7);
    require(first == 7 && batched.instructions == 8 && batched.eip == 0x7c00 &&
            batched.reg16(Cpu::ECX) == 72 && batched.last_rep_batch_bytes() == 14,
            "partial REP must preserve restart IP and count exact steps");
    require(run_rep(batched, 31) > 0, "copy must use more than one page-local batch");
    run_rep(scalar, 1);
    require(scalar.instructions == batched.instructions && scalar.regs == batched.regs &&
            scalar.eflags == batched.eflags && scalar.eip == batched.eip &&
            scalar.memory.allocated_pages() == batched.memory.allocated_pages(), "real copy state mismatch");
    for (unsigned i = 0; i < 160; ++i)
      require(batched.read8(0x4fe4 + i) == scalar.read8(0x4fe4 + i), "real copy bytes mismatch");
  }
}

void restart_and_fallback() {
  Cpu cpu(2 * 1024 * 1024, 2 * MemoryFabric::kPageSize);
  cpu.reset(0, 0x1000);
  emit(cpu, 0x1000, {0xf3,0xaa,0xf4});
  cpu.regs[Cpu::EAX] = 0x27;
  cpu.regs[Cpu::ECX] = 12;
  cpu.regs[Cpu::EDI] = 0x8ffa;
  expect_step(cpu);
  require(cpu.try_rep_batch(12) == 5 && cpu.reg16(Cpu::ECX) == 6 && cpu.reg16(Cpu::EDI) == 0x9000,
          "batch must stop before the next destination page");
  require(cpu.try_rep_batch(12) == 0 && !cpu.fault && cpu.instructions == 6 && cpu.read8(0x9000) == 0,
          "batch must leave a failed allocation to the scalar instruction");
  require(cpu.step() == StepResult::Faulted && cpu.fault->code == FaultCode::MemoryBudget &&
          cpu.eip == 0x1000 && cpu.reg16(Cpu::ECX) == 6 && cpu.read8(0x8fff) == 0x27,
          "fault must preserve previously completed elements");

  Cpu guarded;
  copy_fixture(guarded, false);
  expect_step(guarded);
  const auto before = guarded.instructions;
  guarded.memory[0x7c01] = 0x90;
  require(guarded.try_rep_batch(31) == 0 && guarded.instructions == before,
          "changed guest code must bypass the decoded batch");
  guarded.memory[0x7c01] = 0xa5;
  guarded.regs[Cpu::EDI] = 0x7c00;
  require(guarded.try_rep_batch(31) == 0, "destination overlaps executing code");
  guarded.regs[Cpu::EDI] = guarded.regs[Cpu::ESI] + 4;
  require(guarded.try_rep_batch(31) == 0, "overlapping copy must retain sequential semantics");
  guarded.regs[Cpu::EDI] = 0x5000;
  guarded.memory.attach_window(0x5010, 4, [](std::uint64_t) { return std::uint8_t{0}; },
                               [](std::uint64_t, std::uint8_t) {});
  require(guarded.try_rep_batch(31) == 0, "Ether window must keep exact callbacks");
  guarded.regs[Cpu::EDI] = 0x5100;
  guarded.mmio_claim_hook = [](std::uint64_t addr) { return addr == 0x5110; };
  require(guarded.try_rep_batch(31) == 0, "legacy MMIO needs an authoritative span check");
  guarded.mmio_range_claim_hook = [](std::uint64_t addr, std::size_t bytes) {
    return addr <= 0x5110 && 0x5110 - addr < bytes;
  };
  require(guarded.try_rep_batch(31) == 0, "MMIO in the middle of a span must reject batch");
  guarded.regs[Cpu::EDI] = 0x5200;
  guarded.step_trace_hook = [](const auto&) {};
  require(guarded.try_rep_batch(31) == 0, "precise trace must use scalar steps");
  guarded.step_trace_hook = {};
  require(guarded.try_rep_batch(31) == 15, "guarded ordinary RAM must permit bounded batching");
}

void a20_alias() {
  Cpu cpu;
  cpu.reset(0, 0x7c00);
  cpu.segments[Cpu::ES] = 0xffff;
  emit(cpu, 0x7c00, {0xf3,0xaa,0xf4});
  cpu.regs[Cpu::EAX] = 0xde;
  cpu.regs[Cpu::ECX] = 20;
  cpu.regs[Cpu::EDI] = 8;
  expect_step(cpu);
  require(cpu.try_rep_batch(20) == 7 && cpu.reg16(Cpu::EDI) == 16,
          "A20 wrap must bound the batch");
  require(cpu.try_rep_batch(20) == 12 && cpu.eip == 0x7c02 && cpu.read8(0) == 0xde &&
          cpu.memory.read_byte(0x100000) == 0, "A20 wrap must write the low alias only");
}

void irq_checkpoint() {
  Cpu cpu;
  copy_fixture(cpu, false);
  emit(cpu, 0x1800, {0xcf}); // IRET to the interrupted REP.
  cpu.write16(0x20 * 4, 0x1800);
  cpu.regs[Cpu::ESP] = 0x7a00;
  cpu.set_flag(Cpu::IF, true);
  expect_step(cpu);
  require(cpu.try_rep_batch(5) == 5, "fixture must batch exactly to the IRQ boundary");
  const auto indices = cpu.regs;
  require(cpu.raise_external_irq(0x20) && cpu.eip == 0x1800 && cpu.read16(0x79fa) == 0x7c00,
          "external IRQ must stack REP restart IP after counted elements");
  expect_step(cpu);
  require(cpu.eip == 0x7c00 && cpu.regs == indices && cpu.flag(Cpu::IF),
          "IRET must restore partially consumed REP registers and IP");
  require(cpu.try_rep_batch(12) == 0, "IRET must invalidate the preceding decode seed");
  expect_step(cpu);
  require(cpu.try_rep_batch(6) == 6, "REP may batch again after restart decodes");
  cpu.cr0 |= 0x80000000U;
  require(cpu.try_rep_batch(8) == 0, "paging must retain precise scalar page walk");
}

void protected_fixture(Cpu& cpu) {
  const auto disk = cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00, disk.data(), disk.size());
  cpu.interrupt_hook = [](Cpu& c, std::uint8_t number) {
    if (number != 0x15 || c.reg16(Cpu::EAX) != 0x2401) return false;
    c.a20_enabled = true;
    c.set_reg8(4, 0);
    c.set_flag(Cpu::CF, false);
    return true;
  };
  while (std::string_view(cpu.mode_name()) != "protected32" || cpu.segments[Cpu::SS] != 0x10)
    expect_step(cpu);
  emit(cpu, 0x8000, {0xf3,0xab,0xf3,0xa5,0x0f,0x31,0xf4});
  cpu.eip = 0x8000;
}

void copy_under_pressure() {
  constexpr auto page = MemoryFabric::kPageSize;
  constexpr std::uint32_t from = 0x32000, to = 0x11c1c000;
  constexpr std::uint32_t count = 3 * page / sizeof(std::uint32_t);
  constexpr std::array<std::uint8_t, 8> pattern{0x11, 0x22, 0x44, 0x66, 0x31, 0x86, 0x37, 0xeb};

  Cpu cpu(300 * 1024 * 1024, 4 * page + 3 * pattern.size());
  protected_fixture(cpu);
  cpu.eip = 0x8002;
  require(cpu.memory.allocated_pages() == 2, "fixture code must occupy two dense pages");
  std::array<std::uint8_t, page> source;
  for (std::size_t i = 0; i < source.size(); ++i) source[i] = pattern[i % pattern.size()];
  // The first page is backed densely, the second is an exact pattern, and
  // the third stays implicit zero. Give the scalar seed one dense page.
  cpu.memory.write_block(from, source.data(), source.size());
  cpu.memory.write_block(from + page, source.data(), source.size());
  require(cpu.memory.compact_repeating_page(from + page), "fixture compact source page");
  cpu.regs[Cpu::ESI] = from;
  cpu.regs[Cpu::EDI] = to;
  cpu.regs[Cpu::ECX] = count;
  expect_step(cpu);
  require(cpu.memory.budget_bytes() - cpu.memory.allocated_bytes() == 2 * pattern.size(),
          "fixture must leave less than one dense page for REP");
  require(cpu.try_rep_batch(count) == page / 4 - 1 && cpu.memory.pattern_pages() == 2,
          "finishing the first copied page must compact its exact bytes");
  // Consume the dense headroom before copying the compact and zero pages.
  cpu.memory.write_byte(0x15000, 1);
  require(cpu.memory.budget_bytes() - cpu.memory.allocated_bytes() == pattern.size(),
          "the next page must fit in its exact pattern backing");
  const auto resident = cpu.memory.allocated_pages();
  require(cpu.try_rep_batch(count) == page / 4 && cpu.memory.pattern_pages() == 3 &&
              cpu.memory.allocated_pages() == resident &&
              cpu.memory.allocated_bytes() == cpu.memory.budget_bytes(),
          "copy of a repeated page must use only its stored pattern");

  require(cpu.try_rep_batch(count) == page / 4 && cpu.regs[Cpu::ECX] == 0 &&
              cpu.eip == 0x8004 && cpu.instructions > count,
          "copy of implicit zero must complete without dense backing");
  std::array<std::uint8_t, page> bytes;
  for (const auto offset : std::array<std::uint64_t, 2>{to, to + page}) {
    cpu.memory.read_block(offset, bytes.data(), bytes.size());
    require(bytes == source, "copied pattern bytes differ");
  }
  cpu.memory.read_block(to + 2 * page, bytes.data(), bytes.size());
  require(bytes == decltype(bytes){}, "copy of implicit zero produced nonzero bytes");

  // Replacing a populated page frees its old backing. Mutating the compact
  // destination afterwards must leave the original bytes independent.
  require(cpu.memory.try_copy_repeating_page(from + 2 * page, 0x15000) &&
          cpu.memory.read_byte(0x15000) == 0 && cpu.memory.allocated_pages() == resident - 1,
          "full zero page must replace stale backing");
  cpu.memory.write_byte(to + page + 7, 0x4a);
  require(cpu.read8(from + page + 7) == pattern[7] && cpu.read8(to + page + 7) == 0x4a,
          "copied page writes must not alias the source");
}

struct BenchResult {
  std::uint64_t steps = 0;
  std::int64_t host_us = 0;
  unsigned batches = 0;
  unsigned patterns = 0;
};

BenchResult measure(bool use_batches) {
  Cpu cpu(8 * 1024 * 1024);
  protected_fixture(cpu);
  constexpr unsigned elements = 1U << 18, address = 0x100000;
  constexpr unsigned pattern = 0xd84ea613;
  cpu.regs[Cpu::EAX] = pattern;
  cpu.regs[Cpu::ECX] = elements;
  cpu.regs[Cpu::EDI] = address;
  const auto begin = std::chrono::steady_clock::now();
  BenchResult result;
  const auto run_to = [&](unsigned first_ip, unsigned next_ip) {
    while (cpu.eip == first_ip) {
      if (use_batches && cpu.try_rep_batch(4096)) ++result.batches;
      else expect_step(cpu);
    }
    require(cpu.eip == next_ip && cpu.regs[Cpu::ECX] == 0, "protected REP must finish exactly");
  };
  run_to(0x8000, 0x8002);
  result.patterns = static_cast<unsigned>(cpu.memory.pattern_pages());
  cpu.regs[Cpu::ESI] = address;
  cpu.regs[Cpu::ECX] = elements;
  run_to(0x8002, 0x8004);
  result.host_us = std::chrono::duration_cast<std::chrono::microseconds>(
      std::chrono::steady_clock::now() - begin).count();
  result.steps = cpu.instructions;
  expect_step(cpu); // RDTSC in the same guest instruction stream.
  require(cpu.regs[Cpu::EAX] == result.steps * 64 && cpu.regs[Cpu::EDX] == 0, "guest clock mismatch");
  for (unsigned i = 0; i < elements * 8; i += 4)
    require(cpu.read32(address + i) == pattern, "protected REP fill or copy mismatch");
  return result;
}

}  // namespace

int main() {
  try {
    real_copy();
    restart_and_fallback();
    a20_alias();
    irq_checkpoint();
    copy_under_pressure();
    const auto scalar = measure(false), batched = measure(true);
    require(scalar.steps == batched.steps && batched.batches > 0 && batched.patterns == 256 &&
            !scalar.patterns, "REP changed step count or failed to compact full STOS pages");
    std::cout << "ETHER_REP_BATCH_PASS real16=1 protected32=1 restart=1 exact_clock=1 a20=1 irq=1"
              << " patterned_movsd=1 sparse_zero_movsd=1"
              << " scalar_us=" << scalar.host_us << " batched_us=" << batched.host_us
              << " logical_steps=" << batched.steps << " batches=" << batched.batches
              << " pattern_pages=" << batched.patterns << '\n';
  } catch (const std::exception& e) {
    std::cerr << "ETHER_REP_BATCH_FAIL " << e.what() << '\n';
    return 1;
  }
}
