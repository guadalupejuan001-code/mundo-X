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
  bool grouped = false, paced = false;
  unsigned writes = 0;
  unsigned paced_starts = 0, paced_ends = 0;
  std::vector<std::size_t> group_sizes;
  std::size_t group_limit = kMaxWritePages;
  std::size_t prepare_limit = kMaxWritePages;
  void read(std::uint64_t page, std::uint8_t* bytes) override {
    if (fail_read) throw std::runtime_error("injected read failure");
    std::memcpy(bytes, pages.at(page).data(), page_size);
  }
  void write(std::uint64_t page, const std::uint8_t* bytes) override {
    if (fail_write) throw std::runtime_error("injected write failure");
    std::memcpy(pages[page].data(), bytes, page_size); ++writes;
  }
  bool supports_grouped_writes() const noexcept override { return grouped; }
  void begin_paced_reclaim() noexcept override { paced = true; ++paced_starts; }
  void end_paced_reclaim() noexcept override { paced = false; ++paced_ends; }
  std::size_t write_pages(const PageWrite* input, std::size_t count,
                          Ram::ReclaimClock::time_point deadline) override {
    if (deadline != Ram::ReclaimClock::time_point::max() && !paced)
      throw std::logic_error("pressure group outside paced scope");
    group_sizes.push_back(count);
    if (fail_write) throw std::runtime_error("injected group write failure");
    count = std::min(count, group_limit);
    for (std::size_t i = 0; i < count; ++i) write(input[i].page, input[i].bytes);
    return count;
  }
  std::size_t prepare_pages(const PageWrite* input, std::size_t count,
                            Ram::ReclaimClock::time_point) override {
    if (fail_write) throw std::runtime_error("injected prepare failure");
    count = std::min(count, prepare_limit);
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

void test_periodic_compaction_budget() {
  Ram ram(8 * page_size, 8 * page_size);
  ram.write_byte(0, 0xc1);
  ram.write_byte(page_size, 0x92);
  std::array<std::uint8_t, page_size> contents{};
  ram.write_block(2 * page_size, contents.data(), contents.size());
  contents.fill(0x7b);
  ram.write_block(3 * page_size, contents.data(), contents.size());
  ram.fill(4 * page_size, 0x5f, page_size);
  need(ram.allocated_pages() == 4 && ram.pattern_pages() == 1,
       "full-page fill must remain immediately compact");

  std::size_t visited = 0;
  need(ram.compact_repeating_pages(2, &visited) == 0 && visited == 2,
       "periodic compaction exceeded its visit budget or compressed deltas");
  {
    const auto pins = ram.pin_writes();
    need(ram.compact_repeating_pages(2, &visited) == 0 && visited == 0,
         "periodic compaction ran inside a write journal");
  }
  need(ram.compact_repeating_pages(2, &visited) == 2 && visited == 2 &&
           ram.allocated_pages() == 2 && ram.pattern_pages() == 2,
       "periodic compaction did not resume from the first unvisited slot");
  std::array<std::uint8_t, 3 * page_size> actual{};
  ram.read_block(2 * page_size, actual.data(), actual.size());
  need(std::equal(contents.begin(), contents.end(), actual.begin() + page_size) &&
           actual.front() == 0 && actual.back() == 0x5f,
       "compacting a repeating or all-zero page changed guest contents");
  ram.write_block(0, contents.data(), contents.size());
  need(ram.compact_repeating_pages(2, &visited) == 1 && visited == 2 &&
           ram.read_byte(0) == 0x7b && ram.read_byte(page_size) == 0x92,
       "sweep failed to wrap its cursor and revisit a rewritten page");
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

void test_incremental_reclaim_preparation() {
  constexpr auto compact_prefix = 4 * cosmic_pc::SparseRam::kPagesPerTable;
  constexpr auto first_dense = compact_prefix + 8;
  Ram ram((first_dense + 2) * page_size, 8 * page_size);
  auto backing = std::make_unique<Backing>();
  auto* host = backing.get();
  ram.attach_page_backing(std::move(backing));
  host->prepare_limit = 1;
  ram.fill(0, 0x65, compact_prefix * page_size);
  const auto first = varied(19), second = varied(39);
  ram.write_block(first_dense * page_size, first.data(), first.size());
  ram.write_block((first_dense + 1) * page_size, second.data(), second.size());

  const auto no_deadline = Ram::ReclaimClock::time_point::max();
  const auto skipped = ram.prepare_reclaim(2, no_deadline);
  need(!skipped.dense_pages && !skipped.backed_pages && host->pages.empty(),
       "preparation exceeded its bound when walking compact states");
  const auto partial = ram.prepare_reclaim(2, no_deadline);
  need(partial.dense_pages == 2 && partial.backed_pages == 1 && host->pages.at(first_dense) == first,
       "preparation did not resume at high RAM or honor the committed prefix");
  const auto resumed = ram.prepare_reclaim(2, no_deadline);
  need(resumed.dense_pages == 1 && resumed.backed_pages == 1 && host->pages.at(first_dense + 1) == second &&
           ram.clean_backing_pages() == 2,
       "partial prewrite did not resume with the uncommitted page");
  need(ram.read_byte(0) == 0x65 && ram.read_byte(first_dense * page_size) == first[0] &&
           ram.read_byte((first_dense + 1) * page_size) == second[0],
       "backing preparation changed ordinary RAM");
}

void test_indexed_clean_reclamation() {
  constexpr auto compact_prefix = 4 * cosmic_pc::SparseRam::kPagesPerTable;
  constexpr auto first = compact_prefix + 4;
  Ram ram((first + 10) * page_size, compact_prefix * Ram::kPatternSize + 5 * page_size);
  auto backing = std::make_unique<Backing>();
  auto* host = backing.get();
  ram.attach_page_backing(std::move(backing));
  ram.fill(0, 0x65, compact_prefix * page_size);
  for (unsigned index = 0; index < 5; ++index) {
    const auto bytes = varied(index);
    ram.write_block((first + 2 * index) * page_size, bytes.data(), bytes.size());
  }
  const auto deadline = Ram::ReclaimClock::time_point::max();
  for (unsigned batch = 0; batch < 4 && ram.clean_backing_pages() < 5; ++batch)
    ram.prepare_reclaim(5, deadline);
  need(ram.clean_backing_pages() == 5 && host->writes == 5,
       "clean index must track sparse high pages prepared after a compact prefix");

  constexpr auto pinned = first * page_size;
  constexpr auto excluded = (first + 2) * page_size;
  const std::array<std::uint8_t, 8> crossing{{0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97, 0x98}};
  {
    const auto pins = ram.pin_writes();
    ram.prepare_write(pinned, page_size);
    ram.write_block(excluded + page_size - 4, crossing.data(), crossing.size());
    need(ram.resident_span(pinned, page_size) && ram.resident_span(excluded, page_size),
         "indexed clean victim selection displaced pinned or excluded pages");
  }
  std::array<std::uint8_t, crossing.size()> restored_crossing{};
  ram.read_block(excluded + page_size - 4, restored_crossing.data(), restored_crossing.size());
  const auto metrics = ram.metrics();
  need(restored_crossing == crossing && metrics.backing_clean_pages == 3 &&
           metrics.backing_clean_reclaims == 1 && metrics.spilled_pages == 1 && host->writes == 5,
       "clean index did not reduce forced reclaim without writing or preserve the cross-page store");

  auto expected_excluded = varied(1);
  std::copy_n(crossing.begin(), 4, expected_excluded.end() - 4);
  for (unsigned index = 0; index < 5; ++index) {
    auto expected = index == 1 ? expected_excluded : varied(index);
    Page restored{};
    ram.read_block((first + 2 * index) * page_size, restored.data(), restored.size());
    need(restored == expected, "clean victim selection restored a stale guest value");
  }

  ram.fill(pinned, 0, page_size);
  need(ram.clean_backing_pages() == 2 && !host->pages.count(first),
       "erasing a clean page left its backing or its index entry live");
  const auto target = ram.allocated_bytes() - page_size;
  need(ram.adjust_payload_budget(target, 0, deadline, 1) == Ram::BudgetAdjustment::Applied &&
           ram.clean_backing_pages() == 1 && host->writes == 5,
       "paced maintenance did not reuse one remaining indexed clean page");
  ram.reset_zero();
  need(ram.clean_backing_pages() == 0 && ram.allocated_bytes() == 0 && host->pages.empty(),
       "reset retained indexed clean pages or backing copies");

  constexpr std::uint64_t pattern = 0x2222222222222222ULL;
  need(ram.try_fill_repeating_page(pinned, pattern) &&
           ram.adjust_payload_budget(0, 1) == Ram::BudgetAdjustment::Applied &&
           ram.adjust_payload_budget(2 * page_size, 0) == Ram::BudgetAdjustment::Applied,
       "could not create a backed compact page for promotion");
  ram.prepare_write(pinned, page_size);
  need(ram.clean_backing_pages() == 1 && ram.compact_exact_repeating_page(pinned) &&
           ram.clean_backing_pages() == 0 && !host->pages.count(first) && ram.read_byte(pinned) == 0x22,
       "compacting a promoted clean page retained stale indexing or altered data");
}

void test_staged_group_reclaim() {
  Ram ram(32 * page_size, 16 * page_size);
  auto backing = std::make_unique<Backing>();
  auto* host = backing.get();
  host->grouped = true;
  host->group_limit = 2;
  ram.attach_page_backing(std::move(backing));
  for (unsigned page = 0; page < 8; ++page) {
    const auto bytes = varied(page);
    ram.write_block(page * page_size, bytes.data(), bytes.size());
  }
  const auto prepare = ram.prepare_reclaim(1, Ram::ReclaimClock::time_point::max());
  need(prepare.backed_pages == 1 && host->writes == 1, "clean reclaim fixture failed to prewrite a page");
  const auto deadline = [] { return Ram::ReclaimClock::now() + std::chrono::seconds(1); };
  const auto first = ram.adjust_payload_budget(0, 4, deadline(), 1);
  need(first == Ram::BudgetAdjustment::Reclaiming && host->group_sizes == std::vector<std::size_t>{4} &&
           host->writes == 3 && ram.spilled_pages() == 3 && ram.clean_backing_pages() == 0,
       "paced reclaim did not release clean backing and the dirty group's committed prefix");
  need(ram.allocated_bytes() == 5 * page_size && ram.budget_bytes() == 7 * page_size &&
           host->paced_starts == 1 && host->paced_ends == 1 && !host->paced,
       "partial progress did not restrict paused admission or close the paced scope");

  host->fail_write = true;
  bool reported = false;
  try { (void)ram.adjust_payload_budget(0, 4, deadline()); }
  catch (const std::runtime_error& error) { reported = std::string(error.what()) == "injected group write failure"; }
  need(reported && ram.allocated_bytes() == 5 * page_size && ram.spilled_pages() == 3 &&
           host->paced_starts == 2 && host->paced_ends == 2 && !host->paced,
       "failed pressure write hid its error, dropped resident data or leaked the paced scope");
  host->fail_write = false;
  host->group_limit = Backing::kMaxWritePages;
  need(ram.adjust_payload_budget(0, 8, deadline()) == Ram::BudgetAdjustment::Applied &&
           ram.allocated_bytes() == 0 && ram.budget_bytes() == 0 && ram.spilled_pages() == 8,
       "staged pressure did not resume and publish the exact final cap");
  for (unsigned page = 0; page < 8; ++page) {
    Page restored{};
    ram.read_block(page * page_size, restored.data(), restored.size());
    need(restored == varied(page), "pressure writes changed guest bytes");
  }
}

void test_compact_budget_and_fill_preflight() {
  Ram edges(4 * page_size, page_size);
  rejects([&] { edges.fill(page_size - 1, 0x5a, page_size + 2); },
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

void test_implicit_zero_fill_inside_journal() {
  constexpr std::uint64_t image_bytes = 7680ULL * 4320 * 4;
  Ram ram(2 * image_bytes, 2 * page_size);
  auto backing = std::make_unique<Backing>(); ram.attach_page_backing(std::move(backing));
  ram.write_byte(0, 41);
  {
    const auto pins = ram.pin_writes();
    ram.prepare_write(0, 1);
    const auto before = ram.allocated_bytes();
    ram.fill(0, 0, image_bytes);
    need(ram.allocated_bytes() == before && ram.read_byte(image_bytes - 1) == 0,
         "pinned zero fill materialized the untouched 8K image");
    need(ram.restore_byte(0, 41) && ram.read_byte(0) == 41,
         "implicit zero fill released a journal's resident rollback page");
    ram.prepare_write(page_size, 1);
    bool contextual = false;
    try { ram.write_byte(2 * page_size, 99); }
    catch (const Ram::BudgetExceeded& error) {
      contextual = std::string(error.what()).find("budget exceeded") != std::string::npos &&
          std::string(error.what()).find("unpinned") != std::string::npos;
    }
    need(contextual, "budget failure lost its recognizable context or pin cause");
  }
}

void test_full_overwrite_and_lazy_shared_cache() {
  constexpr std::size_t rows = 96, rounds = 12;
  constexpr std::uint64_t mib = 1024 * 1024;
  for (auto policy : {Ram::BackingCachePolicy::Fixed, Ram::BackingCachePolicy::Adaptive}) {
    Ram ram(128 * page_size, 64 * mib);
    auto backing = std::make_unique<Backing>(); auto* host = backing.get();
    ram.attach_page_backing(std::move(backing), policy);
    need(ram.backing_cache_bytes() == 0, "cache eagerly allocated unused decoded payload");
    for (unsigned row = 0; row < rows; ++row) {
      const auto data = varied(row);
      ram.write_block(row * page_size, data.data(), data.size());
    }
    need(ram.adjust_payload_budget(0, rows) == Ram::BudgetAdjustment::Applied &&
         ram.adjust_payload_budget(64 * mib, 0) == Ram::BudgetAdjustment::Applied,
         "shared-cache fixture did not spill and restore the resident budget");
    Page result;
    for (unsigned round = 0; round < rounds; ++round)
      for (unsigned pass = 0; pass < 3; ++pass)
        for (unsigned row = 0; row < rows; ++row) {
          ram.read_block(row * page_size, result.data(), pass == 1 ? page_size : 4);
          need(result[0] == varied(row)[0], "shared-cache read changed guest bytes");
        }
    const auto before = ram.metrics();
    if (policy == Ram::BackingCachePolicy::Adaptive) {
      need(before.backing_reads == rows && before.backing_cache_hits == 3 * rows * rounds - rows &&
           before.backing_cache_bytes == rows * page_size &&
           before.backing_cache_capacity_bytes == 128 * page_size,
           "adaptive cache did not distinguish used payload from reserved capacity");
      need(ram.adjust_payload_budget(16 * mib, 0) == Ram::BudgetAdjustment::Applied &&
           ram.backing_cache_bytes() > 0 && ram.backing_cache_bytes() <= 128 * 1024 &&
           ram.metrics().backing_cache_capacity_bytes == 128 * 1024,
           "cache shrink did not retain valid pages within its reserved capacity");
    } else need(before.backing_reads == 3 * rows * rounds, "fixed cache did not exercise misses");
    auto replacement = varied(129);
    host->fail_read = true;
    ram.write_block(0, replacement.data(), replacement.size());
    host->fail_read = false;
    need(ram.metrics().backing_reads == before.backing_reads &&
         ram.metrics().backing_overwrite_pages == 1 && !host->pages.count(0),
         "full overwrite recovered or retained the obsolete backed page");
    ram.read_block(0, result.data(), result.size());
    need(result == replacement, "full overwrite did not publish its replacement");
  }

  // Recovering a partial suffix can fail after a complete prefix is staged.
  // The complete old page must stay authoritative until all preflight succeeds.
  Ram ram(3 * page_size, 3 * page_size);
  auto backing = std::make_unique<Backing>(); auto* host = backing.get();
  ram.attach_page_backing(std::move(backing));
  const auto first = varied(1), second = varied(2);
  ram.write_block(0, first.data(), first.size());
  ram.write_block(page_size, second.data(), second.size());
  need(ram.adjust_payload_budget(0, 2) == Ram::BudgetAdjustment::Applied &&
       ram.adjust_payload_budget(3 * page_size, 0) == Ram::BudgetAdjustment::Applied,
       "overwrite failure fixture");
  std::vector<std::uint8_t> replacement(2 * page_size - 1, 0xa7);
  host->fail_read = true;
  rejects([&] { ram.write_block(0, replacement.data(), replacement.size()); },
          "partial recovery failure did not reject a mixed overwrite");
  host->fail_read = false;
  Page recovered;
  ram.read_block(0, recovered.data(), recovered.size());
  need(recovered == first && ram.metrics().backing_overwrite_pages == 0 && host->pages.count(0),
       "failed preflight discarded or published its full-page candidate");
  ram.read_block(page_size, recovered.data(), recovered.size());
  need(recovered == second, "failed preflight altered its partial suffix");

  // Quantum rejection must retain the complete old page even when the caller
  // offers a full replacement and requests no classical undo operand.
  Ram coherent(2 * page_size, 2 * page_size);
  auto quantum_backing = std::make_unique<Backing>(); auto* quantum_host = quantum_backing.get();
  coherent.attach_page_backing(std::move(quantum_backing));
  auto cells = std::make_shared<Cells>(); coherent.attach_quantum_authority(cells);
  coherent.write_block(0, first.data(), first.size());
  coherent.bind_quantum_byte(cells->base, [&](std::uint8_t value) { cells->bind(cells->base, value); });
  need(coherent.adjust_payload_budget(0, 2) == Ram::BudgetAdjustment::Applied &&
       coherent.adjust_payload_budget(2 * page_size, 0) == Ram::BudgetAdjustment::Applied,
       "coherent full overwrite fixture");
  cells->fail_store = true;
  rejects([&] { coherent.write_block(0, second.data(), second.size()); },
          "full replacement bypassed quantum rejection");
  cells->fail_store = false;
  coherent.read_block(0, recovered.data(), recovered.size());
  need(recovered == first && coherent.metrics().backing_overwrite_pages == 0 && quantum_host->pages.count(0),
       "quantum failure discarded the previous full-page authority");
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
    test_periodic_compaction_budget();
    test_pressure_and_reference();
    test_failures_pins_and_clean_reclaim();
    test_incremental_reclaim_preparation();
    test_indexed_clean_reclamation();
    test_staged_group_reclaim();
    test_compact_budget_and_fill_preflight();
    test_implicit_zero_fill_inside_journal();
    test_full_overwrite_and_lazy_shared_cache();
    test_quantum_routes_and_rollback();
    std::cout << "ETHER_STATE_RAM_PASS checks=" << checks << " logical_bytes=" << (64ULL << 30) << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ETHER_STATE_RAM_FAIL check=" << checks << " reason=" << error.what() << '\n';
    return 1;
  }
}
