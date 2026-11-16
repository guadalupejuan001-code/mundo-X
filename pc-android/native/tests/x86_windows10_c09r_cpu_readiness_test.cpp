#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
unsigned checks=0;
void check(bool ok,const char* reason) {
  ++checks;
  if(!ok)throw std::runtime_error(reason);
}
void program(Cpu& cpu,std::uint64_t addr,std::initializer_list<std::uint8_t> code) {
  const std::vector<std::uint8_t> body(code);
  cpu.memory.write_block(addr,body.data(),body.size());
  cpu.eip=addr;cpu.halted=false;cpu.fault.reset();
}
void boot64(Cpu& cpu) {
  const auto guest=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,guest.data(),guest.size());
  cpu.interrupt_hook=[](Cpu& emu,std::uint8_t number) {
    if(number!=0x15||emu.reg16(Cpu::EAX)!=0x2401)
      emu.fail("unexpected BIOS boot service");
    emu.a20_enabled=true;emu.set_reg8(4,0);
    emu.set_flag(Cpu::CF,false);
    return true;
  };
  bool ready=false;
  for(unsigned i=0;i<12000;++i) {
    const auto result=cpu.step();
    if(result==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(result==StepResult::Halted&&cpu.is_long_mode()){ready=true;break;}
  }
  check(ready,"canonical Mundo X x64 guest never entered long mode");
  cpu.regs[Cpu::ESP]=0x190000;
}
void cmpxchg16b() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  boot64(cpu);
  const std::uint64_t original_lo=0x1122334455667788ULL;
  const std::uint64_t original_hi=0x99aabbccddeeff00ULL;
  const std::uint64_t replacement_lo=0x1020304050607080ULL;
  const std::uint64_t replacement_hi=0xa1b2c3d4e5f60718ULL;
  cpu.write64(0xb000,original_lo);cpu.write64(0xb008,original_hi);
  cpu.regs[Cpu::EAX]=original_lo;cpu.regs[Cpu::EDX]=original_hi;
  cpu.regs[Cpu::EBX]=replacement_lo;cpu.regs[Cpu::ECX]=replacement_hi;
  // LOCK REX.W 0F C7 /1 with SIB absolute [0000B000].
  program(cpu,0xa000,{0xf0,0x48,0x0f,0xc7,0x0c,0x25,0x00,0xb0,0,0});
  check(cpu.step()==StepResult::Executed&&!cpu.fault,"LOCK CMPXCHG16B failed");
  check(cpu.flag(Cpu::ZF),"successful CMPXCHG16B must set ZF");
  check(cpu.read64(0xb000)==replacement_lo&&
        cpu.read64(0xb008)==replacement_hi,
        "CMPXCHG16B wrote incorrect two-word little-endian value");
  check(cpu.regs[Cpu::EAX]==original_lo&&cpu.regs[Cpu::EDX]==original_hi,
        "successful CX16 must not overwrite RDX:RAX");
  cpu.regs[Cpu::EAX]=0;cpu.regs[Cpu::EDX]=0;
  program(cpu,0xa000,{0xf0,0x48,0x0f,0xc7,0x0c,0x25,0x00,0xb0,0,0});
  check(cpu.step()==StepResult::Executed&&!cpu.fault,"mismatched CMPXCHG16B faulted");
  check(!cpu.flag(Cpu::ZF),"failed CX16 must clear ZF");
  check(cpu.regs[Cpu::EAX]==replacement_lo&&
        cpu.regs[Cpu::EDX]==replacement_hi,
        "failed CX16 must load existing 128bit RAM value into RDX:RAX");
  check(cpu.read64(0xb000)==replacement_lo&&
        cpu.read64(0xb008)==replacement_hi,
        "failed CX16 must not change guest RAM");
  // The same bytes misaligned by 1 must trigger the precise protection gate.
  program(cpu,0xa010,{0xf0,0x48,0x0f,0xc7,0x0c,0x25,0x01,0xb0,0,0});
  check(cpu.step()==StepResult::Faulted&&cpu.fault&&
        cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,
        "unaligned CMPXCHG16B must reject address with #GP");
  check(cpu.read64(0xb000)==replacement_lo&&
        cpu.read64(0xb008)==replacement_hi,
        "unaligned CX16 changed memory despite #GP");
  std::cout<<"C09R_CX16_ATOMIC_COMPARE_PASS aligned=1 mismatch=1 unaligned_rejected=1\n";
}
void cmpxchg8b() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  cpu.write32(0xb000,0x12345678);cpu.write32(0xb004,0x90abcdef);
  cpu.regs[Cpu::EAX]=0x12345678;cpu.regs[Cpu::EDX]=0x90abcdef;
  cpu.regs[Cpu::EBX]=0x76543210;cpu.regs[Cpu::ECX]=0xfedcba09;
  // address-size override chooses absolute disp32 in real16.
  program(cpu,0x1000,{0x67,0x0f,0xc7,0x0d,0x00,0xb0,0x00,0x00});
  check(cpu.step()==StepResult::Executed&&!cpu.fault,
        "real-mode CMPXCHG8B should execute");
  check(cpu.flag(Cpu::ZF),"successful CX8 did not set ZF");
  check(cpu.read32(0xb000)==0x76543210&&cpu.read32(0xb004)==0xfedcba09,
        "CX8 failed to commit two 32bit halves");
  std::cout<<"C09R_CX8_REAL_GUEST_PASS\n";
}
void prefetchw() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  const auto before=cpu.memory.allocated_bytes();
  // 67 address-size prefix; 0F 0D /1 PREFETCHW on unmapped 0x7fffffff.
  program(cpu,0x1000,{0x67,0x0f,0x0d,0x0d,0xff,0xff,0xff,0x7f});
  const auto used=cpu.memory.allocated_bytes();
  check(cpu.step()==StepResult::Executed&&!cpu.fault,
        "PREFETCHW must be a nonfaulting hint");
  check(cpu.memory.allocated_bytes()==used&&cpu.eip==0x1008,
        "PREFETCHW illegally materialized virtual RAM or misdecoded length");
  check(before<=used,"guest fixture memory use counter inconsistent");
  std::cout<<"C09R_PREFETCHW_NONFAULTING_PASS new_pages=0\n";
}
void cpuid_profile() {
  Cpu cpu(2*1024*1024);
  auto cpuid=[&](std::uint32_t leaf){
    cpu.reset(0,0x1000);cpu.a20_enabled=true;
    cpu.memory[0x1000]=0x0f;cpu.memory[0x1001]=0xa2;
    cpu.regs[Cpu::EAX]=leaf;
    check(cpu.step()==StepResult::Executed&&!cpu.fault,"CPUID guest test failed");
    return std::array<std::uint32_t,4>{
      std::uint32_t(cpu.regs[Cpu::EAX]),
      std::uint32_t(cpu.regs[Cpu::EBX]),
      std::uint32_t(cpu.regs[Cpu::ECX]),
      std::uint32_t(cpu.regs[Cpu::EDX])};
  };
  const auto basic=cpuid(1);
  check((basic[2]&(1u<<13))!=0,"CX16 must be advertised after implementation");
  check((basic[3]&(1u<<8))!=0,"CX8 must be advertised after implementation");
  constexpr std::uint32_t simd=(1U<<23)|(1U<<24)|(1U<<25)|(1U<<26);
  check((basic[3]&simd)==simd,"verified MMX/FXSR/SSE/SSE2 missing from CPUID");
  check((basic[3]&(1u<<0))==0,"must NOT advertise x87 yet");
  const auto ext=cpuid(0x80000001u);
  check((ext[2]&((1u<<0)|(1u<<8)))==((1u<<0)|(1u<<8)),
        "LAHF/SAHF and PREFETCHW features missing from CPUID");
  check((ext[3]&(1u<<20))!=0,"NX feature lost");
  // LOCK is legal on a writable destination in memory, never on registers.
  cpu.set_reg16(Cpu::EBX,0x6000);cpu.set_reg16(Cpu::ESI,0);
  cpu.set_reg16(Cpu::EAX,2);cpu.write16(0x6000,1);
  program(cpu,0x1000,{0xf0,0x01,0x00}); // LOCK ADD [BX+SI],AX.
  check(cpu.step()==StepResult::Executed&&cpu.read16(0x6000)==3,
        "LOCK ADD memory destination must execute");
  program(cpu,0x1000,{0xf0,0x01,0xc3}); // LOCK ADD BX,AX.
  check(cpu.step()==StepResult::Faulted&&cpu.fault&&
        cpu.fault->code==cosmic_pc::FaultCode::InvalidInstruction,
        "LOCK ADD register destination must raise #UD");
  std::cout<<"C09R_CPUID_TRUTHFUL_PROFILE_PASS cx16=1 mmx=1 fxsr=1 sse=1 sse2=1 x87=0\n";
}

void windows_x64_processor_gate() {
  constexpr std::uint32_t required =
      (1U<<0)|(1U<<2)|(1U<<3)|(1U<<4)|(1U<<5)|(1U<<6)|(1U<<7)|(1U<<8)|(1U<<9)|
      (1U<<12)|(1U<<13)|(1U<<14)|(1U<<15)|(1U<<16)|(1U<<19)|(1U<<23)|(1U<<24)|
      (1U<<25)|(1U<<26);
  std::vector<std::uint8_t> code;
  std::vector<std::size_t> failures;
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    code.insert(code.end(), bytes.begin(), bytes.end());
  };
  const auto word = [&](std::uint32_t value) {
    for (unsigned i = 0; i < 4; ++i) code.push_back(std::uint8_t(value >> (i * 8)));
  };
  const auto reject_if_unequal = [&] {
    emit({0x75,0});
    failures.push_back(code.size() - 1);
  };
  emit({0x31,0xc0,0x0f,0xa2});               // XOR EAX,EAX; CPUID
  emit({0x81,0xfb}); word(0x756e6547U); reject_if_unequal(); // "Genu"
  emit({0x81,0xfa}); word(0x49656e69U); reject_if_unequal(); // "ineI"
  emit({0x81,0xf9}); word(0x6c65746eU); reject_if_unequal(); // "ntel"
  emit({0xb8,1,0,0,0,0x0f,0xa2});            // CPUID(1)
  emit({0x81,0xe2}); word(required);
  emit({0x81,0xfa}); word(required); reject_if_unequal();
  emit({0xb8,1,0,0,0x80,0x0f,0xa2});         // CPUID(80000001h)
  constexpr auto extended = (1U<<11)|(1U<<20)|(1U<<29);
  emit({0x81,0xe2}); word(extended);
  emit({0x81,0xfa}); word(extended); reject_if_unequal();
  emit({0x41,0xbf,0xce,0x38,0,0,0xf4});       // Mark the path beyond the gate; HLT.
  for (const auto displacement : failures) code[displacement] = std::uint8_t(code.size() - displacement - 1);
  emit({0xcc});                               // Early kernel-style breakpoint.

  for (const bool compatible : {false, true}) {
    Cpu cpu(4U<<20);
    cpu.use_windows_x64_profile(compatible);
    cpu.set_local_apic_present(true);
    boot64(cpu);
    cpu.idtr = {4095,0x15000}; // No interrupt handlers exist before the test gate.
    cpu.memory.write_block(0xa000,code.data(),code.size());
    cpu.halted = false;
    cpu.eip = 0xa000;
    for (unsigned i = 0; i < 100 && !cpu.halted && !cpu.fault; ++i) cpu.step();
    if (compatible)
      check(cpu.halted && !cpu.fault && cpu.regs[Cpu::R15] == 0x38ce,
            "guest stopped before progressing beyond the Windows x64 CPU gate");
    else
      check(cpu.fault && cpu.fault->opcode == 0xcc && cpu.fault->idt_attempt_count == 3,
            "original ID must reproduce the early INT3/empty-IDT failure pattern");
  }
  std::cout << "WINDOWS_X64_CPU_GATE_PASS original_triple_fault=1 compatible_continues=1\n";
}

void windows_x64_platform_registers() {
  Cpu cpu(4U<<20);
  cpu.use_windows_x64_profile();
  boot64(cpu);
  const auto read = [&](std::uint32_t index) {
    program(cpu,0xa000,{0x0f,0x32});
    cpu.regs[Cpu::ECX] = index;
    check(cpu.step() == StepResult::Executed, "guest RDMSR failed");
    return (cpu.regs[Cpu::EDX] << 32) | cpu.regs[Cpu::EAX];
  };
  const auto write = [&](std::uint32_t index, std::uint64_t value) {
    program(cpu,0xa000,{0x0f,0x30});
    cpu.regs[Cpu::ECX] = index;
    cpu.regs[Cpu::EAX] = std::uint32_t(value);
    cpu.regs[Cpu::EDX] = value >> 32;
    return cpu.step();
  };
  check(read(0xfe) == 0x508 && read(0x179) == 1 &&
        read(0x277) == 0x0007040600070406ULL && read(0x401) == 0, "wrong platform MSR reset values");
  check(read(0x1a0) == 0x1801U, "Windows loader must be able to query IA32_MISC_ENABLE");
  check(read(0x17) == (1ULL<<50) && read(0x8b) == 0,
        "Windows microcode discovery must see the virtual package and firmware identity");
  check(write(0x17,0) == StepResult::Faulted &&
        cpu.fault->code == cosmic_pc::FaultCode::ProtectionFault && read(0x17) == (1ULL<<50),
        "virtual platform ID must be read-only");
  const auto nx = [&] {
    program(cpu,0xa000,{0x0f,0xa2});
    cpu.regs[Cpu::EAX] = 0x80000001U;
    check(cpu.step() == StepResult::Executed, "NX CPUID query failed");
    return (cpu.regs[Cpu::EDX] & (1U << 20)) != 0;
  };
  check(nx() && write(0x1a0,0x1801U | (1ULL<<34)) == StepResult::Executed && !nx() &&
        write(0x1a0,0x1801U) == StepResult::Executed && nx(),
        "IA32_MISC_ENABLE.NX disable must control what the boot loader sees");
  for (const auto& setting : std::array<std::pair<std::uint32_t,std::uint64_t>,6>{{
       {0x277,0x0007060504010006ULL},{0x200,0x100006},{0x201,0x0ff00800},
       {0x250,0x0606060606060606ULL},{0x2ff,0xc06},{0x400,UINT64_MAX}}})
    check(write(setting.first,setting.second) == StepResult::Executed && read(setting.first) == setting.second,
          "platform MSR did not retain valid guest bits");
  check(write(0x277,2) == StepResult::Faulted && cpu.fault->code == cosmic_pc::FaultCode::ProtectionFault &&
        read(0x277) == 0x0007060504010006ULL, "reserved PAT write changed the previous value");
  program(cpu,0xa000,{0x0f,0x22,0xe0}); // MOV CR4,RAX
  cpu.regs[Cpu::EAX] = cpu.cr4 | 0x40;
  check(cpu.step() == StepResult::Executed && (cpu.cr4 & 0x40), "CR4.MCE could not be enabled");
  program(cpu,0xa000,{
      0xdb,0xe3,0xd9,0x3c,0x25,0,0xb0,0,0, // FNINIT; FNSTCW [B000]
      0xd9,0xe8,0xd9,0xe8,0xde,0xc1,0xdb,0x1c,0x25,8,0xb0,0,0, // FLD1 twice; FADDP; FISTP [B008]
      0x0f,0xae,0x3c,0x25,8,0xb0,0,0,0xf4}); // CLFLUSH [B008]; HLT
  for (unsigned i = 0; i < 8 && !cpu.halted && !cpu.fault; ++i) cpu.step();
  check(!cpu.fault && cpu.halted && cpu.read16(0xb000) == 0x37f && cpu.read32(0xb008) == 2,
        "x87 startup and CLFLUSH did not perform the advertised operations");
  cpu.reset();
  check(cpu.windows_x64_profile() && read(0x277) == 0x0007040600070406ULL && read(0x400) == 0,
        "processor identity and register reset must survive guest reboot");
  std::cout << "WINDOWS_X64_PLATFORM_PASS mtrr=1 pat=1 mca=1 x87=1 clflush=1\n";
}

void windows_profile_legacy_pages() {
  Cpu cpu(16U<<20);
  cpu.use_windows_x64_profile();
  cpu.reset(0,0x1000);
  cpu.a20_enabled = true;
  cpu.gdtr = {31,0x3000};
  cpu.write64(0x3010,0x00cf9a000000ffffULL);
  cpu.write64(0x3018,0x00cf92000000ffffULL);
  cpu.write32(0x4000,0x00000083U);
  cpu.write32(0x4004,0x00800083U);
  cpu.write32(0x801000,0xb16b00b5U);
  program(cpu,0x2000,{
      0x66,0xb8,0x18,0,0x8e,0xd8,                // Flat data selector.
      0xb8,0,0x40,0,0,0x0f,0x22,0xd8,            // CR3=4000h
      0xb8,0x10,0,0,0,0x0f,0x22,0xe0,            // CR4.PSE
      0xb8,0x11,0,1,0x80,0x0f,0x22,0xc0,        // CR0.PG|WP|PE|ET
      0xa1,0,0x10,0x40,0,0xa3,8,0x10,0x40,0,0xf4});
  program(cpu,0x1000,{0x66,0xb8,0x11,0,0,0,0x0f,0x22,0xc0,0x66,0xea,0,0x20,0,0,0x10,0});
  for (unsigned i = 0; i < 20 && !cpu.halted && !cpu.fault; ++i) cpu.step();
  check(!cpu.fault && cpu.halted && std::string(cpu.mode_name()) == "protected32" &&
        cpu.read32(0x801008) == 0xb16b00b5U && (cpu.read32(0x4004) & 0x60U) == 0x60U,
        "4 MiB legacy PSE translation or A/D bits were incorrect");
  std::cout << "WINDOWS_X64_LEGACY_PSE_PASS\n";
}

void windows_profile_io_debug() {
  Cpu cpu(4U<<20);
  cpu.use_windows_x64_profile();
  boot64(cpu);
  cpu.idtr = {4095,0x15000};
  cpu.write64(0x15010,0x00008e000008b000ULL);
  cpu.write64(0x15018,0);
  cpu.write16(0xb000,0xcf48); // IRETQ
  const auto setup = [&](std::uint64_t value, std::initializer_list<std::uint8_t> bytes) {
    program(cpu,0xa000,bytes);
    cpu.regs[Cpu::EAX] = value;
    check(cpu.step() == StepResult::Executed, "setup for I/O breakpoint failed");
  };
  // The demo uses CS=0010h; reuse its active code selector in the trap gate.
  cpu.write16(0x15012,cpu.segments[Cpu::CS]);
  setup(cpu.cr4 | 8U,{0x0f,0x22,0xe0});
  setup(0x80,{0x0f,0x23,0xc0}); // DR0
  setup(0x00020401,{0x0f,0x23,0xf8}); // DR7, local I/O breakpoint 0.
  unsigned reads = 0;
  cpu.in_hook = [&](std::uint16_t port,unsigned bytes) -> std::optional<std::uint32_t> {
    if (port != 0x80 || bytes != 1) return std::nullopt;
    ++reads;
    return 0xac;
  };
  program(cpu,0xa000,{0xe4,0x80,0x0f,0x21,0xf0,0xf4}); // IN AL,80h; MOV RAX,DR6
  const auto steps = cpu.instructions;
  check(cpu.step() == StepResult::Executed && cpu.eip == 0xb000 && reads == 1 &&
        cpu.reg8(Cpu::EAX) == 0xac && cpu.read64(cpu.regs[Cpu::ESP]) == 0xa002 &&
        cpu.instructions == steps + 1, "I/O #DB must follow retirement and stack the next instruction");
  check(cpu.step() == StepResult::Executed && cpu.eip == 0xa002 &&
        cpu.step() == StepResult::Executed && (cpu.regs[Cpu::EAX] & 1U) && reads == 1,
        "IRETQ must resume after the I/O without repeating it");
  std::cout << "WINDOWS_X64_IO_DEBUG_PASS\n";
}
}
int main(){
 try {
   cmpxchg16b();cmpxchg8b();prefetchw();cpuid_profile();
   windows_x64_processor_gate();
   windows_x64_platform_registers();
   windows_profile_legacy_pages();
   windows_profile_io_debug();
   std::cout<<"REVERSE_WINDOWS_C09R_CPU_READINESS_PASS checks="<<checks
            <<" Windows10_ready=false windows_iso_tested=false\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"REVERSE_WINDOWS_C09R_CPU_READINESS_FAIL "<<e.what()<<"\n";
   return 1;
 }
}
