#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;

void check(bool condition, const std::string& message) {
    if (!condition) throw std::runtime_error(message);
}

void instruction(Cpu& cpu, const char* label) {
    const auto result = cpu.step();
    check(result == StepResult::Executed,
          std::string(label) + ": " +
              (cpu.fault ? cpu.fault->describe() : "instruction did not retire"));
}

void write_code(Cpu& cpu, std::uint32_t address, std::initializer_list<std::uint8_t> bytes) {
    cpu.memory.write_block(address, bytes.begin(), bytes.size());
}

std::uint64_t descriptor(std::uint32_t base, std::uint32_t limit,
                         unsigned access, unsigned attributes = 0) {
    return (std::uint64_t(limit & 0xffffU)) |
           (std::uint64_t(base & 0xffffffU) << 16) |
           (std::uint64_t(access) << 40) |
           (std::uint64_t((limit >> 16) & 15U) << 48) |
           (std::uint64_t(attributes) << 52) |
           (std::uint64_t(base >> 24) << 56);
}

void load_protected16(Cpu& cpu) {
    cpu.reset(0x1000, 0x0000);
    cpu.a20_enabled = true;
    cpu.gdtr = {0x1f, 0x6000};
    cpu.write64(0x6008, descriptor(0x20000, 0xffff, 0x9a));
    cpu.write64(0x6010, descriptor(0x30000, 0x1f, 0x92, 8)); // 128 KiB limit.
    cpu.write64(0x6018, descriptor(0x50000, 0xffff, 0x92));
    write_code(cpu, 0x10000, {
        0xfa,                           // CLI
        0xb8,0x11,0x00,                 // MOV AX,0011h
        0x0f,0x22,0xc0,                 // MOV CR0,EAX
        0xea,0x00,0x01,0x08,0x00        // JMP FAR 0008:0100
    });
    write_code(cpu, 0x20100, {
        0xb8,0x10,0x00,0x8e,0xd8,      // MOV AX,0010h; MOV DS,AX
        0xb8,0x18,0x00,0x8e,0xd0,      // MOV AX,0018h; MOV SS,AX
        0xbc,0x00,0x08                  // MOV SP,0800h
    });
    for (unsigned i = 0; i < 9; ++i) instruction(cpu, "protected16 setup");
    check(std::string(cpu.mode_name()) == "protected16" && cpu.cpl() == 0 &&
              cpu.segments[Cpu::CS] == 8 && cpu.eip == 0x10d,
          "fixture did not reach a protected16 ring-0 GDT-backed code segment");
}

void exit_reload_and_reenter() {
    Cpu cpu(2 * 1024 * 1024);
    load_protected16(cpu);
    cpu.write16(0x42000, 0x115a); // DS hidden base 30000h + offset 12000h.
    cpu.write16(0x7a000, 0x227b); // New DS base 68000h + the same offset.
    cpu.write16(0x12100, 0xdead); // Decoy: visible protected DS * 16 + offset.
    write_code(cpu, 0x2010d, {
        0x66,0xb8,0x10,0x00,0x00,0x00,  // MOV EAX,00000010h
        0x0f,0x22,0xc0,                 // MOV CR0,EAX (reported instruction)
        0x67,0xa1,0x00,0x20,0x01,0x00,  // MOV AX,[DS:00012000h]
        0x6a,0x51,                      // PUSH 51h through inherited SS
        0xea,0x00,0x02,0x00,0x60       // JMP FAR 6000:0200
    });
    write_code(cpu, 0x60200, {
        0x5b,                           // POP BX through inherited SS
        0xb8,0x00,0x68,0x8e,0xd8,      // MOV AX,6800h; MOV DS,AX
        0x67,0xa1,0x00,0x20,0x01,0x00, // MOV AX,[DS:00012000h]
        0xb8,0x00,0x70,0x8e,0xd0,      // MOV AX,7000h; MOV SS,AX
        0x36,0x67,0xa1,0x00,0x00,0x01,0x00, // MOV AX,[SS:00010000h], outside SS limit
        0x6a,0x52,                     // PUSH 52h through reloaded SS
        0x5b,                          // POP BX through reloaded SS
        0xb8,0x11,0x00,0x0f,0x22,0xc0,// Set PE again
        0xea,0x40,0x01,0x08,0x00      // JMP FAR 0008:0140
    });
    write_code(cpu, 0x20140, {0xb8,0xa7,0x42,0xf4});

    instruction(cpu, "load reported CR0 value");
    check(cpu.regs[Cpu::EAX] == 0x10 && cpu.eip == 0x113,
          "fixture did not reach the exact MOV CR0,EAX operands");
    const auto before = cpu.instructions;
    instruction(cpu, "reported MOV CR0,EAX with EAX=10h");
    check(cpu.instructions == before + 1 && cpu.cr0 == 0x10 && cpu.eip == 0x116 &&
              cpu.segments[Cpu::CS] == 8 && cpu.cpl() == 0 &&
              std::string(cpu.mode_name()) == "real16",
          "PE clear must retire once, keep CS selector and advance IP");

    instruction(cpu, "real-mode fetch through the inherited CS descriptor");
    check(cpu.reg16(Cpu::EAX) == 0x115a && cpu.segments[Cpu::DS] == 0x10,
          "unreal DS access must use its inherited base and extended limit");
    instruction(cpu, "real-mode push through the inherited stack descriptor");
    check(cpu.reg16(Cpu::ESP) == 0x07fe && cpu.read16(0x507fe) == 0x51,
          "clearing PE changed the cached SS base or stack width");
    instruction(cpu, "far jump to real-mode CS base");
    check(cpu.segments[Cpu::CS] == 0x6000 && cpu.eip == 0x0200,
          "far jump did not load the real-mode CS and IP");
    instruction(cpu, "real-mode far target fetch and inherited stack pop");
    check(cpu.reg16(Cpu::EBX) == 0x51 && cpu.reg16(Cpu::ESP) == 0x0800,
          "far transfer incorrectly changed the inherited stack");

    instruction(cpu, "load real-mode data segment value");
    instruction(cpu, "reload real-mode DS");
    instruction(cpu, "read beyond 64 KiB using the reloaded real-mode DS");
    check(cpu.reg16(Cpu::EAX) == 0x227b && cpu.segments[Cpu::DS] == 0x6800,
          "real-mode DS reload must replace its base and preserve its cached limit");
    instruction(cpu, "load real-mode stack segment value");
    instruction(cpu, "reload real-mode SS");
    const auto stack_limit_ip = cpu.eip;
    check(cpu.step() == StepResult::Faulted && cpu.fault &&
              cpu.fault->code == FaultCode::SegmentLimit && cpu.eip == stack_limit_ip,
          "reloaded SS must retain the ordinary 64 KiB limit");
    cpu.fault.reset();
    cpu.eip += 7;
    instruction(cpu, "push through the reloaded real-mode stack");
    check(cpu.segments[Cpu::SS] == 0x7000 && cpu.read16(0x707fe) == 0x52 &&
              cpu.read16(0x507fe) == 0x51,
          "explicit SS reload must move the stack base without overwriting the old stack");
    instruction(cpu, "pop through the reloaded real-mode stack");
    check(cpu.reg16(Cpu::EBX) == 0x52 && cpu.reg16(Cpu::ESP) == 0x0800,
          "stack did not round-trip through the reloaded segment");
    instruction(cpu, "load protected-mode control value");
    instruction(cpu, "re-enter protected mode");
    check((cpu.cr0 & 1) && cpu.cpl() == 0 &&
              std::string(cpu.mode_name()) == "protected16",
          "setting PE again must enter protected16 at ring 0");
    instruction(cpu, "load protected CS from GDT after re-entry");
    instruction(cpu, "execute code from the reloaded protected CS");
    check(cpu.segments[Cpu::CS] == 8 && cpu.reg16(Cpu::EAX) == 0x42a7 &&
              cpu.step() == StepResult::Halted,
          "protected code was not reached after re-entry");
}

void reject_pg_without_pe() {
    Cpu cpu(2 * 1024 * 1024);
    load_protected16(cpu);
    write_code(cpu, 0x2010d, {0x0f,0x22,0xc0});
    cpu.regs[Cpu::EAX] = 0x80000010U;
    const auto before = cpu.instructions;
    const auto initial_cr0 = cpu.cr0;
    check(cpu.step() == StepResult::Faulted && cpu.fault &&
              cpu.fault->code == FaultCode::ProtectionFault &&
              cpu.fault->reason.find("CR0.PG requires CR0.PE") != std::string::npos,
          "attempt to set PG without PE must still fault");
    check(cpu.cr0 == initial_cr0 && cpu.eip == 0x10d && cpu.instructions == before &&
              cpu.segments[Cpu::CS] == 8 && cpu.segments[Cpu::DS] == 0x10 &&
              cpu.segments[Cpu::SS] == 0x18,
          "invalid CR0 write must preserve execution mode, selectors, IP and step count");
    cpu.fault.reset();
    cpu.write16(0x30100, 0x2e7b);
    write_code(cpu, 0x2010d, {0xa1,0x00,0x01});
    instruction(cpu, "protected access after rejected control write");
    check(cpu.reg16(Cpu::EAX) == 0x2e7b,
          "invalid CR0 write must not change the hidden DS descriptor");
}

void retain_default_sizes() {
    Cpu cpu(2 * 1024 * 1024);
    cpu.reset(0x1000, 0);
    cpu.a20_enabled = true;
    cpu.gdtr = {0x17, 0x6000};
    cpu.write64(0x6008, descriptor(0x20000, 0x1f, 0x9a, 12)); // CS.D=1, 128 KiB.
    cpu.write64(0x6010, descriptor(0x30000, 0x1f, 0x92, 12)); // SS.B=1, 128 KiB.
    write_code(cpu, 0x10000, {
        0xb8,0x11,0x00,0x0f,0x22,0xc0, // Set PE.
        0x66,0xea,0x00,0x10,0x01,0x00,0x08,0x00 // 32-bit far pointer 0008:00011000.
    });
    write_code(cpu, 0x31000, {
        0x66,0xb8,0x10,0x00,0x8e,0xd0, // MOV AX,0010h; MOV SS,AX
        0xbc,0x00,0x20,0x01,0x00,      // MOV ESP,00012000h
        0xb8,0x10,0x00,0x00,0x00,      // MOV EAX,00000010h
        0x0f,0x22,0xc0,                // MOV CR0,EAX
        0x68,0x78,0x56,0x34,0x12,      // PUSH 12345678h with D=1 and B=1
        0x5b,                          // POP EBX
        0xf4
    });
    for (unsigned i = 0; i < 8; ++i) instruction(cpu, "32-bit cached-default fixture");
    check(cpu.is_real_mode() && cpu.cr0 == 0x10 && cpu.eip == 0x11013,
          "PE clear truncated EIP even though the cached CS.D remained set");
    instruction(cpu, "real mode with inherited 32-bit push and ESP");
    check(cpu.regs[Cpu::ESP] == 0x11ffc && cpu.read32(0x41ffc) == 0x12345678,
          "cached CS.D/SS.B must still select a dword push and a 32-bit stack");
    instruction(cpu, "real mode with inherited 32-bit pop and ESP");
    check(cpu.regs[Cpu::EBX] == 0x12345678 && cpu.regs[Cpu::ESP] == 0x12000 &&
              cpu.step() == StepResult::Halted,
          "32-bit defaults were lost before the guest finished");
}

void reload_unusable_ds() {
    Cpu cpu(2 * 1024 * 1024);
    load_protected16(cpu);
    write_code(cpu, 0x2010d, {
        0xb8,0x00,0x00,0x8e,0xd8, // Load a null DS in protected mode.
        0xb8,0x10,0x00,0x0f,0x22,0xc0,
        0xa1,0x00,0x01,           // This cannot read through unusable DS.
        0xb8,0x00,0x68,0x8e,0xd8, // Explicit real-mode load makes DS usable again.
        0xa1,0x00,0x01
    });
    cpu.write16(0x68100, 0xcab5);
    for (unsigned i = 0; i < 4; ++i) instruction(cpu, "unusable DS fixture");
    check(cpu.is_real_mode() && cpu.segments[Cpu::DS] == 0,
          "null DS selector changed while clearing PE");
    const auto fault_ip = cpu.eip;
    check(cpu.step() == StepResult::Faulted && cpu.fault &&
              cpu.fault->code == FaultCode::ProtectionFault && cpu.eip == fault_ip,
          "null protected DS must remain unusable in real mode until explicitly loaded");
    cpu.fault.reset();
    cpu.eip += 3;
    instruction(cpu, "load replacement DS selector");
    instruction(cpu, "restore a usable real-mode DS");
    instruction(cpu, "read through the new real-mode DS base");
    check(cpu.reg16(Cpu::EAX) == 0xcab5,
          "real-mode DS reload did not restore access with the new base");
}

void reject_active_long_code_exit() {
    Cpu cpu(32 * 1024 * 1024);
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
        instruction(cpu, "boot active long code");
    check(cpu.is_long_mode(), "fixture never reached active 64-bit code");
    cpu.idtr = {0, 0}; // Observe the rejected write without a guest handler.
    cpu.eip = 0xa000;
    write_code(cpu, 0xa000, {0x0f,0x22,0xc0});
    cpu.regs[Cpu::EAX] = 0x10;
    const auto cr0 = cpu.cr0;
    const auto efer = cpu.efer;
    const auto before = cpu.instructions;
    check(cpu.step() == StepResult::Faulted && cpu.fault &&
              cpu.fault->code == FaultCode::ProtectionFault &&
              cpu.fault->reason.find("leave 64-bit code before disabling paging") != std::string::npos,
          "clearing PG and PE from active 64-bit code must still fault");
    check(cpu.is_long_mode() && cpu.cr0 == cr0 && cpu.efer == efer &&
              cpu.eip == 0xa000 && cpu.instructions == before,
          "rejected 64-bit exit changed paging, mode or execution position");
}
} // namespace

int main() {
    try {
        exit_reload_and_reenter();
        reject_pg_without_pe();
        retain_default_sizes();
        reload_unusable_ds();
        reject_active_long_code_exit();
        std::cout << "REVERSE_WINDOWS_PROTECTED_REAL_TRANSITION_PASS "
                     "mov_cr0=0f22c0 eax=0x10 far_jump=1 unreal_cache=1 reentry=1 "
                     "cached_default_sizes=1 null_ds_reload=1 pg_guards=1\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "REVERSE_WINDOWS_PROTECTED_REAL_TRANSITION_FAIL " << error.what() << '\n';
        return 1;
    }
}
