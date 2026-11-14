#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"

#include <cstdlib>
#include <initializer_list>
#include <iostream>
#include <set>
#include <string>
#include <vector>

using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

namespace {
unsigned checks = 0;
void check(bool value, const std::string& reason) {
  ++checks; if (!value) throw std::runtime_error(reason);
}
void one(Cpu& cpu) {
  if (cpu.step() == StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
}
void run(Cpu& cpu, unsigned budget = 10000) {
  for (unsigned i = 0; i < budget; ++i) {
    one(cpu); if (cpu.halted) return;
  }
  throw std::runtime_error("guest exceeded execution budget");
}
void program(Cpu& cpu, std::initializer_list<std::uint8_t> code, std::uint64_t address = 0xa000) {
  cpu.eip = address; cpu.halted = false; cpu.fault.reset();
  for (const auto b : code) cpu.memory[address++] = b;
}
std::set<std::string> boot(Cpu& cpu) {
  const auto disk = cosmic_pc::long_mode_boot_disk();
  check(disk.size() == 512 && disk[510] == 0x55 && disk[511] == 0xaa, "real boot-sector signature");
  cpu.memory.reset_zero(); cpu.reset();
  cpu.memory.write_block(0x7c00, disk.data(), disk.size());
  cpu.interrupt_hook = [](Cpu& c, std::uint8_t vector) {
    if (vector != 0x15 || c.reg16(Cpu::EAX) != 0x2401) c.fail("unexpected BIOS service in x64 guest test");
    c.a20_enabled = true; c.set_reg8(4, 0); c.set_flag(Cpu::CF, false); return true;
  };
  std::set<std::string> modes{cpu.mode_name()};
  for (unsigned i = 0; i < 10000; ++i) {
    one(cpu); modes.insert(cpu.mode_name());
    if (cpu.halted) return modes;
  }
  throw std::runtime_error("real-to-long guest did not halt");
}
void expect_fault(Cpu& cpu, FaultCode code, const std::string& reason) {
  const auto ip = cpu.eip;
  check(cpu.step() == StepResult::Faulted && cpu.fault && cpu.fault->code == code,
        reason + (cpu.fault ? ": " + cpu.fault->describe() : ": no fault"));
  check(cpu.eip == ip && cpu.fault->eip == ip, "fault preserves instruction start IP");
}

void boot_and_register_tests() {
  Cpu cpu(std::size_t{2} << 30, std::size_t{1} << 30);
  const auto modes = boot(cpu);
  for (const auto* name : {"real16", "protected16", "protected32", "compat32", "long64"})
    check(modes.count(name) != 0, std::string("guest traversed ") + name);
  check(cpu.is_long_mode() && cpu.halted, "long-mode HLT");
  check(cpu.regs[Cpu::EAX] == cosmic_pc::kLongModeResult && cpu.regs[Cpu::R9] == cosmic_pc::kLongModeResult, "64-bit result in RAX and R9");
  check(cpu.read64(cosmic_pc::kLongModeResultAddress) == cosmic_pc::kLongModeResult, "64-bit result in guest RAM");
  check((cpu.cr0 & 0x80010001U) == 0x80010001U && (cpu.cr4 & 0x20) && cpu.cr3 == 0x1000 && (cpu.efer & 0x500) == 0x500,
        "guest control registers activate PAE/LME/LMA/PG/WP");
  check(cpu.read64(0x1000) == 0x2023 && cpu.read64(0x2000) == 0x3023 && cpu.read64(0x3000) == 0xe3,
        "page walker updates accessed and dirty bits");
  std::string screen;
  for (unsigned i = 0; i < 17; ++i) screen += char(cpu.read8(0xb8000 + i * 2));
  check(screen == "COSMIC PC x64: OK", "VGA success is written by guest instructions");
  check(cpu.memory.size() == (std::uint64_t{2} << 30) && cpu.memory.allocated_bytes() < 1024 * 1024,
        "2-GiB logical RAM does not commit 2 GiB for this guest");

  program(cpu, {0x41,0xb8,0x78,0x56,0x34,0x12, 0x66,0x41,0xb8,0xcd,0xab, 0xf4});
  cpu.regs[Cpu::R8] = UINT64_MAX; run(cpu);
  check(cpu.regs[Cpu::R8] == 0x1234abcd, "R8D clears high32; R8W preserves all other bits");
  program(cpu, {0x40,0xb4,0x22, 0xb4,0x33, 0x41,0xb4,0x44, 0xf4});
  cpu.regs[Cpu::EAX] = 0x123456789abcdef0ULL; cpu.regs[Cpu::ESP] = 0x7000; run(cpu);
  check(cpu.regs[Cpu::ESP] == 0x7022 && cpu.regs[Cpu::EAX] == 0x123456789abc33f0ULL && cpu.regs[Cpu::R12] == 0x44,
        "REX byte-register mapping distinguishes SPL/AH/R12B");
  program(cpu, {0x48,0x66,0xb8,0x34,0x12, 0xf4}); cpu.regs[Cpu::EAX] = 0xabcdef0000000000ULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0xabcdef0000001234ULL, "legacy prefix after REX discards REX");
  program(cpu, {0x66,0x48,0xb8,0x88,0x77,0x66,0x55,0x44,0x33,0x22,0x11, 0xf4}); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x1122334455667788ULL, "REX.W after 66 selects64");
  program(cpu, {0x90,0xf3,0x90,0xf4}); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x1122334455667788ULL, "NOP/PAUSE preserve high RAX");
  program(cpu, {0x49,0x90,0xf4}); cpu.regs[Cpu::R8] = 0xdeadbeef87654321ULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0xdeadbeef87654321ULL && cpu.regs[Cpu::R8] == 0x1122334455667788ULL, "REX.B opcode90 exchanges RAX and R8");
  program(cpu, {0x0f,0x01,0xe0,0xf4}); cpu.regs[Cpu::EAX] = UINT64_MAX; run(cpu);
  check(cpu.regs[Cpu::EAX] == cpu.cr0, "SMSW r32 in long mode copies all32 CR0 bits and clears high32");
  program(cpu, {0x48,0x0f,0x01,0xe0,0xf4}); cpu.regs[Cpu::EAX] = UINT64_MAX; run(cpu);
  check(cpu.regs[Cpu::EAX] == cpu.cr0, "SMSW r64 in long mode preserves PG in the returned value");
}

void multibyte_nops() {
  Cpu cpu(8U << 20); boot(cpu);
  constexpr std::uint64_t observed_rip = 0x2a5a73aa;
  constexpr std::uint64_t physical = 0x400000 + (observed_rip & 0x1fffff);
  cpu.write64(0x3000 + 8 * (observed_rip >> 21), 0x400083);
  // The report ended after 66 0F 1F. Supply a SIB and disp32, with a
  // non-canonical effective address to catch any operand data access.
  program(cpu, {0x66,0x0f,0x1f,0x84,0x0b,0,0,0,0,0x0f,0x1f,0xc0,0xf4}, physical);
  cpu.eip = observed_rip;
  cpu.regs[Cpu::EBX] = 0x0000800000000000ULL;
  cpu.regs[Cpu::ECX] = 1;
  cpu.eflags = 2 | Cpu::CF | Cpu::ZF | Cpu::IF | Cpu::OF;
  const auto registers = cpu.regs;
  const auto flags = cpu.eflags;
  one(cpu);
  check(cpu.eip == observed_rip + 9 && cpu.last_decoded_bytes().size() == 9,
        "66 0F 1F consumes the full SIB and displacement at the reported RIP");
  one(cpu);
  check(cpu.eip == observed_rip + 12 && cpu.regs == registers && cpu.eflags == flags,
        "memory-form and register-form NOPs preserve registers and flags");
  one(cpu);
  check(cpu.halted, "execution continues at the instruction after both NOPs");

  cpu.idtr = {0, 0};
  program(cpu, {0xf0,0x0f,0x1f,0x03});
  expect_fault(cpu, FaultCode::InvalidInstruction, "LOCK multi-byte NOP raises #UD");
  program(cpu, {0x66,0x0f,0x1f}, 0x1ffffd);
  expect_fault(cpu, FaultCode::PageFault, "multi-byte NOP must fetch its ModR/M");
  check(cpu.cr2 == 0x200000, "instruction fetch across the unmapped code boundary sets CR2");

  cpu.reset(0, 0xa000);
  program(cpu, {0x0f,0x1f,0x86,0x34,0x12,0xf4});
  one(cpu);
  check(cpu.eip == 0xa005, "multi-byte NOP consumes the 16-bit addressing displacement");
}

void addressing_stack_and_alu_tests() {
  Cpu cpu; boot(cpu);
  // C7: effective RIP must follow displacement AND immediate, not ModRM alone.
  program(cpu, {0x48,0xc7,0x05,0x05,0,0,0,0xff,0xff,0xff,0xff,0xf4});
  run(cpu); check(cpu.read64(0xa010) == UINT64_MAX, "RIP-relative sign-extended imm32 store");
  program(cpu, {0x48,0x83,0x05,0x08,0,0,0,0x01,0xf4});
  cpu.write64(0xa010, 0xffffffffULL); run(cpu);
  check(cpu.read64(0xa010) == 0x100000000ULL, "RIP-relative ALU imm8 includes immediate in RIP base");
  program(cpu, {0x48,0xf7,0x05,0x05,0,0,0,0xff,0xff,0xff,0xff,0xf4}); run(cpu);
  check(!cpu.flag(Cpu::ZF), "RIP-relative TEST imm32 sign extends and uses final RIP");
  program(cpu, {0x4b,0x8b,0x44,0xa5,0x08,0xf4});
  cpu.regs[Cpu::R13] = 0xb000; cpu.regs[Cpu::R12] = 3; cpu.write64(0xb014, 0x8765432101234567ULL); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x8765432101234567ULL, "REX.X/B SIB R13+R12*4+disp8");
  program(cpu, {0x67,0x48,0x8b,0x03,0xf4});
  cpu.regs[Cpu::EBX] = 0xffffffff0000b014ULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x8765432101234567ULL, "address-size32 truncates base without truncating operand64");
  program(cpu, {0x48,0x8d,0x05,0x04,0,0,0,0xf4}); run(cpu);
  check(cpu.regs[Cpu::EAX] == 0xa00b, "LEA RIP-relative uses next RIP");

  program(cpu, {0x41,0x50,0x41,0x59,0x6a,0xff,0x58,0xf4});
  cpu.regs[Cpu::ESP] = 0x7000; cpu.regs[Cpu::R8] = 0xabcdef0198765432ULL; run(cpu);
  check(cpu.regs[Cpu::R9] == 0xabcdef0198765432ULL && cpu.regs[Cpu::EAX] == UINT64_MAX && cpu.regs[Cpu::ESP] == 0x7000,
        "default64 extended PUSH/POP and sign-extended PUSH imm8");
  program(cpu, {0x66,0x48,0x50,0x66,0x48,0x59,0xf4});
  cpu.regs[Cpu::EAX] = 0x1122334455667788ULL; one(cpu);
  check(cpu.regs[Cpu::ESP] == 0x6ff8 && cpu.read64(0x6ff8) == 0x1122334455667788ULL, "66 REX.W PUSH uses64-bit operand, confirmed against native x86-64");
  run(cpu); check(cpu.regs[Cpu::ECX] == 0x1122334455667788ULL && cpu.regs[Cpu::ESP] == 0x7000, "66 REX.W POP restores64 bits");
  program(cpu, {0x48,0x66,0x50,0x48,0x66,0x59,0xf4}); one(cpu);
  check(cpu.regs[Cpu::ESP] == 0x6ffe && cpu.read16(0x6ffe) == 0x7788, "legacy66 afterREX cancelsREX and PUSH uses16 bits");
  run(cpu); check(cpu.regs[Cpu::ESP] == 0x7000, "16-bit PUSH/POP balance in long mode");
  program(cpu, {0xe8,0x01,0,0,0,0xf4,0x48,0xff,0xc0,0xc3}); cpu.regs[Cpu::EAX] = 0xffffffffULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x100000000ULL && cpu.regs[Cpu::ESP] == 0x7000, "CALL rel32 / RET use 64-bit return slots");
  program(cpu, {0x48,0x83,0xc0,0xff,0xf4}); cpu.regs[Cpu::EAX] = 0x8000000000000000ULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x7fffffffffffffffULL && cpu.flag(Cpu::CF) && cpu.flag(Cpu::OF), "ADD64 sign-extended imm8 CF/OF");
  program(cpu, {0x48,0xc1,0xe0,0x20,0xf4}); cpu.regs[Cpu::EAX] = 1; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x100000000ULL, "64-bit shift count is masked to6 bits");
  for (const std::uint8_t modrm : {0xe8,0xd0}) {
    program(cpu, {0xc1,modrm,0x00,0xf4}); cpu.regs[Cpu::EAX] = 0x1122334455667788ULL; cpu.eflags = 0xad7;
    run(cpu);
    check(cpu.regs[Cpu::EAX] == 0x55667788 && cpu.eflags == 0xad7,
          "SHR/RCL EAX count0 zeroextends destination and preserves flags, confirmed against native x86-64");
  }
  program(cpu, {0x48,0x6b,0xc0,0xfd,0xf4}); cpu.regs[Cpu::EAX] = 0x100000000ULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0xfffffffd00000000ULL && !cpu.flag(Cpu::OF), "two-operand64 IMUL signed immediate");
  program(cpu, {0x48,0x6b,0xc0,0x02,0xf4}); cpu.regs[Cpu::EAX] = 0x4000000000000000ULL; run(cpu);
  check(cpu.regs[Cpu::EAX] == 0x8000000000000000ULL && cpu.flag(Cpu::OF) && cpu.flag(Cpu::CF), "IMUL64 overflow uses defined host arithmetic");
}

void enter_leave_long_mode_tests() {
  Cpu cpu; boot(cpu);
  // A high canonical alias of the identity mapping detects accidental 32-bit
  // truncation by 67, and tests full RSP/RBP values beyond physical RAM size.
  constexpr auto high = 0xffff800000000000ULL;
  cpu.write64(0x1000 + 256 * 8, 0x2003);
  cpu.fs_base = 0x100000; cpu.gs_base = 0x120000;
  constexpr auto flags = 2U | Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::IF | Cpu::DF | Cpu::OF;
  struct Encoding { std::vector<std::uint8_t> prefix; unsigned bytes; };
  const Encoding encodings[] = {
    {{}, 8}, {{0x67}, 8}, {{0x66}, 2}, {{0x66, 0x67}, 2},
    {{0x66, 0x48}, 8}, {{0x48, 0x66}, 2}, {{0x64}, 8}, {{0x65, 0x67, 0x66}, 2}, {{0x4f}, 8}
  };
  for (const auto& encoding : encodings) for (unsigned raw = 0; raw < 256; ++raw) {
    const auto bytes = encoding.bytes;
    auto code = encoding.prefix;
    code.insert(code.end(), {0xc8, 0x23, 0x01, std::uint8_t(raw)});
    code.insert(code.end(), encoding.prefix.begin(), encoding.prefix.end()); code.push_back(0xc9);
    cpu.memory.write_block(0xa000, code.data(), code.size()); cpu.eip = 0xa000; cpu.halted = false;
    cpu.regs[Cpu::ESP] = high + 0x190800; cpu.regs[Cpu::EBP] = high + 0x191000;
    cpu.regs[Cpu::R13] = 0x123456789abcdef0ULL; cpu.eflags = flags; const auto saved = cpu.regs;
    for (unsigned i = 1; i < 32; ++i) {
      if (bytes == 2) cpu.write16(0x191000 - i * bytes, std::uint16_t(0x5300 + i));
      else cpu.write64(0x191000 - i * bytes, 0xabcdef0198765300ULL + i);
    }
    const auto level = raw & 31U, slots = level == 0 ? 1 : level + 1;
    const auto frame = bytes == 2 ? 0x7feULL : high + 0x1907f8;
    const auto expected_bp = bytes == 2 ? high + 0x1907fe : frame;
    const auto expected_sp = high + 0x190800 - slots * bytes - 0x123;
    cpu.memory[expected_sp - high] = 0xa5; cpu.memory[0x190800] = 0x5a; one(cpu);
    check(cpu.regs[Cpu::EBP] == expected_bp && cpu.regs[Cpu::ESP] == expected_sp && cpu.eflags == flags,
          "long-mode ENTER uses operand-sized frame slots and full64 stack addressing independent of 67/FS/GS");
    check((bytes == 2 ? cpu.read16(0x190800 - bytes) : cpu.read64(0x190800 - bytes)) ==
          (bytes == 2 ? 0x1000ULL : high + 0x191000), "long-mode ENTER saves precisely BP or RBP");
    for (unsigned i = 1; i < level; ++i)
      check((bytes == 2 ? cpu.read16(0x190800 - (i + 1) * bytes) : cpu.read64(0x190800 - (i + 1) * bytes)) ==
            (bytes == 2 ? 0x5300ULL + i : 0xabcdef0198765300ULL + i), "long-mode ENTER copies each enclosing frame via translated SS addresses");
    if (level != 0)
      check((bytes == 2 ? cpu.read16(0x190800 - slots * bytes) : cpu.read64(0x190800 - slots * bytes)) == frame,
            "long-mode ENTER pushes the correct operand-sized FrameTemp at every nesting level");
    check(cpu.memory[expected_sp - high] == 0xa5 && cpu.memory[0x190800] == 0x5a,
          "long-mode ENTER probes local storage without overwriting it or neighboring data");
    one(cpu); check(cpu.regs == saved && cpu.eflags == flags,
                    "matching long-mode LEAVE restores full RSP and the operand-sized frame pointer without altering flags");
  }

  // Confirmed against native x86-64: the chain walker is full RBP even for
  // 66 ENTER; its borrow must not modify the high bits preserved by BP output.
  program(cpu, {0x66, 0x67, 0xc8, 0, 0, 2});
  cpu.regs[Cpu::ESP] = high + 0x180800; cpu.regs[Cpu::EBP] = high + 0x190000;
  cpu.write16(0x18fffe, 0xbeef); one(cpu);
  check(cpu.read16(0x1807fc) == 0xbeef && cpu.regs[Cpu::EBP] == high + 0x1907fe,
        "66 ENTER chain crossing 64KiB preserves the original high RBP bits and reads with all64 address bits");
  program(cpu, {0x66, 0xc8, 4, 0, 0, 0x66, 0xc9});
  cpu.regs[Cpu::ESP] = high + 0x180004; cpu.regs[Cpu::EBP] = high + 0x181000; one(cpu);
  check(cpu.regs[Cpu::ESP] == high + 0x17fffe && cpu.regs[Cpu::EBP] == high + 0x180002,
        "66 ENTER local allocation borrows across 64KiB using full RSP");
  one(cpu); check(cpu.regs[Cpu::ESP] == high + 0x180004 && cpu.regs[Cpu::EBP] == high + 0x181000,
                  "66 LEAVE restores a64-bit stack pointer even when locals crossed64KiB");
}

void enter_leave_paging_fault_tests() {
  Cpu cpu; boot(cpu);
  for (unsigned i = 0; i < 512; ++i) cpu.write64(0x4000 + i * 8, std::uint64_t(i) * 4096 | 3);
  cpu.write64(0x3000, 0x4003);
  constexpr auto flags = 2U | Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::IF | Cpu::DF | Cpu::OF;
  const auto expect_rollback = [&](std::initializer_list<std::uint8_t> code, std::uint64_t sp, std::uint64_t bp,
                                   FaultCode fault, std::uint64_t address, std::uint32_t error,
                                   std::uint64_t saved_slot) {
    program(cpu, code); cpu.regs[Cpu::ESP] = sp; cpu.regs[Cpu::EBP] = bp; cpu.eflags = flags;
    cpu.write64(saved_slot, 0x123456789abcdef0ULL);
    const auto saved = cpu.regs; const auto retired = cpu.instructions;
    expect_fault(cpu, fault, "ENTER/LEAVE reports the architectural stack access fault");
    check(cpu.regs == saved && cpu.eflags == flags && cpu.instructions == retired &&
          cpu.read64(saved_slot) == 0x123456789abcdef0ULL,
          "ENTER/LEAVE fault rolls back registers, flags, retired count and all earlier frame stores");
    if (fault == FaultCode::PageFault)
      check(cpu.cr2 == address && cpu.fault->linear_address == address && cpu.fault->error_code == error,
            "stack page fault records the exact linear address and read/write/present error bits");
  };

  // Stack page C000 is physically E000; page B000 is the reserved-local page.
  cpu.write64(0x4000 + 12 * 8, 0xe003);
  cpu.write64(0x4000 + 11 * 8, 0);
  expect_rollback({0xc8, 0x00, 0x10, 0}, 0xc010, 0x9000, FaultCode::PageFault, 0xb008, 2, 0xe008);
  cpu.write64(0x4000 + 11 * 8, 0xd001);
  expect_rollback({0xc8, 0x00, 0x10, 0}, 0xc010, 0x9000, FaultCode::PageFault, 0xb008, 3, 0xe008);
  cpu.write64(0x4000 + 11 * 8, 0);
  expect_rollback({0xc8, 0, 0, 2}, 0xc010, 0xb008, FaultCode::PageFault, 0xb000, 0, 0xe008);
  expect_rollback({0xc8, 0, 0, 1}, 0xc008, 0x9000, FaultCode::PageFault, 0xbff8, 2, 0xe000);
  expect_rollback({0x66, 0x67, 0xc9}, 0xc010, 0xb000, FaultCode::PageFault, 0xb000, 0, 0xe008);

  // The final probe checks a complete slot, even when only its trailing bytes
  // cross into an absent page. Both64-bit and66 forms are native-confirmed.
  cpu.write64(0x4000 + 11 * 8, 0xd003); cpu.write64(0x4000 + 12 * 8, 0);
  expect_rollback({0xc8, 0x0c, 0x20, 0}, 0xe010, 0x9000, FaultCode::PageFault, 0xc000, 2, 0xe008);
  expect_rollback({0x66, 0xc8, 0x0f, 0x20, 0}, 0xe010, 0x9000, FaultCode::PageFault, 0xc000, 2, 0xe00e);

  // A high canonical stack with a one-byte reservation enters the address gap.
  cpu.write64(0x1000 + 256 * 8, 0x2003);
  expect_rollback({0xc8, 1, 0, 0}, 0xffff800000000008ULL, 0x9000, FaultCode::StackFault, 0, 0, 0);

  cpu.write64(0x4000 + 11 * 8, 0);
  program(cpu, {0xc8, 0, 0}, 0xaffd); cpu.regs[Cpu::ESP] = 0xe010;
  const auto saved = cpu.regs; cpu.write64(0xe008, 0xabcdef1234567890ULL);
  expect_fault(cpu, FaultCode::PageFault, "ENTER immediate fetch crossing an absent code page");
  check(cpu.regs == saved && cpu.cr2 == 0xb000 && cpu.fault->error_code == 16 && cpu.fault->bytes.size() == 3 &&
        cpu.read64(0xe008) == 0xabcdef1234567890ULL, "ENTER finishes decoding before performing any stack access");
}

void double_shift_long_mode_tests() {
  Cpu cpu; boot(cpu);
  constexpr std::uint32_t status = Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF | Cpu::SF | Cpu::OF;
  for (const bool left : {false, true}) for (const bool immediate : {false, true}) {
    const auto op = std::uint8_t((left ? 0xa4 : 0xac) + (immediate ? 0 : 1));
    program(cpu, {0x48, 0x0f, op, 0xd8, 4});
    cpu.regs[Cpu::EAX] = 0x123456789abcdef0ULL; cpu.regs[Cpu::EBX] = 0xfedcba9876543211ULL;
    cpu.regs[Cpu::ECX] = 0xffffffffffff0004ULL; one(cpu);
    check(cpu.regs[Cpu::EAX] == (left ? 0x23456789abcdef0fULL : 0x1123456789abcdefULL), "SHLD/SHRD64 fixed result");
    check(cpu.regs[Cpu::EBX] == 0xfedcba9876543211ULL && cpu.regs[Cpu::ECX] == 0xffffffffffff0004ULL &&
          cpu.eip == 0xa000 + (immediate ? 5U : 4U), "SHLD/SHRD64 source/count and instruction length");
    for (const std::uint8_t raw : {63, 127, 255}) {
      program(cpu, {0x48, 0x0f, op, 0xd8, raw});
      cpu.regs[Cpu::EAX] = 0x8000000000000001ULL; cpu.regs[Cpu::EBX] = 3;
      cpu.set_reg8(Cpu::ECX, raw); one(cpu);
      check(cpu.regs[Cpu::EAX] == (left ? 0x8000000000000001ULL : 7ULL) && !cpu.flag(Cpu::CF),
            "SHLD/SHRD64 count63 and six-bit count mask");
    }
    for (const std::uint8_t raw : {0, 64, 128}) {
      program(cpu, {0x48, 0x0f, op, 0xd8, raw});
      cpu.regs[Cpu::EAX] = 0x123456789abcdef0ULL; cpu.regs[Cpu::EBX] = UINT64_MAX;
      cpu.set_reg8(Cpu::ECX, raw); cpu.eflags = 2 | status; one(cpu);
      check(cpu.regs[Cpu::EAX] == 0x123456789abcdef0ULL && cpu.eflags == (2 | status),
            "SHLD/SHRD64 masked zero count retains all 64 destination bits and flags");
    }
    for (const std::uint8_t raw : {1, 65}) {
      program(cpu, {0x48, 0x0f, op, 0xd8, raw});
      cpu.regs[Cpu::EAX] = left ? 0x8000000000000001ULL : 1ULL;
      cpu.regs[Cpu::EBX] = left ? 0ULL : 1ULL; cpu.set_reg8(Cpu::ECX, raw); cpu.eflags = 2;
      one(cpu);
      check(cpu.regs[Cpu::EAX] == (left ? 2ULL : 0x8000000000000000ULL) && cpu.flag(Cpu::CF) && cpu.flag(Cpu::OF),
            "SHLD/SHRD64 one-bit carry and overflow");
    }
    // REX.R/B select R9 as source and R8 as destination at every operand size.
    for (const unsigned bytes : {2U, 4U, 8U}) {
      if (bytes == 2) program(cpu, {0x66, 0x45, 0x0f, op, 0xc8, 0});
      else program(cpu, {std::uint8_t(bytes == 8 ? 0x4d : 0x45), 0x0f, op, 0xc8, 0});
      cpu.regs[Cpu::R8] = 0x123456789abcdef0ULL; cpu.regs[Cpu::R9] = UINT64_MAX;
      cpu.set_reg8(Cpu::ECX, 0); cpu.eflags = 2 | status; one(cpu);
      check(cpu.regs[Cpu::R8] == (bytes == 4 ? 0x9abcdef0ULL : 0x123456789abcdef0ULL) &&
            cpu.regs[Cpu::R9] == UINT64_MAX && cpu.eflags == (2 | status),
            "SHLD/SHRD extended register count0 honors r16 preservation / r32 zero-extension / r64");
    }
  }

  program(cpu, {0x4d, 0x0f, 0xad, 0xc8}); // SHRD R8,R9,CL
  cpu.regs[Cpu::R8] = 0x123456789abcdef0ULL; cpu.regs[Cpu::R9] = 0xfedcba9876543211ULL;
  cpu.set_reg8(Cpu::ECX, 4); one(cpu);
  check(cpu.regs[Cpu::R8] == 0x1123456789abcdefULL && cpu.regs[Cpu::R9] == 0xfedcba9876543211ULL,
        "SHRD extended REX.R/B source and destination operate on all 64 bits");
  program(cpu, {0x66, 0x48, 0x0f, 0xa4, 0xd8, 4});
  cpu.regs[Cpu::EAX] = 0x123456789abcdef0ULL; cpu.regs[Cpu::EBX] = 0xfedcba9876543211ULL; one(cpu);
  check(cpu.regs[Cpu::EAX] == 0x23456789abcdef0fULL, "SHLD REX.W following 66 selects64");
  program(cpu, {0x48, 0x66, 0x0f, 0xa4, 0xd8, 4});
  cpu.regs[Cpu::EAX] = 0x123456789abc1234ULL; cpu.regs[Cpu::EBX] = 0x5678; one(cpu);
  check(cpu.regs[Cpu::EAX] == 0x123456789abc2345ULL, "SHLD legacy66 after REX cancels REX.W");

  // RIP-relative address must be resolved after the immediate count is read.
  program(cpu, {0x48, 0x0f, 0xac, 0x1d, 7, 0, 0, 0, 4});
  cpu.regs[Cpu::EBX] = 0xfedcba9876543211ULL; cpu.write64(0xa010, 0x123456789abcdef0ULL);
  cpu.memory[0xa00f] = 0xa5; cpu.memory[0xa018] = 0x5a; one(cpu);
  check(cpu.read64(0xa010) == 0x1123456789abcdefULL && cpu.memory[0xa00f] == 0xa5 && cpu.memory[0xa018] == 0x5a,
        "SHRD RIP-relative memory64 includes imm8 in final RIP and writes eight bytes");
  program(cpu, {0x64, 0x67, 0x4f, 0x0f, 0xa4, 0x5c, 0x91, 8, 4});
  cpu.fs_base = 0x1000; cpu.regs[Cpu::R9] = 0xffffffff0000b000ULL; cpu.regs[Cpu::R10] = 3;
  cpu.regs[Cpu::R11] = 0xfedcba9876543211ULL; cpu.write64(0xc014, 0x123456789abcdef0ULL); one(cpu);
  check(cpu.read64(0xc014) == 0x23456789abcdef0fULL && cpu.regs[Cpu::R11] == 0xfedcba9876543211ULL,
        "SHLD memory64 applies FS, address32, extended SIB and REX.R source independently");

  // A read-modify-write must respect write protection even with count zero.
  cpu.fs_base = 0;
  for (unsigned i = 0; i < 512; ++i) cpu.write64(0x4000 + i * 8, std::uint64_t(i) * 4096 | 3);
  cpu.write64(0x3000, 0x4003); cpu.write64(0x4000 + 11 * 8, 0xd001);
  cpu.regs[Cpu::EBX] = 0xb000; cpu.regs[Cpu::EDX] = 0x1234;
  cpu.write64(0xd000, 0xabcdef1234567890ULL);
  for (const std::uint8_t count : {0, 1}) {
    program(cpu, {0x48, 0x0f, 0xac, 0x13, count}); // SHRD [RBX],RDX,imm8
    cpu.eflags = 2 | status; const auto saved = cpu.regs;
    expect_fault(cpu, FaultCode::PageFault, "SHRD observes supervisor write protection for every count");
    check(cpu.fault->error_code == 3 && cpu.regs == saved && cpu.eflags == (2 | status) &&
          cpu.read64(0xd000) == 0xabcdef1234567890ULL, "faulting SHRD rolls back memory and flags");
  }
}

void signed_multiply_boundaries() {
  Cpu cpu; boot(cpu);
  // Fixed mathematical products keep the INT64_MIN and overflow regressions
  // executable on MSVC and other hosts where the native x86 oracle is skipped.
  struct Case { std::uint64_t a, b, low; bool overflow; };
  const Case cases[] = {
    {0, 0, 0, false},
    {0, 0x8000000000000000ULL, 0, false},
    {0x8000000000000000ULL, 0, 0, false},
    {0x8000000000000000ULL, 1, 0x8000000000000000ULL, false},
    {1, 0x8000000000000000ULL, 0x8000000000000000ULL, false},
    {0x8000000000000000ULL, 0xffffffffffffffffULL, 0x8000000000000000ULL, true},
    {0xffffffffffffffffULL, 0x8000000000000000ULL, 0x8000000000000000ULL, true},
    {0x7fffffffffffffffULL, 1, 0x7fffffffffffffffULL, false},
    {0x7fffffffffffffffULL, 2, 0xfffffffffffffffeULL, true},
    {0x7fffffffffffffffULL, 0xffffffffffffffffULL, 0x8000000000000001ULL, false},
    {0x8000000000000001ULL, 0xffffffffffffffffULL, 0x7fffffffffffffffULL, false},
    {0x8000000000000000ULL, 2, 0, true},
    {0x8000000000000000ULL, 0x8000000000000000ULL, 0, true},
    {0xffffffffffffffffULL, 0xffffffffffffffffULL, 1, false},
    {0x00000000ffffffffULL, 0x00000000ffffffffULL, 0xfffffffe00000001ULL, true},
    {0xffffffff00000000ULL, 0x0000000100000000ULL, 0, true},
    {0x00000000b504f333ULL, 0x00000000b504f333ULL, 0x7ffffffe9ea1dc29ULL, false},
    {0x00000000b504f334ULL, 0x00000000b504f334ULL, 0x8000000008abc290ULL, true},
    {0xffffffff4afb0cccULL, 0x00000000b504f334ULL, 0x7ffffffff7543d70ULL, true},
    {3, 0x2aaaaaaaaaaaaaaaULL, 0x7ffffffffffffffeULL, false},
    {3, 0x2aaaaaaaaaaaaaabULL, 0x8000000000000001ULL, true},
  };
  for (const auto& value : cases) for (const bool initial_overflow : {false, true}) {
    program(cpu, {0x48, 0x0f, 0xaf, 0xc3}); // IMUL RAX,RBX
    cpu.regs[Cpu::EAX] = value.a;
    cpu.regs[Cpu::EBX] = value.b;
    cpu.eflags = 2 | Cpu::IF | Cpu::DF |
                 (initial_overflow ? Cpu::CF | Cpu::OF : 0);
    one(cpu);
    check(cpu.regs[Cpu::EAX] == value.low, "IMUL64 boundary low product");
    check(cpu.flag(Cpu::CF) == value.overflow && cpu.flag(Cpu::OF) == value.overflow,
          "IMUL64 boundary CF/OF");
    check(cpu.regs[Cpu::EBX] == value.b, "IMUL64 boundary source preserved");
    check(cpu.flag(Cpu::IF) && cpu.flag(Cpu::DF), "IMUL64 preserves control flags");
  }
}

void instruction_page_fetch() {
  Cpu cpu; boot(cpu);
  cpu.idtr = {0, 0};
  for (unsigned i = 0; i < 512; ++i) cpu.write64(0x4000 + i * 8, std::uint64_t(i) * 4096 | 3);
  cpu.write64(0x3000, 0x4003);
  cpu.write64(0x4000 + 11 * 8, 0xd003);
  cpu.write8(0xd000, 0xc0);
  program(cpu, {0x0f,0x1f}, 0xaffe);
  cpu.page_walks = cpu.instruction_page_hits = 0;
  one(cpu);
  check(cpu.eip == 0xb001 && cpu.page_walks == 2 && cpu.instruction_page_hits == 1,
        "a cross-page NOP must follow two mappings without fetching extra bytes");

  cpu.write64(0x4000 + 11 * 8, 0);
  cpu.eip = 0xaffe;
  expect_fault(cpu, FaultCode::PageFault, "the same NOP must see a guest page-table write");
  check(cpu.cr2 == 0xb000 && cpu.fault->error_code == 16,
        "missing continuation page must keep the exact execute fault");

  program(cpu, {0x66,0x0f,0x1f,0xc0,0xf4});
  cpu.mmio_claim_hook = [](std::uint64_t address) { return address == 0xa001; };
  cpu.mmio_range_claim_hook = [](std::uint64_t address, std::size_t count) {
    return address <= 0xa001 && 0xa001 - address < count;
  };
  unsigned reads = 0;
  cpu.mmio_read_hook = [&](std::uint64_t) -> std::optional<std::uint8_t> {
    ++reads;
    return 0x0f;
  };
  cpu.page_walks = cpu.instruction_page_hits = 0;
  one(cpu);
  check(cpu.eip == 0xa004 && reads == 1 && cpu.page_walks == 4 && !cpu.instruction_page_hits,
        "instruction fetch on a device page must use exact byte callbacks");
}

void scanned_boot_loop() {
  constexpr std::uint64_t loop = 0x2a52a9a4;
  constexpr std::uint64_t physical = 0x400000 + (loop & 0x1fffff);
  constexpr std::uint64_t source = 0xcfe0, dest = 0x100d8;
  constexpr unsigned offset = 211;
  const auto setup = [&](Cpu& cpu, bool accelerated) {
    boot(cpu);
    cpu.enable_page_translation_cache(accelerated);
    cpu.write64(0x3000 + 8 * (loop >> 21), 0x400083);
    program(cpu, {0x44,0x8d,0x40,0x01,0x48,0xff,0xc2,0x41,0x8b,0xc0,
                  0x41,0x89,0x81,0xd8,0x2e,0,0,0x80,0x3a,0xe8,0x75,0xea,0xf4}, physical);
    cpu.memory.fill(source, 0x8c, offset + 8);
    cpu.write8(source + offset, 0xe8);
    cpu.eip = loop;
    cpu.regs[Cpu::EAX] = 0xff000000ffffff5eULL; // Exercise wrap and zero-extension.
    cpu.regs[Cpu::EDX] = source - 1;
    cpu.regs[Cpu::R9] = dest - 0x2ed8;
    cpu.set_flag(Cpu::DF, true);
  };
  Cpu scalar(8U << 20), fast(8U << 20);
  setup(scalar, false);
  setup(fast, true);
  unsigned accelerated_steps = 0;
  for (unsigned remaining = 1500; remaining && !fast.halted; --remaining) {
    const auto count = fast.try_scan_loop_batch(103);
    if (count) {
      check(count % 6 == 0 && count <= 103, "scan must respect the device budget");
      accelerated_steps += count;
    } else if (fast.step() == StepResult::Faulted) {
      throw std::runtime_error(fast.fault->describe());
    }
  }
  run(scalar, 1500);
  check(accelerated_steps > 1000 && fast.halted && fast.instructions == scalar.instructions &&
        fast.regs == scalar.regs && fast.eflags == scalar.eflags && fast.eip == scalar.eip &&
        fast.read32(dest) == scalar.read32(dest) && fast.regs[Cpu::EDX] == source + offset,
        "scan must preserve the reported loop's byte, flags, registers and RIP");
  setup(fast, true);
  for (unsigned i = 0; i < 6; ++i) one(fast);
  const auto before = fast.instructions;
  fast.write8(physical + 19, 0x42);
  check(fast.try_scan_loop_batch(96) == 0 && fast.instructions == before,
        "rewritten instruction bytes must invalidate the scan pattern");
}

void cached_page_translations() {
  Cpu cpu; boot(cpu);
  cpu.idtr = {0, 0};
  cpu.enable_page_translation_cache();
  for (unsigned i = 0; i < 512; ++i) cpu.write64(0x4000 + i * 8, std::uint64_t(i) * 4096 | 3);
  cpu.write64(0x3000, 0x4003);
  constexpr auto pte = 0x4058;
  cpu.write64(pte, 0xd003);
  cpu.write64(0xd000, 0x34);
  cpu.write64(0xe000, 0x56);
  program(cpu, {0x48,0x8b,0x03,0xeb,0xfb}); // MOV RAX,[RBX]; JMP to MOV
  cpu.regs[Cpu::EBX] = 0xb000;
  for (unsigned i = 0; i < 4; ++i) one(cpu);
  check(cpu.regs[Cpu::EAX] == 0x34 && cpu.page_translation_hits >= 3,
        "repeated x64 accesses must share validated translations");
  cpu.memory[pte + 1] = 0xe0; // Direct guest-memory write through the host API.
  one(cpu);
  check(cpu.regs[Cpu::EAX] == 0x56 && (cpu.read64(pte) & 0x20),
        "page-table changes through any RAM alias must invalidate the cache and restore A");
  one(cpu);
  program(cpu, {0x48,0x89,0x03,0xeb,0xfb});
  cpu.regs[Cpu::EAX] = 0x78;
  one(cpu);
  check(cpu.read64(0xe000) == 0x78 && (cpu.read64(pte) & 0x60) == 0x60,
        "the first write through a cached read mapping must set D");
  one(cpu);
  cpu.write64(pte, 0xe021);
  expect_fault(cpu, FaultCode::PageFault, "cached writes must respect a newly read-only PTE");
  check(cpu.fault->error_code == 3 && cpu.read64(0xe000) == 0x78, "cached write fault or rollback");

  cpu.write64(pte, 0xe003);
  program(cpu, {0x90,0xeb,0xfd});
  for (unsigned i = 0; i < 4; ++i) one(cpu);
  cpu.efer |= 0x800;
  cpu.write64(0x4050, 0x800000000000a003ULL);
  expect_fault(cpu, FaultCode::PageFault, "cached instruction fetch must respect NX");
  check(cpu.fault->error_code == 17 && cpu.cr2 == 0xa000, "cached NX fault context");
}

void paged_rep_batch() {
  Cpu scalar, batched;
  const auto fixture = [](Cpu& cpu, bool enable, unsigned opcode, bool backward) {
    boot(cpu);
    cpu.enable_page_translation_cache(enable);
    program(cpu, {0xf3,std::uint8_t(opcode),0xf4});
    cpu.regs[Cpu::EAX] = 0x96;
    cpu.regs[Cpu::ESI] = backward ? 0xc020 : 0xbfe0;
    cpu.regs[Cpu::EDI] = backward ? 0xe020 : 0xdfe0;
    cpu.regs[Cpu::ECX] = 123;
    cpu.set_flag(Cpu::DF, backward);
    for (unsigned i = 0; i < 320; ++i)
      cpu.write8(0xbf60 + i, std::uint8_t(i * 7));
  };
  for (const auto op : {0xaaU, 0xa4U}) {
    for (const auto backward : {false, true}) {
      fixture(scalar, false, op, backward);
      fixture(batched, true, op, backward);
      const auto step = batched.instructions;
      unsigned batches = 0;
      for (unsigned i = 0; i < 124 && !batched.halted; ++i) {
        const auto result = batched.try_rep_batch(17);
        if (!result) one(batched);
        else {
          ++batches;
          check(result <= 17, "paged REP exceeded the granted device budget");
        }
      }
      run(scalar, 130);
      check(batched.halted && batches && batched.instructions == scalar.instructions &&
            batched.instructions == step + 124 && batched.regs == scalar.regs &&
            batched.eflags == scalar.eflags && batched.eip == scalar.eip,
            "paged REP must retire the equivalent guest steps and state");
      std::array<std::uint8_t, 512> expected, actual;
      scalar.memory.read_block(0xdf00, expected.data(), expected.size());
      batched.memory.read_block(0xdf00, actual.data(), actual.size());
      check(actual == expected, "paged REP must write exactly the scalar bytes in both directions");
    }
  }

  fixture(batched, true, 0xaa, false);
  batched.idtr = {0, 0};
  for (unsigned i = 0; i < 512; ++i) batched.write64(0x4000 + i * 8, std::uint64_t(i) * 4096 | 3);
  batched.write64(0x3000, 0x4003);
  batched.write64(0x4070, 0);
  one(batched);
  check(batched.try_rep_batch(128) == 31 && batched.regs[Cpu::EDI] == 0xe000,
        "paged REP must stop at the end of the mapped destination page");
  check(batched.try_rep_batch(128) == 0, "batch must leave the absent page to the scalar fault path");
  expect_fault(batched, FaultCode::PageFault, "REP STOS exact next-page fault");
  check(batched.cr2 == 0xe000 && batched.fault->error_code == 2 &&
            batched.regs[Cpu::ECX] == 91 && batched.read8(0xdfff) == 0x96,
        "fault leaves the committed REP prefix and remaining count intact");

  fixture(batched, true, 0xa4, false);
  batched.write64(0x3000, 0x4003);
  batched.write64(0x4050, 0xa023); // Instruction bytes.
  batched.write64(0x4058, 0xb023); // Source mapping, modified by the destination.
  batched.write64(0x4068, 0x4063); // Destination aliases the page table.
  batched.regs[Cpu::ESI] = 0xb000;
  batched.regs[Cpu::EDI] = 0xd058;
  one(batched);
  check(batched.try_rep_batch(64) == 0, "a paged REP must not batch through its own table");

  fixture(batched, true, 0xaa, false);
  batched.memory.attach_window(0xd000, 1, [](std::uint64_t) { return std::uint8_t{0}; },
                              [](std::uint64_t, std::uint8_t) {});
  one(batched);
  check(batched.try_rep_batch(124) == 0,
        "a page containing Ether memory must retain precise scalar callbacks");
}

void paging_and_fault_tests() {
  Cpu cpu; boot(cpu);
  // Replace the large identity page by a genuine 4-KiB page table. Data virtual
  // B000 maps to physical D000, so a physical-memory bypass would fail this test.
  for (unsigned i = 0; i < 512; ++i) cpu.write64(0x4000 + i * 8, std::uint64_t(i) * 4096 | 3);
  cpu.write64(0x3000, 0x4003); cpu.write64(0x4000 + 11 * 8, 0xd003);
  cpu.write64(0xb000, 0x1111111111111111ULL); cpu.write64(0xd000, 0x876543219abcdef0ULL);
  program(cpu, {0x48,0x8b,0x03,0xf4}); cpu.regs[Cpu::EBX] = 0xb000;
  cpu.page_walks=0; one(cpu);
  check(cpu.regs[Cpu::EAX] == 0x876543219abcdef0ULL, "four-level4KiB translation reads remapped data");
  check(cpu.page_walks==2,
        "MOV r64,[mem] must walk one instruction page and one data page");
  one(cpu);
  program(cpu, {0x48,0x89,0x03,0xf4}); cpu.regs[Cpu::EAX] = 0xfedcba9876543210ULL;
  cpu.page_walks=0; one(cpu);
  check(cpu.page_walks==2,
        "MOV [mem],r64 must walk one instruction page and one writable data page");
  one(cpu);
  check(cpu.read64(0xd000) == 0xfedcba9876543210ULL && cpu.read64(0xb000) == 0x1111111111111111ULL,
        "four-level4KiB translation writes the remapped physical frame");
  check((cpu.read64(0x4000 + 11 * 8) & 0x60) == 0x60, "PTE A/D bits");

  cpu.write64(0x4000 + 11 * 8, 0);
  program(cpu, {0x48,0x8b,0x03}); const auto saved = cpu.regs;
  expect_fault(cpu, FaultCode::PageFault, "not-present data page");
  check(cpu.regs == saved && cpu.cr2 == 0xb000 && cpu.fault->linear_address == 0xb000 && cpu.fault->error_code == 0,
        "read page-fault diagnostics and register rollback");
  cpu.write64(0x4000 + 11 * 8, 0xd001);
  program(cpu, {0x48,0x89,0x03});
  expect_fault(cpu, FaultCode::PageFault, "CR0.WP denies supervisor write");
  check(cpu.fault->error_code == 3 && cpu.read64(0xd000) == 0xfedcba9876543210ULL, "write page fault reports P/W and preserves memory");
  // A write spanning present / absent pages must not partially overwrite RAM.
  cpu.write64(0x4000 + 11 * 8, 0xd003); cpu.write64(0x4000 + 12 * 8, 0);
  cpu.write32(0xdffc, 0x11223344); cpu.regs[Cpu::EBX] = 0xbffc;
  program(cpu, {0x48,0x89,0x03}); expect_fault(cpu, FaultCode::PageFault, "cross-page write validates both pages");
  check(cpu.read32(0xdffc) == 0x11223344 && cpu.cr2 == 0xc000, "cross-page write rollback preserves first page");

  // Enable NXE with guest WRMSR, then deny execution on the code PTE.
  program(cpu, {0x0f,0x30,0xf4}); cpu.regs[Cpu::ECX] = 0xc0000080U; cpu.regs[Cpu::EDX] = 0; cpu.regs[Cpu::EAX] = 0xd00; run(cpu);
  cpu.write64(0x4000 + 10 * 8, 0x800000000000a003ULL);
  program(cpu, {0xf4}); expect_fault(cpu, FaultCode::PageFault, "NX instruction fetch");
  check(cpu.fault->error_code == 17 && cpu.cr2 == 0xa000 && cpu.fault->bytes.empty(), "NX fetch fault contains CR2 and instruction-fetch error bit");
  cpu.write64(0x4000 + 10 * 8, 0xa003);
  program(cpu, {0x48,0x8b,0x03}); cpu.regs[Cpu::EBX] = 0x0000800000000000ULL;
  expect_fault(cpu, FaultCode::ProtectionFault, "non-canonical address");

  program(cpu, {0xcd,0x10}); bool called = false;
  cpu.idtr = {0, 0}; // no guest gate is configured: must not invent a BIOS service.
  cpu.interrupt_hook = [&](Cpu&,std::uint8_t) { called = true; return true; };
  expect_fault(cpu, FaultCode::ProtectionFault, "long-mode INT without a valid IDT stops before BIOS hook");
  check(cpu.fault->reason.find("triple fault") != std::string::npos,
        "invalid IDT must produce explicit unhandled/triple-fault diagnostic");
  check(!called, "BIOS is never substituted for a long-mode IDT");
  // CPUID is now implemented, but must declare only the emulated features.
  program(cpu, {0x0f,0xa2});
  cpu.regs[Cpu::EAX]=0;cpu.regs[Cpu::ECX]=0;
  const auto before_cpuid=cpu.instructions;
  check(cpu.step()==StepResult::Executed && cpu.regs[Cpu::EAX]==7 &&
        cpu.instructions==before_cpuid+1 && !cpu.fault,
        "modeled CPUID executes correctly in long mode");
  program(cpu, {0x0f,0xa2});
  cpu.regs[Cpu::EAX]=1;
  constexpr std::uint32_t features=(1U<<15)|(1U<<23)|(1U<<24)|(1U<<25)|(1U<<26);
  check(cpu.step()==StepResult::Executed && (cpu.regs[Cpu::EDX]&(features|1U))==features,
        "long-mode CPUID must expose CMOV and the verified SIMD features and keep FPU clear");
  program(cpu, {0x0f,0x05}); expect_fault(cpu, FaultCode::InvalidInstruction, "SYSCALL requires EFER.SCE");
  program(cpu, {0x0f,0x22,0xe0}); cpu.regs[Cpu::EAX] = 0;
  expect_fault(cpu, FaultCode::ProtectionFault, "PAE cannot be cleared while LMA");
  check(cpu.cr4 == 0x20, "failed control-register write preserves CR4");
  // CPUID now modifies EDX. Clear its high WRMSR half so this test
  // isolates the intended EFER.LME transition rather than CPUID side effects.
  program(cpu, {0x0f,0x30}); cpu.regs[Cpu::ECX] = 0xc0000080U;
  cpu.regs[Cpu::EAX] = 0xc00; cpu.regs[Cpu::EDX] = 0;
  expect_fault(cpu, FaultCode::ProtectionFault, "LME cannot change while paging");
  check(cpu.efer == 0xd00, "failed WRMSR preserves EFER");
}

void conditional_moves() {
  Cpu cpu; boot(cpu);
  constexpr std::uint64_t original = 0x8877665544332211ULL;
  constexpr std::uint64_t source = 0xffeeddccbbaa9988ULL;
  for (unsigned flags = 0; flags < 32; ++flags) {
    const bool o = flags & 1, c = flags & 2, z = flags & 4, s = flags & 8, p = flags & 16;
    const std::uint32_t eflags = 2 | Cpu::AF | Cpu::DF |
        (o ? Cpu::OF : 0U) | (c ? Cpu::CF : 0U) | (z ? Cpu::ZF : 0U) |
        (s ? Cpu::SF : 0U) | (p ? Cpu::PF : 0U);
    const bool matches[]{o, !o, c, !c, z, !z, c || z, !(c || z),
                         s, !s, p, !p, s != o, s == o, z || s != o, !z && s == o};
    for (unsigned condition = 0; condition < 16; ++condition) {
      for (unsigned width : {2U, 4U, 8U}) {
        const auto op = std::uint8_t(0x40 + condition);
        if (width == 2) program(cpu, {0x66, 0x45, 0x0f, op, 0xc1});
        else program(cpu, {std::uint8_t(width == 8 ? 0x4d : 0x45), 0x0f, op, 0xc1});
        cpu.regs[Cpu::R8] = original;
        cpu.regs[Cpu::R9] = source;
        cpu.eflags = eflags;
        one(cpu);
        const auto value = matches[condition] ? source : original;
        const auto expected = width == 2 ? (original & ~0xffffULL) | (value & 0xffffU)
                            : width == 4 ? std::uint32_t(value) : value;
        check(cpu.regs[Cpu::R8] == expected && cpu.regs[Cpu::R9] == source && cpu.eflags == eflags,
              "CMOVcc truth table, operand width, REX or flag preservation");
      }
    }
  }
  // The conditional write follows the read. Both conditions reach source #PF.
  cpu.regs[Cpu::EAX] = original; cpu.regs[Cpu::EBX] = 0x200000;
  for (const auto flags : {2U, 2U | Cpu::ZF}) {
    program(cpu, {0x0f, 0x44, 0x03}); // CMOVZ EAX,[RBX]
    cpu.eflags = flags;
    expect_fault(cpu, FaultCode::PageFault, "CMOV must read memory even when false");
    check(cpu.regs[Cpu::EAX] == original && cpu.eflags == flags && cpu.cr2 == 0x200000,
          "CMOV fault preserves destination and flags");
  }
  program(cpu, {0xf0, 0x0f, 0x44, 0x03});
  expect_fault(cpu, FaultCode::InvalidInstruction, "LOCK CMOV rejected before reading the source");
  cpu.reset(0, 0xa000);
  program(cpu, {0x0f, 0x44, 0xc3, 0x66, 0x0f, 0x44, 0xc3});
  cpu.regs[Cpu::EAX] = original; cpu.regs[Cpu::EBX] = source; cpu.eflags |= Cpu::ZF;
  one(cpu); check(cpu.regs[Cpu::EAX] == 0x8877665544339988ULL, "real-mode CMOV default is 16 bits");
  one(cpu); check(cpu.regs[Cpu::EAX] == 0xbbaa9988ULL, "real-mode CMOV with 66 is 32 bits");
}

void implicit_arithmetic64() {
  Cpu cpu; boot(cpu);
  constexpr auto maximum = UINT64_MAX;
  constexpr std::uint64_t sign = 0x8000000000000000ULL;
  constexpr std::uint32_t flags = 2 | Cpu::IF | Cpu::DF;
  struct Product { std::uint64_t a, b, high, low; bool signed_op, overflow; };
  const Product products[]{
    {maximum, maximum, maximum - 1, 1, false, true},
    {maximum, maximum, 0, 1, true, false},
    {sign, sign, sign >> 1, 0, true, true},
    {sign, 2, maximum, 0, true, true},
    {maximum, 2, maximum, maximum - 1, true, false},
    {sign + 1, maximum, 0, sign - 1, true, false},
    {0x123456789abcdef0ULL, 0xfedcba9876543211ULL, 0x121fa00ad77d7422ULL,
        0x35a1df76f0d5adf0ULL, false, true},
    {0x123456789abcdef0ULL, 0xfedcba9876543211ULL, 0xffeb49923cc09532ULL,
        0x35a1df76f0d5adf0ULL, true, true},
  };
  for (const auto& value : products) {
    program(cpu, {0x49, 0xf7, std::uint8_t(value.signed_op ? 0xe9 : 0xe1)});
    cpu.regs[Cpu::EAX] = value.a; cpu.regs[Cpu::EDX] = maximum; cpu.regs[Cpu::R9] = value.b;
    cpu.eflags = flags | (value.overflow ? 0 : Cpu::CF | Cpu::OF);
    one(cpu);
    check(cpu.regs[Cpu::EAX] == value.low && cpu.regs[Cpu::EDX] == value.high &&
          cpu.regs[Cpu::R9] == value.b && cpu.eflags == (flags | (value.overflow ? Cpu::CF | Cpu::OF : 0)),
          "MUL64/IMUL64 writes the full doubleword and defined flags");
  }
  struct Quotient { std::uint64_t high, low, divisor, quotient, remainder; bool signed_op; };
  const Quotient quotients[]{
    {1, 0, 3, 0x5555555555555555ULL, 1, false},
    {maximum - 1, maximum, maximum, maximum, maximum - 1, false},
    {sign, 0, maximum, sign, sign, false},
    {0, 17, 5, 3, 2, true},
    {maximum, maximum - 16, 5, maximum - 2, maximum - 1, true},
    {0, 17, maximum - 4, maximum - 2, 2, true},
    {maximum, maximum - 16, maximum - 4, 3, maximum - 1, true},
    {maximum, sign, 1, sign, 0, true},
    {0, sign, maximum, sign, 0, true},
    {maximum - 1, maximum - 1, maximum - 3, sign >> 1, maximum - 1, true},
    {maximum - 1, maximum, 3, 0xaaaaaaaaaaaaaaabULL, maximum - 1, true},
  };
  for (const auto& value : quotients) {
    program(cpu, {0x48, 0xf7, std::uint8_t(value.signed_op ? 0x3b : 0x33)}); // [RBX]
    cpu.regs[Cpu::EAX] = value.low; cpu.regs[Cpu::EDX] = value.high;
    cpu.regs[Cpu::EBX] = 0xa100; cpu.write64(0xa100, value.divisor);
    cpu.eflags = flags | Cpu::OF | Cpu::CF;
    one(cpu);
    check(cpu.regs[Cpu::EAX] == value.quotient && cpu.regs[Cpu::EDX] == value.remainder &&
          cpu.read64(0xa100) == value.divisor && (cpu.eflags & flags) == flags,
          "DIV64/IDIV64 produces exact quotient and signed remainder");
  }
  const Quotient invalid[]{
    {9, 0, 0, 0, 0, false}, {5, 0, 5, 0, 0, false}, {0, 1, 0, 0, 0, true},
    {sign, 0, maximum, 0, 0, true}, {maximum, sign, maximum, 0, 0, true},
    {0, sign, 1, 0, 0, true},
  };
  cpu.idtr = {0, 0};
  for (const auto& value : invalid) {
    program(cpu, {0x48, 0xf7, std::uint8_t(value.signed_op ? 0xfb : 0xf3)});
    cpu.regs[Cpu::EAX] = value.low; cpu.regs[Cpu::EDX] = value.high; cpu.regs[Cpu::EBX] = value.divisor;
    cpu.eflags = flags | Cpu::OF | Cpu::CF;
    const auto state = cpu.regs; const auto flag_state = cpu.eflags;
    const auto instructions = cpu.instructions;
    expect_fault(cpu, FaultCode::DivideError, "DIV64/IDIV64 zero and overflow raise #DE");
    check(cpu.regs == state && cpu.eflags == flag_state && cpu.instructions == instructions,
          "faulting division leaves registers, flags and step count unchanged");
  }
  program(cpu, {0x48, 0xf7, 0x33}); cpu.regs[Cpu::EBX] = 0x200000;
  expect_fault(cpu, FaultCode::PageFault, "memory-source DIV64 reads operand before #DE");
  program(cpu, {0x48, 0xf7, 0xe2}); // MUL RDX: source aliases high output.
  cpu.regs[Cpu::EAX] = maximum; cpu.regs[Cpu::EDX] = 2; one(cpu);
  check(cpu.regs[Cpu::EAX] == maximum - 1 && cpu.regs[Cpu::EDX] == 1,
        "MUL64 source alias is read before high output is written");
}
}

int main() {
  try {
    boot_and_register_tests(); multibyte_nops(); addressing_stack_and_alu_tests();
    enter_leave_long_mode_tests(); enter_leave_paging_fault_tests();
    double_shift_long_mode_tests();
    signed_multiply_boundaries(); instruction_page_fetch(); paging_and_fault_tests();
    cached_page_translations(); paged_rep_batch(); scanned_boot_loop();
    conditional_moves(); implicit_arithmetic64();
    std::cout << "PASS x86 long-mode: " << checks << " assertions; real guest transition, high64 arithmetic, paging and explicit faults\n";
    return EXIT_SUCCESS;
  } catch (const std::exception& e) {
    std::cerr << "FAIL after " << checks << " assertions: " << e.what() << '\n'; return EXIT_FAILURE;
  }
}
