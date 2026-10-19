#pragma once

#include <cstddef>
#include <stdexcept>
#include <string>

namespace mundo_x::infinite_detail {

// Addresses are decimal strings, not machine-sized integers. The budget is
// checked on the original input so leading-zero aliases cannot bypass it.
inline std::string canonical_address(const std::string& input,
                                     std::size_t max_digits) {
  if (input.empty()) throw std::invalid_argument("An address cannot be empty");
  if (input.size() > max_digits)
    throw std::length_error("Address input exceeds the digit budget");
  for (const char digit : input)
    if (digit < '0' || digit > '9')
      throw std::invalid_argument("An address requires ASCII decimal digits");
  const auto first = input.find_first_not_of('0');
  return first == std::string::npos ? std::string("0") : input.substr(first);
}

inline void require_canonical_address(const std::string& address) {
  if (address.empty() || (address.size() > 1 && address.front() == '0'))
    throw std::invalid_argument("Expected a canonical decimal address");
  for (const char digit : address)
    if (digit < '0' || digit > '9')
      throw std::invalid_argument("Expected a canonical decimal address");
}

inline bool is_odd(const std::string& canonical) {
  require_canonical_address(canonical);
  return ((canonical.back() - '0') & 1) != 0;
}

// Bell pairs are q(2k) <-> q(2k+1). No carry or borrow crosses the final
// decimal digit: an even digit is at most 8, and an odd digit is at least 1.
// Consequently even arbitrarily long addresses retain their exact length.
inline std::string partner_address(const std::string& canonical) {
  require_canonical_address(canonical);
  std::string partner = canonical;
  const bool odd = ((partner.back() - '0') & 1) != 0;
  partner.back() = static_cast<char>(partner.back() + (odd ? -1 : 1));
  return partner;
}

// Numeric ordering for canonical keys. Call canonical_address at the input
// boundary before using addresses in an ordered map or set.
struct AddressLess {
  bool operator()(const std::string& left, const std::string& right) const noexcept {
    if (left.size() != right.size()) return left.size() < right.size();
    return left < right;
  }
};

}  // namespace mundo_x::infinite_detail
