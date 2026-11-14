#include "../pc_machine.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;

constexpr std::uint16_t kSegment = 0x2000, kEntry = 0x0a21, kIntIp = 0x0a27;
constexpr unsigned kProgram = kSegment * 16 + kEntry, kResult = 0x500;
constexpr unsigned kHead = 0x41a, kTail = 0x41c, kFirstSlot = 0x41e;
constexpr unsigned kStart = 0x480, kEnd = 0x482;
constexpr std::uint32_t kFlags = 0x0243;
unsigned checks = 0;

void need(bool result, const char* reason) {
  ++checks;
  if (!result) throw std::runtime_error(reason);
}

void start(PcMachine& pc, std::uint16_t ax, std::uint16_t cx = 0, std::uint32_t flags = kFlags) {
  // MOV AX,service; MOV CX,word; INT 16h at 2000:0A27;
  // MOV [0500],AX; HLT. The guest records the returned value itself.
  const std::array<std::uint8_t, 12> code{
      0xb8, std::uint8_t(ax), std::uint8_t(ax >> 8),
      0xb9, std::uint8_t(cx), std::uint8_t(cx >> 8),
      0xcd, 0x16, 0xa3, 0x00, 0x05, 0xf4};
  pc.cpu.memory.write_block(kProgram, code.data(), code.size());
  pc.cpu.eip = kEntry;
  pc.cpu.halted = false;
  pc.cpu.eflags = flags;
  pc.run(16);
  if (pc.cpu.fault) throw std::runtime_error(pc.cpu.fault->describe());
}

std::uint16_t returned(PcMachine& pc) {
  need(pc.cpu.halted && !pc.waiting_key, "the BIOS call did not return to its next instruction");
  return pc.cpu.read16(kResult);
}

std::uint16_t call(PcMachine& pc, std::uint8_t function, std::uint16_t cx = 0) {
  start(pc, std::uint16_t(function) << 8, cx);
  return returned(pc);
}

void exact_call_and_shared_queue() {
  PcMachine pc;
  pc.boot_demo();
  pc.cpu.reset(kSegment, kEntry);
  need(pc.cpu.read16(kStart) == 0x1e && pc.cpu.read16(kEnd) == 0x3e &&
       pc.cpu.read16(kHead) == 0x1e && pc.cpu.read16(kTail) == 0x1e,
       "BIOS must publish a valid empty ring in the BDA");
  pc.cpu.regs[Cpu::EAX] = 0x12340000;
  pc.cpu.regs[Cpu::ECX] = 0x34560000;
  pc.cpu.regs[Cpu::EBX] = 0x11223344;
  unsigned matched_calls = 0;
  pc.cpu.step_trace_hook = [&](const Cpu::StepTrace& step) {
    if (step.cs_before == kSegment && step.rip_before == kIntIp &&
        step.decoded_bytes == std::vector<std::uint8_t>{0xcd, 0x16})
      ++matched_calls;
  };
  need(call(pc, 0x05, 0x0020) == 0x0500 && matched_calls == 1,
       "2000:0A27 must store AX=0500/CX=0020");
  pc.cpu.step_trace_hook = {};
  need(pc.cpu.regs[Cpu::EAX] == 0x12340500 && pc.cpu.regs[Cpu::ECX] == 0x34560020 &&
       pc.cpu.regs[Cpu::EBX] == 0x11223344 && pc.cpu.eflags == kFlags,
       "AH=05h may return AL without changing CX, other registers, or FLAGS");
  need(pc.cpu.read16(kFirstSlot) == 0x0020 && pc.cpu.read16(kTail) == 0x20,
       "the reported keystroke must be visible to the guest in the BDA");
  pc.key('K', 0x25);
  for (auto service : {0x01, 0x11}) {
    need(call(pc, std::uint8_t(service)) == 0x0020 && pc.cpu.eflags == (kFlags & ~Cpu::ZF) &&
         pc.cpu.read16(kHead) == 0x1e, "status must peek without consuming or modifying other flags");
  }
  need(call(pc, 0x10) == 0x0020 && call(pc, 0x00) == 0x254b,
       "the guest-inserted space and Android input must share FIFO order");
  for (const std::uint16_t service : {0x019a, 0x119b}) {
    start(pc, service, 0, kFlags & ~Cpu::ZF);
    need(returned(pc) == service && pc.cpu.eflags == kFlags,
         "empty status must preserve AX and all FLAGS other than ZF");
  }
  need(call(pc, 0x05, 0xffff) == 0x0500 && call(pc, 0x10) == 0xffff,
       "IBM enhanced-keyboard probe must round-trip FFFF");
  need(call(pc, 0x05, 0x0000) == 0x0500 && call(pc, 0x00) == 0,
       "a zero key word must be distinguishable from an empty queue");
  pc.reset();
  pc.cpu.reset(kSegment, kEntry);
  need(pc.cpu.read16(kHead) == 0x1e && pc.cpu.read16(kTail) == 0x1e &&
       pc.cpu.read16(kFirstSlot) == 0, "machine reset must clear the BIOS keyboard data");
}

void full_and_relocated() {
  PcMachine pc;
  pc.boot_demo();
  pc.cpu.reset(kSegment, kEntry);
  for (unsigned i = 0; i < 15; ++i)
    need(call(pc, 0x05, std::uint16_t(0x1e41 + i)) == 0x0500, "default ring must accept 15 words");
  need(call(pc, 0x05, 0x7777) == 0x0501 && pc.cpu.read16(kTail) == 0x3c &&
       pc.cpu.read16(0x43c) == 0, "overflow must preserve the reserved slot and the tail");
  pc.key('Z', 0x2c);
  need(call(pc, 0x00) == 0x1e41 && call(pc, 0x05, 0x2d58) == 0x0500 &&
       pc.cpu.read16(kTail) == 0x1e, "one free slot must allow the tail to wrap");
  for (unsigned i = 1; i < 15; ++i)
    need(call(pc, 0x10) == 0x1e41 + i, "queue wrap or overflow changed FIFO order");
  need(call(pc, 0x10) == 0x2d58 && pc.cpu.read16(kHead) == pc.cpu.read16(kTail),
       "head reaches the wrapped tail after all slots are read");

  // A guest may install its own compatible queue pointers. Exercise a
  // ring crossing a RAM page and seed a word without calling the BIOS.
  pc.cpu.write16(kStart, 0x0bfc);
  pc.cpu.write16(kEnd, 0x0c02);
  pc.cpu.write16(kHead, 0x0bfc);
  pc.cpu.write16(kTail, 0x0bfe);
  pc.cpu.write16(0x0ffc, 0x1e61);
  need(call(pc, 0x01) == 0x1e61 && call(pc, 0x05, 0x3062) == 0x0500 &&
       pc.cpu.read16(0x0ffe) == 0x3062, "the ring must honor guest-provided bounds and words");
  need(call(pc, 0x00) == 0x1e61 && call(pc, 0x05, 0x2e63) == 0x0500 &&
       pc.cpu.read16(0x1000) == 0x2e63 && pc.cpu.read16(kTail) == 0x0bfc,
       "the relocated ring must wrap after crossing the RAM-page boundary");
  pc.cpu.write16(kTail, 0x14); // Not a buffer slot.
  const auto unchanged = pc.cpu.read16(0x414);
  need(call(pc, 0x05, 0x1234) == 0x0501 && pc.cpu.read16(kTail) == 0x14 &&
       pc.cpu.read16(0x414) == unchanged, "malformed pointers must not write outside the ring");
  need(call(pc, 0x11) == 0x1100 && pc.cpu.flag(Cpu::ZF), "malformed pointers must read as unavailable");
}

void enhanced_keys_and_wait() {
  PcMachine pc;
  pc.boot_demo();
  pc.cpu.reset(kSegment, kEntry);
  pc.key_event(0, 0xe04b, true);
  pc.key_event(0, 0xe04b, false);
  need(call(pc, 0x11) == 0x4be0 && call(pc, 0x01) == 0x4b00 && call(pc, 0x10) == 0x4be0,
       "legacy and enhanced status must expose the proper form of the same arrow key");
  pc.key_event(13, 0xe01c, true);
  need(call(pc, 0x11) == 0xe00d && call(pc, 0x00) == 0x1c0d, "keypad Enter conversion");
  pc.key_event(0, 0x57, true);
  need(call(pc, 0x11) == 0x8500 && call(pc, 0x10) == 0x8500, "F11 enhanced BIOS code");
  need(call(pc, 0x05, 0x3bf0) == 0x0500 && call(pc, 0x10) == 0x3b00,
       "enhanced read must clear the F0 marker with a nonzero scan code");
  pc.key_event(0, 0x58, true);
  pc.key('Q', 0x10);
  need(call(pc, 0x01) == 0x1051 && call(pc, 0x10) == 0x1051,
       "legacy status discards enhanced-only codes and leaves the compatible entry queued");

  start(pc, 0);
  need(pc.waiting_key && pc.cpu.eip == kIntIp + 2, "empty AH=00h must wait after the INT");
  const auto steps = pc.cpu.instructions;
  pc.run(64);
  pc.key_event('X', 0x2d, false);
  pc.key_event(0, 0x57, true);
  pc.key_event(0, 0x2a, true);
  need(pc.waiting_key && pc.cpu.instructions == steps,
       "release, modifier and incompatible key must not wake legacy wait");
  pc.key('X', 0x2d);
  pc.run(16);
  need(returned(pc) == 0x2d58 && pc.cpu.instructions - steps == 2,
       "host key must complete the suspended INT exactly once");
  start(pc, 0x1000);
  need(pc.waiting_key, "empty AH=10h must wait for an enhanced key");
  pc.key_event(0, 0x58, true);
  pc.run(16);
  need(returned(pc) == 0x8600, "enhanced wait must admit F12");

  pc.key_event(0, 0x1d, true);
  pc.key_event(0, 0xe01d, true);
  pc.key_event(0, 0xe038, true);
  need(call(pc, 0x02) == 0x020e && call(pc, 0x12) == 0x0d0e, "both modifier queries must read the BDA state");
  pc.key_event(0, 0x1d, false);
  need(call(pc, 0x12) == 0x0c0e, "releasing left Ctrl must leave the right Ctrl active");
  pc.key_event(0, 0xe01d, false);
  pc.key_event(0, 0xe038, false);
  pc.key_event(0, 0x2a, false);
  need(call(pc, 0x12) == 0 && call(pc, 0x11) == 0x1100 && pc.cpu.flag(Cpu::ZF),
       "modifiers must leave no queued keystrokes");
}
}  // namespace

int main() {
  try {
    exact_call_and_shared_queue();
    full_and_relocated();
    enhanced_keys_and_wait();
    std::cout << "PRIME_C01_BIOS_KEYBOARD_PASS checks=" << checks
              << " cs_ip=2000:0A27 ax=0500 cx=0020 bda_fifo=1 overflow=1 wait=1\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "PRIME_C01_BIOS_KEYBOARD_FAIL " << error.what() << '\n';
    return 1;
  }
}
