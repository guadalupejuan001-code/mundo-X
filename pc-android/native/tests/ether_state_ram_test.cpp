#define MUNDO_X_PC_STATE_RAM 1
#include "../memory_fabric.hpp"

#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <random>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {
using Ram = cosmic_pc::EtherStateRam;
using Fabric = cosmic_pc::MemoryFabric;
constexpr auto page_size = Ram::kPageSize;
using Page = std::array<std::uint8_t, page_size>;
unsigned checks = 0;
void need(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template <class Function> void rejects(Function&& fn, const char* message) {
  bool failed = false;
  try { fn(); } catch (const std::exception&) { failed = true; }
  need(failed, message);
}
struct Backing final : Ram::PageBacking {
  std::map<std::uint64_t, Page> pages;
  bool fail_write = false, fail_read = false;
  unsigned writes = 0;
  void read(std::uint64_t page, std::uint8_t* bytes) override {
    if (fail_read) throw std::runtime_error("injected read failure");
    std::memcpy(bytes, pages.at(page).data(), page_size);
  }
  void write(std::uint64_t page, const std::uint8_t* bytes) override {
    if (fail_write) throw std::runtime_error("injected write failure");
    std::memcpy(pages[page].data(), bytes, page_size); ++writes;
  }
  std::size_t prepare_pages(const PageWrite* input, std::size_t count,
                            Ram::ReclaimClock::time_point) override {
    if (fail_write) throw std::runtime_error("injected prepare failure");
    auto candidate = pages;
    for (std::size_t i = 0; i < count; ++i)
      std::memcpy(candidate[input[i].page].data(), input[i].bytes, page_size);
    pages.swap(candidate); writes += static_cast<unsigned>(count); return count;
  }
  void discard(std::uint64_t page) noexcept override { pages.erase(page); }
  void reset() noexcept override { pages.clear(); }
};
Page varied(unsigned seed) {
  Page result{};
  for (unsigned i = 0; i < result.size(); ++i) result[i] = std::uint8_t((i * 17 + (i >> 4) + seed) & 255);
  return result;
}

// Finite fault-injection authority. The separate Windows integration test uses
// the real QuantumRuntime/InverseMemory/adaptive-vector implementation.
struct Cells final : Ram::QuantumAuthority {
  std::array<bool, 16> bound{}, unresolved{};
  std::array<std::uint8_t, 16> bytes{}, saved{};
  std::uint64_t base = page_size - 4, generation = 0;
  bool scope = false, restored = false, fail_store = false;
  bool contains(std::uint64_t at) const noexcept override {
    return at >= base && at - base < bound.size() && bound[at - base];
  }
  std::vector<std::uint64_t> bound_addresses(std::uint64_t first, std::size_t count) const override {
    std::vector<std::uint64_t> result;
    for (std::size_t i = 0; i < bound.size(); ++i)
      if (bound[i] && base + i >= first && base + i - first < count) result.push_back(base + i);
    return result;
  }
  std::uint8_t read(std::uint64_t at) const override {
    if (!contains(at) || unresolved[at - base]) throw std::runtime_error("unresolved cell");
    return bytes[at - base];
  }
  void write(const std::vector<std::pair<std::uint64_t, std::uint8_t>>& input) override {
    for (const auto& item : input) (void)read(item.first);
    if (fail_store) throw std::runtime_error("injected vector budget failure");
    for (const auto& item : input) bytes[item.first - base] = item.second;
    ++generation;
  }
  std::uint64_t revision() const noexcept override { return generation; }
  std::size_t state_bytes() const noexcept override { return bytes.size(); }
  void reset() noexcept override { bound = {}; bytes = {}; unresolved = {}; ++generation; }
  void begin_write_scope() noexcept override { saved = bytes; scope = true; restored = false; }
  void end_write_scope() noexcept override { scope = false; }
  bool restore_write_scope() noexcept override {
    if (!scope) return false;
    if (!restored) { bytes = saved; ++generation; restored = true; }
    return true;
  }
  void bind(std::uint64_t at, std::uint8_t value) {
    bytes[at - base] = value; bound[at - base] = true; ++generation;
  }
  std::uint8_t unbind(std::uint64_t at) {
    const auto value = read(at); bound[at - base] = false; ++generation; return value;
  }
};

void test_capacity_and_rules() {
  constexpr auto capacity = 64ULL << 30;
  Ram ram(capacity, 2 * page_size);
  need(ram.size() == capacity && ram.allocated_bytes() == 0, "64 GiB logical capacity allocated dense RAM");
  const std::uint8_t tail[]{1, 2, 3, 4, 5};
  ram.write_block(capacity - sizeof(tail), tail, sizeof(tail));
  need(ram.read_byte(capacity - 1) == 5 && ram.allocated_pages() == 1, "high-address state storage");
  need(ram.compact_repeating_page(capacity - 1) && ram.allocated_bytes() < 64,
       "basis word rules did not compact the high page");
  ram.fill(0, 0x6a, 128 * page_size);
  need(ram.allocated_bytes() < 2 * page_size && ram.read_byte(127 * page_size + 321) == 0x6a,
       "repeating basis fill materialized every byte");
  ram.fill(0, 0, static_cast<std::size_t>(capacity));
  need(ram.allocated_bytes() == 0 && ram.read_byte(capacity - 1) == 0, "zero rule reset left guest bytes");
  rejects([&] { ram.write_byte(capacity, 1); }, "out-of-range store accepted");
  rejects([&] { ram.read_block(capacity - 1, nullptr, 2); }, "wrapped range accepted");
}

void test_pressure_and_reference() {
  constexpr auto bytes = 12 * page_size;
  Ram ram(bytes, 3 * page_size);
  auto backing = std::make_unique<Backing>();
  auto* host = backing.get();
  ram.attach_page_backing(std::move(backing));
  std::vector<std::uint8_t> reference(bytes), actual(bytes);
  std::mt19937 random(91371);
  for (unsigned step = 0; step < 500; ++step) {
    const auto at = static_cast<std::size_t>(random() % bytes);
    const auto count = std::min<std::size_t>(1 + random() % 7000, bytes - at);
    if (step % 3 == 0) {
      const auto value = static_cast<std::uint8_t>(random());
      ram.fill(at, value, count);
      std::fill_n(reference.begin() + at, count, value);
    } else {
      std::vector<std::uint8_t> data(count);
      for (auto& byte : data) byte = static_cast<std::uint8_t>(random());
      ram.write_block(at, data.data(), data.size());
      std::copy(data.begin(), data.end(), reference.begin() + at);
    }
    if (step % 11 == 0) ram.compact_repeating_pages(4);
    if (step % 17 == 0) {
      ram.read_block(0, actual.data(), actual.size());
      need(actual == reference, "state RAM diverged from dense reference under pressure");
      need(ram.allocated_bytes() <= ram.budget_bytes(), "resident state budget exceeded");
    }
  }
  need(host->writes > 0 && ram.metrics().backing_reads > 0, "pressure fixture never used backing");
  ram.read_block(0, actual.data(), actual.size());
  need(actual == reference, "final pressured memory differs");
  std::array<std::vector<std::uint8_t>, 3> parallel;
  std::array<std::thread, 3> readers;
  for (std::size_t i = 0; i < readers.size(); ++i) {
    parallel[i].resize(bytes);
    readers[i] = std::thread([&, i] { ram.read_block(0, parallel[i].data(), bytes); });
  }
  for (auto& reader : readers) reader.join();
  for (const auto& result : parallel) need(result == reference, "parallel backing read diverged");
  ram.set_backing_read_timing_enabled(true);
  ram.reset_zero();
  need(ram.allocated_bytes() == 0 && host->pages.empty() && ram.pressure_metrics().backing_timing_enabled,
       "reset lost instrumentation configuration or retained backing");
}

void test_failures_pins_and_clean_reclaim() {
  Ram ram(4 * page_size, 2 * page_size);
  auto backing = std::make_unique<Backing>();
  auto* host = backing.get(); ram.attach_page_backing(std::move(backing));
  const auto data = varied(7);
  ram.write_block(0, data.data(), data.size());
  ram.write_block(page_size, data.data(), data.size());
  {
    const auto pins = ram.pin_writes();
    ram.prepare_write(0, 2 * page_size);
    ram.write_byte(0, 91); ram.write_byte(page_size, 92);
    rejects([&] { ram.write_block(2 * page_size, data.data(), data.size()); }, "pinned writes were evicted");
    need(ram.restore_byte(0, data[0]) && ram.restore_byte(page_size, data[0]), "pinned rollback failed");
  }
  host->fail_write = true;
  rejects([&] { ram.write_block(2 * page_size, data.data(), data.size()); }, "backing failure was swallowed");
  need(ram.read_byte(0) == data[0] && ram.read_byte(page_size) == data[0] && ram.read_byte(2 * page_size) == 0,
       "backing failure changed committed bytes");
  const auto failed = ram.prepare_reclaim(2, Ram::ReclaimClock::time_point::max());
  need(failed.backing_failed && ram.allocated_pages() == 2, "failed prewrite damaged authority");
  host->fail_write = false;
  const auto ready = ram.prepare_reclaim(2, Ram::ReclaimClock::time_point::max());
  need(ready.backed_pages == 2 && ram.clean_backing_pages() == 2, "clean reclaim preparation");
  const auto writes = host->writes;
  need(ram.adjust_payload_budget(page_size, 0, Ram::ReclaimClock::time_point::max(), 1) ==
       Ram::BudgetAdjustment::Applied, "clean reclaim did not apply lower budget");
  need(host->writes == writes && ram.read_byte(0) == data[0], "clean reclaim rewrote backing or lost data");
}

void test_compact_budget_and_fill_preflight() {
  Ram edges(4 * page_size, page_size);
  rejects([&] { edges.fill(page_size - 1, 0, page_size + 2); },
          "separate fill edges lost their prepared pins under pressure");
  need(edges.read_byte(page_size - 1) == 0 && edges.read_byte(2 * page_size) == 0,
       "failed fill preflight changed guest bytes");

  Ram ram(20 * page_size, 4 * page_size);
  auto backing = std::make_unique<Backing>(); auto* host = backing.get();
  ram.attach_page_backing(std::move(backing));
  for (unsigned i = 0; i < 20; ++i) {
    const std::uint32_t first = 0x12120000U + i, last = 0xaabbcc00U + i;
    ram.write_block(i * page_size, reinterpret_cast<const std::uint8_t*>(&first), sizeof(first));
    ram.write_block((i + 1) * page_size - sizeof(last),
                    reinterpret_cast<const std::uint8_t*>(&last), sizeof(last));
  }
  constexpr auto compact_bytes = 20 * (8 + 2 * 10);
  need(ram.adjust_payload_budget(compact_bytes, 20) == Ram::BudgetAdjustment::Applied,
       "packed basis deltas did not fit the exact resident budget");
  const auto compact = ram.metrics();
  need(compact.allocated_bytes == compact_bytes && compact.pattern_delta_bytes == compact_bytes &&
       compact.pattern_delta_pages == 20 && host->writes == 0,
       "delta accounting hid padding or unnecessarily used backing");
  need(ram.adjust_payload_budget(0, 20) == Ram::BudgetAdjustment::Applied && host->pages.size() == 20,
       "complete compact states were not transferable to backing");
  for (unsigned i = 0; i < 20; ++i) {
    std::uint32_t first{}, last{};
    ram.read_block(i * page_size, reinterpret_cast<std::uint8_t*>(&first), sizeof(first));
    ram.read_block((i + 1) * page_size - sizeof(last), reinterpret_cast<std::uint8_t*>(&last), sizeof(last));
    need(first == 0x12120000U + i && last == 0xaabbcc00U + i,
         "packed state changed during backing transfer");
  }
}

void test_quantum_routes_and_rollback() {
  Fabric ram(3 * page_size, 3 * page_size);
  need(std::string(ram.backend_name()) == "ether-state-ram", "fabric selected old RAM");
  auto cells = std::make_shared<Cells>(); ram.attach_quantum_authority(cells);
  const auto first = cells->base;
  ram.fill(first - 4, 7, 24);
  for (auto at : {first, first + 7})
    ram.bind_quantum_byte(at, [&](std::uint8_t value) { cells->bind(at, value); });
  rejects([&] { ram.attach_window(first, 1, [](std::uint64_t) { return std::uint8_t{0}; },
                                  [](std::uint64_t, std::uint8_t) {}); },
          "device window concealed an existing quantum RAM cell");
  need(cells->read(first) == 7 && ram.resident_ram_span(first - 1, 2) == nullptr,
       "bind did not transfer authority or bypassed coherent span");
  cells->unresolved[7] = true;
  std::array<std::uint8_t, 16> operand{}; operand.fill(0x55);
  rejects([&] { ram.read_parallel_ram_block(first - 1, operand.data(), operand.size()); },
          "parallel read silently measured a cell");
  need(std::all_of(operand.begin(), operand.end(), [](auto value) { return value == 0x55; }),
       "failed coherent operand exposed partial bytes");
  const auto scan = ram.scan_ram_until_byte(first - 1, 16, 7);
  need(scan.examined == 1 && scan.last == 7, "scan accessed beyond its first matching byte");
  rejects([&] { ram.write_block(first - 1, operand.data(), operand.size()); },
          "mixed write accepted unresolved cell");
  need(ram.read_byte(first - 1) == 7 && cells->read(first) == 7, "mixed write failure changed one authority");
  cells->unresolved[7] = false;
  {
    const auto pins = ram.pin_writes();
    ram.write_byte(first, 41); ram.write_byte(first + 7, 42);
    cells->fail_store = true;
    need(ram.restore_byte(first, 7) && ram.restore_byte(first + 7, 7), "checkpoint rollback allocated a store");
    need(cells->read(first) == 7 && cells->read(first + 7) == 7, "checkpoint failed to restore full authority");
    cells->fail_store = false;
  }
  ram.fill(0, 9, 2 * page_size);
  need(ram.read_byte(first) == 9 && ram.read_byte(first + 7) == 9 && ram.read_byte(10) == 9,
       "full-page fill split quantum and basis values");
  ram.enable_code_page_versions(true);
  need(ram.watch_code_page(first) != 0, "code page version missing");
  ram.watch_page_table(first);
  ram.enable_write_watch(0, 2 * page_size, 2 * page_size); ram.acknowledge_write_watch();
  cells->write({{first, 33}}); // Intentionally omit the explicit notification hook.
  need(ram.code_page_version(first) == 0 && ram.page_table_epoch() == 0 && ram.write_watch()->full(),
       "external quantum revision left stale code, translations or video");
  ram.unbind_quantum_byte(first, [&] { return cells->unbind(first); });
  need(ram.read_byte(first) == 33 && !cells->contains(first), "unbind lost the authoritative value");
  ram.reset_zero();
  need(ram.read_byte(first) == 0 && ram.quantum_state_bytes() == cells->state_bytes(), "reset retained guest state");
}
}  // namespace

int main() {
  try {
    test_capacity_and_rules();
    test_pressure_and_reference();
    test_failures_pins_and_clean_reclaim();
    test_compact_budget_and_fill_preflight();
    test_quantum_routes_and_rollback();
    std::cout << "ETHER_STATE_RAM_PASS checks=" << checks << " logical_bytes=" << (64ULL << 30) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ETHER_STATE_RAM_FAIL check=" << checks << " reason=" << error.what() << '\n';
    return 1;
  }
}
