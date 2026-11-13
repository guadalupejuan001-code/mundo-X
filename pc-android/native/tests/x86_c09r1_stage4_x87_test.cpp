#include "../x86_cpu.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void need(bool yes,const char* why){++checks;if(!yes)throw std::runtime_error(why);}
void execute(Cpu& cpu,std::initializer_list<std::uint8_t> bytes){
  const std::vector<std::uint8_t> code(bytes);
  cpu.memory.write_block(0x1000,code.data(),code.size());
  cpu.eip=0x1000;cpu.fault.reset();cpu.halted=false;
  auto result=cpu.step();
  if(result!=StepResult::Executed)throw std::runtime_error(
     cpu.fault?cpu.fault->describe():"x87 guest opcode did not retire");
}
std::uint16_t lo16(const std::array<std::uint8_t,512>& b,unsigned i){
  return std::uint16_t(b[i])|std::uint16_t(b[i+1])<<8;
}
void stack_faults(){
  Cpu cpu(2*1024*1024);cpu.reset(0,0x1000);cpu.a20_enabled=true;
  for(unsigned i=0;i<8;++i)execute(cpu,{0xd9,0xe8}); // FLD1
  need(cpu.fpu_tags==0&&((cpu.fpu_status>>11)&7U)==0,
       "eight x87 pushes must fill every physical slot");
  execute(cpu,{0xd9,0xe8}); // ninth, masked stack overflow
  need(cpu.fpu_status==0x3a41&&((cpu.fpu_tags>>14)&3U)==2,
       "masked ninth FLD1: FSW IE+SF+C1, TOP7 and indefinite FTW");
  need(cpu.x87[7][7]==0xc0&&cpu.x87[7][8]==0xff&&
       cpu.x87[7][9]==0xff,"masked x87 overflow must push canonical 80-bit indefinite");
#if defined(__x86_64__) || defined(__i386__)
  alignas(16)std::array<std::uint8_t,512> backup{},observed{};
  asm volatile("fxsave %0":"=m"(backup)::"memory");
  asm volatile("fninit; fld1; fld1; fld1; fld1; fld1; fld1; fld1; fld1; fld1; fxsave %0"
               :"=m"(observed)::"memory");
  asm volatile("fxrstor %0"::"m"(backup):"memory");
  need(lo16(observed,2)==cpu.fpu_status && observed[4]==0xff,
       "masked x87 stack overflow differs from x86 hardware FSW/FTW");
  for(unsigned i=0;i<10;++i)
    need(observed[32+i]==cpu.x87[7][i],
         "masked x87 overflow indefinite ST0 differs from x86 hardware");
#endif
  cpu.reset(0,0x1000);
  execute(cpu,{0xdd,0xd8}); // FSTP ST0 with empty stack, masked invalid
  need(cpu.fpu_status==0x0841&&cpu.fpu_tags==0xffff,
       "masked empty FSTP ST0 must set IE/SF, C1=0, advance TOP1");
#if defined(__x86_64__) || defined(__i386__)
  {
    alignas(16)std::array<std::uint8_t,512> backup{},observed{};
    asm volatile("fxsave %0":"=m"(backup)::"memory");
    asm volatile("fninit; fstp %%st(0); fxsave %0"
                 :"=m"(observed)::"memory");
    asm volatile("fxrstor %0"::"m"(backup):"memory");
    need(lo16(observed,2)==cpu.fpu_status&&observed[4]==0,
         "masked empty x87 FSTP FSW/FTW differs from physical CPU");
  }
#endif
  cpu.reset(0,0x1000);cpu.fpu_control&=std::uint16_t(~1U);
  for(unsigned i=0;i<9;++i)execute(cpu,{0xd9,0xe8});
  need(cpu.fpu_status==0x82c1&&((cpu.fpu_status>>11)&7U)==0,
       "unmasked stack overflow must set pending ES/B without changing TOP");
  cpu.reset(0,0x1000);cpu.fpu_control&=std::uint16_t(~1U);
  execute(cpu,{0xdd,0xd8});
  need(cpu.fpu_status==0x80c1&&cpu.fpu_tags==0xffff,
       "unmasked stack underflow must preserve empty stack and set ES/B");
  std::cout<<"C09R1_STAGE4_X87_STACK_HW_ORACLE_PASS masked_unmasked=true\n";
}
void precision_mode(std::uint16_t control){
  Cpu cpu(2*1024*1024);cpu.reset(0,0x1000);cpu.a20_enabled=true;
  const std::uint32_t tiny=0x33000000U; // +2^-25, normal IEEE32
  cpu.write16(0x6000,control);
  cpu.write32(0x5000,tiny);
  execute(cpu,{0xd9,0x2e,0x00,0x60}); // FLDCW [6000]
  execute(cpu,{0xd9,0xe8});           // FLD1
  execute(cpu,{0xd9,0x06,0x00,0x50}); // FLD m32real (2^-25)
  execute(cpu,{0xd8,0xc1});           // FADD ST0,ST1
  need(cpu.fpu_control==control,"x87 FLDCW failed to install PC/RC");
  need(((cpu.fpu_status>>11)&7U)==6,"x87 FADD unexpectedly changed TOP");
#if defined(__x86_64__) || defined(__i386__)
  alignas(16)std::array<std::uint8_t,512> backup{},observed{};
  float input{};std::memcpy(&input,&tiny,4);
  asm volatile("fxsave %0":"=m"(backup)::"memory");
  asm volatile("fninit; fldcw %1; fld1; flds %2; fadd %%st(1),%%st(0); fxsave %0"
               :"=m"(observed):"m"(control),"m"(input):"memory");
  asm volatile("fxrstor %0"::"m"(backup):"memory");
  for(unsigned i=0;i<10;++i)
    need(cpu.x87[6][i]==observed[32+i],
         "x87 PC/RC rounded extended80 significand differs from native x86");
  need((cpu.fpu_status&0x3a3fU)==(lo16(observed,2)&0x3a3fU),
       "x87 PC/RC precision/round-up/TOP status differs from native x86");
#endif
}
void modes(){
  for(const auto cw:{std::uint16_t{0x007f},std::uint16_t{0x027f},
                     std::uint16_t{0x037f},std::uint16_t{0x047f},
                     std::uint16_t{0x087f},std::uint16_t{0x0c7f}})
    precision_mode(cw);
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  cpu.fpu_control=0x017f; // reserved precision PC=01
  execute(cpu,{0xd9,0xe8});execute(cpu,{0xd9,0xe8});
  std::vector<std::uint8_t> code{0xd8,0xc1};
  cpu.memory.write_block(0x1000,code.data(),code.size());
  cpu.eip=0x1000;cpu.fault.reset();
  need(cpu.step()==StepResult::Faulted&&cpu.fault &&
       cpu.fault->code==cosmic_pc::FaultCode::UnsupportedState,
       "reserved precision control must fail visibly, not invent result");
  std::cout<<"C09R1_STAGE4_X87_PC_RC_HW_ORACLE_PASS precision_modes=6\n";
}
}
int main(){
  try{
    stack_faults();modes();
    std::cout<<"REVERSE_WINDOWS_C09R1_STAGE4_X87_PASS checks="<<checks
             <<" windows_iso_used=false full_x87_cpuid=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_STAGE4_X87_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
