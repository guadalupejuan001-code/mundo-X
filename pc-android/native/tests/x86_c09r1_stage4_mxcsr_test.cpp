#include "../x86_cpu.hpp"
#include <array>
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
void need(bool condition,const char* why){++checks;if(!condition)throw std::runtime_error(why);}
std::uint64_t lane(const Cpu& cpu,unsigned r,unsigned at=0) {
  std::uint64_t result=0;for(unsigned j=0;j<8;++j)
    result|=std::uint64_t(cpu.xmm[r][at+j])<<(j*8);
  return result;
}
void lane(Cpu& cpu,unsigned r,unsigned at,std::uint64_t raw) {
  for(unsigned j=0;j<8;++j)cpu.xmm[r][at+j]=std::uint8_t(raw>>(j*8));
}
void opcode(Cpu& cpu,std::initializer_list<std::uint8_t> bytes) {
  std::vector<std::uint8_t> content(bytes);
  cpu.memory.write_block(0x1000,content.data(),content.size());
  cpu.eip=0x1000;cpu.halted=false;cpu.fault.reset();
}
void daz_oracle(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  const auto tiny=std::uint64_t{1};
  // Masked denormal operand: with DAZ=0 it passes through and sets DE.
  cpu.mxcsr=0x1f80;
  lane(cpu,0,0,0);lane(cpu,1,0,tiny);
  opcode(cpu,{0xf2,0x0f,0x58,0xc1});
  need(cpu.step()==StepResult::Executed&&!cpu.fault,
       "denormal ADDSD with masked DE failed");
  need(lane(cpu,0)==tiny && (cpu.mxcsr&2U)!=0,
       "DAZ=0 must preserve tiny operand and set DE");
#if defined(__x86_64__) || defined(__i386__)
  const auto prior=_mm_getcsr();
  {
    _mm_setcsr(0x1f80);
    double a=0.0,b{};
    std::memcpy(&b,&tiny,8);
    asm volatile("addsd %1,%0":"+x"(a):"x"(b));
    std::uint64_t hw=0;std::memcpy(&hw,&a,8);
    const auto flags=_mm_getcsr()&0x3fU;
    _mm_setcsr(prior);
    need(hw==lane(cpu,0) && flags==(cpu.mxcsr&0x3fU),
         "DAZ=0 guest operand flags/results differ from physical x86");
  }
#endif
  cpu.mxcsr=0x1fc0; // DAZ=1 (bit6), masked
  lane(cpu,0,0,0);lane(cpu,1,0,tiny);
  opcode(cpu,{0xf2,0x0f,0x58,0xc1});
  need(cpu.step()==StepResult::Executed&&!cpu.fault,
       "DAZ=1 ADDSD failed");
  need(lane(cpu,0)==0 && (cpu.mxcsr&2U)==0,
       "DAZ must treat tiny source as signed zero without setting DE");
#if defined(__x86_64__) || defined(__i386__)
  {
    _mm_setcsr(0x1fc0);
    double a=0.0,b{};std::memcpy(&b,&tiny,8);
    asm volatile("addsd %1,%0":"+x"(a):"x"(b));
    std::uint64_t hw{};std::memcpy(&hw,&a,8);
    const auto flags=_mm_getcsr()&0x3fU;
    _mm_setcsr(prior);
    need(hw==lane(cpu,0) && flags==(cpu.mxcsr&0x3fU),
         "DAZ=1 guest result/status differ from real x86");
  }
#endif
  std::cout<<"C09R1_STAGE4_DAZ_X86_ORACLE_PASS no_false_DE=true\n";
}
void ftz_oracle(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  const auto min_normal=0x0010000000000000ULL;
  const auto half=0x3fe0000000000000ULL;
  cpu.mxcsr=0x1f80;
  lane(cpu,0,0,min_normal);lane(cpu,1,0,half);
  opcode(cpu,{0xf2,0x0f,0x59,0xc1}); // MULSD tiny result
  need(cpu.step()==StepResult::Executed&&!cpu.fault,
       "gradual-underflow MULSD failed");
  const auto gradual=lane(cpu,0);
  need(gradual==0x0008000000000000ULL && (cpu.mxcsr&0x3fU)==0,
       "x86 golden: gradual exact tiny result, no exception flags");
#if defined(__x86_64__) || defined(__i386__)
  const auto prior=_mm_getcsr();
  {
    _mm_setcsr(0x1f80);
    double a{},b{};std::memcpy(&a,&min_normal,8);std::memcpy(&b,&half,8);
    asm volatile("mulsd %1,%0":"+x"(a):"x"(b));
    std::uint64_t hw{};std::memcpy(&hw,&a,8);
    const auto flags=_mm_getcsr()&0x3fU;_mm_setcsr(prior);
    need(gradual==hw&&(cpu.mxcsr&0x3fU)==flags,
         "gradual underflow result/flags differ from physical x86");
  }
#endif
  cpu.mxcsr=0x9f80; // masked underflow + FTZ
  lane(cpu,0,0,min_normal);lane(cpu,1,0,half);
  opcode(cpu,{0xf2,0x0f,0x59,0xc1});
  need(cpu.step()==StepResult::Executed&&!cpu.fault,
       "FTZ masked MULSD failed");
  need(lane(cpu,0)==0 && (cpu.mxcsr&0x3fU)==0x30,
       "x86 golden: masked FTZ flushes exact tiny result with UE+PE");
#if defined(__x86_64__) || defined(__i386__)
  {
    _mm_setcsr(0x9f80);
    double a{},b{};std::memcpy(&a,&min_normal,8);std::memcpy(&b,&half,8);
    asm volatile("mulsd %1,%0":"+x"(a):"x"(b));
    std::uint64_t hw{};std::memcpy(&hw,&a,8);
    const auto flags=_mm_getcsr()&0x3fU;_mm_setcsr(prior);
    need(lane(cpu,0)==hw && (cpu.mxcsr&0x3fU)==flags,
         "masked FTZ result/underflow flags disagree with physical x86");
  }
#endif
  for(const auto control:{0x1780U,0x9780U}) { // Underflow unmasked, FTZ off/on.
    for(const auto factor:{half,half+1}) { // Exact and inexact tiny results.
      cpu.mxcsr=control;
      lane(cpu,0,0,min_normal);lane(cpu,1,0,factor);
      const auto before=cpu.xmm[0];
      opcode(cpu,{0xf2,0x0f,0x59,0xc1});
      need(cpu.step()==StepResult::Faulted && cpu.fault &&
           cpu.fault->code==cosmic_pc::FaultCode::SimdFloatingPoint,
           "x86 golden: exact and inexact tiny MULSD must deliver #XM");
      need(cpu.xmm[0]==before && (cpu.mxcsr&0x3fU)==0x10U,
           "x86 golden: unmasked underflow keeps XMM and reports only UE");
    }
  }
  std::cout<<"C09R1_STAGE4_FTZ_MASKED_X86_ORACLE_PASS\n";
}
void packed_exception_union(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  lane(cpu,0,0,0x3ff0000000000000ULL); // 1/0 -> ZE
  lane(cpu,0,8,0);                     // 0/0 -> IE
  lane(cpu,1,0,0);lane(cpu,1,8,0);
  const auto before=cpu.xmm[0];
  cpu.mxcsr=0x1f80&~((1U<<7)|(1U<<9)); // unmask IE and ZE
  opcode(cpu,{0x66,0x0f,0x5e,0xc1}); // DIVPD xmm0,xmm1
  const auto result=cpu.step();
  need(result==StepResult::Faulted && cpu.fault &&
       cpu.fault->code==cosmic_pc::FaultCode::SimdFloatingPoint,
       "unmasked packed two-lane IE/ZE must fault as #XM");
  need((cpu.mxcsr&5U)==5U && cpu.xmm[0]==before,
       "packed #XM must aggregate both lane flags, preserving XMM");
  std::cout<<"C09R1_STAGE4_PACKED_IE_ZE_UNION_PASS first_and_second_lane=true\n";
}
void nan_compare(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  const auto snan=0x7ff0000000000001ULL;
  lane(cpu,0,0,0x3ff0000000000000ULL);
  lane(cpu,1,0,snan);
  cpu.mxcsr=0x1f80;
  opcode(cpu,{0xf2,0x0f,0x5d,0xc1}); // MINSD first finite, second SNaN
  need(cpu.step()==StepResult::Executed && !cpu.fault,
       "masked SNaN MINSD must not stop guest");
  need(lane(cpu,0)==snan && (cpu.mxcsr&1U)!=0,
       "MINSD must select second source payload and mark signaling NaN");
  const auto qnan=0x7ff8000000000001ULL;
  cpu.mxcsr=0x1f80;
  lane(cpu,0,0,1);lane(cpu,1,0,qnan);
  opcode(cpu,{0xf2,0x0f,0x5d,0xc1});
  need(cpu.step()==StepResult::Executed && lane(cpu,0)==qnan &&
       (cpu.mxcsr&0x3fU)==1U,"MINSD QNaN must signal IE and suppress DE");
#if defined(__x86_64__) || defined(__i386__)
  const auto prior=_mm_getcsr();
  _mm_setcsr(0x1f80);
  const std::uint64_t subnormal=1;
  double a{},b{};std::memcpy(&a,&subnormal,8);std::memcpy(&b,&qnan,8);
  asm volatile("minsd %1,%0":"+x"(a):"x"(b));
  std::uint64_t hw{};std::memcpy(&hw,&a,8);
  const auto flags=_mm_getcsr()&0x3fU;
  _mm_setcsr(prior);
  need(hw==lane(cpu,0) && flags==(cpu.mxcsr&0x3fU),
       "MINSD QNaN result/status differs from physical x86");
#endif
  cpu.mxcsr=0x1f80;
  lane(cpu,0,0,1);lane(cpu,1,0,qnan);
  opcode(cpu,{0xf2,0x0f,0x58,0xc1});
  need(cpu.step()==StepResult::Executed && lane(cpu,0)==qnan &&
       (cpu.mxcsr&0x3fU)==0,"ADDSD QNaN must suppress DE and preserve its payload");
  cpu.mxcsr=0x1f80;
  lane(cpu,0,0,snan);lane(cpu,1,0,0xfff8000000000002ULL);
  opcode(cpu,{0xf2,0x0f,0x58,0xc1});
  need(cpu.step()==StepResult::Executed && lane(cpu,0)==qnan &&
       (cpu.mxcsr&0x3fU)==1U,"ADDSD must quiet the first NaN and report IE");
  cpu.mxcsr=0x1f80;
  lane(cpu,0,0,1);lane(cpu,1,0,qnan);
  opcode(cpu,{0x66,0x0f,0x2e,0xc1});
  need(cpu.step()==StepResult::Executed && (cpu.mxcsr&0x3fU)==0 &&
       (cpu.eflags&(Cpu::ZF|Cpu::PF|Cpu::CF))==(Cpu::ZF|Cpu::PF|Cpu::CF),
       "UCOMISD QNaN must report unordered without IE or DE");
  std::cout<<"C09R1_STAGE4_SNAN_PAYLOAD_PASS payload_exact=true\n";
}
void host_controls_do_not_change_guest(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  std::fenv_t saved{};
  need(std::fegetenv(&saved)==0,"could not save the host FP environment");
#if defined(__x86_64__) || defined(__i386__)
  const auto host_control=(_mm_getcsr()|0x9fc0U)&~(1U<<9);
  _mm_setcsr(host_control); // DAZ/FTZ on, divide-by-zero trap unmasked.
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
  std::uint64_t host_control=0;
  asm volatile("mrs %0, fpcr":"=r"(host_control)::"memory");
  host_control|=1ULL<<24;
  asm volatile("msr fpcr, %0"::"r"(host_control):"memory");
#endif
  lane(cpu,0,0,0);lane(cpu,1,0,1);
  opcode(cpu,{0xf2,0x0f,0x58,0xc1});
  const bool add=cpu.step()==StepResult::Executed && lane(cpu,0)==1 &&
                 (cpu.mxcsr&0x3fU)==2U;
  lane(cpu,0,0,1);lane(cpu,1,0,0);
  opcode(cpu,{0x66,0x0f,0x2e,0xc1});
  const bool compare=cpu.step()==StepResult::Executed &&
      (cpu.eflags&(Cpu::ZF|Cpu::PF|Cpu::CF))==0;
  lane(cpu,0,0,0x3ff0000000000000ULL);
  opcode(cpu,{0xf2,0x0f,0x5e,0xc1});
  const bool divide=cpu.step()==StepResult::Executed &&
                    lane(cpu,0)==0x7ff0000000000000ULL && (cpu.mxcsr&4U);
  bool restored=true;
#if defined(__x86_64__) || defined(__i386__)
  restored=_mm_getcsr()==host_control;
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
  std::uint64_t current=0;
  asm volatile("mrs %0, fpcr":"=r"(current)::"memory");
  restored=current==host_control;
#endif
  (void)std::fesetenv(&saved);
  need(add && compare,"host flush controls must not change a guest subnormal");
  need(divide,"host unmasked divide-by-zero must remain isolated from the guest");
  need(restored,"guest FP execution must restore host FP controls");
}
}
int main(){
  try{
    daz_oracle();ftz_oracle();packed_exception_union();nan_compare();
    host_controls_do_not_change_guest();
    std::cout<<"REVERSE_WINDOWS_C09R1_STAGE4_MXCSR_PASS checks="<<checks
             <<" iso_used=false\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"REVERSE_WINDOWS_C09R1_STAGE4_MXCSR_FAIL "<<error.what()<<"\n";
    return 1;
  }
}
