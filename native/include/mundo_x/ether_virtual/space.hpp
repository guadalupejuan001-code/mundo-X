#pragma once

#include <cstdint>
#include <iterator>
#include <map>
#include <memory>
#include <stdexcept>
#include <typeindex>
#include <typeinfo>
#include <utility>

namespace mundo_x::ether_virtual {

// Region registration is a control operation. Consumers resolve once and
// execute the region's own rules through the typed handle.
class Space final {
 public:
  template <class Rule> struct Access {
    std::shared_ptr<Rule> region;
    std::uint64_t offset;
  };

  explicit Space(std::uint64_t bytes) : bytes_(bytes) {
    if (!bytes) throw std::invalid_argument("Ether Virtual space requires a nonempty extent");
  }

  Space(const Space&) = delete;
  Space& operator=(const Space&) = delete;

  template <class Rule, class... Args>
  [[nodiscard]] std::shared_ptr<Rule> create(std::uint64_t base, std::uint64_t bytes,
                                             Args&&... args) {
    if (!bytes || base > bytes_ || bytes > bytes_ - base)
      throw std::out_of_range("Ether Virtual region outside its space");

    const auto end = base + bytes;
    const auto next = regions_.lower_bound(base);
    if ((next != regions_.end() && end > next->first) ||
        (next != regions_.begin() && std::prev(next)->second.end > base))
      throw std::invalid_argument("Ether Virtual regions overlap");

    auto region = std::make_shared<Rule>(bytes, std::forward<Args>(args)...);
    regions_.emplace_hint(next, base, Entry{end, typeid(Rule), region});
    return region;
  }

  template <class Rule>
  [[nodiscard]] Access<Rule> resolve(std::uint64_t address, std::uint64_t bytes) const {
    if (bytes) {
      const auto next = regions_.upper_bound(address);
      if (next != regions_.begin()) {
        const auto& [base, entry] = *std::prev(next);
        if (address < entry.end && bytes <= entry.end - address) {
          if (entry.rule != typeid(Rule)) throw std::bad_cast();
          return {std::static_pointer_cast<Rule>(entry.region), address - base};
        }
      }
    }
    throw std::out_of_range("Ether Virtual range does not belong to one region");
  }

  // Unmap the address range. A retained handle keeps that region alive.
  bool remove(std::uint64_t base) { return regions_.erase(base) != 0; }

 private:
  struct Entry {
    std::uint64_t end;
    std::type_index rule;
    std::shared_ptr<void> region;
  };

  const std::uint64_t bytes_;
  std::map<std::uint64_t, Entry> regions_;
};

}  // namespace mundo_x::ether_virtual
