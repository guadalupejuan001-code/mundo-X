#pragma once

#include "boot_timing.hpp"
#include "sparse_ram.hpp"

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosmic_pc {

// MemoryFabric keeps ordinary x86 RAM in SparseRam and can reserve one exact
// byte-addressed window whose values are provided by another state authority.
// Mundo X uses that window to expose Ether Memory directly to x86 loads/stores.
//
// The callbacks carry classical bytes only. A provider may reject a read/write
// when its underlying state is not a deterministic byte. The physical RAM and
// the Ether-backed window therefore keep distinct resource budgets.
class MemoryFabric {
 public:
  using ReadByte = std::function<std::uint8_t(std::uint64_t)>;
  using WriteByte = std::function<void(std::uint64_t, std::uint8_t)>;
  using ReadWindowBlock = std::function<void(std::uint64_t, std::uint8_t*, std::size_t)>;
  using WriteWindowBlock = std::function<void(std::uint64_t, const std::uint8_t*, std::size_t)>;
  using BudgetExceeded = SparseRam::BudgetExceeded;
  using Metrics = SparseRam::Metrics;
  static constexpr std::size_t kPageSize = SparseRam::kPageSize;
  static constexpr std::size_t kPagesPerTable = SparseRam::kPagesPerTable;
  static constexpr std::uint64_t kDefaultPayloadLimit = SparseRam::kDefaultPayloadLimit;

  // One optional consumer of writes to a range of ordinary physical RAM.
  // Offsets in pages() are relative to that range, even at a partial last page.
  class WriteWatch {
   public:
    explicit WriteWatch(std::size_t max_bytes)
        : marked_(max_bytes / kPageSize + (max_bytes % kPageSize != 0)) {
      pages_.reserve(marked_.size());
    }
    bool pending() const noexcept { return full_ || !pages_.empty(); }
    bool full() const noexcept { return full_; }
    const std::vector<std::uint32_t>& pages() const noexcept { return pages_; }

   private:
    friend class MemoryFabric;
    void clear() noexcept {
      for (auto page : pages_) marked_[page] = false;
      pages_.clear();
      full_ = false;
    }
    void invalidate() noexcept {
      clear();
      full_ = true;
    }
    void range(std::uint64_t base, std::size_t bytes) noexcept {
      base_ = base;
      end_ = base + bytes;
      invalidate();
    }
    void note(std::uint64_t offset, std::size_t bytes) noexcept {
      if (full_ || offset >= end_ || offset + bytes <= base_) return;
      const auto first = (std::max(offset, base_) - base_) / kPageSize;
      const auto last = (std::min(offset + bytes, end_) - 1 - base_) / kPageSize;
      for (auto page = first; page <= last; ++page) {
        if (marked_[page]) continue;
        marked_[page] = true;
        pages_.push_back(static_cast<std::uint32_t>(page));
      }
    }
    std::uint64_t base_ = 0;
    std::uint64_t end_ = 0;
    bool full_ = true;
    std::vector<bool> marked_;
    std::vector<std::uint32_t> pages_;
  };

  class ByteProxy {
   public:
    ByteProxy(const ByteProxy&) = default;
    operator std::uint8_t() const { return owner_->read_byte(offset_); }
    ByteProxy& operator=(std::uint8_t value) {
      owner_->write_byte(offset_, value);
      return *this;
    }
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
    friend class MemoryFabric;
    ByteProxy(MemoryFabric& owner, std::uint64_t offset) : owner_(&owner), offset_(offset) {}
    MemoryFabric* owner_;
    std::uint64_t offset_;
  };

  static std::uint64_t default_budget(std::uint64_t logical_bytes) noexcept {
    return SparseRam::default_budget(logical_bytes);
  }

  explicit MemoryFabric(std::uint64_t logical_bytes)
      : ram_(logical_bytes) {}
  MemoryFabric(std::uint64_t logical_bytes, std::uint64_t payload_cap_bytes)
      : ram_(logical_bytes, payload_cap_bytes) {}

  MemoryFabric(const MemoryFabric&) = delete;
  MemoryFabric& operator=(const MemoryFabric&) = delete;
  MemoryFabric(MemoryFabric&&) noexcept = default;
  MemoryFabric& operator=(MemoryFabric&&) noexcept = default;

  void attach_window(std::uint64_t base, std::size_t bytes, ReadByte read, WriteByte write,
                     ReadWindowBlock read_block = {}, WriteWindowBlock write_block = {}) {
    if (bytes == 0 || base > size() || static_cast<std::uint64_t>(bytes) > size() - base)
      throw std::invalid_argument("Ether memory window is outside logical RAM");
    if (!read || !write) throw std::invalid_argument("Ether memory window requires read/write callbacks");
    if (bool(read_block) != bool(write_block))
      throw std::invalid_argument("Ether block access requires paired callbacks");
    if (write_watch_ && base < write_watch_->end_ && base + bytes > write_watch_->base_)
      throw std::invalid_argument("Ether memory window overlaps a physical write watch");
    invalidate_page_tables();
    if (code_page_versions_) code_page_versions_->clear();
    base_ = base;
    bytes_ = bytes;
    read_ = std::move(read);
    write_ = std::move(write);
    read_window_block_ = std::move(read_block);
    write_window_block_ = std::move(write_block);
  }

  std::uint64_t size() const noexcept { return ram_.size(); }
  bool empty() const noexcept { return ram_.empty(); }
  std::size_t allocated_pages() const noexcept { return ram_.allocated_pages(); }
  std::size_t pattern_pages() const noexcept { return ram_.pattern_pages(); }
  bool has_page_backing() const noexcept { return ram_.has_page_backing(); }
  bool has_pinned_writes() const noexcept { return ram_.has_pinned_writes(); }
  void attach_page_backing(std::unique_ptr<SparseRam::PageBacking> backing) {
    ram_.attach_page_backing(std::move(backing));
    if (code_page_versions_) code_page_versions_->clear();
  }
  [[nodiscard]] SparseRam::WritePins pin_writes() noexcept { return ram_.pin_writes(); }
  std::uint64_t allocated_bytes() const noexcept { return ram_.allocated_bytes(); }
  std::uint64_t budget_bytes() const noexcept { return ram_.budget_bytes(); }
  SparseRam::BudgetAdjustment adjust_payload_budget(std::uint64_t target, std::size_t reclaim_pages) {
    return ram_.adjust_payload_budget(target, reclaim_pages);
  }
  std::uint64_t metadata_bytes() const noexcept {
    return ram_.metadata_bytes() + sizeof(*this) - sizeof(ram_) +
        (code_page_versions_ ? sizeof(CodePageVersions) : 0) +
        (write_watch_ ? sizeof(WriteWatch) + write_watch_->marked_.capacity() / 8 +
            write_watch_->pages_.capacity() * sizeof(std::uint32_t) : 0);
  }
  Metrics metrics() const noexcept {
    auto out = ram_.metrics();
    out.metadata_bytes = metadata_bytes();
    return out;
  }
  bool has_window() const noexcept { return bytes_ != 0; }
  std::uint64_t window_base() const noexcept { return base_; }
  std::size_t window_bytes() const noexcept { return bytes_; }
  bool has_window_block(std::uint64_t offset, std::size_t count) const noexcept {
    return offset >= base_ && offset - base_ < bytes_ && count &&
           count <= bytes_ - (offset - base_) && bool(read_window_block_);
  }
  void set_boot_scalar_timing(BootScalarTiming* timing) noexcept { boot_scalar_timing_ = timing; }

  void enable_write_watch(std::uint64_t base, std::size_t bytes, std::size_t capacity) {
    check_range(base, bytes);
    if (!bytes || bytes > capacity || capacity / kPageSize >= UINT32_MAX ||
        overlaps_window(base, bytes))
      throw std::invalid_argument("MemoryFabric write watch size");
    write_watch_ = std::make_unique<WriteWatch>(capacity);
    write_watch_->range(base, bytes);
  }
  const WriteWatch* write_watch() const noexcept { return write_watch_.get(); }
  void move_write_watch(std::uint64_t base, std::size_t bytes) {
    if (!write_watch_) return;
    if (!bytes || base > size() || bytes > size() - base ||
        (bytes - 1) / kPageSize >= write_watch_->marked_.size() || overlaps_window(base, bytes))
      throw std::invalid_argument("MemoryFabric write watch range");
    write_watch_->range(base, bytes);
  }
  void invalidate_write_watch() noexcept {
    if (write_watch_) write_watch_->invalidate();
  }
  void acknowledge_write_watch() noexcept {
    if (write_watch_) write_watch_->clear();
  }

  void enable_code_page_versions(bool enabled) noexcept {
    if (!enabled) code_page_versions_.reset();
    else if (!code_page_versions_)
      code_page_versions_.reset(new (std::nothrow) CodePageVersions{});
  }
  std::uint64_t code_page_version(std::uint64_t offset) const noexcept {
    return code_page_versions_ ? code_page_versions_->get(offset / kPageSize) : 0;
  }
  std::uint64_t watch_code_page(std::uint64_t offset) noexcept {
    return code_page_versions_ ? code_page_versions_->watch(offset / kPageSize) : 0;
  }

  std::uint64_t page_table_epoch() const noexcept { return page_table_epoch_; }
  void watch_page_table(std::uint64_t offset) noexcept {
    if (!page_table_epoch_)
      page_table_epoch_ = next_page_table_epoch_.fetch_add(1, std::memory_order_relaxed);
    page_table_buckets_[page_table_bucket(offset / kPageSize)] = true;
  }
  void invalidate_page_tables() noexcept {
    if (!page_table_epoch_) return;
    page_table_epoch_ = 0;
    page_table_buckets_.reset();
  }
  bool may_touch_page_tables(std::uint64_t offset, std::size_t count) const noexcept {
    if (!page_table_epoch_ || !count) return false;
    const auto first = offset / kPageSize, last = (offset + count - 1) / kPageSize;
    if (last - first >= 64) return true;
    for (auto page = first; page <= last; ++page)
      if (page_table_buckets_[page_table_bucket(page)]) return true;
    return false;
  }

  ByteProxy operator[](std::uint64_t offset) {
    check_range(offset, 1);
    return ByteProxy(*this, offset);
  }
  std::uint8_t operator[](std::uint64_t offset) const { return read_byte(offset); }
  ByteProxy at(std::uint64_t offset) { return (*this)[offset]; }
  std::uint8_t at(std::uint64_t offset) const { return read_byte(offset); }

  std::uint8_t read_byte(std::uint64_t offset) const {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, 1);
    if (in_window(offset)) return read_(offset - base_);
    return ram_.read_byte(offset);
  }

  void write_byte(std::uint64_t offset, std::uint8_t value) {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, 1);
    note_write(offset, 1);
    if (in_window(offset)) {
      write_(offset - base_, value);
      return;
    }
    ram_.write_byte(offset, value);
  }

  void read_block(std::uint64_t offset, std::uint8_t* destination, std::size_t count) const {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, count);
    if (count && !destination) throw std::invalid_argument("MemoryFabric null read destination");
    if (!count) return;
    if (!overlaps_window(offset, count)) {
      ram_.read_block(offset, destination, count);
      return;
    }
    if (has_window_block(offset, count)) {
      read_window_block_(offset - base_, destination, count);
      return;
    }
    split_read(offset, destination, count);
  }

  // Mixed classical/Ether writes are atomic with respect to expected provider
  // exceptions: Ether bytes are written first with old values retained, and
  // every affected SparseRam page is preflighted before either side commits.
  void write_block(std::uint64_t offset, const std::uint8_t* source, std::size_t count) {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, count);
    if (count && !source) throw std::invalid_argument("MemoryFabric null write source");
    if (!count) return;
    note_write(offset, count);
    if (!overlaps_window(offset, count)) {
      ram_.write_block(offset, source, count);
      return;
    }
    if (has_window_block(offset, count)) {
      write_window_block_(offset - base_, source, count);
      return;
    }
    const auto pins = pin_writes();
    prepare_write(offset, count);
    std::vector<std::pair<std::uint64_t, std::uint8_t>> old;
    old.reserve(std::min<std::size_t>(count, bytes_));
    for (std::size_t i = 0; i < count; ++i) {
      const auto address = offset + i;
      if (in_window(address)) old.emplace_back(address, read_(address - base_));
    }
    std::size_t committed = 0;
    try {
      for (std::size_t i = 0; i < count; ++i) {
        const auto address = offset + i;
        if (in_window(address)) {
          write_(address - base_, source[i]);
          ++committed;
        }
      }
      write_classical_parts(offset, source, count);
    } catch (...) {
      while (committed) {
        --committed;
        const auto& item = old[committed];
        try { write_(item.first - base_, item.second); } catch (...) {}
      }
      throw;
    }
  }

  void fill(std::uint64_t offset, std::uint8_t value, std::size_t count) {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, count);
    if (!count) return;
    note_write(offset, count);
    if (!overlaps_window(offset, count)) {
      ram_.fill(offset, value, count);
      return;
    }
    const auto pins = pin_writes();
    prepare_write(offset, count);
    std::vector<std::pair<std::uint64_t, std::uint8_t>> old;
    old.reserve(std::min<std::size_t>(count, bytes_));
    for (std::size_t i = 0; i < count; ++i) {
      const auto address = offset + i;
      if (in_window(address)) old.emplace_back(address, read_(address - base_));
    }
    std::size_t committed = 0;
    try {
      for (const auto& item : old) {
        write_(item.first - base_, value);
        ++committed;
      }
      fill_classical_parts(offset, value, count);
    } catch (...) {
      while (committed) {
        --committed;
        try { write_(old[committed].first - base_, old[committed].second); } catch (...) {}
      }
      throw;
    }
  }

  bool try_copy_compact_page(std::uint64_t source, std::uint64_t target) {
    if (overlaps_window(source, kPageSize) || overlaps_window(target, kPageSize)) return false;
    if (!ram_.try_copy_compact_page(source, target)) return false;
    note_write(target, kPageSize);
    return true;
  }

  bool try_fill_repeating_page(std::uint64_t target, std::uint64_t pattern) {
    if (overlaps_window(target, kPageSize) || !ram_.try_fill_repeating_page(target, pattern))
      return false;
    note_write(target, kPageSize);
    return true;
  }

  bool try_copy_resident_block(std::uint64_t source, std::uint64_t target, std::size_t count) {
    if (overlaps_window(source, count) || overlaps_window(target, count)) return false;
    if (!ram_.try_copy_resident_block(source, target, count)) return false;
    note_write(target, count);
    return true;
  }

  bool try_copy_to_implicit_page(std::uint64_t source, std::uint64_t target) {
    if (overlaps_window(source, kPageSize) || overlaps_window(target, kPageSize)) return false;
    if (!ram_.try_copy_to_implicit_page(source, target)) return false;
    note_write(target, kPageSize);
    return true;
  }

  // The Ether window does not reserve classical pages. Provider-side qstore8
  // commits a complete byte atomically; CPU rollback restores the old byte.
  void prepare_write(std::uint64_t offset, std::size_t count) {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, count);
    if (!count) return;
    if (!overlaps_window(offset, count)) {
      ram_.prepare_write(offset, count);
      return;
    }
    const auto end = offset + static_cast<std::uint64_t>(count);
    if (offset < base_) {
      const auto before = static_cast<std::size_t>(base_ - offset);
      ram_.prepare_write(offset, before);
    }
    const auto window_end = base_ + bytes_;
    if (end > window_end) {
      const auto after_start = std::max(offset, window_end);
      ram_.prepare_write(after_start, static_cast<std::size_t>(end - after_start));
    }
  }

  bool restore_byte(std::uint64_t offset, std::uint8_t old_value) noexcept {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    if (offset >= size()) return false;
    note_write(offset, 1);
    if (!in_window(offset)) return ram_.restore_byte(offset, old_value);
    try {
      write_(offset - base_, old_value);
      return true;
    } catch (...) {
      return false;
    }
  }

  void reset_zero() noexcept {
    invalidate_page_tables();
    if (code_page_versions_) code_page_versions_->clear();
    ram_.reset_zero();
    invalidate_write_watch();
  }
  void clear() noexcept { reset_zero(); }
  std::size_t trim_zero_pages() noexcept { return ram_.trim_zero_pages(); }
  std::size_t compact_repeating_pages(std::size_t max_pages) noexcept {
    return ram_.compact_repeating_pages(max_pages);
  }
  bool compact_repeating_page(std::uint64_t offset) noexcept {
    return ram_.compact_repeating_page(offset);
  }

 private:
  // Tracks only physical pages whose instruction bytes have been checked.
  // Tokens are unique across slot collisions, RAM replacement and reset.
  class CodePageVersions {
   public:
    std::uint64_t get(std::uint64_t page) const noexcept {
      const auto& slot = slots_[index(page)];
      return slot.page == page ? slot.version : 0;
    }
    std::uint64_t watch(std::uint64_t page) noexcept {
      auto& slot = slots_[index(page)];
      if (slot.page != page || !slot.version) slot = {page, next_version()};
      return slot.version;
    }
    void note(std::uint64_t offset, std::size_t count) noexcept {
      const auto first = offset / kPageSize;
      const auto last = (offset + count - 1) / kPageSize;
      if (last - first >= slots_.size()) { clear(); return; }
      for (auto page = first; page <= last; ++page) {
        auto& slot = slots_[index(page)];
        if (slot.page == page) slot.version = 0;
      }
    }
    void clear() noexcept { slots_.fill({}); }

   private:
    struct Slot { std::uint64_t page = 0, version = 0; };
    static constexpr std::size_t kSlots = 256;
    std::array<Slot, kSlots> slots_{};
    inline static std::atomic<std::uint64_t> next_{1};

    static std::size_t index(std::uint64_t page) noexcept {
      return (page ^ (page >> 6) ^ (page >> 12)) & (kSlots - 1);
    }
    static std::uint64_t next_version() noexcept {
      auto next = next_.load(std::memory_order_relaxed);
      // Exhaustion leaves code on the exact byte-comparison path.
      while (next != UINT64_MAX) {
        if (next_.compare_exchange_weak(next, next + 1, std::memory_order_relaxed))
          return next;
      }
      return 0;
    }
  };

  BootScalarTiming* boot_scalar_timing_ = nullptr;
  static constexpr std::size_t kPageTableBuckets = 4096;
  // A collision only discards extra translations. All writes reach this
  // fabric, including boot-media copies, device writes to RAM and rollback.
  std::bitset<kPageTableBuckets> page_table_buckets_;
  std::uint64_t page_table_epoch_ = 0;
  inline static std::atomic<std::uint64_t> next_page_table_epoch_{1};
  static std::size_t page_table_bucket(std::uint64_t page) noexcept {
    return std::size_t((page ^ (page >> 6) ^ (page >> 12)) % kPageTableBuckets);
  }
  void note_write(std::uint64_t offset, std::size_t count) noexcept {
    if (may_touch_page_tables(offset, count)) invalidate_page_tables();
    if (write_watch_ && count) write_watch_->note(offset, count);
    if (code_page_versions_ && count) code_page_versions_->note(offset, count);
  }
  SparseRam ram_;
  std::unique_ptr<WriteWatch> write_watch_;
  std::unique_ptr<CodePageVersions> code_page_versions_;
  std::uint64_t base_ = 0;
  std::size_t bytes_ = 0;
  ReadByte read_;
  WriteByte write_;
  ReadWindowBlock read_window_block_;
  WriteWindowBlock write_window_block_;

  void check_range(std::uint64_t offset, std::size_t count) const {
    if (offset > size() || static_cast<std::uint64_t>(count) > size() - offset)
      throw std::out_of_range("MemoryFabric address range outside logical RAM");
  }
  bool in_window(std::uint64_t address) const noexcept {
    return bytes_ && address >= base_ && address - base_ < bytes_;
  }
  bool overlaps_window(std::uint64_t offset, std::size_t count) const noexcept {
    if (!bytes_ || !count) return false;
    const auto end = offset + static_cast<std::uint64_t>(count);
    return offset < base_ + bytes_ && end > base_;
  }
  void split_read(std::uint64_t offset, std::uint8_t* destination, std::size_t count) const {
    while (count) {
      if (in_window(offset)) {
        const auto n = std::min<std::size_t>(count, bytes_ - static_cast<std::size_t>(offset - base_));
        for (std::size_t i = 0; i < n; ++i) destination[i] = read_(offset + i - base_);
        offset += n; destination += n; count -= n;
      } else {
        const auto next = offset < base_ ? base_ : size();
        const auto n = std::min<std::size_t>(count, static_cast<std::size_t>(next - offset));
        ram_.read_block(offset, destination, n);
        offset += n; destination += n; count -= n;
      }
    }
  }
  void write_classical_parts(std::uint64_t offset, const std::uint8_t* source, std::size_t count) {
    const auto end = offset + static_cast<std::uint64_t>(count);
    if (offset < base_) {
      const auto n = std::min<std::size_t>(count, static_cast<std::size_t>(base_ - offset));
      ram_.write_block(offset, source, n);
    }
    const auto window_end = base_ + bytes_;
    if (end > window_end) {
      const auto start = std::max(offset, window_end);
      ram_.write_block(start, source + static_cast<std::size_t>(start - offset),
                       static_cast<std::size_t>(end - start));
    }
  }
  void fill_classical_parts(std::uint64_t offset, std::uint8_t value, std::size_t count) {
    const auto end = offset + static_cast<std::uint64_t>(count);
    if (offset < base_) {
      const auto n = std::min<std::size_t>(count, static_cast<std::size_t>(base_ - offset));
      ram_.fill(offset, value, n);
    }
    const auto window_end = base_ + bytes_;
    if (end > window_end) {
      const auto start = std::max(offset, window_end);
      ram_.fill(start, value, static_cast<std::size_t>(end - start));
    }
  }
};

}  // namespace cosmic_pc
