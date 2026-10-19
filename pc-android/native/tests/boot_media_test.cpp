#include "../boot_media.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using cosmic_pc::BootMedia;
using Bytes = std::vector<std::uint8_t>;
constexpr std::size_t kCdSector = 2048;
constexpr std::size_t kBootRecord = 17 * kCdSector;
constexpr std::size_t kCatalog = 20 * kCdSector;
constexpr std::size_t kEntry = kCatalog + 32;
constexpr std::size_t kImage = 24 * kCdSector;

void require(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

template <typename Operation>
void expectError(Operation operation, const std::string& fragment) {
    try {
        operation();
    } catch (const std::runtime_error& error) {
        require(std::string(error.what()).find(fragment) != std::string::npos,
                "Expected error containing '" + fragment + "', received '" +
                    error.what() + "'.");
        return;
    }
    throw std::runtime_error("Expected an error containing '" + fragment + "'.");
}

void put16(Bytes& bytes, std::size_t offset, std::uint16_t value) {
    bytes.at(offset) = static_cast<std::uint8_t>(value);
    bytes.at(offset + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(Bytes& bytes, std::size_t offset, std::uint32_t value) {
    for (std::size_t i = 0; i < 4; ++i) {
        bytes.at(offset + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void descriptor(Bytes& bytes, std::size_t sector, std::uint8_t type) {
    const std::size_t offset = sector * kCdSector;
    bytes.at(offset) = type;
    std::memcpy(bytes.data() + offset + 1, "CD001", 5);
    bytes.at(offset + 6) = 1;
}

void fixChecksum(Bytes& bytes) {
    put16(bytes, kCatalog + 28, 0);
    std::uint32_t sum = 0;
    for (std::size_t i = 0; i < 32; i += 2) {
        sum += static_cast<std::uint32_t>(bytes.at(kCatalog + i)) |
               (static_cast<std::uint32_t>(bytes.at(kCatalog + i + 1)) << 8);
    }
    put16(bytes, kCatalog + 28, static_cast<std::uint16_t>(0u - sum));
}

Bytes rawFixture() {
    Bytes bytes(512, 0);
    // Our own tiny real-mode program: mov ax,0e43h; int 10h; hlt.
    // It would ask a BIOS to print 'C'. Tests only compare these bytes.
    constexpr std::array<std::uint8_t, 6> program{{0xb8, 0x43, 0x0e, 0xcd, 0x10, 0xf4}};
    std::copy(program.begin(), program.end(), bytes.begin());
    bytes[510] = 0x55;
    bytes[511] = 0xaa;
    return bytes;
}

Bytes isoFixture(std::uint16_t sector_count = 4, std::uint16_t segment = 0) {
    Bytes bytes(kImage + std::max<std::size_t>(kCdSector,
                                             static_cast<std::size_t>(sector_count) * 512),
                0);
    descriptor(bytes, 16, 1);
    descriptor(bytes, 17, 0);
    descriptor(bytes, 18, 255);
    constexpr char identifier[] = "EL TORITO SPECIFICATION";
    std::memcpy(bytes.data() + kBootRecord + 7, identifier, sizeof(identifier) - 1);
    put32(bytes, kBootRecord + 0x47, 20);
    bytes[kCatalog] = 0x01;
    bytes[kCatalog + 1] = 0x00;
    bytes[kCatalog + 30] = 0x55;
    bytes[kCatalog + 31] = 0xaa;
    fixChecksum(bytes);
    bytes[kEntry] = 0x88;
    bytes[kEntry + 1] = 0x00;
    put16(bytes, kEntry + 2, segment);
    put16(bytes, kEntry + 6, sector_count);
    put32(bytes, kEntry + 8, 24);
    // Original fixture instructions, deliberately without a 55 AA signature.
    constexpr std::array<std::uint8_t, 9> program{{
        0xb8, 0x49, 0x0e, 0xbb, 0x07, 0x00, 0xcd, 0x10, 0xf4}};
    std::copy(program.begin(), program.end(), bytes.begin() + kImage);
    for (std::size_t i = program.size(); i < static_cast<std::size_t>(sector_count) * 512; ++i) {
        bytes[kImage + i] = static_cast<std::uint8_t>((i * 37u + 11u) & 0xffu);
    }
    return bytes;
}

BootMedia memoryMedia(const Bytes& bytes) {
    return BootMedia(bytes.size(), [&bytes](std::uint64_t offset,
                                           std::uint8_t* destination,
                                           std::size_t count) {
        if (offset > bytes.size() || count > bytes.size() - offset) {
            return false;
        }
        std::memcpy(destination, bytes.data() + static_cast<std::size_t>(offset), count);
        return true;
    });
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"raw boot plan and complete bytes", [] {
             const Bytes bytes = rawFixture();
             const auto plan = memoryMedia(bytes).plan();
             require(plan.image == bytes, "Raw image bytes changed.");
             require(plan.kind == "raw" && plan.source_offset == 0, "Wrong raw source.");
             require(plan.load_segment == 0x07c0 && plan.initial_cs == 0 &&
                         plan.initial_ip == 0x7c00, "Wrong raw entry point.");
             require(plan.drive == 0x80 && plan.logical_sector_size == 512,
                     "Wrong raw disk geometry.");
         }},
        {"raw disk larger than its boot sector", [] {
             Bytes bytes = rawFixture();
             bytes.resize(4096, 0x9a);
             const auto plan = memoryMedia(bytes).plan("raw");
             require(plan.image.size() == 512, "Raw boot must load exactly 512 bytes.");
         }},
        {"missing raw signature", [] {
             Bytes bytes = rawFixture();
             bytes[511] = 0;
             expectError([&] { memoryMedia(bytes).plan("raw"); }, "55 AA");
         }},
        {"truncated raw sector", [] {
             Bytes bytes = rawFixture();
             bytes.resize(511);
             expectError([&] { memoryMedia(bytes).plan(); }, "exceeds medium size");
         }},
        {"default ISO plan and full image", [] {
             const Bytes bytes = isoFixture();
             const auto plan = memoryMedia(bytes).plan();
             require(plan.kind == "iso" && plan.source_offset == kImage, "Wrong ISO source.");
             require(plan.image.size() == 4 * 512, "Count is measured in 512-byte sectors.");
             require(std::equal(plan.image.begin(), plan.image.end(), bytes.begin() + kImage),
                     "ISO payload bytes changed.");
             require(plan.load_segment == 0x07c0 && plan.initial_cs == 0 &&
                         plan.initial_ip == 0x7c00, "Wrong default ISO entry point.");
             require(plan.drive == 0xe0 && plan.logical_sector_size == 2048,
                     "Wrong ISO disk geometry.");
         }},
        {"explicit default load segment", [] {
             const Bytes bytes = isoFixture(1, 0x07c0);
             const auto plan = memoryMedia(bytes).plan("iso");
             require(plan.initial_cs == 0 && plan.initial_ip == 0x7c00,
                     "Explicit default segment should use the canonical entry point.");
         }},
        {"nondefault load segment", [] {
             const Bytes bytes = isoFixture(1, 0x1000);
             const auto plan = memoryMedia(bytes).plan("iso");
             require(plan.load_segment == 0x1000 && plan.initial_cs == 0x1000 &&
                         plan.initial_ip == 0, "Nondefault segment was discarded.");
         }},
        {"64 KiB boot image accepted", [] {
             const Bytes bytes = isoFixture(128);
             require(memoryMedia(bytes).plan("iso").image.size() == 65536,
                     "Maximum supported image size must work.");
         }},
        {"no-emulation image does not require a raw signature", [] {
             Bytes bytes = isoFixture(1);
             bytes[kImage + 510] = 0;
             bytes[kImage + 511] = 0;
             require(memoryMedia(bytes).plan("iso").image.size() == 512,
                     "No-emulation images need no 55 AA boot-sector signature.");
         }},
        {"hybrid ISO preferred over raw", [] {
             Bytes bytes = isoFixture();
             bytes[510] = 0x55;
             bytes[511] = 0xaa;
             require(memoryMedia(bytes).plan().kind == "iso", "Hybrid must select ISO.");
             require(memoryMedia(bytes).plan("raw").kind == "raw", "Explicit raw mode ignored.");
         }},
        {"boot record found after sector 17", [] {
             Bytes bytes = isoFixture();
             std::copy_n(bytes.begin() + kBootRecord, kCdSector,
                         bytes.begin() + 18 * kCdSector);
             std::fill_n(bytes.begin() + kBootRecord, kCdSector, 0);
             descriptor(bytes, 17, 2);
             descriptor(bytes, 19, 255);
             require(memoryMedia(bytes).plan("iso").source_offset == kImage,
                     "Later boot record not found.");
         }},
        {"bad catalog checksum", [] {
             Bytes bytes = isoFixture();
             bytes[kCatalog + 4] ^= 1;
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "checksum");
         }},
        {"bad catalog validation header", [] {
             Bytes bytes = isoFixture();
             bytes[kCatalog] = 2;
             fixChecksum(bytes);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "validation header");
         }},
        {"unsupported catalog platform", [] {
             Bytes bytes = isoFixture();
             bytes[kCatalog + 1] = 0xef;
             fixChecksum(bytes);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "platform");
         }},
        {"bad catalog keys", [] {
             Bytes bytes = isoFixture();
             bytes[kCatalog + 30] = 0;
             fixChecksum(bytes);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "validation keys");
         }},
        {"unsupported emulated media", [] {
             Bytes bytes = isoFixture();
             bytes[kEntry + 1] = 4;
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "no-emulation");
         }},
        {"entry marked nonbootable", [] {
             Bytes bytes = isoFixture();
             bytes[kEntry] = 0;
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "not bootable");
         }},
        {"zero boot sectors", [] {
             Bytes bytes = isoFixture();
             put16(bytes, kEntry + 6, 0);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "count must be 1..128");
         }},
        {"excessive boot sector count", [] {
             Bytes bytes = isoFixture();
             put16(bytes, kEntry + 6, 129);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "count must be 1..128");
         }},
        {"invalid ISO identifier", [] {
             Bytes bytes = isoFixture();
             bytes[kBootRecord + 1] = 'X';
             expectError([&] { memoryMedia(bytes).plan(); }, "CD001");
         }},
        {"invalid descriptor version", [] {
             Bytes bytes = isoFixture();
             bytes[kBootRecord + 6] = 2;
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "version");
         }},
        {"missing boot record", [] {
             Bytes bytes = isoFixture();
             bytes[kBootRecord] = 2;
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "No El Torito boot record");
         }},
        {"invalid El Torito system identifier", [] {
             Bytes bytes = isoFixture();
             bytes[kBootRecord + 7] = 'X';
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "No El Torito boot record");
         }},
        {"nonzero system identifier padding", [] {
             Bytes bytes = isoFixture();
             bytes[kBootRecord + 7 + 31] = 1;
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "No El Torito boot record");
         }},
        {"truncated ISO descriptor", [] {
             Bytes bytes = isoFixture();
             bytes.resize(kBootRecord + kCdSector - 1);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "volume descriptor");
         }},
        {"truncated catalog", [] {
             Bytes bytes = isoFixture();
             bytes.resize(kCatalog + 63);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "boot catalog: byte range");
         }},
        {"truncated boot image", [] {
             Bytes bytes = isoFixture();
             bytes.resize(kImage + 4 * 512 - 1);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "boot image: byte range");
         }},
        {"catalog LBA does not wrap at 32 bits", [] {
             Bytes bytes = isoFixture();
             put32(bytes, kBootRecord + 0x47, 0xffffffffu);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "8796093020160");
         }},
        {"image LBA does not wrap at 32 bits", [] {
             Bytes bytes = isoFixture();
             put32(bytes, kEntry + 8, 0xffffffffu);
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "8796093020160");
         }},
        {"failed reader is reported", [] {
             const BootMedia media(512, [](std::uint64_t, std::uint8_t*, std::size_t) {
                 return false;
             });
             expectError([&] { media.plan("raw"); }, "read callback failed");
         }},
        {"missing callback rejected", [] {
             expectError([] { BootMedia media(512, {}); }, "no read callback");
         }},
        {"public read enforces bounds without calling reader", [] {
             std::size_t calls = 0;
             const BootMedia media(std::numeric_limits<std::uint64_t>::max(),
                                   [&calls](std::uint64_t, std::uint8_t*, std::size_t) {
                                       ++calls;
                                       return true;
                                   });
             std::array<std::uint8_t, 16> destination{};
             require(!media.read(std::numeric_limits<std::uint64_t>::max() - 3,
                                 destination.data(), destination.size()),
                     "Overflowing range accepted.");
             require(!media.read(0, nullptr, 1), "Nonempty null destination accepted.");
             require(media.read(media.size(), nullptr, 0), "Empty read at EOF rejected.");
             require(calls == 0, "Invalid/empty ranges invoked callback.");
             require(media.read(10, destination.data(), destination.size()) && calls == 1,
                     "Valid read failed.");
         }},
        {"descriptor scanning is bounded", [] {
             Bytes bytes((17 + BootMedia::kMaxVolumeDescriptors) * kCdSector, 0);
             for (std::size_t i = 16; i < 17 + BootMedia::kMaxVolumeDescriptors; ++i) {
                 descriptor(bytes, i, 2);
             }
             expectError([&] { memoryMedia(bytes).plan("iso"); }, "scan limit");
         }},
        {"unknown mode rejected", [] {
             const Bytes bytes = rawFixture();
             expectError([&] { memoryMedia(bytes).plan("uefi"); }, "Unsupported boot-medium mode");
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
    std::cout << "BootMedia: " << cases.size() << "/" << cases.size()
              << " synthetic cases passed.\n";
    return 0;
}
