#include "../x86_cpu.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
static int checks=0;
static void ok(bool v,const char* reason){++checks;if(!v)throw std::runtime_error(reason);}
static void emit(Cpu& cpu,std::initializer_list<std::uint8_t> bytes){
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  std::vector<std::uint8_t> b(bytes);
  cpu.memory.write_block(0x1000,b.data(),b.size());
}
int main(){
 try {
  Cpu cpu(2*1024*1024);
  emit(cpu,{0x66,0x0f,0x6f,0x06,0x00,0x20}); // MOVDQA XMM0,[2000]
  for(unsigned i=0;i<16;++i)cpu.memory[0x2000+i]=std::uint8_t(i+1);
  ok(cpu.step()==StepResult::Faulted&&cpu.fault.has_value(),
     "OSFXSR=0 must reject SSE2 instruction");
  cpu.reset(0,0x1000);cpu.cr4=0x200;
  ok(cpu.step()==StepResult::Executed&&!cpu.fault,"MOVDQA load failed");
  for(unsigned i=0;i<16;++i)ok(cpu.xmm[0][i]==i+1,"MOVDQA changed byte");
  cpu.memory[0x2010]=0;
  cpu.memory.write_block(0x1000,std::array<std::uint8_t,6>{{0x66,0x0f,0x7f,0x06,0x10,0x20}}.data(),6);
  cpu.eip=0x1000;
  ok(cpu.step()==StepResult::Executed,"MOVDQA store failed");
  for(unsigned i=0;i<16;++i)ok(cpu.memory[0x2010+i]==i+1,"MOVDQA store wrong");
  cpu.memory.write_block(0x1000,std::array<std::uint8_t,6>{{0x66,0x0f,0xef,0x06,0x00,0x20}}.data(),6);
  cpu.eip=0x1000;
  ok(cpu.step()==StepResult::Executed,"PXOR failed");
  for(auto b:cpu.xmm[0])ok(b==0,"PXOR did not clear XMM");
  cpu.memory.write_block(0x1000,std::array<std::uint8_t,6>{{0x0f,0xae,0x16,0x00,0x20,0x90}}.data(),6);
  cpu.memory.write_byte(0x2000,0x80);cpu.memory.write_byte(0x2001,0x1f);
  cpu.memory.write_byte(0x2002,0);cpu.memory.write_byte(0x2003,0);
  cpu.eip=0x1000;
  ok(cpu.step()==StepResult::Executed&&cpu.mxcsr==0x1f80,"LDMXCSR failed");
  cpu.memory.write_block(0x1000,std::array<std::uint8_t,5>{{0x0f,0xae,0x1e,0x10,0x20}}.data(),5);
  cpu.eip=0x1000;
  ok(cpu.step()==StepResult::Executed&&cpu.memory[0x2010]==0x80,"STMXCSR failed");
  cpu.memory.write_byte(0x2003,0x80);
  cpu.memory.write_block(0x1000,std::array<std::uint8_t,5>{{0x0f,0xae,0x16,0x00,0x20}}.data(),5);
  cpu.eip=0x1000;
  ok(cpu.step()==StepResult::Faulted&&cpu.mxcsr==0x1f80,
     "reserved MXCSR bit must fault without changing state");
  cpu.reset(0,0x1000);
  ok(cpu.mxcsr==0x1f80&&cpu.xmm[0][0]==0&&cpu.cr4==0,
     "reset must clear XMM/OSFXSR and default MXCSR");
  std::cout<<"C09R1_XMM_MXCSR_SUBSET_PASS checks="<<checks
           <<" sse2_cpuid=0 fxsave=0 x87=0 windows_ready=0\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"C09R1_XMM_MXCSR_SUBSET_FAIL "<<e.what()<<"\n";return 1;
 }
}
