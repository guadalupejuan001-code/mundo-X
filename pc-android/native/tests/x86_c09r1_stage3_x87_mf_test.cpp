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
void deferred_mf_real_mode(){
  using cosmic_pc::FaultCode;
  constexpr std::uint16_t cs=0x0100, handler_cs=0x0300;
  constexpr std::uint16_t start=0x1000, wait_ip=start+11, handler_ip=0x0040;
  constexpr std::uint32_t ivt=0x400, slot=ivt+16*4, stack=0x8000;
  Cpu cpu(2*1024*1024);
  for(const bool interrupts_enabled:{false,true}) {
    cpu.reset(cs,start);cpu.a20_enabled=true;cpu.regs[Cpu::ESP]=stack;
    cpu.eflags=2|Cpu::CF|(interrupts_enabled?Cpu::IF:0U);
    const auto original_flags=cpu.eflags;
    cpu.cr0|=0x20U;cpu.fpu_control&=std::uint16_t(~4U);
    bool bios_called=false;
    cpu.interrupt_hook=[&](Cpu&,std::uint8_t){bios_called=true;return true;};
    cpu.write16(0x900,0x3ff);cpu.write32(0x902,ivt);
    cpu.write16(slot,handler_ip);cpu.write16(slot+2,handler_cs);
    put(cpu,(std::uint32_t(cs)<<4)+start,{0x0f,0x01,0x1e,0x00,0x09, // LIDT [0900h]
        0xd9,0xee,0xd9,0xe8,0xd8,0xf1,0x9b,0xf4}); // FLDZ, FLD1, FDIV, FWAIT, HLT
    put(cpu,(std::uint32_t(handler_cs)<<4)+handler_ip,{0xdb,0xe2,0xcf}); // FNCLEX, IRET
    for(unsigned i=0;i<4;++i)one(cpu);
    need(cpu.eip==wait_ip&&(cpu.fpu_status&0x8084U)==0x8084U,
         "real16 FDIV must defer the unmasked exception to FWAIT");
    need(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==handler_ip&&
         cpu.segments[Cpu::CS]==handler_cs&&!bios_called&&!(cpu.eflags&Cpu::IF),
         "real16 #MF must enter the relocated IVT handler regardless of IF");
    need(cpu.reg16(Cpu::ESP)==stack-6&&cpu.read16(stack-6)==wait_ip&&
         cpu.read16(stack-4)==cs&&cpu.read16(stack-2)==original_flags,
         "real16 #MF must stack the faulting IP, CS and FLAGS as words");
    one(cpu);one(cpu);
    need(cpu.eip==wait_ip&&cpu.segments[Cpu::CS]==cs&&cpu.reg16(Cpu::ESP)==stack&&
         cpu.eflags==original_flags&&!(cpu.fpu_status&0x80U),
         "FNCLEX and IRET must return to the faulting FWAIT with FLAGS restored");
    need(cpu.step()==StepResult::Executed&&cpu.step()==StepResult::Halted,
         "real16 FWAIT must retire after the exception is cleared");
    cpu.interrupt_hook={};
  }
  for(const bool empty_vector:{false,true}) {
    cpu.reset(cs,wait_ip);cpu.a20_enabled=true;cpu.idtr={0x3ff,ivt};
    cpu.cr0|=0x20U;cpu.fpu_status=0x8084U;cpu.fpu_control&=std::uint16_t(~4U);
    cpu.regs[Cpu::ESP]=empty_vector?stack:3;
    cpu.write16(slot,empty_vector?0:handler_ip);
    cpu.write16(slot+2,empty_vector?0:handler_cs);
    cpu.write8(1,0xa5);cpu.write8(2,0x5a);
    const auto old_sp=cpu.reg16(Cpu::ESP);
    need(cpu.step()==StepResult::Faulted&&cpu.fault&&cpu.fault->code==FaultCode::X87FloatingPoint&&
         cpu.eip==wait_ip&&cpu.reg16(Cpu::ESP)==old_sp&&cpu.fpu_status==0x8084U&&
         cpu.read8(1)==0xa5&&cpu.read8(2)==0x5a,
         "empty IVT or failed exception frame must preserve the original #MF and guest state");
  }
  std::cout<<"C02_X87_REAL16_MF_IVT_PASS relocated_ivt=1 flags=1 restart=1 rollback=1\n";
}
void real_mode_simd_exceptions(){
  using cosmic_pc::FaultCode;
  constexpr auto start=0x1000U, handler=0x2000U, ivt=0x400U, stack=0x8000U;
  Cpu cpu(2*1024*1024);
  for(const unsigned vector:{6U,7U,19U}) {
    cpu.reset(0,start);cpu.a20_enabled=true;cpu.idtr={0x3ff,ivt};
    cpu.regs[Cpu::ESP]=stack;
    cpu.cr4=vector==6?0x200U:0x600U;
    cpu.mxcsr=0x1d80;cpu.xmm[0][6]=0xf0;cpu.xmm[0][7]=0x3f; // DIVSD 1/0, ZE unmasked.
    cpu.write16(ivt+4*vector,handler);
    int traced_vector=-1;
    cpu.step_trace_hook=[&](const Cpu::StepTrace& trace) {
      traced_vector=trace.outcome=="exception-delivered"?trace.delivered_vector:-1;
    };
    const auto xmm_before=cpu.xmm;
    if(vector==7) {
      cpu.cr0|=8;
      put(cpu,start,{0x0f,0xae,0x06,0x00,0x30,0xf4}); // FXSAVE [3000h], HLT
      put(cpu,handler,{0x0f,0x06,0xcf}); // CLTS, IRET
    } else {
      put(cpu,start,{0xf2,0x0f,0x5e,0xc1}); // DIVSD xmm0,xmm1
      put(cpu,handler,{0xcf}); // The host test masks ZE before returning.
    }
    need(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==handler&&
         cpu.reg16(Cpu::ESP)==stack-6&&cpu.read16(stack-6)==start&&cpu.xmm==xmm_before&&
         traced_vector==int(vector),
         "real16 FP/SIMD fault must enter its IVT handler with unchanged XMM");
    if(vector==7)one(cpu);
    else {
      need((cpu.mxcsr&0x3fU)==4U,"real16 SIMD fault lost its sticky ZE flag");
      cpu.mxcsr|=1U<<9;
    }
    one(cpu);
    need(cpu.eip==start&&cpu.reg16(Cpu::ESP)==stack&&cpu.step()==StepResult::Executed,
         "real16 IRET must retry the faulting instruction");
    if(vector==7)need(cpu.read16(0x3000)==0x037f,"retried real16 FXSAVE did not write its image");
    cpu.step_trace_hook={};
  }

  cpu.reset(0,start);cpu.a20_enabled=true;cpu.idtr={0x3ff,ivt};
  cpu.regs[Cpu::ESP]=3;cpu.cr4=0x600;cpu.mxcsr=0x1d80;
  cpu.xmm[0][6]=0xf0;cpu.xmm[0][7]=0x3f;
  cpu.write8(1,0xa5);cpu.write8(2,0x5a);
  need(cpu.step()==StepResult::Faulted&&cpu.fault&&cpu.fault->code==FaultCode::SimdFloatingPoint&&
       (cpu.mxcsr&0x3fU)==4U&&cpu.eip==start&&cpu.reg16(Cpu::ESP)==3&&
       cpu.read8(1)==0xa5&&cpu.read8(2)==0x5a,
       "failed real16 #XM frame must preserve the original fault and sticky flag");
  std::cout<<"C02_REAL16_SIMD_IVT_PASS ud=6 nm=7 xm=19 sticky_rollback=1\n";
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
  put(cpu,isr,{0xdb,0xe2,0x48,0xcf});
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
 try{masked();deferred_mf_real_mode();real_mode_simd_exceptions();deferred_mf();
   std::cout<<"REVERSE_WINDOWS_C09R1_X87_MF_PASS checks="<<checks
            <<" windows_iso=false full_x87_cpuid=false\n";return 0;
 }catch(const std::exception& e){
   std::cerr<<"REVERSE_WINDOWS_C09R1_X87_MF_FAIL "<<e.what()<<"\n";return 1;
 }
}
