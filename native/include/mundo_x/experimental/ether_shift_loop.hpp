#pragma once

#include <cstdint>

namespace mundo_x::ether_next {

struct ShiftPair16 {
  std::uint16_t shifted;
  std::uint16_t rotated;
  bool carry;
  bool overflow;
};

// A single-bit shift followed by a same-direction rotate through carry
// is a shift of the combined word. Recover the final carry from the word
// before the last iteration and overflow from the final rotate.
inline ShiftPair16 shift_pair_16(std::uint16_t shifted, std::uint16_t rotated,
                                bool left, unsigned iterations) noexcept {
  const auto high = left ? rotated : shifted;
  const auto low = left ? shifted : rotated;
  const auto pair = (std::uint32_t(high) << 16) | low;
  const auto preceding_shifts = iterations - 1;
  const auto before_last = preceding_shifts >= 32 ? 0U :
      left ? pair << preceding_shifts : pair >> preceding_shifts;
  const auto result = left ? before_last << 1 : before_last >> 1;
  const auto high_result = std::uint16_t(result >> 16);
  const auto low_result = std::uint16_t(result);
  if (left) {
    const bool carry = (before_last >> 31) != 0;
    return {low_result, high_result, carry, ((high_result >> 15) != 0) != carry};
  }
  return {high_result, low_result, (before_last & 1U) != 0,
          ((low_result >> 15) & 1U) != ((low_result >> 14) & 1U)};
}

}  // namespace mundo_x::ether_next
