#pragma once

#include "sparse_ram.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
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
  using BudgetExceeded = SparseRam::BudgetExceeded;
  using Metrics = SparseRam::Metrics;
  static constexpr std::size_t kPageSize = SparseRam::kPageSize;
  static constexpr std::size_t kPagesPerTable = SparseRam::kPagesPerTable;
  static constexpr std::uint64_t kDefaultPayloadLimit = SparseRam::kDefaultPayloadLimit;

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

  void attach_window(std::uint64_t base, std::size_t bytes, ReadByte read, WriteByte write) {
    if (bytes == 0 || base > size() || static_cast<std::uint64_t>(bytes) > size() - base)
      throw std::invalid_argument("Ether memory window is outside logical RAM");
    if (!read || !write) throw std::invalid_argument("Ether memory window requires read/write callbacks");
    base_ = base;
    bytes_ = bytes;
    read_ = std::move(read);
    write_ = std::move(write);
  }

  std::uint64_t size() const noexcept { return ram_.size(); }
  bool empty() const noexcept { return ram_.empty(); }
  std::size_t allocated_pages() const noexcept { return ram_.allocated_pages(); }
  std::uint64_t allocated_bytes() const noexcept { return ram_.allocated_bytes(); }
  std::uint64_t budget_bytes() const noexcept { return ram_.budget_bytes(); }
  std::uint64_t metadata_bytes() const noexcept {
    return ram_.metadata_bytes() + sizeof(*this) - sizeof(ram_);
  }
  Metrics metrics() const noexcept {
    auto out = ram_.metrics();
    out.metadata_bytes = metadata_bytes();
    return out;
  }
  bool has_window() const noexcept { return bytes_ != 0; }
  std::uint64_t window_base() const noexcept { return base_; }
  std::size_t window_bytes() const noexcept { return bytes_; }

  ByteProxy operator[](std::uint64_t offset) {
    check_range(offset, 1);
    return ByteProxy(*this, offset);
  }
  std::uint8_t operator[](std::uint64_t offset) const { return read_byte(offset); }
  ByteProxy at(std::uint64_t offset) { return (*this)[offset]; }
  std::uint8_t at(std::uint64_t offset) const { return read_byte(offset); }

  std::uint8_t read_byte(std::uint64_t offset) const {
    check_range(offset, 1);
    if (in_window(offset)) return read_(offset - base_);
    return ram_.read_byte(offset);
  }

  void write_byte(std::uint64_t offset, std::uint8_t value) {
    check_range(offset, 1);
    if (in_window(offset)) {
      write_(offset - base_, value);
      return;
    }
    ram_.write_byte(offset, value);
  }

  void read_block(std::uint64_t offset, std::uint8_t* destination, std::size_t count) const {
    check_range(offset, count);
    if (count && !destination) throw std::invalid_argument("MemoryFabric null read destination");
    if (!count) return;
    if (!overlaps_window(offset, count)) {
      ram_.read_block(offset, destination, count);
      return;
    }
    split_read(offset, destination, count);
  }

  // Mixed classical/Ether writes are atomic with respect to expected provider
  // exceptions: Ether bytes are written first with old values retained, and
  // every affected SparseRam page is preflighted before either side commits.
  void write_block(std::uint64_t offset, const std::uint8_t* source, std::size_t count) {
    check_range(offset, count);
    if (count && !source) throw std::invalid_argument("MemoryFabric null write source");
    if (!count) return;
    if (!overlaps_window(offset, count)) {
      ram_.write_block(offset, source, count);
      return;
    }
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
    check_range(offset, count);
    if (!count) return;
    if (!overlaps_window(offset, count)) {
      ram_.fill(offset, value, count);
      return;
    }
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

  // The Ether window does not reserve classical pages. Provider-side qstore8
  // commits a complete byte atomically; CPU rollback restores the old byte.
  void prepare_write(std::uint64_t offset, std::size_t count) {
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
    if (offset >= size()) return false;
    if (!in_window(offset)) return ram_.restore_byte(offset, old_value);
    try {
      write_(offset - base_, old_value);
      return true;
    } catch (...) {
      return false;
    }
  }

  void reset_zero() noexcept { ram_.reset_zero(); }
  void clear() noexcept { ram_.clear(); }
  std::size_t trim_zero_pages() noexcept { return ram_.trim_zero_pages(); }

 private:
  SparseRam ram_;
  std::uint64_t base_ = 0;
  std::size_t bytes_ = 0;
  ReadByte read_;
  WriteByte write_;

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
