#pragma once

#include "mundo_x/infinite_address.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory>
#include <queue>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x::memory_detail {

// A bounded directory with independently shared immutable-on-read pages.
// Copies of the directory share leaf maps; only a changed page is cloned.
// This is a CPU data structure, not quantum RAM or a dense byte allocation.
template<class Value, std::size_t Shards = 256>
class CowShardedMap {
  static_assert(Shards != 0 && (Shards & (Shards - 1)) == 0,
                "Shards must be a power of two");
  using Page = std::map<std::string, Value, infinite_detail::AddressLess>;

 public:
  std::size_t size() const noexcept { return size_; }
  bool empty() const noexcept { return size_ == 0; }

  const Value* get(const std::string& address) const {
    const auto& page = pages_[shard(address)];
    if (!page) return nullptr;
    const auto it = page->find(address);
    return it == page->end() ? nullptr : &it->second;
  }

  bool insert(const std::string& address, const Value& value) {
    if (get(address)) return false;
    auto& page = writable_page(shard(address));
    if (!page.emplace(address, value).second) return false;
    ++size_;
    return true;
  }

  void assign_existing(const std::string& address, const Value& value) {
    if (!get(address)) throw std::out_of_range("missing Ether Memory entry");
    writable_page(shard(address)).at(address) = value;
  }

  bool erase(const std::string& address) {
    if (!get(address)) return false;
    const auto removed = writable_page(shard(address)).erase(address);
    size_ -= removed;
    return removed != 0;
  }

  // Merge the already-sorted leaf iterators. Snapshot order stays numeric
  // without materializing a second N-entry list or copying address strings.
  template<class Fn>
  void for_each_sorted(Fn&& emit) const {
    struct Cursor { const Page* page; typename Page::const_iterator it; };
    struct Later {
      bool operator()(const Cursor& a, const Cursor& b) const {
        return infinite_detail::AddressLess{}(b.it->first, a.it->first);
      }
    };
    std::priority_queue<Cursor, std::vector<Cursor>, Later> pending;
    for (const auto& page : pages_)
      if (page && !page->empty()) pending.push({page.get(), page->cbegin()});
    while (!pending.empty()) {
      auto cursor = pending.top();
      pending.pop();
      emit(*cursor.it);
      if (++cursor.it != cursor.page->cend()) pending.push(cursor);
    }
  }

 private:
  std::array<std::shared_ptr<Page>, Shards> pages_{};
  std::size_t size_ = 0;

  static std::size_t shard(const std::string& address) noexcept {
    // Fixed hashing is for distribution only; it is not used as an identity,
    // does not affect serialized order and creates no probabilistic results.
    std::uint64_t hash = UINT64_C(14695981039346656037);
    for (const unsigned char digit : address) {
      hash ^= digit;
      hash *= UINT64_C(1099511628211);
    }
    return static_cast<std::size_t>(hash & (Shards - 1));
  }

  Page& writable_page(std::size_t index) {
    auto& page = pages_[index];
    if (!page) page = std::make_shared<Page>();
    else if (!page.unique()) page = std::make_shared<Page>(*page);
    return *page;
  }
};

}  // namespace mundo_x::memory_detail
