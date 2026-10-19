#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <stdexcept>
#include <string>
#include <vector>

namespace cosmic_pc {

inline constexpr std::uint64_t kLongModeResult = 0x2345678922222223ULL;
inline constexpr std::uint32_t kLongModeResultAddress = 0x9000;

// Original boot-sector program, assembled here from x86 instruction encodings.
// No host mode switch or injected result: the guest executes LGDT, MOV CR0,
// CR4.PAE, CR3, RDMSR/WRMSR(EFER.LME), paging and a far jump into CS.L=1.
// Its own compare/branch checks both the arithmetic and the 64-bit RAM read.
// This is a small CPU milestone, not a Windows loader or compatibility test.
inline std::vector<std::uint8_t> long_mode_boot_disk() {
  constexpr std::uint32_t origin = 0x7c00;
  std::vector<std::uint8_t> disk(512, 0);
  std::size_t at = 0;
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    if (bytes.size() > 510 - at) throw std::logic_error("x64 boot sector overflow");
    for (const auto b : bytes) disk[at++] = b;
  };
  const auto number = [&](std::uint64_t value, unsigned bytes) {
    if (bytes > 510 - at) throw std::logic_error("x64 boot sector overflow");
    for (unsigned i = 0; i < bytes; ++i) disk[at++] = std::uint8_t(value >> (i * 8));
  };
  const auto patch = [&](std::size_t offset, std::uint64_t value, unsigned bytes) {
    if (offset + bytes > 510) throw std::logic_error("x64 guest relocation overflow");
    for (unsigned i = 0; i < bytes; ++i) disk[offset + i] = std::uint8_t(value >> (i * 8));
  };
  const auto rel8 = [&](std::size_t offset, std::size_t target) {
    const auto displacement = std::int64_t(target) - std::int64_t(offset + 1);
    if (displacement < -128 || displacement > 127) throw std::logic_error("x64 guest branch out of range");
    disk[offset] = std::uint8_t(displacement);
  };

  // REAL16. Establish known data/stack segments; request the BIOS A20 service.
  emit({0xfa, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0});
  emit({0xbc, 0x00, 0x70, 0xb8, 0x01, 0x24, 0xcd, 0x15});
  emit({0x66, 0x0f, 0x01, 0x16}); const auto gdtr_pointer = at; number(0, 2);
  emit({0x0f, 0x20, 0xc0, 0x0c, 0x01, 0x0f, 0x22, 0xc0}); // CR0.PE
  emit({0x66, 0xea}); const auto protected_pointer = at; number(0, 4); number(0x08, 2);

  const auto protected_entry = at;
  // PROTECTED32. Flat ring-0 GDT, a 32-bit stack, and three cleared page tables.
  emit({0x66, 0xb8, 0x10, 0x00, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0});
  emit({0xbc}); number(0x7000, 4);
  emit({0xfc, 0x31, 0xc0, 0xbf}); number(0x1000, 4);
  emit({0xb9}); number(3 * 4096 / 4, 4); emit({0xf3, 0xab});
  const auto store32 = [&](std::uint32_t address, std::uint32_t value) {
    emit({0xc7, 0x05}); number(address, 4); number(value, 4);
  };
  store32(0x1000, 0x2003); // PML4[0] -> PDPT, present/read-write.
  store32(0x2000, 0x3003); // PDPT[0] -> PD.
  store32(0x3000, 0x0083); // PD[0]: 2-MiB identity page, present/read-write/PS.
  emit({0xb8}); number(0x20, 4); emit({0x0f, 0x22, 0xe0}); // CR4.PAE
  emit({0xb8}); number(0x1000, 4); emit({0x0f, 0x22, 0xd8}); // CR3=PML4
  emit({0xb9}); number(0xc0000080U, 4); emit({0x0f, 0x32, 0x0d});
  number(0x100, 4); emit({0x0f, 0x30}); // EFER.LME via RDMSR/WRMSR.
  emit({0x0f, 0x20, 0xc0, 0x0d}); number(0x80010000U, 4);
  emit({0x0f, 0x22, 0xc0}); // CR0.PG + WP; next instruction stays identity mapped.
  emit({0xea}); const auto long_pointer = at; number(0, 4); number(0x18, 2);

  const auto long_entry = at;
  // LONG64. Constants exceed 32 bits; R8/R9 exercise the REX extensions.
  emit({0x48, 0xb8}); number(0x1234567800000001ULL, 8);
  emit({0x49, 0xb8}); number(0x1111111122222222ULL, 8);
  emit({0x4c, 0x01, 0xc0}); // ADD RAX,R8.
  emit({0x49, 0xba}); number(kLongModeResult, 8);
  emit({0x4c, 0x39, 0xd0, 0x75}); const auto arithmetic_failure = at; number(0, 1);
  emit({0x48, 0x89, 0x04, 0x25}); number(kLongModeResultAddress, 4);
  emit({0x4c, 0x8b, 0x0c, 0x25}); number(kLongModeResultAddress, 4); // MOV R9,[absolute].
  emit({0x49, 0x39, 0xc1, 0x75}); const auto memory_failure = at; number(0, 1);

  const std::string message = "COSMIC PC x64: OK";
  // Only show success after the guest's checks. Preserve the arithmetic RAX.
  emit({0x50, 0xbe}); const auto text_pointer = at; number(0, 4);
  emit({0xbf}); number(0xb8000, 4); emit({0xb9}); number(message.size(), 4);
  const auto print_loop = at;
  emit({0xac, 0xb4, 0x0a, 0x66, 0xab, 0xe2}); const auto print_back = at; number(0, 1);
  emit({0x58, 0xf4}); // POP RAX; HLT, with R9 retaining the RAM result.
  const auto failed = at; emit({0x0f, 0x0b}); // Failed self-check is an explicit UD2.

  while (at % 8) emit({0});
  const auto gdt = at;
  number(0, 8);
  number(0x00cf9a000000ffffULL, 8); // 08: base0,4GiB,code32,ring0,present.
  number(0x00cf92000000ffffULL, 8); // 10: base0,4GiB,data32,ring0,present.
  number(0x00af9a000000ffffULL, 8); // 18: code64, L=1,D=0,ring0,present.
  const auto gdtr = at; number(31, 2); number(origin + gdt, 4);
  const auto text = at;
  for (unsigned char c : message) emit({c});

  patch(gdtr_pointer, origin + gdtr, 2);
  patch(protected_pointer, origin + protected_entry, 4);
  patch(long_pointer, origin + long_entry, 4);
  patch(text_pointer, origin + text, 4);
  rel8(arithmetic_failure, failed); rel8(memory_failure, failed); rel8(print_back, print_loop);
  disk[510] = 0x55; disk[511] = 0xaa;
  return disk;
}

} // namespace cosmic_pc
