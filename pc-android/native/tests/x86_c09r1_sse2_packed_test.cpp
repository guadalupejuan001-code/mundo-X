#include "../x86_cpu.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void ok(bool success,const char* reason){++checks;if(!success)throw std::runtime_error(reason);}
void step(Cpu& cpu,std::initializer_list<std::uint8_t> code){
  const std::vector<std::uint8_t> body(code);
  cpu.memory.write_block(0x1000,body.data(),body.size());
  cpu.eip=0x1000;cpu.halted=false;cpu.fault.reset();
  const auto result=cpu.step();
  if(result!=StepResult::Executed)throw std::runtime_error(
    cpu.fault?cpu.fault->describe():"expected executed SIMD instruction");
}
void packed_integer(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  for(unsigned i=0;i<16;++i){
    cpu.xmm[0][i]=std::uint8_t(i);
    cpu.xmm[1][i]=std::uint8_t(15-i);
  }
  step(cpu,{0x66,0x0f,0xdb,0xc1}); // PAND XMM0,XMM1
  for(unsigned i=0;i<16;++i)
    ok(cpu.xmm[0][i]==(i&(15-i)),"PAND wrong byte");
  cpu.xmm[0].fill(0xf0);cpu.xmm[1].fill(0x0f);
  step(cpu,{0x66,0x0f,0xdf,0xc1}); // PANDN
  for(auto b:cpu.xmm[0])ok(b==0x0f,"PANDN wrong");
  step(cpu,{0x66,0x0f,0xeb,0xc1}); // POR
  for(auto b:cpu.xmm[0])ok(b==0x0f,"POR wrong");
  cpu.xmm[0].fill(0xff);cpu.xmm[1].fill(2);
  step(cpu,{0x66,0x0f,0xfc,0xc1}); // PADDB
  for(auto b:cpu.xmm[0])ok(b==1,"PADDB unsigned byte wrap wrong");
  cpu.xmm[0].fill(0);cpu.xmm[1].fill(0);
  cpu.xmm[0][0]=0xff;cpu.xmm[0][1]=0xff;
  cpu.xmm[1][0]=2;cpu.xmm[1][1]=0;
  step(cpu,{0x66,0x0f,0xfd,0xc1}); // PADDW
  ok(cpu.xmm[0][0]==1&&cpu.xmm[0][1]==0,"PADDW 16bit overflow wrong");
  cpu.xmm[0].fill(0);cpu.xmm[1].fill(0);
  cpu.xmm[0][0]=1;cpu.xmm[1][0]=2;
  step(cpu,{0x66,0x0f,0xfa,0xc1}); // PSUBD
  ok(cpu.xmm[0][0]==0xff&&cpu.xmm[0][1]==0xff &&
     cpu.xmm[0][2]==0xff&&cpu.xmm[0][3]==0xff,
     "PSUBD 32bit underflow wrong");
  cpu.xmm[0].fill(1);cpu.xmm[1].fill(1);cpu.xmm[1][3]=0;
  step(cpu,{0x66,0x0f,0x74,0xc1}); // PCMPEQB
  for(unsigned i=0;i<16;++i)
    ok(cpu.xmm[0][i]==(i==3?0:0xff),"PCMPEQB mask wrong");
  cpu.xmm[0].fill(0);cpu.xmm[1].fill(0);
  cpu.xmm[0][0]=1;cpu.xmm[1][0]=0xff;cpu.xmm[1][1]=0xff;
  step(cpu,{0x66,0x0f,0x65,0xc1}); // PCMPGTW signed
  ok(cpu.xmm[0][0]==0xff&&cpu.xmm[0][1]==0xff&&
     cpu.xmm[0][2]==0,"PCMPGTW signed comparison wrong");
  for(unsigned i=0;i<16;++i){
    cpu.xmm[0][i]=std::uint8_t(i);cpu.xmm[1][i]=std::uint8_t(0x80+i);
  }
  step(cpu,{0x66,0x0f,0x60,0xc1}); // PUNPCKLBW
  for(unsigned i=0;i<8;++i)
    ok(cpu.xmm[0][i*2]==i&&cpu.xmm[0][i*2+1]==0x80+i,
       "PUNPCKLBW wrong source position");
  for(unsigned lane=0;lane<4;++lane){
    for(unsigned byte=0;byte<4;++byte)cpu.xmm[1][lane*4+byte]=std::uint8_t(10*lane+byte);
  }
  step(cpu,{0x66,0x0f,0x70,0xc1,0x1b}); // PSHUFD reverse four dwords
  for(unsigned lane=0;lane<4;++lane)
    for(unsigned byte=0;byte<4;++byte)
      ok(cpu.xmm[0][lane*4+byte]==10*(3-lane)+byte,"PSHUFD reverse wrong");
  for(unsigned i=0;i<16;++i)cpu.xmm[0][i]=std::uint8_t(i+1);
  step(cpu,{0x66,0x0f,0x73,0xf8,0x04}); // PSLLDQ xmm0,4
  for(unsigned i=0;i<16;++i)
    ok(cpu.xmm[0][i]==(i<4?0:i-3),"PSLLDQ wrong");
  step(cpu,{0x66,0x0f,0x73,0xd8,0x04}); // PSRLDQ xmm0,4
  for(unsigned i=0;i<16;++i)
    ok(cpu.xmm[0][i]==(i<12?i+1:0),"PSRLDQ wrong");
  cpu.xmm[0].fill(0);cpu.xmm[1].fill(0);
  cpu.xmm[0][0]=0xff;cpu.xmm[0][1]=0x7f;
  cpu.xmm[1][0]=2;
  step(cpu,{0x66,0x0f,0xf1,0xc1}); // PSLLW xmm0,xmm1 count 2
  ok(cpu.xmm[0][0]==0xfc&&cpu.xmm[0][1]==0xff,"PSLLW wrong");
  cpu.xmm[0].fill(0);
  for(unsigned i=0;i<16;++i)cpu.xmm[1][i]=i&1?0:0x80;
  step(cpu,{0x66,0x0f,0xd7,0xc1}); // PMOVMSKB EAX,XMM1
  ok(cpu.regs[Cpu::EAX]==0x5555,"PMOVMSKB mask wrong");
  ok(cpu.eflags==2,"packed SSE2 unexpectedly changed EFLAGS");
  std::cout<<"C09R1_PACKED_INTEGER_SSE2_PASS operations=13\n";
}
void register_moves(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  cpu.regs[Cpu::ECX]=0x12345678;
  cpu.xmm[0].fill(0xff);
  step(cpu,{0x66,0x0f,0x6e,0xc1}); // MOVD XMM0,ECX
  ok(cpu.xmm[0][0]==0x78&&cpu.xmm[0][3]==0x12&&
     cpu.xmm[0][4]==0,"MOVD reg-to-XMM must zero high 96bits");
  cpu.regs[Cpu::ECX]=0;
  step(cpu,{0x66,0x0f,0x7e,0xc1}); // MOVD ECX,XMM0
  ok(cpu.regs[Cpu::ECX]==0x12345678,"MOVD XMM-to-GPR incorrect");
  cpu.xmm[0].fill(0xaa);cpu.xmm[1].fill(0);
  step(cpu,{0x66,0x0f,0xd6,0xc1}); // MOVQ XMM1,XMM0
  ok(cpu.xmm[1][0]==0xaa&&cpu.xmm[1][7]==0xaa&&
     cpu.xmm[1][8]==0,"MOVQ register destination must clear high64");
  cpu.xmm[0].fill(0);
  step(cpu,{0xf3,0x0f,0x7e,0xc1}); // MOVQ XMM0,XMM1
  ok(cpu.xmm[0][7]==0xaa&&cpu.xmm[0][8]==0,
     "F3 MOVQ from XMM register incorrect");
  for(unsigned i=0;i<16;++i)cpu.xmm[1][i]=std::uint8_t(i+1);
  step(cpu,{0x66,0x0f,0x10,0xc1}); // MOVUPD XMM0,XMM1
  ok(cpu.xmm[0]==cpu.xmm[1],"MOVUPD register-to-register wrong");
  step(cpu,{0x66,0x0f,0x29,0x06,0x00,0x40}); // MOVAPD [0x4000],XMM0
  for(unsigned i=0;i<16;++i)
    ok(cpu.memory[0x4000+i]==i+1,"MOVAPD memory store failed");
  std::cout<<"C09R1_SSE2_REGISTER_MEMORY_MOVES_PASS\n";
}
void fault_rollback(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  for(unsigned i=0;i<16;++i)cpu.xmm[0][i]=std::uint8_t(i+10);
  const auto before=cpu.xmm[0];
  const std::vector<std::uint8_t> insn{0x66,0x0f,0x29,0x06,0x01,0x40}; // MOVAPD misaligned
  cpu.memory.write_block(0x1000,insn.data(),insn.size());
  const auto status=cpu.step();
  ok(status==StepResult::Faulted&&cpu.fault&&
     cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,
     "aligned packed move must produce protection fault");
  ok(cpu.xmm[0]==before,"faulted packed move changed XMM state");
  ok(cpu.read32(0x4000)==0,"faulted packed move wrote guest memory");
  std::cout<<"C09R1_SSE2_ALIGNED_FAULT_ROLLBACK_PASS\n";
}
}
int main(){
  try{packed_integer();register_moves();fault_rollback();
    std::cout<<"REVERSE_WINDOWS_C09R1_SSE2_PACKED_PASS checks="<<checks
             <<" Windows10_ready=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_SSE2_PACKED_FAIL "<<e.what()<<"\n";return 1;
  }
}
