#pragma once

#include "x86_cpu.hpp"
#include <array>
#include <cstring>

namespace cosmic_pc {

// The guest owns these pixels. The host presents a copy; it never substitutes
// a desktop, picture or animation for a guest framebuffer.
struct PcVideoInfo {
  bool graphics = false;
  std::uint16_t mode = 3;
  std::uint32_t width = 80, height = 25, pitch = 160;
  std::uint64_t physical_address = 0xb8000, byte_size = 4000;
};

struct PcVideoMode {
  std::uint16_t id, width, height;
};

// Original linear framebuffer device with a deliberately limited VBE 2.0
// layout/interface (4F00..4F03). No banked VGA, palette, GPU command processor,
// accelerated 3D or claim of full VBE certification is implied.
// Layout reference: VESA VBE Core Functions 3.0, pp.25-40 (2.0 fields):
// https://pdos.csail.mit.edu/6.828/2018/readings/hardware/vbe3.pdf
class PcVideo {
 public:
  static constexpr std::uint64_t kFramebufferAddress = 0x40000000ULL;
  static constexpr std::uint64_t kApertureBytes = 128ULL * 1024 * 1024;
  static constexpr std::uint64_t kBiosTablesAddress = 0xc0000;
  inline static constexpr std::array<PcVideoMode, 8> kModes{{
    {0x140, 640, 480}, {0x141, 800, 600}, {0x142, 1024, 768},
    {0x143, 1280, 720}, {0x144, 1920, 1080}, {0x145, 2560, 1440},
    {0x146, 3840, 2160}, {0x147, 7680, 4320}
  }};

  static bool available(const MemoryFabric& ram) noexcept {
    return ram.size() >= kFramebufferAddress + kApertureBytes;
  }
  static const PcVideoMode* find_mode(std::uint16_t id) noexcept {
    for (const auto& mode : kModes) if (mode.id == id) return &mode;
    return nullptr;
  }
  static const PcVideoMode* find_resolution(unsigned width, unsigned height) noexcept {
    for (const auto& mode : kModes)
      if (mode.width == width && mode.height == height) return &mode;
    return nullptr;
  }
  PcVideoInfo info() const noexcept { return info_; }
  std::uint16_t current_vbe_mode() const noexcept { return current_vbe_mode_; }
  void text_mode(std::uint16_t mode, bool preserve = false) noexcept {
    info_ = PcVideoInfo{};
    info_.mode = mode;
    current_vbe_mode_ = mode | (preserve ? 0x8000 : 0);
  }
  void reset(MemoryFabric& ram) {
    text_mode(3);
    if (!available(ram)) return;
    std::array<std::uint8_t, 256> tables{};
    for (std::size_t i = 0; i < kModes.size(); ++i) put16(tables.data(), i * 2, kModes[i].id);
    put16(tables.data(), kModes.size() * 2, 0xffff);
    const auto text = [&](std::size_t offset, const char* value) {
      std::memcpy(tables.data() + offset, value, std::strlen(value) + 1);
    };
    text(0x40, "COSMIC X PC linear framebuffer");
    text(0x80, "X");
    text(0xa0, "COSMIC X PC");
    text(0xc0, "1");
    ram.write_block(kBiosTablesAddress, tables.data(), tables.size());
  }

  // The AX status is the only modified register except BX for 4F03. Unknown
  // functions/modes return a VBE failure, without changing pixels or mode.
  void bios(Cpu& cpu) {
    const auto function = cpu.reg16(Cpu::EAX);
    if (!available(cpu.memory)) { cpu.set_reg16(Cpu::EAX, 0x014f); return; }
    if (function == 0x4f00) {
      std::uint64_t destination;
      if (!buffer_address(cpu, 4, destination)) { failure(cpu); return; }
      std::array<std::uint8_t, 4> requested{};
      cpu.memory.read_block(destination, requested.data(), requested.size());
      const bool extended = requested == std::array<std::uint8_t, 4>{{'V','B','E','2'}};
      const std::size_t bytes = extended ? 512 : 256;
      if (!buffer_address(cpu, bytes, destination)) { failure(cpu); return; }
      std::array<std::uint8_t, 512> block{};
      std::memcpy(block.data(), "VESA", 4);
      put16(block.data(), 4, 0x0200);
      put32(block.data(), 6, bios_pointer(0x40));
      put32(block.data(), 10, 2); // Not programmable through VGA I/O registers.
      put32(block.data(), 14, bios_pointer(0));
      put16(block.data(), 18, kApertureBytes / 65536);
      if (extended) {
        put16(block.data(), 20, 0x0100);
        put32(block.data(), 22, bios_pointer(0x80));
        put32(block.data(), 26, bios_pointer(0xa0));
        put32(block.data(), 30, bios_pointer(0xc0));
      }
      cpu.memory.write_block(destination, block.data(), bytes);
      success(cpu);
      return;
    }
    if (function == 0x4f01) {
      // CX is a mode number, with optional memory-model bits stripped.
      const auto* mode = find_mode(cpu.reg16(Cpu::ECX) & 0x3fff);
      std::uint64_t destination;
      if (!mode || !buffer_address(cpu, 256, destination)) { failure(cpu); return; }
      std::array<std::uint8_t, 256> block{};
      // Supported, required bit1, color, graphics, non-VGA, LFB only. TTY,
      // banked windows, stereo, multiple image pages and 3D are not advertised.
      put16(block.data(), 0, 0x00fb);
      put16(block.data(), 16, std::uint32_t(mode->width) * 4);
      put16(block.data(), 18, mode->width);
      put16(block.data(), 20, mode->height);
      block[24] = 1; // One plane.
      block[25] = 32;
      block[27] = 6; // Direct color.
      block[29] = 0; // Only the visible image is supported; no page flipping.
      block[30] = 1; // VBE's required reserved byte.
      block[31] = 8; block[32] = 16; // Red.
      block[33] = 8; block[34] = 8;  // Green.
      block[35] = 8; block[36] = 0;  // Blue.
      block[37] = 8; block[38] = 24; // Reserved X, not guest alpha.
      block[39] = 0; // Fixed RGB fields; reserved bits are not usable alpha.
      put32(block.data(), 40, kFramebufferAddress);
      cpu.memory.write_block(destination, block.data(), block.size());
      success(cpu);
      return;
    }
    if (function == 0x4f02) {
      const auto request = cpu.reg16(Cpu::EBX);
      const auto* mode = find_mode(request & 0x1ff);
      // CRTC, reserved bits and banked access have no implementation.
      if (!mode || (request & 0x3e00) || !(request & 0x4000)) { failure(cpu); return; }
      PcVideoInfo candidate;
      candidate.graphics = true;
      candidate.mode = mode->id;
      candidate.width = mode->width;
      candidate.height = mode->height;
      candidate.pitch = std::uint32_t(mode->width) * 4;
      candidate.physical_address = kFramebufferAddress;
      candidate.byte_size = std::uint64_t(candidate.pitch) * candidate.height;
      if (candidate.byte_size > kApertureBytes) { failure(cpu); return; }
      // Clear only the one reported image. Sparse zero pages remain absent;
      // preserve bit15 leaves every pixel (including X bytes) unchanged.
      if (!(request & 0x8000))
        cpu.memory.fill(kFramebufferAddress, 0, static_cast<std::size_t>(candidate.byte_size));
      info_ = candidate;
      current_vbe_mode_ = request;
      success(cpu);
      return;
    }
    if (function == 0x4f03) {
      cpu.set_reg16(Cpu::EBX, current_vbe_mode_);
      success(cpu);
      return;
    }
    failure(cpu);
  }

  void copy_bgrx(const MemoryFabric& ram, std::uint8_t* destination,
                 std::size_t capacity) const {
    if (!info_.graphics) throw std::logic_error("El dispositivo de vídeo está en modo texto");
    if (!destination || info_.byte_size > capacity)
      throw std::invalid_argument("Buffer BGRX insuficiente");
    ram.read_block(info_.physical_address, destination, static_cast<std::size_t>(info_.byte_size));
  }
  void copy_rgba(const MemoryFabric& ram, std::uint32_t x, std::uint32_t y,
                 std::uint32_t width, std::uint32_t height,
                 std::uint8_t* destination, std::size_t capacity,
                 std::size_t stride = 0) const {
    if (!info_.graphics) throw std::logic_error("El dispositivo de vídeo está en modo texto");
    if (!width || !height || x > info_.width || width > info_.width - x ||
        y > info_.height || height > info_.height - y)
      throw std::out_of_range("Tile de vídeo fuera del framebuffer");
    const std::size_t row_bytes = std::size_t(width) * 4;
    if (!stride) stride = row_bytes;
    if (stride < row_bytes || !destination || capacity < row_bytes ||
        std::uint64_t(height - 1) > (capacity - row_bytes) / stride)
      throw std::invalid_argument("Buffer RGBA insuficiente o stride inválido");
    for (std::uint32_t row = 0; row < height; ++row) {
      auto* output = destination + std::size_t(row) * stride;
      const auto address = info_.physical_address + std::uint64_t(y + row) * info_.pitch + std::uint64_t(x) * 4;
      ram.read_block(address, output, row_bytes);
      for (std::uint32_t column = 0; column < width; ++column) {
        std::swap(output[column * 4], output[column * 4 + 2]);
        output[column * 4 + 3] = 255;
      }
    }
  }

 private:
  PcVideoInfo info_;
  std::uint16_t current_vbe_mode_ = 3;
  static void success(Cpu& cpu) { cpu.set_reg16(Cpu::EAX, 0x004f); }
  static void failure(Cpu& cpu) { cpu.set_reg16(Cpu::EAX, 0x014f); }
  static constexpr std::uint32_t bios_pointer(std::uint16_t offset) {
    return (std::uint32_t(kBiosTablesAddress / 16) << 16) | offset;
  }
  static void put16(std::uint8_t* bytes, std::size_t offset, std::uint32_t value) {
    bytes[offset] = std::uint8_t(value);
    bytes[offset + 1] = std::uint8_t(value >> 8);
  }
  static void put32(std::uint8_t* bytes, std::size_t offset, std::uint64_t value) {
    for (unsigned i = 0; i < 4; ++i) bytes[offset + i] = std::uint8_t(value >> (8 * i));
  }
  static bool buffer_address(const Cpu& cpu, std::size_t bytes, std::uint64_t& address) {
    const auto offset = cpu.reg16(Cpu::EDI);
    if (bytes > 0x10000U - unsigned(offset)) return false;
    const auto raw = std::uint64_t(cpu.segments[Cpu::ES]) * 16 + offset;
    const auto mask = cpu.a20_enabled ? ~std::uint64_t{0} : ~std::uint64_t{0x100000};
    address = raw & mask;
    // Refuse a wrapped buffer as one transaction rather than corrupting IVT.
    if (bytes && ((raw + bytes - 1) & mask) != address + bytes - 1) return false;
    return address <= cpu.memory.size() && bytes <= cpu.memory.size() - address;
  }
};

} // namespace cosmic_pc
