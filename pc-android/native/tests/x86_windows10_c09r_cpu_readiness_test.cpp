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
}
int main(){
 try {
   cmpxchg16b();cmpxchg8b();prefetchw();cpuid_profile();
   std::cout<<"REVERSE_WINDOWS_C09R_CPU_READINESS_PASS checks="<<checks
            <<" Windows10_ready=false windows_iso_tested=false\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"REVERSE_WINDOWS_C09R_CPU_READINESS_FAIL "<<e.what()<<"\n";
   return 1;
 }
}
