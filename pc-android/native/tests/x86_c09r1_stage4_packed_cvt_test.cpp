#include "../x86_cpu.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
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
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void setlane(Cpu& cpu,unsigned reg,unsigned offset,std::uint64_t bits){
  for(unsigned i=0;i<8;++i)cpu.xmm[reg][offset+i]=std::uint8_t(bits>>(8*i));
}
std::uint64_t bits(double x){std::uint64_t n{};std::memcpy(&n,&x,8);return n;}
std::uint32_t dword(const Cpu& cpu,unsigned reg,unsigned off) {
  std::uint32_t v=0;
  for(unsigned i=0;i<4;++i)v|=std::uint32_t(cpu.xmm[reg][off+i])<<(8*i);
  return v;
}
std::uint64_t qword(const Cpu& cpu,unsigned reg,unsigned off){
  std::uint64_t v=0;
  for(unsigned i=0;i<8;++i)v|=std::uint64_t(cpu.xmm[reg][off+i])<<(8*i);
  return v;
}
StepResult step(Cpu& cpu,std::initializer_list<std::uint8_t> op){
  std::vector<std::uint8_t> bytes(op);
  cpu.memory.write_block(0x1000,bytes.data(),bytes.size());
  cpu.eip=0x1000;cpu.halted=false;cpu.fault.reset();
  return cpu.step();
}
void conversions(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  cpu.mxcsr=0x1f80;
  cpu.xmm[1].fill(0);
  for(unsigned i=0;i<4;++i){
    cpu.xmm[1][i]=std::uint8_t(0xffffffffULL>>(i*8));
    cpu.xmm[1][4+i]=std::uint8_t(0x01000001ULL>>(i*8));
  }
  need(step(cpu,{0xf3,0x0f,0xe6,0xc1})==StepResult::Executed,
       "CVTDQ2PD failed to convert two signed32 integers");
  need(qword(cpu,0,0)==bits(-1.0)&&qword(cpu,0,8)==bits(16777217.0),
       "CVTDQ2PD must convert both integers without losing precision");
  need((cpu.mxcsr&0x3fU)==0,"CVTDQ2PD signed32→double is exact");
  setlane(cpu,1,0,bits(-2.5));setlane(cpu,1,8,bits(1.5));
  cpu.mxcsr=0x1f80;
  need(step(cpu,{0xf2,0x0f,0xe6,0xc1})==StepResult::Executed,
       "CVTPD2DQ failed");
  need(dword(cpu,0,0)==0xfffffffeU&&dword(cpu,0,4)==2U &&
       qword(cpu,0,8)==0&&(cpu.mxcsr&32U),
       "CVTPD2DQ nearest-even mask/lane/high64/PE incorrect");
  setlane(cpu,1,0,bits(-2.9));setlane(cpu,1,8,bits(1.9));
  cpu.mxcsr=0x5f80; // MXCSR round up (but CVTTPD2DQ must truncate)
  need(step(cpu,{0x66,0x0f,0xe6,0xc1})==StepResult::Executed,
       "CVTTPD2DQ failed");
  need(dword(cpu,0,0)==0xfffffffeU&&dword(cpu,0,4)==1U&&
       (cpu.mxcsr&32U),"CVTTPD2DQ must truncate independent of RC");
#if defined(__x86_64__) || defined(__i386__)
  alignas(16)std::array<std::uint64_t,2> source{bits(-2.9),bits(1.9)};
  alignas(16)std::array<std::uint32_t,4> native{};
  asm volatile("movapd %1,%%xmm0; cvttpd2dq %%xmm0,%%xmm1; movdqu %%xmm1,%0"
               :"=m"(native):"m"(source):"xmm0","xmm1","memory");
  need(native[0]==dword(cpu,0,0)&&native[1]==dword(cpu,0,4)&&
       native[2]==0&&native[3]==0,
       "CVTTPD2DQ output differs from real x86 SSE2 CPU");
#endif
  std::cout<<"C09R1_STAGE4_PACKED_CVT_X86_ORACLE_PASS two_lanes=true\n";
}
void exceptions(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x600;
  const std::uint64_t nan=0x7ff8000000000000ULL;
  for(const auto nan_lane:{0U,1U}) {
    setlane(cpu,1,nan_lane*8,nan);
    setlane(cpu,1,(1U-nan_lane)*8,bits(2.5));
    cpu.mxcsr=0x1f80;
    need(step(cpu,{0xf2,0x0f,0xe6,0xc1})==StepResult::Executed,
         "masked invalid CVTPD2DQ should complete");
    need(dword(cpu,0,nan_lane*4)==0x80000000U &&
         dword(cpu,0,(1U-nan_lane)*4)==2U && (cpu.mxcsr&0x3fU)==0x21U,
         "masked packed invalid and precision flags must be independent of lane order");
#if defined(__x86_64__) || defined(__i386__)
    alignas(16)std::array<std::uint64_t,2> input{};
    input[nan_lane]=nan;input[1U-nan_lane]=bits(2.5);
    alignas(16)std::array<std::uint32_t,4> native{};
    const auto saved=_mm_getcsr();_mm_setcsr(0x1f80);
    asm volatile("movapd %1,%%xmm0; cvtpd2dq %%xmm0,%%xmm1; movdqu %%xmm1,%0"
                 :"=m"(native):"m"(input):"xmm0","xmm1","memory");
    const auto flags=_mm_getcsr()&0x3fU;_mm_setcsr(saved);
    need(native[0]==dword(cpu,0,0) && native[1]==dword(cpu,0,4) &&
         flags==(cpu.mxcsr&0x3fU),
         "mixed IE/PE CVTPD2DQ differs from physical x86");
#endif
    const auto prior=cpu.xmm[0];
    cpu.mxcsr=0x1f80&~(1U<<7);
    need(step(cpu,{0xf2,0x0f,0xe6,0xc1})==StepResult::Faulted &&
         cpu.fault && cpu.fault->code==cosmic_pc::FaultCode::SimdFloatingPoint,
         "unmasked packed conversion invalid must raise #XM");
    need(cpu.xmm[0]==prior && (cpu.mxcsr&0x3fU)==1U,
         "unmasked IE traps before PE and preserves the destination");
  }
  setlane(cpu,1,0,0x0000000000000001ULL); // min positive subnormal
  setlane(cpu,1,8,bits(0));
  cpu.mxcsr=0x1f80&~(1U<<8); // Denormal operand is unmasked.
  need(step(cpu,{0xf2,0x0f,0xe6,0xc1})==StepResult::Executed &&
       dword(cpu,0,0)==0 && (cpu.mxcsr&0x3fU)==32U,
       "subnormal CVTPD2DQ must set PE without DE");
  cpu.mxcsr=0x1f80&~(1U<<8);
  need(step(cpu,{0xf2,0x0f,0x2d,0xc1})==StepResult::Executed &&
       (cpu.regs[Cpu::EAX]&0xffffffffU)==0 && (cpu.mxcsr&0x3fU)==32U,
       "subnormal CVTSD2SI must set PE without DE");
  cpu.mxcsr=0x1fc0;
  need(step(cpu,{0xf2,0x0f,0xe6,0xc1})==StepResult::Executed,
       "DAZ CVTPD2DQ failed");
  need((cpu.mxcsr&0x3fU)==0&&dword(cpu,0,0)==0,
       "DAZ must suppress PE for subnormal converted to exact zero");
  std::cout<<"C09R1_STAGE4_PACKED_CVT_EXCEPTIONS_PASS IE_PE=true denormal_operand_flag=false\n";
}
}
int main(){
  try{
    conversions();exceptions();
    std::cout<<"REVERSE_WINDOWS_C09R1_STAGE4_PACKED_CVT_PASS checks="<<checks
             <<" Windows_booted=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_STAGE4_PACKED_CVT_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
