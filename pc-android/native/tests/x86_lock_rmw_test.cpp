#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void program(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> bytes) {
    cpu.memory.write_block(address, bytes.begin(), bytes.size());
    cpu.eip = address;
    cpu.halted = false;
    cpu.fault.reset();
}

void retire(Cpu& cpu, const char* message) {
    const auto status = cpu.step();
    check(status == StepResult::Executed,
          std::string(message) + ": " +
              (cpu.fault ? cpu.fault->describe() : "instruction did not retire"));
}

void boot_protected32(Cpu& cpu) {
    cpu.reset(0, 0x1000);
    cpu.a20_enabled = true;
    cpu.gdtr = {0x2f, 0x3000};
    cpu.write64(0x3020, 0x00cf9a000000ffffULL); // 0020h, flat protected32 code.
    cpu.write64(0x3028, 0x00cf92000000ffffULL); // 0028h, flat protected32 data.
    program(cpu, 0x1000, {
        0xb8,0x11,0x00,0x0f,0x22,0xc0,
        0x66,0xea,0x00,0x20,0x00,0x00,0x20,0x00
    });
    const std::initializer_list<std::uint8_t> setup{
        0x66,0xb8,0x28,0x00,0x8e,0xd8,0x8e,0xd0 // MOV AX,28h; MOV DS,AX; MOV SS,AX.
    };
    cpu.memory.write_block(0x2000, setup.begin(), setup.size());
    for (unsigned i = 0; i < 6; ++i) retire(cpu, "protected32 fixture");
    check(std::string(cpu.mode_name()) == "protected32" && cpu.cpl() == 0 &&
              cpu.segments[Cpu::CS] == 0x20,
          "fixture must execute in the reported protected32 code selector");
}

void replay_prefix_and_opcode() {
    Cpu cpu(8 * 1024 * 1024);
    boot_protected32(cpu);
    cpu.regs[Cpu::EAX] = 0x4b8598;
    cpu.regs[Cpu::ECX] = 1;
    cpu.regs[Cpu::EDX] = 0x170;
    cpu.regs[Cpu::EBX] = 0;
    cpu.regs[Cpu::ESP] = 0x61f0c;
    cpu.regs[Cpu::EBP] = 0x61f10;
    cpu.regs[Cpu::ESI] = 0;
    cpu.regs[Cpu::EDI] = 0x61f94;
    cpu.eflags = 2 | Cpu::CF | Cpu::OF | Cpu::ZF | Cpu::PF | Cpu::IF;
    constexpr auto ip = 0x45b043;
    constexpr auto destination = 0x4b8598;
    cpu.write32(destination, 0x80000002);
    // The phone recorded F0 09 only. 08 is a synthetic legal ModR/M:
    // LOCK OR DWORD PTR [EAX],ECX.
    program(cpu, ip, {0xf0,0x09,0x08});
    const auto regs = cpu.regs;
    const auto before = cpu.instructions;
    retire(cpu, "reported LOCK OR prefix/opcode at 0020:45B043");
    check(cpu.read32(destination) == 0x80000003 && cpu.regs == regs &&
              cpu.eip == ip + 3 && cpu.instructions == before + 1,
          "locked OR must commit one precise memory update and preserve registers");
    check(cpu.eflags == (2 | Cpu::SF | Cpu::PF | Cpu::IF),
          "locked OR lost ordinary OR flags or changed interrupt enable");
}

void locked_arithmetic_and_exchange() {
    Cpu cpu(2 * 1024 * 1024);
    boot_protected32(cpu);
    constexpr auto code = 0x2100;
    constexpr auto destination = 0x7000;
    cpu.regs[Cpu::EBX] = destination;
    cpu.regs[Cpu::ECX] = 1;
    cpu.write32(destination, 0xffffffff);
    program(cpu, code, {0xf0,0x01,0x0b}); // LOCK ADD [EBX],ECX
    retire(cpu, "locked ADD");
    check(cpu.read32(destination) == 0 &&
              cpu.eflags == (2 | Cpu::CF | Cpu::PF | Cpu::AF | Cpu::ZF),
          "LOCK ADD must use the ordinary arithmetic flags");

    program(cpu, code, {0xf0,0x83,0x13,0xff}); // LOCK ADC [EBX],-1
    retire(cpu, "locked immediate ADC");
    check(cpu.read32(destination) == 0 && cpu.flag(Cpu::CF) && cpu.flag(Cpu::ZF),
          "locked ADC must consume carry and sign-extend imm8");
    program(cpu, code, {0xf0,0xff,0x03}); // LOCK INC [EBX]
    retire(cpu, "locked INC");
    check(cpu.read32(destination) == 1 && cpu.flag(Cpu::CF) && !cpu.flag(Cpu::ZF),
          "LOCK INC must preserve the carry flag");
    program(cpu, code, {0xf0,0xf7,0x13}); // LOCK NOT [EBX]
    const auto prior_flags = cpu.eflags;
    retire(cpu, "locked NOT");
    check(cpu.read32(destination) == 0xfffffffe && cpu.eflags == prior_flags,
          "LOCK NOT must invert memory without touching flags");

    cpu.regs[Cpu::ECX] = 0x11223344;
    program(cpu, code, {0x87,0x0b}); // XCHG [EBX],ECX has implicit LOCK.
    retire(cpu, "implicit-locked XCHG");
    check(cpu.read32(destination) == 0x11223344 && cpu.regs[Cpu::ECX] == 0xfffffffe &&
              cpu.eflags == prior_flags,
          "memory XCHG must swap register and RAM without changing flags");
    cpu.regs[Cpu::ECX] = 3;
    program(cpu, code, {0xf0,0x0f,0xc1,0x0b}); // LOCK XADD [EBX],ECX
    retire(cpu, "locked XADD");
    check(cpu.read32(destination) == 0x11223347 && cpu.regs[Cpu::ECX] == 0x11223344,
          "LOCK XADD must store the sum and return the original memory value");

    cpu.regs[Cpu::EAX] = 0;
    program(cpu, code, {0xf0,0x0f,0xb1,0x0b}); // LOCK CMPXCHG [EBX],ECX; mismatch.
    retire(cpu, "locked CMPXCHG mismatch");
    check(cpu.read32(destination) == 0x11223347 && cpu.regs[Cpu::EAX] == 0x11223347 &&
              !cpu.flag(Cpu::ZF),
          "LOCK CMPXCHG mismatch must update EAX without changing memory");
    program(cpu, code, {0xf0,0x0f,0xb1,0x0b}); // EAX now matches.
    retire(cpu, "locked CMPXCHG match");
    check(cpu.read32(destination) == 0x11223344 && cpu.flag(Cpu::ZF),
          "LOCK CMPXCHG match must commit the source and set ZF");

    cpu.regs[Cpu::ECX] = 33;
    cpu.write32(destination + 4, 0);
    program(cpu, code, {0xf0,0x0f,0xab,0x0b}); // LOCK BTS [EBX],ECX
    retire(cpu, "locked BTS with a bit-string index");
    check(cpu.read32(destination + 4) == 2 && cpu.read32(destination) == 0x11223344 &&
              !cpu.flag(Cpu::CF),
          "LOCK BTS index 33 must select bit 1 in the next dword");
    program(cpu, code, {0xf0,0x0f,0xba,0x73,0x04,0x01}); // LOCK BTR [EBX+4],1
    retire(cpu, "locked immediate BTR");
    check(cpu.read32(destination + 4) == 0 && cpu.flag(Cpu::CF),
          "LOCK BTR must return the replaced bit in CF");
}

void invalid_encodings_and_mmio() {
    Cpu cpu(2 * 1024 * 1024);
    boot_protected32(cpu);
    constexpr auto code = 0x2100;
    unsigned reads = 0, writes = 0;
    cpu.regs[Cpu::EBX] = 0x7000;
    cpu.regs[Cpu::ECX] = 1;
    cpu.eflags = 2 | Cpu::CF | Cpu::OF;
    cpu.idtr = {0, 0}; // Report #UD directly, with no guest exception handler.
    cpu.mmio_claim_hook = [](std::uint64_t address) {
        return address >= 0x7000 && address < 0x7004;
    };
    cpu.mmio_read_hook = [&reads](std::uint64_t) {
        ++reads;
        return std::uint8_t{0x5a};
    };
    cpu.mmio_write_hook = [&writes](std::uint64_t, std::uint8_t) {
        ++writes;
        return true;
    };
    const auto regs = cpu.regs;
    const auto flags = cpu.eflags;
    const auto before = cpu.instructions;
    const auto invalid = [&](std::initializer_list<std::uint8_t> bytes) {
        program(cpu, code, bytes);
        check(cpu.step() == StepResult::Faulted && cpu.fault &&
                  cpu.fault->code == FaultCode::InvalidInstruction,
              "invalid LOCK encoding must raise #UD");
        check(cpu.regs == regs && cpu.eflags == flags && cpu.eip == code &&
                  cpu.instructions == before && reads == 0 && writes == 0,
              "invalid LOCK encoding must fail before touching operands");
    };
    invalid({0xf0,0x09,0xcb});           // LOCK OR EBX,ECX
    invalid({0xf0,0x0b,0x0b});           // LOCK OR ECX,[EBX]
    invalid({0xf0,0x39,0x0b});           // LOCK CMP [EBX],ECX
    invalid({0xf0,0x83,0x3b,0x01});     // LOCK CMP [EBX],imm8
    invalid({0xf0,0x0f,0xa3,0x0b});     // LOCK BT [EBX],ECX
    invalid({0xf0,0x90});                // LOCK NOP

    for (const auto opcode : {0x09, 0x87}) {
        // Explicit LOCK OR versus implicit-locked memory XCHG. Neither may
        // produce irreversible reads/writes through the MMIO callbacks.
        if (opcode == 0x09) program(cpu, code, {0xf0,0x09,0x0b});
        else program(cpu, code, {0x87,0x0b});
        check(cpu.step() == StepResult::Faulted && reads == 0 && writes == 0 &&
                  cpu.regs == regs && cpu.eflags == flags && cpu.eip == code &&
                  cpu.instructions == before,
              "locked MMIO must fail before any device callback or guest commit");
    }

    cpu.mmio_claim_hook = {};
    cpu.memory.attach_window(0x7000, 4,
        [&reads](std::uint64_t) {
            ++reads;
            return std::uint8_t{0x5a};
        },
        [&writes](std::uint64_t, std::uint8_t) { ++writes; });
    program(cpu, code, {0xf0,0x09,0x0b});
    check(cpu.step() == StepResult::Faulted && reads == 0 && writes == 0 &&
              cpu.regs == regs && cpu.eflags == flags && cpu.eip == code &&
              cpu.instructions == before,
          "locked Ether callback memory must fail before invoking nontransactional callbacks");
}

void boot_long(Cpu& cpu) {
    cpu.reset();
    const auto disk = cosmic_pc::long_mode_boot_disk();
    cpu.memory.write_block(0x7c00, disk.data(), disk.size());
    cpu.interrupt_hook = [](Cpu& current, std::uint8_t vector) {
        if (vector != 0x15 || current.reg16(Cpu::EAX) != 0x2401) return false;
        current.a20_enabled = true;
        current.set_reg8(4, 0);
        current.set_flag(Cpu::CF, false);
        return true;
    };
    for (unsigned i = 0; i < 10000 && !cpu.is_long_mode(); ++i)
        retire(cpu, "long-mode fixture");
    check(cpu.is_long_mode(), "fixture did not enter 64-bit code");
    for (unsigned i = 0; i < 512; ++i)
        cpu.write64(0x4000 + i * 8, (std::uint64_t(i) << 12) | 3);
    cpu.write64(0x3000, 0x4003);
    cpu.idtr = {0, 0};
    cpu.regs[Cpu::ESP] = 0x190000;
}

void cross_page_rollback() {
    Cpu cpu(2 * 1024 * 1024);
    boot_long(cpu);
    constexpr auto code = 0xa000;
    constexpr auto first_frame = 0xd000;
    constexpr auto second_frame = 0x17000;
    constexpr auto pte_first = 0x4000 + 11 * 8;
    constexpr auto pte_second = 0x4000 + 12 * 8;
    cpu.write64(pte_first, first_frame | 3);
    cpu.write16(first_frame + 0xffe, 0x2211);
    cpu.write16(second_frame, 0x4433);
    cpu.regs[Cpu::EBX] = 0xbffe; // Four bytes span two non-contiguous physical frames.
    cpu.regs[Cpu::ECX] = 0x01010101;
    cpu.regs[Cpu::EAX] = 0; // CMPXCHG comparison mismatch.
    cpu.eflags = 2 | Cpu::CF | Cpu::OF;
    const auto regs = cpu.regs;
    const auto flags = cpu.eflags;
    const auto before = cpu.instructions;
    const auto fault = [&](std::initializer_list<std::uint8_t> bytes, unsigned error) {
        program(cpu, code, bytes);
        check(cpu.step() == StepResult::Faulted && cpu.fault &&
                  cpu.fault->code == FaultCode::PageFault &&
                  cpu.fault->linear_address == 0xc000 && cpu.cr2 == 0xc000 &&
                  cpu.fault->error_code == error,
              "cross-page LOCK must fault at the second page with the write-access error code");
        check(cpu.regs == regs && cpu.eflags == flags && cpu.eip == code &&
                  cpu.instructions == before && cpu.read16(first_frame + 0xffe) == 0x2211 &&
                  cpu.read16(second_frame) == 0x4433,
              "cross-page LOCK failure must roll back registers, flags and both memory banks");
    };
    cpu.write64(pte_second, 0);
    fault({0xf0,0x09,0x0b}, 2); // Absent page, write access.
    cpu.write64(pte_second, second_frame | 1);
    fault({0xf0,0x0f,0xb1,0x0b}, 3); // CMPXCHG checks write permission even on mismatch.
    fault({0xf0,0x09,0x0b}, 3);       // Present read-only, write access.
    cpu.write64(pte_second, second_frame | 3);
    program(cpu, code, {0xf0,0x09,0x0b});
    retire(cpu, "cross-page OR with both pages writable");
    check(cpu.read16(first_frame + 0xffe) == 0x2311 && cpu.read16(second_frame) == 0x4533 &&
              cpu.regs == regs,
          "cross-page LOCK must update the two mapped physical banks consecutively");
}
} // namespace

int main() {
    try {
        replay_prefix_and_opcode();
        locked_arithmetic_and_exchange();
        invalid_encodings_and_mmio();
        cross_page_rollback();
        std::cout << "REVERSE_WINDOWS_LOCK_RMW_PASS opcode=f009 synthetic_modrm=1 "
                     "protected32=1 invalid_ud=1 flags=1 cross_page=1 rollback=1 "
                     "mmio_nonmutating=1 ether_window_nonmutating=1 atomics=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "REVERSE_WINDOWS_LOCK_RMW_FAIL " << error.what() << '\n';
        return 1;
    }
}
