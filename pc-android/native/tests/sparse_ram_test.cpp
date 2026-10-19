#include "../sparse_ram.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using cosmic_pc::SparseRam;
constexpr std::uint64_t kTwoGiB = std::uint64_t{2} << 30;
constexpr std::size_t kPage = SparseRam::kPageSize;
unsigned checks = 0;

void need(bool condition, const std::string& reason) {
    ++checks;
    if (!condition) throw std::runtime_error(reason);
}

template <class Error, class Operation>
void expect(Operation operation, const std::string& text) {
    ++checks;
    try {
        operation();
    } catch (const Error& error) {
        if (std::string(error.what()).find(text) == std::string::npos) {
            throw std::runtime_error("Wrong exception message: " + std::string(error.what()));
        }
        return;
    }
    throw std::runtime_error("Expected exception containing " + text);
}

std::vector<std::uint8_t> read(const SparseRam& ram, std::uint64_t offset, std::size_t count) {
    std::vector<std::uint8_t> result(count);
    ram.read_block(offset, result.data(), result.size());
    return result;
}
} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"2 GiB logical zero reads allocate no pages", [] {
             SparseRam ram(kTwoGiB);
             const auto metadata = ram.metadata_bytes();
             const SparseRam& constant = ram;
             for (const std::uint64_t at : {std::uint64_t{0}, std::uint64_t{kPage - 1},
                                           std::uint64_t{kPage}, kTwoGiB / 2, kTwoGiB - 1}) {
                 need(ram[at] == 0 && constant.at(at) == 0, "Absent RAM must read zero");
             }
             const auto tail = read(ram, kTwoGiB - 8192, 8192);
             need(std::all_of(tail.begin(), tail.end(), [](std::uint8_t v) { return v == 0; }),
                  "Zero block read differs from zero byte reads");
             need(ram.size() == kTwoGiB && ram.allocated_pages() == 0 && ram.allocated_bytes() == 0,
                  "Zero read allocated payload");
             need(ram.metadata_bytes() == metadata && metadata < 16384,
                  "Logical 2 GiB should need only a small page-table root");
             need(ram.budget_bytes() == (std::uint64_t{1} << 30), "Wrong default payload cap");
         }},
        {"high physical addresses remain distinct", [] {
             SparseRam ram(kTwoGiB, 2 * kPage);
             ram[0] = 0x31;
             ram[kTwoGiB - 1] = 0xd4;
             need(ram[0] == 0x31 && ram[kTwoGiB - 1] == 0xd4 && ram[0x7ffff000] == 0,
                  "High address truncated or aliased");
             need(ram.allocated_pages() == 2 && ram.allocated_bytes() == 2 * kPage,
                  "Byte writes must allocate exactly their pages");
         }},
        {"proxy assignment copies values", [] {
             SparseRam ram(3 * kPage);
             ram[0] = 0x35;
             ram[kPage] = ram[0];
             auto target = ram[2 * kPage];
             target = ram[kPage];
             ram[0] = 0xab;
             need(ram[kPage] == 0x35 && target == 0x35, "Proxy assignment copied its address");
             target = target;
             target ^= 0xff;
             need(ram[2 * kPage] == 0xca, "Proxy self-assignment or compound assignment failed");
             need(ram[0] == 0xab && ram.allocated_pages() == 3, "Proxy wrote the wrong owner slot");
         }},
        {"strict bounds include UINT64_MAX and empty RAM", [] {
             SparseRam ram(kTwoGiB);
             expect<std::out_of_range>([&] { (void)ram.at(kTwoGiB); }, "outside logical RAM");
             expect<std::out_of_range>([&] { ram[std::numeric_limits<std::uint64_t>::max()] = 1; },
                                       "outside logical RAM");
             expect<std::out_of_range>([&] { (void)static_cast<const SparseRam&>(ram)[kTwoGiB]; },
                                       "outside logical RAM");
             SparseRam empty(0);
             need(empty.empty() && empty.budget_bytes() == 0, "Empty RAM metadata incorrect");
             expect<std::out_of_range>([&] { (void)empty[0]; }, "outside logical RAM");
             empty.read_block(0, nullptr, 0);
             empty.write_block(0, nullptr, 0);
             need(empty.allocated_pages() == 0, "Empty operation allocated a page");
         }},
        {"cross-page blocks preserve exact bytes", [] {
             SparseRam ram(4 * kPage + 13);
             std::vector<std::uint8_t> payload(kPage + 31);
             for (std::size_t i = 0; i < payload.size(); ++i) {
                 payload[i] = static_cast<std::uint8_t>(i * 37 + 9);
             }
             ram.write_block(kPage - 7, payload.data(), payload.size());
             need(read(ram, kPage - 7, payload.size()) == payload, "Cross-page block was corrupted");
             need(ram[kPage - 8] == 0 && ram[2 * kPage + 24] == 0,
                  "Block write changed neighboring bytes");
             need(ram.allocated_pages() == 3, "Cross-page write backed an incorrect page count");
         }},
        {"payload cap rejects new backing explicitly", [] {
             SparseRam ram(kTwoGiB, kPage);
             ram[0] = 0x61;
             const auto metadata = ram.metadata_bytes();
             expect<SparseRam::BudgetExceeded>([&] { ram[kTwoGiB - 1] = 0x62; }, "budget exceeded");
             need(ram[0] == 0x61 && ram[kTwoGiB - 1] == 0 && ram.allocated_pages() == 1,
                  "Failed capped write changed RAM");
             need(ram.metadata_bytes() == metadata, "Failed preflight allocated a page table");
             ram[kTwoGiB - 1] = 0;
             need(ram.allocated_pages() == 1, "Writing an absent zero byte consumed the budget");
         }},
        {"failed block write preserves bytes and allocation state", [] {
             SparseRam ram(4 * kPage, 2 * kPage);
             ram[0] = 0x5a;
             const auto before = read(ram, 0, 4 * kPage);
             const auto metadata = ram.metadata_bytes();
             std::vector<std::uint8_t> payload(3 * kPage, 0x9b);
             expect<SparseRam::BudgetExceeded>([&] {
                 ram.write_block(0, payload.data(), payload.size());
             }, "budget exceeded");
             need(read(ram, 0, 4 * kPage) == before, "Capped block write committed partial bytes");
             need(ram.allocated_pages() == 1 && ram.metadata_bytes() == metadata,
                  "Capped block preflight partially committed backing");
         }},
        {"invalid block ranges preserve both source and destination state", [] {
             SparseRam ram(kPage);
             std::array<std::uint8_t, 16> bytes{};
             bytes.fill(0x77);
             expect<std::out_of_range>([&] { ram.read_block(kPage - 8, bytes.data(), bytes.size()); },
                                       "outside logical RAM");
             need(std::all_of(bytes.begin(), bytes.end(), [](std::uint8_t v) { return v == 0x77; }),
                  "Out-of-range read partially changed the caller buffer");
             expect<std::out_of_range>([&] { ram.write_block(kPage - 8, bytes.data(), bytes.size()); },
                                       "outside logical RAM");
             expect<std::out_of_range>([&] {
                 ram.prepare_write(std::numeric_limits<std::uint64_t>::max() - 3, 8);
             }, "outside logical RAM");
             expect<std::invalid_argument>([&] { ram.read_block(0, nullptr, 1); }, "null read");
             expect<std::invalid_argument>([&] { ram.write_block(0, nullptr, 1); }, "null write");
             need(ram.allocated_pages() == 0, "Invalid range or pointer allocated backing");
         }},
        {"prepare_write covers a page-table boundary atomically", [] {
             SparseRam ram(4 * 1024 * 1024, 2 * kPage);
             const std::uint64_t boundary = kPage * SparseRam::kPagesPerTable;
             ram.prepare_write(boundary - 1, 2);
             need(ram.allocated_pages() == 2, "Preflight failed across a table boundary");
             const auto metadata = ram.metadata_bytes();
             ram.prepare_write(boundary - 1, 2);
             need(ram.allocated_pages() == 2 && ram.metadata_bytes() == metadata,
                  "Repeated preflight changed backing");
             expect<SparseRam::BudgetExceeded>([&] { ram.prepare_write(0, 1); }, "budget exceeded");
             need(ram.allocated_pages() == 2 && ram.metadata_bytes() == metadata,
                  "Failed preflight partially installed a table");
         }},
        {"rollback needs no allocation after writing zeros", [] {
             SparseRam ram(3 * kPage, 2 * kPage);
             ram[0] = 0x42;
             const std::uint8_t old = ram[0];
             ram[0] = 0;
             need(ram.allocated_pages() == 1, "Writing zero released a transaction's backing");
             ram[kPage] = 0x99; // Consume all remaining budget before rollback.
             const auto metadata = ram.metadata_bytes();
             need(ram.restore_byte(0, old) && ram[0] == 0x42,
                  "Rollback could not restore a byte at a full budget");
             need(ram.restore_byte(2 * kPage, 0), "Absent zero rollback should be a no-op");
             need(!ram.restore_byte(2 * kPage, 1) && !ram.restore_byte(ram.size(), 0),
                  "Invalid rollback invariant was not reported");
             need(ram.allocated_pages() == 2 && ram.metadata_bytes() == metadata,
                  "Rollback allocated or released backing");
         }},
        {"reset and clear discard backing but preserve logical RAM", [] {
             SparseRam ram(kTwoGiB, 3 * kPage);
             const auto initial_metadata = ram.metadata_bytes();
             ram[1] = 0x17;
             ram[kTwoGiB - 1] = 0x29;
             ram.reset_zero();
             need(ram.size() == kTwoGiB && ram.budget_bytes() == 3 * kPage,
                  "Reset changed logical RAM or its cap");
             need(ram.allocated_pages() == 0 && ram.allocated_bytes() == 0 &&
                      ram.metadata_bytes() == initial_metadata,
                  "Reset retained payload or second-level metadata");
             need(ram[1] == 0 && ram[kTwoGiB - 1] == 0, "Reset did not restore implicit zeros");
             ram[kPage] = 0x37;
             ram.clear();
             need(ram.size() == kTwoGiB && ram[kPage] == 0 && ram.allocated_pages() == 0,
                  "clear() did not preserve logical zero RAM");
         }},
        {"explicit trim removes only entirely zero pages", [] {
             SparseRam ram(4 * 1024 * 1024);
             const auto initial_metadata = ram.metadata_bytes();
             ram[0] = 0x55;
             ram[3 * 1024 * 1024] = 0x66;
             ram[3 * 1024 * 1024] = 0;
             need(ram.allocated_pages() == 2, "Write automatically trimmed a page");
             need(ram.trim_zero_pages() == 1 && ram.allocated_pages() == 1 && ram[0] == 0x55,
                  "Explicit trim removed nonzero data or retained a zero page");
             ram[0] = 0;
             need(ram.trim_zero_pages() == 1 && ram.metadata_bytes() == initial_metadata,
                  "Trim did not free the empty second-level table");
         }},
        {"partial last page and budget granularity", [] {
             SparseRam ram(17);
             ram[16] = 0xa3;
             need(ram[16] == 0xa3 && ram.allocated_bytes() == kPage && ram.budget_bytes() == kPage,
                  "Default budget must accommodate one partial logical page");
             expect<std::out_of_range>([&] { ram[17] = 1; }, "outside logical RAM");
             SparseRam capped(17, kPage - 1);
             expect<SparseRam::BudgetExceeded>([&] { capped[0] = 1; }, "budget exceeded");
             need(capped.allocated_pages() == 0, "An allocation exceeded a sub-page explicit cap");
         }},
        {"zero fill stays sparse and nonzero fill preflights", [] {
             SparseRam ram(kTwoGiB, kPage);
             ram.fill(0, 0, static_cast<std::size_t>(ram.size()));
             need(ram.allocated_pages() == 0, "Zero fill materialized implicit pages");
             ram.fill(0, 0x3c, kPage);
             expect<SparseRam::BudgetExceeded>([&] { ram.fill(kPage - 1, 0x77, 2); }, "budget exceeded");
             need(ram[kPage - 1] == 0x3c && ram[kPage] == 0,
                  "Failed nonzero fill changed its resident prefix");
             ram.fill(0, 0, kPage);
             need(ram[0] == 0 && ram.allocated_pages() == 1, "Zero fill released rollback backing");
         }},
        {"reproducible mixed blocks match a dense byte oracle", [] {
             constexpr std::size_t length = 17 * kPage + 37;
             SparseRam ram(length);
             std::vector<std::uint8_t> oracle(length, 0);
             std::uint64_t state = 0x736f6c6172737061ULL;
             auto next = [&] {
                 state = state * 6364136223846793005ULL + 1442695040888963407ULL;
                 return state;
             };
             for (unsigned operation = 0; operation < 256; ++operation) {
                 const std::size_t offset = static_cast<std::size_t>(next() % length);
                 const std::size_t count = std::min<std::size_t>(next() % (2 * kPage + 1), length - offset);
                 std::vector<std::uint8_t> payload(count);
                 for (auto& byte : payload) byte = static_cast<std::uint8_t>(next() >> 24);
                 if (operation % 3 == 0) {
                     ram.fill(offset, 0, count);
                     std::fill_n(oracle.begin() + offset, count, 0);
                 } else {
                     ram.write_block(offset, payload.data(), payload.size());
                     std::copy(payload.begin(), payload.end(), oracle.begin() + offset);
                 }
             }
             need(read(ram, 0, length) == oracle, "Sparse and dense byte semantics differ");
             need(ram.allocated_bytes() <= ram.budget_bytes(), "Mixed operations exceeded the cap");
         }},
    };

    for (const auto& test : cases) {
        try {
            test.second();
        } catch (const std::exception& error) {
            std::cerr << "FAIL: " << test.first << ": " << error.what() << '\n';
            return 1;
        }
    }
    std::cout << "Sparse RAM: " << cases.size() << "/" << cases.size()
              << " cases passed (" << checks << " assertions).\n";
    return 0;
}
