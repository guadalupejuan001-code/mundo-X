// Bounded, reproducible memory-copy workload. The reported elapsed times are
// local-host measurements; they are not Android RAM or quantum speedup claims.
#include "mundo_x/inverse_memory.hpp"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mundo_x::InfiniteCore;
using mundo_x::InverseMemory;

int main() {
  using Clock = std::chrono::steady_clock;
  try {
    InfiniteCore quantum;
    InverseMemory memory;
    constexpr unsigned overrides = 8192;
    constexpr unsigned bindings = 512;
    constexpr unsigned copies = 256;
    constexpr unsigned writers = 16;

    for (unsigned i = 0; i < overrides; ++i)
      memory.store(std::to_string(i), static_cast<std::uint8_t>(1 + i % 254), quantum);
    for (unsigned i = 0; i < bindings; ++i)
      memory.bind_quantum_byte(std::to_string(40000 + i),
                               std::to_string(10000 + i * 8), quantum);
    const auto reference = memory.snapshot();
    std::uint64_t snapshot_hash = UINT64_C(14695981039346656037);
    for (const unsigned char byte : reference) {
      snapshot_hash ^= byte;
      snapshot_hash *= UINT64_C(1099511628211);
    }
    const auto start = Clock::now();

    std::vector<InverseMemory> readers;
    readers.reserve(copies);
    std::size_t checksum = 0;
    for (unsigned i = 0; i < copies; ++i) {
      readers.push_back(memory);
      checksum += readers.back().read("42", quantum).value;
    }
    const auto copied = Clock::now();

    for (unsigned i = 0; i < writers; ++i)
      readers[i].store(std::to_string(i), 255, quantum);
    const auto written = Clock::now();

    if (memory.snapshot() != reference || readers.back().snapshot() != reference)
      throw std::runtime_error("copy-on-write reader was modified");
    if (readers[0].read("0", quantum).value != 255 ||
        memory.read("0", quantum).value != 1)
      throw std::runtime_error("copy-on-write writer did not detach");
    if (checksum != copies * 43U)
      throw std::runtime_error("copy-on-write read changed values");

    const auto read_copy_ms =
        std::chrono::duration<double, std::milli>(copied - start).count();
    const auto write_detach_ms =
        std::chrono::duration<double, std::milli>(written - copied).count();
    std::cout << "{\"suite\":\"ether_memory_copy_benchmark\","
              << "\"overrides\":" << overrides << ","
              << "\"quantum_bindings\":" << bindings << ","
              << "\"read_only_transaction_copies\":" << copies << ","
              << "\"write_detaches\":" << writers << ","
              << "\"copy_and_read_ms\":" << read_copy_ms << ","
              << "\"first_write_detach_ms\":" << write_detach_ms << ","
              << "\"checksum\":" << checksum << ","
              << "\"snapshot_fnv64\":" << snapshot_hash << ","
              << "\"failures\":0}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL Ether Memory COW benchmark: " << error.what() << '\n';
    return 1;
  }
}
