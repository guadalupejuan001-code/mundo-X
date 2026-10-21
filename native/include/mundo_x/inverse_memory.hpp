#pragma once

#include "mundo_x/infinite_core.hpp"

#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <limits>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// Inverse Ether Memory v0.1.
//
// The logical byte domain is rule-first: one background byte describes every
// address, finite overrides describe classical differences, and finite quantum
// bindings explicitly connect selected byte views to eight qubit addresses.
// There is no byte array and no implicit "8 qubits per byte" mapping.
//
// A classical consumer receives bytes only when it asks for a projection.
// Quantum bindings are never measured implicitly: if any bit is not
// deterministic in Z, read() returns a quantum handle instead of inventing a
// classical value. materialize() therefore fails on a genuinely quantum byte.
//
// This class is intentionally independent of PC, graphics, Solar and physics.
// It operates directly on InfiniteCore so the quantum-only runtime can be built
// without those subsystems.
class InverseMemory {
 public:
  struct Limits {
    std::size_t max_address_digits = 4096;
    std::size_t max_overrides = 65536;
    std::size_t max_quantum_bindings = 4096;
    std::size_t max_materialize_bytes = 65536;
  };

  struct ReadResult {
    bool deterministic = true;
    std::uint8_t value = 0;
    std::string handle;
  };

  struct Stats {
    std::uint8_t background_byte = 0;
    std::size_t overrides = 0;
    std::size_t quantum_bindings = 0;
    std::uint64_t revision = 0;
  };

  explicit InverseMemory(std::uint8_t background = 0)
      : InverseMemory(background, Limits{}) {}

  InverseMemory(std::uint8_t background, Limits limits)
      : background_(background), limits_(limits) {
    if (limits_.max_address_digits == 0)
      throw std::invalid_argument("Inverse Memory address digit budget must be positive");
  }

  const Limits& limits() const noexcept { return limits_; }

  Stats stats() const noexcept {
    return {background_, overrides_.size(), bindings_.size(), revision_};
  }

  // Replace the global rule and discard every finite difference/binding.
  void reset(std::uint8_t background) {
    if (background_ == background && overrides_.empty() && bindings_.empty()) return;
    require_revision();
    background_ = background;
    overrides_.clear();
    bindings_.clear();
    ++revision_;
  }

  ReadResult read(const std::string& logical_address, const InfiniteCore& core) const {
    const auto address = canonical(logical_address);
    const auto binding = bindings_.find(address);
    if (binding == bindings_.end()) {
      const auto changed = overrides_.find(address);
      return {true, changed == overrides_.end() ? background_ : changed->second, {}};
    }
    return read_quantum(address, binding->second, core);
  }

  // Classical stores preserve the rule-first representation. An unbound byte
  // equal to the background consumes no per-address state. A bound byte is
  // updated through exact X operations only if its current Z value is already
  // deterministic; a superposed/entangled byte is never collapsed implicitly.
  void store(const std::string& logical_address, std::uint8_t value, InfiniteCore& core) {
    const auto address = canonical(logical_address);
    const auto binding = bindings_.find(address);
    if (binding != bindings_.end()) {
      const auto current = read_quantum(address, binding->second, core);
      if (!current.deterministic)
        throw std::runtime_error("Inverse Memory store requires an explicit quantum decision");
      std::vector<InfiniteCore::Gate> flips;
      for (unsigned bit = 0; bit < 8; ++bit)
        if (((current.value ^ value) >> bit) & 1U)
          flips.push_back({'X', add_small(binding->second, bit, core.limits().max_address_digits), {}});
      if (!flips.empty()) core.apply(flips);
      return;
    }

    const auto old = overrides_.find(address);
    if (value == background_) {
      if (old == overrides_.end()) return;
      require_revision();
      overrides_.erase(old);
      ++revision_;
      return;
    }
    if (old != overrides_.end()) {
      if (old->second == value) return;
      require_revision();
      old->second = value;
      ++revision_;
      return;
    }
    if (overrides_.size() == limits_.max_overrides)
      throw std::length_error("Inverse Memory classical override budget exceeded");
    require_revision();
    overrides_.emplace(address, value);
    ++revision_;
  }

  // A binding is an explicit view, not a global address formula. The byte's
  // eight bits are qbase..qbase+7 only for this selected logical address.
  // The underlying classical rule/delta remains underneath the binding and is
  // visible again after unbind().
  void bind_quantum_byte(const std::string& logical_address,
                         const std::string& qbase,
                         const InfiniteCore& core) {
    const auto address = canonical(logical_address);
    const auto base = infinite_detail::canonical_address(
        qbase, std::min(limits_.max_address_digits, core.limits().max_address_digits));
    (void)add_small(base, 7, core.limits().max_address_digits);
    const auto found = bindings_.find(address);
    if (found != bindings_.end()) {
      if (found->second == base) return;
      require_revision();
      found->second = base;
      ++revision_;
      return;
    }
    if (bindings_.size() == limits_.max_quantum_bindings)
      throw std::length_error("Inverse Memory quantum binding budget exceeded");
    require_revision();
    bindings_.emplace(address, base);
    ++revision_;
  }

  void unbind_quantum_byte(const std::string& logical_address) {
    const auto address = canonical(logical_address);
    const auto found = bindings_.find(address);
    if (found == bindings_.end()) return;
    require_revision();
    bindings_.erase(found);
    ++revision_;
  }

  // A temporary classical projection. The returned vector is the only dense
  // byte storage created by this class. It exists because the caller explicitly
  // requested it and is bounded separately from the logical address domain.
  std::vector<std::uint8_t> materialize(const std::string& first,
                                        std::size_t count,
                                        const InfiniteCore& core) const {
    if (count > limits_.max_materialize_bytes)
      throw std::length_error("Inverse Memory projection exceeds materialization budget");
    std::vector<std::uint8_t> bytes;
    bytes.reserve(count);
    auto address = canonical(first);
    for (std::size_t i = 0; i < count; ++i) {
      const auto value = read(address, core);
      if (!value.deterministic)
        throw std::runtime_error("Inverse Memory projection reached quantum handle " + value.handle);
      bytes.push_back(value.value);
      if (i + 1 != count) address = add_small(address, 1, limits_.max_address_digits);
    }
    return bytes;
  }

  // Independent memory metadata persistence. Quantum bindings contain only
  // addresses; the actual quantum state remains owned by InfiniteCore and must
  // be snapshotted/restored by its owner.
  std::string snapshot() const {
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << "MXIM1 " << unsigned(background_) << '\n';
    for (const auto& item : overrides_)
      out << "O " << item.first << ' ' << unsigned(item.second) << '\n';
    for (const auto& item : bindings_)
      out << "Q " << item.first << ' ' << item.second << '\n';
    return out.str();
  }

  static InverseMemory from_snapshot(const std::string& saved, Limits limits = Limits{}) {
    std::istringstream input(saved);
    input.imbue(std::locale::classic());
    std::string magic, value;
    if (!(input >> magic >> value) || magic != "MXIM1")
      throw std::invalid_argument("Unsupported or malformed Inverse Memory snapshot");
    const auto background = byte_value(value, "Inverse Memory background");
    InverseMemory result(background, limits);
    std::string kind, address, payload;
    while (input >> kind >> address >> payload) {
      address = result.canonical(address);
      if (kind == "O") {
        if (result.overrides_.size() == limits.max_overrides)
          throw std::length_error("Inverse Memory snapshot override budget exceeded");
        if (!result.overrides_.emplace(address, byte_value(payload, "Inverse Memory byte")).second)
          throw std::invalid_argument("Duplicate Inverse Memory override");
      } else if (kind == "Q") {
        const auto base = infinite_detail::canonical_address(payload, limits.max_address_digits);
        if (result.bindings_.size() == limits.max_quantum_bindings)
          throw std::length_error("Inverse Memory snapshot binding budget exceeded");
        if (!result.bindings_.emplace(address, base).second)
          throw std::invalid_argument("Duplicate Inverse Memory binding");
      } else {
        throw std::invalid_argument("Unknown Inverse Memory snapshot record");
      }
    }
    if (!input.eof()) throw std::invalid_argument("Malformed Inverse Memory snapshot");
    return result;
  }

 private:
  using AddressMap = std::map<std::string, std::uint8_t, infinite_detail::AddressLess>;
  using BindingMap = std::map<std::string, std::string, infinite_detail::AddressLess>;

  std::uint8_t background_ = 0;
  Limits limits_;
  AddressMap overrides_;
  BindingMap bindings_;
  std::uint64_t revision_ = 0;

  std::string canonical(const std::string& address) const {
    return infinite_detail::canonical_address(address, limits_.max_address_digits);
  }

  static std::uint8_t byte_value(const std::string& text, const char* name) {
    if (text.empty()) throw std::invalid_argument(std::string(name) + " is empty");
    unsigned value = 0;
    for (const char ch : text) {
      if (ch < '0' || ch > '9')
        throw std::invalid_argument(std::string(name) + " requires decimal digits");
      value = value * 10U + unsigned(ch - '0');
      if (value > 255U) throw std::out_of_range(std::string(name) + " exceeds one byte");
    }
    return static_cast<std::uint8_t>(value);
  }

  static std::string add_small(std::string address, std::size_t amount,
                               std::size_t max_digits) {
    infinite_detail::require_canonical_address(address);
    std::size_t cursor = address.size();
    while (amount != 0 && cursor != 0) {
      --cursor;
      const unsigned sum = unsigned(address[cursor] - '0') + unsigned(amount % 10);
      amount /= 10;
      if (sum >= 10) ++amount;
      address[cursor] = static_cast<char>('0' + sum % 10);
    }
    if (amount != 0) address.insert(0, std::to_string(amount));
    if (address.size() > max_digits)
      throw std::length_error("Inverse Memory address digit budget exceeded");
    return address;
  }

  ReadResult read_quantum(const std::string& logical_address,
                          const std::string& qbase,
                          const InfiniteCore& core) const {
    std::uint8_t value = 0;
    for (unsigned bit = 0; bit < 8; ++bit) {
      const auto q = add_small(qbase, bit, core.limits().max_address_digits);
      const double probability = core.probability_one(q);
      if (probability == 0.5)
        return {false, 0, "qbyte:" + logical_address + "@" + qbase};
      if (probability == 1.0) value |= static_cast<std::uint8_t>(1U << bit);
      else if (probability != 0.0)
        throw std::logic_error("InfiniteCore returned a non-stabilizer Z probability");
    }
    return {true, value, {}};
  }

  void require_revision() const {
    if (revision_ == std::numeric_limits<std::uint64_t>::max())
      throw std::overflow_error("Inverse Memory revision overflow");
  }
};

}  // namespace mundo_x
