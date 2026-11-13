#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <memory>
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
}  // namespace

int main() {
  try {
    copy_guest();
  } catch (const std::exception& error) {
    std::cerr << "X86_RAM_BACKING_FAIL " << error.what() << '\n';
    return 1;
  }
}
