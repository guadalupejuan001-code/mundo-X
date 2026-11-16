#pragma once

#include <cstdint>

namespace cosmic_pc::wide_integer {

struct DoubleWord {
  std::uint64_t high, low;
};

inline DoubleWord negate(DoubleWord value) {
  return {~value.high + (value.low == 0), std::uint64_t{0} - value.low};
}

inline DoubleWord multiply(std::uint64_t lhs, std::uint64_t rhs) {
#if defined(__SIZEOF_INT128__)
  const auto product = static_cast<__uint128_t>(lhs) * rhs;
  return {std::uint64_t(product >> 64), std::uint64_t(product)};
#else
  constexpr std::uint64_t half = 0xffffffffULL;
  const auto a = lhs & half, b = lhs >> 32;
  const auto c = rhs & half, d = rhs >> 32;
  const auto low = a * c;
  const auto cross = b * c + (low >> 32);
  const auto middle = a * d + (cross & half);
  return {b * d + (cross >> 32) + (middle >> 32), (middle << 32) | (low & half)};
#endif
}

inline DoubleWord multiply_signed(std::uint64_t lhs, std::uint64_t rhs) {
  constexpr std::uint64_t sign = 1ULL << 63;
#if defined(__SIZEOF_INT128__)
  const auto signed_bits = [](std::uint64_t bits) {
    return bits & sign ? -1 - std::int64_t(~bits) : std::int64_t(bits);
  };
  const auto product = static_cast<__int128_t>(signed_bits(lhs)) * signed_bits(rhs);
  const auto bits = static_cast<__uint128_t>(product);
  return {std::uint64_t(bits >> 64), std::uint64_t(bits)};
#else
  auto product = multiply(lhs, rhs);
  // Correct the unsigned high word for each negative input.
  if (lhs & sign) product.high -= rhs;
  if (rhs & sign) product.high -= lhs;
  return product;
#endif
}

struct Division {
  std::uint64_t quotient, remainder;
};

// Requires a nonzero divisor and dividend.high < divisor, which guarantees
// that a one-word quotient fits. Both words are host-independent.
inline Division divide(DoubleWord dividend, std::uint64_t divisor) {
  if (!dividend.high) return {dividend.low / divisor, dividend.low % divisor};
  Division result{0, dividend.high};
  for (unsigned bit = 64; bit != 0; --bit) {
    const bool carry = (result.remainder >> 63) != 0;
    result.remainder = (result.remainder << 1) | ((dividend.low >> (bit - 1)) & 1);
    const bool subtract = carry || result.remainder >= divisor;
    if (subtract) result.remainder -= divisor;
    result.quotient = (result.quotient << 1) | std::uint64_t(subtract);
  }
  return result;
}

} // namespace cosmic_pc::wide_integer
