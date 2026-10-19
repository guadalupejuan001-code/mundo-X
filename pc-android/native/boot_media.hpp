#ifndef COSMIC_PC_BOOT_MEDIA_HPP
#define COSMIC_PC_BOOT_MEDIA_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace cosmic_pc {

struct BootPlan {
    std::vector<std::uint8_t> image;
    std::uint16_t load_segment = 0;
    std::uint16_t initial_cs = 0;
    std::uint16_t initial_ip = 0;
    std::uint8_t drive = 0;
    std::uint32_t logical_sector_size = 0;
    std::uint64_t source_offset = 0;
    std::string kind;
};

// A bounded, read-only loader. It neither executes the image nor owns a file.
// The callback must fill all requested bytes before returning true.
//
// El Torito field offsets follow the Phoenix/IBM specification, version 1.0:
// https://pdos.csail.mit.edu/6.828/2005/readings/boot-cdrom.pdf
// This implementation supports only the initial x86 no-emulation entry.
class BootMedia {
public:
    using ReadCallback =
        std::function<bool(std::uint64_t, std::uint8_t*, std::size_t)>;
    using Reader = ReadCallback;

    static constexpr std::uint32_t kCdSectorSize = 2048;
    static constexpr std::uint32_t kDiskSectorSize = 512;
    static constexpr std::uint16_t kMaxBootSectors = 128; // 64 KiB.
    static constexpr std::uint32_t kMaxVolumeDescriptors = 256;

    BootMedia(std::uint64_t size_bytes, ReadCallback reader)
        : size_bytes_(size_bytes), reader_(std::move(reader)) {
        if (!reader_) {
            throw std::runtime_error("Boot medium has no read callback.");
        }
    }

    std::uint64_t size() const noexcept { return size_bytes_; }

    // Invalid ranges, a null nonempty destination, and callback I/O failures
    // return false. No callback is made for an invalid or empty range.
    bool read(std::uint64_t offset, std::uint8_t* destination,
              std::size_t count) const {
        if (!contains(offset, count) || (count != 0 && destination == nullptr)) {
            return false;
        }
        if (count == 0) {
            return true;
        }
        return reader_(offset, destination, count);
    }

    // Supported modes and BootPlan::kind values are "raw" and "iso".
    // "auto" prefers an ISO descriptor signature over a raw boot signature,
    // so hybrid images use their El Torito boot entry. Invalid ISO data is not
    // silently retried as a raw disk. All load failures throw runtime_error.
    BootPlan plan(const std::string& mode = "auto") const {
        if (mode == "raw") {
            return loadRaw();
        }
        if (mode == "iso") {
            return loadIso();
        }
        if (mode != "auto") {
            throw std::runtime_error("Unsupported boot-medium mode '" + mode +
                                     "'; use auto, raw, or iso.");
        }
        return looksLikeIso() ? loadIso() : loadRaw();
    }

private:
    static constexpr std::uint16_t kDefaultLoadSegment = 0x07c0;
    static constexpr std::uint64_t kFirstBootDescriptor = 17;

    std::uint64_t size_bytes_;
    ReadCallback reader_;

    bool contains(std::uint64_t offset, std::size_t count) const noexcept {
        static_assert(sizeof(std::size_t) <= sizeof(std::uint64_t),
                      "BootMedia requires size_t to fit in uint64_t.");
        // Subtraction after the offset check avoids offset + count overflow.
        return offset <= size_bytes_ &&
               static_cast<std::uint64_t>(count) <= size_bytes_ - offset;
    }

    void readExact(std::uint64_t offset, std::uint8_t* destination,
                   std::size_t count, const std::string& context) const {
        if (!contains(offset, count)) {
            throw std::runtime_error(
                context + ": byte range at " + std::to_string(offset) +
                " with length " + std::to_string(count) +
                " exceeds medium size " + std::to_string(size_bytes_) + ".");
        }
        if (!read(offset, destination, count)) {
            throw std::runtime_error(context + ": read callback failed at byte " +
                                     std::to_string(offset) + ".");
        }
    }

    static std::uint16_t le16(const std::uint8_t* bytes) noexcept {
        return static_cast<std::uint16_t>(
            static_cast<std::uint16_t>(bytes[0]) |
            (static_cast<std::uint16_t>(bytes[1]) << 8));
    }

    static std::uint32_t le32(const std::uint8_t* bytes) noexcept {
        return static_cast<std::uint32_t>(bytes[0]) |
               (static_cast<std::uint32_t>(bytes[1]) << 8) |
               (static_cast<std::uint32_t>(bytes[2]) << 16) |
               (static_cast<std::uint32_t>(bytes[3]) << 24);
    }

    static bool hasIsoIdentifier(const std::uint8_t* bytes) noexcept {
        return std::memcmp(bytes + 1, "CD001", 5) == 0;
    }

    bool looksLikeIso() const {
        std::array<std::uint8_t, 7> prefix{};
        for (const std::uint64_t lba : {std::uint64_t{16}, std::uint64_t{17}}) {
            const std::uint64_t offset = lba * kCdSectorSize;
            if (!contains(offset, prefix.size())) {
                continue;
            }
            readExact(offset, prefix.data(), prefix.size(),
                      "Probing ISO9660 volume descriptor");
            if (hasIsoIdentifier(prefix.data())) {
                return true;
            }
        }
        return false;
    }

    BootPlan loadRaw() const {
        BootPlan result;
        result.image.resize(kDiskSectorSize);
        readExact(0, result.image.data(), result.image.size(), "Raw boot sector");
        if (result.image[510] != 0x55 || result.image[511] != 0xaa) {
            throw std::runtime_error("Raw boot sector is missing the 55 AA signature.");
        }
        result.load_segment = kDefaultLoadSegment;
        result.initial_cs = 0;
        result.initial_ip = 0x7c00;
        result.drive = 0x80;
        result.logical_sector_size = kDiskSectorSize;
        result.source_offset = 0;
        result.kind = "raw";
        return result;
    }

    static bool hasElToritoIdentifier(const std::uint8_t* descriptor) noexcept {
        constexpr char identifier[] = "EL TORITO SPECIFICATION";
        constexpr std::size_t length = sizeof(identifier) - 1;
        if (std::memcmp(descriptor + 7, identifier, length) != 0) {
            return false;
        }
        // The specification reserves 32 bytes and requires zero padding.
        for (std::size_t i = length; i < 32; ++i) {
            if (descriptor[7 + i] != 0) {
                return false;
            }
        }
        return true;
    }

    BootPlan loadIso() const {
        std::array<std::uint8_t, kCdSectorSize> descriptor{};
        for (std::uint32_t index = 0; index < kMaxVolumeDescriptors; ++index) {
            const std::uint64_t lba = kFirstBootDescriptor + index;
            const std::uint64_t offset = lba * kCdSectorSize;
            // Both factors above are bounded constants/uint32 values. LBA
            // fields below are promoted to uint64 before multiplication.
            readExact(offset, descriptor.data(), descriptor.size(),
                      "ISO9660 volume descriptor at LBA " + std::to_string(lba));
            if (!hasIsoIdentifier(descriptor.data())) {
                throw std::runtime_error("Invalid ISO9660 CD001 signature at LBA " +
                                         std::to_string(lba) + ".");
            }
            if (descriptor[6] != 1) {
                throw std::runtime_error("Unsupported ISO9660 descriptor version at LBA " +
                                         std::to_string(lba) + "; expected 1.");
            }
            if (descriptor[0] == 255) {
                throw std::runtime_error("No El Torito boot record before the ISO9660 terminator.");
            }
            if (descriptor[0] == 0 && hasElToritoIdentifier(descriptor.data())) {
                return loadCatalog(le32(descriptor.data() + 0x47));
            }
        }
        throw std::runtime_error("No El Torito boot record within the 256-descriptor scan limit.");
    }

    BootPlan loadCatalog(std::uint32_t catalog_lba) const {
        std::array<std::uint8_t, 64> catalog{};
        const std::uint64_t catalog_offset =
            static_cast<std::uint64_t>(catalog_lba) * kCdSectorSize;
        readExact(catalog_offset, catalog.data(), catalog.size(), "El Torito boot catalog");
        if (catalog[0] != 0x01) {
            throw std::runtime_error("Invalid El Torito validation header; expected 01.");
        }
        if (catalog[1] != 0x00) {
            throw std::runtime_error("Unsupported El Torito platform; the initial entry must be x86 (00).");
        }
        if (catalog[30] != 0x55 || catalog[31] != 0xaa) {
            throw std::runtime_error("Invalid El Torito validation keys; expected 55 AA.");
        }
        std::uint32_t sum = 0;
        for (std::size_t i = 0; i < 32; i += 2) {
            sum += le16(catalog.data() + i);
        }
        if ((sum & 0xffffu) != 0) {
            throw std::runtime_error("Invalid El Torito validation checksum.");
        }

        const std::uint8_t* entry = catalog.data() + 32;
        if (entry[0] != 0x88) {
            throw std::runtime_error("El Torito initial entry is not bootable (expected 88).");
        }
        if (entry[1] != 0x00) {
            throw std::runtime_error("Unsupported El Torito media type; only no-emulation (00) is supported.");
        }
        const std::uint16_t sector_count = le16(entry + 6);
        if (sector_count == 0 || sector_count > kMaxBootSectors) {
            throw std::runtime_error("El Torito boot sector count must be 1..128 (at most 64 KiB).");
        }
        const std::uint16_t requested_segment = le16(entry + 2);

        BootPlan result;
        result.load_segment =
            requested_segment == 0 ? kDefaultLoadSegment : requested_segment;
        result.initial_cs = result.load_segment == kDefaultLoadSegment
                                ? 0
                                : result.load_segment;
        result.initial_ip = result.load_segment == kDefaultLoadSegment ? 0x7c00 : 0;
        result.drive = 0xe0;
        result.logical_sector_size = kCdSectorSize;
        result.source_offset =
            static_cast<std::uint64_t>(le32(entry + 8)) * kCdSectorSize;
        result.kind = "iso";
        result.image.resize(static_cast<std::size_t>(sector_count) * kDiskSectorSize);
        readExact(result.source_offset, result.image.data(), result.image.size(),
                  "El Torito boot image");
        // No 55 AA check here: no-emulation boot images need not carry it.
        // The consumer must check the target RAM range before loading bytes.
        return result;
    }
};

} // namespace cosmic_pc

#endif // COSMIC_PC_BOOT_MEDIA_HPP
