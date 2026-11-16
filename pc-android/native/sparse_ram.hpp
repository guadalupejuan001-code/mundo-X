#ifndef COSMIC_PC_SPARSE_RAM_HPP
#define COSMIC_PC_SPARSE_RAM_HPP

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosmic_pc {

// Sparse physical RAM. Absent pages read as zero. Compact pages retain an
// eight-byte repeating pattern, a single word, or a pattern with changed words.
// Logical addresses are independent of the explicitly bounded payload.
// Not thread-safe: the CPU and its host must serialize access.
class SparseRam {
public:
    static constexpr std::size_t kPageSize = 4096;
    static constexpr std::size_t kPatternSize = 8;
    static constexpr std::size_t kPagesPerTable = 512;
    static constexpr std::uint64_t kDefaultPayloadLimit = std::uint64_t{1} << 30;

    enum class BudgetAdjustment { Applied, Reclaiming, Blocked };

    class BudgetExceeded : public std::runtime_error {
    public:
        explicit BudgetExceeded(const std::string& reason) : std::runtime_error(reason) {}
    };

    // The host owns the ephemeral storage. Read and write transfer exactly
    // one full page or throw; an unsuccessful write leaves RAM authoritative.
    class PageBacking {
    public:
        virtual ~PageBacking() = default;
        virtual void read(std::uint64_t page, std::uint8_t* bytes) = 0;
        virtual void write(std::uint64_t page, const std::uint8_t* bytes) = 0;
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
          backing_reads_(std::exchange(other.backing_reads_, 0)),
          backing_writes_(std::exchange(other.backing_writes_, 0)),
          eviction_page_(std::exchange(other.eviction_page_, 0)),
          compact_eviction_page_(std::exchange(other.compact_eviction_page_, 0)),
          pin_epoch_(std::exchange(other.pin_epoch_, 0)) {}
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
            backing_reads_ = std::exchange(other.backing_reads_, 0);
            backing_writes_ = std::exchange(other.backing_writes_, 0);
            eviction_page_ = std::exchange(other.eviction_page_, 0);
            compact_eviction_page_ = std::exchange(other.compact_eviction_page_, 0);
            pin_epoch_ = std::exchange(other.pin_epoch_, 0);
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
    void attach_page_backing(std::unique_ptr<PageBacking> backing) {
        if (!backing || backing_) throw std::invalid_argument("SparseRam expects one page backing.");
        backing_cache_ = std::make_unique<BackingCache>();
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
    BudgetAdjustment adjust_payload_budget(std::uint64_t target, std::size_t reclaim_pages) {
        if (has_pinned_writes()) return BudgetAdjustment::Blocked;
        if (target < allocated_bytes()) {
            for (std::size_t i = 0; i < reclaim_pages && allocated_bytes() > target; ++i)
                if (!evict_page(UINT64_MAX, UINT64_MAX, target) && !spill_compact_page(target))
                    return BudgetAdjustment::Blocked;
            const auto used = allocated_bytes();
            if (used > target) {
                if (backing_ && reclaim_pages != 0) {
                    const auto reserve = std::min<std::uint64_t>(
                        std::max<std::size_t>(1, reclaim_pages / 2),
                        (payload_cap_bytes_ - used) / kPageSize) * kPageSize;
                    payload_cap_bytes_ = used + reserve;
                }
                return BudgetAdjustment::Reclaiming;
            }
        }
        payload_cap_bytes_ = target;
        return BudgetAdjustment::Applied;
    }
    // Exact owned object/table storage, excluding allocator bookkeeping and
    // temporary staging buffers. This is not total process RSS or PSS.
    std::uint64_t metadata_bytes() const noexcept {
        const auto inline_payload =
            static_cast<std::uint64_t>(pattern_pages_ + sparse_word_pages_ + pattern_delta_pages_) * kPatternSize;
        return sizeof(*this) +
               static_cast<std::uint64_t>(tables_.capacity()) * sizeof(std::unique_ptr<PageTable>) +
               static_cast<std::uint64_t>(allocated_tables_) * sizeof(PageTable) - inline_payload +
               (backing_cache_ ? sizeof(BackingCache) : 0);
    }
    Metrics metrics() const noexcept {
        return {size(), allocated_bytes(), budget_bytes(), metadata_bytes(), allocated_pages(), pattern_pages(),
                sparse_word_pages_, spilled_pages_, static_cast<std::uint64_t>(spilled_pages_) * kPageSize,
                backing_reads_, backing_writes_, pattern_delta_pages_, pattern_delta_bytes_};
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
        if (slot->spilled) return backed_page(page)[inside];
        if (backing_ && slot->has_dense()) slot->recently_used = true;
        return slot_byte(*slot, inside);
    }

    // Borrow a span for an immediate read. A RAM mutation can replace its backing.
    const std::uint8_t* resident_span(std::uint64_t offset, std::size_t count) const noexcept {
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        if (!count || count > kPageSize - inside || offset >= logical_bytes_ ||
            count > logical_bytes_ - offset) return nullptr;
        const auto* slot = find_slot(offset / kPageSize);
        if (!slot || !slot->has_dense()) return nullptr;
        if (backing_) slot->recently_used = true;
        return slot->payload.get() + inside;
    }

    void write_byte(std::uint64_t offset, std::uint8_t value) {
        check_range(offset, 1);
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        auto* slot = find_slot(offset / kPageSize);
        if (!slot || !slot->has_dense()) {
            if ((!slot || !slot->spilled) && (slot ? slot_byte(*slot, inside) : 0) == value) return;
            prepare_write(offset, 1);
            slot = find_slot(offset / kPageSize);
        }
        if (backing_) mark_write(*slot);
        slot->payload[inside] = value;
        // Never free a page here, including when it becomes entirely zero.
    }

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
                if (backing_) slot->recently_used = true;
                std::memcpy(destination, slot->payload.get() + inside, chunk);
            } else if (slot && slot->spilled) {
                std::memcpy(destination, backed_page(page).data() + inside, chunk);
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
                if (backing_) slot.recently_used = true;
                bytes = slot.payload.get() + inside;
            } else if (slot.spilled) {
                bytes = backed_page(page).data() + inside;
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
        if (count != 0 && !source) throw std::invalid_argument("SparseRam null write source.");
        prepare_dense_pages(offset, count, true);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            auto& page = find_slot(offset / kPageSize)->payload;
            if (previous) {
                std::memcpy(previous, page.get() + inside, chunk);
                previous += chunk;
            }
            std::memcpy(page.get() + inside, source, chunk);
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
            from->recently_used = true;
            mark_write(*to);
        }
        std::memcpy(to->payload.get() + to_offset, from->payload.get() + from_offset, count);
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
        if (payload_cap_bytes_ < kPageSize || allocated_bytes() > payload_cap_bytes_ - kPageSize)
            return false;

        std::unique_ptr<std::uint8_t[]> payload(new std::uint8_t[kPageSize]);
        std::memcpy(payload.get(), from->payload.get(), kPageSize);
        auto& table = tables_[static_cast<std::size_t>(page / kPagesPerTable)];
        if (!table) {
            table = std::make_unique<PageTable>();
            ++allocated_tables_;
        }
        auto& destination = table->slots[static_cast<std::size_t>(page % kPagesPerTable)];
        destination = PageSlot{};
        destination.payload = std::move(payload);
        ++table->resident;
        ++allocated_pages_;
        if (backing_) {
            from->recently_used = true;
            mark_write(destination);
        }
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
        if (value != 0) prepare_write(offset, count);
        else if (compact_bytes() || spilled_pages_)
            prepare_dense_pages(offset, count, false);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            auto& table = tables_[static_cast<std::size_t>(offset / kPageSize / kPagesPerTable)];
            if (table) {
                auto& slot = table->slots[static_cast<std::size_t>(offset / kPageSize % kPagesPerTable)];
                if (slot.has_dense()) {
                    std::memset(slot.payload.get() + inside, value, chunk);
                } else if (slot.has_compact() || slot.spilled) {
                    // All partially covered pages were expanded by preflight.
                    if (slot.spilled) --spilled_pages_;
                    else erase_compact(slot);
                    slot = PageSlot{};
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
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        auto* slot = find_slot(offset / kPageSize);
        if (slot && slot->has_dense()) {
            slot->payload[inside] = old_value;
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
        spilled_pages_ = backing_reads_ = backing_writes_ = eviction_page_ = compact_eviction_page_ = 0;
        if (backing_) {
            for (auto& entry : backing_cache_->entries) entry.page = BackingCache::kUnused;
            backing_->reset();
        }
    }
    void clear() noexcept { reset_zero(); }

    std::size_t trim_zero_pages() noexcept {
        if (has_pinned_writes()) return 0;
        std::size_t removed = 0;
        for (auto& table : tables_) {
            if (!table) continue;
            for (auto& slot : table->slots) {
                if (slot.has_dense() && std::all_of(slot.payload.get(), slot.payload.get() + kPageSize,
                                             [](std::uint8_t byte) { return byte == 0; })) {
                    slot.payload.reset();
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
            auto visited = std::uint64_t{1};
            if (!table) {
                visited = std::min({remaining, pages - compaction_page_,
                                    std::uint64_t(kPagesPerTable - index)});
            } else {
                --max_pages;
                compacted += compact_page(table, index);
            }
            compaction_page_ = (compaction_page_ + visited) % pages;
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
        // Dense bytes or sorted pairs of (word index, exact word bytes).
        std::unique_ptr<std::uint8_t[]> payload;
        std::uint64_t pattern = 0;
        std::uint32_t write_epoch = 0;
        // Below kRepeating: one word on zero. Above: number of changed words.
        std::uint16_t word_index = kRepeating;
        mutable bool recently_used = false;
        bool spilled = false;

        bool has_dense() const noexcept { return payload && word_index == kRepeating; }
        bool has_compact() const noexcept { return pattern || word_index > kRepeating; }
        std::size_t patch_count() const noexcept {
            return word_index > kRepeating ? word_index - kRepeating : 0;
        }
        std::size_t compact_size() const noexcept {
            return has_compact() ? kPatternSize + patch_count() * kPatchSize : 0;
        }
    };
    struct PageTable {
        std::array<PageSlot, kPagesPerTable> slots{};
        std::size_t resident = 0;
    };
    struct BackingCache {
        static constexpr std::uint64_t kUnused = UINT64_MAX;
        struct Entry {
            std::uint64_t page = kUnused;
            Page bytes;
        };
        std::array<Entry, 16> entries;
        Entry& entry(std::uint64_t page) {
            return entries[(page ^ (page >> 4)) % entries.size()];
        }
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
    mutable std::uint64_t backing_reads_ = 0;
    std::uint64_t backing_writes_ = 0;
    std::uint64_t eviction_page_ = 0;
    std::uint64_t compact_eviction_page_ = 0;
    std::uint32_t pin_epoch_ = 0;
    unsigned pin_depth_ = 0;

    std::uint64_t compact_bytes() const noexcept {
        return static_cast<std::uint64_t>(pattern_pages_ + sparse_word_pages_) * kPatternSize +
               pattern_delta_bytes_;
    }

    const Page& backed_page(std::uint64_t page) const {
        auto& entry = backing_cache_->entry(page);
        if (entry.page != page) {
            entry.page = BackingCache::kUnused;
            backing_->read(page, entry.bytes.data());
            entry.page = page;
            ++backing_reads_;
        }
        return entry.bytes;
    }

    void mark_write(PageSlot& slot) noexcept {
        slot.recently_used = true;
        if (pin_depth_) slot.write_epoch = pin_epoch_;
    }

    void write_back(std::uint64_t page, const std::uint8_t* bytes) {
        backing_->write(page, bytes);
        if (auto& cached = backing_cache_->entry(page); cached.page == page)
            cached.page = BackingCache::kUnused;
        ++spilled_pages_;
        ++backing_writes_;
    }

    bool spill_compact_page(std::uint64_t compact_limit, std::uint64_t first = UINT64_MAX,
                            std::uint64_t last = UINT64_MAX) {
        if (!backing_ || compact_bytes() <= compact_limit) return false;
        const auto pages = logical_bytes_ / kPageSize + (logical_bytes_ % kPageSize != 0);
        for (auto remaining = pages; remaining;) {
            const auto page = compact_eviction_page_;
            auto* slot = find_slot(page);
            const auto count = slot ? std::uint64_t{1} : std::min({remaining, pages - page,
                std::uint64_t(kPagesPerTable - page % kPagesPerTable)});
            compact_eviction_page_ = (page + count) % pages;
            remaining -= count;
            if (!slot || !slot->has_compact() || (page >= first && page <= last)) continue;
            Page bytes;
            copy_compact(*slot, 0, bytes.data(), bytes.size());
            write_back(page, bytes.data());
            erase_compact(*slot);
            *slot = PageSlot{};
            slot->spilled = true;
            return true;
        }
        return false;
    }

    bool evict_page(std::uint64_t first, std::uint64_t last, std::uint64_t compact_limit) {
        if (!allocated_pages_) return false;
        const auto pages = logical_bytes_ / kPageSize + (logical_bytes_ % kPageSize != 0);
        // A second clock pass can remove pages that were used recently.
        for (auto remaining = 2 * pages; remaining;) {
            auto& table = tables_[static_cast<std::size_t>(eviction_page_ / kPagesPerTable)];
            const auto index = static_cast<std::size_t>(eviction_page_ % kPagesPerTable);
            if (!table) {
                const auto skip = std::min({remaining, pages - eviction_page_,
                                          std::uint64_t(kPagesPerTable - index)});
                eviction_page_ = (eviction_page_ + skip) % pages;
                remaining -= skip;
                continue;
            }
            const auto page = eviction_page_;
            eviction_page_ = (page + 1) % pages;
            --remaining;
            auto& slot = table->slots[index];
            if (!slot.has_dense() || (page >= first && page <= last) ||
                (pin_depth_ && slot.write_epoch == pin_epoch_)) continue;
            if (std::exchange(slot.recently_used, false)) continue;
            const auto compact_room = compact_limit - std::min(compact_limit, compact_bytes());
            if ((compact_room >= kPatternSize ||
                 std::all_of(slot.payload.get(), slot.payload.get() + kPageSize,
                             [](auto byte) { return byte == 0; })) &&
                compact_page(table, index)) return true;
            if (compact_room >= kPatternSize && compact_changed_words(slot, compact_room)) return true;
            if (!backing_) continue;
            write_back(page, slot.payload.get());
            slot.payload.reset();
            slot.spilled = true;
            --allocated_pages_;
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
        if (retained > payload_cap_bytes_ || new_bytes > payload_cap_bytes_ - retained) return false;
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
        if (slot.spilled) --spilled_pages_;
        slot = PageSlot{};
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
    bool compact_changed_words(PageSlot& slot, std::uint64_t compact_room) noexcept {
        const auto* bytes = slot.payload.get();
        // If a page has at most kMaxChangedWords exceptions, its background
        // is the majority even in this short prefix. Verify against all words.
        std::uint64_t pattern = 0;
        unsigned votes = 0;
        for (std::size_t word = 0; word < 2 * kMaxChangedWords + 1; ++word) {
            const auto value = load_word(bytes + word * kPatternSize);
            if (votes == 0) {
                pattern = value;
                votes = 1;
            } else if (value == pattern) ++votes;
            else --votes;
        }

        std::array<std::uint8_t, kMaxChangedWords * kPatchSize> patches;
        std::size_t changed = 0;
        for (std::uint16_t word = 0; word < kRepeating; ++word) {
            const auto value = load_word(bytes + word * kPatternSize);
            if (value == pattern) continue;
            if (changed == kMaxChangedWords) return false;
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
        if (!slot.has_dense() || std::memcmp(slot.payload.get(), slot.payload.get() + kPatternSize,
                                            kPageSize - kPatternSize) != 0) return false;
        slot.pattern = load_word(slot.payload.get());
        slot.payload.reset();
        --allocated_pages_;
        if (slot.pattern) ++pattern_pages_;
        else --table->resident;
        release_empty_table(table);
        return true;
    }

    void prepare_dense_pages(std::uint64_t offset, std::size_t count, bool include_implicit) {
        if (count == 0) return;
        const std::uint64_t first = offset / kPageSize;
        const std::uint64_t last = (offset + static_cast<std::uint64_t>(count) - 1) / kPageSize;
        const auto expands_pattern = [&](std::uint64_t page) {
            // During a zero fill, only pages that retain bytes outside the
            // span need expansion. Fully covered compact pages become zero.
            return include_implicit || (page == first && offset % kPageSize != 0) ||
                   (page == last && (offset + count) % kPageSize != 0);
        };
        std::size_t missing_pages = 0;
        std::uint64_t expanded_compact_bytes = 0;
        for (std::uint64_t page = first; page <= last; ++page) {
            auto* slot = find_slot(page);
            if (slot && slot->has_dense()) {
                if (backing_) mark_write(*slot);
                continue;
            }
            if (include_implicit || (slot && (slot->has_compact() || slot->spilled) && expands_pattern(page))) {
                ++missing_pages;
                if (slot) expanded_compact_bytes += slot->compact_size();
            }
        }
        if (missing_pages == 0) return;
        const auto fits = [&] {
            const auto retained = allocated_bytes() - expanded_compact_bytes;
            return retained <= payload_cap_bytes_ && missing_pages <= (payload_cap_bytes_ - retained) / kPageSize;
        };
        if (backing_ && missing_pages <= payload_cap_bytes_ / kPageSize) {
            const auto compact_limit = payload_cap_bytes_ - missing_pages * kPageSize +
                                       expanded_compact_bytes;
            while (!fits() && (evict_page(first, last, compact_limit) ||
                               spill_compact_page(compact_limit, first, last))) {}
        }
        if (!fits()) {
            const auto max_extra = std::numeric_limits<std::uint64_t>::max() / kPageSize;
            const auto additional = std::min<std::uint64_t>(missing_pages, max_extra) * kPageSize;
            throw BudgetExceeded(
                "SparseRam payload budget exceeded: allocated=" + std::to_string(allocated_bytes()) +
                ", additional=" + std::to_string(additional) +
                ", budget=" + std::to_string(payload_cap_bytes_) + " bytes.");
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
                if (slot && (slot->has_compact() || slot->spilled) && expands_pattern(page)) {
                    std::unique_ptr<std::uint8_t[]> dense(new std::uint8_t[kPageSize]);
                    if (slot->spilled) std::memcpy(dense.get(), backed_page(page).data(), kPageSize);
                    else copy_compact(*slot, 0, dense.get(), kPageSize);
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
                if (slot.spilled) --spilled_pages_;
                else if (slot.has_compact()) erase_compact(slot);
                else ++table.resident;
                slot = PageSlot{};
                slot.payload = std::move(page.second);
                if (backing_) mark_write(slot);
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
