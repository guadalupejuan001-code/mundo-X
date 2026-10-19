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

// Original sparse physical RAM. An absent page reads as zero; the logical
// address space is independent of the explicitly bounded resident payload.
// Not thread-safe: the CPU and its host must serialize access.
class SparseRam {
public:
    static constexpr std::size_t kPageSize = 4096;
    static constexpr std::size_t kPagesPerTable = 512;
    static constexpr std::uint64_t kDefaultPayloadLimit = std::uint64_t{1} << 30;

    class BudgetExceeded : public std::runtime_error {
    public:
        explicit BudgetExceeded(const std::string& reason) : std::runtime_error(reason) {}
    };

    struct Metrics {
        std::uint64_t logical_bytes;
        std::uint64_t allocated_bytes;
        std::uint64_t budget_bytes;
        std::uint64_t metadata_bytes;
        std::size_t allocated_pages;
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
          allocated_tables_(std::exchange(other.allocated_tables_, 0)) {}
    SparseRam& operator=(SparseRam&& other) noexcept {
        if (this != &other) {
            tables_ = std::move(other.tables_);
            logical_bytes_ = std::exchange(other.logical_bytes_, 0);
            payload_cap_bytes_ = std::exchange(other.payload_cap_bytes_, 0);
            allocated_pages_ = std::exchange(other.allocated_pages_, 0);
            allocated_tables_ = std::exchange(other.allocated_tables_, 0);
        }
        return *this;
    }

    std::uint64_t size() const noexcept { return logical_bytes_; }
    bool empty() const noexcept { return logical_bytes_ == 0; }
    std::size_t allocated_pages() const noexcept { return allocated_pages_; }
    std::uint64_t allocated_bytes() const noexcept {
        return static_cast<std::uint64_t>(allocated_pages_) * kPageSize;
    }
    std::uint64_t budget_bytes() const noexcept { return payload_cap_bytes_; }
    // Exact owned object/table storage, excluding allocator bookkeeping and
    // temporary staging buffers. This is not total process RSS or PSS.
    std::uint64_t metadata_bytes() const noexcept {
        return sizeof(*this) +
               static_cast<std::uint64_t>(tables_.capacity()) * sizeof(std::unique_ptr<PageTable>) +
               static_cast<std::uint64_t>(allocated_tables_) * sizeof(PageTable);
    }
    Metrics metrics() const noexcept {
        return {size(), allocated_bytes(), budget_bytes(), metadata_bytes(), allocated_pages()};
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
        const Page* page = find_page(offset / kPageSize);
        return page ? (*page)[static_cast<std::size_t>(offset % kPageSize)] : 0;
    }

    void write_byte(std::uint64_t offset, std::uint8_t value) {
        check_range(offset, 1);
        Page* page = find_page(offset / kPageSize);
        if (!page) {
            if (value == 0) return; // An absent zero byte needs no backing.
            prepare_write(offset, 1);
            page = find_page(offset / kPageSize);
        }
        (*page)[static_cast<std::size_t>(offset % kPageSize)] = value;
        // Never free a page here, including when it becomes entirely zero.
    }

    void read_block(std::uint64_t offset, std::uint8_t* destination,
                    std::size_t count) const {
        check_range(offset, count);
        if (count != 0 && !destination) throw std::invalid_argument("SparseRam null read destination.");
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            const Page* page = find_page(offset / kPageSize);
            if (page) std::memcpy(destination, page->data() + inside, chunk);
            else std::memset(destination, 0, chunk);
            offset += chunk;
            destination += chunk;
            count -= chunk;
        }
    }

    // Range, budget and all backing allocations are checked before any byte
    // is changed. The caller's source must remain valid for the full call.
    void write_block(std::uint64_t offset, const std::uint8_t* source,
                     std::size_t count) {
        check_range(offset, count);
        if (count != 0 && !source) throw std::invalid_argument("SparseRam null write source.");
        prepare_write(offset, count);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            Page* page = find_page(offset / kPageSize);
            std::memcpy(page->data() + inside, source, chunk);
            offset += chunk;
            source += chunk;
            count -= chunk;
        }
    }

    void fill(std::uint64_t offset, std::uint8_t value, std::size_t count) {
        check_range(offset, count);
        if (value != 0) prepare_write(offset, count);
        while (count != 0) {
            const std::size_t inside = static_cast<std::size_t>(offset % kPageSize);
            const std::size_t chunk = std::min(count, kPageSize - inside);
            if (Page* page = find_page(offset / kPageSize)) {
                std::memset(page->data() + inside, value, chunk);
            }
            offset += chunk;
            count -= chunk;
        }
    }

    // Strong allocation guarantee: either every missing page is installed,
    // or resident pages, bytes and metrics remain unchanged. This operation
    // deliberately backs zero pages too, for transactional write preflight.
    void prepare_write(std::uint64_t offset, std::size_t count) {
        check_range(offset, count);
        if (count == 0) return;
        const std::uint64_t first = offset / kPageSize;
        const std::uint64_t last = (offset + static_cast<std::uint64_t>(count) - 1) / kPageSize;
        std::size_t missing_pages = 0;
        for (std::uint64_t page = first; page <= last; ++page) {
            if (!find_page(page)) ++missing_pages;
        }
        if (missing_pages == 0) return;
        const std::uint64_t capacity = payload_cap_bytes_ / kPageSize;
        if (allocated_pages_ > capacity || missing_pages > capacity - allocated_pages_) {
            throw BudgetExceeded(
                "SparseRam payload budget exceeded: allocated=" + std::to_string(allocated_bytes()) +
                ", additional=" + std::to_string(static_cast<std::uint64_t>(missing_pages) * kPageSize) +
                ", budget=" + std::to_string(payload_cap_bytes_) + " bytes.");
        }

        const std::size_t first_table = static_cast<std::size_t>(first / kPagesPerTable);
        const std::size_t last_table = static_cast<std::size_t>(last / kPagesPerTable);
        std::size_t missing_tables = 0;
        for (std::size_t table = first_table; table <= last_table; ++table) {
            if (!tables_[table]) ++missing_tables;
        }
        try {
            std::vector<std::pair<std::size_t, std::unique_ptr<PageTable>>> new_tables;
            std::vector<std::pair<std::uint64_t, std::unique_ptr<Page>>> new_pages;
            new_tables.reserve(missing_tables);
            new_pages.reserve(missing_pages);
            for (std::size_t table = first_table; table <= last_table; ++table) {
                if (!tables_[table]) new_tables.emplace_back(table, std::make_unique<PageTable>());
            }
            for (std::uint64_t page = first; page <= last; ++page) {
                if (!find_page(page)) new_pages.emplace_back(page, std::make_unique<Page>());
            }
            // Everything that can allocate has succeeded. Pointer moves and
            // counters below cannot throw, so publication is all-or-nothing.
            for (auto& table : new_tables) tables_[table.first] = std::move(table.second);
            for (auto& page : new_pages) {
                PageTable& table = *tables_[static_cast<std::size_t>(page.first / kPagesPerTable)];
                table.pages[static_cast<std::size_t>(page.first % kPagesPerTable)] = std::move(page.second);
                ++table.resident;
            }
            allocated_tables_ += missing_tables;
            allocated_pages_ += missing_pages;
        } catch (const std::bad_alloc&) {
            throw std::runtime_error("SparseRam host allocation failed while preparing page backing.");
        }
    }

    // Rollback never allocates and therefore still works at a full budget.
    // A missing page can only have had a zero old byte. false reports a
    // violated caller invariant without throwing from a rollback operation.
    // Do not reset, trim or move RAM while a CPU write journal is live.
    bool restore_byte(std::uint64_t offset, std::uint8_t old_value) noexcept {
        if (offset >= logical_bytes_) return false;
        if (Page* page = find_page(offset / kPageSize)) {
            (*page)[static_cast<std::size_t>(offset % kPageSize)] = old_value;
            return true;
        }
        return old_value == 0;
    }

    // Explicit lifecycle operations; never called automatically by writes.
    // reset_zero/clear discard backing while preserving size and budget.
    void reset_zero() noexcept {
        for (auto& table : tables_) table.reset();
        allocated_pages_ = allocated_tables_ = 0;
    }
    void clear() noexcept { reset_zero(); }

    std::size_t trim_zero_pages() noexcept {
        std::size_t removed = 0;
        for (auto& table : tables_) {
            if (!table) continue;
            for (auto& page : table->pages) {
                if (page && std::all_of(page->begin(), page->end(),
                                       [](std::uint8_t byte) { return byte == 0; })) {
                    page.reset();
                    --table->resident;
                    ++removed;
                }
            }
            if (table->resident == 0) {
                table.reset();
                --allocated_tables_;
            }
        }
        allocated_pages_ -= removed;
        return removed;
    }

private:
    using Page = std::array<std::uint8_t, kPageSize>;
    struct PageTable {
        std::array<std::unique_ptr<Page>, kPagesPerTable> pages{};
        std::size_t resident = 0;
    };

    std::uint64_t logical_bytes_ = 0;
    std::uint64_t payload_cap_bytes_ = 0;
    std::vector<std::unique_ptr<PageTable>> tables_;
    std::size_t allocated_pages_ = 0;
    std::size_t allocated_tables_ = 0;

    void check_range(std::uint64_t offset, std::size_t count) const {
        static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
                      "SparseRam requires size_t to fit in uint64_t.");
        if (offset > logical_bytes_ || static_cast<std::uint64_t>(count) > logical_bytes_ - offset) {
            throw std::out_of_range("SparseRam address range outside logical RAM: offset=" +
                                    std::to_string(offset) + ", length=" + std::to_string(count) +
                                    ", logical=" + std::to_string(logical_bytes_) + ".");
        }
    }
    Page* find_page(std::uint64_t page_index) noexcept {
        const auto& table = tables_[static_cast<std::size_t>(page_index / kPagesPerTable)];
        return table ? table->pages[static_cast<std::size_t>(page_index % kPagesPerTable)].get() : nullptr;
    }
    const Page* find_page(std::uint64_t page_index) const noexcept {
        const auto& table = tables_[static_cast<std::size_t>(page_index / kPagesPerTable)];
        return table ? table->pages[static_cast<std::size_t>(page_index % kPagesPerTable)].get() : nullptr;
    }
};

} // namespace cosmic_pc

#endif // COSMIC_PC_SPARSE_RAM_HPP
