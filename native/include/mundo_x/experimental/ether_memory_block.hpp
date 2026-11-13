#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace mundo_x::ether_next {

// Materialize one bounded block of classical guest bytes. The CPU validates
// every address in the span and supplies the exact number of safe elements.
template <class Memory>
void copy_ram_block(Memory& memory, std::uint64_t from, std::uint64_t to,
                    std::size_t bytes) {
  if (bytes == Memory::kPageSize && memory.try_copy_repeating_page(from, to)) return;
  if (memory.try_copy_resident_block(from, to, bytes)) return;
  std::array<std::uint8_t, Memory::kPageSize> block;
  memory.read_block(from, block.data(), bytes);
  memory.write_block(to, block.data(), bytes);
}

template <class Memory>
void repeat_ram_value(Memory& memory, std::uint64_t to, std::uint64_t value,
                      unsigned width, std::size_t bytes) {
  std::array<std::uint8_t, Memory::kPageSize> block;
  for (unsigned i = 0; i < width; ++i)
    block[i] = std::uint8_t(value >> (8 * i));
  // Reuse each materialized prefix to fill the next span of the same pattern.
  for (std::size_t ready = width; ready < bytes;) {
    const auto next = std::min(ready, bytes - ready);
    std::copy_n(block.data(), next, block.data() + ready);
    ready += next;
  }
  memory.write_block(to, block.data(), bytes);
}

}  // namespace mundo_x::ether_next
