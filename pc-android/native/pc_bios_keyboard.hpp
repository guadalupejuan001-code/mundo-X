#pragma once

#include "x86_cpu.hpp"
#include <cstdint>
#include <optional>

namespace cosmic_pc {

class PcBiosKeyboard {
 public:
  static void reset(Cpu& cpu) {
    cpu.write16(kBufferStart, kDefaultStart);
    cpu.write16(kBufferEnd, kDefaultEnd);
    cpu.write16(kHead, kDefaultStart);
    cpu.write16(kTail, kDefaultStart);
    cpu.write16(kShift, 0);
    cpu.write8(kMode, 0x10); // Enhanced keyboard.
  }

  static bool store(Cpu& cpu, std::uint16_t key) {
    const auto buffer = read_buffer(cpu);
    if (!buffer) return false;
    const auto after = buffer->next(buffer->tail);
    if (after == buffer->head) return false;
    const auto slot = kBda + buffer->tail;
    const auto pins = cpu.memory.pin_writes();
    cpu.memory.prepare_write(slot, 2);
    cpu.memory.prepare_write(kTail, 2);
    cpu.write16(slot, key);
    cpu.write16(kTail, after);
    return true;
  }

  static std::optional<std::uint16_t> read(Cpu& cpu, bool enhanced, bool consume) {
    const auto buffer = read_buffer(cpu);
    if (!buffer) return std::nullopt;
    auto head = buffer->head;
    while (head != buffer->tail) {
      const auto key = convert(cpu.read16(kBda + head), enhanced);
      if (key) {
        if (consume) cpu.write16(kHead, buffer->next(head));
        else if (head != buffer->head) cpu.write16(kHead, head);
        return key;
      }
      // Legacy reads/status can discard an enhanced-only entry. A status
      // call leaves the first compatible entry in the buffer.
      head = buffer->next(head);
    }
    if (head != buffer->head) cpu.write16(kHead, head);
    return std::nullopt;
  }

  static std::uint8_t shift_status(const Cpu& cpu) { return cpu.read8(kShift); }

  static std::uint16_t extended_shift_status(const Cpu& cpu) {
    const auto flags = cpu.read8(kExtendedShift);
    const auto right = cpu.read8(kMode) & 0x0c;
    const auto extended = (flags & 0x73) | right | ((flags & 0x04) << 5);
    return std::uint16_t((extended << 8) | shift_status(cpu));
  }

  static std::optional<std::uint16_t> host_key(
      Cpu& cpu, std::uint8_t ascii, unsigned scan, bool pressed) {
    if (update_modifier(cpu, scan, pressed) || !pressed || (!ascii && !scan))
      return std::nullopt;
    switch (scan) {
      case 0x57: return 0x8500; // F11 and F12 have different BIOS scan codes.
      case 0x58: return 0x8600;
      case 0xe01c: return std::uint16_t(0xe000 | ascii);
      case 0xe035: return 0xe02f;
      default:
        if ((scan >> 8) == 0xe0 && !ascii) ascii = 0xe0;
        return std::uint16_t((std::uint16_t(std::uint8_t(scan)) << 8) | ascii);
    }
  }

 private:
  static constexpr unsigned kBda = 0x400;
  static constexpr unsigned kShift = kBda + 0x17, kExtendedShift = kBda + 0x18;
  static constexpr unsigned kHead = kBda + 0x1a, kTail = kBda + 0x1c;
  static constexpr unsigned kBoundsOffset = 0x80, kModeOffset = 0x96;
  static constexpr unsigned kBufferStart = kBda + kBoundsOffset;
  static constexpr unsigned kBufferEnd = kBufferStart + 2;
  static constexpr unsigned kMode = kBda + kModeOffset;
  static constexpr std::uint16_t kDefaultStart = 0x1e, kDefaultEnd = 0x3e;

  struct Buffer {
    std::uint16_t start, end, head, tail;
    bool contains(std::uint16_t offset) const {
      return offset >= start && offset < end && !((offset - start) & 1);
    }
    std::uint16_t next(std::uint16_t offset) const {
      return offset + 2 == end ? start : std::uint16_t(offset + 2);
    }
  };

  static std::optional<Buffer> read_buffer(const Cpu& cpu) {
    const Buffer b{cpu.read16(kBufferStart), cpu.read16(kBufferEnd),
                   cpu.read16(kHead), cpu.read16(kTail)};
    // Validate a guest-supplied ring before using it. The range must leave
    // at least one usable slot and cannot overwrite its own bounds or mode.
    if (b.start < kDefaultStart || b.end < b.start + 4 || ((b.end - b.start) & 1) ||
        (b.start < kBoundsOffset + 4 && b.end > kBoundsOffset) ||
        (b.start <= kModeOffset && b.end > kModeOffset) ||
        !b.contains(b.head) || !b.contains(b.tail))
      return std::nullopt;
    return b;
  }

  static std::optional<std::uint16_t> convert(std::uint16_t key, bool enhanced) {
    const auto scan = key >> 8;
    const auto ascii = key & 0xff;
    if (enhanced)
      return ascii == 0xf0 && scan ? std::uint16_t(key & 0xff00) : key;

    if (scan == 0xe0) {
      if (ascii == 0x0d || ascii == 0x0a) return std::uint16_t(0x1c00 | ascii);
      if (ascii == '/') return 0x352f;
      return std::nullopt;
    }
    if (scan > 0x84 || (scan && ascii == 0xf0)) return std::nullopt;
    if (scan && ascii == 0xe0) return std::uint16_t(key & 0xff00);
    return key;
  }

  static bool update_modifier(Cpu& cpu, unsigned scan, bool pressed) {
    unsigned aggregate = 0, side = 0;
    switch (scan) {
      case 0x2a: aggregate = 0x02; break;
      case 0x36: aggregate = 0x01; break;
      case 0x1d: aggregate = 0x04; side = 0x01; break;
      case 0xe01d: aggregate = 0x04; side = 0x04; break;
      case 0x38: aggregate = 0x08; side = 0x02; break;
      case 0xe038: aggregate = 0x08; side = 0x08; break;
      default: return false;
    }
    if (side) {
      const auto address = scan > 0xff ? kMode : kExtendedShift;
      const auto previous = cpu.read8(address);
      cpu.write8(address, std::uint8_t(pressed ? previous | side : previous & ~side));
      const auto left = cpu.read8(kExtendedShift) & (aggregate >> 2);
      const auto right = cpu.read8(kMode) & aggregate;
      pressed = left || right;
    }
    const auto previous = cpu.read8(kShift);
    cpu.write8(kShift, std::uint8_t(pressed ? previous | aggregate : previous & ~aggregate));
    return true;
  }
};

}  // namespace cosmic_pc
