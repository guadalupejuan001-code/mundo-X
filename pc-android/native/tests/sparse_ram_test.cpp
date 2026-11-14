#include "../sparse_ram.hpp"
#include "../memory_fabric.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {
using cosmic_pc::SparseRam;
constexpr std::uint64_t kTwoGiB = std::uint64_t{2} << 30;
constexpr std::size_t kPage = SparseRam::kPageSize;
unsigned checks = 0;

void need(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

template <class Error, class Operation>
void expect(Operation operation, const std::string& text) {
    ++checks;
    try {
        operation();
    } catch (const Error& error) {
        if (std::string(error.what()).find(text) == std::string::npos) {
            throw std::runtime_error("Wrong exception message: " + std::string(error.what()));
        }
        return;
    }
    throw std::runtime_error("Expected exception containing " + text);
}

std::vector<std::uint8_t> read(const SparseRam& ram, std::uint64_t offset, std::size_t count) {
    std::vector<std::uint8_t> result(count);
    ram.read_block(offset, result.data(), result.size());
    return result;
}

struct TestBacking : SparseRam::PageBacking {
    using Page = std::array<std::uint8_t, kPage>;
    std::unordered_map<std::uint64_t, Page> pages;
    bool fail_read = false, fail_write = false;
    unsigned resets = 0;

    void read(std::uint64_t page, std::uint8_t* bytes) override {
        if (fail_read) throw std::runtime_error("test backing read I/O error");
        const auto& stored = pages.at(page);
        std::copy(stored.begin(), stored.end(), bytes);
    }
    void write(std::uint64_t page, const std::uint8_t* bytes) override {
        if (fail_write) throw std::runtime_error("test backing write I/O error");
        std::copy_n(bytes, kPage, pages[page].begin());
    }
    // Deliberately retain old bytes. RAM reset must discard the old mapping.
    void reset() noexcept override { ++resets; }
};
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"bounded backing preserves dense pages and cache state", [] {
             SparseRam ram(17 * kPage, 2 * kPage);
             auto backing = std::make_unique<TestBacking>();
             const auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (std::size_t page = 0; page < 12; ++page) {
                 ram[page * kPage] = static_cast<std::uint8_t>(page + 1);
                 ram[page * kPage + kPage - 1] = static_cast<std::uint8_t>(page + 15);
             }
             const auto metrics = ram.metrics();
             need(metrics.allocated_pages == 2 && metrics.allocated_bytes == 2 * kPage &&
                      metrics.spilled_pages == 10 && metrics.backing_bytes == 10 * kPage &&
                      metrics.backing_writes == 10 && metrics.backing_reads == 0,
                  "Backing did not preserve a bounded resident working set");
             for (std::size_t page = 0; page < 12; ++page)
                 need(ram[page * kPage] == page + 1 && ram[page * kPage + kPage - 1] == page + 15,
                      "Read-through cache changed persisted guest bytes");
             need(ram.metrics().backing_reads == 10 && ram.allocated_bytes() == 2 * kPage,
                  "Multiple reads of a page should use its bounded cache");
             ram[0] = 0xa2;
             ram[12 * kPage] = 0x99;
             need(ram[0] == 0xa2 && ram[kPage - 1] == 15 && ram[12 * kPage] == 0x99,
                  "Dirty page promotion corrupted untouched or modified bytes");
             ram.fill(kPage + 1, 0, 2 * kPage);
             need(ram[kPage] == 2 && ram[2 * kPage - 1] == 0 &&
                      ram[2 * kPage] == 0 && ram[3 * kPage - 1] == 0 &&
                      ram[3 * kPage] == 0 && ram[4 * kPage - 1] == 18,
                  "Partial or full zero fill corrupted backed pages");
             const auto moved_metrics = ram.metrics();
             SparseRam moved(std::move(ram));
             need(!ram.has_page_backing() && moved.has_page_backing() &&
                      moved.metrics().spilled_pages == moved_metrics.spilled_pages &&
                      moved[0] == 0xa2, "Moving RAM lost its live backing");
             moved.reset_zero();
             need(store->resets == 1 && moved[0] == 0 && moved[kPage] == 0 &&
                      moved.metrics().backing_writes == 0 && moved.metrics().spilled_pages == 0,
                  "RAM reset exposed a page from an old boot");
         }},
        {"backing I/O failures preserve resident and spilled bytes", [] {
             SparseRam ram(5 * kPage, 2 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             ram[0] = 1;
             ram[8] = 4;
             ram[kPage] = 2;
             ram[kPage + 8] = 5;
             store->fail_write = true;
             const std::array<std::uint8_t, 2> patch{0xe1, 0xe2};
             expect<std::runtime_error>([&] { ram.write_block(2 * kPage - 1, patch.data(), patch.size()); },
                                        "backing write I/O error");
             need(ram[0] == 1 && ram[kPage] == 2 && ram[2 * kPage - 1] == 0 &&
                      ram[2 * kPage] == 0 && ram.spilled_pages() == 0,
                  "Failed eviction committed part of the intended block write");
             store->fail_write = false;
             ram[2 * kPage] = 3;
             const auto spilled = store->pages.begin()->first;
             need(ram.spilled_pages() == 1, "Fixture failed to exercise spill");
             store->fail_read = true;
             expect<std::runtime_error>([&] { ram[spilled * kPage + 1] = 0xc4; },
                                        "backing read I/O error");
             store->fail_read = false;
             need(ram[spilled * kPage] == spilled + 1 && ram[spilled * kPage + 1] == 0 &&
                      ram[2 * kPage] == 3 && ram.allocated_bytes() <= ram.budget_bytes(),
                  "A failed reload changed a guest byte or exceeded its resident budget");
         }},
        {"zero background retains one exact word and copies it independently", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(4 * kPage, 4 * kPage);
             const std::array<std::uint8_t, 8> word{0, 0x3a, 0x11, 0x6b, 0, 0, 0x95, 0xe3};
             ram.write_block(120, word.data(), word.size());
             const auto oracle = read(ram, 0, kPage);
             const auto before = ram.allocated_bytes() + ram.metadata_bytes();
             need(ram.adjust_payload_budget(2 * word.size(), 1) == Adjustment::Applied,
                  "A cold sparse word must fit an eight-byte representation without a spill file");
             need(ram.metrics().sparse_word_pages == 1 && ram.allocated_pages() == 0 &&
                      ram.allocated_bytes() == word.size() &&
                      before - ram.metadata_bytes() - ram.allocated_bytes() == kPage,
                  "One-word representation must reuse the existing slot padding and payload");
             need(ram.try_copy_compact_page(0, kPage) &&
                      read(ram, 0, kPage) == oracle && read(ram, kPage, kPage) == oracle,
                  "Compact block copy must preserve the whole zero background and exception");
             need(ram[121] == word[1] && ram[127] == word[7] && ram[128] == 0 &&
                      ram[kPage + 121] == word[1], "Compact byte read differs from block read");
             expect<SparseRam::BudgetExceeded>([&] { ram[kPage + 121] = 0x4a; }, "budget exceeded");
             need(ram.adjust_payload_budget(2 * kPage, 1) == Adjustment::Applied,
                  "A larger host budget must be immediately usable");
             ram[kPage + 121] = 0x4a;
             need(ram[kPage + 121] == 0x4a && ram[121] == word[1] &&
                      ram.restore_byte(kPage + 121, word[1]) && read(ram, kPage, kPage) == oracle,
                  "Dense transaction and rollback must preserve independent compact pages");
             ram.fill(0, 0, kPage);
             need(ram.metrics().sparse_word_pages == 0 && ram[121] == 0,
                  "Full zero fill must discard the sparse exception");
             std::cout << "PRIME_C04_SPARSE_WORD_PASS dense_bytes=" << kPage
                       << " compact_payload_bytes=" << word.size() << " spill_writes=0\n";
         }},
        {"live budget adjusts incrementally and preserves spilled and failed writes", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(6 * kPage, 5 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (unsigned page = 0; page < 5; ++page) {
                 ram[page * kPage] = page + 1;
                 ram[page * kPage + kPage - 1] = page + 31;
             }
             store->fail_write = true;
             expect<std::runtime_error>([&] { ram.adjust_payload_budget(2 * kPage, 1); },
                                        "backing write I/O error");
             need(ram.budget_bytes() == 5 * kPage && ram.metrics().backing_writes == 0,
                  "Failed budget change advertised released memory");
             store->fail_write = false;
             {
                 const auto pins = ram.pin_writes();
                 need(ram.adjust_payload_budget(2 * kPage, 1) == Adjustment::Blocked,
                      "Live journal must block budget maintenance");
             }
             need(ram.adjust_payload_budget(2 * kPage, 1) == Adjustment::Reclaiming &&
                      ram.metrics().backing_writes == 1 && ram.budget_bytes() >= ram.allocated_bytes(),
                  "Host maintenance must reclaim at most the requested number of pages");
             need(ram.adjust_payload_budget(2 * kPage, 2) == Adjustment::Applied &&
                      ram.budget_bytes() == 2 * kPage && ram.metrics().backing_writes == 3,
                  "Incremental reclaims must reach the requested budget");
             for (unsigned page = 0; page < 5; ++page)
                 need(ram[page * kPage] == page + 1 && ram[page * kPage + kPage - 1] == page + 31,
                      "Budget adjustment changed guest bytes");
         }},
        {"full dense write may spill a compact page while preserving its exact bytes", [] {
             SparseRam ram(4 * kPage, 2 * kPage);
             ram.attach_page_backing(std::make_unique<TestBacking>());
             ram[9] = 0x61;
             need(ram.adjust_payload_budget(kPage, 1) == SparseRam::BudgetAdjustment::Applied &&
                      ram.adjust_payload_budget(kPage - 1, 1) == SparseRam::BudgetAdjustment::Applied &&
                      ram.metrics().sparse_word_pages == 1, "Fixture must retain a single compact word");
             ram.adjust_payload_budget(2 * kPage, 1);
             const std::vector<std::uint8_t> input(2 * kPage, 0xb3);
             ram.write_block(kPage, input.data(), input.size());
             need(ram.metrics().spilled_pages == 1 && ram.metrics().sparse_word_pages == 0 &&
                      ram[8] == 0 && ram[9] == 0x61 && ram[10] == 0 &&
                      read(ram, kPage, input.size()) == input,
                  "Compact eviction failed to make room for a dense transaction");
         }},
        {"transaction pins preserve cross-page rollback at capacity", [] {
             SparseRam ram(6 * kPage, 2 * kPage);
             ram.attach_page_backing(std::make_unique<TestBacking>());
             ram[0] = 7;
             ram[kPage - 1] = 8;
             ram[kPage] = 9;
             {
                 const auto pins = ram.pin_writes();
                 ram.prepare_write(kPage - 1, 2);
                 ram[kPage - 1] = 10;
                 ram[kPage] = 11;
                 {
                     const auto nested = ram.pin_writes();
                     expect<SparseRam::BudgetExceeded>([&] { ram[2 * kPage] = 12; }, "budget exceeded");
                 }
                 need(ram.restore_byte(kPage, 9) && ram.restore_byte(kPage - 1, 8) &&
                          ram[0] == 7 && ram[2 * kPage] == 0 && ram.spilled_pages() == 0,
                      "Failed write displaced a live transaction's rollback bytes");
             }
             ram[2 * kPage] = 12;
             need(ram.spilled_pages() == 1 && ram[kPage - 1] == 8 && ram[kPage] == 9 &&
                      ram[2 * kPage] == 12, "Pin lifetime outlasted the transaction");
         }},
        {"mixed Ether writes preflight both classical sides with spill", [] {
             cosmic_pc::MemoryFabric fabric(5 * kPage, kPage);
             fabric.attach_page_backing(std::make_unique<TestBacking>());
             std::uint8_t ether = 0x81;
             fabric.attach_window(kPage - 1, 1, [&](auto) { return ether; },
                                  [&](auto, auto value) { ether = value; });
             fabric[kPage - 2] = 0x45;
             const std::array<std::uint8_t, 3> patch{4, 5, 6};
             expect<SparseRam::BudgetExceeded>([&] {
                 fabric.write_block(kPage - 2, patch.data(), patch.size());
             }, "budget exceeded");
             expect<SparseRam::BudgetExceeded>([&] {
                 fabric.fill(kPage - 2, 0x99, patch.size());
             }, "budget exceeded");
             need(fabric[kPage - 2] == 0x45 && ether == 0x81 && fabric[kPage] == 0,
                  "Mixed Ether write updated a prefix before all RAM pages were prepared");
         }},
        {"2 GiB logical zero reads allocate no pages", [] {
             SparseRam ram(kTwoGiB);
             const auto metadata = ram.metadata_bytes();
             const SparseRam& constant = ram;
             for (const std::uint64_t at : {std::uint64_t{0}, std::uint64_t{kPage - 1},
                                           std::uint64_t{kPage}, kTwoGiB / 2, kTwoGiB - 1}) {
                 need(ram[at] == 0 && constant.at(at) == 0, "Absent RAM must read zero");
             }
             const auto tail = read(ram, kTwoGiB - 8192, 8192);
             need(std::all_of(tail.begin(), tail.end(), [](std::uint8_t v) { return v == 0; }),
                  "Zero block read differs from zero byte reads");
             need(ram.size() == kTwoGiB && ram.allocated_pages() == 0 && ram.allocated_bytes() == 0,
                  "Zero read allocated payload");
             need(ram.metadata_bytes() == metadata && metadata < 16384,
                  "Logical 2 GiB should need only a small page-table root");
             need(ram.budget_bytes() == (std::uint64_t{1} << 30), "Wrong default payload cap");
         }},
        {"high physical addresses remain distinct", [] {
             SparseRam ram(kTwoGiB, 2 * kPage);
             ram[0] = 0x31;
             ram[kTwoGiB - 1] = 0xd4;
             need(ram[0] == 0x31 && ram[kTwoGiB - 1] == 0xd4 && ram[0x7ffff000] == 0,
                  "High address truncated or aliased");
             need(ram.allocated_pages() == 2 && ram.allocated_bytes() == 2 * kPage,
                  "Byte writes must allocate exactly their pages");
         }},
        {"proxy assignment copies values", [] {
             SparseRam ram(3 * kPage);
             ram[0] = 0x35;
             ram[kPage] = ram[0];
             auto target = ram[2 * kPage];
             target = ram[kPage];
             ram[0] = 0xab;
             need(ram[kPage] == 0x35 && target == 0x35, "Proxy assignment copied its address");
             target = target;
             target ^= 0xff;
             need(ram[2 * kPage] == 0xca, "Proxy self-assignment or compound assignment failed");
             need(ram[0] == 0xab && ram.allocated_pages() == 3, "Proxy wrote the wrong owner slot");
         }},
        {"strict bounds include UINT64_MAX and empty RAM", [] {
             SparseRam ram(kTwoGiB);
             expect<std::out_of_range>([&] { (void)ram.at(kTwoGiB); }, "outside logical RAM");
             expect<std::out_of_range>([&] { ram[std::numeric_limits<std::uint64_t>::max()] = 1; },
                                       "outside logical RAM");
             expect<std::out_of_range>([&] { (void)static_cast<const SparseRam&>(ram)[kTwoGiB]; },
                                       "outside logical RAM");
             SparseRam empty(0);
             need(empty.empty() && empty.budget_bytes() == 0, "Empty RAM metadata incorrect");
             expect<std::out_of_range>([&] { (void)empty[0]; }, "outside logical RAM");
             empty.read_block(0, nullptr, 0);
             empty.write_block(0, nullptr, 0);
             need(empty.allocated_pages() == 0, "Empty operation allocated a page");
         }},
        {"cross-page blocks preserve exact bytes", [] {
             SparseRam ram(4 * kPage + 13);
             std::vector<std::uint8_t> payload(kPage + 31);
             for (std::size_t i = 0; i < payload.size(); ++i) {
                 payload[i] = static_cast<std::uint8_t>(i * 37 + 9);
             }
             ram.write_block(kPage - 7, payload.data(), payload.size());
             need(read(ram, kPage - 7, payload.size()) == payload, "Cross-page block was corrupted");
             need(ram[kPage - 8] == 0 && ram[2 * kPage + 24] == 0,
                  "Block write changed neighboring bytes");
             need(ram.allocated_pages() == 3, "Cross-page write backed an incorrect page count");
         }},
        {"payload cap rejects new backing explicitly", [] {
             SparseRam ram(kTwoGiB, kPage);
             ram[0] = 0x61;
             const auto metadata = ram.metadata_bytes();
             expect<SparseRam::BudgetExceeded>([&] { ram[kTwoGiB - 1] = 0x62; }, "budget exceeded");
             need(ram[0] == 0x61 && ram[kTwoGiB - 1] == 0 && ram.allocated_pages() == 1,
                  "Failed capped write changed RAM");
             need(ram.metadata_bytes() == metadata, "Failed preflight allocated a page table");
             ram[kTwoGiB - 1] = 0;
             need(ram.allocated_pages() == 1, "Writing an absent zero byte consumed the budget");
         }},
        {"failed block write preserves bytes and allocation state", [] {
             SparseRam ram(4 * kPage, 2 * kPage);
             ram[0] = 0x5a;
             const auto before = read(ram, 0, 4 * kPage);
             const auto metadata = ram.metadata_bytes();
             std::vector<std::uint8_t> payload(3 * kPage, 0x9b);
             expect<SparseRam::BudgetExceeded>([&] {
                 ram.write_block(0, payload.data(), payload.size());
             }, "budget exceeded");
             need(read(ram, 0, 4 * kPage) == before, "Capped block write committed partial bytes");
             need(ram.allocated_pages() == 1 && ram.metadata_bytes() == metadata,
                  "Capped block preflight partially committed backing");
         }},
        {"invalid block ranges preserve both source and destination state", [] {
             SparseRam ram(kPage);
             std::array<std::uint8_t, 16> bytes{};
             bytes.fill(0x77);
             expect<std::out_of_range>([&] { ram.read_block(kPage - 8, bytes.data(), bytes.size()); },
                                       "outside logical RAM");
             need(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t v) { return v == 0x77; }),
                  "Out-of-range read partially changed the caller buffer");
             expect<std::out_of_range>([&] { ram.write_block(kPage - 8, bytes.data(), bytes.size()); },
                                       "outside logical RAM");
             expect<std::out_of_range>([&] {
                 ram.prepare_write(std::numeric_limits<std::uint64_t>::max() - 3, 8);
             }, "outside logical RAM");
             expect<std::invalid_argument>([&] { ram.read_block(0, nullptr, 1); }, "null read");
             expect<std::invalid_argument>([&] { ram.write_block(0, nullptr, 1); }, "null write");
             need(ram.allocated_pages() == 0, "Invalid range or pointer allocated backing");
         }},
        {"prepare_write covers a page-table boundary atomically", [] {
             SparseRam ram(4 * 1024 * 1024, 2 * kPage);
             const std::uint64_t boundary = kPage * SparseRam::kPagesPerTable;
             ram.prepare_write(boundary - 1, 2);
             need(ram.allocated_pages() == 2, "Preflight failed across a table boundary");
             const auto metadata = ram.metadata_bytes();
             ram.prepare_write(boundary - 1, 2);
             need(ram.allocated_pages() == 2 && ram.metadata_bytes() == metadata,
                  "Repeated preflight changed backing");
             expect<SparseRam::BudgetExceeded>([&] { ram.prepare_write(0, 1); }, "budget exceeded");
             need(ram.allocated_pages() == 2 && ram.metadata_bytes() == metadata,
                  "Failed preflight partially installed a table");
         }},
        {"rollback needs no allocation after writing zeros", [] {
             SparseRam ram(3 * kPage, 2 * kPage);
             ram[0] = 0x42;
             const std::uint8_t old = ram[0];
             ram[0] = 0;
             need(ram.allocated_pages() == 1, "Writing zero released a transaction's backing");
             ram[kPage] = 0x99; // Consume all remaining budget before rollback.
             const auto metadata = ram.metadata_bytes();
             need(ram.restore_byte(0, old) && ram[0] == 0x42,
                  "Rollback could not restore a byte at a full budget");
             need(ram.restore_byte(2 * kPage, 0), "Absent zero rollback should be a no-op");
             need(!ram.restore_byte(2 * kPage, 1) && !ram.restore_byte(ram.size(), 0),
                  "Invalid rollback invariant was not reported");
             need(ram.allocated_pages() == 2 && ram.metadata_bytes() == metadata,
                  "Rollback allocated or released backing");
         }},
        {"reset and clear discard backing but preserve logical RAM", [] {
             SparseRam ram(kTwoGiB, 3 * kPage);
             const auto initial_metadata = ram.metadata_bytes();
             ram[1] = 0x17;
             ram[kTwoGiB - 1] = 0x29;
             ram.reset_zero();
             need(ram.size() == kTwoGiB && ram.budget_bytes() == 3 * kPage,
                  "Reset changed logical RAM or its cap");
             need(ram.allocated_pages() == 0 && ram.allocated_bytes() == 0 &&
                      ram.metadata_bytes() == initial_metadata,
                  "Reset retained payload or second-level metadata");
             need(ram[1] == 0 && ram[kTwoGiB - 1] == 0, "Reset did not restore implicit zeros");
             ram[kPage] = 0x37;
             ram.clear();
             need(ram.size() == kTwoGiB && ram[kPage] == 0 && ram.allocated_pages() == 0,
                  "clear() did not preserve logical zero RAM");
         }},
        {"explicit trim removes only entirely zero pages", [] {
             SparseRam ram(4 * 1024 * 1024);
             const auto initial_metadata = ram.metadata_bytes();
             ram[0] = 0x55;
             ram[3 * 1024 * 1024] = 0x66;
             ram[3 * 1024 * 1024] = 0;
             need(ram.allocated_pages() == 2, "Write automatically trimmed a page");
             need(ram.trim_zero_pages() == 1 && ram.allocated_pages() == 1 && ram[0] == 0x55,
                  "Explicit trim removed nonzero data or retained a zero page");
             ram[0] = 0;
             need(ram.trim_zero_pages() == 1 && ram.metadata_bytes() == initial_metadata,
                  "Trim did not free the empty second-level table");
         }},
        {"partial last page and budget granularity", [] {
             SparseRam ram(17);
             ram[16] = 0xa3;
             need(ram[16] == 0xa3 && ram.allocated_bytes() == kPage && ram.budget_bytes() == kPage,
                  "Default budget must accommodate one partial logical page");
             expect<std::out_of_range>([&] { ram[17] = 1; }, "outside logical RAM");
             SparseRam capped(17, kPage - 1);
             expect<SparseRam::BudgetExceeded>([&] { capped[0] = 1; }, "budget exceeded");
             need(capped.allocated_pages() == 0, "An allocation exceeded a sub-page explicit cap");
         }},
        {"zero fill stays sparse and nonzero fill preflights", [] {
             SparseRam ram(kTwoGiB, kPage);
             ram.fill(0, 0, static_cast<std::size_t>(ram.size()));
             need(ram.allocated_pages() == 0, "Zero fill materialized implicit pages");
             ram.fill(0, 0x3c, kPage);
             expect<SparseRam::BudgetExceeded>([&] { ram.fill(kPage - 1, 0x77, 2); }, "budget exceeded");
             need(ram[kPage - 1] == 0x3c && ram[kPage] == 0,
                  "Failed nonzero fill changed its resident prefix");
             ram.fill(0, 0, kPage);
             need(ram[0] == 0 && ram.allocated_pages() == 1, "Zero fill released rollback backing");
         }},
        {"reproducible mixed blocks match a dense byte oracle", [] {
             constexpr std::size_t length = 17 * kPage + 37;
             SparseRam ram(length);
             std::vector<std::uint8_t> oracle(length, 0);
             std::uint64_t state = 0x736f6c6172737061ULL;
             auto next = [&] {
                 state = state * 6364136223846793005ULL + 1442695040888963407ULL;
                 return state;
             };
             for (unsigned operation = 0; operation < 256; ++operation) {
                 const std::size_t offset = static_cast<std::size_t>(next() % length);
                 const std::size_t count = std::min<std::size_t>(next() % (2 * kPage + 1), length - offset);
                 std::vector<std::uint8_t> payload(count);
                 for (auto& byte : payload) byte = static_cast<std::uint8_t>(next() >> 24);
                 if (operation % 3 == 0) {
                     ram.fill(offset, 0, count);
                     std::fill_n(oracle.begin() + offset, count, 0);
                 } else {
                     ram.write_block(offset, payload.data(), payload.size());
                     std::copy(payload.begin(), payload.end(), oracle.begin() + offset);
                 }
             }
             need(read(ram, 0, length) == oracle, "Sparse and dense byte semantics differ");
             need(ram.allocated_bytes() <= ram.budget_bytes(), "Mixed operations exceeded the cap");
         }},
        {"repeating nonzero pages retain exact bytes with less backing", [] {
             constexpr std::size_t pages = 64;
             constexpr std::size_t bytes = pages * kPage;
             SparseRam ram(bytes, bytes);
             const std::array<std::uint8_t, SparseRam::kPatternSize> pattern{1, 2, 3, 4, 5, 6, 7, 8};
             std::vector<std::uint8_t> payload(bytes);
             for (std::size_t i = 0; i < payload.size(); ++i) payload[i] = pattern[i % pattern.size()];
             ram.write_block(0, payload.data(), payload.size());
             const auto before = ram.allocated_bytes() + ram.metadata_bytes();
             need(ram.compact_repeating_pages(0) == 0 && ram.allocated_pages() == pages,
                  "Zero compaction budget changed RAM");
             need(ram.compact_repeating_pages(7) == 7 && ram.pattern_pages() == 7,
                  "Bounded compaction ignored the requested page count");
             need(ram.compact_repeating_pages(pages) == pages - 7 && ram.allocated_pages() == 0,
                  "Compaction did not resume at the previous page");
             const auto after = ram.allocated_bytes() + ram.metadata_bytes();
             need(ram.pattern_pages() == pages && ram.allocated_bytes() == pages * pattern.size() &&
                      before - after == bytes,
                  "Repeating page payload or complete storage accounting is wrong");
             need(read(ram, 0, payload.size()) == payload, "Compaction changed guest bytes");
             for (std::size_t offset = 1; offset <= pattern.size(); ++offset) {
                 const auto start = kPage - offset;
                 const auto data = read(ram, start, pattern.size() + offset);
                 need(std::equal(data.begin(), data.end(), payload.begin() + start),
                      "Rotated pattern blocks or cross-page read differ");
             }
             ram[5] = pattern[5];
             need(ram.pattern_pages() == pages, "Unchanged byte eagerly expanded a compact page");
             ram[kPage + 5] = 0x91;
             need(ram[kPage + 5] == 0x91 && ram[5] == pattern[5] && ram[2 * kPage + 5] == pattern[5] &&
                      ram.allocated_pages() == 1 && ram.pattern_pages() == pages - 1,
                  "Divergent write changed another compact guest page");
             need(ram.restore_byte(kPage + 5, pattern[5]) && read(ram, 0, payload.size()) == payload,
                  "Rollback failed to restore expanded repeating bytes");
             need(ram.compact_repeating_pages(pages) == 1, "Restored pattern could not be compacted again");
             const auto before_move = ram.metrics();
             SparseRam moved(std::move(ram));
             need(moved.metrics().pattern_pages == before_move.pattern_pages &&
                      read(moved, 0, payload.size()) == payload && ram.pattern_pages() == 0,
                  "Moving RAM lost compact pages");
             moved.reset_zero();
             need(moved.pattern_pages() == 0 && moved.allocated_bytes() == 0 && moved[2] == 0,
                  "Reset retained compact RAM");
             std::cout << "ETHER_REPEATING_RAM_PASS pages=" << pages
                       << " dense_payload=" << bytes << " pattern_payload=" << pages * pattern.size()
                       << " owned_before=" << before << " owned_after=" << after << '\n';
         }},
        {"expansion past the cap preserves compact bytes and metrics", [] {
             SparseRam ram(4 * kPage, 2 * kPage);
             ram.fill(0, 0x35, kPage);
             need(ram.compact_repeating_pages(4) == 1, "Nonzero uniform page did not compact");
             ram[kPage + 1] = 0x89;
             const auto before = read(ram, 0, static_cast<std::size_t>(ram.size()));
             const auto metadata = ram.metadata_bytes();
             const auto used = ram.allocated_bytes();
             const std::vector<std::uint8_t> replacement(2 * kPage + 1, 0x4b);
             expect<SparseRam::BudgetExceeded>([&] {
                 ram.write_block(0, replacement.data(), replacement.size());
             }, "budget exceeded");
             expect<SparseRam::BudgetExceeded>([&] { ram[kPage * 2] = 0x42; }, "budget exceeded");
             need(read(ram, 0, before.size()) == before && ram.allocated_bytes() == used &&
                      ram.metadata_bytes() == metadata && ram.pattern_pages() == 1,
                  "Failed operation changed the compact prefix or backing counts");
             ram.fill(kPage - 1, 0, 3);
             need(ram[kPage - 2] == 0x35 && ram[kPage - 1] == 0 && ram[kPage + 1] == 0 &&
                      ram.pattern_pages() == 0 && ram.allocated_bytes() == 2 * kPage,
                  "Zero fill did not apply across compact and dense backing");
             need(ram.compact_repeating_pages(4) == 1 && ram.allocated_pages() == 1 && ram.pattern_pages() == 0,
                  "Compaction did not release the wholly zero dense page");
         }},
        {"completed-page compaction permits a long fill under a small cap", [] {
             constexpr std::size_t pages = 16;
             SparseRam ram(pages * kPage, kPage + pages * SparseRam::kPatternSize);
             for (std::size_t page = 0; page < pages; ++page) {
                 ram.fill(page * kPage, 0x63, kPage);
                 need(ram.compact_repeating_page((page + 1) * kPage - 1),
                      "Direct page completion failed to compact at its address");
             }
             need(ram.allocated_pages() == 0 && ram.pattern_pages() == pages &&
                      ram.allocated_bytes() == pages * SparseRam::kPatternSize,
                  "Long fill still retained dense backing for completed repeating pages");
             const auto populated = read(ram, 0, static_cast<std::size_t>(ram.size()));
             need(std::all_of(populated.begin(), populated.end(), [](std::uint8_t c) { return c == 0x63; }),
                  "Bytes changed across directly compacted pages");
             ram.fill(kPage, 0, (pages - 2) * kPage);
             need(ram.pattern_pages() == 2 && ram.allocated_pages() == 0 &&
                      ram[0] == 0x63 && ram[kPage] == 0 && ram[ram.size() - 1] == 0x63,
                  "Clearing complete compact pages should not expand them or change neighbors");
             expect<SparseRam::BudgetExceeded>([&] { ram.fill(1, 0, ram.size() - 2); }, "budget exceeded");
             need(ram.pattern_pages() == 2 && ram[1] == 0x63 && ram[ram.size() - 2] == 0x63,
                  "Failed partial edge expansion changed the remaining compact pages");
             ram.fill(0, 0, static_cast<std::size_t>(ram.size()));
             need(ram.pattern_pages() == 0 && ram.allocated_bytes() == 0,
                  "Zeroing the volume left repeating payload behind");
         }},
        {"compaction keeps Ether-owned byte windows independent", [] {
             cosmic_pc::MemoryFabric memory(2 * kPage, 2 * kPage);
             std::uint8_t ether_byte = 0xba;
             unsigned ether_reads = 0, ether_writes = 0;
             memory.fill(0, 0x27, kPage);
             memory.attach_window(kPage - 1, 1,
                 [&](std::uint64_t) { ++ether_reads; return ether_byte; },
                 [&](std::uint64_t, std::uint8_t value) { ++ether_writes; ether_byte = value; });
             need(memory.compact_repeating_pages(2) == 1 && memory.pattern_pages() == 1 &&
                      ether_reads == 0 && ether_writes == 0,
                  "RAM compaction accessed the independent Ether window");
             std::array<std::uint8_t, 3> bytes{};
             memory.read_block(kPage - 2, bytes.data(), bytes.size());
             need(bytes == std::array<std::uint8_t, 3>{0x27, 0xba, 0} && ether_reads == 1,
                  "Mixed read ignored the compact backing or Ether byte");
             memory.write_block(kPage - 2, bytes.data(), bytes.size());
             need(ether_writes == 1 && ether_byte == 0xba && memory.pattern_pages() == 0 &&
                      memory.allocated_bytes() == 2 * kPage,
                  "Mixed write failed transactional preflight or changed the Ether byte");
         }},
    };

    for (const auto& test : cases) {
        try {
            test.second();
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Sparse RAM: " << cases.size() << "/" << cases.size()
              << " cases passed (" << checks << " assertions).\n";
    return 0;
}
