#include "../sparse_ram.hpp"
#include "../memory_fabric.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>

namespace {
using cosmic_pc::SparseRam;
constexpr auto kPage = SparseRam::kPageSize;
using Page = std::array<std::uint8_t, kPage>;
unsigned checks = 0;

void need(bool condition, const char* reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

template <class Operation>
void fails(Operation operation) {
    ++checks;
    try { operation(); }
    catch (const std::runtime_error&) { return; }
    throw std::runtime_error("Expected failed memory operation");
}

Page contents(std::uint32_t seed) {
    Page bytes{};
    for (auto& byte : bytes) {
        seed ^= seed << 13;
        seed ^= seed >> 17;
        seed ^= seed << 5;
        byte = static_cast<std::uint8_t>(seed);
    }
    return bytes;
}

struct Backing : SparseRam::PageBacking {
    std::unordered_map<std::uint64_t, Page> pages;
    bool fail_read = false, fail_write = false;
    void read(std::uint64_t page, std::uint8_t* bytes) override {
        if (fail_read) throw std::runtime_error("Injected read failure");
        std::copy_n(pages.at(page).data(), kPage, bytes);
    }
    void write(std::uint64_t page, const std::uint8_t* bytes) override {
        if (fail_write) throw std::runtime_error("Injected write failure");
        std::copy_n(bytes, kPage, pages[page].data());
    }
    std::size_t prepare_pages(const PageWrite* writes, std::size_t count,
                              SparseRam::ReclaimClock::time_point) override {
        for (std::size_t i = 0; i < count; ++i) write(writes[i].page, writes[i].bytes);
        return count;
    }
    void reset() noexcept override { pages.clear(); }
};

Backing& attach(SparseRam& ram) {
    auto backing = std::make_unique<Backing>();
    auto& reference = *backing;
    ram.attach_page_backing(std::move(backing));
    return reference;
}

void put(SparseRam& ram, std::uint64_t page, const Page& bytes) {
    ram.write_block(page * kPage, bytes.data(), bytes.size());
}

Page get(const SparseRam& ram, std::uint64_t page) {
    Page bytes;
    ram.read_block(page * kPage, bytes.data(), bytes.size());
    return bytes;
}

void empty_snapshot(const SparseRam::PressureMetrics& m, bool timing_enabled = false) {
    need(m.backing_timing_enabled == timing_enabled, "Timing configuration must match the instance");
    const std::uint64_t counters[]{m.forced_reclaims, m.forced_failures, m.requested_pages,
        m.reclaimed_bytes, m.forced_reclaim_ns, m.forced_reclaim_max_ns, m.selection_ns,
        m.compact_attempts, m.compacted_pages, m.compact_ns, m.spilled_pages, m.spill_ns,
        m.clean_reclaims, m.write_promotion_attempts, m.write_promotions, m.write_promotion_ns,
        m.backing_timed_attempts, m.backing_read_attempts, m.backing_read_failures, m.backing_read_ns,
        m.backing_read_max_ns, m.backing_lock_wait_ns};
    need(std::all_of(std::begin(counters), std::end(counters), [](auto value) { return value == 0; }),
         "Unused or reset slow paths must have zero counters and durations");
}

void timing_relationships(const SparseRam::PressureMetrics& m) {
    need(m.forced_reclaim_ns >= m.forced_reclaim_max_ns, "Maximum reclaim exceeds its sum");
    need(m.selection_ns >= m.compact_ns, "Selection includes its nested compaction time");
    need(m.forced_reclaim_ns >= m.selection_ns + m.spill_ns,
         "Selection and spill belong to the enclosing forced reclaim");
    need(m.backing_read_ns >= m.backing_read_max_ns, "Maximum backing read exceeds its sum");
}

void normal_and_forced_paths() {
    SparseRam ram(8 * kPage, 2 * kPage);
    attach(ram);
    const auto a = contents(1), b = contents(2), c = contents(3);
    put(ram, 0, a);
    put(ram, 1, b);
    need(get(ram, 0) == a && get(ram, 1) == b, "Resident access changed data");
    empty_snapshot(ram.pressure_metrics());

    put(ram, 2, c);
    auto m = ram.pressure_metrics();
    need(m.forced_reclaims == 1 && m.forced_failures == 0 && m.requested_pages == 1,
         "The third dense page must create one forced reclaim admission");
    need(m.reclaimed_bytes == kPage && m.spilled_pages == 1 && m.compact_attempts == 1 &&
         m.compacted_pages == 0 && m.clean_reclaims == 0, "Dirty reclaim counters are wrong");
    timing_relationships(m);

    need(get(ram, 0) == a, "Spilled bytes must survive recovery");
    const auto recovered = ram.pressure_metrics();
    need(recovered.backing_read_attempts == 1 && recovered.backing_read_failures == 0,
         "A cache miss must record one backing read");
    need(!recovered.backing_timing_enabled && recovered.backing_timed_attempts == 0 &&
         recovered.backing_read_ns == 0 && recovered.backing_read_max_ns == 0 &&
         recovered.backing_lock_wait_ns == 0, "Detailed clocks must be disabled by default");
    need(get(ram, 0) == a, "Cached bytes must remain valid");
    m = ram.pressure_metrics();
    need(m.backing_read_attempts == 1 && m.backing_read_ns == recovered.backing_read_ns &&
         m.backing_lock_wait_ns == recovered.backing_lock_wait_ns,
         "A cache hit must not enter or time backing storage");

    ram[0] = 0xa5;
    m = ram.pressure_metrics();
    need(m.forced_reclaims == 2 && m.requested_pages == 2 && m.spilled_pages == 2,
         "Write promotion must count its forced space reclamation");
    need(m.write_promotion_attempts == 1 && m.write_promotions == 1 &&
         m.backing_read_attempts == 1, "A cached write promotion must not count another backing read");
    auto updated = a;
    updated[0] = 0xa5;
    need(get(ram, 0) == updated && get(ram, 1) == b && get(ram, 2) == c,
         "Write promotion must preserve every other guest byte");
    timing_relationships(ram.pressure_metrics());

    const auto original = ram.pressure_metrics();
    SparseRam moved(std::move(ram));
    empty_snapshot(ram.pressure_metrics());
    need(moved.pressure_metrics().forced_reclaims == original.forced_reclaims &&
         moved.pressure_metrics().backing_read_attempts == original.backing_read_attempts,
         "Move construction must preserve both owner and backing metrics");
    SparseRam assigned(kPage);
    assigned = std::move(moved);
    empty_snapshot(moved.pressure_metrics());
    need(assigned.pressure_metrics().forced_reclaim_ns == original.forced_reclaim_ns,
         "Move assignment must preserve elapsed metrics");
    assigned.reset_zero();
    empty_snapshot(assigned.pressure_metrics());
    need(get(assigned, 0) == Page{}, "Reset must not expose old backing data");
}

void failed_reclaim_and_recovery() {
    SparseRam ram(8 * kPage, 2 * kPage);
    auto& backing = attach(ram);
    const auto a = contents(4), b = contents(5), c = contents(6);
    put(ram, 0, a);
    put(ram, 1, b);
    backing.fail_write = true;
    fails([&] { put(ram, 2, c); });
    auto m = ram.pressure_metrics();
    need(m.forced_reclaims == 1 && m.forced_failures == 1 && m.spilled_pages == 0 &&
         m.reclaimed_bytes == 0, "A failed backing write must record failure without false progress");
    need(get(ram, 0) == a && get(ram, 1) == b && get(ram, 2) == Page{},
         "Failed backing write must keep original bytes authoritative");
    timing_relationships(m);
    backing.fail_write = false;
    put(ram, 2, c);
    need(ram.pressure_metrics().forced_reclaims == 2 &&
         ram.pressure_metrics().forced_failures == 1, "A successful retry must preserve failure history");

    // The failed scan advanced its clock hand; recover whichever page was actually spilled.
    need(backing.pages.size() == 1, "One page must have reached the backing");
    const auto spilled = backing.pages.begin()->first;
    const auto original = spilled == 0 ? a : b;
    backing.fail_read = true;
    fails([&] { (void)get(ram, spilled); });
    m = ram.pressure_metrics();
    need(m.backing_read_attempts == 1 && m.backing_read_failures == 1 &&
         ram.metrics().backing_reads == 0, "Failed read must count an attempt, not a successful transfer");
    timing_relationships(m);
    backing.fail_read = false;
    need(get(ram, spilled) == original, "Failed cache fill must be safely retryable");
    m = ram.pressure_metrics();
    need(m.backing_read_attempts == 2 && m.backing_read_failures == 1 &&
         ram.metrics().backing_reads == 1, "Read retry counters are wrong");
}

void failed_promotion() {
    SparseRam ram(8 * kPage, 2 * kPage);
    auto& backing = attach(ram);
    const auto a = contents(7), b = contents(8), c = contents(9);
    put(ram, 0, a);
    put(ram, 1, b);
    put(ram, 2, c);
    backing.fail_read = true;
    fails([&] { ram[0] = 0xab; });
    auto m = ram.pressure_metrics();
    need(m.forced_reclaims == 2 && m.forced_failures == 0 && m.write_promotion_attempts == 1 &&
         m.write_promotions == 0 && m.backing_read_failures == 1,
         "Successful space reclamation and failed destination recovery are distinct");
    backing.fail_read = false;
    need(get(ram, 0) == a && get(ram, 1) == b && get(ram, 2) == c,
         "Failed promotion must not change any guest bytes");
    ram[0] = 0xab;
    m = ram.pressure_metrics();
    need(m.write_promotion_attempts == 2 && m.write_promotions == 1,
         "Only a published write promotion counts as completed");
}

void requested_pages_exclude_existing_residents() {
    SparseRam ram(8 * kPage, 3 * kPage);
    attach(ram);
    const auto bytes = contents(11);
    put(ram, 0, bytes);
    put(ram, 4, bytes);
    put(ram, 5, bytes);
    std::array<std::uint8_t, 3 * kPage> replacement;
    replacement.fill(0xab);
    ram.write_block(0, replacement.data(), replacement.size());
    const auto m = ram.pressure_metrics();
    need(m.forced_reclaims == 1 && m.requested_pages == 2 && m.spilled_pages == 2 &&
         m.reclaimed_bytes == 2 * kPage,
         "A three-page write with one resident destination needs only two new pages");
    for (unsigned i = 0; i < 3; ++i) {
        const auto page = get(ram, i);
        need(std::all_of(page.begin(), page.end(), [](auto byte) { return byte == 0xab; }),
             "Multi-page admission must publish all requested bytes");
    }
}

void compact_and_clean_reclaim() {
    const auto bytes = contents(10);
    {
        SparseRam ram(8 * kPage, 2 * kPage);
        attach(ram);
        put(ram, 0, Page{});
        put(ram, 1, bytes);
        put(ram, 2, bytes);
        const auto m = ram.pressure_metrics();
        need(m.forced_reclaims == 1 && m.compacted_pages == 1 && m.spilled_pages == 0 &&
             m.reclaimed_bytes == kPage, "Zero compaction must not be counted as backing I/O");
        timing_relationships(m);
    }
    {
        SparseRam ram(8 * kPage, 2 * kPage);
        attach(ram);
        put(ram, 0, bytes);
        put(ram, 1, bytes);
        const auto prepared = ram.prepare_reclaim(2, SparseRam::ReclaimClock::time_point::max());
        need(prepared.backed_pages == 2, "Fixture must prepare current copies");
        empty_snapshot(ram.pressure_metrics());
        put(ram, 2, bytes);
        const auto m = ram.pressure_metrics();
        need(m.forced_reclaims == 1 && m.clean_reclaims == 1 && m.spilled_pages == 0 &&
             m.compact_attempts == 0, "Prepared copy reclaim must avoid both compaction and a fresh write");
        timing_relationships(m);
    }
}

void scheduled_reclaim_and_parallel_reads(bool timing_enabled) {
    using Adjustment = SparseRam::BudgetAdjustment;
    SparseRam ram(8 * kPage, 4 * kPage);
    attach(ram);
    ram.set_backing_read_timing_enabled(timing_enabled);
    for (unsigned i = 0; i < 4; ++i) put(ram, i, contents(i + 20));
    need(ram.adjust_payload_budget(0, 4) == Adjustment::Applied,
         "Fixture must move all four pages into backing");
    empty_snapshot(ram.pressure_metrics(), timing_enabled);
    std::array<std::future<Page>, 4> readers;
    for (unsigned i = 0; i < readers.size(); ++i)
        readers[i] = std::async(std::launch::async, [&ram, i] { return get(ram, i); });
    for (unsigned i = 0; i < readers.size(); ++i)
        need(readers[i].get() == contents(i + 20), "Parallel recovery changed bytes");
    const auto m = ram.pressure_metrics();
    need(m.forced_reclaims == 0 && m.spilled_pages == 0 && m.backing_read_attempts == 4 &&
         m.backing_read_failures == 0, "Parallel read counters must not lose updates or count scheduled spills");
    need(m.backing_timed_attempts == (timing_enabled ? 4U : 0U),
         "Parallel timing must count only enabled misses");
    if (!timing_enabled)
        need(m.backing_read_ns == 0 && m.backing_read_max_ns == 0 && m.backing_lock_wait_ns == 0,
             "Disabled parallel reads must not accumulate detailed timings");
    timing_relationships(m);
    fails([&] { put(ram, 4, contents(24)); });
    need(ram.pressure_metrics().forced_reclaims == 1 && ram.pressure_metrics().forced_failures == 1,
         "An impossible admission must be visible even if no victim can fit");
}

void timing_toggle_reset_and_move() {
    struct DelayedBacking final : Backing {
        void read(std::uint64_t page, std::uint8_t* bytes) override {
            // A bounded fake I/O delay makes enabled duration assertions
            // meaningful without depending on a real filesystem's latency.
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            Backing::read(page, bytes);
        }
    };
    SparseRam ram(8 * kPage, 4 * kPage);
    auto backend = std::make_unique<DelayedBacking>();
    auto& backing = *backend;
    ram.attach_page_backing(std::move(backend));
    const auto bytes = contents(25);
    for (unsigned i = 0; i < 4; ++i) put(ram, i, bytes);
    need(ram.adjust_payload_budget(0, 4) == SparseRam::BudgetAdjustment::Applied,
         "Timing fixture must start with four cold pages");
    need(get(ram, 0) == bytes, "Untimed cold read changed guest data");
    auto m = ram.pressure_metrics();
    need(!m.backing_timing_enabled && m.backing_read_attempts == 1 && m.backing_timed_attempts == 0 &&
         m.backing_read_ns == 0 && m.backing_lock_wait_ns == 0,
         "Even slow untimed reads must leave detailed times unchanged");

    ram.set_backing_read_timing_enabled(true);
    backing.fail_read = true;
    fails([&] { (void)get(ram, 1); });
    m = ram.pressure_metrics();
    need(m.backing_timing_enabled && m.backing_read_attempts == 2 && m.backing_read_failures == 1 &&
         m.backing_timed_attempts == 1 && m.backing_read_ns >= 1'000'000 &&
         m.backing_read_max_ns == m.backing_read_ns, "Enabled failed reads must retain their duration");
    backing.fail_read = false;
    need(get(ram, 1) == bytes, "Enabled recovery retry changed guest data");
    const auto measured = ram.pressure_metrics();
    need(measured.backing_read_attempts == 3 && measured.backing_timed_attempts == 2 &&
         measured.backing_read_ns >= m.backing_read_ns + 1'000'000,
         "Enabled retries must accumulate both attempts and duration");

    ram.set_backing_read_timing_enabled(false);
    need(get(ram, 2) == bytes, "Untimed read after toggle changed guest data");
    m = ram.pressure_metrics();
    need(!m.backing_timing_enabled && m.backing_read_attempts == 4 &&
         m.backing_timed_attempts == measured.backing_timed_attempts &&
         m.backing_read_ns == measured.backing_read_ns &&
         m.backing_read_max_ns == measured.backing_read_max_ns &&
         m.backing_lock_wait_ns == measured.backing_lock_wait_ns,
         "Disabling timing must freeze measured history without stopping ordinary counters");
    ram.set_backing_read_timing_enabled(true);
    need(get(ram, 3) == bytes, "Reenabled read changed guest data");
    const auto before_move = ram.pressure_metrics();
    need(before_move.backing_read_attempts == 5 && before_move.backing_timed_attempts == 3,
         "Reenabling timing must continue the existing history");
    SparseRam moved(std::move(ram));
    empty_snapshot(ram.pressure_metrics());
    need(moved.pressure_metrics().backing_timing_enabled &&
         moved.pressure_metrics().backing_timed_attempts == before_move.backing_timed_attempts,
         "Move construction must transfer enabled timing and its history");
    SparseRam assigned(kPage);
    assigned = std::move(moved);
    empty_snapshot(moved.pressure_metrics());
    need(assigned.pressure_metrics().backing_timing_enabled &&
         assigned.pressure_metrics().backing_read_ns == before_move.backing_read_ns,
         "Move assignment must transfer timing configuration and duration");
    assigned.reset_zero();
    empty_snapshot(assigned.pressure_metrics(), true);
}

void memory_fabric_forwards_timing() {
    cosmic_pc::MemoryFabric memory(8 * kPage, 2 * kPage);
    need(!memory.pressure_metrics().backing_timing_enabled, "MemoryFabric must default to timing off");
    memory.set_backing_read_timing_enabled(true); // Also valid before attaching storage.
    memory.attach_page_backing(std::make_unique<Backing>());
    const auto bytes = contents(26);
    memory.write_block(0, bytes.data(), bytes.size());
    need(memory.adjust_payload_budget(0, 1) == SparseRam::BudgetAdjustment::Applied,
         "MemoryFabric fixture must have a spilled page");
    Page restored;
    memory.read_block(0, restored.data(), restored.size());
    const cosmic_pc::MemoryFabric::PressureMetrics measured = memory.pressure_metrics();
    need(restored == bytes && measured.backing_timing_enabled && measured.backing_timed_attempts == 1 &&
         measured.backing_read_attempts == 1, "MemoryFabric must forward configuration and snapshots");
    memory.set_backing_read_timing_enabled(false);
    need(!memory.pressure_metrics().backing_timing_enabled &&
         memory.pressure_metrics().backing_timed_attempts == 1,
         "MemoryFabric toggling must preserve measured history");
}
} // namespace

int main() {
    try {
        normal_and_forced_paths();
        failed_reclaim_and_recovery();
        failed_promotion();
        requested_pages_exclude_existing_residents();
        compact_and_clean_reclaim();
        scheduled_reclaim_and_parallel_reads(false);
        scheduled_reclaim_and_parallel_reads(true);
        timing_toggle_reset_and_move();
        memory_fabric_forwards_timing();
        std::cout << "Sparse RAM pressure: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Sparse RAM pressure: " << error.what() << '\n';
        return 1;
    }
}
