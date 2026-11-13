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

// Sparse physical RAM. Absent pages read as zero and exactly repeating pages
// can retain one eight-byte pattern instead of 4 KiB of dense backing.
// Logical addresses are independent of the explicitly bounded payload.
// Not thread-safe: the CPU and its host must serialize access.
class SparseRam {
public:
    static constexpr std::size_t kPageSize = 4096;
    static constexpr std::size_t kPatternSize = 8;
    static constexpr std::size_t kPagesPerTable = 512;
    static constexpr std::uint64_t kDefaultPayloadLimit = std::uint64_t{1} << 30;

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
        std::size_t spilled_pages;
        std::uint64_t backing_bytes;
        std::uint64_t backing_reads;
        std::uint64_t backing_writes;
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
          compaction_page_(std::exchange(other.compaction_page_, 0)),
          backing_(std::move(other.backing_)),
          backing_cache_(std::move(other.backing_cache_)),
          spilled_pages_(std::exchange(other.spilled_pages_, 0)),
          backing_reads_(std::exchange(other.backing_reads_, 0)),
          backing_writes_(std::exchange(other.backing_writes_, 0)),
          eviction_page_(std::exchange(other.eviction_page_, 0)),
          pin_epoch_(std::exchange(other.pin_epoch_, 0)) {}
    SparseRam& operator=(SparseRam&& other) noexcept {
        if (this != &other) {
            tables_ = std::move(other.tables_);
            logical_bytes_ = std::exchange(other.logical_bytes_, 0);
            payload_cap_bytes_ = std::exchange(other.payload_cap_bytes_, 0);
            allocated_pages_ = std::exchange(other.allocated_pages_, 0);
            allocated_tables_ = std::exchange(other.allocated_tables_, 0);
            pattern_pages_ = std::exchange(other.pattern_pages_, 0);
            compaction_page_ = std::exchange(other.compaction_page_, 0);
            backing_ = std::move(other.backing_);
            backing_cache_ = std::move(other.backing_cache_);
            spilled_pages_ = std::exchange(other.spilled_pages_, 0);
            backing_reads_ = std::exchange(other.backing_reads_, 0);
            backing_writes_ = std::exchange(other.backing_writes_, 0);
            eviction_page_ = std::exchange(other.eviction_page_, 0);
            pin_epoch_ = std::exchange(other.pin_epoch_, 0);
        }
        return *this;
    }

    std::uint64_t size() const noexcept { return logical_bytes_; }
    bool empty() const noexcept { return logical_bytes_ == 0; }
    // Counts only full resident pages. Each compact page uses kPatternSize.
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
        return static_cast<std::uint64_t>(allocated_pages_) * kPageSize + pattern_bytes();
    }
    std::uint64_t budget_bytes() const noexcept { return payload_cap_bytes_; }
    // Exact owned object/table storage, excluding allocator bookkeeping and
    // temporary staging buffers. This is not total process RSS or PSS.
    std::uint64_t metadata_bytes() const noexcept {
        return sizeof(*this) +
               static_cast<std::uint64_t>(tables_.capacity()) * sizeof(std::unique_ptr<PageTable>) +
               static_cast<std::uint64_t>(allocated_tables_) * sizeof(PageTable) - pattern_bytes() +
               (backing_cache_ ? sizeof(BackingCache) : 0);
    }
    Metrics metrics() const noexcept {
        return {size(), allocated_bytes(), budget_bytes(), metadata_bytes(), allocated_pages(), pattern_pages(),
                spilled_pages_, static_cast<std::uint64_t>(spilled_pages_) * kPageSize,
                backing_reads_, backing_writes_};
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
        if (backing_ && slot->dense) slot->recently_used = true;
        return slot_byte(*slot, inside);
    }

    void write_byte(std::uint64_t offset, std::uint8_t value) {
        check_range(offset, 1);
        const auto inside = static_cast<std::size_t>(offset % kPageSize);
        auto* slot = find_slot(offset / kPageSize);
        if (!slot || !slot->dense) {
            if ((!slot || !slot->spilled) && (slot ? slot_byte(*slot, inside) : 0) == value) return;
            prepare_write(offset, 1);
            slot = find_slot(offset / kPageSize);
        }
        if (backing_) mark_write(*slot);
        (*slot->dense)[inside] = value;
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
            if (slot && slot->dense) {
                if (backing_) slot->recently_used = true;
                std::memcpy(destination, slot->dense->data() + inside, chunk);
            } else if (slot && slot->spilled) {
                std::memcpy(destination, backed_page(page).data() + inside, chunk);
            } else if (slot && slot->pattern) copy_pattern(slot->pattern, inside, destination, chunk);
            else std::memset(destination, 0, chunk);
            offset += chunk;
            destination += chunk;
            count -= chunk;
        }
    }

    // Guest bytes change only after the range, budget and allocations are
    // checked. The caller's source must remain valid for the full call.
    void write_block(std::uint64_t offset, const std::uint8_t* source,
                     std::size_t count) {
        check_range(offset, count);
        if (count != 0 && !source) throw std::invalid_argument("SparseRam null write source.");
        prepare_write(offset, count);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            auto& page = *find_slot(offset / kPageSize)->dense;
            std::memcpy(page.data() + inside, source, chunk);
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
        if (!from || !from->dense || !to || !to->dense) return false;
        if (backing_) {
            from->recently_used = true;
            mark_write(*to);
        }
        std::memcpy(to->dense->data() + to_offset, from->dense->data() + from_offset, count);
        return true;
    }

    // Borrow the exact compact representation of a full source page. The
    // destination has independent slots, so later guest writes never alias.
    bool try_copy_repeating_page(std::uint64_t source, std::uint64_t target) {
        check_range(source, kPageSize);
        check_range(target, kPageSize);
        if ((source % kPageSize) || (target % kPageSize) || has_pinned_writes()) return false;

        // Dense and spilled sources already have efficient block reads and
        // can be checked for repetition after an ordinary page copy.
        const auto* from = find_slot(source / kPageSize);
        if (from && (from->dense || from->spilled)) return false;
        const auto pattern = from ? from->pattern : 0;

        const auto page = target / kPageSize;
        auto& table = tables_[static_cast<std::size_t>(page / kPagesPerTable)];
        const auto index = static_cast<std::size_t>(page % kPagesPerTable);
        if (!table && !pattern) return true;
        const auto* previous = table ? &table->slots[index] : nullptr;
        const auto old_bytes = !previous ? 0 : previous->dense ? kPageSize : previous->pattern ? kPatternSize : 0;
        const auto retained = allocated_bytes() - old_bytes;
        if (pattern && (retained > payload_cap_bytes_ || kPatternSize > payload_cap_bytes_ - retained))
            return false;
        if (!table) {
            table = std::make_unique<PageTable>();
            ++allocated_tables_;
        }
        auto& to = table->slots[index];
        if (to.dense || to.pattern || to.spilled) --table->resident;
        if (to.dense) --allocated_pages_;
        if (to.pattern) --pattern_pages_;
        if (to.spilled) --spilled_pages_;
        to = PageSlot{};
        to.pattern = pattern;
        if (pattern) {
            ++table->resident;
            ++pattern_pages_;
        }
        release_empty_table(table);
        return true;
    }

    void fill(std::uint64_t offset, std::uint8_t value, std::size_t count) {
        check_range(offset, count);
        if (value != 0) prepare_write(offset, count);
        else if (pattern_pages_ || spilled_pages_) prepare_dense_pages(offset, count, false);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            auto& table = tables_[static_cast<std::size_t>(offset / kPageSize / kPagesPerTable)];
            if (table) {
                auto& slot = table->slots[static_cast<std::size_t>(offset / kPageSize % kPagesPerTable)];
                if (slot.dense) {
                    std::memset(slot.dense->data() + inside, value, chunk);
                } else if (slot.pattern || slot.spilled) {
                    // All partially covered pages were expanded by preflight.
                    if (slot.spilled) --spilled_pages_;
                    else --pattern_pages_;
                    slot.pattern = 0;
                    slot.spilled = false;
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
        if (slot && slot->dense) {
            (*slot->dense)[inside] = old_value;
            return true;
        }
        return (!slot || !slot->spilled) && (slot ? slot_byte(*slot, inside) : 0) == old_value;
    }

    // Explicit lifecycle operations; never called automatically by writes.
    // reset_zero/clear discard backing while preserving size and budget.
    void reset_zero() noexcept {
        for (auto& table : tables_) table.reset();
        allocated_pages_ = allocated_tables_ = pattern_pages_ = compaction_page_ = 0;
        spilled_pages_ = backing_reads_ = backing_writes_ = eviction_page_ = 0;
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
                if (slot.dense && std::all_of(slot.dense->begin(), slot.dense->end(),
                                             [](std::uint8_t byte) { return byte == 0; })) {
                    slot.dense.reset();
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
    struct PageSlot {
        std::unique_ptr<Page> dense;
        // Raw bytes in native storage order. All zero means no backing.
        std::uint64_t pattern = 0;
        std::uint32_t write_epoch = 0;
        mutable bool recently_used = false;
        bool spilled = false;
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
    std::uint64_t compaction_page_ = 0;
    std::unique_ptr<PageBacking> backing_;
    std::unique_ptr<BackingCache> backing_cache_;
    std::size_t spilled_pages_ = 0;
    mutable std::uint64_t backing_reads_ = 0;
    std::uint64_t backing_writes_ = 0;
    std::uint64_t eviction_page_ = 0;
    std::uint32_t pin_epoch_ = 0;
    unsigned pin_depth_ = 0;

    std::uint64_t pattern_bytes() const noexcept {
        return static_cast<std::uint64_t>(pattern_pages_) * kPatternSize;
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

    bool evict_page(std::uint64_t first, std::uint64_t last) {
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
            if (!slot.dense || (page >= first && page <= last) ||
                (pin_depth_ && slot.write_epoch == pin_epoch_)) continue;
            if (std::exchange(slot.recently_used, false)) continue;
            if (compact_page(table, index)) return true;
            backing_->write(page, slot.dense->data());
            if (auto& cached = backing_cache_->entry(page); cached.page == page)
                cached.page = BackingCache::kUnused;
            slot.dense.reset();
            slot.spilled = true;
            --allocated_pages_;
            ++spilled_pages_;
            ++backing_writes_;
            return true;
        }
        return false;
    }

    static std::uint8_t slot_byte(const PageSlot& slot, std::size_t inside) noexcept {
        if (slot.dense) return (*slot.dense)[inside];
        return reinterpret_cast<const unsigned char*>(&slot.pattern)[inside % kPatternSize];
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

    bool compact_page(std::unique_ptr<PageTable>& table, std::size_t index) noexcept {
        auto& slot = table->slots[index];
        if (!slot.dense || std::memcmp(slot.dense->data(), slot.dense->data() + kPatternSize,
                                      kPageSize - kPatternSize) != 0) return false;
        std::memcpy(&slot.pattern, slot.dense->data(), kPatternSize);
        slot.dense.reset();
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
        std::size_t expanded_patterns = 0;
        for (std::uint64_t page = first; page <= last; ++page) {
            auto* slot = find_slot(page);
            const bool compact = slot && slot->pattern && expands_pattern(page);
            const bool spilled = slot && slot->spilled && expands_pattern(page);
            if (compact) ++expanded_patterns;
            if (compact || spilled || (include_implicit && (!slot || !slot->dense))) ++missing_pages;
            if (backing_ && slot && slot->dense) mark_write(*slot);
        }
        if (missing_pages == 0) return;
        const auto fits = [&] {
            const auto retained = allocated_bytes() - std::uint64_t(expanded_patterns) * kPatternSize;
            return retained <= payload_cap_bytes_ && missing_pages <= (payload_cap_bytes_ - retained) / kPageSize;
        };
        if (backing_ && missing_pages <= payload_cap_bytes_ / kPageSize) {
            while (!fits() && evict_page(first, last)) {}
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
            std::vector<std::pair<std::uint64_t, std::unique_ptr<Page>>> new_pages;
            new_tables.reserve(missing_tables);
            new_pages.reserve(missing_pages);
            for (std::size_t table = first_table; table <= last_table; ++table) {
                if (include_implicit && !tables_[table])
                    new_tables.emplace_back(table, std::make_unique<PageTable>());
            }
            for (std::uint64_t page = first; page <= last; ++page) {
                const auto* slot = find_slot(page);
                if (slot && slot->pattern && expands_pattern(page)) {
                    auto dense = std::make_unique<Page>();
                    copy_pattern(slot->pattern, 0, dense->data(), dense->size());
                    new_pages.emplace_back(page, std::move(dense));
                } else if (slot && slot->spilled && expands_pattern(page)) {
                    new_pages.emplace_back(page, std::make_unique<Page>(backed_page(page)));
                } else if (include_implicit && (!slot || !slot->dense)) {
                    new_pages.emplace_back(page, std::make_unique<Page>());
                }
            }
            // Everything that can allocate has succeeded. Pointer moves and
            // counters below cannot throw, so publication is all-or-nothing.
            for (auto& table : new_tables) tables_[table.first] = std::move(table.second);
            for (auto& page : new_pages) {
                PageTable& table = *tables_[static_cast<std::size_t>(page.first / kPagesPerTable)];
                auto& slot = table.slots[static_cast<std::size_t>(page.first % kPagesPerTable)];
                if (slot.spilled) --spilled_pages_;
                else if (!slot.pattern) ++table.resident;
                slot.pattern = 0;
                slot.spilled = false;
                slot.dense = std::move(page.second);
                if (backing_) mark_write(slot);
            }
            allocated_tables_ += missing_tables;
            allocated_pages_ += missing_pages;
            pattern_pages_ -= expanded_patterns;
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
