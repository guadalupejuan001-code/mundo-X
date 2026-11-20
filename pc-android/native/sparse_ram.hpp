#ifndef COSMIC_PC_SPARSE_RAM_HPP
#define COSMIC_PC_SPARSE_RAM_HPP

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosmic_pc {

// Sparse physical RAM. Absent pages read as zero. Compact pages retain an
// eight-byte repeating pattern, a single word, or a pattern with changed words.
// Logical addresses are independent of the explicitly bounded payload.
// Readers may share stable page tables. The owner must join those readers
// before writing, reclaiming or replacing RAM or its backing.
class SparseRam {
public:
    static constexpr std::size_t kPageSize = 4096;
    static constexpr std::size_t kPatternSize = 8;
    static constexpr std::size_t kPagesPerTable = 512;
    static constexpr std::uint64_t kDefaultPayloadLimit = std::uint64_t{1} << 30;

    enum class BudgetAdjustment { Applied, Reclaiming, Blocked };
    enum class BackingCachePolicy { Fixed, Adaptive };
    using ReclaimClock = std::chrono::steady_clock;

    class BudgetExceeded : public std::runtime_error {
    public:
        explicit BudgetExceeded(const std::string& reason) : std::runtime_error(reason) {}
    };

    // The host owns the ephemeral storage. Read and write transfer exactly
    // one full page or throw; an unsuccessful write leaves RAM authoritative.
    class PageBacking {
    public:
        class CapacityExceeded : public std::runtime_error {
        public:
            using std::runtime_error::runtime_error;
        };

        struct PageWrite {
            std::uint64_t page;
            const std::uint8_t* bytes;
        };
        static constexpr std::size_t kMaxWritePages = 16;

        virtual ~PageBacking() = default;
        virtual void read(std::uint64_t page, std::uint8_t* bytes) = 0;
        virtual void write(std::uint64_t page, const std::uint8_t* bytes) = 0;
        virtual bool supports_grouped_writes() const noexcept { return false; }
        virtual void begin_paced_reclaim() noexcept {}
        virtual void end_paced_reclaim() noexcept {}
        // Return the length of the committed prefix. On failure all pages
        // remain authoritative in RAM. Finish at most one in-flight group.
        virtual std::size_t write_pages(const PageWrite*, std::size_t, ReclaimClock::time_point) {
            throw std::logic_error("SparseRam backing does not support grouped writes.");
        }
        // Prepare clean copies of still-resident pages. A backend can decline
        // optional work by returning zero. On exception it publishes nothing.
        virtual std::size_t prepare_pages(const PageWrite*, std::size_t, ReclaimClock::time_point) {
            return 0;
        }
        virtual void discard(std::uint64_t) noexcept {}
        virtual void reset() noexcept = 0;
    };

    struct Metrics {
        std::uint64_t logical_bytes;
        std::uint64_t allocated_bytes;
        std::uint64_t budget_bytes;
        std::uint64_t metadata_bytes;
        std::size_t allocated_pages;
        std::size_t pattern_pages;
        std::size_t sparse_word_pages;
        std::size_t spilled_pages;
        std::uint64_t backing_bytes;
        std::uint64_t backing_reads;
        std::uint64_t backing_writes;
        std::size_t pattern_delta_pages;
        std::uint64_t pattern_delta_bytes;
        std::uint64_t backing_cache_hits;
        std::uint64_t backing_cache_bytes;
        std::uint64_t backing_cache_capacity_bytes;
        std::uint64_t backing_overwrite_pages;
        std::size_t page_state_vectors;
        std::size_t backing_clean_pages;
        std::uint64_t backing_clean_reclaims;
    };

    // Cumulative counts and wall-clock timings since reset_zero(), sampled by
    // the owner after readers join. Failed attempts retain their elapsed time.
    // Forced reclaim is only the space-making portion of a guest write, not
    // scheduled maintenance. selection_ns includes compact_ns; selection_ns
    // and spill_ns are portions of forced_reclaim_ns, not additional costs.
    // Backing read counts always include all misses (inspection/promotion too).
    // Their detailed timers are opt-in: durations and timed_attempts accumulate
    // only while enabled, and toggling does not erase previous observations.
    // Read time excludes lock wait; durations can overlap other readers' time.
    struct PressureMetrics {
        std::uint64_t forced_reclaims = 0;
        // Failure to obtain space; a later allocation/read can fail separately.
        std::uint64_t forced_failures = 0;
        // New dense representations needed by pressured preflights, not all
        // touched pages and not necessarily pages successfully allocated.
        std::uint64_t requested_pages = 0;
        std::uint64_t reclaimed_bytes = 0;
        std::uint64_t forced_reclaim_ns = 0;
        std::uint64_t forced_reclaim_max_ns = 0;
        std::uint64_t selection_ns = 0;
        std::uint64_t compact_attempts = 0;
        std::uint64_t compacted_pages = 0;
        std::uint64_t compact_ns = 0;
        std::uint64_t spilled_pages = 0;
        std::uint64_t spill_ns = 0;
        std::uint64_t clean_reclaims = 0;
        std::uint64_t write_promotion_attempts = 0;
        // Published promotions only. Recovery timing includes cache/lock work,
        // overlaps backing_read_ns when it misses, and excludes allocation.
        std::uint64_t write_promotions = 0;
        std::uint64_t write_promotion_ns = 0;
        bool backing_timing_enabled = false;
        std::uint64_t backing_timed_attempts = 0;
        std::uint64_t backing_read_attempts = 0;
        std::uint64_t backing_read_failures = 0;
        std::uint64_t backing_read_ns = 0;
        std::uint64_t backing_read_max_ns = 0;
        std::uint64_t backing_lock_wait_ns = 0;
    };

    struct ReclaimPreparation {
        std::size_t backed_pages = 0;
        std::size_t compact_up_to_2k = 0;
        std::size_t compact_up_to_3k = 0;
        std::size_t dense_pages = 0;
        std::uint64_t released_bytes = 0;
        bool backing_failed = false;
    };

    class WritePins {
    public:
        WritePins(const WritePins&) = delete;
        WritePins& operator=(const WritePins&) = delete;
        ~WritePins() {
            if (owner_) --owner_->pin_depth_;
        }

    private:
        friend class SparseRam;
        explicit WritePins(SparseRam* owner) : owner_(owner) {}
        SparseRam* owner_;
    };

    class ByteProxy {
    public:
        ByteProxy(const ByteProxy&) = default;
        operator std::uint8_t() const { return owner_->read_byte(offset_); }
        ByteProxy& operator=(std::uint8_t value) {
            owner_->write_byte(offset_, value);
            return *this;
        }
        // Assignment copies the byte, never the proxy's owner or address.
        ByteProxy& operator=(const ByteProxy& other) {
            return *this = static_cast<std::uint8_t>(other);
        }
        ByteProxy& operator|=(std::uint8_t value) {
            return *this = static_cast<std::uint8_t>(*this) | value;
        }
        ByteProxy& operator&=(std::uint8_t value) {
            return *this = static_cast<std::uint8_t>(*this) & value;
        }
        ByteProxy& operator^=(std::uint8_t value) {
            return *this = static_cast<std::uint8_t>(*this) ^ value;
        }

    private:
        friend class SparseRam;
        ByteProxy(SparseRam& owner, std::uint64_t offset) : owner_(&owner), offset_(offset) {}
        SparseRam* owner_;
        std::uint64_t offset_;
    };

    static std::uint64_t default_budget(std::uint64_t logical_bytes) noexcept {
        if (logical_bytes >= kDefaultPayloadLimit) return kDefaultPayloadLimit;
        return ((logical_bytes + kPageSize - 1) / kPageSize) * kPageSize;
    }

    explicit SparseRam(std::uint64_t logical_bytes)
        : SparseRam(logical_bytes, default_budget(logical_bytes)) {}

    SparseRam(std::uint64_t logical_bytes, std::uint64_t payload_cap_bytes)
        : logical_bytes_(logical_bytes), payload_cap_bytes_(payload_cap_bytes) {
        const std::uint64_t pages = logical_bytes / kPageSize + (logical_bytes % kPageSize != 0);
        const std::uint64_t tables = pages / kPagesPerTable + (pages % kPagesPerTable != 0);
        if (tables > std::numeric_limits<std::size_t>::max() / sizeof(std::unique_ptr<PageTable>)) {
            throw std::length_error("SparseRam page-table root exceeds host address capacity.");
        }
        try {
            tables_.resize(static_cast<std::size_t>(tables));
        } catch (const std::bad_alloc&) {
            throw std::runtime_error("SparseRam host allocation failed for the page-table root.");
        }
    }

    SparseRam(const SparseRam&) = delete;
    SparseRam& operator=(const SparseRam&) = delete;
    SparseRam(SparseRam&& other) noexcept
        : logical_bytes_(std::exchange(other.logical_bytes_, 0)),
          payload_cap_bytes_(std::exchange(other.payload_cap_bytes_, 0)),
          tables_(std::move(other.tables_)),
          allocated_pages_(std::exchange(other.allocated_pages_, 0)),
          allocated_tables_(std::exchange(other.allocated_tables_, 0)),
          pattern_pages_(std::exchange(other.pattern_pages_, 0)),
          sparse_word_pages_(std::exchange(other.sparse_word_pages_, 0)),
          pattern_delta_pages_(std::exchange(other.pattern_delta_pages_, 0)),
          pattern_delta_bytes_(std::exchange(other.pattern_delta_bytes_, 0)),
          compaction_page_(std::exchange(other.compaction_page_, 0)),
          backing_(std::move(other.backing_)),
          backing_cache_(std::move(other.backing_cache_)),
          spilled_pages_(std::exchange(other.spilled_pages_, 0)),
          backing_writes_(std::exchange(other.backing_writes_, 0)),
          backing_clean_pages_(std::exchange(other.backing_clean_pages_, 0)),
          backing_clean_reclaims_(std::exchange(other.backing_clean_reclaims_, 0)),
          clean_history_(std::move(other.clean_history_)),
          clean_history_size_(std::exchange(other.clean_history_size_, 0)),
          clean_history_next_(std::exchange(other.clean_history_next_, 0)),
          preparation_page_(std::exchange(other.preparation_page_, 0)),
          eviction_page_(std::exchange(other.eviction_page_, 0)),
          compact_eviction_page_(std::exchange(other.compact_eviction_page_, 0)),
          pin_epoch_(std::exchange(other.pin_epoch_, 0)),
          pressure_(std::exchange(other.pressure_, PressureMetrics{})),
          backing_read_timing_enabled_(std::exchange(other.backing_read_timing_enabled_, false)) {}
    SparseRam& operator=(SparseRam&& other) noexcept {
        if (this != &other) {
            tables_ = std::move(other.tables_);
            logical_bytes_ = std::exchange(other.logical_bytes_, 0);
            payload_cap_bytes_ = std::exchange(other.payload_cap_bytes_, 0);
            allocated_pages_ = std::exchange(other.allocated_pages_, 0);
            allocated_tables_ = std::exchange(other.allocated_tables_, 0);
            pattern_pages_ = std::exchange(other.pattern_pages_, 0);
            sparse_word_pages_ = std::exchange(other.sparse_word_pages_, 0);
            pattern_delta_pages_ = std::exchange(other.pattern_delta_pages_, 0);
            pattern_delta_bytes_ = std::exchange(other.pattern_delta_bytes_, 0);
            compaction_page_ = std::exchange(other.compaction_page_, 0);
            backing_ = std::move(other.backing_);
            backing_cache_ = std::move(other.backing_cache_);
            spilled_pages_ = std::exchange(other.spilled_pages_, 0);
            backing_writes_ = std::exchange(other.backing_writes_, 0);
            backing_clean_pages_ = std::exchange(other.backing_clean_pages_, 0);
            backing_clean_reclaims_ = std::exchange(other.backing_clean_reclaims_, 0);
            clean_history_ = std::move(other.clean_history_);
            clean_history_size_ = std::exchange(other.clean_history_size_, 0);
            clean_history_next_ = std::exchange(other.clean_history_next_, 0);
            preparation_page_ = std::exchange(other.preparation_page_, 0);
            eviction_page_ = std::exchange(other.eviction_page_, 0);
            compact_eviction_page_ = std::exchange(other.compact_eviction_page_, 0);
            pin_epoch_ = std::exchange(other.pin_epoch_, 0);
            pressure_ = std::exchange(other.pressure_, PressureMetrics{});
            backing_read_timing_enabled_ = std::exchange(other.backing_read_timing_enabled_, false);
        }
        return *this;
    }

    std::uint64_t size() const noexcept { return logical_bytes_; }
    bool empty() const noexcept { return logical_bytes_ == 0; }
    // Counts only full resident pages.
    std::size_t allocated_pages() const noexcept { return allocated_pages_; }
    std::size_t pattern_pages() const noexcept { return pattern_pages_; }
    std::size_t spilled_pages() const noexcept { return spilled_pages_; }
    bool has_page_backing() const noexcept { return bool(backing_); }
    bool has_pinned_writes() const noexcept { return pin_depth_ != 0; }
    void attach_page_backing(std::unique_ptr<PageBacking> backing,
                             BackingCachePolicy policy = BackingCachePolicy::Fixed,
                             std::size_t clean_history_pages = 512) {
        if (!backing || backing_ || !clean_history_pages)
            throw std::invalid_argument("SparseRam expects one page backing and a clean page history.");
        auto cache = std::make_unique<BackingCache>(policy);
        cache->read_timing_enabled = backing_read_timing_enabled_;
        cache->resize(cache->sets_for(payload_cap_bytes_, payload_cap_bytes_ - allocated_bytes()));
        clean_history_.resize(clean_history_pages);
        backing_cache_ = std::move(cache);
        backing_ = std::move(backing);
    }
    // A CPU journal holds these pins through rollback. Read-only accesses
    // never promote pages and cannot disturb the write set while pinned.
    [[nodiscard]] WritePins pin_writes() noexcept {
        if (!backing_) return WritePins(nullptr);
        if (pin_depth_++ == 0 && ++pin_epoch_ == 0) {
            for (auto& table : tables_)
                if (table) for (auto& slot : table->slots) slot.write_epoch = 0;
            pin_epoch_ = 1;
        }
        return WritePins(this);
    }
    std::uint64_t allocated_bytes() const noexcept {
        return static_cast<std::uint64_t>(allocated_pages_) * kPageSize + compact_bytes();
    }
    std::uint64_t budget_bytes() const noexcept { return payload_cap_bytes_; }
    std::uint64_t backing_cache_bytes() const noexcept {
        return backing_cache_ ? backing_cache_->payload_bytes() : 0;
    }
    std::size_t clean_backing_pages() const noexcept { return backing_clean_pages_; }

    // Owner only, after joining readers, just like backing attachment/reset.
    // Reset clears observations but preserves this per-instance configuration.
    // Enabling can be deferred until investigation so ordinary misses do not
    // read the host clock. Switching modes keeps all accumulated observations.
    void set_backing_read_timing_enabled(bool enabled) noexcept {
        backing_read_timing_enabled_ = enabled;
        if (backing_cache_) backing_cache_->read_timing_enabled = enabled;
    }

    // The owner calls this between execution slices, before memory pressure.
    // Neither a failed prewrite nor a declined batch changes a dense page.
    ReclaimPreparation prepare_reclaim(std::size_t ready_limit, ReclaimClock::time_point deadline) {
        ReclaimPreparation result;
        ready_limit = std::min(ready_limit, clean_history_.size());
        if (!backing_ || has_pinned_writes() || !allocated_pages_ ||
            backing_clean_pages_ >= ready_limit || ReclaimClock::now() >= deadline) return result;

        constexpr auto compact_limit = 3 * kPageSize / 4;
        constexpr auto max_changes = (compact_limit - kPatternSize) / kPatchSize;
        std::array<PageBacking::PageWrite, PageBacking::kMaxWritePages> writes;
        const auto limit = std::min(writes.size(), ready_limit - backing_clean_pages_);
        const auto pages = logical_bytes_ / kPageSize + (logical_bytes_ % kPageSize != 0);
        std::size_t count = 0, candidates = 0;
        ReclaimDeadline window{deadline};
        auto remaining = pages;
        for (std::size_t scanned = 0; remaining && scanned < 4 * kPagesPerTable &&
                candidates < writes.size() && count < limit; ++scanned) {
            if (window.expired_during_scan()) break;
            const auto page = preparation_page_;
            auto& table = tables_[static_cast<std::size_t>(page / kPagesPerTable)];
            if (!table) {
                remaining -= skip_empty_tables(preparation_page_, pages, remaining);
                continue;
            }
            if (++preparation_page_ == pages) preparation_page_ = 0;
            --remaining;
            const auto index = static_cast<std::size_t>(page % kPagesPerTable);
            auto& slot = table->slots[index];
            if (!slot.has_dense() || slot.backing_current || slot.give_second_chance()) continue;
            if (window.expired()) break;
            ++candidates;
            const auto before = allocated_bytes();
            if (compact_page(table, index) || compact_changed_words<max_changes>(slot, compact_limit)) {
                const auto released = before - allocated_bytes();
                result.released_bytes += released;
                if (kPageSize - released <= kPageSize / 2) ++result.compact_up_to_2k;
                else ++result.compact_up_to_3k;
            } else {
                ++result.dense_pages;
                writes[count++] = {page, slot.payload.get()};
            }
        }
        if (!count || window.expired()) return result;
        try {
            count = backing_->prepare_pages(writes.data(), count, deadline);
        } catch (const std::exception&) {
            result.backing_failed = true;
            return result;
        }
        for (std::size_t i = 0; i < count; ++i) {
            const auto page = writes[i].page;
            backing_cache_->invalidate(page);
            retain_clean_backing(page, *find_slot(page));
        }
        backing_writes_ += count;
        result.backed_pages = count;
        return result;
    }

    BudgetAdjustment adjust_payload_budget(std::uint64_t target, std::size_t reclaim_pages,
                                           ReclaimClock::time_point deadline = ReclaimClock::time_point::max(),
                                           std::size_t clean_reclaim_pages = 0) {
        if (has_pinned_writes()) return BudgetAdjustment::Blocked;
        const auto cache_sets = backing_cache_ ? backing_cache_->sets_for(target, target) : 0;
        const auto guest_target = target - (backing_cache_ ? backing_cache_->reserved_bytes(cache_sets) : 0);
        const auto before = allocated_bytes();
        if (guest_target < before) {
            ReclaimDeadline limit{deadline};
            auto* paced = deadline == ReclaimClock::time_point::max() ? nullptr : &limit;
            std::size_t reclaimed_pages = 0;
            const auto clean_limit = std::max(reclaim_pages, clean_reclaim_pages);
            if (paced && clean_limit && !paced->expired()) {
                // Ready copies release resident bytes before cache maintenance
                // or entering the backend's paced write scope.
                reclaimed_pages = reclaim_cold_backed_pages(guest_target, clean_limit, *paced);
                if (backing_cache_ && (allocated_bytes() > guest_target ||
                    cache_sets == backing_cache_->active_sets)) backing_cache_->release_unused(deadline);
            }
            struct ReclaimScope {
                PageBacking* backing;
                explicit ReclaimScope(PageBacking* backing) noexcept : backing(backing) {
                    if (backing) backing->begin_paced_reclaim();
                }
                ~ReclaimScope() { if (backing) backing->end_paced_reclaim(); }
            } scope(paced && reclaimed_pages < reclaim_pages && allocated_bytes() > guest_target
                    ? backing_.get() : nullptr);
            const bool grouped = backing_ && backing_->supports_grouped_writes();
            for (auto i = reclaimed_pages; i < reclaim_pages && allocated_bytes() > guest_target;) {
                if (paced && paced->expired()) break;
                const auto reclaimed = grouped
                    ? evict_dense_group(guest_target, reclaim_pages - i, paced)
                    : std::size_t(evict_page(UINT64_MAX, UINT64_MAX, guest_target, paced));
                if (reclaimed) {
                    i += reclaimed;
                    continue;
                }
                if (paced && paced->expired()) break;
                if (spill_compact_page(guest_target, UINT64_MAX, UINT64_MAX, paced)) {
                    ++i;
                    continue;
                }
                if (paced && paced->expired()) break;
                return BudgetAdjustment::Blocked;
            }
            const auto used = allocated_bytes();
            if (used > guest_target) {
                // With no storage, a full working set may offer no further
                // compaction. Leave retries to the next pressure sample.
                if (!backing_ && used == before && paced && paced->expired())
                    return BudgetAdjustment::Blocked;
                if (backing_ && used < before) {
                    const auto reserve = std::min<std::uint64_t>(
                        std::max<std::size_t>(1, reclaim_pages / 2),
                        (guest_payload_limit() - used) / kPageSize) * kPageSize;
                    payload_cap_bytes_ = used + reserve + backing_cache_->reserved_bytes(backing_cache_->active_sets);
                }
                return BudgetAdjustment::Reclaiming;
            }
        }
        // Keep decoded hot pages during a staged shrink. The cache still fits
        // the current budget and can be resized alongside the final target.
        payload_cap_bytes_ = target;
        if (backing_cache_) backing_cache_->resize(cache_sets);
        return BudgetAdjustment::Applied;
    }
    // Exact owned object/table storage, excluding allocator bookkeeping and
    // temporary staging buffers. This is not total process RSS or PSS.
    std::uint64_t metadata_bytes() const noexcept {
        const auto inline_payload =
            static_cast<std::uint64_t>(pattern_pages_ + sparse_word_pages_ + pattern_delta_pages_) * kPatternSize;
        return sizeof(*this) +
               static_cast<std::uint64_t>(tables_.capacity()) * sizeof(std::unique_ptr<PageTable>) +
               static_cast<std::uint64_t>(clean_history_.capacity()) * sizeof(std::uint64_t) +
               static_cast<std::uint64_t>(allocated_tables_) * sizeof(PageTable) - inline_payload +
               (backing_cache_ ? backing_cache_->storage_bytes() : 0);
    }
    Metrics metrics() const noexcept {
        return {size(), allocated_bytes(), budget_bytes(), metadata_bytes(), allocated_pages(), pattern_pages(),
                sparse_word_pages_, spilled_pages_, static_cast<std::uint64_t>(spilled_pages_) * kPageSize,
                backing_cache_ ? backing_cache_->reads.load(std::memory_order_relaxed) : 0,
                backing_writes_, pattern_delta_pages_, pattern_delta_bytes_,
                backing_cache_ ? backing_cache_->hits.load(std::memory_order_relaxed) : 0, backing_cache_bytes(),
                backing_cache_ ? backing_cache_->active_sets * BackingCache::kBytesPerSet : 0,
                backing_cache_ ? backing_cache_->overwrite_pages : 0, allocated_tables_,
                backing_clean_pages_, backing_clean_reclaims_};
    }

    PressureMetrics pressure_metrics() const noexcept {
        auto snapshot = pressure_;
        snapshot.backing_timing_enabled = backing_read_timing_enabled_;
        if (backing_cache_) {
            const auto& cache = *backing_cache_;
            snapshot.backing_read_failures = cache.read_failures.load(std::memory_order_relaxed);
            snapshot.backing_read_attempts = cache.reads.load(std::memory_order_relaxed) +
                                             snapshot.backing_read_failures;
            snapshot.backing_timed_attempts = cache.timed_attempts.load(std::memory_order_relaxed);
            snapshot.backing_read_ns = cache.read_ns.load(std::memory_order_relaxed);
            snapshot.backing_read_max_ns = cache.read_max_ns.load(std::memory_order_relaxed);
            snapshot.backing_lock_wait_ns = cache.lock_wait_ns.load(std::memory_order_relaxed);
        }
        return snapshot;
    }

    ByteProxy operator[](std::uint64_t offset) {
        check_range(offset, 1);
        return ByteProxy(*this, offset);
    }
    std::uint8_t operator[](std::uint64_t offset) const { return read_byte(offset); }
    ByteProxy at(std::uint64_t offset) { return (*this)[offset]; }
    std::uint8_t at(std::uint64_t offset) const { return read_byte(offset); }

    std::uint8_t read_byte(std::uint64_t offset) const {
        check_range(offset, 1);
        const auto page = offset / kPageSize;
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        const auto* slot = find_slot(page);
        if (!slot) return 0;
        if (slot->spilled) {
            std::uint8_t byte;
            read_backed_page(page, inside, &byte, 1);
            return byte;
        }
        if (backing_ && slot->has_dense()) slot->touch();
        return slot_byte(*slot, inside);
    }

    // Borrow a span for an immediate read. A RAM mutation can replace its backing.
    const std::uint8_t* resident_span(std::uint64_t offset, std::size_t count) const noexcept {
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        if (!count || count > kPageSize - inside || offset >= logical_bytes_ ||
            count > logical_bytes_ - offset) return nullptr;
        const auto* slot = find_slot(offset / kPageSize);
        if (!slot || !slot->has_dense()) return nullptr;
        if (backing_) slot->touch();
        return slot->payload.get() + inside;
    }

    void write_byte(std::uint64_t offset, std::uint8_t value) {
        check_range(offset, 1);
        const auto page = offset / kPageSize;
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        auto* slot = find_slot(page);
        if (!slot || !slot->has_dense()) {
            if ((!slot || !slot->spilled) && (slot ? slot_byte(*slot, inside) : 0) == value) return;
            prepare_write(offset, 1);
            slot = find_slot(page);
        } else if (backing_) mark_write(*slot);
        if (slot->backing_current && slot->payload[inside] != value)
            discard_clean_backing(page, *slot);
        slot->payload[inside] = value;
        slot->nonrepeating = false;
        // Never free a page here, including when it becomes entirely zero.
    }

    // Use the default for concurrent readers. Serialized callers may bypass
    // cache-set locking when they own the RAM for the whole read.
    template <bool Synchronized = true>
    void read_block(std::uint64_t offset, std::uint8_t* destination,
                    std::size_t count) const {
        check_range(offset, count);
        if (count != 0 && !destination) throw std::invalid_argument("SparseRam null read destination.");
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            const auto page = offset / kPageSize;
            const auto* slot = find_slot(page);
            if (slot && slot->has_dense()) {
                if (backing_) slot->touch();
                std::memcpy(destination, slot->payload.get() + inside, chunk);
            } else if (slot && slot->spilled) {
                read_backed_page<Synchronized>(page, inside, destination, chunk);
            } else if (slot && slot->has_compact()) copy_compact(*slot, inside, destination, chunk);
            else std::memset(destination, 0, chunk);
            offset += chunk;
            destination += chunk;
            count -= chunk;
        }
    }

    struct ByteScan {
        std::size_t examined;
        std::uint8_t last;
    };

    // Stop at the first matching byte, including it in the returned count.
    ByteScan scan_until_byte(std::uint64_t start, std::size_t count, std::uint8_t value) const {
        check_range(start, count);
        constexpr auto table_bytes = kPageSize * kPagesPerTable;
        std::size_t examined = 0;
        std::uint8_t last = 0;
        std::array<std::uint8_t, kPageSize> expanded;
        while (examined < count) {
            const auto address = start + examined;
            const auto& table = tables_[static_cast<std::size_t>(address / table_bytes)];
            if (!table) {
                if (value == 0) return {examined + 1, 0};
                examined += std::min(count - examined, table_bytes - std::size_t(address % table_bytes));
                last = 0;
                continue;
            }
            const auto page = address / kPageSize;
            const auto& slot = table->slots[static_cast<std::size_t>(page % kPagesPerTable)];
            const auto inside = static_cast<std::size_t>(address % kPageSize);
            const auto chunk = std::min(count - examined, kPageSize - inside);
            const std::uint8_t* bytes;
            if (slot.has_dense()) {
                if (backing_) slot.touch();
                bytes = slot.payload.get() + inside;
            } else if (slot.spilled) {
                read_backed_page(page, inside, expanded.data(), chunk);
                bytes = expanded.data();
            } else if (slot.word_index == kRepeating) {
                const auto* pattern = reinterpret_cast<const unsigned char*>(&slot.pattern);
                const auto probe = slot.pattern ? std::min(chunk, kPatternSize) : 1;
                for (std::size_t i = 0; i < probe; ++i)
                    if (pattern[(inside + i) % kPatternSize] == value) return {examined + i + 1, value};
                last = pattern[(inside + chunk - 1) % kPatternSize];
                examined += chunk;
                continue;
            } else {
                copy_compact(slot, inside, expanded.data(), chunk);
                bytes = expanded.data();
            }
            const auto* match = static_cast<const std::uint8_t*>(std::memchr(bytes, value, chunk));
            if (match) return {examined + static_cast<std::size_t>(match - bytes) + 1, value};
            last = bytes[chunk - 1];
            examined += chunk;
        }
        return {examined, last};
    }

    // Guest bytes change only after the range, budget and allocations are
    // checked. The caller's source must remain valid for the full call.
    void write_block(std::uint64_t offset, const std::uint8_t* source,
                     std::size_t count, std::uint8_t* previous = nullptr) {
        check_range(offset, count);
        if (!count) return;
        if (!source) throw std::invalid_argument("SparseRam null write source.");
        const auto first_offset = static_cast<std::size_t>(offset % kPageSize);
        if (count <= kPageSize - first_offset) {
            auto* slot = find_slot(offset / kPageSize);
            if (slot && slot->has_dense()) {
                if (backing_) mark_write(*slot);
                write_dense_bytes(offset / kPageSize, *slot, first_offset, source, count, previous);
                return;
            }
        }
        prepare_dense_pages(offset, count, true, previous == nullptr);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            const auto page = offset / kPageSize;
            write_dense_bytes(page, *find_slot(page), inside, source, chunk, previous);
            if (previous) previous += chunk;
            offset += chunk;
            source += chunk;
            count -= chunk;
        }
    }

    // Both pages must already be dense. This keeps the source alive without
    // pinning or eviction and avoids copying via a temporary buffer.
    bool try_copy_resident_block(std::uint64_t source, std::uint64_t target,
                                 std::size_t count) {
        check_range(source, count);
        check_range(target, count);
        const auto from_offset = static_cast<std::size_t>(source % kPageSize);
        const auto to_offset = static_cast<std::size_t>(target % kPageSize);
        if (!count || count > kPageSize - from_offset || count > kPageSize - to_offset ||
            (source < target + count && target < source + count)) return false;
        const auto* from = find_slot(source / kPageSize);
        auto* to = find_slot(target / kPageSize);
        if (!from || !from->has_dense() || !to || !to->has_dense()) return false;
        if (backing_) {
            from->touch();
            mark_write(*to);
        }
        write_dense_bytes(target / kPageSize, *to, to_offset, from->payload.get() + from_offset, count, nullptr);
        return true;
    }

    // A full-page copy can initialize an implicit destination directly.
    // Require spare backing so allocating it cannot evict the source.
    bool try_copy_to_implicit_page(std::uint64_t source, std::uint64_t target) {
        check_range(source, kPageSize);
        check_range(target, kPageSize);
        if (source % kPageSize || target % kPageSize || has_pinned_writes()) return false;

        const auto* from = find_slot(source / kPageSize);
        if (!from || !from->has_dense()) return false;
        const auto page = target / kPageSize;
        const auto* to = find_slot(page);
        if (to && (to->has_dense() || to->has_compact() || to->spilled)) return false;
        const auto limit = guest_payload_limit();
        if (limit < kPageSize || allocated_bytes() > limit - kPageSize)
            return false;

        std::unique_ptr<std::uint8_t[]> payload(new std::uint8_t[kPageSize]);
        std::memcpy(payload.get(), from->payload.get(), kPageSize);
        auto& table = tables_[static_cast<std::size_t>(page / kPagesPerTable)];
        if (!table) {
            table = std::make_unique<PageTable>();
            ++allocated_tables_;
        }
        auto& destination = table->slots[static_cast<std::size_t>(page % kPagesPerTable)];
        destination.clear();
        destination.payload = std::move(payload);
        ++table->resident;
        ++allocated_pages_;
        if (backing_) from->touch();
        return true;
    }

    // Copy the exact compact representation into an independent slot.
    bool try_copy_compact_page(std::uint64_t source, std::uint64_t target) {
        check_range(source, kPageSize);
        check_range(target, kPageSize);
        if ((source % kPageSize) || (target % kPageSize) || has_pinned_writes()) return false;

        const auto* from = find_slot(source / kPageSize);
        if (from && (from->has_dense() || from->spilled)) return false;
        return replace_compact_page(target, from ? from->pattern : 0,
                                    from ? from->word_index : kRepeating,
                                    from ? from->payload.get() : nullptr);
    }

    // The pattern is a byte array held in native storage order.
    bool try_fill_repeating_page(std::uint64_t target, std::uint64_t pattern) {
        check_range(target, kPageSize);
        if (target % kPageSize || has_pinned_writes()) return false;
        return replace_compact_page(target, pattern, kRepeating);
    }

    void fill(std::uint64_t offset, std::uint8_t value, std::size_t count) {
        check_range(offset, count);
        if (value != 0) prepare_dense_pages(offset, count, true, true);
        else if (compact_bytes() || spilled_pages_)
            prepare_dense_pages(offset, count, false);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            auto& table = tables_[static_cast<std::size_t>(offset / kPageSize / kPagesPerTable)];
            if (table) {
                auto& slot = table->slots[static_cast<std::size_t>(offset / kPageSize % kPagesPerTable)];
                if (slot.has_dense()) {
                    const auto* first = slot.payload.get() + inside;
                    if (slot.backing_current &&
                        !std::all_of(first, first + chunk, [value](auto byte) { return byte == value; }))
                        discard_clean_backing(offset / kPageSize, slot);
                    std::memset(slot.payload.get() + inside, value, chunk);
                    slot.nonrepeating = false;
                } else if (slot.has_compact() || slot.spilled) {
                    // All partially covered pages were expanded by preflight.
                    if (slot.spilled) discard_spill(offset / kPageSize);
                    else erase_compact(slot);
                    slot.clear();
                    --table->resident;
                    release_empty_table(table);
                }
            }
            offset += chunk;
            count -= chunk;
        }
    }

    // Every destination becomes resident together. On failure guest bytes
    // remain intact, but pages already moved to optional backing can stay there.
    // Absent pages also become resident so a transaction can undo later writes.
    void prepare_write(std::uint64_t offset, std::size_t count) {
        check_range(offset, count);
        prepare_dense_pages(offset, count, true);
    }

    // Rollback never allocates and therefore still works at a full budget.
    // Compact/implicit pages can only be unchanged by the caller's write.
    // Do not compact, trim, reset or move RAM while a CPU journal is live.
    bool restore_byte(std::uint64_t offset, std::uint8_t old_value) noexcept {
        if (offset >= logical_bytes_) return false;
        const auto page = offset / kPageSize;
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        auto* slot = find_slot(page);
        if (slot && slot->has_dense()) {
            if (slot->backing_current && slot->payload[inside] != old_value)
                discard_clean_backing(page, *slot);
            slot->payload[inside] = old_value;
            slot->nonrepeating = false;
            return true;
        }
        return (!slot || !slot->spilled) && (slot ? slot_byte(*slot, inside) : 0) == old_value;
    }

    // Explicit lifecycle operations; never called automatically by writes.
    // reset_zero/clear discard backing while preserving size and budget.
    void reset_zero() noexcept {
        for (auto& table : tables_) table.reset();
        allocated_pages_ = allocated_tables_ = pattern_pages_ = sparse_word_pages_ = compaction_page_ = 0;
        pattern_delta_pages_ = pattern_delta_bytes_ = 0;
        spilled_pages_ = backing_writes_ = eviction_page_ = compact_eviction_page_ = 0;
        backing_clean_pages_ = backing_clean_reclaims_ = 0;
        clean_history_size_ = clean_history_next_ = 0;
        preparation_page_ = 0;
        pressure_ = {};
        if (backing_) {
            backing_cache_->clear();
            backing_cache_->reads.store(0, std::memory_order_relaxed);
            backing_cache_->hits.store(0, std::memory_order_relaxed);
            backing_cache_->timed_attempts.store(0, std::memory_order_relaxed);
            backing_cache_->read_failures.store(0, std::memory_order_relaxed);
            backing_cache_->read_ns.store(0, std::memory_order_relaxed);
            backing_cache_->read_max_ns.store(0, std::memory_order_relaxed);
            backing_cache_->lock_wait_ns.store(0, std::memory_order_relaxed);
            backing_cache_->overwrite_pages = 0;
            backing_->reset();
        }
    }
    void clear() noexcept { reset_zero(); }

    std::size_t trim_zero_pages() noexcept {
        if (has_pinned_writes()) return 0;
        std::size_t removed = 0;
        for (auto& table : tables_) {
            if (!table) continue;
            for (std::size_t index = 0; index < kPagesPerTable; ++index) {
                auto& slot = table->slots[index];
                if (slot.has_dense() && !slot.nonrepeating &&
                    std::all_of(slot.payload.get(), slot.payload.get() + kPageSize,
                                             [](std::uint8_t byte) { return byte == 0; })) {
                    if (slot.backing_current) discard_clean_backing(page_index(table, index), slot);
                    slot.clear();
                    --table->resident;
                    ++removed;
                }
            }
            release_empty_table(table);
        }
        allocated_pages_ -= removed;
        return removed;
    }

    // Call between CPU transactions. Walk at most max_pages resident page
    // slots, resuming from the last address, and skip unallocated tables.
    std::size_t compact_repeating_pages(std::size_t max_pages) noexcept {
        if (!max_pages || !allocated_pages_ || has_pinned_writes()) return 0;
        const auto pages = logical_bytes_ / kPageSize + (logical_bytes_ % kPageSize != 0);
        auto remaining = pages;
        std::size_t compacted = 0;
        while (remaining && max_pages && allocated_pages_) {
            auto& table = tables_[static_cast<std::size_t>(compaction_page_ / kPagesPerTable)];
            const auto index = static_cast<std::size_t>(compaction_page_ % kPagesPerTable);
            const auto span = std::min({remaining, pages - compaction_page_,
                                        std::uint64_t(kPagesPerTable - index)});
            auto visited = span;
            if (table) {
                const auto stop = std::min<std::uint64_t>(span, max_pages);
                visited = 0;
                // The last resident slot can release this table while compacting.
                while (visited < stop && table && allocated_pages_) {
                    compacted += compact_page(table, index + static_cast<std::size_t>(visited));
                    ++visited;
                }
                max_pages -= static_cast<std::size_t>(visited);
            }
            compaction_page_ += visited;
            if (compaction_page_ == pages) compaction_page_ = 0;
            remaining -= visited;
        }
        return compacted;
    }

    // The caller knows which physical page it just filled. As with the scan,
    // the commit is allocation-free and must sit outside any write journal.
    bool compact_repeating_page(std::uint64_t offset) noexcept {
        if (offset >= logical_bytes_ || has_pinned_writes()) return false;
        const auto page = offset / kPageSize;
        auto& table = tables_[static_cast<std::size_t>(page / kPagesPerTable)];
        return table && compact_page(table, static_cast<std::size_t>(page % kPagesPerTable));
    }

private:
    using Page = std::array<std::uint8_t, kPageSize>;
    static constexpr std::uint16_t kRepeating = kPageSize / kPatternSize;
    static constexpr std::size_t kMaxChangedWords = 32;
    static constexpr std::size_t kPatchIndexSize = sizeof(std::uint16_t);
    static constexpr std::size_t kPatchSize = kPatchIndexSize + kPatternSize;
    static_assert(2 * kMaxChangedWords + 1 <= kRepeating);
    struct PageSlot {
        static constexpr std::uint8_t kWritten = 1, kRead = 2;

        // Dense bytes or sorted pairs of (word index, exact word bytes).
        std::unique_ptr<std::uint8_t[]> payload;
        std::uint64_t pattern = 0;
        std::uint32_t write_epoch = 0;
        // Below kRepeating: one word on zero. Above: number of changed words.
        std::uint16_t word_index = kRepeating;
        mutable std::atomic<std::uint8_t> recency{0};
        bool spilled : 1;
        // Reject unchanged dense pages after one unsuccessful pattern check.
        bool nonrepeating : 1;
        // A dense promotion still exactly matches its copy in storage.
        bool backing_current : 1;

        PageSlot() noexcept : spilled(false), nonrepeating(false), backing_current(false) {}

        void touch() const noexcept {
            if (recency.load(std::memory_order_relaxed) != kRead)
                recency.store(kRead, std::memory_order_relaxed);
        }
        void mark_written() noexcept {
            if (!recency.load(std::memory_order_relaxed))
                recency.store(kWritten, std::memory_order_relaxed);
        }
        bool give_second_chance() noexcept {
            const auto heat = recency.load(std::memory_order_relaxed);
            if (!heat) return false;
            recency.store(heat - 1, std::memory_order_relaxed);
            return true;
        }
        void clear() noexcept {
            payload.reset();
            pattern = write_epoch = 0;
            word_index = kRepeating;
            recency.store(0, std::memory_order_relaxed);
            spilled = false;
            nonrepeating = false;
            backing_current = false;
        }
        bool has_dense() const noexcept { return payload && word_index == kRepeating; }
        bool has_compact() const noexcept { return pattern || word_index > kRepeating; }
        std::size_t patch_count() const noexcept {
            return word_index > kRepeating ? word_index - kRepeating : 0;
        }
        std::size_t compact_size() const noexcept {
            return has_compact() ? kPatternSize + patch_count() * kPatchSize : 0;
        }
    };
    // One state vector for each populated range of guest physical pages.
    struct PageTable {
        std::array<PageSlot, kPagesPerTable> slots{};
        std::size_t resident = 0;
    };
    static std::uint64_t elapsed_ns(ReclaimClock::time_point start) noexcept {
        return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
            ReclaimClock::now() - start).count());
    }

    // An inactive timer does not read the clock. Only pressure/miss paths
    // create active timers; ordinary resident accesses keep their fast path.
    struct PressureTimer {
        explicit PressureTimer(std::uint64_t* total) noexcept : total(total) {
            if (total) start = ReclaimClock::now();
        }
        ~PressureTimer() { finish(); }
        PressureTimer(const PressureTimer&) = delete;
        PressureTimer& operator=(const PressureTimer&) = delete;
        void finish() noexcept {
            if (!total) return;
            *total += elapsed_ns(start);
            total = nullptr;
        }
        std::uint64_t* total;
        ReclaimClock::time_point start;
    };

    struct BackingCache {
        static constexpr std::uint64_t kUnused = UINT64_MAX;
        static constexpr std::size_t kBytesPerSet = 2 * kPageSize;
        static constexpr std::size_t kAdaptiveSets = 256;
        struct Entry {
            std::uint64_t page = kUnused;
            std::unique_ptr<std::uint8_t[]> bytes;
        };
        struct Set {
            std::mutex mutex;
            std::array<Entry, 2> entries;
            unsigned newest = 0;
        };

        explicit BackingCache(BackingCachePolicy policy)
            : adaptive(policy == BackingCachePolicy::Adaptive), sets(adaptive ? kAdaptiveSets : 8) {}

        std::size_t sets_for(std::uint64_t budget, std::uint64_t unused) const noexcept {
            if (!adaptive) return sets.size();
            // Reserve at most 1/128 of the resident budget, up to 2 MiB.
            return static_cast<std::size_t>(std::min<std::uint64_t>(sets.size(),
                std::min(budget / 128, unused) / kBytesPerSet));
        }
        std::uint64_t reserved_bytes(std::size_t count) const noexcept {
            return adaptive ? count * kBytesPerSet : 0;
        }
        std::uint64_t payload_bytes() const noexcept {
            return allocated.load(std::memory_order_relaxed) * kPageSize;
        }
        std::uint64_t storage_bytes() const noexcept {
            return sizeof(*this) + sets.capacity() * sizeof(Set) + payload_bytes();
        }
        void clear() noexcept {
            for (std::size_t i = 0; i < active_sets; ++i) {
                sets[i].entries = {};
                sets[i].newest = 0;
            }
            allocated.store(0, std::memory_order_relaxed);
        }
        void release_unused(ReclaimClock::time_point deadline) noexcept {
            if (!allocated.load(std::memory_order_relaxed)) return;
            std::size_t released = 0;
            for (std::size_t i = 0; i < active_sets; ++i) {
                if (i % 16 == 0 && ReclaimClock::now() >= deadline) break;
                for (auto& entry : sets[i].entries) {
                    if (entry.page != kUnused || !entry.bytes) continue;
                    entry.bytes.reset();
                    ++released;
                }
            }
            allocated.fetch_sub(released, std::memory_order_relaxed);
        }
        void resize(std::size_t count) noexcept {
            if (count == active_sets) return;
            if (!count || !active_sets) {
                clear();
                active_sets = count;
                return;
            }
            // The owner has joined readers before changing the budget. Move
            // decoded pages, not their bytes, so rehashing never allocates a
            // second payload. Invalidated buffers are not cache entries.
            std::array<Entry, 2 * kAdaptiveSets> previous{};
            const auto old_sets = active_sets;
            for (std::size_t i = 0; i < old_sets; ++i) {
                auto& set = sets[i];
                previous[i] = std::move(set.entries[1 - set.newest]);
                previous[old_sets + i] = std::move(set.entries[set.newest]);
                set.entries = {};
                set.newest = 0;
            }
            active_sets = count;
            std::size_t retained = 0;
            // Insert each old set's newer entry last. Collisions may still
            // evict pages, but shrinking favors those recently reused.
            for (std::size_t i = 0; i < 2 * old_sets; ++i) {
                auto& entry = previous[i];
                if (entry.page == kUnused || !entry.bytes) continue;
                auto& set = set_for(entry.page);
                unsigned index = 1 - set.newest;
                if (set.entries[set.newest].page == kUnused) index = set.newest;
                if (!set.entries[index].bytes) ++retained;
                set.entries[index] = std::move(entry);
                set.newest = index;
            }
            allocated.store(retained, std::memory_order_relaxed);
        }
        Set& set_for(std::uint64_t page) noexcept {
            return sets[(page ^ (page >> 9) ^ (page >> 18)) % active_sets];
        }
        void invalidate(std::uint64_t page) noexcept {
            if (!active_sets) return;
            for (auto& entry : set_for(page).entries)
                if (entry.page == page) entry.page = kUnused;
        }
        template <bool Synchronized>
        void read(std::uint64_t page, PageBacking& backing, std::size_t inside,
                  std::uint8_t* destination, std::size_t count) {
            if (!active_sets) return read_uncached(page, backing, inside, destination, count);
            auto& set = set_for(page);
            if constexpr (Synchronized) {
                // Keep the decoded bytes valid until this reader consumes them.
                std::lock_guard<std::mutex> lock(set.mutex);
                read_set<true>(set, page, backing, inside, destination, count);
            } else {
                read_set<false>(set, page, backing, inside, destination, count);
            }
        }
        template <bool Synchronized>
        void read_set(Set& set, std::uint64_t page, PageBacking& backing,
                      std::size_t inside, std::uint8_t* destination, std::size_t count) {
            for (unsigned index = 0; index < set.entries.size(); ++index) {
                auto& entry = set.entries[index];
                if (entry.page != page) continue;
                set.newest = index;
                if constexpr (Synchronized) hits.fetch_add(1, std::memory_order_relaxed);
                else hits.store(hits.load(std::memory_order_relaxed) + 1, std::memory_order_relaxed);
                std::memcpy(destination, entry.bytes.get() + inside, count);
                return;
            }
            unsigned index = 1 - set.newest;
            if (set.entries[set.newest].page == kUnused) index = set.newest;
            auto& entry = set.entries[index];
            if (!entry.bytes) {
                entry.bytes.reset(new (std::nothrow) std::uint8_t[kPageSize]);
                if (!entry.bytes) return read_uncached(page, backing, inside, destination, count);
                allocated.fetch_add(1, std::memory_order_relaxed);
            }
            entry.page = kUnused;
            read_storage(page, backing, entry.bytes.get());
            entry.page = page;
            set.newest = index;
            std::memcpy(destination, entry.bytes.get() + inside, count);
        }
        void read_uncached(std::uint64_t page, PageBacking& backing, std::size_t inside,
                           std::uint8_t* destination, std::size_t count) {
            Page buffer;
            read_storage(page, backing, buffer.data());
            std::memcpy(destination, buffer.data() + inside, count);
        }
        void read_timed_storage(std::uint64_t page, PageBacking& backing, std::uint8_t* buffer) {
            const auto waiting = ReclaimClock::now();
            std::lock_guard<std::mutex> lock(storage_mutex);
            const auto acquired = ReclaimClock::now();
            add_serialized(lock_wait_ns, static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(acquired - waiting).count()));
            add_serialized(timed_attempts, 1);
            const auto record_time = [&] {
                const auto elapsed = elapsed_ns(acquired);
                add_serialized(read_ns, elapsed);
                // storage_mutex also serializes updates to this maximum.
                read_max_ns.store(std::max(read_max_ns.load(std::memory_order_relaxed), elapsed),
                                  std::memory_order_relaxed);
            };
            try {
                backing.read(page, buffer);
            } catch (...) {
                record_time();
                add_serialized(read_failures, 1);
                throw;
            }
            record_time();
            add_serialized(reads, 1);
        }
        void read_storage(std::uint64_t page, PageBacking& backing, std::uint8_t* buffer) {
            if (read_timing_enabled) return read_timed_storage(page, backing, buffer);
            // A PageBacking can share mutable I/O or codec state. The mutex
            // serializes counter writers too: no clock or atomic RMW is needed
            // for an ordinary untimed cache miss. Snapshot reads stay atomic.
            std::lock_guard<std::mutex> lock(storage_mutex);
            try {
                backing.read(page, buffer);
            } catch (...) {
                add_serialized(read_failures, 1);
                throw;
            }
            add_serialized(reads, 1);
        }
        static void add_serialized(std::atomic<std::uint64_t>& counter, std::uint64_t value) noexcept {
            // All callers hold storage_mutex; reset requires joined readers.
            counter.store(counter.load(std::memory_order_relaxed) + value, std::memory_order_relaxed);
        }
        const bool adaptive;
        std::vector<Set> sets;
        std::mutex storage_mutex;
        std::size_t active_sets = 0;
        std::atomic<std::size_t> allocated{0};
        std::atomic<std::uint64_t> reads{0}, hits{0};
        std::atomic<std::uint64_t> timed_attempts{0}, read_failures{0}, read_ns{0};
        std::atomic<std::uint64_t> read_max_ns{0}, lock_wait_ns{0};
        bool read_timing_enabled = false;
        std::uint64_t overwrite_pages = 0;
    };

    std::uint64_t logical_bytes_ = 0;
    std::uint64_t payload_cap_bytes_ = 0;
    std::vector<std::unique_ptr<PageTable>> tables_;
    std::size_t allocated_pages_ = 0;
    std::size_t allocated_tables_ = 0;
    std::size_t pattern_pages_ = 0;
    std::size_t sparse_word_pages_ = 0;
    std::size_t pattern_delta_pages_ = 0;
    std::uint64_t pattern_delta_bytes_ = 0;
    std::uint64_t compaction_page_ = 0;
    std::unique_ptr<PageBacking> backing_;
    std::unique_ptr<BackingCache> backing_cache_;
    std::size_t spilled_pages_ = 0;
    std::uint64_t backing_writes_ = 0;
    std::size_t backing_clean_pages_ = 0;
    std::uint64_t backing_clean_reclaims_ = 0;
    // Bound optional copies and the work of dropping them at backing capacity.
    std::vector<std::uint64_t> clean_history_;
    std::size_t clean_history_size_ = 0, clean_history_next_ = 0;
    std::uint64_t preparation_page_ = 0;
    std::uint64_t eviction_page_ = 0;
    std::uint64_t compact_eviction_page_ = 0;
    std::uint32_t pin_epoch_ = 0;
    unsigned pin_depth_ = 0;
    PressureMetrics pressure_;
    bool backing_read_timing_enabled_ = false;

    std::uint64_t compact_bytes() const noexcept {
        return static_cast<std::uint64_t>(pattern_pages_ + sparse_word_pages_) * kPatternSize +
               pattern_delta_bytes_;
    }

    std::uint64_t guest_payload_limit() const noexcept {
        return backing_cache_ ? payload_cap_bytes_ - backing_cache_->reserved_bytes(backing_cache_->active_sets)
                              : payload_cap_bytes_;
    }

    template <bool Synchronized = true>
    void read_backed_page(std::uint64_t page, std::size_t inside,
                          std::uint8_t* destination, std::size_t count) const {
        backing_cache_->read<Synchronized>(page, *backing_, inside, destination, count);
    }

    void write_dense_bytes(std::uint64_t page, PageSlot& slot, std::size_t inside,
                           const std::uint8_t* source, std::size_t count,
                           std::uint8_t* previous) noexcept {
        auto* destination = slot.payload.get() + inside;
        if (slot.backing_current && std::memcmp(destination, source, count) != 0)
            discard_clean_backing(page, slot);
        if (previous) std::memcpy(previous, destination, count);
        std::memcpy(destination, source, count);
        slot.nonrepeating = false;
    }

    void mark_write(PageSlot& slot) noexcept {
        slot.mark_written();
        pin_write(slot);
    }

    void pin_write(PageSlot& slot) noexcept {
        if (pin_depth_) slot.write_epoch = pin_epoch_;
    }

    void write_back(std::uint64_t page, const std::uint8_t* bytes, PressureMetrics* pressure = nullptr) {
        const PressureTimer timing(pressure ? &pressure->spill_ns : nullptr);
        try {
            backing_->write(page, bytes);
        } catch (const PageBacking::CapacityExceeded&) {
            if (!backing_clean_pages_) throw;
            for (std::size_t i = 0; i < clean_history_size_; ++i)
                discard_clean_backing_at(clean_history_[i]);
            clean_history_size_ = clean_history_next_ = 0;
            backing_->write(page, bytes);
        }
        backing_cache_->invalidate(page);
        ++spilled_pages_;
        ++backing_writes_;
        if (pressure) ++pressure->spilled_pages;
    }

    void discard_spill(std::uint64_t page) noexcept {
        backing_->discard(page);
        backing_cache_->invalidate(page);
        --spilled_pages_;
    }

    void discard_clean_backing(std::uint64_t page, PageSlot& slot) noexcept {
        backing_->discard(page);
        backing_cache_->invalidate(page);
        slot.backing_current = false;
        --backing_clean_pages_;
    }

    void discard_clean_backing_at(std::uint64_t page) noexcept {
        auto* slot = find_slot(page);
        if (slot && slot->backing_current) discard_clean_backing(page, *slot);
    }

    void retain_clean_backing(std::uint64_t page, PageSlot& slot) noexcept {
        if (clean_history_size_ == clean_history_.size())
            discard_clean_backing_at(clean_history_[clean_history_next_]);
        else ++clean_history_size_;
        clean_history_[clean_history_next_] = page;
        clean_history_next_ = (clean_history_next_ + 1) % clean_history_.size();
        slot.backing_current = true;
        ++backing_clean_pages_;
    }

    bool try_reclaim_clean_backing(PageSlot& slot) noexcept {
        if (!slot.backing_current) return false;
        make_clean_page_spilled(slot);
        record_clean_reclaims(1);
        return true;
    }

    static void make_clean_page_spilled(PageSlot& slot) noexcept {
        // This is already a dense page with a valid backing copy. As with
        // ordinary spill, later promotion initializes the dense slot anew.
        slot.payload.reset();
        slot.backing_current = false;
        slot.spilled = true;
    }

    void record_clean_reclaims(std::size_t count) noexcept {
        backing_clean_pages_ -= count;
        allocated_pages_ -= count;
        spilled_pages_ += count;
        backing_clean_reclaims_ += count;
    }

    struct ReclaimDeadline {
        ReclaimClock::time_point deadline;
        unsigned scanned = 0;
        bool yielded = false;

        bool expired() const noexcept { return yielded || ReclaimClock::now() >= deadline; }
        bool expired_during_scan() noexcept { return (++scanned % 64) == 0 && expired(); }
    };

    std::size_t reclaim_cold_backed_pages(std::uint64_t target, std::size_t limit,
                                          ReclaimDeadline& deadline) noexcept {
        // Promotion history is bounded. It can avoid walking the address
        // space or writing dirty pages when cold resident copies suffice.
        const auto used = allocated_bytes();
        if (!backing_clean_pages_ || used <= target || deadline.expired()) return 0;
        const auto entries = clean_history_.size();
        auto index = (clean_history_next_ + entries - clean_history_size_) % entries;
        const auto pages_needed = std::min<std::uint64_t>(
            {limit, backing_clean_pages_, (used - target - 1) / kPageSize + 1});
        std::size_t reclaimed = 0;
        for (std::size_t i = 0; i < clean_history_size_ && reclaimed < pages_needed; ++i) {
            if (deadline.expired_during_scan()) break;
            auto* slot = find_slot(clean_history_[index]);
            if (++index == entries) index = 0;
            // Let the normal clock hand age live reads and writes.
            if (!slot || !slot->backing_current || slot->recency.load(std::memory_order_relaxed)) continue;
            make_clean_page_spilled(*slot);
            ++reclaimed;
        }
        record_clean_reclaims(reclaimed);
        return reclaimed;
    }

    std::uint64_t skip_empty_tables(std::uint64_t& page, std::uint64_t pages,
                                    std::uint64_t remaining) const noexcept {
        const auto start = page;
        const auto table = page / kPagesPerTable;
        // Bound each pointer scan so a sparse address space still yields at
        // the next deadline check. The first table was already found empty.
        const auto end = std::min({pages, page + remaining, (table + 64) * kPagesPerTable});
        const auto last = (end - 1) / kPagesPerTable + 1;
        auto next = table + 1;
        while (next < last && !tables_[next]) ++next;
        const auto stop = std::min(end, next * kPagesPerTable);
        page = stop == pages ? 0 : stop;
        return stop - start;
    }

    bool spill_compact_page(std::uint64_t compact_limit, std::uint64_t first = UINT64_MAX,
                            std::uint64_t last = UINT64_MAX, ReclaimDeadline* deadline = nullptr,
                            PressureMetrics* pressure = nullptr) {
        PressureTimer selection(pressure ? &pressure->selection_ns : nullptr);
        if (!backing_ || compact_bytes() <= compact_limit) return false;
        const auto pages = logical_bytes_ / kPageSize + (logical_bytes_ % kPageSize != 0);
        for (auto remaining = pages; remaining;) {
            if (deadline && deadline->expired_during_scan()) return false;
            const auto page = compact_eviction_page_;
            auto* slot = find_slot(page);
            if (!slot) {
                if (deadline && deadline->expired()) return false;
                remaining -= skip_empty_tables(compact_eviction_page_, pages, remaining);
                continue;
            }
            if (++compact_eviction_page_ == pages) compact_eviction_page_ = 0;
            --remaining;
            if (!slot->has_compact() || (page >= first && page <= last)) continue;
            Page bytes;
            copy_compact(*slot, 0, bytes.data(), bytes.size());
            selection.finish();
            write_back(page, bytes.data(), pressure);
            erase_compact(*slot);
            slot->clear();
            slot->spilled = true;
            return true;
        }
        return false;
    }

    bool evict_page(std::uint64_t first, std::uint64_t last, std::uint64_t compact_limit,
                    ReclaimDeadline* deadline = nullptr, PressureMetrics* pressure = nullptr) {
        PageBacking::PageWrite write{};
        {
            const PressureTimer selection(pressure ? &pressure->selection_ns : nullptr);
            if (!select_reclaim_page(write, first, last, compact_limit, deadline, UINT64_MAX, pressure))
                return false;
        }
        if (write.bytes) spill_dense_page(write, pressure);
        return true;
    }

    std::size_t evict_dense_group(std::uint64_t target, std::size_t limit, ReclaimDeadline* deadline) {
        std::array<PageBacking::PageWrite, PageBacking::kMaxWritePages> writes;
        std::size_t count = 0, reclaimed = 0;
        while (count < writes.size() && reclaimed < limit &&
               allocated_bytes() - count * kPageSize > target && !(deadline && deadline->expired())) {
            PageBacking::PageWrite write{};
            if (!select_reclaim_page(write, UINT64_MAX, UINT64_MAX, target, deadline,
                                     count ? writes.front().page : UINT64_MAX)) break;
            if (write.bytes) writes[count++] = write;
            ++reclaimed;
        }
        if (!count) return reclaimed;
        if (count == 1 && !deadline) {
            spill_dense_page(writes.front());
            return reclaimed;
        }
        std::size_t committed;
        try {
            committed = backing_->write_pages(writes.data(), count,
                deadline ? deadline->deadline : ReclaimClock::time_point::max());
        } catch (const std::exception&) {
            // A group can exceed the remaining file capacity even though a
            // prefix fits. Keep that progress and report the first real failure.
            for (std::size_t i = 0; i < count; ++i) {
                spill_dense_page(writes[i]);
                if (i + 1 < count && deadline && deadline->expired()) {
                    eviction_page_ = writes[i + 1].page;
                    return reclaimed - (count - i - 1);
                }
            }
            return reclaimed;
        }
        for (std::size_t i = 0; i < committed; ++i) {
            backing_cache_->invalidate(writes[i].page);
            mark_dense_spilled(writes[i].page);
        }
        spilled_pages_ += committed;
        backing_writes_ += committed;
        if (committed < count) {
            eviction_page_ = writes[committed].page;
            if (deadline) deadline->yielded = true;
        }
        return reclaimed - (count - committed);
    }

    void spill_dense_page(const PageBacking::PageWrite& write, PressureMetrics* pressure = nullptr) {
        write_back(write.page, write.bytes, pressure);
        mark_dense_spilled(write.page);
    }

    void mark_dense_spilled(std::uint64_t page) noexcept {
        auto& slot = *find_slot(page);
        slot.payload.reset();
        slot.spilled = true;
        --allocated_pages_;
    }

    bool select_reclaim_page(PageBacking::PageWrite& write, std::uint64_t first, std::uint64_t last,
                             std::uint64_t compact_limit, ReclaimDeadline* deadline,
                             std::uint64_t first_pending = UINT64_MAX,
                             PressureMetrics* pressure = nullptr) {
        if (!allocated_pages_) return false;
        const auto pages = logical_bytes_ / kPageSize + (logical_bytes_ % kPageSize != 0);
        // Repeated writes get one chance; reads get two. This protects the
        // working set from a sequential fill while still allowing reclamation.
        for (auto remaining = (PageSlot::kRead + 1) * pages; remaining;) {
            if (deadline && deadline->expired_during_scan()) return false;
            if (eviction_page_ == first_pending) return false;
            auto& table = tables_[static_cast<std::size_t>(eviction_page_ / kPagesPerTable)];
            const auto index = static_cast<std::size_t>(eviction_page_ % kPagesPerTable);
            if (!table) {
                if (deadline && deadline->expired()) return false;
                remaining -= skip_empty_tables(eviction_page_, pages, remaining);
                continue;
            }
            const auto page = eviction_page_;
            if (++eviction_page_ == pages) eviction_page_ = 0;
            --remaining;
            auto& slot = table->slots[index];
            if (!slot.has_dense() || (page >= first && page <= last) ||
                (pin_depth_ && slot.write_epoch == pin_epoch_)) continue;
            if (slot.give_second_chance()) continue;
            if (try_reclaim_clean_backing(slot)) {
                if (pressure) ++pressure->clean_reclaims;
                return true;
            }
            const auto try_compact = [&] {
                const auto compact_room = compact_limit - std::min(compact_limit, compact_bytes());
                if (!slot.nonrepeating && (compact_room >= kPatternSize ||
                     std::all_of(slot.payload.get(), slot.payload.get() + kPageSize,
                                 [](auto byte) { return byte == 0; })) &&
                    compact_page(table, index)) return true;
                return compact_room >= kPatternSize && compact_changed_words(slot, compact_room);
            };
            {
                const PressureTimer compact(pressure ? &pressure->compact_ns : nullptr);
                if (pressure) ++pressure->compact_attempts;
                if (try_compact()) {
                    if (pressure) ++pressure->compacted_pages;
                    return true;
                }
            }
            if (!backing_) continue;
            write = {page, slot.payload.get()};
            return true;
        }
        return false;
    }

    static std::uint8_t slot_byte(const PageSlot& slot, std::size_t inside) noexcept {
        if (slot.has_dense()) return slot.payload[inside];
        const auto word = inside / kPatternSize;
        if (slot.word_index < kRepeating && word != slot.word_index) return 0;
        std::size_t first = 0, last = slot.patch_count();
        while (first < last) {
            const auto middle = first + (last - first) / 2;
            const auto* patch = slot.payload.get() + middle * kPatchSize;
            const auto index = patch_index(patch);
            if (word == index) return patch[kPatchIndexSize + inside % kPatternSize];
            if (word < index) last = middle;
            else first = middle + 1;
        }
        return reinterpret_cast<const unsigned char*>(&slot.pattern)[inside % kPatternSize];
    }

    static std::uint16_t patch_index(const std::uint8_t* patch) noexcept {
        std::uint16_t index;
        std::memcpy(&index, patch, kPatchIndexSize);
        return index;
    }

    static void copy_compact(const PageSlot& slot, std::size_t inside,
                             std::uint8_t* destination, std::size_t count) noexcept {
        if (slot.word_index < kRepeating) {
            std::memset(destination, 0, count);
            copy_word_overlap(reinterpret_cast<const unsigned char*>(&slot.pattern),
                              slot.word_index, inside, destination, count);
            return;
        }
        copy_pattern(slot.pattern, inside, destination, count);
        for (std::size_t i = 0; i < slot.patch_count(); ++i) {
            const auto* patch = slot.payload.get() + i * kPatchSize;
            const auto index = patch_index(patch);
            if (index * kPatternSize >= inside + count) break;
            copy_word_overlap(patch + kPatchIndexSize, index, inside, destination, count);
        }
    }

    static void copy_word_overlap(const unsigned char* bytes, std::size_t index, std::size_t inside,
                                  std::uint8_t* destination, std::size_t count) noexcept {
        const auto word = index * kPatternSize;
        const auto begin = std::max(word, inside), end = std::min(word + kPatternSize, inside + count);
        if (begin < end) std::memcpy(destination + (begin - inside), bytes + (begin - word), end - begin);
    }

    static void copy_pattern(const std::uint64_t& pattern, std::size_t inside,
                             std::uint8_t* destination, std::size_t count) noexcept {
        const auto within = inside % kPatternSize;
        const auto first = std::min(count, kPatternSize - within);
        const auto* source = reinterpret_cast<const unsigned char*>(&pattern);
        std::memcpy(destination, source + within, first);
        const auto next = std::min(count - first, within);
        std::memcpy(destination + first, source, next);
        for (auto copied = first + next; copied < count;) {
            const auto chunk = std::min(copied, count - copied);
            std::memcpy(destination + copied, destination, chunk);
            copied += chunk;
        }
    }

    void release_empty_table(std::unique_ptr<PageTable>& table) noexcept {
        if (table->resident == 0) {
            table.reset();
            --allocated_tables_;
        }
    }

    void record_compact(const PageSlot& slot) noexcept {
        if (slot.patch_count()) {
            ++pattern_delta_pages_;
            pattern_delta_bytes_ += slot.compact_size();
        } else if (slot.word_index == kRepeating) ++pattern_pages_;
        else ++sparse_word_pages_;
    }

    void erase_compact(const PageSlot& slot) noexcept {
        if (slot.patch_count()) {
            --pattern_delta_pages_;
            pattern_delta_bytes_ -= slot.compact_size();
        } else if (slot.word_index == kRepeating) --pattern_pages_;
        else --sparse_word_pages_;
    }

    bool replace_compact_page(std::uint64_t target, std::uint64_t pattern, std::uint16_t word_index,
                              const std::uint8_t* patches = nullptr) {
        const auto page = target / kPageSize;
        auto& table = tables_[static_cast<std::size_t>(page / kPagesPerTable)];
        const auto index = static_cast<std::size_t>(page % kPagesPerTable);
        if (!table && !pattern && !patches) return true;
        const auto* previous = table ? &table->slots[index] : nullptr;
        const auto old_bytes = !previous ? 0 : previous->has_dense() ? kPageSize : previous->compact_size();
        const auto retained = allocated_bytes() - old_bytes;
        const auto patch_bytes = patches ? (word_index - kRepeating) * kPatchSize : 0;
        const auto new_bytes = pattern || patches ? kPatternSize + patch_bytes : 0;
        const auto limit = guest_payload_limit();
        if (retained > limit || new_bytes > limit - retained) return false;
        std::unique_ptr<std::uint8_t[]> payload;
        if (patches) {
            payload = std::make_unique<std::uint8_t[]>(patch_bytes);
            std::memcpy(payload.get(), patches, patch_bytes);
        }
        if (!table) {
            table = std::make_unique<PageTable>();
            ++allocated_tables_;
        }
        auto& slot = table->slots[index];
        if (slot.has_dense() || slot.has_compact() || slot.spilled) --table->resident;
        if (slot.has_dense()) --allocated_pages_;
        if (slot.has_compact()) erase_compact(slot);
        if (slot.spilled) discard_spill(page);
        if (slot.backing_current) discard_clean_backing(page, slot);
        slot.clear();
        if (new_bytes) {
            slot.pattern = pattern;
            slot.word_index = word_index;
            slot.payload = std::move(payload);
            record_compact(slot);
            ++table->resident;
        }
        release_empty_table(table);
        return true;
    }

    // Only reclaim cold pages this way. A CPU write still gets a dense
    // destination for its rollback journal.
    template <std::size_t MaxChangedWords = kMaxChangedWords>
    bool compact_changed_words(PageSlot& slot, std::uint64_t compact_room) noexcept {
        const auto* bytes = slot.payload.get();
        // Small deltas have a majority even in a short prefix. A wider pass
        // can also use zero when it covers enough words to fit the envelope.
        constexpr auto prefix = std::min<std::size_t>(kRepeating, 2 * MaxChangedWords + 1);
        std::uint64_t pattern = 0;
        unsigned votes = 0, zeros = 0;
        for (std::size_t word = 0; word < prefix; ++word) {
            const auto value = load_word(bytes + word * kPatternSize);
            if constexpr (prefix == kRepeating) zeros += value == 0;
            if (votes == 0) {
                pattern = value;
                votes = 1;
            } else if (value == pattern) ++votes;
            else --votes;
        }
        if constexpr (prefix == kRepeating) {
            if (zeros >= kRepeating - MaxChangedWords) pattern = 0;
        }

        std::array<std::uint8_t, MaxChangedWords * kPatchSize> patches;
        std::size_t changed = 0;
        for (std::uint16_t word = 0; word < kRepeating; ++word) {
            const auto value = load_word(bytes + word * kPatternSize);
            if (value == pattern) continue;
            if (changed == MaxChangedWords) return false;
            auto* patch = patches.data() + changed * kPatchSize;
            std::memcpy(patch, &word, kPatchIndexSize);
            std::memcpy(patch + kPatchIndexSize, &value, kPatternSize);
            ++changed;
        }
        if (!changed) return false;
        if (changed == 1 && pattern == 0) {
            slot.pattern = load_word(patches.data() + kPatchIndexSize);
            slot.word_index = patch_index(patches.data());
            slot.payload.reset();
        } else {
            const auto patch_bytes = changed * kPatchSize;
            if (kPatternSize + patch_bytes > compact_room) return false;
            std::unique_ptr<std::uint8_t[]> payload(new (std::nothrow) std::uint8_t[patch_bytes]);
            if (!payload) return false;
            std::memcpy(payload.get(), patches.data(), patch_bytes);
            slot.pattern = pattern;
            slot.word_index = static_cast<std::uint16_t>(kRepeating + changed);
            slot.payload = std::move(payload);
        }
        --allocated_pages_;
        record_compact(slot);
        return true;
    }

    static std::uint64_t load_word(const std::uint8_t* bytes) noexcept {
        std::uint64_t value;
        std::memcpy(&value, bytes, kPatternSize);
        return value;
    }

    bool compact_page(std::unique_ptr<PageTable>& table, std::size_t index) noexcept {
        auto& slot = table->slots[index];
        if (!slot.has_dense() || slot.nonrepeating) return false;
        if (std::memcmp(slot.payload.get(), slot.payload.get() + kPatternSize,
                        kPageSize - kPatternSize) != 0) {
            slot.nonrepeating = true;
            return false;
        }
        slot.pattern = load_word(slot.payload.get());
        if (slot.backing_current) discard_clean_backing(page_index(table, index), slot);
        slot.payload.reset();
        --allocated_pages_;
        if (slot.pattern) ++pattern_pages_;
        else --table->resident;
        release_empty_table(table);
        return true;
    }

    std::uint64_t page_index(const std::unique_ptr<PageTable>& table, std::size_t index) const noexcept {
        return static_cast<std::uint64_t>(&table - tables_.data()) * kPagesPerTable + index;
    }

    void prepare_dense_pages(std::uint64_t offset, std::size_t count, bool include_implicit,
                             bool replace_full_pages = false) {
        if (count == 0) return;
        const std::uint64_t first = offset / kPageSize;
        const std::uint64_t last = (offset + static_cast<std::uint64_t>(count) - 1) / kPageSize;
        const auto partial_page = [&](std::uint64_t page) {
            return (page == first && offset % kPageSize != 0) ||
                   (page == last && (offset + count) % kPageSize != 0);
        };
        const auto replaces_page = [&](std::uint64_t page) {
            return replace_full_pages && !partial_page(page);
        };
        std::size_t missing_pages = 0;
        std::uint64_t expanded_compact_bytes = 0;
        for (std::uint64_t page = first; page <= last; ++page) {
            auto* slot = find_slot(page);
            if (slot && slot->has_dense()) {
                if (backing_) mark_write(*slot);
                continue;
            }
            if (include_implicit || (slot && (slot->has_compact() || slot->spilled) && partial_page(page))) {
                ++missing_pages;
                if (slot) expanded_compact_bytes += slot->compact_size();
            }
        }
        if (missing_pages == 0) return;
        const auto limit = guest_payload_limit();
        const auto fits = [&] {
            const auto retained = allocated_bytes() - expanded_compact_bytes;
            return retained <= limit && missing_pages <= (limit - retained) / kPageSize;
        };
        if (!fits()) {
            ++pressure_.forced_reclaims;
            // These are pages that need a new dense representation, including
            // compact expansion and write promotion, not total pages touched.
            pressure_.requested_pages += missing_pages;
            const auto before = allocated_bytes();
            const auto started = ReclaimClock::now();
            const auto finish = [&](bool admitted) {
                const auto elapsed = elapsed_ns(started);
                pressure_.forced_reclaim_ns += elapsed;
                pressure_.forced_reclaim_max_ns = std::max(pressure_.forced_reclaim_max_ns, elapsed);
                pressure_.reclaimed_bytes += before - std::min(before, allocated_bytes());
                if (!admitted) ++pressure_.forced_failures;
            };
            try {
                if (backing_ && missing_pages <= limit / kPageSize) {
                    const auto compact_limit = limit - missing_pages * kPageSize + expanded_compact_bytes;
                    while (!fits() && (evict_page(first, last, compact_limit, nullptr, &pressure_) ||
                                       spill_compact_page(compact_limit, first, last, nullptr, &pressure_))) {}
                }
            } catch (...) {
                finish(false);
                throw;
            }
            finish(fits());
        }
        if (!fits()) {
            const auto max_extra = std::numeric_limits<std::uint64_t>::max() / kPageSize;
            const auto additional = std::min<std::uint64_t>(missing_pages, max_extra) * kPageSize;
            throw BudgetExceeded(
                "SparseRam payload budget exceeded: allocated=" + std::to_string(allocated_bytes()) +
                ", additional=" + std::to_string(additional) +
                ", budget=" + std::to_string(limit) + " bytes.");
        }

        const std::size_t first_table = static_cast<std::size_t>(first / kPagesPerTable);
        const std::size_t last_table = static_cast<std::size_t>(last / kPagesPerTable);
        std::size_t missing_tables = 0;
        for (std::size_t table = first_table; table <= last_table; ++table) {
            if (include_implicit && !tables_[table]) ++missing_tables;
        }
        try {
            std::vector<std::pair<std::size_t, std::unique_ptr<PageTable>>> new_tables;
            std::vector<std::pair<std::uint64_t, std::unique_ptr<std::uint8_t[]>>> new_pages;
            new_tables.reserve(missing_tables);
            new_pages.reserve(missing_pages);
            for (std::size_t table = first_table; table <= last_table; ++table) {
                if (include_implicit && !tables_[table])
                    new_tables.emplace_back(table, std::make_unique<PageTable>());
            }
            for (std::uint64_t page = first; page <= last; ++page) {
                const auto* slot = find_slot(page);
                if (slot && slot->has_dense()) continue;
                if (replaces_page(page)) {
                    // The caller replaces every byte after preflight succeeds.
                    new_pages.emplace_back(page, std::unique_ptr<std::uint8_t[]>(new std::uint8_t[kPageSize]));
                } else if (slot && (slot->has_compact() || slot->spilled) &&
                           (include_implicit || partial_page(page))) {
                    std::unique_ptr<std::uint8_t[]> dense(new std::uint8_t[kPageSize]);
                    if (slot->spilled) {
                        ++pressure_.write_promotion_attempts;
                        const PressureTimer promotion(&pressure_.write_promotion_ns);
                        read_backed_page(page, 0, dense.get(), kPageSize);
                    } else copy_compact(*slot, 0, dense.get(), kPageSize);
                    new_pages.emplace_back(page, std::move(dense));
                } else if (include_implicit && (!slot || !slot->has_dense())) {
                    new_pages.emplace_back(page, std::make_unique<std::uint8_t[]>(kPageSize));
                }
            }
            // Everything that can allocate has succeeded. Pointer moves and
            // counters below cannot throw, so publication is all-or-nothing.
            for (auto& table : new_tables) tables_[table.first] = std::move(table.second);
            for (auto& page : new_pages) {
                PageTable& table = *tables_[static_cast<std::size_t>(page.first / kPagesPerTable)];
                auto& slot = table.slots[static_cast<std::size_t>(page.first % kPagesPerTable)];
                const bool reused = !replaces_page(page.first) && (slot.spilled || slot.has_compact());
                const bool clean_backing = slot.spilled && !replaces_page(page.first);
                if (slot.spilled) {
                    if (clean_backing) {
                        --spilled_pages_;
                        ++pressure_.write_promotions;
                    }
                    else {
                        ++backing_cache_->overwrite_pages;
                        discard_spill(page.first);
                    }
                }
                else if (slot.has_compact()) erase_compact(slot);
                else ++table.resident;
                slot.clear();
                slot.payload = std::move(page.second);
                if (clean_backing) retain_clean_backing(page.first, slot);
                if (backing_) {
                    // A first write in a streaming fill should not protect a
                    // page as much as reusing data already in RAM or backing.
                    pin_write(slot);
                    if (reused) slot.touch();
                }
            }
            allocated_tables_ += missing_tables;
            allocated_pages_ += missing_pages;
        } catch (const std::bad_alloc&) {
            throw std::runtime_error("SparseRam host allocation failed while preparing page backing.");
        }
    }

    void check_range(std::uint64_t offset, std::size_t count) const {
        static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
                      "SparseRam requires size_t to fit in uint64_t.");
        if (offset > logical_bytes_ || static_cast<std::uint64_t>(count) > logical_bytes_ - offset) {
            throw std::out_of_range("SparseRam address range outside logical RAM: offset=" +
                                    std::to_string(offset) + ", length=" + std::to_string(count) +
                                    ", logical=" + std::to_string(logical_bytes_) + ".");
        }
    }
    PageSlot* find_slot(std::uint64_t page_index) noexcept {
        const auto& table = tables_[static_cast<std::size_t>(page_index / kPagesPerTable)];
        return table ? &table->slots[static_cast<std::size_t>(page_index % kPagesPerTable)] : nullptr;
    }
    const PageSlot* find_slot(std::uint64_t page_index) const noexcept {
        const auto& table = tables_[static_cast<std::size_t>(page_index / kPagesPerTable)];
        return table ? &table->slots[static_cast<std::size_t>(page_index % kPagesPerTable)] : nullptr;
    }
};

} // namespace cosmic_pc

#endif // COSMIC_PC_SPARSE_RAM_HPP
