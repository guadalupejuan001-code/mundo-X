#pragma once

#include "sparse_ram.hpp"  // Shared host-backing contracts, not a SparseRam instance.

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>

namespace cosmic_pc {

// Guest RAM represented by computational-basis rules and optional coherent
// cells. Absent pages are the zero basis state. A resident page is either an
// eight-byte rule with changed words, or an exact packed basis vector. Only
// explicitly bound cells use amplitudes: their QuantumAuthority is their sole
// value authority, including CPU, DMA, journal and block accesses.
//
// This is a classical simulator: logical capacity does not create host memory,
// and the packed basis states/backing/amplitudes still consume host resources.
// Mutations and maintenance require the owner to join parallel readers first.
class EtherStateRam {
 public:
  static constexpr std::size_t kPageSize = SparseRam::kPageSize;
  static constexpr std::size_t kPatternSize = SparseRam::kPatternSize;
  static constexpr std::size_t kPagesPerTable = SparseRam::kPagesPerTable;
  static constexpr std::uint64_t kDefaultPayloadLimit = SparseRam::kDefaultPayloadLimit;
  using BudgetExceeded = SparseRam::BudgetExceeded;
  using BudgetAdjustment = SparseRam::BudgetAdjustment;
  using BackingCachePolicy = SparseRam::BackingCachePolicy;
  using ReclaimClock = SparseRam::ReclaimClock;
  using PageBacking = SparseRam::PageBacking;
  using Metrics = SparseRam::Metrics;
  using PressureMetrics = SparseRam::PressureMetrics;
  using ReclaimPreparation = SparseRam::ReclaimPreparation;
  using ByteScan = SparseRam::ByteScan;

  class QuantumAuthority {
   public:
    class LocalCheckpoint {
     public:
      virtual ~LocalCheckpoint() = default;
      virtual void restore() noexcept = 0;
    };
    virtual ~QuantumAuthority() = default;
    virtual std::unique_ptr<LocalCheckpoint> checkpoint() { return {}; }
    virtual bool contains(std::uint64_t address) const noexcept = 0;
    virtual bool intersects(std::uint64_t first, std::size_t count) const noexcept {
      for (std::size_t i = 0; i < count; ++i) if (contains(first + i)) return true;
      return false;
    }
    // Return sorted unique addresses in this half-open range. Implementations
    // must support concurrent readers while their state is held stable.
    virtual std::vector<std::uint64_t> bound_addresses(std::uint64_t first,
                                                     std::size_t count) const = 0;
    // Reading an unresolved byte must throw, never measure it implicitly.
    virtual std::uint8_t read(std::uint64_t address) const = 0;
    // A batch either commits completely or leaves the full quantum state
    // unchanged, including phases, correlations, bindings and revision.
    virtual void write(const std::vector<std::pair<std::uint64_t, std::uint8_t>>& bytes) = 0;
    virtual std::uint64_t revision() const noexcept = 0;
    virtual std::size_t state_bytes() const noexcept = 0;
    virtual void reset() noexcept = 0;
    // Optional no-allocation instruction checkpoint. A successful restore
    // reverts all quantum stores in this outer pin scope exactly once; later
    // journal bytes return true without replaying intermediate classical bits.
    virtual void begin_write_scope() noexcept {}
    virtual void end_write_scope() noexcept {}
    virtual void commit_write_scope() noexcept {}
    virtual bool restore_write_scope() noexcept { return false; }
  };

 private:
  using Bytes = std::array<std::uint8_t, kPageSize>;
  // Explicit byte encoding keeps every changed word at ten owned bytes on
  // every ABI; no alignment padding is hidden from the resident budget.
  struct Patch {
    static constexpr std::size_t kBytes = sizeof(std::uint16_t) + sizeof(std::uint64_t);
    std::array<std::uint8_t, kBytes> bytes;
    Patch(std::uint16_t word, std::uint64_t value) noexcept {
      std::memcpy(bytes.data(), &word, sizeof(word));
      std::memcpy(bytes.data() + sizeof(word), &value, sizeof(value));
    }
    std::uint16_t word() const noexcept {
      std::uint16_t value; std::memcpy(&value, bytes.data(), sizeof(value)); return value;
    }
    std::uint64_t value() const noexcept {
      std::uint64_t value;
      std::memcpy(&value, bytes.data() + sizeof(std::uint16_t), sizeof(value)); return value;
    }
  };
  static_assert(sizeof(Patch) == 10);
  static constexpr std::uint32_t kNoCleanSlot = UINT32_MAX;
  struct BasisPage {
    std::unique_ptr<Bytes> packed;
    std::uint64_t background = 0;
    std::vector<Patch> changes;
    std::uint64_t write_epoch = 0;
    std::uint32_t clean_slot = kNoCleanSlot;
    bool spilled = false;
    bool clean() const noexcept { return clean_slot != kNoCleanSlot; }
    std::uint64_t payload() const noexcept {
      return packed ? kPageSize : spilled || (!background && changes.empty()) ? 0 :
          kPatternSize + changes.capacity() * sizeof(Patch);
    }
  };
  struct CacheLine {
    std::uint64_t page = UINT64_MAX;
    std::uint64_t age = 0;
    std::unique_ptr<Bytes> bytes;
  };
  static constexpr std::size_t kMaxCacheLines = 512;
  static constexpr std::size_t kCacheHintSlots = 2048;
  static constexpr std::size_t kPageLookupSlots = 1024;
  static_assert(kMaxCacheLines <= UINT16_MAX);
  using PageMap = std::map<std::uint64_t, BasisPage>;
  using PageEntry = PageMap::value_type;
  struct PageLookupSlot {
    std::atomic<const PageEntry*> entry{nullptr};
  };
  struct Storage {
    explicit Storage(std::uint64_t logical, std::uint64_t budget)
        : logical_bytes(logical), payload_cap(budget) {}
    std::uint64_t logical_bytes;
    std::uint64_t payload_cap;
    PageMap pages;
    // Parallel readers may replace a hint. The owner joins them before
    // changing the page map, so a loaded entry remains valid for the read.
    mutable std::array<PageLookupSlot, kPageLookupSlots> page_lookup;
    // Map entries stay stable until explicitly erased. Clean eviction visits
    // this compact index instead of walking spilled and dirty guest pages.
    std::vector<PageMap::value_type*> clean_pages;
    std::shared_ptr<QuantumAuthority> quantum;
    std::unique_ptr<PageBacking> backing;
    mutable std::mutex read_mutex;
    mutable std::vector<CacheLine> cache;
    // A hint stores a one-based cache slot. Hits still require the exact tag.
    mutable std::array<std::uint16_t, kCacheHintSlots> cache_hints{};
    mutable std::uint64_t cache_age = 0, cache_hits = 0;
    mutable std::uint64_t backing_reads = 0, backing_read_failures = 0;
    mutable std::uint64_t backing_timed_attempts = 0, backing_read_ns = 0;
    mutable std::uint64_t backing_read_max_ns = 0, backing_lock_wait_ns = 0;
    std::uint64_t backing_writes = 0, backing_overwrites = 0, clean_reclaims = 0;
    std::uint64_t payload = 0;
    std::size_t dense_pages = 0, rule_pages = 0, spilled_pages = 0;
    std::size_t pin_depth = 0, clean_history_pages = 512;
    std::uint64_t pin_epoch = 0, reclaim_cursor = 0, compact_cursor = 0, preparation_cursor = 0;
    bool backing_timing = false;
    BackingCachePolicy cache_policy = BackingCachePolicy::Fixed;
    PressureMetrics pressure;
  };

 public:
  class WritePins {
   public:
    WritePins(const WritePins&) = delete;
    WritePins& operator=(const WritePins&) = delete;
    WritePins(WritePins&& other) noexcept
        : owner_(std::exchange(other.owner_, nullptr)), quantum_(std::move(other.quantum_)) {}
    ~WritePins() {
      if (owner_ && --owner_->pin_depth == 0 && quantum_) quantum_->end_write_scope();
    }
   private:
    friend class EtherStateRam;
    explicit WritePins(Storage* owner) noexcept : owner_(owner), quantum_(owner->quantum) {}
    Storage* owner_;
    std::shared_ptr<QuantumAuthority> quantum_;
  };

  static std::uint64_t default_budget(std::uint64_t logical_bytes) noexcept {
    return SparseRam::default_budget(logical_bytes);
  }
  explicit EtherStateRam(std::uint64_t logical_bytes)
      : EtherStateRam(logical_bytes, default_budget(logical_bytes)) {}
  EtherStateRam(std::uint64_t logical_bytes, std::uint64_t payload_cap_bytes)
      : storage_(std::make_unique<Storage>(logical_bytes, payload_cap_bytes)) {}
  EtherStateRam(const EtherStateRam&) = delete;
  EtherStateRam& operator=(const EtherStateRam&) = delete;
  EtherStateRam(EtherStateRam&&) noexcept = default;
  EtherStateRam& operator=(EtherStateRam&&) noexcept = default;

  const char* backend_name() const noexcept { return "ether-state-ram"; }
  std::uint64_t size() const noexcept { return storage_ ? storage_->logical_bytes : 0; }
  bool empty() const noexcept { return size() == 0; }
  std::size_t allocated_pages() const noexcept { return state().dense_pages; }
  std::size_t pattern_pages() const noexcept { return state().rule_pages; }
  std::size_t spilled_pages() const noexcept { return state().spilled_pages; }
  std::uint64_t allocated_bytes() const noexcept { return state().payload; }
  std::uint64_t budget_bytes() const noexcept { return state().payload_cap; }
  std::uint64_t backing_cache_bytes() const noexcept {
    std::size_t allocated = 0;
    for (const auto& line : state().cache) allocated += bool(line.bytes);
    return allocated * kPageSize;
  }
  std::size_t clean_backing_pages() const noexcept { return state().clean_pages.size(); }
  bool has_page_backing() const noexcept { return bool(state().backing); }
  bool has_pinned_writes() const noexcept { return state().pin_depth != 0; }
  std::uint64_t quantum_revision() const noexcept {
    return state().quantum ? state().quantum->revision() : 0;
  }
  std::size_t quantum_state_bytes() const noexcept {
    return state().quantum ? state().quantum->state_bytes() : 0;
  }
  void attach_quantum_authority(std::shared_ptr<QuantumAuthority> authority) {
    if (!authority || state().quantum || has_pinned_writes())
      throw std::invalid_argument("EtherStateRam expects one unpinned quantum authority");
    const auto bindings = authority->bound_addresses(0, static_cast<std::size_t>(size()));
    for (auto address : bindings)
      if (address >= size()) throw std::out_of_range("Quantum cell outside guest RAM");
    const auto pins = pin_writes();
    for (auto address : bindings) prepare_write(address, 1);
    state().quantum = std::move(authority);
    for (auto address : bindings) {
      auto& page = *find_page(address / kPageSize);
      mark_write(address / kPageSize, page);
      (*page.packed)[address % kPageSize] = 0;
    }
  }
  const std::shared_ptr<QuantumAuthority>& quantum_authority() const noexcept { return state().quantum; }
  std::unique_ptr<QuantumAuthority::LocalCheckpoint> checkpoint_quantum() {
    return state().quantum ? state().quantum->checkpoint() : nullptr;
  }

  // The callback publishes the corresponding binding in the provider. All
  // fallible basis preparation precedes it, so there is no second byte owner
  // and no allocation after the authority changes.
  template <class Binder>
  void bind_quantum_byte(std::uint64_t address, Binder&& binder) {
    check_range(address, 1);
    if (!state().quantum || is_quantum(address) || has_pinned_writes())
      throw std::invalid_argument("Quantum bind requires an unbound byte outside a write scope");
    const auto previous = read_byte(address);
    prepare_write(address, 1);
    std::forward<Binder>(binder)(previous);
    auto& page = *find_page(address / kPageSize);
    mark_write(address / kPageSize, page);
    (*page.packed)[address % kPageSize] = 0;
  }
  template <class Unbinder>
  void unbind_quantum_byte(std::uint64_t address, Unbinder&& unbinder) {
    check_range(address, 1);
    if (!is_quantum(address) || has_pinned_writes())
      throw std::invalid_argument("Quantum unbind requires a bound byte outside a write scope");
    // Validate determinism before any representation work. The provider also
    // validates while unbinding, so no measurement is introduced by transfer.
    (void)state().quantum->read(address);
    prepare_write(address, 1);
    const auto value = std::forward<Unbinder>(unbinder)();
    auto& page = *find_page(address / kPageSize);
    mark_write(address / kPageSize, page);
    (*page.packed)[address % kPageSize] = value;
  }

  void attach_page_backing(std::unique_ptr<PageBacking> backing,
                           BackingCachePolicy policy = BackingCachePolicy::Fixed,
                           std::size_t clean_history_pages = 512) {
    if (!backing || state().backing || !clean_history_pages || has_pinned_writes())
      throw std::invalid_argument("EtherStateRam expects one backing and a clean page history");
    auto cache_pages = cache_capacity(state().payload_cap, policy);
    if (policy == BackingCachePolicy::Adaptive)
      cache_pages = std::min<std::size_t>(cache_pages,
          (state().payload_cap - std::min(state().payload, state().payload_cap)) / kPageSize);
    state().cache.resize(cache_pages);
    state().cache_policy = policy;
    state().clean_history_pages = clean_history_pages;
    state().backing = std::move(backing);
  }
  [[nodiscard]] WritePins pin_writes() noexcept {
    auto& s = state();
    if (s.pin_depth++ == 0) {
      if (++s.pin_epoch == 0) {
        for (auto& item : s.pages) item.second.write_epoch = 0;
        s.pin_epoch = 1;
      }
      if (s.quantum) s.quantum->begin_write_scope();
    }
    return WritePins(&s);
  }
  void commit_write_scope() noexcept {
    if (has_pinned_writes() && state().quantum) state().quantum->commit_write_scope();
  }
  void set_backing_read_timing_enabled(bool enabled) noexcept { state().backing_timing = enabled; }

  std::uint64_t metadata_bytes() const noexcept {
    // std::map's three links/color are conservatively charged separately.
    return sizeof(*this) + sizeof(Storage) + state().pages.size() *
        (sizeof(std::pair<const std::uint64_t, BasisPage>) + 4 * sizeof(void*)) +
        state().cache.capacity() * sizeof(CacheLine) +
        state().clean_pages.capacity() * sizeof(PageMap::value_type*);
  }
  Metrics metrics() const noexcept {
    auto result = Metrics{};
    result.logical_bytes = size();
    result.allocated_bytes = allocated_bytes();
    result.budget_bytes = budget_bytes();
    result.metadata_bytes = metadata_bytes();
    result.allocated_pages = allocated_pages();
    result.pattern_pages = pattern_pages();
    result.spilled_pages = spilled_pages();
    result.backing_bytes = static_cast<std::uint64_t>(spilled_pages()) * kPageSize;
    result.backing_reads = state().backing_reads;
    result.backing_writes = state().backing_writes;
    result.backing_cache_hits = state().cache_hits;
    result.backing_cache_bytes = backing_cache_bytes();
    result.backing_cache_capacity_bytes = state().cache.size() * kPageSize;
    result.backing_overwrite_pages = state().backing_overwrites;
    result.page_state_vectors = state().pages.size();
    result.backing_clean_pages = clean_backing_pages();
    result.backing_clean_reclaims = state().clean_reclaims;
    for (const auto& item : state().pages) if (!item.second.changes.empty()) {
      ++result.pattern_delta_pages;
      result.pattern_delta_bytes += item.second.payload();
    }
    return result;
  }
  PressureMetrics pressure_metrics() const noexcept {
    auto result = state().pressure;
    result.backing_timing_enabled = state().backing_timing;
    result.backing_read_attempts = state().backing_reads + state().backing_read_failures;
    result.backing_read_failures = state().backing_read_failures;
    result.backing_timed_attempts = state().backing_timed_attempts;
    result.backing_read_ns = state().backing_read_ns;
    result.backing_read_max_ns = state().backing_read_max_ns;
    result.backing_lock_wait_ns = state().backing_lock_wait_ns;
    return result;
  }

  template <bool Synchronized = true>
  std::uint8_t read_byte(std::uint64_t address) const {
    check_range(address, 1);
    if (is_quantum(address)) return state().quantum->read(address);
    const auto* page = find_page(address / kPageSize);
    if (!page) return 0;
    const auto inside = static_cast<std::size_t>(address % kPageSize);
    if (page->packed) return (*page->packed)[inside];
    if (!page->spilled) return rule_byte(*page, inside);
    std::uint8_t value;
    read_backing<Synchronized>(address / kPageSize, inside, &value, 1);
    return value;
  }
  const std::uint8_t* resident_span(std::uint64_t address, std::size_t count) const noexcept {
    if (!count || address >= size() || count > size() - address ||
        count > kPageSize - address % kPageSize || quantum_intersects(address, count)) return nullptr;
    const auto* page = find_page(address / kPageSize);
    return page && page->packed ? page->packed->data() + address % kPageSize : nullptr;
  }

  // Copy ordinary bytes that can be resolved without backing I/O. Untouched
  // pages and compact rules have exact values without allocating memory.
  template <bool Synchronized = true>
  bool try_read_present_block(std::uint64_t address, std::uint8_t* destination,
                             std::size_t count) const {
    const auto inside = static_cast<std::size_t>(address % kPageSize);
    if (!destination || !count || address >= size() || count > size() - address ||
        count > kPageSize - inside || quantum_intersects(address, count)) return false;
    const auto index = address / kPageSize;
    const auto* page = find_page(index);
    if (!page) {
      std::memset(destination, 0, count);
      return true;
    }
    if (page->packed) {
      std::memcpy(destination, page->packed->data() + inside, count);
      return true;
    }
    if (!page->spilled) {
      copy_rule(*page, inside, destination, count);
      return true;
    }
    const auto& s = state();
    std::unique_lock<std::mutex> lock(s.read_mutex, std::defer_lock);
    const auto lock_start = s.backing_timing ? ReclaimClock::now() : ReclaimClock::time_point{};
    if constexpr (Synchronized) lock.lock();
    if (s.backing_timing) s.backing_lock_wait_ns += elapsed(lock_start);
    return copy_cached_line(s, index, inside, destination, count);
  }

  template <bool Synchronized = true>
  void read_block(std::uint64_t address, std::uint8_t* destination, std::size_t count) const {
    check_range(address, count);
    if (count && !destination) throw std::invalid_argument("EtherStateRam null read destination");
    const auto cells = quantum_values(address, count); // Reject before exposing a partial operand.
    const auto first = address;
    auto* const output = destination;
    while (count) {
      const auto inside = static_cast<std::size_t>(address % kPageSize);
      const auto chunk = std::min(count, kPageSize - inside);
      const auto* page = find_page(address / kPageSize);
      if (!page) std::memset(destination, 0, chunk);
      else if (page->packed) std::memcpy(destination, page->packed->data() + inside, chunk);
      else if (page->spilled) read_backing<Synchronized>(address / kPageSize, inside, destination, chunk);
      else copy_rule(*page, inside, destination, chunk);
      address += chunk; destination += chunk; count -= chunk;
    }
    for (const auto& cell : cells) output[cell.first - first] = cell.second;
  }

  ByteScan scan_until_byte(std::uint64_t first, std::size_t count, std::uint8_t value) const {
    check_range(first, count);
    std::size_t examined = 0;
    std::uint8_t last = 0;
    Bytes bytes;
    while (examined < count) {
      const auto address = first + examined;
      const auto chunk = std::min(count - examined, kPageSize - static_cast<std::size_t>(address % kPageSize));
      // A scan observes bytes in order. Do not reject an unresolved cell after
      // a previous matching byte that the guest would never have accessed.
      if (quantum_intersects(address, chunk)) {
        for (std::size_t i = 0; i < chunk; ++i) {
          last = read_byte(address + i);
          ++examined;
          if (last == value) return {examined, last};
        }
      } else {
        read_block(address, bytes.data(), chunk);
        const auto* match = static_cast<const std::uint8_t*>(std::memchr(bytes.data(), value, chunk));
        if (match) return {examined + static_cast<std::size_t>(match - bytes.data()) + 1, value};
        last = bytes[chunk - 1];
        examined += chunk;
      }
    }
    return {examined, last};
  }

  void write_byte(std::uint64_t address, std::uint8_t value) {
    check_range(address, 1);
    if (is_quantum(address)) {
      state().quantum->write({{address, value}});
      return;
    }
    auto* page = find_page(address / kPageSize);
    const auto inside = static_cast<std::size_t>(address % kPageSize);
    if (!page || !page->packed) {
      if ((!page || !page->spilled) && (page ? rule_byte(*page, inside) : 0) == value) return;
      prepare_write(address, 1);
      page = find_page(address / kPageSize);
    }
    mark_write(address / kPageSize, *page);
    (*page->packed)[inside] = value;
  }

  // Materialization is transactional with respect to values. Eviction may
  // change residency, but every target remains unchanged on any exception.
  void prepare_write(std::uint64_t address, std::size_t count) {
    prepare_dense_pages(address, count, false);
  }

 private:
  void prepare_dense_pages(std::uint64_t address, std::size_t count, bool replace_full_pages) {
    check_range(address, count);
    if (!count) return;
    const auto first = address / kPageSize;
    const auto last = (address + count - 1) / kPageSize;
    const auto end = address + count;
    const auto replaces_page = [&](std::uint64_t index) {
      return replace_full_pages && !(index == first && address % kPageSize) &&
          !(index == last && end % kPageSize);
    };
    std::uint64_t additional = 0;
    std::size_t missing = 0;
    for (auto index = first; index <= last; ++index) {
      const auto* page = find_page(index);
      if (!page || !page->packed) {
        additional += kPageSize - (page ? page->payload() : 0);
        ++missing;
      }
    }
    ensure_space(additional, first, last, missing);
    struct Prepared { std::uint64_t index; std::unique_ptr<Bytes> bytes; };
    std::vector<Prepared> prepared;
    prepared.reserve(missing);
    std::size_t promotions = 0;
    // Allocate nodes before publishing any new representation. Empty nodes
    // represent zero exactly and are safe even if a later allocation fails.
    for (auto index = first; index <= last; ++index) {
      auto* page = find_page(index);
      if (page && page->packed) continue;
      auto bytes = std::make_unique<Bytes>();
      if (!page || replaces_page(index)) bytes->fill(0);
      else if (page->spilled) {
        ++promotions;
        ++state().pressure.write_promotion_attempts;
        const auto start = ReclaimClock::now();
        read_backing<true>(index, 0, bytes->data(), kPageSize);
        state().pressure.write_promotion_ns += elapsed(start);
      } else copy_rule(*page, 0, bytes->data(), kPageSize);
      prepared.push_back({index, std::move(bytes)});
    }
    reserve_clean_slots(promotions);
    for (const auto& item : prepared) {
      if (!find_page(item.index)) {
        auto result = state().pages.try_emplace(item.index);
        add_accounting(result.first->second);
      }
    }
    for (auto& item : prepared) {
      auto& entry = *state().pages.find(item.index);
      auto& page = entry.second;
      const bool replaced = replaces_page(item.index);
      const bool promoted = page.spilled && !replaced;
      if (page.spilled && replaced) {
        // Every allocation and partial-page recovery has succeeded. This path
        // is used only when no undo operand or quantum batch can still fail.
        state().backing->discard(item.index);
        invalidate_cache(item.index);
        ++state().backing_overwrites;
      }
      remove_accounting(page);
      page.packed = std::move(item.bytes);
      std::vector<Patch>{}.swap(page.changes);
      page.background = 0;
      page.spilled = false;
      // A promoted backing copy remains clean until the actual write.
      if (promoted) {
        mark_clean(entry);
        ++state().pressure.write_promotions;
      }
      add_accounting(page);
    }
    for (auto index = first; index <= last; ++index) mark_pin(*find_page(index));
  }

 public:
  void write_block(std::uint64_t address, const std::uint8_t* source, std::size_t count,
                   std::uint8_t* previous = nullptr) {
    check_range(address, count);
    if (count && !source) throw std::invalid_argument("EtherStateRam null write source");
    if (!count) return;
    const auto inside = static_cast<std::size_t>(address % kPageSize);
    if (count <= kPageSize - inside) {
      const auto index = address / kPageSize;
      if (auto* page = find_page(index); page && page->packed &&
          allocated_bytes() <= resident_payload_limit() && !quantum_intersects(address, count)) {
        // A dense ordinary page needs no materialization. Capture the undo
        // bytes before dirtying its backing and keep it pinned as usual.
        auto* data = page->packed->data() + inside;
        if (previous) std::memmove(previous, data, count);
        mark_write(index, *page);
        std::memmove(data, source, count);
        return;
      }
    }
    const auto bindings = quantum_addresses(address, count);
    std::vector<std::pair<std::uint64_t, std::uint8_t>> updates;
    updates.reserve(bindings.size());
    for (auto cell : bindings) updates.emplace_back(cell, source[cell - address]);
    // Read the complete old operand before materialization or either commit.
    if (previous) read_block(address, previous, count);
    // Full ordinary pages need no old data when the caller requests no undo.
    // A coherent batch retains the old basis state until its provider commits.
    prepare_dense_pages(address, count, previous == nullptr && bindings.empty());
    if (!updates.empty()) state().quantum->write(updates);
    commit_bytes(address, source, count, bindings);
  }

  void fill(std::uint64_t address, std::uint8_t value, std::size_t count) {
    check_range(address, count);
    if (!count) return;
    const auto bindings = quantum_addresses(address, count);
    std::vector<std::pair<std::uint64_t, std::uint8_t>> updates;
    updates.reserve(bindings.size());
    for (auto cell : bindings) updates.emplace_back(cell, value);
    if (value == 0) {
      fill_zero(address, count, updates);
      return;
    }
    // Zeroing large untouched regions is a rule operation, not a dense 64 GiB
    // allocation. Pins retain dense pages needed for an instruction rollback.
    if (!has_pinned_writes()) {
      const auto pins = pin_writes();
      const auto first_full = address / kPageSize + (address % kPageSize != 0);
      const auto end = address + count;
      const auto end_full = end / kPageSize;
      std::vector<std::uint64_t> coherent_pages;
      for (auto cell : bindings) {
        const auto page = cell / kPageSize;
        if (page >= first_full && page < end_full &&
            (coherent_pages.empty() || coherent_pages.back() != page))
          coherent_pages.push_back(page);
      }
      std::vector<std::uint64_t> full_pages;
      if (value == 0) {
        for (auto it = state().pages.lower_bound(first_full);
             it != state().pages.end() && it->first < end_full; ++it)
          full_pages.push_back(it->first);
      } else {
        full_pages.reserve(static_cast<std::size_t>(end_full - std::min(first_full, end_full)));
        for (auto page = first_full; page < end_full; ++page) full_pages.push_back(page);
      }
      if (address % kPageSize)
        prepare_write(address, std::min(count, kPageSize - static_cast<std::size_t>(address % kPageSize)));
      if (end % kPageSize && end / kPageSize != address / kPageSize)
        prepare_write(end - end % kPageSize, static_cast<std::size_t>(end % kPageSize));
      if (first_full == end_full && address % kPageSize == 0 && end % kPageSize)
        prepare_write(address, count);
      for (auto index : coherent_pages) prepare_write(index * kPageSize, kPageSize);
      std::uint64_t next_payload = 0, old_payload = 0;
      for (auto index : full_pages) {
        if (std::binary_search(coherent_pages.begin(), coherent_pages.end(), index)) continue;
        const auto* page = find_page(index);
        old_payload += page ? page->payload() : 0;
        if (value) next_payload += kPatternSize;
      }
      const auto additional = next_payload > old_payload ? next_payload - old_payload : 0;
      const auto first = address / kPageSize, last = (end - 1) / kPageSize;
      ensure_space(additional, first, last, 0);
      if (value) for (auto index : full_pages) if (!find_page(index)) {
        const auto result = state().pages.try_emplace(index);
        add_accounting(result.first->second);
      }
      if (!updates.empty()) state().quantum->write(updates);
      const auto pattern = std::uint64_t{0x0101010101010101ULL} * value;
      for (auto index : full_pages)
        if (!std::binary_search(coherent_pages.begin(), coherent_pages.end(), index)) set_rule(index, pattern);
      for (auto index : coherent_pages) {
        auto& page = *find_page(index);
        mark_write(index, page);
        page.packed->fill(value);
      }
      commit_fill_edges(address, value, count);
      for (auto cell : bindings) (*find_page(cell / kPageSize)->packed)[cell % kPageSize] = 0;
      return;
    }
    prepare_write(address, count);
    if (!updates.empty()) state().quantum->write(updates);
    for_each_chunk(address, count, [&](std::uint64_t index, std::size_t inside, std::size_t chunk) {
      auto& page = *find_page(index);
      mark_write(index, page);
      std::memset(page.packed->data() + inside, value, chunk);
    });
    // Bound cells deliberately have no independent value in the basis image.
    for (auto cell : bindings) (*find_page(cell / kPageSize)->packed)[cell % kPageSize] = 0;
  }

  bool restore_byte(std::uint64_t address, std::uint8_t value) noexcept {
    if (address >= size()) return false;
    if (is_quantum(address)) {
      if (state().quantum->restore_write_scope()) return true;
      try { state().quantum->write({{address, value}}); return true; } catch (...) { return false; }
    }
    auto* page = find_page(address / kPageSize);
    if (!page || !page->packed)
      return (!page || !page->spilled) && (page ? rule_byte(*page, address % kPageSize) : 0) == value;
    mark_write(address / kPageSize, *page);
    (*page->packed)[address % kPageSize] = value;
    return true;
  }

  bool try_fill_repeating_page(std::uint64_t target, std::uint64_t pattern) {
    check_range(target, kPageSize);
    if (target % kPageSize || has_pinned_writes() || quantum_intersects(target, kPageSize)) return false;
    const auto index = target / kPageSize;
    const auto* current = find_page(index);
    ensure_space(pattern && (!current || current->spilled) ? kPatternSize : 0, index, index, 0);
    if (pattern && !current) {
      const auto result = state().pages.try_emplace(index);
      add_accounting(result.first->second);
    }
    set_rule(index, pattern);
    return true;
  }
  bool try_copy_compact_page(std::uint64_t source, std::uint64_t target) {
    check_range(source, kPageSize); check_range(target, kPageSize);
    if (source % kPageSize || target % kPageSize || quantum_intersects(source, kPageSize) ||
        quantum_intersects(target, kPageSize)) return false;
    const auto* page = find_page(source / kPageSize);
    if (page && (page->packed || page->spilled || !page->changes.empty())) return false;
    return try_fill_repeating_page(target, page ? page->background : 0);
  }
  bool try_copy_resident_block(std::uint64_t source, std::uint64_t target, std::size_t count) {
    check_range(source, count); check_range(target, count);
    if (!count || count > kPageSize - source % kPageSize || count > kPageSize - target % kPageSize ||
        (source < target + count && target < source + count) ||
        quantum_intersects(source, count) || quantum_intersects(target, count)) return false;
    const auto* from = find_page(source / kPageSize);
    auto* to = find_page(target / kPageSize);
    if (!from || !from->packed || !to || !to->packed) return false;
    mark_write(target / kPageSize, *to);
    std::memcpy(to->packed->data() + target % kPageSize, from->packed->data() + source % kPageSize, count);
    return true;
  }
  bool try_copy_to_implicit_page(std::uint64_t source, std::uint64_t target) {
    check_range(source, kPageSize); check_range(target, kPageSize);
    if (source % kPageSize || target % kPageSize || has_pinned_writes() ||
        quantum_intersects(source, kPageSize) || quantum_intersects(target, kPageSize)) return false;
    const auto* from = find_page(source / kPageSize);
    const auto index = target / kPageSize;
    if (!from || !from->packed || find_page(index)) return false;

    // Direct copies require space for both pages. The ordinary copy path
    // can stage source bytes and reclaim payload before writing its target.
    const auto limit = resident_payload_limit();
    if (limit < kPageSize || allocated_bytes() > limit - kPageSize) return false;
    auto payload = std::make_unique<Bytes>(*from->packed);
    auto& page = state().pages.try_emplace(index).first->second;
    page.packed = std::move(payload);
    add_accounting(page);
    return true;
  }

  bool compact_repeating_page(std::uint64_t address) noexcept {
    if (address >= size()) return false;
    return compact_page(address / kPageSize);
  }
  bool compact_exact_repeating_page(std::uint64_t address) noexcept {
    return address < size() && compact_page(address / kPageSize, 0);
  }
  std::size_t compact_repeating_pages(std::size_t max_pages,
                                      std::size_t* visited_pages = nullptr) noexcept {
    if (visited_pages) *visited_pages = 0;
    if (!max_pages || !allocated_pages() || has_pinned_writes()) return 0;
    std::size_t compacted = 0, scanned = 0;
    auto it = state().pages.lower_bound(state().compact_cursor);
    if (it == state().pages.end()) it = state().pages.begin();
    const auto visit_limit = std::min(max_pages, state().pages.size());
    while (scanned < visit_limit && allocated_pages()) {
      const auto index = (it++)->first;
      ++scanned;
      state().compact_cursor = index + 1;
      // Periodic work packs repeating pages. Pressure reclaim also considers deltas.
      if (compact_page(index, 0)) ++compacted;
      if (it == state().pages.end()) it = state().pages.begin();
    }
    if (visited_pages) *visited_pages = scanned;
    return compacted;
  }
  std::size_t trim_zero_pages() noexcept {
    std::size_t removed = 0;
    for (auto it = state().pages.begin(); it != state().pages.end();) {
      const auto index = it++->first;
      const auto* page = find_page(index);
      if (page && !pinned(*page) && !page->spilled && is_zero(*page)) {
        erase_page(index); ++removed;
      }
    }
    return removed;
  }

  ReclaimPreparation prepare_reclaim(std::size_t ready_limit, ReclaimClock::time_point deadline) {
    ReclaimPreparation result;
    ready_limit = std::min(ready_limit, state().clean_history_pages);
    if (!state().backing || has_pinned_writes() || state().dense_pages == clean_backing_pages() ||
        clean_backing_pages() >= ready_limit || ReclaimClock::now() >= deadline) return result;
    std::array<PageBacking::PageWrite, PageBacking::kMaxWritePages> writes{};
    const auto selection_limit = std::min(writes.size(), ready_limit - clean_backing_pages());
    const auto visit_limit = std::min(state().pages.size(), 4 * kPagesPerTable);
    std::size_t count = 0, candidates = 0;
    auto it = state().pages.lower_bound(state().preparation_cursor);
    for (std::size_t visited = 0; visited < visit_limit && candidates < selection_limit &&
         state().dense_pages > clean_backing_pages() && ReclaimClock::now() < deadline; ++visited) {
      if (it == state().pages.end()) it = state().pages.begin();
      const auto current = it++;
      const auto index = current->first;
      auto& page = current->second;
      state().preparation_cursor = index + 1;
      if (!page.packed || page.clean()) continue;
      ++candidates;
      const auto before = allocated_bytes();
      if (compact_page(index, (3 * kPageSize / 4 - kPatternSize) / sizeof(Patch))) {
        const auto released = before - allocated_bytes();
        result.released_bytes += released;
        if (kPageSize - released <= kPageSize / 2) ++result.compact_up_to_2k;
        else ++result.compact_up_to_3k;
      } else {
        ++result.dense_pages;
        writes[count++] = {index, page.packed->data()};
      }
    }
    if (!count) return result;
    if (ReclaimClock::now() >= deadline) {
      state().preparation_cursor = writes[0].page;
      return result;
    }
    try {
      reserve_clean_slots(count);
      const auto committed = state().backing->prepare_pages(writes.data(), count, deadline);
      if (committed > count) throw std::logic_error("Backing returned an invalid committed prefix");
      for (std::size_t i = 0; i < committed; ++i) {
        mark_clean(*state().pages.find(writes[i].page));
        invalidate_cache(writes[i].page);
      }
      result.backed_pages = committed;
      state().backing_writes += committed;
      if (committed < count) state().preparation_cursor = writes[committed].page;
    } catch (...) {
      state().preparation_cursor = writes[0].page;
      result.backing_failed = true;
    }
    return result;
  }

  BudgetAdjustment adjust_payload_budget(std::uint64_t target, std::size_t reclaim_pages,
      ReclaimClock::time_point deadline = ReclaimClock::time_point::max(),
      std::size_t clean_reclaim_pages = 0) {
    if (has_pinned_writes()) return BudgetAdjustment::Blocked;
    const auto desired_cache = state().backing ? cache_capacity(target, state().cache_policy) : 0;
    const auto guest_target = target -
        (state().cache_policy == BackingCachePolicy::Adaptive ? desired_cache * kPageSize : 0);
    const auto before = allocated_bytes();
    if (before > guest_target) {
      reclaim_clean_pages(guest_target, clean_reclaim_pages, deadline);
      const SparseRam::PacedReclaimScope scope(
          deadline != ReclaimClock::time_point::max() && reclaim_pages &&
          allocated_bytes() > guest_target ? state().backing.get() : nullptr);
      bool yielded = false;
      for (std::size_t reclaimed = 0; reclaimed < reclaim_pages &&
           allocated_bytes() > guest_target && !yielded && ReclaimClock::now() < deadline;) {
        const auto released = reclaim_budget_group(guest_target, reclaim_pages - reclaimed, deadline, yielded);
        if (!released) break;
        reclaimed += released;
      }
    }
    if (allocated_bytes() > guest_target) {
      const auto used = allocated_bytes();
      if (state().backing && used < before) {
        // Keep a small write reserve while Android schedules the next slice.
        // The old cap would let the guest refill the entire reclaimed region.
        const auto limit = resident_payload_limit();
        const auto old_headroom = limit - std::min(limit, used);
        const auto reserve = std::min<std::uint64_t>(std::max<std::size_t>(1, reclaim_pages / 2),
                                                     old_headroom / kPageSize) * kPageSize;
        state().payload_cap -= old_headroom - reserve;
      }
      return state().backing || allocated_pages() ? BudgetAdjustment::Reclaiming : BudgetAdjustment::Blocked;
    }
    // Preserve retained cache entries across resizing, including growth.
    if (desired_cache < state().cache.size()) {
      std::vector<CacheLine> resized;
      resized.reserve(desired_cache);
      // Favor recently read pages when the cache must shrink.
      std::sort(state().cache.begin(), state().cache.end(),
          [](const CacheLine& a, const CacheLine& b) {
            if ((a.page == UINT64_MAX) != (b.page == UINT64_MAX)) return a.page != UINT64_MAX;
            return a.age > b.age;
          });
      for (std::size_t i = 0; i < desired_cache; ++i) resized.push_back(std::move(state().cache[i]));
      state().cache.swap(resized);
      state().cache_hints.fill(0);
      for (const auto& line : state().cache)
        if (line.page != UINT64_MAX) remember_cache_line(state(), line);
    } else state().cache.resize(desired_cache);
    state().payload_cap = target;
    return BudgetAdjustment::Applied;
  }

  void reset_zero() noexcept {
    std::vector<PageMap::value_type*>{}.swap(state().clean_pages);
    for (auto& slot : state().page_lookup) slot.entry.store(nullptr, std::memory_order_relaxed);
    state().pages.clear();
    state().payload = 0;
    state().dense_pages = state().rule_pages = state().spilled_pages = 0;
    state().backing_reads = state().backing_writes = state().backing_read_failures = 0;
    state().backing_timed_attempts = state().backing_read_ns = state().backing_read_max_ns = 0;
    state().backing_lock_wait_ns = state().cache_hits = state().backing_overwrites = 0;
    state().cache_age = state().clean_reclaims = state().reclaim_cursor = state().compact_cursor = 0;
    state().preparation_cursor = 0;
    state().pressure = {};
    state().cache_hints.fill(0);
    for (auto& line : state().cache) { line.page = UINT64_MAX; line.age = 0; line.bytes.reset(); }
    if (state().backing) state().backing->reset();
    if (state().quantum) state().quantum->reset();
  }
  void clear() noexcept { reset_zero(); }

 private:
  std::unique_ptr<Storage> storage_;
  Storage& state() noexcept { return *storage_; }
  const Storage& state() const noexcept { return *storage_; }
  void fill_zero(std::uint64_t address, std::size_t count,
                  const std::vector<std::pair<std::uint64_t, std::uint8_t>>& updates) {
    const bool retain_dense = has_pinned_writes();
    const auto pins = pin_writes();
    const auto first = address / kPageSize, last = (address + count - 1) / kPageSize;
    const auto end = address + count;
    std::vector<std::uint64_t> affected;
    for (auto it = state().pages.lower_bound(first); it != state().pages.end() && it->first <= last; ++it) {
      affected.push_back(it->first);
      // A previous journal may need the current dense representation. Keep
      // all resident targets stable while partial edge pages are prepared.
      if (it->second.packed) mark_pin(it->second);
    }
    for (auto index : affected) {
      auto* page = find_page(index);
      if (!page || page->packed) continue;
      const auto begin = std::max(address, index * kPageSize);
      const auto finish = begin + std::min<std::uint64_t>(end - begin, kPageSize - begin % kPageSize);
      const bool full_page = finish - begin == kPageSize;
      // Untouched zeros remain implicit, even inside a CPU pin scope. A full
      // unpinned compact/backed page can be discarded after quantum commit;
      // only a partial nonzero state needs its previous bytes recovered.
      if ((full_page && !pinned(*page)) || (!page->spilled && is_zero(*page))) continue;
      prepare_write(begin, static_cast<std::size_t>(finish - begin));
    }
    if (!updates.empty()) state().quantum->write(updates);
    for (auto index : affected) {
      auto* page = find_page(index);
      if (!page) continue;
      const auto begin = std::max(address, index * kPageSize);
      const auto finish = begin + std::min<std::uint64_t>(end - begin, kPageSize - begin % kPageSize);
      const bool full_page = finish - begin == kPageSize;
      if (full_page && (!page->packed || !retain_dense)) erase_page(index);
      else if (page->packed) {
        mark_write(index, *page);
        std::memset(page->packed->data() + begin % kPageSize, 0, static_cast<std::size_t>(finish - begin));
      }
    }
  }
  static std::uint64_t elapsed(ReclaimClock::time_point start) noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        ReclaimClock::now() - start).count());
  }
  static std::size_t cache_capacity(std::uint64_t budget, BackingCachePolicy policy) noexcept {
    // Fixed cache is the legacy host-side auxiliary allowance. Adaptive cache
    // is charged inside the resident limit and reserves at most 1/128 of it.
    if (policy == BackingCachePolicy::Fixed) return 16;
    return static_cast<std::size_t>(std::min<std::uint64_t>(kMaxCacheLines, budget / (128 * kPageSize)));
  }
  void check_range(std::uint64_t address, std::size_t count) const {
    if (address > size() || static_cast<std::uint64_t>(count) > size() - address)
      throw std::out_of_range("EtherStateRam address outside logical RAM");
  }
  static std::size_t page_lookup_slot(std::uint64_t index) noexcept {
    return (index ^ (index >> 10) ^ (index >> 20)) & (kPageLookupSlots - 1);
  }
  BasisPage* find_page(std::uint64_t index) noexcept {
    return const_cast<BasisPage*>(std::as_const(*this).find_page(index));
  }
  const BasisPage* find_page(std::uint64_t index) const noexcept {
    const auto& data = state();
    auto& slot = data.page_lookup[page_lookup_slot(index)].entry;
    if (const auto* entry = slot.load(std::memory_order_relaxed))
      if (entry->first == index) return &entry->second;
    const auto it = data.pages.find(index);
    if (it == data.pages.end()) return nullptr;
    slot.store(&*it, std::memory_order_relaxed);
    return &it->second;
  }
  bool is_quantum(std::uint64_t address) const noexcept {
    return state().quantum && state().quantum->contains(address);
  }
  bool quantum_intersects(std::uint64_t address, std::size_t count) const noexcept {
    return count && state().quantum && state().quantum->intersects(address, count);
  }
  std::vector<std::uint64_t> quantum_addresses(std::uint64_t address, std::size_t count) const {
    if (!quantum_intersects(address, count)) return {};
    auto result = state().quantum->bound_addresses(address, count);
    auto previous = address;
    bool first = true;
    for (auto cell : result) {
      if (cell < address || cell - address >= count || (!first && cell <= previous))
        throw std::logic_error("Quantum authority returned invalid cell addresses");
      previous = cell; first = false;
    }
    return result;
  }
  std::vector<std::pair<std::uint64_t, std::uint8_t>> quantum_values(
      std::uint64_t address, std::size_t count) const {
    std::vector<std::pair<std::uint64_t, std::uint8_t>> result;
    const auto bindings = quantum_addresses(address, count);
    result.reserve(bindings.size());
    for (auto cell : bindings) result.emplace_back(cell, state().quantum->read(cell));
    return result;
  }
  static std::uint8_t rule_byte(const BasisPage& page, std::size_t inside) noexcept {
    const auto word = static_cast<std::uint16_t>(inside / kPatternSize);
    auto bits = page.background;
    const auto it = std::lower_bound(page.changes.begin(), page.changes.end(), word,
        [](const Patch& patch, std::uint16_t key) { return patch.word() < key; });
    if (it != page.changes.end() && it->word() == word) bits = it->value();
    return reinterpret_cast<const std::uint8_t*>(&bits)[inside % kPatternSize];
  }
  static void copy_rule(const BasisPage& page, std::size_t inside,
                        std::uint8_t* destination, std::size_t count) noexcept {
    const auto* background = reinterpret_cast<const std::uint8_t*>(&page.background);
    for (std::size_t i = 0; i < count; ++i) destination[i] = background[(inside + i) % kPatternSize];
    for (const auto& change : page.changes) {
      const auto begin = std::size_t(change.word()) * kPatternSize;
      if (begin + kPatternSize <= inside) continue;
      if (begin >= inside + count) break;
      const auto first = std::max(begin, inside), end = std::min(begin + kPatternSize, inside + count);
      std::memcpy(destination + first - inside,
                  change.bytes.data() + sizeof(std::uint16_t) + first - begin, end - first);
    }
  }
  void add_accounting(const BasisPage& page) noexcept {
    state().payload += page.payload();
    if (page.packed) ++state().dense_pages;
    else if (page.spilled) ++state().spilled_pages;
    else if (page.payload()) ++state().rule_pages;
  }
  void remove_accounting(const BasisPage& page) noexcept {
    state().payload -= page.payload();
    if (page.packed) --state().dense_pages;
    else if (page.spilled) --state().spilled_pages;
    else if (page.payload()) --state().rule_pages;
  }
  void reserve_clean_slots(std::size_t count) {
    if (!count) return;
    auto& clean = state().clean_pages;
    const auto limit = std::min<std::size_t>(kNoCleanSlot, clean.max_size());
    if (count > limit - clean.size()) throw std::length_error("EtherStateRam clean page index is full");
    const auto required = clean.size() + count;
    if (required > clean.capacity()) {
      const auto growth = std::min(clean.capacity(), limit - clean.capacity());
      clean.reserve(std::max(required, clean.capacity() + growth));
    }
  }
  // Preparation and promotion reserve capacity before publishing page state.
  void mark_clean(PageMap::value_type& entry) noexcept {
    auto& clean = state().clean_pages;
    entry.second.clean_slot = static_cast<std::uint32_t>(clean.size());
    clean.push_back(&entry);
  }
  void forget_clean(BasisPage& page) noexcept {
    if (!page.clean()) return;
    auto& clean = state().clean_pages;
    auto* last = clean.back();
    clean[page.clean_slot] = last;
    last->second.clean_slot = page.clean_slot;
    clean.pop_back();
    page.clean_slot = kNoCleanSlot;
  }
  bool pinned(const BasisPage& page) const noexcept {
    return state().pin_depth && page.write_epoch == state().pin_epoch;
  }
  void mark_pin(BasisPage& page) noexcept { if (state().pin_depth) page.write_epoch = state().pin_epoch; }
  static std::size_t cache_hint_index(std::uint64_t page) noexcept {
    return (page ^ (page >> 8) ^ (page >> 16)) & (kCacheHintSlots - 1);
  }
  static void remember_cache_line(const Storage& s, const CacheLine& line) noexcept {
    s.cache_hints[cache_hint_index(line.page)] = static_cast<std::uint16_t>(&line - s.cache.data() + 1);
  }
  static CacheLine* find_cache_line(const Storage& s, std::uint64_t index) noexcept {
    const auto slot = s.cache_hints[cache_hint_index(index)];
    if (slot && slot <= s.cache.size()) {
      auto& line = s.cache[slot - 1];
      if (line.page == index) return &line;
    }
    for (auto& line : s.cache) if (line.page == index) {
      remember_cache_line(s, line);
      return &line;
    }
    return nullptr;
  }
  static bool copy_cached_line(const Storage& s, std::uint64_t index, std::size_t inside,
                               std::uint8_t* destination, std::size_t count) noexcept {
    auto* line = find_cache_line(s, index);
    if (!line) return false;
    ++s.cache_hits;
    line->age = ++s.cache_age;
    std::memcpy(destination, line->bytes->data() + inside, count);
    return true;
  }
  void invalidate_cache(std::uint64_t index) noexcept {
    if (auto* line = find_cache_line(state(), index)) {
      line->page = UINT64_MAX;
      state().cache_hints[cache_hint_index(index)] = 0;
    }
  }
  void mark_write(std::uint64_t index, BasisPage& page) noexcept {
    mark_pin(page);
    if (page.clean()) {
      forget_clean(page);
      state().backing->discard(index);
    } else if (!page.spilled) return;
    // Decoded bytes originate from a spilled page. A promotion carrying
    // those bytes remains clean until its first mutation.
    invalidate_cache(index);
  }
  void erase_page(std::uint64_t index) noexcept {
    const auto it = state().pages.find(index);
    if (it == state().pages.end()) return;
    if (state().backing && (it->second.clean() || it->second.spilled)) state().backing->discard(index);
    forget_clean(it->second);
    invalidate_cache(index);
    remove_accounting(it->second);
    auto& slot = state().page_lookup[page_lookup_slot(index)].entry;
    if (slot.load(std::memory_order_relaxed) == &*it)
      slot.store(nullptr, std::memory_order_relaxed);
    state().pages.erase(it);
  }
  void set_rule(std::uint64_t index, std::uint64_t pattern) noexcept {
    if (!pattern) { erase_page(index); return; }
    auto& page = *find_page(index);
    mark_write(index, page);
    if (page.spilled && state().backing) state().backing->discard(index);
    remove_accounting(page);
    page.packed.reset(); std::vector<Patch>{}.swap(page.changes);
    page.background = pattern; page.spilled = false;
    add_accounting(page);
  }
  template <class Function>
  static void for_each_chunk(std::uint64_t address, std::size_t count, Function&& fn) {
    while (count) {
      const auto inside = static_cast<std::size_t>(address % kPageSize);
      const auto chunk = std::min(count, kPageSize - inside);
      fn(address / kPageSize, inside, chunk);
      address += chunk; count -= chunk;
    }
  }
  void commit_bytes(std::uint64_t address, const std::uint8_t* source, std::size_t count,
                     const std::vector<std::uint64_t>& bindings) noexcept {
    for_each_chunk(address, count, [&](std::uint64_t index, std::size_t inside, std::size_t chunk) {
      auto& page = *find_page(index);
      mark_write(index, page);
      std::memmove(page.packed->data() + inside, source, chunk);
      source += chunk;
    });
    for (auto cell : bindings) (*find_page(cell / kPageSize)->packed)[cell % kPageSize] = 0;
  }
  void commit_fill_edges(std::uint64_t address, std::uint8_t value, std::size_t count) noexcept {
    for_each_chunk(address, count, [&](std::uint64_t index, std::size_t inside, std::size_t chunk) {
      if (!inside && chunk == kPageSize) return;
      auto& page = *find_page(index);
      mark_write(index, page);
      std::memset(page.packed->data() + inside, value, chunk);
      if (state().quantum) for (std::size_t i = inside; i < inside + chunk; ++i)
        if (is_quantum(index * kPageSize + i)) (*page.packed)[i] = 0;
    });
  }

  template <bool Synchronized>
  void read_backing(std::uint64_t index, std::size_t inside,
                     std::uint8_t* destination, std::size_t count) const {
    auto& s = state();
    std::unique_lock<std::mutex> lock(s.read_mutex, std::defer_lock);
    const auto lock_start = s.backing_timing ? ReclaimClock::now() : ReclaimClock::time_point{};
    if constexpr (Synchronized) lock.lock();
    if (s.backing_timing) s.backing_lock_wait_ns += elapsed(lock_start);
    if (copy_cached_line(s, index, inside, destination, count)) return;
    Bytes scratch;
    CacheLine* selected = nullptr;
    if (!s.cache.empty()) selected = &*std::min_element(s.cache.begin(), s.cache.end(),
        [](const CacheLine& a, const CacheLine& b) {
          if ((a.page == UINT64_MAX) != (b.page == UINT64_MAX)) return a.page == UINT64_MAX;
          return a.age < b.age;
        });
    auto allocation = selected && !selected->bytes ? std::make_unique<Bytes>() : nullptr;
    // Read into a temporary: a failing backend must not corrupt a valid cache
    // line or publish a partial page under a different address.
    const auto start = s.backing_timing ? ReclaimClock::now() : ReclaimClock::time_point{};
    try { s.backing->read(index, scratch.data()); ++s.backing_reads; }
    catch (...) {
      ++s.backing_read_failures;
      if (s.backing_timing) record_backing_time(start);
      throw;
    }
    if (s.backing_timing) record_backing_time(start);
    if (selected) {
      if (allocation) selected->bytes = std::move(allocation);
      *selected->bytes = scratch; selected->page = index; selected->age = ++s.cache_age;
      remember_cache_line(s, *selected);
    }
    std::memcpy(destination, scratch.data() + inside, count);
  }
  void record_backing_time(ReclaimClock::time_point start) const noexcept {
    const auto duration = elapsed(start);
    ++state().backing_timed_attempts;
    state().backing_read_ns += duration;
    state().backing_read_max_ns = std::max(state().backing_read_max_ns, duration);
  }
  static bool is_zero(const BasisPage& page) noexcept {
    if (!page.packed) return page.background == 0 && page.changes.empty();
    for (auto byte : *page.packed) if (byte) return false;
    return true;
  }
  bool compact_page(std::uint64_t index, std::size_t max_changes = 32) noexcept {
    auto* page = find_page(index);
    if (!page || !page->packed || pinned(*page)) return false;
    std::uint64_t background;
    std::memcpy(&background, page->packed->data(), kPatternSize);
    if (!max_changes) {
      const auto* bytes = page->packed->data();
      if (std::memcmp(bytes, bytes + kPatternSize, kPageSize - kPatternSize) != 0) return false;
      set_rule(index, background);
      return true;
    }
    std::size_t changes = 0, nonzero = 0;
    for (std::size_t word = 0; word < kPageSize / kPatternSize; ++word) {
      std::uint64_t bits;
      std::memcpy(&bits, page->packed->data() + word * kPatternSize, kPatternSize);
      changes += bits != background; nonzero += bits != 0;
    }
    if (nonzero < changes) { background = 0; changes = nonzero; }
    if (!changes && !background) { erase_page(index); return true; }
    if (changes > max_changes || kPatternSize + changes * sizeof(Patch) >= kPageSize) return false;
    try {
      std::vector<Patch> patches;
      patches.reserve(changes);
      for (std::size_t word = 0; word < kPageSize / kPatternSize; ++word) {
        std::uint64_t bits;
        std::memcpy(&bits, page->packed->data() + word * kPatternSize, kPatternSize);
        if (bits != background) patches.push_back({static_cast<std::uint16_t>(word), bits});
      }
      if (kPatternSize + patches.capacity() * sizeof(Patch) >= kPageSize) return false;
      mark_write(index, *page); // Rule now owns the value, so discard a redundant backing copy.
      remove_accounting(*page);
      page->packed.reset(); page->background = background; page->changes = std::move(patches);
      add_accounting(*page);
      return true;
    } catch (...) { return false; }
  }
  void reclaim_clean_pages(std::uint64_t target, std::size_t limit,
                           ReclaimClock::time_point deadline) noexcept {
    auto& clean = state().clean_pages;
    for (; limit && !clean.empty() && allocated_bytes() > target && ReclaimClock::now() < deadline; --limit) {
      auto& entry = *clean.back();
      state().reclaim_cursor = entry.first + 1;
      mark_spilled(entry.first, entry.second);
    }
  }
  std::uint64_t reclaim_candidate(std::uint64_t excluded_first, std::uint64_t excluded_last,
                                   bool prefer_clean = false,
                                   ReclaimClock::time_point deadline = ReclaimClock::time_point::max(),
                                   std::uint64_t first_pending = UINT64_MAX) noexcept {
    auto eligible = [&](const auto& item) {
      return !item.second.spilled && (item.second.packed || state().backing) && !pinned(item.second) &&
          !(item.first >= excluded_first && item.first <= excluded_last);
    };
    if (prefer_clean) {
      for (const auto* entry : state().clean_pages)
        if (eligible(*entry)) return entry->first;
    }
    const auto count = state().pages.size();
    // Retain compact basis rules while a dense state can free a full page.
    // In particular, shrinking sparse writes must compact the remaining dense
    // pages before falling back to QSSD for the already compacted rules.
    for (unsigned pass = 0; pass < 2; ++pass) {
      auto it = state().pages.lower_bound(state().reclaim_cursor);
      for (std::size_t scanned = 0; scanned < count; ++scanned) {
        if (it == state().pages.end()) it = state().pages.begin();
        if (it == state().pages.end()) break;
        if ((scanned % 64) == 0 && deadline != ReclaimClock::time_point::max() &&
            ReclaimClock::now() >= deadline) {
          state().reclaim_cursor = it->first;
          return UINT64_MAX;
        }
        const auto current = it++;
        if (current->first == first_pending) return UINT64_MAX;
        if (eligible(*current) && (pass != 0 || current->second.packed)) {
          state().reclaim_cursor = current->first + 1; return current->first;
        }
      }
    }
    return UINT64_MAX;
  }
  std::size_t reclaim_budget_group(std::uint64_t target, std::size_t limit,
                                   ReclaimClock::time_point deadline, bool& yielded) {
    const bool grouped = state().backing && state().backing->supports_grouped_writes();
    std::array<PageBacking::PageWrite, PageBacking::kMaxWritePages> writes{};
    std::size_t count = 0, released = 0;
    while (count < writes.size() && released + count < limit &&
           allocated_bytes() - count * kPageSize > target && ReclaimClock::now() < deadline) {
      const auto candidate = reclaim_candidate(UINT64_MAX, 0, false, deadline,
                                                 count ? writes.front().page : UINT64_MAX);
      if (candidate == UINT64_MAX) break;
      auto& page = *find_page(candidate);
      if (page.clean()) mark_spilled(candidate, page);
      else if (!compact_page(candidate)) {
        if (!state().backing) break;
        if (grouped && page.packed) {
          writes[count++] = {candidate, page.packed->data()};
          continue;
        }
        if (!spill_page(candidate, deadline)) {
          state().reclaim_cursor = candidate;
          yielded = true;
          break;
        }
      }
      ++released;
    }
    if (!count) return released;
    if (ReclaimClock::now() >= deadline) {
      state().reclaim_cursor = writes.front().page;
      yielded = true;
      return released;
    }
    const auto start = ReclaimClock::now();
    std::size_t committed;
    try {
      committed = state().backing->write_pages(writes.data(), count, deadline);
    } catch (const std::exception&) {
      state().pressure.spill_ns += elapsed(start);
      // A group may exceed the remaining file capacity with a viable prefix.
      // Keep each successful prefix page before reporting a single-page error.
      for (committed = 0; committed < count && ReclaimClock::now() < deadline; ++committed) {
        state().reclaim_cursor = writes[committed].page;
        if (!spill_page(writes[committed].page, deadline)) break;
        state().reclaim_cursor = writes[committed].page + 1;
      }
      yielded = committed < count;
      if (yielded) state().reclaim_cursor = writes[committed].page;
      if (!committed) throw;
      return released + committed;
    }
    if (committed > count) throw std::logic_error("Backing returned an invalid committed prefix");
    state().pressure.spill_ns += elapsed(start);
    state().backing_writes += committed;
    for (std::size_t i = 0; i < committed; ++i)
      mark_spilled(writes[i].page, *find_page(writes[i].page));
    if (committed < count) {
      state().reclaim_cursor = writes[committed].page;
      yielded = true;
    }
    return released + committed;
  }
  bool spill_page(std::uint64_t index, ReclaimClock::time_point deadline) {
    auto& page = *find_page(index);
    if (!state().backing || page.spilled || pinned(page)) return false;
    const bool clean = page.clean();
    if (!clean) {
      Bytes expanded;
      const auto* bytes = page.packed ? page.packed->data() : expanded.data();
      if (!page.packed) copy_rule(page, 0, expanded.data(), expanded.size());
      const auto start = ReclaimClock::now();
      if (state().backing->supports_grouped_writes()) {
        const PageBacking::PageWrite write{index, bytes};
        const auto committed = state().backing->write_pages(&write, 1, deadline);
        if (!committed) return false;
        if (committed != 1) throw std::logic_error("Backing returned an invalid committed prefix");
      } else state().backing->write(index, bytes);
      state().pressure.spill_ns += elapsed(start);
      ++state().backing_writes;
    }
    mark_spilled(index, page);
    return true;
  }
  void mark_spilled(std::uint64_t index, BasisPage& page) noexcept {
    const bool clean = page.clean();
    const auto released = page.payload();
    remove_accounting(page);
    page.packed.reset(); std::vector<Patch>{}.swap(page.changes); page.background = 0;
    page.spilled = true;
    forget_clean(page);
    add_accounting(page);
    invalidate_cache(index);
    ++state().pressure.spilled_pages;
    state().pressure.reclaimed_bytes += released;
    if (clean) { ++state().clean_reclaims; ++state().pressure.clean_reclaims; }
  }
  std::uint64_t resident_payload_limit() const noexcept {
    const auto cache = state().cache_policy == BackingCachePolicy::Adaptive ?
        state().cache.size() * kPageSize : 0;
    return state().payload_cap >= cache ? state().payload_cap - cache : 0;
  }
  void ensure_space(std::uint64_t additional, std::uint64_t excluded_first,
                      std::uint64_t excluded_last, std::size_t requested) {
    const auto available = resident_payload_limit();
    if (additional <= available && allocated_bytes() <= available - additional) return;
    const auto start = ReclaimClock::now();
    ++state().pressure.forced_reclaims;
    state().pressure.requested_pages += requested;
    try {
      if (additional > available)
        throw BudgetExceeded("EtherStateRam payload budget exceeded: write set exceeds the resident limit");
      std::size_t declined = 0;
      while (allocated_bytes() > available - additional) {
        const auto selection = ReclaimClock::now();
        const auto index = reclaim_candidate(excluded_first, excluded_last, true);
        if (index == UINT64_MAX)
          throw BudgetExceeded("EtherStateRam payload budget exceeded: no reclaimable unpinned states");
        ++state().pressure.compact_attempts;
        const auto compact_start = ReclaimClock::now();
        const auto before = allocated_bytes();
        // At a hard unbacked limit, only release exact repeating/zero pages
        // during the guest write. Maintenance can compact sparse deltas
        // separately, without changing admission at an instruction boundary.
        const bool compacted = state().backing ? compact_page(index) : compact_page(index, 0);
        state().pressure.compact_ns += elapsed(compact_start);
        state().pressure.selection_ns += elapsed(selection);
        if (compacted) {
          declined = 0;
          ++state().pressure.compacted_pages;
          state().pressure.reclaimed_bytes += before - allocated_bytes();
        } else if (!state().backing) {
          if (++declined >= state().pages.size())
            throw BudgetExceeded("EtherStateRam payload budget exceeded: additional backing capacity is required");
        } else if (!spill_page(index, ReclaimClock::time_point::max()))
          throw BudgetExceeded("EtherStateRam payload budget exceeded: backing declined the page write");
      }
    } catch (...) {
      ++state().pressure.forced_failures;
      const auto duration = elapsed(start);
      state().pressure.forced_reclaim_ns += duration;
      state().pressure.forced_reclaim_max_ns = std::max(state().pressure.forced_reclaim_max_ns, duration);
      throw;
    }
    const auto duration = elapsed(start);
    state().pressure.forced_reclaim_ns += duration;
    state().pressure.forced_reclaim_max_ns = std::max(state().pressure.forced_reclaim_max_ns, duration);
  }
};

}  // namespace cosmic_pc
