#include "../sparse_ram.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__linux__)
#include <sys/resource.h>
#endif

namespace {
using cosmic_pc::SparseRam;
constexpr std::uint64_t kLogicalBytes = std::uint64_t{2} << 30;
constexpr std::size_t kPage = SparseRam::kPageSize;
constexpr std::size_t kWorkingPages = 2048; // 8 MiB spread across 2 GiB.
using Clock = std::chrono::steady_clock;

std::uint64_t rss_bytes() {
#if defined(__linux__)
    std::ifstream status("/proc/self/status");
    std::string line;
    while (std::getline(status, line)) {
        if (line.compare(0, 6, "VmRSS:") == 0) {
            std::istringstream values(line.substr(6));
            std::uint64_t kib = 0;
            if (values >> kib) return kib * 1024;
        }
    }
#endif
    throw std::runtime_error("Actual RSS is unavailable; benchmark requires Linux /proc/self/status.");
}

std::uint64_t peak_rss_bytes(std::uint64_t observed_rss) {
#if defined(__linux__)
    rusage usage{};
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        std::uint64_t peak = std::max(observed_rss,
                                     static_cast<std::uint64_t>(usage.ru_maxrss) * 1024);
        std::ifstream status("/proc/self/status");
        std::string line;
        while (std::getline(status, line)) {
            if (line.compare(0, 6, "VmHWM:") == 0) {
                std::istringstream values(line.substr(6));
                std::uint64_t kib = 0;
                if (values >> kib) peak = std::max(peak, kib * 1024);
                break;
            }
        }
        // Kernel RSS counters can be sampled/accounted differently. Include
        // actual observed RSS in the peak instead of reporting peak < current.
        return peak;
    }
#endif
    throw std::runtime_error("Actual peak RSS is unavailable.");
}

std::uint64_t offset_for(std::size_t index) {
    // Odd stride: all selected physical pages are distinct modulo 2^19.
    return ((std::uint64_t(index) * 257 + 17) % (kLogicalBytes / kPage)) * kPage;
}

void pattern(std::array<std::uint8_t, kPage>& bytes, std::size_t index) {
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        bytes[i] = static_cast<std::uint8_t>((std::uint64_t(index) * 131 + i * 17 + 0x5b) & 255);
    }
}

std::uint64_t elapsed_us(Clock::time_point from, Clock::time_point to) {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(to - from).count());
}
} // namespace

// Run each mode as a separate process for an independent peak-RSS observation:
//   memory_benchmark dense
//   memory_benchmark sparse
// Dense means an eagerly touched zero-initialized byte vector. Both modes have
// exactly the same logical size, guest writes, reads and hash workload. This
// is a synthetic memory measurement, not an application or game benchmark.
int main(int argc, char** argv) {
    if (argc != 2 || (std::string(argv[1]) != "dense" && std::string(argv[1]) != "sparse")) {
        std::cerr << "Usage: memory_benchmark dense|sparse\n";
        return 2;
    }
    try {
        static_assert(sizeof(std::size_t) >= 8, "The 2 GiB benchmark needs a 64-bit host.");
        const bool sparse = std::string(argv[1]) == "sparse";
        const auto rss_before = rss_bytes();
        const auto start = Clock::now();
        std::unique_ptr<SparseRam> paged;
        std::vector<std::uint8_t> dense;
        if (sparse) {
            paged = std::make_unique<SparseRam>(kLogicalBytes);
        } else {
            dense.resize(static_cast<std::size_t>(kLogicalBytes), 0);
            // Explicit stores ensure the eager baseline really touches every
            // page even if the allocator uses demand-zero virtual mappings.
            volatile std::uint8_t* physical = dense.data();
            for (std::uint64_t offset = 0; offset < kLogicalBytes; offset += kPage) physical[offset] = 0;
        }
        const auto initialized = Clock::now();
        const auto rss_initialized = rss_bytes();
        std::array<std::uint8_t, kPage> block{};
        for (std::size_t i = 0; i < kWorkingPages; ++i) {
            pattern(block, i);
            const auto offset = offset_for(i);
            if (sparse) paged->write_block(offset, block.data(), block.size());
            else std::memcpy(dense.data() + static_cast<std::size_t>(offset), block.data(), block.size());
        }
        const auto written = Clock::now();
        std::uint64_t hash = 14695981039346656037ULL;
        for (std::size_t i = 0; i < kWorkingPages; ++i) {
            const auto offset = offset_for(i);
            if (sparse) paged->read_block(offset, block.data(), block.size());
            else std::memcpy(block.data(), dense.data() + static_cast<std::size_t>(offset), block.size());
            std::array<std::uint8_t, kPage> expected{};
            pattern(expected, i);
            if (block != expected) throw std::runtime_error("Working-set byte verification failed.");
            for (const auto byte : block) hash = (hash ^ byte) * 1099511628211ULL;
        }
        const auto zero = sparse ? paged->read_byte(0) : dense[0];
        if (zero != 0) throw std::runtime_error("Untouched logical memory was not zero.");
        hash = (hash ^ zero) * 1099511628211ULL;
        const auto completed = Clock::now();
        const auto rss_after = rss_bytes();
        const auto peak = peak_rss_bytes(std::max({rss_before, rss_initialized, rss_after}));
        const auto payload = sparse ? paged->allocated_bytes() : kLogicalBytes;
        const auto metadata = sparse ? paged->metadata_bytes() : sizeof(dense);
        const auto pages = sparse ? paged->allocated_pages() : kLogicalBytes / kPage;
        std::ostringstream hash_text;
        hash_text << std::hex << std::setfill('0') << std::setw(16) << hash;
        std::cout << "{\"mode\":\"" << argv[1] << "\",\"logical_bytes\":" << kLogicalBytes
                  << ",\"working_set_bytes\":" << kWorkingPages * kPage
                  << ",\"page_bytes\":" << kPage << ",\"allocated_pages\":" << pages
                  << ",\"payload_bytes\":" << payload << ",\"metadata_bytes\":" << metadata
                  << ",\"rss_before_bytes\":" << rss_before
                  << ",\"rss_initialized_bytes\":" << rss_initialized
                  << ",\"rss_after_bytes\":" << rss_after << ",\"peak_rss_bytes\":" << peak
                  << ",\"initialize_us\":" << elapsed_us(start, initialized)
                  << ",\"write_us\":" << elapsed_us(initialized, written)
                  << ",\"verify_us\":" << elapsed_us(written, completed)
                  << ",\"hash_fnv1a64\":\"" << hash_text.str() << "\"}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Benchmark failed: " << error.what() << '\n';
        return 1;
    }
}
