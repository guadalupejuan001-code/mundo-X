#include "../sparse_ram.hpp"
#include "../memory_fabric.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <functional>
#include <future>
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

void keep_dense(SparseRam& ram, std::uint64_t page) {
    // More changed words than the compact representation can hold.
    ram.fill(page * kPage + 256, 0x58, 512);
}

struct TestBacking : SparseRam::PageBacking {
    using Page = std::array<std::uint8_t, kPage>;
    std::unordered_map<std::uint64_t, Page> pages;
    bool fail_read = false, fail_write = false;
    unsigned resets = 0, discards = 0;

    void read(std::uint64_t page, std::uint8_t* bytes) override {
        if (fail_read) throw std::runtime_error("test backing read I/O error");
        const auto& stored = pages.at(page);
        std::copy(stored.begin(), stored.end(), bytes);
    }
    void write(std::uint64_t page, const std::uint8_t* bytes) override {
        if (fail_write) throw std::runtime_error("test backing write I/O error");
        std::copy_n(bytes, kPage, pages[page].begin());
    }
    void discard(std::uint64_t) noexcept override { ++discards; }
    // Deliberately retain old bytes. RAM reset must discard the old mapping.
    void reset() noexcept override { ++resets; }
};

void parallel_state_vectors() {
    using namespace std::chrono_literals;
    struct GatedBacking : TestBacking {
        explicit GatedBacking(std::future<void> ready) : ready(std::move(ready)) {}
        void read(std::uint64_t page, std::uint8_t* bytes) override {
            started.set_value();
            if (ready.wait_for(10s) != std::future_status::ready)
                throw std::runtime_error("Backing read timed out");
            TestBacking::read(page, bytes);
        }
        std::promise<void> started;
        std::future<void> ready;
    };

    constexpr auto next_vector = SparseRam::kPagesPerTable * kPage;
    SparseRam ram(2 * next_vector, 3 * kPage);
    std::promise<void> release;
    auto backing = std::make_unique<GatedBacking>(release.get_future());
    auto started = backing->started.get_future();
    ram.attach_page_backing(std::move(backing));
    keep_dense(ram, 0);
    ram[0] = 0x41;
    using Adjustment = SparseRam::BudgetAdjustment;
    need(ram.adjust_payload_budget(0, 1) == Adjustment::Applied &&
             ram.adjust_payload_budget(3 * kPage, 0) == Adjustment::Applied,
         "The fixture must leave one page on backing");
    ram[kPage] = 0x52;
    ram[next_vector] = 0x63;
    need(ram.metrics().page_state_vectors == 2, "Both page ranges need a state vector");

    auto cold = std::async(std::launch::async, [&] { return ram.read_byte(0); });
    const bool reading = started.wait_for(2s) == std::future_status::ready;
    auto shared = std::async(std::launch::async, [&] { return ram.read_byte(0); });
    auto resident = std::async(std::launch::async, [&] {
        return ram.read_byte(kPage) == 0x52 && ram.read_byte(next_vector) == 0x63;
    });
    const bool progressed = resident.wait_for(2s) == std::future_status::ready;
    release.set_value();

    need(reading && progressed && resident.get(), "Resident vectors must progress during a cold read");
    need(cold.get() == 0x41 && shared.get() == 0x41, "Both readers must see the same recovered page");
    need(ram.metrics().backing_reads == 1 && ram.metrics().backing_cache_hits == 1,
         "Both reads must share one backing transfer");
    ram.fill(0, 0, kPage);
    need(ram.read_byte(0) == 0 && ram.metrics().page_state_vectors == 2,
         "Discard must invalidate the decoded page while retaining its neighbor");
}
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"state vectors read resident and shared backed pages concurrently", parallel_state_vectors},
        {"byte searches preserve the first match and final byte in sparse RAM", [] {
             constexpr auto table_bytes = kPage * SparseRam::kPagesPerTable;
             SparseRam ram(2 * table_bytes, 16 * kPage);
             const auto base = table_bytes + 2 * kPage;
             ram[base - 1] = 0xe8;
             const std::array<std::uint8_t, 8> pattern{0x80, 0x31, 0xe8, 0x53, 0x54, 0x55, 0x56, 0x57};
             std::uint64_t word;
             std::memcpy(&word, pattern.data(), sizeof(word));
             need(ram.try_fill_repeating_page(base, word), "Failed to create repeating page");
             const auto sparse_match = ram.scan_until_byte(0, base, 0xe8);
             need(sparse_match.examined == base && sparse_match.last == 0xe8,
                  "An implicit table hid a match in the next table");
             const auto first_zero = ram.scan_until_byte(0, table_bytes, 0);
             need(first_zero.examined == 1 && first_zero.last == 0, "Implicit zero is a real match");
             const auto wrapped = ram.scan_until_byte(base + 5, 100, 0xe8);
             need(wrapped.examined == 6 && wrapped.last == 0xe8, "Pattern search lost the offset phase");
             const auto limit = ram.scan_until_byte(base + 5, 4, 0xe8);
             need(limit.examined == 4 && limit.last == pattern[0], "Short search returned a byte outside its budget");
         }},
        {"bounded backing preserves dense pages and cache state", [] {
             SparseRam ram(17 * kPage, 2 * kPage);
             auto backing = std::make_unique<TestBacking>();
             const auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (std::size_t page = 0; page < 12; ++page) {
                 keep_dense(ram, page);
                 ram[page * kPage] = static_cast<std::uint8_t>(page + 1);
                 ram[page * kPage + kPage - 1] = static_cast<std::uint8_t>(page + 15);
             }
             const auto metrics = ram.metrics();
             need(metrics.allocated_pages == 2 && metrics.allocated_bytes == 2 * kPage &&
                      metrics.spilled_pages == 10 && metrics.backing_bytes == 10 * kPage &&
                      metrics.backing_writes == 10 && metrics.backing_reads == 0,
                  "Backing did not preserve a bounded resident working set");
             const auto spilled_match = ram.scan_until_byte(8, 1024, 0x58);
             need(spilled_match.examined == 249 && spilled_match.last == 0x58,
                  "A byte search must see the first match in a spilled page");
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
        {"full overwrites bypass backing reads after successful preflight", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(6 * kPage, 3 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (unsigned page = 0; page < 3; ++page) keep_dense(ram, page);
             need(ram.adjust_payload_budget(0, 3) == Adjustment::Applied && ram.spilled_pages() == 3 &&
                      ram.adjust_payload_budget(3 * kPage, 0) == Adjustment::Applied,
                  "Full-overwrite fixture must leave all three pages in backing");

             const std::vector<std::uint8_t> replacement(2 * kPage, 0xb7);
             std::vector<std::uint8_t> previous(kPage);
             store->fail_read = true;
             expect<std::runtime_error>([&] { ram.write_block(0, replacement.data(), kPage + 1); },
                                        "test backing read I/O error");
             expect<std::runtime_error>([&] { ram.write_block(0, replacement.data(), kPage, previous.data()); },
                                        "test backing read I/O error");
             need(ram.spilled_pages() == 3 && ram.metrics().backing_overwrite_pages == 0,
                  "A partial write or undo capture failure must leave every old page intact");
             ram.write_block(0, replacement.data(), replacement.size());
             ram.fill(2 * kPage, 0x2a, kPage);
             need(ram.metrics().backing_reads == 0 && ram.metrics().backing_overwrite_pages == 3 &&
                      ram.spilled_pages() == 0 && read(ram, 0, replacement.size()) == replacement &&
                      read(ram, 2 * kPage, kPage) == std::vector<std::uint8_t>(kPage, 0x2a),
                  "Full-page writes and fills must replace cold bytes without a backing read");
         }},
        {"clean promoted pages reuse backing and changed pages never expose stale bytes", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(3 * kPage, 3 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             keep_dense(ram, 0);
             keep_dense(ram, 1);
             ram[kPage] = 0x51;
             const auto original = read(ram, 0, 2 * kPage);
             need(ram.adjust_payload_budget(0, 2) == Adjustment::Applied &&
                      ram.adjust_payload_budget(3 * kPage, 0) == Adjustment::Applied,
                  "Clean promotion fixture must start with two stored pages");
             const auto saved = ram.metrics();
             {
                 const auto pins = ram.pin_writes();
                 ram.prepare_write(0, 2 * kPage);
                 ram.write_byte(256, 0x58);
                 const std::uint8_t same[]{0x51, 0};
                 std::uint8_t previous[2]{};
                 ram.write_block(kPage, same, sizeof same, previous);
                 ram.fill(257, 0x58, 12);
                 need(std::equal(std::begin(same), std::end(same), std::begin(previous)) &&
                          ram.metrics().backing_clean_pages == 2 && ram.spilled_pages() == 0,
                      "Write preflight and same-value stores must preserve the exact backing copies");
             }
             store->fail_write = true;
             need(ram.adjust_payload_budget(0, 2) == Adjustment::Applied &&
                      ram.metrics().backing_clean_reclaims == 2 &&
                      ram.metrics().backing_writes == saved.backing_writes && store->discards == 0 &&
                      read(ram, 0, 2 * kPage) == original,
                  "Unmodified pages must reclaim resident memory without storage writes");
             need(ram.adjust_payload_budget(3 * kPage, 0) == Adjustment::Applied,
                  "Clean reuse fixture must make room again");
             ram.prepare_write(0, 2 * kPage);
             const std::uint8_t patch[]{0xb5, 0x14};
             ram.write_block(kPage - 1, patch, sizeof patch);
             need(ram.metrics().backing_clean_pages == 0 && store->discards == 2,
                  "Cross-page mutation must retire every stale copy");
             expect<std::runtime_error>([&] { ram.adjust_payload_budget(0, 2); }, "test backing write I/O error");
             need(ram.spilled_pages() == 0 && ram[kPage - 1] == patch[0] && ram[kPage] == patch[1],
                  "Failed rewrite must leave dirty resident data authoritative");
             store->fail_write = false;
             need(ram.adjust_payload_budget(0, 2) == Adjustment::Applied &&
                      ram.metrics().backing_writes == saved.backing_writes + 2 &&
                      ram[kPage - 1] == patch[0] && ram[kPage] == patch[1],
                  "Successful rewrite must publish the new bytes rather than a stale decoded slot");
         }},
        {"backing capacity retires optional copies before failing a new spill", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             struct BoundedBacking : TestBacking {
                 void write(std::uint64_t page, const std::uint8_t* bytes) override {
                     if (!fail_write && pages.size() == 2 && !pages.count(page))
                         throw CapacityExceeded("test backing capacity");
                     TestBacking::write(page, bytes);
                 }
                 void discard(std::uint64_t page) noexcept override {
                     TestBacking::discard(page);
                     pages.erase(page);
                 }
             };
             SparseRam ram(4 * kPage, 3 * kPage);
             auto backing = std::make_unique<BoundedBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             keep_dense(ram, 0);
             keep_dense(ram, 1);
             need(ram.adjust_payload_budget(0, 2) == Adjustment::Applied &&
                      ram.adjust_payload_budget(3 * kPage, 0) == Adjustment::Applied,
                  "Bounded fixture must start with a full backing store");
             ram.prepare_write(0, 2 * kPage);
             keep_dense(ram, 2);
             store->fail_write = true;
             expect<std::runtime_error>([&] { ram.adjust_payload_budget(2 * kPage, 1); },
                                        "test backing write I/O error");
             need(ram.metrics().backing_clean_pages == 2 && store->discards == 0 &&
                      ram.allocated_pages() == 3,
                  "A general I/O failure must keep optional copies and resident bytes");
             store->fail_write = false;
             need(ram.adjust_payload_budget(2 * kPage, 1) == Adjustment::Applied &&
                      ram.metrics().backing_clean_pages == 0 && store->discards == 2 &&
                      ram.spilled_pages() == 1 && store->pages.count(2) &&
                      ram[256] == 0x58 && ram[kPage + 256] == 0x58 && ram[2 * kPage + 256] == 0x58,
                  "A full store must release optional copies and spill the pending page exactly once");
         }},
        {"paced pressure releases cold backing duplicates before writing dirty pages", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             using Clock = SparseRam::ReclaimClock;
             constexpr std::size_t cold = 8, hot = 9, cursor = 12;
             SparseRam ram(16 * kPage, 6 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             keep_dense(ram, cold);
             keep_dense(ram, hot);
             need(ram.adjust_payload_budget(0, 2) == Adjustment::Applied &&
                      ram.adjust_payload_budget(6 * kPage, 0) == Adjustment::Applied,
                  "Fixture must persist both pages before promoting them");
             ram.prepare_write(cold * kPage, 2 * kPage);
             for (unsigned pass = 0; pass < 3; ++pass) {
                 const auto offset = cursor * kPage;
                 std::array<std::uint8_t, kPage> page{};
                 std::fill_n(page.begin() + 256, 512, 0x58);
                 ram.write_block(offset, page.data(), page.size());
                 need(ram.adjust_payload_budget(2 * kPage, 1) == Adjustment::Applied &&
                          ram.adjust_payload_budget(6 * kPage, 0) == Adjustment::Applied,
                      "Advancing the clock hand must age both clean copies");
                 need(ram.read_byte(offset + 256) == 0x58, "The cursor page must seed the decoded cache");
             }
             need(ram.read_byte(hot * kPage + 256) == 0x58, "The second clean page must remain hot");
             ram.fill(cursor * kPage, 0, kPage);
             keep_dense(ram, 0);
             const auto before = ram.metrics();
             need(before.backing_clean_pages == 2 && before.backing_cache_bytes == 3 * kPage,
                  "Pressure fixture needs clean copies plus an obsolete decoded buffer");
             store->fail_write = true;
             {
                 const auto pins = ram.pin_writes();
                 need(ram.adjust_payload_budget(2 * kPage, 1, Clock::now() + std::chrono::seconds(1)) ==
                          Adjustment::Blocked && ram.metrics().backing_cache_bytes == before.backing_cache_bytes,
                      "A live write journal must prevent any maintenance");
             }
             need(ram.adjust_payload_budget(2 * kPage, 1, Clock::now() + std::chrono::seconds(1)) ==
                      Adjustment::Applied,
                  "Reclaim should meet the target without trying the first dirty page");
             const auto after = ram.metrics();
             need(after.allocated_bytes == 2 * kPage && after.backing_clean_reclaims == 1 &&
                      after.backing_writes == before.backing_writes && after.backing_cache_bytes == 2 * kPage,
                  "Reclaim must release one resident page and its obsolete neighbor's decoded buffer");
             need(!ram.resident_span(cold * kPage, kPage) && ram.resident_span(hot * kPage, kPage) &&
                      ram.resident_span(0, kPage), "Reclaim must preserve the hot and dirty resident pages");
             need(ram.read_byte(cold * kPage + 256) == 0x58 && ram.read_byte(hot * kPage + 256) == 0x58 &&
                      ram.read_byte(256) == 0x58 && ram.read_byte(cursor * kPage + 256) == 0 &&
                      ram.metrics().backing_reads == after.backing_reads,
                  "Valid decoded cache entries and all original guest bytes must stay readable");
         }},
        {"backing I/O failures preserve resident and spilled bytes", [] {
             SparseRam ram(5 * kPage, 2 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             keep_dense(ram, 0);
             keep_dense(ram, 1);
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
             const auto sparse_match = ram.scan_until_byte(kPage, kPage, word[1]);
             need(sparse_match.examined == 122 && sparse_match.last == word[1],
                  "Single-word compact search must see a match above its implicit zeros");
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
        {"cold pages retain exact changes above repeating and zero backgrounds", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(4 * kPage, 4 * kPage);
             const std::array<std::uint8_t, 8> pattern{0x2a, 4, 0, 7, 0xb2, 1, 9, 5};
             std::vector<std::uint8_t> repeated(kPage), mostly_zero(kPage);
             for (std::size_t i = 0; i < kPage; ++i) repeated[i] = pattern[i % pattern.size()];
             repeated[0] = 0;
             repeated[15] = 0xcd;
             repeated[kPage - 1] = 0xfe;
             mostly_zero[8] = 0x18;
             mostly_zero[kPage - 2] = 0x81;
             ram.write_block(0, repeated.data(), repeated.size());
             ram.write_block(kPage, mostly_zero.data(), mostly_zero.size());
             constexpr auto patch_bytes = 5 * (SparseRam::kPatternSize + sizeof(std::uint16_t));
             constexpr auto compact_bytes = 2 * SparseRam::kPatternSize + patch_bytes;
             const auto before = ram.allocated_bytes() + ram.metadata_bytes();
             need(ram.adjust_payload_budget(compact_bytes, 2) == Adjustment::Applied &&
                      ram.allocated_pages() == 0 && ram.allocated_bytes() == compact_bytes &&
                      ram.metrics().pattern_delta_pages == 2 &&
                      ram.metrics().pattern_delta_bytes == compact_bytes &&
                      before - ram.metadata_bytes() - ram.allocated_bytes() == 2 * kPage - patch_bytes,
                  "Both backgrounds must compact without increasing per-page metadata");
             need(read(ram, 0, kPage) == repeated && read(ram, kPage, kPage) == mostly_zero &&
                      ram[0] == repeated[0] && ram[15] == repeated[15] &&
                      ram[kPage + 8] == mostly_zero[8] && ram[2 * kPage - 2] == mostly_zero[kPage - 2],
                  "Compact byte and block readers must reconstruct both backgrounds exactly");
             const auto delta_match = ram.scan_until_byte(3, kPage, 0xcd);
             need(delta_match.examined == 13 && delta_match.last == 0xcd,
                  "Pattern search must honor a changed word before the next page");
             need(read(ram, kPage - 2, 4) == std::vector<std::uint8_t>{repeated[kPage - 2], 0xfe, 0, 0},
                  "An unaligned cross-page read lost the last override");
             ram[15] = repeated[15];
             need(!ram.try_copy_compact_page(kPage, 2 * kPage) && ram[2 * kPage + 8] == 0 &&
                      ram.metrics().pattern_delta_bytes == compact_bytes,
                  "A no-op write or over-budget copy changed the compact representation");
             expect<SparseRam::BudgetExceeded>([&] { ram[15] = 3; }, "budget exceeded");
             need(read(ram, 0, kPage) == repeated, "Failed expansion changed a compact byte");
             ram.adjust_payload_budget(3 * kPage, 0);
             need(ram.try_copy_compact_page(kPage, 2 * kPage) &&
                      ram.try_copy_compact_page(2 * kPage, 2 * kPage),
                  "Compact replacement and self-copy must preserve independent payloads");
             SparseRam moved(0);
             moved = std::move(ram);
             need(moved.metrics().pattern_delta_pages == 3 && ram.metrics().pattern_delta_bytes == 0 &&
                      read(moved, 2 * kPage, kPage) == mostly_zero,
                  "Moving RAM lost its compact encodings or counters");
             moved.fill(2 * kPage + 7, 0, 3);
             need(moved[2 * kPage + 8] == 0 && moved[3 * kPage - 2] == 0x81 &&
                      read(moved, kPage, kPage) == mostly_zero,
                  "A partial zero fill changed another page or dropped an untouched byte");
             moved.fill(0, 0, 2 * kPage);
             need(moved.metrics().pattern_delta_bytes == 0 && moved[15] == 0 && moved[kPage + 8] == 0,
                  "Fully clearing compact pages must discard their backing");
             std::cout << "PRIME_RAM_PATTERN_DELTA_PASS dense_bytes=" << 2 * kPage
                       << " compact_payload_bytes=" << compact_bytes << " spill_writes=0\n";
         }},
        {"live budget adjusts incrementally and preserves spilled and failed writes", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(6 * kPage, 5 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (unsigned page = 0; page < 5; ++page) {
                 keep_dense(ram, page);
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
        {"live reclaim yields at its deadline and keeps the read working set", [] {
             using Adjustment = SparseRam::BudgetAdjustment;
             SparseRam ram(6 * kPage, 4 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (unsigned page = 0; page < 4; ++page) {
                 keep_dense(ram, page);
                 ram[page * kPage] = page + 1;
             }
             need(ram.read_byte(0) == 1 && ram.read_byte(kPage) == 2, "Read working set must be hot");
             need(ram.adjust_payload_budget(2 * kPage, 128, SparseRam::ReclaimClock::time_point::min()) ==
                      Adjustment::Reclaiming &&
                      store->pages.empty() && ram.budget_bytes() == 4 * kPage,
                  "An expired maintenance request must yield without touching resident data or budget");
             need(ram.adjust_payload_budget(2 * kPage, 2) == Adjustment::Applied &&
                      store->pages.size() == 2 && store->pages.count(2) && store->pages.count(3),
                  "Reclaim must select write-only pages before the read working set");
             need(ram.adjust_payload_budget(kPage, 1) == Adjustment::Applied && store->pages.size() == 3,
                  "Reclaim must eventually proceed when only hot pages remain");
             for (unsigned page = 0; page < 4; ++page)
                 need(ram.read_byte(page * kPage) == page + 1, "Reclaim changed the page payload");
             std::cout << "PRIME_RAM_DEADLINE_HOT_SET_PASS deadline=1 write_only_evicted=2 exact=1\n";
         }},
        {"guest allocation protects read pages during a streaming write", [] {
             SparseRam ram(6 * kPage, 4 * kPage);
             auto backing = std::make_unique<TestBacking>();
             auto* store = backing.get();
             ram.attach_page_backing(std::move(backing));
             for (unsigned page = 0; page < 4; ++page) {
                 keep_dense(ram, page);
                 ram[page * kPage] = page + 1;
             }
             need(ram.read_byte(0) == 1 && ram.read_byte(kPage) == 2, "Read pages must be made hot");
             keep_dense(ram, 4);
             keep_dense(ram, 5);
             need(store->pages.size() == 2 && store->pages.count(2) && store->pages.count(3),
                  "Write allocation must evict prior streamed pages ahead of the read working set");
             need(ram.read_byte(0) == 1 && ram.read_byte(kPage) == 2 &&
                      ram.read_byte(2 * kPage) == 3 && ram.read_byte(3 * kPage) == 4,
                  "Streaming eviction changed resident or spilled bytes");
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
             keep_dense(ram, 0);
             keep_dense(ram, 1);
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
        {"compaction cursor survives table release, gaps and the final partial page", [] {
             constexpr auto stride = SparseRam::kPagesPerTable * kPage;
             constexpr auto far = 3 * stride;
             SparseRam ram(far + kPage + 1, 4 * kPage);
             ram.prepare_write(0, 1);
             ram.fill(far, 0x37, kPage);
             ram.prepare_write(far + kPage, 1);
             need(ram.compact_repeating_pages(2) == 2 && ram.pattern_pages() == 1 &&
                      ram.allocated_pages() == 1 && ram[far] == 0x37,
                  "The second slot must be visited across a released table and an empty gap");
             ram.fill(0, 0x42, kPage);
             need(ram.compact_repeating_pages(2) == 2 && ram.pattern_pages() == 2 &&
                      ram.allocated_pages() == 0 && ram[0] == 0x42 && ram[far + kPage] == 0,
                  "The scan must pass the partial last page and resume at the first slot");
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
        {"pattern checks retry after every dense mutation", [] {
             constexpr std::uint8_t pattern = 0x63;
             constexpr auto changed = kPage - 1;
             for (const bool backing : {false, true}) {
                 SparseRam ram(3 * kPage);
                 if (backing) ram.attach_page_backing(std::make_unique<TestBacking>());
                 ram.fill(kPage, pattern, kPage);
                 const auto test_write = [&](const auto& repair) {
                     ram.fill(0, pattern, kPage);
                     ram[changed] = 0x21;
                     need(!ram.compact_repeating_page(0) && !ram.compact_repeating_page(0),
                          "Nonrepeating bytes must remain dense on repeated checks");
                     repair();
                     need(ram.compact_repeating_page(0) && ram.pattern_pages() == 1 &&
                             read(ram, 0, kPage) == std::vector<std::uint8_t>(kPage, pattern),
                          "Dense mutation did not invalidate the negative pattern check");
                 };
                 test_write([&] { ram[changed] = pattern; });
                 test_write([&] { ram.write_block(changed, &pattern, 1); });
                 test_write([&] {
                     const std::array<std::uint8_t, 2> edge{pattern, pattern};
                     ram.write_block(changed, edge.data(), edge.size());
                 });
                 test_write([&] { ram.fill(changed, pattern, 1); });
                 test_write([&] {
                     need(ram.restore_byte(changed, pattern), "Dense rollback must succeed");
                 });
                 test_write([&] {
                     need(ram.try_copy_resident_block(kPage, changed, 1), "Resident copy must succeed");
                 });
                 ram.fill(0, pattern, kPage);
                 ram[changed] = 0x21;
                 need(!ram.compact_repeating_page(0), "Trim fixture must start nonrepeating");
                 ram.fill(0, 0, kPage);
                 need(ram.trim_zero_pages() == 1 && ram.allocated_pages() == 1,
                      "Zero trim must recheck an overwritten dense page");
             }
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
