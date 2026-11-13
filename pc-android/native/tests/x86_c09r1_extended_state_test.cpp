#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
unsigned checks=0;
void need(bool ok,const std::string& why) {
  ++checks;
  if(!ok)throw std::runtime_error(why);
}
void instruction(Cpu& cpu,std::initializer_list<std::uint8_t> code,
                 std::uint64_t address=0xa000) {
  const std::vector<std::uint8_t> bytes(code);
  cpu.memory.write_block(address,bytes.data(),bytes.size());
  cpu.eip=address;cpu.halted=false;cpu.fault.reset();
  const auto r=cpu.step();
  if(r==StepResult::Faulted)
    throw std::runtime_error(cpu.fault->describe());
  need(r==StepResult::Executed,"guest instruction did not execute");
}
void boot64(Cpu& cpu){
  auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)
      c.fail("unexpected BIOS during x64 boot");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);
    return true;
  };
  bool arrived=false;
  for(unsigned i=0;i<12000;++i) {
    const auto r=cpu.step();
    if(r==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(r==StepResult::Halted&&cpu.is_long_mode()){arrived=true;break;}
  }
  need(arrived,"x86 guest never entered long64");
  cpu.halted=false;
  cpu.regs[Cpu::ESP]=0x190000;
  // 220h = PAE + OSFXSR. Real guest writes control register.
  instruction(cpu,{0xb8,0x20,0x02,0x00,0x00});
  instruction(cpu,{0x0f,0x22,0xe0});
  need((cpu.cr4&0x220U)==0x220U,"guest MOV CR4 failed to set OSFXSR");
}
void x87_raw(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  instruction(cpu,{0xdb,0xe3},0x1000); // FNINIT
  need(cpu.fx.fcw==0x037f&&cpu.fx.fsw==0&&cpu.fx.ftw==0,
       "FNINIT did not initialize x87 architectural state");
  instruction(cpu,{0xd9,0xe8},0x1000); // FLD1 raw binary80
  need((cpu.fx.ftw&(1U<<7))!=0&&((cpu.fx.fsw>>11)&7)==7,
       "FLD1 did not push x87 physical stack slot 7");
  instruction(cpu,{0x67,0xdb,0x3d,0x00,0xb0,0x00,0x00},0x1000); // FSTP m80
  need(cpu.read8(0xb007)==0x80&&cpu.read16(0xb008)==0x3fff,
       "FSTP m80 did not save exact 80-bit 1.0 encoding");
  need(cpu.fx.ftw==0&&((cpu.fx.fsw>>11)&7)==0,
       "FSTP m80 did not pop x87 stack");
  instruction(cpu,{0x67,0xdb,0x2d,0x00,0xb0,0x00,0x00},0x1000); // FLD m80
  need(cpu.fx.st[7][7]==0x80&&cpu.fx.st[7][8]==0xff,
       "FLD m80 did not recover exact encoded value");
  cpu.write16(0xb100,0x027f);
  instruction(cpu,{0x67,0xd9,0x2d,0x00,0xb1,0x00,0x00},0x1000); // FLDCW
  instruction(cpu,{0x67,0xd9,0x3d,0x02,0xb1,0x00,0x00},0x1000); // FNSTCW
  need(cpu.read16(0xb102)==0x027f&&cpu.fx.fcw==0x027f,
       "FLDCW/FNSTCW did not preserve x87 control word");
  instruction(cpu,{0xdf,0xe0},0x1000);
  need(cpu.reg16(Cpu::EAX)==cpu.fx.fsw,"FNSTSW AX did not report FPU TOP");
  std::cout<<"C09R1_X87_RAW80_STACK_CONTROL_PASS\n";
}
void xmm_operations(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);boot64(cpu);
  std::array<std::uint8_t,16> a{},b{};
  for(unsigned i=0;i<16;++i){a[i]=std::uint8_t(i+10);b[i]=std::uint8_t(0xf0+i);}
  cpu.memory.write_block(0xb000,a.data(),16);
  cpu.memory.write_block(0xb100,b.data(),16);
  instruction(cpu,{0x66,0x0f,0x6f,0x04,0x25,0x00,0xb0,0,0}); // MOVDQA XMM0 [B000]
  need(cpu.fx.xmm[0]==a,"MOVDQA aligned memory load changed bytes");
  instruction(cpu,{0xf3,0x0f,0x6f,0x0c,0x25,0x00,0xb1,0,0}); // MOVDQU XMM1
  need(cpu.fx.xmm[1]==b,"MOVDQU memory load changed bytes");
  instruction(cpu,{0x66,0x0f,0xef,0xc1}); // PXOR xmm0,xmm1
  for(unsigned i=0;i<16;++i)need(cpu.fx.xmm[0][i]==(a[i]^b[i]),"PXOR mismatch");
  instruction(cpu,{0x66,0x0f,0x7f,0x04,0x25,0x00,0xc0,0,0});
  for(unsigned i=0;i<16;++i)
    need(cpu.read8(0xc000+i)==(a[i]^b[i]),"MOVDQA store mismatch");
  instruction(cpu,{0x66,0x0f,0xfc,0xc1}); // PADDB XMM0,XMM1
  for(unsigned i=0;i<16;++i)
    need(cpu.fx.xmm[0][i]==std::uint8_t((a[i]^b[i])+b[i]),
         "PADDB wraparound mismatch");
  instruction(cpu,{0x66,0x0f,0x70,0xc1,0x1b}); // PSHUFD XMM0,XMM1 reverse dwords
  for(unsigned i=0;i<4;++i)
    for(unsigned j=0;j<4;++j)
      need(cpu.fx.xmm[0][4*i+j]==b[4*(3-i)+j],"PSHUFD shuffle mismatch");
  instruction(cpu,{0x66,0x0f,0x73,0xf8,0x04}); // PSLLDQ XMM0,4
  for(unsigned i=0;i<16;++i)
    need(cpu.fx.xmm[0][i]==(i<4?0:b[4*(3-((i-4)/4))+((i-4)%4)]),
         "PSLLDQ byte shift mismatch");
  std::cout<<"C09R1_XMM128_INTEGER_SSE2_SUBSET_PASS\n";
}
void fxsave_fxrstor(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);boot64(cpu);
  for(unsigned i=0;i<16;++i) {
    for(unsigned j=0;j<16;++j)
      cpu.fx.xmm[i][j]=std::uint8_t(i*17+j);
  }
  cpu.fx.mxcsr=0x1f80;
  cpu.fx.fcw=0x027f;
  cpu.fx.ftw=0x80;
  cpu.fx.fsw=0x3800;
  cpu.fx.st[7][7]=0x80;cpu.fx.st[7][8]=0xff;cpu.fx.st[7][9]=0x3f;
  const auto page_walks_before=cpu.page_walks;
  instruction(cpu,{0x0f,0xae,0x04,0x25,0x00,0xc0,0,0}); // FXSAVE m512
  const auto fxsave_page_walks=cpu.page_walks-page_walks_before;
  need(fxsave_page_walks<180,
       "FXSAVE should walk per RAM page and 8-byte transfer, not 512 individual bytes");
  need(cpu.read16(0xc000)==0x027f&&cpu.read32(0xc018)==0x1f80,
       "FXSAVE did not serialize FCW or MXCSR");
  need(cpu.read32(0xc01c)==0xffbf,"FXSAVE MXCSR_MASK missing");
  for(unsigned i=0;i<16;++i)
    for(unsigned j=0;j<16;++j)
      need(cpu.read8(0xc000+160+i*16+j)==std::uint8_t(i*17+j),
           "FXSAVE did not preserve long64 XMM0..15 bytes");
  cpu.fx={};
  cpu.fx.xmm[0].fill(0xcc);
  instruction(cpu,{0x0f,0xae,0x0c,0x25,0x00,0xc0,0,0}); // FXRSTOR m512
  need(cpu.fx.mxcsr==0x1f80&&cpu.fx.fcw==0x027f&&
       cpu.fx.ftw==0x80&&cpu.fx.st[7][9]==0x3f,
       "FXRSTOR lost FPU status or 80-bit raw values");
  for(unsigned i=0;i<16;++i)
    for(unsigned j=0;j<16;++j)
      need(cpu.fx.xmm[i][j]==std::uint8_t(i*17+j),
           "FXRSTOR did not restore XMM0..15");
  // STMXCSR stores MXCSR; LDMXCSR changes it when reserved bits are clear.
  instruction(cpu,{0x0f,0xae,0x1c,0x25,0x00,0xc4,0,0});
  need(cpu.read32(0xc400)==0x1f80,"STMXCSR m32 did not save register");
  cpu.write32(0xc400,0x3f80);
  instruction(cpu,{0x0f,0xae,0x14,0x25,0x00,0xc4,0,0});
  need(cpu.fx.mxcsr==0x3f80,"LDMXCSR m32 did not update control register");
  cpu.fx.mxcsr=0x1f80;
  // MXCSR reserved bit must fault and roll back ALL architectural state.
  cpu.write32(0xc018,0x80000000U);
  const auto previous=cpu.fx;
  const std::vector<std::uint8_t> bad{0x0f,0xae,0x0c,0x25,0x00,0xc0,0,0};
  cpu.memory.write_block(0xa000,bad.data(),bad.size());
  cpu.eip=0xa000;cpu.halted=false;
  need(cpu.step()==StepResult::Faulted&&cpu.fault.has_value(),
       "FXRSTOR reserved MXCSR must cause visible fault");
  need(cpu.fx.mxcsr==previous.mxcsr&&cpu.fx.xmm==previous.xmm,
       "FXRSTOR failure corrupted FPU/XMM state");
  std::cout<<"C09R1_FXSAVE_FXRSTOR_512B_LONG64_PASS registers=16\n";
}
void legacy_fxsave_preserves_upper_xmm(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  cpu.cr4=0x200; // CPU fixture: long-mode MOV CR4 tested separately.
  cpu.fx.xmm[0].fill(0x42);
  cpu.fx.xmm[8].fill(0xab);
  cpu.memory.fill(0xc000,0xa5,512);
  instruction(cpu,{0x67,0x0f,0xae,0x04,0x25,0x00,0xc0,0,0},0x1000);
  need(cpu.read8(0xc000+160)==0x42,
       "legacy FXSAVE did not save XMM0 in 128-bit slot");
  need(cpu.read8(0xc000+288)==0xa5,
       "legacy FXSAVE must not overwrite reserved XMM8..15 image");
  cpu.fx.xmm[0].fill(0);
  cpu.fx.xmm[8].fill(0x33);
  instruction(cpu,{0x67,0x0f,0xae,0x0c,0x25,0x00,0xc0,0,0},0x1000);
  need(cpu.fx.xmm[0][0]==0x42&&cpu.fx.xmm[8][0]==0x33,
       "legacy FXRSTOR must restore only XMM0-7");
  std::cout<<"C09R1_FXSTATE_LEGACY_32BIT_VIEW_PASS xmm8_untouched=1\n";
}
void negative(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);
  const std::uint8_t code[]{0x66,0x0f,0xef,0xc0};
  cpu.memory.write_block(0x1000,code,sizeof(code));
  need(cpu.step()==StepResult::Faulted&&cpu.fault.has_value()&&
       cpu.fault->code==cosmic_pc::FaultCode::InvalidInstruction,
       "SSE2 without guest OSFXSR must trigger invalid opcode");
  cpu.fault.reset();cpu.eip=0x1000;cpu.cr4=0x200;cpu.cr0|=0x8;
  need(cpu.step()==StepResult::Faulted&&cpu.fault &&
       cpu.fault->code==cosmic_pc::FaultCode::DeviceNotAvailable,
       "CR0.TS must prevent SIMD register state access (#NM)");
  cpu.reset(0,0x1000);
  const std::uint8_t cpuid[]{0x0f,0xa2};
  cpu.memory.write_block(0x1000,cpuid,sizeof(cpuid));
  cpu.regs[Cpu::EAX]=1;
  need(cpu.step()==StepResult::Executed,"CPUID after CPU state change failed");
  need((cpu.regs[Cpu::EDX]&(1U<<26))==0 &&
       (cpu.regs[Cpu::EDX]&(1U<<0))==0,
       "CPUID must not advertise full SSE2 or x87 while partial");
  std::cout<<"C09R1_NO_PREMATURE_SSE2_X87_CPUID_PASS\n";
}
}
int main(){
  try{
    x87_raw();xmm_operations();fxsave_fxrstor();legacy_fxsave_preserves_upper_xmm();negative();
    std::cout<<"REVERSE_WINDOWS_C09R1_EXTENDED_STATE_PASS checks="<<checks
             <<" sse2_complete=false x87_complete=false windows_executed=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_EXTENDED_STATE_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
