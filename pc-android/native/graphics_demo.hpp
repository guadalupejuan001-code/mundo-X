#pragma once

#include "pc_video.hpp"
#include <initializer_list>
#include <vector>

namespace cosmic_pc {

inline constexpr std::uint32_t kGraphicsDemoResultAddress = 0x9200;
inline constexpr std::uint32_t kGraphicsDemoResult = 0x5049584c;
inline constexpr std::array<std::uint32_t, 8> kGraphicsDemoPalette{{
  0x00ff4040, 0x00ffaa33, 0x00ffe66d, 0x0032e875,
  0x0037d5ff, 0x00477bff, 0x00a374ff, 0x00f5f7fa
}};

// Original boot-sector guest, built from documented x86 encodings. It queries
// the VBE mode, requests its LFB, executes LGDT/MOV CR0/far JMP and writes real
// BGRX pixels with REP STOSD. The host supplies no demo pixels. Only ~6.6% of an
// 8K image is touched, so demonstrating the high addresses does not require
// filling every zero page or blocking the emulator with 33 million stores.
inline std::vector<std::uint8_t> graphics_boot_disk(unsigned width=1280,
                                                   unsigned height=720) {
  const auto* mode = PcVideo::find_resolution(width, height);
  if (!mode) throw std::invalid_argument("Resolución de la prueba VBE no disponible");
  constexpr std::uint32_t origin = 0x7c00;
  const std::uint32_t pitch = width * 4;
  const std::uint32_t marker_width = width / 32, marker_height = height / 32;
  const std::uint32_t band_y = height / 2 - height / 32;
  std::vector<std::uint8_t> disk(512, 0);
  std::size_t at = 0;
  std::vector<std::size_t> rect_calls;
  const auto number = [&](std::uint64_t value, unsigned bytes) {
    if (at > 510 || bytes > 510 - at) throw std::logic_error("graphics guest boot sector overflow");
    for (unsigned i = 0; i < bytes; ++i) disk[at++] = std::uint8_t(value >> (8 * i));
  };
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    for (const auto value : bytes) number(value, 1);
  };
  const auto patch = [&](std::size_t offset, std::uint64_t value, unsigned bytes) {
    if (offset > 510 || bytes > 510 - offset) throw std::logic_error("graphics guest relocation overflow");
    for (unsigned i = 0; i < bytes; ++i) disk[offset + i] = std::uint8_t(value >> (i * 8));
  };
  const auto rel8 = [&](std::size_t offset, std::size_t target) {
    const auto distance = std::int64_t(target) - std::int64_t(offset + 1);
    if (distance < -128 || distance > 127) throw std::logic_error("graphics guest branch overflow");
    patch(offset, std::uint8_t(distance), 1);
  };
  const auto call_rect = [&] {
    emit({0xe8}); rect_calls.push_back(at); number(0, 4);
  };
  const auto destination = [&](std::uint32_t offset) {
    emit({0x89, 0xef}); // MOV EDI,EBP (PhysBasePtr returned by BIOS).
    if (offset) { emit({0x81, 0xc7}); number(offset, 4); }
  };
  const auto rectangle = [&](std::uint32_t x, std::uint32_t y, std::uint32_t color) {
    destination(y * pitch + x * 4);
    emit({0xb8}); number(color, 4);
    emit({0xb9}); number(marker_width, 4);
    emit({0xba}); number(marker_height, 4);
    call_rect();
  };

  // REAL16. ES:DI=0000:9000 is a full 256-byte ModeInfoBlock.
  emit({0xfa,0x31,0xc0,0x8e,0xd8,0x8e,0xc0,0x8e,0xd0,0xbc,0x00,0x70,0xfc});
  emit({0xbf,0x00,0x90,0xb9}); number(mode->id, 2);
  emit({0xb8,0x01,0x4f,0xcd,0x10,0x3d,0x4f,0x00,0x74,0x02,0x0f,0x0b});
  emit({0xbb}); number(mode->id | 0x4000, 2);
  emit({0xb8,0x02,0x4f,0xcd,0x10,0x3d,0x4f,0x00,0x74,0x02,0x0f,0x0b});
  emit({0x66,0x0f,0x01,0x16}); const auto gdtr_reference = at; number(0, 2);
  emit({0x0f,0x20,0xc0,0x0c,0x01,0x0f,0x22,0xc0,0x66,0xea});
  const auto protected_reference = at; number(0, 4); number(8, 2);

  const auto protected_entry = at;
  // PROTECTED32. A real flat GDT gives the CPU access to the >1-GiB LFB.
  emit({0x66,0xb8,0x10,0x00,0x8e,0xd8,0x8e,0xc0,0x8e,0xd0,0xbc});
  number(0x7000, 4);
  emit({0x8b,0x2d}); number(0x9028, 4); // MOV EBP,[ModeInfoBlock.PhysBasePtr].
  rectangle(0, 0, kGraphicsDemoPalette[0]);
  rectangle(width - marker_width, 0, kGraphicsDemoPalette[3]);
  rectangle(0, height - marker_height, kGraphicsDemoPalette[5]);
  rectangle(width - marker_width, height - marker_height, kGraphicsDemoPalette[7]);

  // Eight color bars. Each inner REP is interruptible by PcMachine's budget.
  destination(band_y * pitch);
  emit({0xbe}); const auto palette_reference = at; number(0, 4);
  emit({0xbb}); number(kGraphicsDemoPalette.size(), 4);
  const auto bars = at;
  emit({0xad,0x57,0xb9}); number(width / 8, 4); // LODSD; PUSH EDI; MOV ECX,bar width.
  emit({0xba}); number(height / 16, 4);
  call_rect();
  emit({0x5f,0x81,0xc7}); number((width / 8) * 4, 4);
  emit({0x4b,0x75}); const auto bars_back = at; number(0, 1);

  // The guest reads back the extreme and middle pixels before publishing PASS.
  const auto check_pixel = [&](std::uint32_t offset, std::uint32_t color) {
    emit({0x81,0xbd}); number(offset, 4); number(color, 4);
    emit({0x74,0x02,0x0f,0x0b}); // JE continues; mismatch executes UD2.
  };
  check_pixel(0, kGraphicsDemoPalette[0]);
  check_pixel((height - 1) * pitch + (width - 1) * 4, kGraphicsDemoPalette[7]);
  check_pixel((height / 2) * pitch + (width / 2) * 4, kGraphicsDemoPalette[4]);
  emit({0xc7,0x05}); number(kGraphicsDemoResultAddress, 4); number(kGraphicsDemoResult, 4);
  emit({0xf4});

  const auto rect = at;
  // EDI=start, EAX=color, ECX=width, EDX=height. Preserve the bar loop's EBX.
  emit({0x53,0x89,0xcb});
  const auto row = at;
  emit({0x57,0x89,0xd9,0xf3,0xab,0x5f,0x81,0xc7}); number(pitch, 4);
  emit({0x4a,0x75}); const auto row_back = at; number(0, 1);
  emit({0x5b,0xc3});

  while (at % 8) emit({0});
  const auto gdt = at;
  number(0, 8);
  number(0x00cf9a000000ffffULL, 8); // 08: flat 4-GiB code32.
  number(0x00cf92000000ffffULL, 8); // 10: flat 4-GiB data32.
  const auto gdtr = at; number(23, 2); number(origin + gdt, 4);
  const auto palette = at;
  for (const auto color : kGraphicsDemoPalette) number(color, 4);

  patch(gdtr_reference, origin + gdtr, 2);
  patch(protected_reference, origin + protected_entry, 4);
  patch(palette_reference, origin + palette, 4);
  for (const auto offset : rect_calls)
    patch(offset, std::uint32_t(std::int64_t(rect) - std::int64_t(offset + 4)), 4);
  rel8(bars_back, bars); rel8(row_back, row);
  disk[510] = 0x55; disk[511] = 0xaa;
  return disk;
}

} // namespace cosmic_pc
