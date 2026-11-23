#pragma once

#include "boot_timing.hpp"
#include "sparse_ram.hpp"
#include "ether_state_ram.hpp"

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

// Every ordinary CPU, DMA, block and journal access uses one selected RAM
// authority. State-RAM builds own EtherStateRam; no SparseRam object exists in
// that profile. A separate reserved device window remains available for the
// board's legacy command device and is not the general RAM authority.
class MemoryFabric {
 public:
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
  using RamAuthority = EtherStateRam;
  static constexpr bool kUsesStateRam = true;
#else
  using RamAuthority = SparseRam;
  static constexpr bool kUsesStateRam = false;
#endif
  using ReadByte = std::function<std::uint8_t(std::uint64_t)>;
  using WriteByte = std::function<void(std::uint64_t, std::uint8_t)>;
  using ReadWindowBlock = std::function<void(std::uint64_t, std::uint8_t*, std::size_t)>;
  using WriteWindowBlock = std::function<void(std::uint64_t, const std::uint8_t*, std::size_t)>;
  using BudgetExceeded = SparseRam::BudgetExceeded;
  using Metrics = SparseRam::Metrics;
  using PressureMetrics = SparseRam::PressureMetrics;
  static constexpr std::size_t kPageSize = SparseRam::kPageSize;
  static constexpr std::size_t kPagesPerTable = SparseRam::kPagesPerTable;
  static constexpr std::uint64_t kDefaultPayloadLimit = SparseRam::kDefaultPayloadLimit;

  // The board uses this scope only while its executor owns all guest RAM.
  class ExclusiveRamReads {
   public:
    explicit ExclusiveRamReads(MemoryFabric& memory) noexcept
        : memory_(memory), previous_(std::exchange(memory.exclusive_ram_reads_, true)) {}
    ~ExclusiveRamReads() { memory_.exclusive_ram_reads_ = previous_; }
    ExclusiveRamReads(const ExclusiveRamReads&) = delete;
    ExclusiveRamReads& operator=(const ExclusiveRamReads&) = delete;

   private:
    MemoryFabric& memory_;
    bool previous_;
  };

  // One optional consumer of writes to a range of ordinary physical RAM.
  // Offsets in pages() are relative to that range, even at a partial last page.
  class WriteWatch {
   public:
    // Half-open byte interval within a watched page.
    struct Bounds { std::uint16_t begin = kPageSize, end = 0; };
    static_assert(kPageSize <= UINT16_MAX);
    explicit WriteWatch(std::size_t max_bytes)
        : bounds_(max_bytes / kPageSize + (max_bytes % kPageSize != 0)) {
      pages_.reserve(bounds_.size());
    }
    bool pending() const noexcept { return full_ || !pages_.empty(); }
    bool full() const noexcept { return full_; }
    const std::vector<std::uint32_t>& pages() const noexcept { return pages_; }
    Bounds bounds(std::uint32_t page) const noexcept { return bounds_[page]; }

   private:
    friend class MemoryFabric;
    void clear() noexcept {
      for (auto page : pages_) bounds_[page] = {};
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
      const auto first_byte = std::max(offset, base_) - base_;
      const auto end_byte = std::min(offset + bytes, end_) - base_;
      for (auto page = first_byte / kPageSize; page <= (end_byte - 1) / kPageSize; ++page) {
        const auto start = page * kPageSize;
        const auto begin = static_cast<std::uint16_t>(std::max(first_byte, start) - start);
        const auto end = static_cast<std::uint16_t>(std::min(end_byte - start, std::uint64_t(kPageSize)));
        auto& span = bounds_[page];
        if (!span.end) pages_.push_back(static_cast<std::uint32_t>(page));
        span.begin = std::min(span.begin, begin);
        span.end = std::max(span.end, end);
      }
    }
    std::uint64_t base_ = 0;
    std::uint64_t end_ = 0;
    bool full_ = true;
    std::vector<Bounds> bounds_;
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
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    if (ram_.quantum_authority() && ram_.quantum_authority()->intersects(base, bytes))
      throw std::invalid_argument("Device window overlaps bound quantum RAM cells");
#endif
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

  const char* backend_name() const noexcept {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    return ram_.backend_name();
#else
    return "sparse-ram";
#endif
  }
  std::size_t quantum_state_bytes() const noexcept {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    return ram_.quantum_state_bytes();
#else
    return 0;
#endif
  }
  std::uint64_t quantum_revision() const noexcept {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    return ram_.quantum_revision();
#else
    return 0;
#endif
  }
  void attach_quantum_authority(std::shared_ptr<EtherStateRam::QuantumAuthority> authority) {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    if (authority && bytes_ && authority->intersects(base_, bytes_))
      throw std::invalid_argument("Quantum authority overlaps the device window");
    ram_.attach_quantum_authority(std::move(authority));
    notify_quantum_state_changed();
#else
    (void)authority;
    throw std::logic_error("This build does not select EtherStateRam");
#endif
  }
  template <class Binder>
  void bind_quantum_byte(std::uint64_t address, Binder&& binder) {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    if (in_window(address)) throw std::invalid_argument("Quantum binding overlaps a device window");
    ram_.bind_quantum_byte(address, std::forward<Binder>(binder));
    notify_quantum_state_changed();
#else
    (void)address; (void)binder;
    throw std::logic_error("This build does not select EtherStateRam");
#endif
  }
  template <class Unbinder>
  void unbind_quantum_byte(std::uint64_t address, Unbinder&& unbinder) {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    ram_.unbind_quantum_byte(address, std::forward<Unbinder>(unbinder));
    notify_quantum_state_changed();
#else
    (void)address; (void)unbinder;
    throw std::logic_error("This build does not select EtherStateRam");
#endif
  }
  // Owner-only, after a gate/measurement/binding/restore outside a CPU store.
  // Cache readers also compare revisions so forgetting this hook cannot use
  // stale instructions or page translations. The hook invalidates the display.
  void notify_quantum_state_changed() noexcept {
    observed_quantum_revision_ = quantum_revision();
    invalidate_page_tables();
    if (code_page_versions_) code_page_versions_->clear();
    invalidate_write_watch();
  }

  std::uint64_t size() const noexcept { return ram_.size(); }
  bool empty() const noexcept { return ram_.empty(); }
  std::size_t allocated_pages() const noexcept { return ram_.allocated_pages(); }
  std::size_t pattern_pages() const noexcept { return ram_.pattern_pages(); }
  bool has_page_backing() const noexcept { return ram_.has_page_backing(); }
  bool has_pinned_writes() const noexcept { return ram_.has_pinned_writes(); }
  void attach_page_backing(std::unique_ptr<SparseRam::PageBacking> backing,
                           SparseRam::BackingCachePolicy policy = SparseRam::BackingCachePolicy::Fixed,
                           std::size_t clean_history_pages = 512) {
    ram_.attach_page_backing(std::move(backing), policy, clean_history_pages);
    if (code_page_versions_) code_page_versions_->clear();
  }
  [[nodiscard]] RamAuthority::WritePins pin_writes() noexcept { return ram_.pin_writes(); }
  void commit_write_scope() noexcept {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    ram_.commit_write_scope();
#endif
  }
  std::uint64_t allocated_bytes() const noexcept { return ram_.allocated_bytes(); }
  std::uint64_t backing_cache_bytes() const noexcept { return ram_.backing_cache_bytes(); }
  std::uint64_t budget_bytes() const noexcept { return ram_.budget_bytes(); }
  std::size_t clean_backing_pages() const noexcept { return ram_.clean_backing_pages(); }
  // The owner must join parallel readers before changing instrumentation.
  void set_backing_read_timing_enabled(bool enabled) noexcept {
    ram_.set_backing_read_timing_enabled(enabled);
  }
  PressureMetrics pressure_metrics() const noexcept { return ram_.pressure_metrics(); }
  SparseRam::ReclaimPreparation prepare_reclaim(
      std::size_t ready_limit, SparseRam::ReclaimClock::time_point deadline) {
    return ram_.prepare_reclaim(ready_limit, deadline);
  }
  SparseRam::BudgetAdjustment adjust_payload_budget(
      std::uint64_t target, std::size_t reclaim_pages,
      SparseRam::ReclaimClock::time_point deadline = SparseRam::ReclaimClock::time_point::max(),
      std::size_t clean_reclaim_pages = 0) {
    return ram_.adjust_payload_budget(target, reclaim_pages, deadline, clean_reclaim_pages);
  }
  std::uint64_t metadata_bytes() const noexcept {
    return ram_.metadata_bytes() + sizeof(*this) - sizeof(ram_) +
        (code_page_versions_ ? sizeof(CodePageVersions) : 0) +
        (write_watch_ ? sizeof(WriteWatch) + write_watch_->bounds_.capacity() * sizeof(WriteWatch::Bounds) +
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
  const WriteWatch* write_watch() const noexcept {
    if (write_watch_ && observed_quantum_revision_ != quantum_revision()) write_watch_->invalidate();
    return write_watch_.get();
  }
  void move_write_watch(std::uint64_t base, std::size_t bytes) {
    if (!write_watch_) return;
    if (!bytes || base > size() || bytes > size() - base ||
        (bytes - 1) / kPageSize >= write_watch_->bounds_.size() || overlaps_window(base, bytes))
      throw std::invalid_argument("MemoryFabric write watch range");
    write_watch_->range(base, bytes);
  }
  void invalidate_write_watch() noexcept {
    if (write_watch_) write_watch_->invalidate();
  }
  void acknowledge_write_watch() noexcept {
    if (observed_quantum_revision_ != quantum_revision()) notify_quantum_state_changed();
    if (write_watch_) write_watch_->clear();
  }

  void enable_code_page_versions(bool enabled) noexcept {
    if (!enabled) code_page_versions_.reset();
    else if (!code_page_versions_)
      code_page_versions_.reset(new (std::nothrow) CodePageVersions{});
  }
  std::uint64_t code_page_version(std::uint64_t offset) const noexcept {
    if (observed_quantum_revision_ != quantum_revision()) return 0;
    return code_page_versions_ ? code_page_versions_->get(offset / kPageSize) : 0;
  }
  std::uint64_t watch_code_page(std::uint64_t offset) noexcept {
    if (observed_quantum_revision_ != quantum_revision()) notify_quantum_state_changed();
    return code_page_versions_ ? code_page_versions_->watch(offset / kPageSize) : 0;
  }

  std::uint64_t page_table_epoch() const noexcept {
    return observed_quantum_revision_ == quantum_revision() ? page_table_epoch_ : 0;
  }
  void watch_page_table(std::uint64_t offset) noexcept {
    if (observed_quantum_revision_ != quantum_revision()) notify_quantum_state_changed();
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
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    if (exclusive_ram_reads_) return ram_.read_byte<false>(offset);
#endif
    return ram_.read_byte(offset);
  }

  const std::uint8_t* resident_ram_span(std::uint64_t offset, unsigned bytes,
                                       bool record_timing = true) const noexcept {
    const BootScalarTiming::Scope sample(record_timing ? boot_scalar_timing_ : nullptr, BootTimePart::Ram);
    if (overlaps_window(offset, bytes)) return nullptr;
    return ram_.resident_span(offset, bytes);
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
      if (exclusive_ram_reads_) ram_.read_block<false>(offset, destination, count);
      else ram_.read_block(offset, destination, count);
      return;
    }
    if (has_window_block(offset, count)) {
      read_window_block_(offset - base_, destination, count);
      return;
    }
    split_read(offset, destination, count);
  }

  // The motherboard holds mappings and RAM stable until its readers join.
  // Worker reads never call an Ether window provider or the shared timer.
  void read_parallel_ram_block(std::uint64_t offset, std::uint8_t* destination,
                               std::size_t count) const {
    check_range(offset, count);
    if (overlaps_window(offset, count)) throw std::invalid_argument("Parallel read requires ordinary RAM");
    ram_.read_block(offset, destination, count);
  }

  SparseRam::ByteScan scan_ram_until_byte(std::uint64_t offset, std::size_t count,
                                        std::uint8_t value) const {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, count);
    if (overlaps_window(offset, count)) throw std::invalid_argument("Byte scan requires ordinary RAM");
    return ram_.scan_until_byte(offset, count, value);
  }

  // Mixed RAM/window writes reserve their complete undo data before changing
  // values. Each RAM part can include quantum cells, so a later part may fail
  // after an earlier part committed. Its local checkpoint must not rewind an
  // enclosing CPU transaction or discard stores preceding this method.
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
    std::vector<std::uint8_t> old(count);
    split_read(offset, old.data(), count);
    auto checkpoint = checkpoint_mixed_quantum(offset, count);
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
      restore_mixed_write(offset, old, committed, checkpoint);
      throw;
    }
  }

  // Capture old RAM bytes in the same pass that commits the store. The
  // backing preflight finishes before either byte range is modified.
  void write_ram_block_with_previous(std::uint64_t offset, const std::uint8_t* source,
                                     std::uint8_t* previous, std::size_t count) {
    const BootScalarTiming::Scope sample(boot_scalar_timing_, BootTimePart::Ram);
    check_range(offset, count);
    if (overlaps_window(offset, count))
      throw std::invalid_argument("A RAM undo capture cannot overlap the Ether window");
    note_write(offset, count);
    ram_.write_block(offset, source, count, previous);
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
    std::vector<std::uint8_t> old(count);
    split_read(offset, old.data(), count);
    auto checkpoint = checkpoint_mixed_quantum(offset, count);
    std::size_t committed = 0;
    try {
      const auto first = std::max(offset, base_);
      const auto end = std::min(offset + static_cast<std::uint64_t>(count), base_ + bytes_);
      for (auto address = first; address < end; ++address) {
        write_(address - base_, value);
        ++committed;
      }
      fill_classical_parts(offset, value, count);
    } catch (...) {
      restore_mixed_write(offset, old, committed, checkpoint);
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
  std::size_t compact_repeating_pages(std::size_t max_pages,
                                      std::size_t* visited_pages = nullptr) noexcept {
    return ram_.compact_repeating_pages(max_pages, visited_pages);
  }
  bool compact_repeating_page(std::uint64_t offset) noexcept {
    return ram_.compact_repeating_page(offset);
  }
  bool compact_exact_repeating_page(std::uint64_t offset) noexcept {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    return ram_.compact_exact_repeating_page(offset);
#else
    return ram_.compact_repeating_page(offset);
#endif
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
  bool exclusive_ram_reads_ = false;
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
  RamAuthority ram_;
  std::uint64_t observed_quantum_revision_ = 0;
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
  using QuantumCheckpoint = EtherStateRam::QuantumAuthority::LocalCheckpoint;
  std::unique_ptr<QuantumCheckpoint> checkpoint_mixed_quantum(std::uint64_t offset,
                                                            std::size_t count) {
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
    const auto& authority = ram_.quantum_authority();
    if (authority && authority->intersects(offset, count)) {
      auto checkpoint = ram_.checkpoint_quantum();
      if (!checkpoint)
        throw std::logic_error("Mixed quantum/window writes require a local rollback checkpoint");
      return checkpoint;
    }
#else
    (void)offset; (void)count;
#endif
    return {};
  }
  void restore_mixed_write(std::uint64_t offset, const std::vector<std::uint8_t>& previous,
                           std::size_t window_committed,
                           const std::unique_ptr<QuantumCheckpoint>& checkpoint) noexcept {
    if (checkpoint) checkpoint->restore();
    // prepare_write and the live pin keep every ordinary destination dense.
    // The local checkpoint already restored quantum cells; using restore_byte
    // on them here would incorrectly invoke the outer CPU undo checkpoint.
    for (std::size_t i = previous.size(); i != 0;) {
      --i;
      const auto address = offset + i;
      if (in_window(address)) continue;
#if defined(MUNDO_X_PC_STATE_RAM) && MUNDO_X_PC_STATE_RAM
      if (checkpoint && ram_.quantum_authority()->contains(address)) continue;
#endif
      (void)ram_.restore_byte(address, previous[i]);
    }
    const auto first = std::max(offset, base_);
    while (window_committed) {
      const auto address = first + --window_committed;
      try { write_(address - base_, previous[address - offset]); } catch (...) {}
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
