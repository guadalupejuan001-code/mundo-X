#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void need(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
void guest64(Cpu& cpu){
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20 BIOS");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<12000;++i){
    const auto result=cpu.step();
    if(result==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(result==StepResult::Halted&&cpu.is_long_mode())break;
  }
  need(cpu.is_long_mode(),"test did not enter actual IA-32e mode");
  cpu.cr4|=0x200;cpu.halted=false;cpu.regs[Cpu::ESP]=0x190000;
}
std::uint64_t bits(double v){
  std::uint64_t raw;std::memcpy(&raw,&v,8);return raw;
}
double number(std::uint64_t b){
  double result;std::memcpy(&result,&b,8);return result;
}
void lane(Cpu& cpu,unsigned n,unsigned offset,std::uint64_t raw){
  for(unsigned i=0;i<8;++i)cpu.xmm[n][offset+i]=std::uint8_t(raw>>(i*8));
}
std::uint64_t lane(const Cpu& cpu,unsigned n,unsigned offset=0){
  std::uint64_t raw=0;
  for(unsigned i=0;i<8;++i)raw|=std::uint64_t(cpu.xmm[n][offset+i])<<(8*i);
  return raw;
}
void run(Cpu& cpu,std::initializer_list<std::uint8_t> bytes){
  std::vector<std::uint8_t> code(bytes);
  cpu.memory.write_block(0xa000,code.data(),code.size());
  cpu.eip=0xa000;cpu.fault.reset();cpu.halted=false;
  const auto status=cpu.step();
  if(status!=StepResult::Executed||cpu.fault)
    throw std::runtime_error("SSE2 instruction: "+(cpu.fault?cpu.fault->describe():std::string("not executed")));
}
void arithmetic_and_oracle(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);guest64(cpu);
  cpu.mxcsr=0x1f80;
  cpu.eflags=2|Cpu::IF|Cpu::ZF;
  lane(cpu,0,0,bits(1.5));lane(cpu,0,8,bits(13.25));
  lane(cpu,1,0,bits(2.0));lane(cpu,1,8,bits(7.5));
  run(cpu,{0xf2,0x0f,0x58,0xc1}); // ADDSD
  need(lane(cpu,0)==bits(3.5)&&lane(cpu,0,8)==bits(13.25),
       "ADDSD must change low64 only");
  need(cpu.eflags==(2|Cpu::IF|Cpu::ZF),"ADDSD illegally modified RFLAGS");
#if defined(__x86_64__) || defined(__i386__)
  double hw_add=1.5,hw_other=2.0;
  asm volatile("addsd %1,%0":"+x"(hw_add):"x"(hw_other));
  need(lane(cpu,0)==bits(hw_add),"ADDSD different from independent x86 CPU");
#endif
  run(cpu,{0xf2,0x0f,0x5c,0xc1}); // SUBSD 3.5-2=1.5
  need(lane(cpu,0)==bits(1.5),"SUBSD low64 error");
  run(cpu,{0xf2,0x0f,0x59,0xc1}); // MULSD 1.5*2=3
  need(lane(cpu,0)==bits(3.0),"MULSD low64 error");
  run(cpu,{0xf2,0x0f,0x5e,0xc1}); // DIVSD 3/2=1.5
  need(lane(cpu,0)==bits(1.5),"DIVSD low64 error");
  lane(cpu,1,0,bits(4.0));
  run(cpu,{0xf2,0x0f,0x51,0xc1}); // SQRTSD sqrt(4)=2
  need(lane(cpu,0)==bits(2.0)&&lane(cpu,0,8)==bits(13.25),
       "SQRTSD must preserve upper64 and use source operand");
  lane(cpu,0,0,bits(-0.0));lane(cpu,1,0,bits(+0.0));
  run(cpu,{0xf2,0x0f,0x5d,0xc1}); // MINSD equal returns second operand
  need(lane(cpu,0)==bits(+0.0),"MINSD signed-zero tie rule incorrect");
  lane(cpu,0,0,bits(-0.0));
  run(cpu,{0xf2,0x0f,0x5f,0xc1}); // MAXSD equal returns second operand
  need(lane(cpu,0)==bits(+0.0),"MAXSD signed-zero tie rule incorrect");
  // MOVSD register moves only low64, memory load zeros high64.
  lane(cpu,0,0,bits(99));lane(cpu,0,8,bits(123));
  lane(cpu,1,0,bits(3.5));
  run(cpu,{0xf2,0x0f,0x10,0xc1});
  need(lane(cpu,0)==bits(3.5)&&lane(cpu,0,8)==bits(123),
       "MOVSD xmm,xmm unexpectedly cleared high bits");
  cpu.write64(0xb000,bits(5.5));
  run(cpu,{0xf2,0x0f,0x10,0x04,0x25,0,0xb0,0,0});
  need(lane(cpu,0)==bits(5.5)&&lane(cpu,0,8)==0,
       "MOVSD xmm,m64 must zero high64");
  run(cpu,{0xf2,0x0f,0x11,0x04,0x25,0x08,0xb0,0,0});
  need(cpu.read64(0xb008)==bits(5.5),"MOVSD m64,xmm failed");
  // Packed low/high independent, strong proof no scalar accidentally reused.
  lane(cpu,0,0,bits(1.5));lane(cpu,0,8,bits(11));
  lane(cpu,1,0,bits(2));lane(cpu,1,8,bits(3));
  run(cpu,{0x66,0x0f,0x58,0xc1}); // ADDPD
  need(lane(cpu,0)==bits(3.5)&&lane(cpu,0,8)==bits(14),
       "ADDPD failed two independent 64bit lanes");
  run(cpu,{0x66,0x0f,0x5e,0xc1}); // DIVPD
  need(lane(cpu,0)==bits(1.75)&&lane(cpu,0,8)==bits(14.0/3),
       "DIVPD failed packed lane math");
  std::cout<<"C09R1_SSE2_BINARY64_ARITH_ORACLE_PASS scalar=1 packed=1\n";
}
void conversions_and_compare(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);guest64(cpu);
  lane(cpu,1,0,bits(3.5));
  cpu.mxcsr=0x1f80;
  run(cpu,{0xf2,0x0f,0x2d,0xc1}); // CVTSD2SI EAX,XMM1 nearest even
  need(cpu.regs[Cpu::EAX]==4&& (cpu.mxcsr&32U),
       "nearest-even CVTSD2SI 3.5 must round 4 with PE flag");
  cpu.mxcsr=0x1f80;
  lane(cpu,1,0,bits(-2.5));
  run(cpu,{0xf2,0x0f,0x2d,0xc1});
  need(std::int32_t(cpu.regs[Cpu::EAX])==-2,
       "nearest-even CVTSD2SI -2.5 must return -2");
  cpu.mxcsr=0x5f80; // RC=10 upwards
  lane(cpu,1,0,bits(1.1));
  run(cpu,{0xf2,0x0f,0x2d,0xc1});
  need(cpu.regs[Cpu::EAX]==2&&(cpu.mxcsr&32),
       "MXCSR round-up missing from CVTSD2SI");
  cpu.mxcsr=0x3f80; // RC=01 downwards
  run(cpu,{0xf2,0x0f,0x2d,0xc1});
  need(cpu.regs[Cpu::EAX]==1,"MXCSR round-down missing");
  lane(cpu,1,0,bits(-2.9));
  cpu.mxcsr=0x5f80;
  run(cpu,{0xf2,0x0f,0x2c,0xc1}); // CVTTSD2SI independent of RC
  need(std::int32_t(cpu.regs[Cpu::EAX])==-2&&(cpu.mxcsr&32),
       "truncating CVTTSD2SI disregarded MXCSR or lost PE");
  lane(cpu,1,0,bits(std::numeric_limits<double>::infinity()));
  cpu.mxcsr=0x1f80;
  run(cpu,{0xf2,0x0f,0x2d,0xc1});
  need(cpu.regs[Cpu::EAX]==0x80000000ULL&&(cpu.mxcsr&1),
       "infinite CVTSD2SI must return integer-indefinite and set IE");
  lane(cpu,1,0,bits(0.0));cpu.mxcsr=0x1f80;
  cpu.regs[Cpu::ECX]=std::uint64_t(0xffffffffULL);
  lane(cpu,0,8,bits(9.25));
  run(cpu,{0xf2,0x0f,0x2a,0xc1}); // CVTSI2SD xmm0,ecx (-1 signed)
  need(lane(cpu,0)==bits(-1.0)&&lane(cpu,0,8)==bits(9.25),
       "CVTSI2SD must convert signed32 preserving upper XMM");
  cpu.regs[Cpu::ECX]=9007199254740993ULL; // 2^53+1
  cpu.mxcsr=0x1f80;
  run(cpu,{0xf2,0x48,0x0f,0x2a,0xc1}); // CVTSI2SD xmm0,rcx
  need(lane(cpu,0)==bits(9007199254740992.0)&&(cpu.mxcsr&32U),
       "CVTSI2SD signed64 2^53+1 must round with precision exception");
#if defined(__x86_64__)
  double hw=0;std::int64_t input=9007199254740993LL;
  asm volatile("cvtsi2sdq %1,%0":"+x"(hw):"rm"(input));
  need(lane(cpu,0)==bits(hw),"CVTSI2SD disagrees with x86 oracle");
#endif
  lane(cpu,0,0,bits(1));lane(cpu,1,0,bits(2));
  cpu.eflags=2|Cpu::OF|Cpu::SF|Cpu::AF;
  run(cpu,{0x66,0x0f,0x2e,0xc1}); // UCOMISD XMM0,XMM1
  need(cpu.flag(Cpu::CF)&&!cpu.flag(Cpu::ZF)&&!cpu.flag(Cpu::PF)
       && !cpu.flag(Cpu::OF)&&!cpu.flag(Cpu::SF)&&!cpu.flag(Cpu::AF),
       "UCOMISD ordered less-than EFLAGS wrong");
  lane(cpu,1,0,bits(1));
  run(cpu,{0x66,0x0f,0x2e,0xc1});
  need(cpu.flag(Cpu::ZF)&&!cpu.flag(Cpu::CF),"UCOMISD equality flags wrong");
  lane(cpu,1,0,0x7ff8000000000000ULL); // QNaN
  cpu.mxcsr=0x1f80;
  run(cpu,{0x66,0x0f,0x2e,0xc1});
  need(cpu.flag(Cpu::ZF)&&cpu.flag(Cpu::CF)&&cpu.flag(Cpu::PF)&&
       !(cpu.mxcsr&1),"UCOMISD QNaN unordered must not assert IE");
  run(cpu,{0x66,0x0f,0x2f,0xc1}); // COMISD signaling all NaNs
  need((cpu.mxcsr&1)!=0,"COMISD QNaN must set invalid sticky IE");
  std::cout<<"C09R1_SSE2_CVT_COMISD_ROUNDING_PASS mxcsr_rc=1\n";
}
void exception_and_context(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);guest64(cpu);
  cpu.cr4|=0x400; // CR4.OSXMMEXCPT; OS installed #XM handler.
  lane(cpu,0,0,bits(1));lane(cpu,0,8,bits(101));
  lane(cpu,1,0,bits(0));
  cpu.mxcsr=0x1f80 & ~(1U<<9); // unmask DIVIDE BY ZERO
  cpu.idtr={0x1000,0x15000};
  constexpr std::uint64_t handler=0x13000;
  const auto gate=(handler&0xffffULL)|(std::uint64_t(0x18)<<16)|
      (std::uint64_t(0x8e)<<40)|((handler>>16)<<48);
  cpu.write64(cpu.idtr.base+19*16,gate);cpu.write64(cpu.idtr.base+19*16+8,0);
  const std::uint8_t isr[]{0x48,0x83,0x04,0x24,0x04,0xcf}; // ADD saved RIP,4; IRETQ
  cpu.memory.write_block(handler,isr,sizeof(isr));
  const std::uint8_t instruction[]{0xf2,0x0f,0x5e,0xc1,0xf4};
  cpu.memory.write_block(0xa000,instruction,sizeof(instruction));
  cpu.eip=0xa000;cpu.fault.reset();cpu.halted=false;
  const auto rsp=cpu.regs[Cpu::ESP];
  need(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==handler,
       "unmasked DIVSD must enter guest #XM vector19 via IDT");
  need((cpu.mxcsr&4)!=0&&lane(cpu,0)==bits(1)&&lane(cpu,0,8)==bits(101),
       "unmasked #XM must preserve MXCSR.ZE and not commit destination");
  need(cpu.step()==StepResult::Executed&&cpu.step()==StepResult::Executed,
       "guest #XM handler must advance saved RIP and IRETQ");
  need(cpu.eip==0xa004&&cpu.regs[Cpu::ESP]==rsp,
       "#XM IRETQ lost guest RIP or RSP");
  need(cpu.step()==StepResult::Halted,"guest resumed after #XM did not HLT");
  // When OS does not enable #XM, unmasked SIMD exception delivers #UD
  // but STILL commits sticky flag bits to MXCSR.
  cpu.mxcsr=0x1f80 & ~(1U<<9);cpu.cr4&=~0x400U;
  cpu.idtr={0x1000,0x15000};
  cpu.write64(cpu.idtr.base+6*16,gate);cpu.write64(cpu.idtr.base+6*16+8,0);
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  need(cpu.step()==StepResult::Executed&&cpu.eip==handler&&(cpu.mxcsr&4),
       "OSXMMEXCPT=0 should deliver #UD with sticky MXCSR.ZE");
  // FXSAVE contexts remain separable even after SIMD flags change.
  cpu.cr4|=0x400U;cpu.mxcsr=0x1f80;
  for(unsigned i=0;i<16;++i)cpu.xmm[0][i]=std::uint8_t(i+7);
  const std::array<std::uint8_t,9> fxsave{{0x48,0x0f,0xae,0x04,0x25,0,0x40,0,0}};
  cpu.memory.write_block(0xa000,fxsave.data(),fxsave.size());
  cpu.eip=0xa000;cpu.fault.reset();
  need(cpu.step()==StepResult::Executed,"FXSAVE64 after #XM failed");
  cpu.xmm[0].fill(0);cpu.mxcsr=0;
  const std::array<std::uint8_t,9> fxrstor{{0x48,0x0f,0xae,0x0c,0x25,0,0x40,0,0}};
  cpu.memory.write_block(0xa000,fxrstor.data(),fxrstor.size());
  cpu.eip=0xa000;cpu.fault.reset();
  need(cpu.step()==StepResult::Executed&&cpu.mxcsr==0x1f80,
       "FXRSTOR64 after #XM lost MXCSR state");
  for(unsigned i=0;i<16;++i)
    need(cpu.xmm[0][i]==i+7,"FXRSTOR64 after #XM lost XMM data");
  std::cout<<"C09R1_SSE2_IDT_XM_UD_FXSTATE_PASS vector19=1 vector6=1\n";
}
}
int main(){
  try {
    arithmetic_and_oracle();
    conversions_and_compare();
    exception_and_context();
    std::cout<<"REVERSE_WINDOWS_C09R1_SSE2_FP_PASS checks="<<checks
             <<" windows_iso_used=false sse2_cpuid=false x87_cpuid=false\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"REVERSE_WINDOWS_C09R1_SSE2_FP_FAIL "<<error.what()<<"\n";
    return 1;
  }
}
