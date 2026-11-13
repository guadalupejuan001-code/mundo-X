#pragma once

#include "x86_wide_integer.hpp"

#include <array>
#include <cstdint>
#include <utility>

namespace cosmic_pc::x87_numeric {

using Value = std::array<std::uint8_t, 10>;

enum Exception : std::uint16_t {
  Invalid = 1, Denormal = 2, DivideByZero = 4, Overflow = 8, Underflow = 16, Precision = 32
};

struct Result {
  Value value{};
  std::uint16_t exceptions = 0;
  bool rounded_up = false;
};

struct Remainder {
  Result number;
  std::uint16_t condition = 0, condition_mask = 0;
};

namespace detail {

constexpr int kBias = 16383, kMinExponent = -16382, kMaxExponent = 16383;
constexpr int kExceptionBias = 24576;
constexpr std::uint64_t kIntegerBit = std::uint64_t{1} << 63;
constexpr std::uint64_t kQuietBit = std::uint64_t{1} << 62;

// Internal significands have their integral bit at position 126. Bit 127
// accommodates a carry. The low word retains guard, round and sticky bits.
struct Wide {
  std::uint64_t high = 0, low = 0;

  bool zero() const { return !(high | low); }
  bool operator<(Wide rhs) const { return high < rhs.high || (high == rhs.high && low < rhs.low); }
  Wide operator+(Wide rhs) const {
    const auto tail = low + rhs.low;
    return {high + rhs.high + (tail < low), tail};
  }
  Wide operator-(Wide rhs) const { return {high - rhs.high - (low < rhs.low), low - rhs.low}; }
  Wide left_one() const { return {(high << 1) | (low >> 63), low << 1}; }
  Wide right(unsigned count) const {
    if (!count) return *this;
    if (count < 64) return {high >> count, (high << (64 - count)) | (low >> count)};
    if (count < 128) return {0, high >> (count - 64)};
    return {};
  }
  Wide right_sticky(unsigned count) const {
    auto value = right(count);
    if (discarded(count)) value.low |= 1;
    return value;
  }
  bool discarded(unsigned count) const {
    if (!count) return false;
    if (count < 64) return (low << (64 - count)) != 0;
    if (count == 64) return low != 0;
    return low != 0 || (count >= 128 ? high != 0 : (high << (128 - count)) != 0);
  }
};

inline Wide significand(std::uint64_t bits) { return {bits >> 1, bits << 63}; }

inline Wide multiply(std::uint64_t lhs, std::uint64_t rhs) {
  const auto product = wide_integer::multiply(lhs, rhs);
  return {product.high, product.low};
}

enum class Kind { Zero, Finite, Infinity, QuietNaN, SignalingNaN, Unsupported };

struct Operand {
  std::uint64_t significand;
  int exponent;
  bool negative, denormal;
  Kind kind;
};

inline Operand unpack(const Value& bytes) {
  std::uint64_t sig = 0;
  for (unsigned i = 0; i < 8; ++i) sig |= std::uint64_t(bytes[i]) << (i * 8);
  const unsigned exp = unsigned(bytes[8]) | ((unsigned(bytes[9]) & 0x7fU) << 8);
  const bool negative = (bytes[9] & 0x80U) != 0;
  if (!exp && !sig) return {0, 0, negative, false, Kind::Zero};
  if (exp && !(sig & kIntegerBit)) return {sig, 0, negative, false, Kind::Unsupported};
  if (exp == 0x7fffU) {
    const auto kind = sig == kIntegerBit ? Kind::Infinity :
                      (sig & kQuietBit) ? Kind::QuietNaN : Kind::SignalingNaN;
    return {sig, 0, negative, false, kind};
  }
  int exponent = exp ? int(exp) - kBias : kMinExponent;
  while (!(sig & kIntegerBit)) {
    sig <<= 1;
    --exponent;
  }
  return {sig, exponent, negative, !exp, Kind::Finite};
}

inline Value pack(std::uint64_t sig, unsigned exponent, bool negative) {
  Value result{};
  for (unsigned i = 0; i < 8; ++i) result[i] = std::uint8_t(sig >> (8 * i));
  result[8] = std::uint8_t(exponent);
  result[9] = std::uint8_t((exponent >> 8) | (negative ? 0x80U : 0));
  return result;
}

inline Result invalid() { return {pack(kIntegerBit | kQuietBit, 0x7fff, true), Invalid, false}; }
inline bool is_nan(Kind kind) { return kind == Kind::QuietNaN || kind == Kind::SignalingNaN; }

inline Result nan_result(Operand lhs, Operand rhs) {
  const bool signaling = lhs.kind == Kind::SignalingNaN || rhs.kind == Kind::SignalingNaN;
  // x87 prefers a quiet NaN to a signaling NaN, then the larger significand.
  if (!is_nan(lhs.kind) || (is_nan(rhs.kind) &&
      (lhs.significand < rhs.significand ||
       (lhs.significand == rhs.significand && lhs.negative)))) lhs = rhs;
  return {pack(lhs.significand | kQuietBit, 0x7fff, lhs.negative),
          signaling ? std::uint16_t(Invalid) : std::uint16_t(0), false};
}

struct Rounded {
  std::uint64_t significand;
  int exponent;
  bool inexact, up;
};

inline Rounded rounded(Wide value, int exponent, unsigned precision, unsigned rc, bool negative) {
  const unsigned drop = 64 - precision;
  const auto kept = value.right(63 + drop).low;
  const auto guard = value.right(62 + drop).low & 1U;
  const bool sticky = value.discarded(62 + drop);
  const bool inexact = guard || sticky;
  const bool up = inexact && (rc == 0 ? guard && (sticky || (kept & 1U)) :
                             rc == 1 ? negative : rc == 2 && !negative);
  const auto sig = (kept + unsigned(up)) << drop;
  if (up && !sig) return {kIntegerBit, exponent + 1, true, true};
  return {sig, exponent, inexact, up};
}

inline Result finish(Wide value, int exponent, bool negative, std::uint16_t control) {
  if (value.zero()) return {pack(0, 0, negative), 0, false};
  if (value.high & kIntegerBit) {
    value = value.right_sticky(1);
    ++exponent;
  } else {
    while (!(value.high & (kIntegerBit >> 1))) {
      value = value.left_one();
      --exponent;
    }
  }

  const auto pc = (control >> 8) & 3U;
  const auto precision = pc == 0 ? 24U : pc == 2 ? 53U : 64U;
  const auto rc = (control >> 10) & 3U;
  auto answer = rounded(value, exponent, precision, rc, negative);
  std::uint16_t exceptions = 0;
  if (answer.exponent > kMaxExponent) {
    if (control & Overflow) {
      const bool infinity = rc == 0 || (rc == 1 && negative) || (rc == 2 && !negative);
      const auto max = ~std::uint64_t{0} << (64 - precision);
      return {pack(infinity ? kIntegerBit : max, infinity ? 0x7fff : 0x7ffe, negative),
              Overflow | Precision, infinity};
    }
    answer.exponent -= kExceptionBias;
    exceptions |= Overflow;
  } else if (exponent < kMinExponent) {
    const bool tiny = answer.exponent < kMinExponent;
    if (tiny && !(control & Underflow)) {
      answer.exponent += kExceptionBias;
      exceptions |= Underflow;
    } else {
      answer = rounded(value.right_sticky(unsigned(kMinExponent - exponent)), kMinExponent,
                       precision, rc, negative);
      if (tiny && answer.inexact) exceptions |= Underflow;
    }
  }
  if (answer.inexact) exceptions |= Precision;
  const auto biased = (answer.significand & kIntegerBit) ? unsigned(answer.exponent + kBias) : 0U;
  return {pack(answer.significand, biased, negative), exceptions, answer.up};
}

inline Result add(Operand lhs, Operand rhs, std::uint16_t control) {
  const auto rc = (control >> 10) & 3U;
  const bool same_sign = lhs.negative == rhs.negative;
  if (lhs.kind == Kind::Infinity || rhs.kind == Kind::Infinity) {
    if (lhs.kind == rhs.kind && !same_sign) return invalid();
    const auto negative = lhs.kind == Kind::Infinity ? lhs.negative : rhs.negative;
    return {pack(kIntegerBit, 0x7fff, negative), 0, false};
  }
  if (lhs.kind == Kind::Zero && rhs.kind == Kind::Zero)
    return {pack(0, 0, same_sign ? lhs.negative : rc == 1), 0, false};
  if (lhs.kind == Kind::Zero) {
    lhs = rhs;
    rhs.significand = 0;
  }
  if (rhs.significand && (lhs.exponent < rhs.exponent ||
      (lhs.exponent == rhs.exponent && lhs.significand < rhs.significand))) std::swap(lhs, rhs);
  const auto a = significand(lhs.significand);
  const auto b = rhs.significand ?
      significand(rhs.significand).right_sticky(unsigned(lhs.exponent - rhs.exponent)) : Wide{};
  const auto magnitude = same_sign ? a + b : a - b;
  return finish(magnitude, lhs.exponent, magnitude.zero() ? rc == 1 : lhs.negative, control);
}

inline Wide quotient(std::uint64_t lhs, std::uint64_t rhs) {
  std::uint64_t remainder = lhs < rhs ? (lhs << 1) - rhs : lhs - rhs;
  std::uint64_t result = 1;
  const auto digit = [&] {
    const bool carry = remainder >> 63;
    remainder <<= 1;
    const bool bit = carry || remainder >= rhs;
    if (bit) remainder -= rhs;
    return bit;
  };
  for (unsigned i = 0; i < 63; ++i) result = (result << 1) | digit();
  const bool guard = digit();
  auto value = significand(result);
  value.low |= (std::uint64_t(guard) << 62) | (remainder != 0);
  return value;
}

inline Wide square_root(Wide radicand) {
  Wide root{};
  for (Wide bit{kIntegerBit >> 1, 0}; !bit.zero(); bit = bit.right(2)) {
    const auto trial = root + bit;
    root = root.right(1);
    if (!(radicand < trial)) {
      radicand = radicand - trial;
      root = root + bit;
    }
  }
  // The integer remainder is r^2 - floor(r)^2. An exact half-way root
  // is impossible with an integer radicand, so compare with the root.
  auto result = significand(root.low);
  result.low |= (std::uint64_t(root < radicand) << 62) | !radicand.zero();
  return result;
}

} // namespace detail

// ModR/M groups used by the x87 arithmetic opcodes: 0 add, 1 multiply,
// 4/5 subtract, 6/7 divide. The caller handles compare and stack faults.
inline Result arithmetic(unsigned group, const Value& left, const Value& right,
                         std::uint16_t control, bool right_denormal = false) {
  using namespace detail;
  auto lhs = unpack(left), rhs = unpack(right);
  // IEEE subnormals widen to normal extended80 values, but the original
  // IEEE memory operand keeps its denormal exception classification.
  rhs.denormal |= right_denormal;
  if (lhs.kind == Kind::Unsupported || rhs.kind == Kind::Unsupported) return invalid();
  if (is_nan(lhs.kind) || is_nan(rhs.kind)) return nan_result(lhs, rhs);
  if (group == 5 || group == 7) std::swap(lhs, rhs);
  if (group == 4 || group == 5) rhs.negative = !rhs.negative;
  const bool negative = lhs.negative != rhs.negative;
  const bool lhs_zero = lhs.kind == Kind::Zero, rhs_zero = rhs.kind == Kind::Zero;
  const bool lhs_inf = lhs.kind == Kind::Infinity, rhs_inf = rhs.kind == Kind::Infinity;
  const bool dividing = group >= 6;
  if ((dividing && ((lhs_zero && rhs_zero) || (lhs_inf && rhs_inf))) ||
      (group == 1 && ((lhs_inf && rhs_zero) || (lhs_zero && rhs_inf))) ||
      ((group == 0 || group == 4 || group == 5) && lhs_inf && rhs_inf && negative))
    return invalid();
  if (dividing && rhs_zero)
    return {pack(kIntegerBit, 0x7fff, negative), lhs_inf ? std::uint16_t(0) :
            std::uint16_t(DivideByZero), false};

  const auto denormal = lhs.denormal || rhs.denormal ? std::uint16_t(Denormal) : std::uint16_t(0);
  if (denormal && !(control & Denormal)) return {{}, denormal, false};
  Result result;
  if (dividing) {
    if (lhs_inf) result.value = pack(kIntegerBit, 0x7fff, negative);
    else if (rhs_inf || lhs_zero) result.value = pack(0, 0, negative);
    else result = finish(quotient(lhs.significand, rhs.significand),
                         lhs.exponent - rhs.exponent - (lhs.significand < rhs.significand), negative, control);
  } else if (group == 1) {
    if (lhs_inf || rhs_inf) result.value = pack(kIntegerBit, 0x7fff, negative);
    else if (lhs_zero || rhs_zero) result.value = pack(0, 0, negative);
    else result = finish(multiply(lhs.significand, rhs.significand), lhs.exponent + rhs.exponent,
                         negative, control);
  } else {
    result = add(lhs, rhs, control);
  }
  result.exceptions |= denormal;
  return result;
}

inline Result square_root(const Value& bits, std::uint16_t control) {
  using namespace detail;
  const auto value = unpack(bits);
  if (value.kind == Kind::Unsupported) return invalid();
  if (is_nan(value.kind)) return nan_result(value, value);
  if (value.kind == Kind::Zero) return {bits, 0, false};
  if (value.negative) return invalid();
  if (value.kind == Kind::Infinity) return {bits, 0, false};
  if (value.denormal && !(control & Denormal)) return {{}, Denormal, false};
  const bool odd = value.exponent % 2 != 0;
  const auto radicand = odd ? Wide{value.significand, 0} : significand(value.significand);
  auto result = finish(detail::square_root(radicand), (value.exponent - int(odd)) / 2, false, control);
  if (value.denormal) result.exceptions |= Denormal;
  return result;
}

inline Result scale(const Value& input, const Value& power, std::uint16_t control) {
  using namespace detail;
  const auto value = unpack(input), exponent = unpack(power);
  if (value.kind == Kind::Unsupported || exponent.kind == Kind::Unsupported) return invalid();
  if (is_nan(value.kind) || is_nan(exponent.kind)) return nan_result(value, exponent);
  const bool zero = value.kind == Kind::Zero, infinite = value.kind == Kind::Infinity;
  if (exponent.kind == Kind::Infinity && ((!exponent.negative && zero) || (exponent.negative && infinite)))
    return invalid();
  const auto denormal = value.denormal || exponent.denormal ? std::uint16_t(Denormal) : std::uint16_t(0);
  if (denormal && !(control & Denormal)) return {{}, denormal, false};
  if (exponent.kind == Kind::Infinity)
    return {pack(exponent.negative ? 0 : kIntegerBit, exponent.negative ? 0 : 0x7fff, value.negative),
            denormal, false};
  if (zero || infinite) return {input, denormal, false};
  // Beyond 2^17 the exact shift cannot affect any finite extended result
  // or fit its exponent in the biased response to an unmasked exception.
  const auto shift = exponent.kind == Kind::Zero || exponent.exponent < 0 ? 0 :
      exponent.exponent >= 17 ? 131072 : int(exponent.significand >> (63 - exponent.exponent));
  const auto ex = value.exponent + (exponent.negative ? -shift : shift);
  if (ex > kMaxExponent + kExceptionBias && !(control & Overflow))
    return {pack(kIntegerBit, 0x7fff, value.negative),
            std::uint16_t(denormal | Overflow | Precision), true};
  if (ex < kMinExponent - kExceptionBias && !(control & Underflow))
    return {pack(0, 0, value.negative), std::uint16_t(denormal | Underflow | Precision), false};
  auto result = finish(significand(value.significand), ex, value.negative, control | 0x0300U);
  result.exceptions |= denormal;
  return result;
}

inline Remainder remainder(const Value& value, const Value& modulus, bool nearest,
                           std::uint16_t control) {
  using namespace detail;
  const auto lhs = unpack(value), rhs = unpack(modulus);
  if (lhs.kind == Kind::Unsupported || rhs.kind == Kind::Unsupported) return {invalid()};
  if (is_nan(lhs.kind) || is_nan(rhs.kind)) return {nan_result(lhs, rhs)};
  if (lhs.kind == Kind::Infinity || rhs.kind == Kind::Zero) return {invalid()};
  const auto denormal = lhs.denormal || rhs.denormal ? std::uint16_t(Denormal) : std::uint16_t(0);
  if (denormal && !(control & Denormal)) return {{{}, denormal, false}};

  const int difference = lhs.exponent - rhs.exponent;
  const bool partial = difference >= 64 && lhs.kind != Kind::Zero && rhs.kind != Kind::Infinity;
  bool negative = lhs.negative;
  unsigned quotient_low = 0;
  auto magnitude = significand(lhs.significand);
  auto exponent = lhs.exponent;
  if (lhs.kind != Kind::Zero && rhs.kind != Kind::Infinity) {
    if (difference >= 0) {
      // A 32–63-bit reduction leaves a multiple of 32 quotient bits for
      // subsequent partial reductions, and matches the native x86 profile.
      const auto digits = partial ? 32U + unsigned(difference & 31) : unsigned(difference);
      auto residue = lhs.significand;
      if (residue >= rhs.significand) {
        residue -= rhs.significand;
        quotient_low = 1;
      }
      for (unsigned i = 0; i < digits; ++i) {
        const auto previous = residue;
        residue <<= 1;
        const bool bit = (previous & kIntegerBit) || residue >= rhs.significand;
        if (bit) residue -= rhs.significand;
        quotient_low = ((quotient_low << 1) | unsigned(bit)) & 7U;
      }
      if (!partial && nearest) {
        const auto complement = rhs.significand - residue;
        if (residue > complement || (residue == complement && (quotient_low & 1U))) {
          residue = complement;
          quotient_low = (quotient_low + 1) & 7U;
          negative = !negative;
        }
      }
      magnitude = significand(residue);
      exponent = lhs.exponent - int(digits);
    } else if (difference == -1 && nearest && lhs.significand > rhs.significand) {
      magnitude = significand(rhs.significand).left_one() - magnitude;
      quotient_low = 1;
      negative = !negative;
    }
  }
  auto result = finish(magnitude, exponent, negative, (control & ~0x0f00U) | 0x0300U);
  result.exceptions |= denormal;
  if (partial) return {result, 0x0400U, 0x0400U};
  const auto condition = std::uint16_t(((quotient_low & 1U) << 9) |
                                      ((quotient_low & 2U) << 13) |
                                      ((quotient_low & 4U) << 6));
  return {result, condition, 0x4700U};
}

} // namespace cosmic_pc::x87_numeric
