#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace{
unsigned checks=0;
void need(bool test,const char* why){++checks;if(!test)throw std::runtime_error(why);}
void put(Cpu& cpu,std::uint64_t where,std::initializer_list<std::uint8_t> bytes){
  const std::vector<std::uint8_t> code(bytes);
  cpu.memory.write_block(where,code.data(),code.size());
}
void one(Cpu& cpu){
  const auto step=cpu.step();
  if(step==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
}
void masked(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  put(cpu,0x1000,{0xd9,0xee, // FLDZ
                  0xd9,0xe8, // FLD1 -> ST0=1, ST1=0
                  0xd8,0xf1, // FDIV ST0,ST1 = +inf, masked ZE
                  0xdf,0xe0,// FNSTSW AX
                  0x9b,0xf4});
  for(unsigned i=0;i<4;++i)one(cpu);
  need((cpu.fpu_status&4U)!=0 && !(cpu.fpu_status&0x80U),
       "masked x87 divide-by-zero must set ZE without ES");
  need(cpu.reg16(Cpu::EAX)==cpu.fpu_status,
       "guest FNSTSW did not expose sticky x87 ZE");
  need(cpu.x87[6][8]==0xff && cpu.x87[6][9]==0x7f,
       "masked x87 FDIV 1/0 must write +inf encoded extended80");
  need(cpu.step()==StepResult::Executed && cpu.step()==StepResult::Halted,
       "masked x87 exception must not trap FWAIT");
#if defined(__x86_64__) || defined(__i386__)
  alignas(16)std::array<std::uint8_t,512> preserved{};
  std::uint16_t native_status=0;
  asm volatile("fxsave %0":"=m"(preserved)::"memory");
  const float zero=0.0f;
  asm volatile("fninit; fld1; fdivs %1; fnstsw %0"
               :"=a"(native_status):"m"(zero):"memory");
  asm volatile("fxrstor %0"::"m"(preserved):"memory");
  need((native_status&0x85U)==(cpu.fpu_status&0x85U),
       "masked x87 divide-zero status differs from real x86 hardware");
#endif
  std::cout<<"C09R1_X87_MASKED_DIVZERO_ORACLE_PASS sticky_ze=true\n";
}
void deferred_mf(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  const auto img=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,img.data(),img.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<12000;++i){
    auto step=cpu.step();
    if(step==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(step==StepResult::Halted&&cpu.is_long_mode())break;
  }
  need(cpu.is_long_mode(),"x64 guest not initialized");
  cpu.cr0|=0x20U; // NE=1, unmasked x87 faults via #MF vector16
  cpu.fpu_control&=std::uint16_t(~4U); // unmask divide-by-zero
  cpu.idtr={0x1000,0x15000};
  constexpr std::uint64_t isr=0x13000;
  const auto gate=(isr&0xffffULL)|(std::uint64_t(0x18)<<16)|
      (std::uint64_t(0x8e)<<40)|((isr>>16)<<48);
  cpu.write64(cpu.idtr.base+16*16,gate);
  cpu.write64(cpu.idtr.base+16*16+8,0);
  // FNCLEX then IRETQ removes pending x87 condition; FWAIT retries.
  put(cpu,isr,{0xdb,0xe2,0xcf});
  put(cpu,0xa000,{0xd9,0xee,0xd9,0xe8,0xd8,0xf1,0x9b,0xf4});
  cpu.regs[Cpu::ESP]=0x190000;
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  one(cpu);one(cpu); // FLDZ, FLD1
  const auto before=cpu.x87[6]; // ST0=1, physical R6
  one(cpu); // FDIV unmasked, deferred pending
  need(cpu.eip==0xa006 && (cpu.fpu_status&0x8084U)==0x8084U,
       "unmasked FDIV must set ES/B/ZE before next FWAIT");
  need(cpu.x87[6]==before,
       "unmasked x87 arithmetic must not commit a destination");
  const auto saved_rsp=cpu.regs[Cpu::ESP];
  need(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==isr,
       "FWAIT did not enter x86 guest #MF vector16");
  one(cpu); // FNCLEX
  need(!(cpu.fpu_status&0x80U),"guest FNCLEX did not clear pending ES");
  one(cpu); // IRETQ
  need(cpu.eip==0xa006&&cpu.regs[Cpu::ESP]==saved_rsp,
       "#MF IRETQ didn't restart FWAIT with same RSP");
  one(cpu); // FWAIT, no exception
  need(cpu.eip==0xa007&&cpu.step()==StepResult::Halted,
       "after FNCLEX, FWAIT should complete then HLT");
  std::cout<<"C09R1_X87_DEFERRED_MF_FNCLEX_CONTEXT_PASS vector16=1\n";
}
}
int main(){
 try{masked();deferred_mf();
   std::cout<<"REVERSE_WINDOWS_C09R1_X87_MF_PASS checks="<<checks
            <<" windows_iso=false full_x87_cpuid=false\n";return 0;
 }catch(const std::exception& e){
   std::cerr<<"REVERSE_WINDOWS_C09R1_X87_MF_FAIL "<<e.what()<<"\n";return 1;
 }
}
