#include "../x86_cpu.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void need(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
void execute(Cpu& cpu,bool wide){
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  const std::array<std::uint8_t,4> code=wide?
      std::array<std::uint8_t,4>{{0xdd,0x06,0x00,0x50}}:
      std::array<std::uint8_t,4>{{0xd9,0x06,0x00,0x50}};
  cpu.memory.write_block(0x1000,code.data(),code.size());
}
void verify_double(std::uint64_t raw){
  Cpu cpu(2*1024*1024);
  execute(cpu,true);
  cpu.write64(0x5000,raw);
  need(cpu.step()==StepResult::Executed&&!cpu.fault,"FLD m64real guest failed");
  need((cpu.fpu_tags>>14 & 3U)!=3U,"FLD m64real did not push physical R7");
#if defined(__x86_64__) || defined(__i386__)
  // Use the native x86 FXSAVE instruction as an independent exact oracle.
  // Preserve the runner process's FPU context before touching x87.
  alignas(16)std::array<std::uint8_t,512> prior{},observed{};
  double input{};
  std::memcpy(&input,&raw,8);
  asm volatile("fxsave %0":"=m"(prior)::"memory");
  asm volatile("fninit; fldl %1; fxsave %0":"=m"(observed):"m"(input):"memory");
  asm volatile("fxrstor %0"::"m"(prior):"memory");
  for(unsigned b=0;b<10;++b)
    need(cpu.x87[7][b]==observed[32+b],
         "Mundo X FLD m64 produced different extended80 bytes from host x87");
#endif
}
void verify_float(std::uint32_t raw){
  Cpu cpu(2*1024*1024);
  execute(cpu,false);
  cpu.write32(0x5000,raw);
  need(cpu.step()==StepResult::Executed&&!cpu.fault,"FLD m32real guest failed");
#if defined(__x86_64__) || defined(__i386__)
  alignas(16)std::array<std::uint8_t,512> prior{},observed{};
  float input{};std::memcpy(&input,&raw,4);
  asm volatile("fxsave %0":"=m"(prior)::"memory");
  asm volatile("fninit; flds %1; fxsave %0":"=m"(observed):"m"(input):"memory");
  asm volatile("fxrstor %0"::"m"(prior):"memory");
  for(unsigned b=0;b<10;++b)
    need(cpu.x87[7][b]==observed[32+b],
         "Mundo X FLD m32 produced different extended80 bytes from host x87");
#endif
}
void invalid_signaling_nan(){
  Cpu cpu(2*1024*1024);
  execute(cpu,true);
  cpu.write64(0x5000,0x7ff0000000000001ULL);
  need(cpu.step()==StepResult::Executed&&cpu.fpu_status==0x3801&&
       cpu.x87[7][1]==8&&cpu.x87[7][7]==0xc0&&
       cpu.x87[7][8]==0xff&&cpu.x87[7][9]==0x7f,
       "masked FLD must quiet a signaling binary64 NaN and retain its payload");
  execute(cpu,true);
  cpu.fpu_control&=std::uint16_t(~1U);
  need(cpu.step()==StepResult::Executed&&cpu.fpu_status==0x8081&&
       cpu.fpu_tags==0xffff&&cpu.fpu_opcode==0x506,
       "unmasked signaling NaN in FLD must defer invalid and leave the stack empty");
}
}
int main(){
  try{
    for(const std::uint64_t raw:{
      0ULL,0x8000000000000000ULL,0x3ff0000000000000ULL,
      0x3ff8000000000000ULL,0xc000000000000000ULL,
      0x0010000000000000ULL,0x0000000000000001ULL,
      0x000fffffffffffffULL,0x7fefffffffffffffULL,
      0x7ff0000000000000ULL})
      verify_double(raw);
    for(const std::uint32_t raw:{
      0U,0x80000000U,0x3f800000U,0x3fc00000U,0xbf400000U,
      0x00800000U,0x00000001U,0x007fffffU,0x7f7fffffU,0x7f800000U})
      verify_float(raw);
    invalid_signaling_nan();
    std::cout<<"C09R1_X87_EXACT_IEEE_TO_EXT80_ORACLE_PASS doubles=10 singles=10\n";
    std::cout<<"REVERSE_WINDOWS_C09R1_X87_PRECISION_PASS checks="<<checks
             <<" iso_used=false x87_cpuid=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_X87_PRECISION_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
