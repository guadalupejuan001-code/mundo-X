#include "../pc_machine.hpp"

#include <algorithm>
#include <array>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

using Bytes = std::vector<std::uint8_t>;
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;

constexpr std::size_t kSector = 2048;
constexpr std::uint32_t kCatalogLba = 20;
constexpr std::uint32_t kBootLba = 24;
constexpr std::uint32_t kDataLba = 30;
constexpr std::uint32_t kVolumeSectors = 31;
constexpr std::uint16_t kDap = 0x7d00;
constexpr std::uint16_t kParameters = 0x7d20;
constexpr std::uint16_t kStatusPacket = 0x7d40;
constexpr std::uint16_t kSavedDiskAx = 0x8ffe;
constexpr std::uint16_t kBuffer = 0x9000;
unsigned checks = 0;

void need(bool value, const std::string& reason) {
    ++checks;
    if (!value) throw std::runtime_error(reason);
}

void put16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}

void put32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * i));
    }
}

void putBig16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    bytes.at(at) = static_cast<std::uint8_t>(value >> 8);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value);
}

void putBig32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) {
        bytes.at(at + i) = static_cast<std::uint8_t>(value >> (8 * (3 - i)));
    }
}

void putBoth16(Bytes& bytes, std::size_t at, std::uint16_t value) {
    put16(bytes, at, value);
    putBig16(bytes, at + 2, value);
}

void putBoth32(Bytes& bytes, std::size_t at, std::uint32_t value) {
    put32(bytes, at, value);
    putBig32(bytes, at + 4, value);
}

void fixedText(Bytes& bytes, std::size_t at, std::size_t length,
               const std::string& text) {
    if (text.size() > length) throw std::logic_error("Fixture text too long");
    std::fill_n(bytes.begin() + at, length, ' ');
    std::copy(text.begin(), text.end(), bytes.begin() + at);
}

void volumeDescriptor(Bytes& bytes, unsigned lba, std::uint8_t type) {
    const auto at = std::size_t(lba) * kSector;
    bytes.at(at) = type;
    std::memcpy(bytes.data() + at + 1, "CD001", 5);
    bytes.at(at + 6) = 1;
}

std::size_t directoryRecord(Bytes& bytes, std::size_t at,
                            std::uint32_t extent, std::uint32_t length,
                            bool directory, const std::string& name) {
    const std::size_t record_size = (33 + name.size() + 1) & ~std::size_t{1};
    bytes.at(at) = static_cast<std::uint8_t>(record_size);
    putBoth32(bytes, at + 2, extent);
    putBoth32(bytes, at + 10, length);
    bytes.at(at + 18) = 126; // 2026-01-01 00:00:00 UTC.
    bytes.at(at + 19) = 1;
    bytes.at(at + 20) = 1;
    bytes.at(at + 25) = directory ? 2 : 0;
    putBoth16(bytes, at + 28, 1);
    bytes.at(at + 32) = static_cast<std::uint8_t>(name.size());
    std::copy(name.begin(), name.end(), bytes.begin() + at + 33);
    return record_size;
}

// A self-contained ISO9660 volume with path tables, a root directory and three
// files. The boot catalog, guest program and data are all authored in this test.
// No external OS, firmware, assembler or emulator is used to make the image.
Bytes isoFixture(const Bytes& guest) {
    if (guest.size() > 512) throw std::logic_error("Fixture guest exceeds boot load");
    Bytes bytes(std::size_t(kVolumeSectors) * kSector, 0);
    volumeDescriptor(bytes, 16, 1);
    volumeDescriptor(bytes, 17, 0);
    volumeDescriptor(bytes, 18, 255);

    const std::size_t pvd = 16 * kSector;
    fixedText(bytes, pvd + 8, 32, "COSMIC PC");
    fixedText(bytes, pvd + 40, 32, "COSMIC_PC_TEST");
    putBoth32(bytes, pvd + 80, kVolumeSectors);
    putBoth16(bytes, pvd + 120, 1);
    putBoth16(bytes, pvd + 124, 1);
    putBoth16(bytes, pvd + 128, kSector);
    putBoth32(bytes, pvd + 132, 10);
    put32(bytes, pvd + 140, 19);
    putBig32(bytes, pvd + 148, 21);
    directoryRecord(bytes, pvd + 156, 22, kSector, true, std::string(1, '\0'));
    fixedText(bytes, pvd + 190, 128, "COSMIC_PC_TEST");
    fixedText(bytes, pvd + 318, 128, "COSMIC PC");
    fixedText(bytes, pvd + 446, 128, "COSMIC PC");
    fixedText(bytes, pvd + 574, 128, "ORIGINAL X86 INTEGRATION FIXTURE");
    fixedText(bytes, pvd + 702, 37, "");
    fixedText(bytes, pvd + 739, 37, "");
    fixedText(bytes, pvd + 776, 37, "");
    for (const unsigned date : {813u, 830u, 847u, 864u}) {
        std::memcpy(bytes.data() + pvd + date, "2026010100000000", 16);
    }
    bytes[pvd + 881] = 1;
    bytes[19 * kSector] = 1;
    put32(bytes, 19 * kSector + 2, 22);
    put16(bytes, 19 * kSector + 6, 1);
    bytes[21 * kSector] = 1;
    putBig32(bytes, 21 * kSector + 2, 22);
    putBig16(bytes, 21 * kSector + 6, 1);
    std::size_t record = 22 * kSector;
    record += directoryRecord(bytes, record, 22, kSector, true, std::string(1, '\0'));
    record += directoryRecord(bytes, record, 22, kSector, true, std::string(1, '\1'));
    record += directoryRecord(bytes, record, kCatalogLba, kSector, false, "BOOT.CAT;1");
    record += directoryRecord(bytes, record, kBootLba, kSector, false, "BOOT.BIN;1");
    directoryRecord(bytes, record, kDataLba, kSector, false, "DATA.BIN;1");

    constexpr char identifier[] = "EL TORITO SPECIFICATION";
    std::memcpy(bytes.data() + 17 * kSector + 7, identifier, sizeof(identifier) - 1);
    put32(bytes, 17 * kSector + 0x47, kCatalogLba);
    const std::size_t catalog = kCatalogLba * kSector;
    bytes[catalog] = 1;
    bytes[catalog + 30] = 0x55;
    bytes[catalog + 31] = 0xaa;
    std::uint32_t sum = 0;
    for (unsigned i = 0; i < 32; i += 2) {
        sum += std::uint32_t(bytes[catalog + i]) |
               (std::uint32_t(bytes[catalog + i + 1]) << 8);
    }
    put16(bytes, catalog + 28, static_cast<std::uint16_t>(0u - sum));
    bytes[catalog + 32] = 0x88;
    put16(bytes, catalog + 32 + 6, 1); // Initial boot load: one 512-byte sector.
    put32(bytes, catalog + 32 + 8, kBootLba);
    std::copy(guest.begin(), guest.end(), bytes.begin() + kBootLba * kSector);

    const std::size_t data = kDataLba * kSector;
    for (std::size_t i = 0; i < kSector; ++i) {
        bytes[data + i] = static_cast<std::uint8_t>((i * 29 + 3) & 255);
    }
    put16(bytes, data, 0x5843);
    put16(bytes, data + kSector - 2, 0xa75d);
    return bytes;
}

Bytes readGuest(std::uint64_t lba = kDataLba) {
    Bytes code;
    std::vector<std::size_t> failures;
    auto emit = [&](std::initializer_list<std::uint8_t> values) {
        code.insert(code.end(), values);
    };
    auto failIf = [&](std::uint8_t branch) {
        emit({branch, 0});
        failures.push_back(code.size() - 1);
    };
    // Initialize the segments in guest code and verify the BIOS boot drive.
    emit({0xfa, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0,
          0xbc, 0x00, 0x7c, 0xfb, 0xfc, 0x80, 0xfa, 0xe0});
    failIf(0x75);
    // INT 13h AH=41h: discover extended disk reads.
    emit({0xb8, 0x00, 0x41, 0xbb, 0xaa, 0x55, 0xcd, 0x13});
    failIf(0x72);
    emit({0x81, 0xfb, 0x55, 0xaa});
    failIf(0x75);
    emit({0xf7, 0xc1, 0x01, 0x00});
    failIf(0x74);
    // AH=48h: the guest requires a logical CD sector to contain 2048 bytes.
    emit({0xbe, 0x20, 0x7d, 0xb4, 0x48, 0xcd, 0x13});
    failIf(0x72);
    emit({0x81, 0x3e, 0x38, 0x7d, 0x00, 0x08});
    failIf(0x75);
    // AH=42h loads another LBA, not any bytes from the initial boot image.
    emit({0xbe, 0x00, 0x7d, 0xb4, 0x42, 0xcd, 0x13, 0xa3, 0xfe, 0x8f});
    failIf(0x72);
    emit({0x81, 0x3e, 0x00, 0x90, 0x43, 0x58});
    failIf(0x75);
    emit({0x81, 0x3e, 0xfe, 0x97, 0x5d, 0xa7});
    failIf(0x75);
    // 'I' is produced only after guest comparisons, via BIOS video output.
    emit({0xb8, 'I', 0x0e, 0xbb, 0x07, 0x00, 0xcd, 0x10, 0xfa, 0xf4});
    const std::size_t failed = code.size();
    emit({0xb8, 'F', 0x0e, 0xbb, 0x07, 0x00, 0xcd, 0x10, 0xfa, 0xf4});
    for (const auto at : failures) {
        const auto distance = static_cast<long>(failed) - static_cast<long>(at + 1);
        if (distance < -128 || distance > 127) throw std::logic_error("Fixture branch range");
        code[at] = static_cast<std::uint8_t>(distance);
    }
    if (code.size() > 0x100) throw std::logic_error("Fixture code overlaps DAP");
    code.resize(512, 0);
    code[kDap - 0x7c00] = 16;
    put16(code, kDap - 0x7c00 + 2, 1);
    put16(code, kDap - 0x7c00 + 4, kBuffer);
    put32(code, kDap - 0x7c00 + 8, static_cast<std::uint32_t>(lba));
    put32(code, kDap - 0x7c00 + 12, static_cast<std::uint32_t>(lba >> 32));
    put16(code, kParameters - 0x7c00, 26);
    return code;
}

struct ReadEvent { std::uint64_t offset; std::size_t count; };

struct MountedFixture {
    std::shared_ptr<Bytes> bytes;
    std::shared_ptr<std::vector<ReadEvent>> reads;
};

MountedFixture mount(PcMachine& pc, Bytes bytes, bool fail_data_read = false) {
    MountedFixture fixture{std::make_shared<Bytes>(std::move(bytes)),
                           std::make_shared<std::vector<ReadEvent>>()};
    const auto image = fixture.bytes;
    const auto reads = fixture.reads;
    pc.mount([image, reads, fail_data_read](std::uint64_t offset,
                                          std::uint8_t* destination,
                                          std::size_t count) {
        reads->push_back({offset, count});
        if (offset > image->size() || count > image->size() - offset) return false;
        if (fail_data_read && offset == std::uint64_t(kDataLba) * kSector) {
            // A failed host read may have dirtied its temporary destination;
            // the BIOS must still leave the guest's destination unchanged.
            std::fill_n(destination, std::min<std::size_t>(count, 37), 0x3e);
            return false;
        }
        std::copy_n(image->data() + static_cast<std::size_t>(offset), count, destination);
        return true;
    }, image->size(), "iso");
    reads->clear(); // Subsequent events are BIOS reads during guest execution.
    return fixture;
}

void run(PcMachine& pc) {
    for (unsigned slice = 0; slice < 8 && !pc.cpu.halted && !pc.cpu.fault &&
                              !pc.waiting_key; ++slice) {
        pc.run(128);
    }
    need(pc.cpu.halted || pc.cpu.fault || pc.waiting_key,
         "Guest did not stop within 1024 execution steps");
}

void completed(const PcMachine& pc, char output) {
    need(!pc.cpu.fault, pc.cpu.fault ? pc.cpu.fault->describe() : "Unexpected CPU fault");
    need(pc.cpu.halted && !pc.waiting_key, "Expected guest HLT");
    need(pc.screen().front() == output, "Wrong character emitted by guest through INT 10h");
}

Bytes ram_bytes(const PcMachine& pc, std::uint64_t offset, std::size_t count) {
    Bytes bytes(count);
    pc.cpu.memory.read_block(offset, bytes.data(), bytes.size());
    return bytes;
}

} // namespace

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> cases{
        {"CD boot, EDD discovery, geometry and extended read", [] {
             PcMachine pc(2 * 1024 * 1024);
             const auto fixture = mount(pc, isoFixture(readGuest()));
             need(pc.cpu.reg8(2) == 0xe0, "El Torito DL must be E0");
             need(pc.cpu.segments[Cpu::CS] == 0 && pc.cpu.eip == 0x7c00,
                  "Wrong default CD entry point");
             run(pc);
             completed(pc, 'I');
             need(pc.cpu.instructions > 20, "Guest verification instructions did not execute");
             need(pc.disk_reads == 1 && fixture.reads->size() == 1,
                  "Expected exactly one guest BIOS disk read");
             need(fixture.reads->front().offset == std::uint64_t(kDataLba) * kSector &&
                      fixture.reads->front().count == kSector,
                  "EDD must use 2048-byte CD sectors and the requested LBA");
             need(ram_bytes(pc, kBuffer, kSector) == Bytes(
                             fixture.bytes->begin() + kDataLba * kSector,
                             fixture.bytes->begin() + (kDataLba + 1) * kSector),
                  "The complete CD data sector must reach guest RAM");
             need(pc.cpu.read16(kDap + 2) == 1 && pc.cpu.read16(kParameters + 24) == 2048,
                  "BIOS parameter packets contain wrong transfer size");
             need(pc.cpu.read32(kParameters + 16) == kVolumeSectors,
                  "BIOS extended geometry contains wrong sector count");
             need(pc.cpu.read16(0x410) == 0x0020,
                  "CD boot BDA equipment must match the available hardware");
             need(pc.cpu.read8(0x475) == 0,
                  "A mounted CD must not advertise a fixed disk in the BDA");
             need(pc.cpu.read16(kParameters + 2) == 0,
                  "CD extended parameters must not advertise valid CHS geometry");
             need(pc.cpu.read32(kParameters + 4) == 0 &&
                      pc.cpu.read32(kParameters + 8) == 0 &&
                      pc.cpu.read32(kParameters + 12) == 0,
                  "CD extended parameters must leave CHS fields zero");
         }},
        {"guest rejects corrupted data", [] {
             PcMachine pc(2 * 1024 * 1024);
             Bytes bytes = isoFixture(readGuest());
             bytes[kDataLba * kSector] ^= 1;
             mount(pc, std::move(bytes));
             run(pc);
             completed(pc, 'F');
             need(pc.disk_reads == 1, "Corruption check must follow a real disk read");
         }},
        {"extended read at EOF fails without touching guest RAM", [] {
             PcMachine pc(2 * 1024 * 1024);
             const auto fixture = mount(pc, isoFixture(readGuest(kVolumeSectors)));
             pc.cpu.memory.fill(kBuffer, 0xa5, kSector);
             run(pc);
             completed(pc, 'F');
             need(pc.disk_reads == 0 && fixture.reads->empty(), "EOF must not invoke disk reader");
             need(pc.cpu.read16(kDap + 2) == 0 && (pc.cpu.read16(kSavedDiskAx) >> 8) == 4,
                  "EOF must report zero sectors and BIOS disk error 04");
             need(ram_bytes(pc, kBuffer, kSector) == Bytes(kSector, 0xa5),
                  "Failed disk read changed guest RAM");
         }},
        {"failed host read is atomic from the guest perspective", [] {
             PcMachine pc(2 * 1024 * 1024);
             const auto fixture = mount(pc, isoFixture(readGuest()), true);
             pc.cpu.memory.fill(kBuffer, 0xa5, kSector);
             run(pc);
             completed(pc, 'F');
             need(fixture.reads->size() == 1 && pc.disk_reads == 0,
                  "A failed callback must not count as a successful disk read");
             need(pc.cpu.read16(kDap + 2) == 0, "Failed read must report zero transferred sectors");
             need(ram_bytes(pc, kBuffer, kSector) == Bytes(kSector, 0xa5),
                  "Partial host data leaked into guest RAM");
         }},
        {"64-bit out-of-range LBA does not wrap", [] {
             PcMachine pc(2 * 1024 * 1024);
             const auto fixture = mount(pc, isoFixture(readGuest(
                 std::numeric_limits<std::uint64_t>::max())));
             run(pc);
             completed(pc, 'F');
             need(fixture.reads->empty() && pc.disk_reads == 0,
                  "Overflowing LBA must fail before calling the reader");
             need((pc.cpu.read16(kSavedDiskAx) >> 8) == 4,
                  "Overflowing LBA must return a BIOS disk error");
         }},
        {"unsupported opcode is an explicit CPU fault", [] {
             PcMachine pc(2 * 1024 * 1024);
             const auto fixture = mount(pc, isoFixture(Bytes{0x0f, 0x31, 0xf4})); // RDTSC.
             run(pc);
             need(pc.cpu.fault.has_value(), "Unsupported RDTSC was silently accepted");
             need(pc.cpu.fault->code == cosmic_pc::FaultCode::UnsupportedOpcode,
                  "Unsupported instruction needs an explicit UnsupportedOpcode fault");
             need(pc.cpu.fault->opcode == 0x0f31 && pc.cpu.fault->cs == 0 &&
                      pc.cpu.fault->eip == 0x7c00 && pc.cpu.eip == 0x7c00,
                  "Fault must preserve the guest opcode and instruction address");
             need(pc.status() == "fault" && !pc.pending.empty() && !pc.cpu.fault->reason.empty(),
                  "Fault diagnostic is not exposed by PcMachine");
             need(fixture.reads->empty() && pc.cpu.instructions == 0,
                  "Faulting instruction must not fabricate completed work");
         }},
        {"El Torito status packet describes the mounted CD", [] {
             PcMachine pc(2 * 1024 * 1024);
             // mov si,7d40h; mov ax,4b01h; int 13h; hlt.
             const auto fixture = mount(pc, isoFixture(Bytes{
                 0xbe, 0x40, 0x7d, 0xb8, 0x01, 0x4b, 0xcd, 0x13, 0xf4}));
             run(pc);
             need(!pc.cpu.fault && pc.cpu.halted && !pc.cpu.flag(Cpu::CF),
                  "El Torito AH=4Bh query failed");
             need(pc.cpu.read8(kStatusPacket) == 0x13 &&
                      pc.cpu.read8(kStatusPacket + 1) == 0 &&
                      pc.cpu.read8(kStatusPacket + 2) == 0xe0,
                  "El Torito status header, media type or drive is wrong");
             need(pc.cpu.read32(kStatusPacket + 4) == kBootLba &&
                      pc.cpu.read16(kStatusPacket + 12) == 0x07c0 &&
                      pc.cpu.read16(kStatusPacket + 14) == 1,
                  "El Torito status boot image coordinates are wrong");
             need(fixture.reads->empty() && pc.disk_reads == 0,
                  "Status query should not read disk payload");
         }},
        {"extended read rejects an unmounted drive", [] {
             PcMachine pc(2 * 1024 * 1024);
             Bytes guest = readGuest();
             // Direct AH=42h call using the existing DAP but DL=80h, not E0h.
             const Bytes code{0xbe, 0x00, 0x7d, 0xba, 0x80, 0x00,
                              0xb8, 0x00, 0x42, 0xcd, 0x13, 0xf4};
             std::copy(code.begin(), code.end(), guest.begin());
             const auto fixture = mount(pc, isoFixture(guest));
             run(pc);
             need(!pc.cpu.fault && pc.cpu.halted && pc.cpu.flag(Cpu::CF),
                  "Unmounted drive must return BIOS CF, not a CPU fault");
             need(pc.cpu.reg8(4) == 1 && fixture.reads->empty() && pc.disk_reads == 0,
                  "Wrong-drive read did not return BIOS error 01 without I/O");
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
    std::cout << "PC ISO: " << cases.size() << "/" << cases.size()
              << " cases passed (" << checks << " assertions).\n";
    return 0;
}
