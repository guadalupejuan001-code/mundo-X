#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::MemoryFabric;
using cosmic_pc::SparseRam;
using cosmic_pc::StepResult;

constexpr std::uint64_t kSource = 0x32000;
constexpr std::uint64_t kTarget = 0x11c1c000;
constexpr std::uint64_t kEntry = 0x48ace3;
constexpr unsigned kElements = 13824;
constexpr unsigned kPage = MemoryFabric::kPageSize;
constexpr unsigned kResidentBudget = 24 * kPage;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

class TestBacking final : public SparseRam::PageBacking {
 public:
  void read(std::uint64_t index, std::uint8_t* page) override {
    const auto& saved = pages_.at(index);
    std::copy(saved.begin(), saved.end(), page);
  }

  void write(std::uint64_t index, const std::uint8_t* page) override {
    std::copy_n(page, kPage, pages_[index].data());
  }

  void reset() noexcept override { pages_.clear(); }

 private:
  std::unordered_map<std::uint64_t, std::array<std::uint8_t, kPage>> pages_;
};

void step(Cpu& cpu) {
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "unexpected guest stop");
}

void fixture(Cpu& cpu, const std::vector<std::uint8_t>& image) {
  const auto boot = cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00, boot.data(), boot.size());
  cpu.interrupt_hook = [](Cpu& c, std::uint8_t vector) {
    if (vector != 0x15 || c.reg16(Cpu::EAX) != 0x2401) return false;
    c.a20_enabled = true;
    c.set_flag(Cpu::CF, false);
    return true;
  };
  for (unsigned i = 0; i < 100; ++i) {
    if (std::string_view(cpu.mode_name()) == "protected32" && cpu.segments[Cpu::SS] == 0x10) break;
    step(cpu);
  }
  require(std::string_view(cpu.mode_name()) == "protected32", "guest did not enter protected32");
  constexpr std::array<std::uint8_t, 3> program{0xf3, 0xa5, 0xf4};
  cpu.memory.write_block(kEntry, program.data(), program.size());
  for (std::size_t i = 0; i < image.size(); i += kPage)
    cpu.memory.write_block(kSource + i, image.data() + i, std::min<std::size_t>(kPage, image.size() - i));
  cpu.eip = kEntry;
  cpu.regs[Cpu::ESI] = kSource;
  cpu.regs[Cpu::EDI] = kTarget;
  cpu.regs[Cpu::ECX] = kElements;
}

void copy_guest() {
  std::vector<std::uint8_t> image(kElements * 4);
  std::uint32_t state = 0x1293ae93;
  for (auto& byte : image) {
    state = state * 1664525U + 1013904223U;
    byte = std::uint8_t(state >> 24);
  }

  Cpu reference(2048ULL << 20, 256 * kPage);
  Cpu capped(2048ULL << 20, kResidentBudget);
  Cpu backed(2048ULL << 20, kResidentBudget);
  backed.memory.attach_page_backing(std::make_unique<TestBacking>());
  fixture(reference, image);
  fixture(capped, image);
  fixture(backed, image);

  while (capped.step() == StepResult::Executed) {}
  require(capped.fault && capped.fault->code == cosmic_pc::FaultCode::MemoryBudget &&
              capped.eip == kEntry && capped.regs[Cpu::ECX] > 0 &&
              capped.regs[Cpu::ECX] < kElements,
          "fixture must reproduce a memory-budget fault partway through REP MOVSD");
  while (reference.eip == kEntry) step(reference);
  unsigned batches = 0;
  while (backed.eip == kEntry) {
    if (backed.try_rep_batch(191)) ++batches;
    else step(backed);
    require(backed.memory.allocated_bytes() <= kResidentBudget, "resident cap exceeded");
  }
  require(batches > 0 && backed.memory.metrics().spilled_pages > 0 && !backed.fault,
          "guest must batch and spill under the resident cap");
  require(backed.eip == reference.eip && backed.regs == reference.regs &&
              backed.eflags == reference.eflags && backed.instructions == reference.instructions,
          "protected32 REP state differs from the scalar reference");
  require(backed.step() == StepResult::Halted && reference.step() == StepResult::Halted,
          "guest did not execute the instruction following REP MOVSD");
  std::vector<std::uint8_t> result(image.size());
  backed.memory.read_block(kTarget, result.data(), result.size());
  require(result == image, "file-backed copy corrupted the destination bytes");
  backed.memory.read_block(kSource, result.data(), result.size());
  require(result == image, "file-backed copy corrupted the source bytes");
  require(backed.memory.metrics().backing_reads > 0, "guest fixture never retrieved a spilled page");
  std::cout << "X86_RAM_BACKING_PASS protected32=1 rep_movsd_elements=" << kElements
            << " spill_bytes=" << backed.memory.metrics().backing_bytes
            << " batches=" << batches << '\n';
}

void protected_immediate_fetch() {
  Cpu cpu(2048ULL << 20, kResidentBudget);
  cpu.memory.attach_page_backing(std::make_unique<TestBacking>());
  fixture(cpu, {});
  const auto expected = std::vector<std::uint8_t>{0xb8, 0x78, 0x56, 0x34, 0x12};
  std::array<std::uint8_t, kPage> code{};
  for (unsigned offset = 0; offset < code.size(); ++offset)
    code[offset] = static_cast<std::uint8_t>(offset * 41 + offset / 7);
  const auto start = kEntry & ~(std::uint64_t(kPage) - 1);
  std::copy(expected.begin(), expected.end(), code.begin() + (kEntry - start));
  cpu.memory.write_block(start, code.data(), code.size());
  step(cpu);
  require(cpu.regs[Cpu::EAX] == 0x12345678 && cpu.eip == kEntry + expected.size() &&
              cpu.last_decoded_bytes() == expected,
          "dense protected32 immediate did not preserve value or trace bytes");
  const std::vector<std::uint8_t> displacement{0x8b, 0x1d, 0x00, 0x90, 0x00, 0x00};
  cpu.memory.write_block(cpu.eip, displacement.data(), displacement.size());
  cpu.write32(0x9000, 0x83afe174);
  step(cpu);
  require(cpu.regs[Cpu::EBX] == 0x83afe174 && cpu.last_decoded_bytes() == displacement,
          "protected32 ModR/M displacement changed the effective address or trace");
  require(cpu.memory.adjust_payload_budget(0, kResidentBudget / kPage) == SparseRam::BudgetAdjustment::Applied &&
              cpu.memory.adjust_payload_budget(kResidentBudget, 0) == SparseRam::BudgetAdjustment::Applied &&
              !cpu.memory.resident_ram_span(kEntry, 1),
          "protected32 fetch fixture did not spill the code page");
  const auto before = cpu.memory.metrics();
  std::array<std::uint8_t, 4> cold{0x45, 0x45, 0x45, 0x45};
  require(!cpu.memory.try_read_present_ram_block(kEntry + 1, cold.data(), cold.size()) &&
              cold == std::array<std::uint8_t, 4>{0x45, 0x45, 0x45, 0x45} &&
              cpu.memory.metrics().backing_reads == before.backing_reads,
          "a cold instruction span performed I/O or modified its caller's bytes");
  cpu.eip = kEntry;
  step(cpu);
  const auto fetched = cpu.memory.metrics();
  require(cpu.regs[Cpu::EAX] == 0x12345678 && cpu.last_decoded_bytes() == expected &&
              fetched.backing_reads == before.backing_reads + 1 &&
              fetched.backing_cache_hits == before.backing_cache_hits + (MemoryFabric::kUsesStateRam ? 1 : 4),
          "cold opcode and decoded immediate did not use the expected exact-width backing path");
  cpu.write8(kEntry + 1, 0xb3);
  cpu.eip = kEntry;
  {
    const MemoryFabric::ExclusiveRamReads reads(cpu.memory);
    step(cpu);
  }
  require(cpu.regs[Cpu::EAX] == 0x123456b3 && cpu.last_decoded_bytes()[1] == 0xb3,
          "an exclusive protected32 fetch observed stale backing after a code write");

  const auto crosses = start + kPage - 2;
  cpu.memory.write_block(crosses, expected.data(), expected.size());
  cpu.eip = crosses;
  step(cpu);
  require(cpu.last_decoded_bytes() == expected && cpu.eip == crosses + expected.size(),
          "a protected32 immediate straddling a page lost bytes");
  const auto device = crosses + 8;
  cpu.memory.write_block(device, expected.data(), expected.size());
  unsigned reads = 0;
  cpu.mmio_claim_hook = [&](std::uint64_t address) { return address == device + 2; };
  cpu.mmio_range_claim_hook = [&](std::uint64_t address, std::uint64_t bytes) {
    return address <= device + 2 && device + 2 - address < bytes;
  };
  cpu.mmio_read_hook = [&](std::uint64_t address) -> std::optional<std::uint8_t> {
    require(address == device + 2, "protected32 fetch addressed the wrong virtual device");
    ++reads;
    return 0xcd;
  };
  cpu.eip = device;
  step(cpu);
  require(reads == 1 && cpu.regs[Cpu::EAX] == 0x1234cd78 && cpu.last_decoded_bytes()[2] == 0xcd,
          "an immediate overlapping a device bypassed byte-precise MMIO reads");
  cpu.mmio_claim_hook = {};
  cpu.mmio_range_claim_hook = {};
  cpu.mmio_read_hook = {};

  cpu.a20_enabled = false;
  constexpr std::uint64_t a20_boundary = 0x100000;
  cpu.memory.write_block(a20_boundary - 3, expected.data(), 3);
  cpu.memory.write_block(0, expected.data() + 3, 2);
  cpu.eip = a20_boundary - 3;
  step(cpu);
  require(cpu.regs[Cpu::EAX] == 0x12345678 && cpu.last_decoded_bytes() == expected,
          "protected32 immediate lost the A20 wrap on its byte path");
  cpu.a20_enabled = true;
  const auto end = cpu.memory.size() - 3;
  cpu.memory.write_block(end, expected.data(), 3);
  cpu.eip = end;
  const auto retired = cpu.instructions;
  require(cpu.step() == StepResult::Faulted && cpu.fault && cpu.fault->code == cosmic_pc::FaultCode::MemoryBounds &&
              cpu.fault->bytes == std::vector<std::uint8_t>(expected.begin(), expected.begin() + 3) &&
              cpu.eip == end && cpu.instructions == retired,
          "out-of-bounds protected32 immediate changed the scalar fault bytes or retirement");

  Cpu restricted(2 * 1024 * 1024);
  restricted.reset(0, 0x1000);
  restricted.a20_enabled = true;
  restricted.gdtr = {0x0f, 0x6000};
  restricted.write64(0x6008, 0x00409a00000020ffULL); // Flat 32-bit executable segment ending at 20ffh.
  constexpr std::uint8_t enter[]{0xfa, 0xb8,0x11,0, 0x0f,0x22,0xc0, 0xea,0xfd,0x20,0x08,0};
  restricted.memory.write_block(0x1000, enter, sizeof(enter));
  restricted.memory.write_block(0x20fd, expected.data(), expected.size());
  for (unsigned i = 0; i < 4; ++i) step(restricted);
  require(restricted.step() == StepResult::Faulted && restricted.fault &&
              restricted.fault->code == cosmic_pc::FaultCode::SegmentLimit && restricted.eip == 0x20fd &&
              restricted.fault->bytes == std::vector<std::uint8_t>(expected.begin(), expected.begin() + 3),
          "an immediate crossing the CS limit changed the precise segment fault");
  std::vector<std::uint8_t> overlength(13, 0x66);
  overlength.insert(overlength.end(), {0xb8, 0x45, 0x89});
  restricted.memory.write_block(0x2070, overlength.data(), overlength.size());
  restricted.eip = 0x2070;
  restricted.fault.reset();
  overlength.pop_back();
  require(restricted.step() == StepResult::Faulted && restricted.fault &&
              restricted.fault->code == cosmic_pc::FaultCode::InvalidInstruction &&
              restricted.eip == 0x2070 && restricted.fault->bytes == overlength,
          "an overlength protected32 immediate changed the first 15 decoded bytes");
  std::cout << "X86_PROTECTED_IMMEDIATE_FETCH_PASS dense=1 displacement=1 decoded_backing=1 crossing=1 mmio=1"
               " a20=1 bounds_fault=1 segment_fault=1 updated_code=1 overlength=1\n";
}
}  // namespace

int main() {
  try {
    copy_guest();
    protected_immediate_fetch();
  } catch (const std::exception& error) {
    std::cerr << "X86_RAM_BACKING_FAIL " << error.what() << '\n';
    return 1;
  }
}
