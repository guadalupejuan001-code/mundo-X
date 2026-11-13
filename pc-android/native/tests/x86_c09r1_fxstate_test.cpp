#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
StepResult execute(Cpu& cpu,std::initializer_list<std::uint8_t> bytes){
  const std::vector<std::uint8_t> code(bytes);
  cpu.memory.write_block(0x1000,code.data(),code.size());
  cpu.eip=0x1000;cpu.halted=false;cpu.fault.reset();
  return cpu.step();
}
void real16_fxstate() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  for(unsigned i=0;i<16;++i)cpu.xmm[0][i]=std::uint8_t(0xa0+i);
  for(unsigned i=0;i<512;++i)cpu.memory[0x4000+i]=0x77;
  check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,"FLD1 failed");
  check(execute(cpu,{0xd9,0xee})==StepResult::Executed,"FLDZ failed");
  check(((cpu.fpu_status>>11)&7U)==6&&((cpu.fpu_tags>>12)&3U)==1&&((cpu.fpu_tags>>14)&3U)==0,
        "x87 physical stack and zero/valid tags are wrong");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Executed,
        "FXSAVE m512 legacy not executed");
  check(cpu.read16(0x4000)==0x037f&&cpu.read16(0x4002)==0x3000,
        "FXSAVE control/status or TOP incorrect");
  check(cpu.memory[0x4004]==0xc0,
        "FXSAVE must store abridged tags in physical R6/R7 order");
  check(cpu.read32(0x4000+24)==0x1f80&&
        cpu.read32(0x4000+28)==Cpu::kMxcsrMask,
        "FXSAVE MXCSR and mask fields invalid");
  check(cpu.read64(0x4000+144)==0x8000000000000000ULL&&
        cpu.read16(0x4000+144+8)==0x3fff,
        "FXSAVE physical R7 must be 80-bit +1");
  for(unsigned i=0;i<16;++i)
    check(cpu.memory[0x4000+160+i]==std::uint8_t(0xa0+i),
          "FXSAVE XMM0 slot bytes wrong");
  for(unsigned i=464;i<512;++i)
    check(cpu.memory[0x4000+i]==0x77,
          "FXSAVE must not overwrite software-owned bytes 464..511");
  check(execute(cpu,{0xdb,0xe3})==StepResult::Executed,
        "FNINIT did not reset x87");
  check(cpu.fpu_status==0&&cpu.fpu_tags==0xffff,
        "FNINIT failed to empty x87 stack");
  cpu.mxcsr=0;
  cpu.xmm[0].fill(0);
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Executed,
        "FXRSTOR legacy not executed");
  check(cpu.fpu_control==0x037f&&((cpu.fpu_status>>11)&7U)==6&&cpu.fpu_tags!=0xffff&&
        cpu.mxcsr==0x1f80,
        "FXRSTOR did not restore FPU TOP, tags, FCW and MXCSR");
  for(unsigned i=0;i<16;++i)
    check(cpu.xmm[0][i]==std::uint8_t(0xa0+i),
          "FXRSTOR lost XMM0 bytes");
  check(execute(cpu,{0xdf,0xe0})==StepResult::Executed &&
        cpu.reg16(Cpu::EAX)==0x3000,
        "FNSTSW AX did not expose restored TOP");
  check(execute(cpu,{0xdd,0x1e,0x00,0x50})==StepResult::Executed,
        "FSTP m64 real of ST0 zero failed");
  check(cpu.read64(0x5000)==0 && ((cpu.fpu_status>>11)&7U)==7,
        "FSTP did not pop zero or encode IEEE64 +0");
  check(execute(cpu,{0xdd,0x1e,0x08,0x50})==StepResult::Executed,
        "FSTP m64 real of ST0 one failed");
  check(cpu.read64(0x5008)==0x3ff0000000000000ULL&&
        cpu.fpu_tags==0xffff&&((cpu.fpu_status>>11)&7U)==0,
        "x87 FLD1/FSTP did not roundtrip exact IEEE +1");
  std::cout<<"C09R1_FXSTATE_REAL16_ROUNDTRIP_PASS software_tail_untouched=1\n";
}
void fxstate_failures() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,
        "FLD1 fixture failed");
  check(execute(cpu,{0x0f,0xae,0x06,0x01,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,
        "misaligned FXSAVE must be #GP");
  check(((cpu.fpu_status>>11)&7U)==7&&((cpu.fpu_tags>>14)&3U)==0,
        "misaligned FXSAVE modified live x87 state");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0xff})==StepResult::Faulted,
        "512 byte FXSAVE crossing segment boundary must fault");
  check(((cpu.fpu_status>>11)&7U)==7,"failed save corrupted the x87 stack");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Executed,
        "FXSAVE fixture failed");
  cpu.memory[0x4000+27]=0x80; // MXCSR reserved high bit
  cpu.xmm[0].fill(0x5a);
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,
        "FXRSTOR must reject reserved MXCSR bits with #GP");
  check(cpu.mxcsr==0x1f80&&cpu.xmm[0][0]==0x5a&&((cpu.fpu_status>>11)&7U)==7,
        "FXRSTOR invalid image changed registers despite rollback");
  cpu.cr0|=8; // TS
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::DeviceNotAvailable,
        "FXSAVE CR0.TS must produce #NM");
  cpu.cr0&=~8U;cpu.cr4=0;
  check(execute(cpu,{0x66,0x0f,0x6f,0x06,0x00,0x40})==StepResult::Faulted,
        "SSE2 cannot operate without CR4.OSFXSR");
  std::cout<<"C09R1_FXSTATE_FAULTS_PASS alignment=1 mxcsr=1 nm=1\n";
}
void fpu_memory_and_stack() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  cpu.write32(0x5000,0x3fc00000U); // float32 +1.5
  cpu.write64(0x5010,0x4000000000000000ULL); // float64 +2.0
  check(execute(cpu,{0xd9,0x06,0x00,0x50})==StepResult::Executed,
        "FLD m32real failed");
  check(execute(cpu,{0xdd,0x06,0x10,0x50})==StepResult::Executed,
        "FLD m64real failed");
  check(execute(cpu,{0xd8,0xc1})==StepResult::Executed,
        "FADD ST0,ST1 failed");
  check(execute(cpu,{0xdd,0x1e,0x20,0x50})==StepResult::Executed,
        "FSTP m64real of exact sum failed");
  check(cpu.read64(0x5020)==0x400c000000000000ULL,
        "FPU exact 2.0 + 1.5 must store 3.5");
  check(execute(cpu,{0xd9,0x1e,0x30,0x50})==StepResult::Executed,
        "FSTP m32real failed");
  check(cpu.read32(0x5030)==0x3fc00000U,
        "FLD/FSTP m32real did not preserve 1.5");
  check(cpu.fpu_tags==0xffff,
        "x87 stack not empty after two FSTPs");
  check(execute(cpu,{0xdf,0xe0})==StepResult::Executed &&
        cpu.reg16(Cpu::EAX)==0,
        "FNSTSW AX incorrect after x87 operations");
  std::cout<<"C09R1_X87_REAL_MEMORY_ARITHMETIC_PASS add_1_5_plus_2=3_5\n";
}
void long64_fxstate(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  const auto image=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,image.data(),image.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned n=0;n<12000;++n){
    const auto result=cpu.step();
    if(result==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(result==StepResult::Halted&&cpu.is_long_mode())break;
  }
  check(cpu.is_long_mode(),"FXSAVE64 guest did not reach long64 mode");
  cpu.cr4|=0x200;
  cpu.mxcsr=0x1f80;
  for(unsigned i=0;i<16;++i){
    cpu.xmm[0][i]=std::uint8_t(i+3);
    cpu.xmm[15][i]=std::uint8_t(i+0x40);
  }
  cpu.fpu_ip=0x00007fff12345678ULL;cpu.fpu_dp=0x00007fff98765430ULL;
  const std::array<std::uint8_t,9> save{{
    0x48,0x0f,0xae,0x04,0x25,0x00,0x40,0,0 // FXSAVE64 [0x4000]
  }};
  cpu.memory.write_block(0xa000,save.data(),save.size());
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  check(cpu.step()==StepResult::Executed,
        "64-bit REX.W FXSAVE failed");
  check(cpu.read64(0x4008)==cpu.fpu_ip&&
        cpu.read64(0x4010)==cpu.fpu_dp,
        "FXSAVE64 did not save 64-bit FIP/FDP");
  for(unsigned i=0;i<16;++i)
    check(cpu.memory[0x4000+400+i]==std::uint8_t(0x40+i),
          "FXSAVE64 did not save XMM15");
  cpu.xmm[0].fill(0);cpu.xmm[15].fill(0);cpu.mxcsr=0;
  cpu.fpu_ip=0;cpu.fpu_dp=0;
  const std::array<std::uint8_t,9> restore{{
    0x48,0x0f,0xae,0x0c,0x25,0x00,0x40,0,0 // FXRSTOR64 [0x4000]
  }};
  cpu.memory.write_block(0xa000,restore.data(),restore.size());
  cpu.eip=0xa000;
  check(cpu.step()==StepResult::Executed&&cpu.mxcsr==0x1f80,
        "64-bit FXRSTOR did not recover SSE context");
  check(cpu.fpu_ip==0x00007fff12345678ULL&&
        cpu.fpu_dp==0x00007fff98765430ULL,
        "FXRSTOR64 lost 64-bit FIP/FDP");
  for(unsigned i=0;i<16;++i)
    check(cpu.xmm[0][i]==std::uint8_t(i+3)&&
          cpu.xmm[15][i]==std::uint8_t(i+0x40),
          "FXRSTOR64 failed XMM0/XMM15 state restore");
  std::cout<<"C09R1_FXSAVE64_XMM15_CONTEXT_PASS xmm16=1\n";
}
void two_contexts_fxstate(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  cpu.mxcsr=0x1f80;
  cpu.xmm[0].fill(0x11);
  check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,
        "context A FLD1 failed");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x60})==StepResult::Executed,
        "context A FXSAVE failed");
  check(execute(cpu,{0xdb,0xe3})==StepResult::Executed,
        "context B FNINIT failed");
  cpu.xmm[0].fill(0x22);
  cpu.mxcsr=0x3f80; // distinct legal MXCSR rounding control
  check(execute(cpu,{0xd9,0xee})==StepResult::Executed,
        "context B FLDZ failed");
  check(execute(cpu,{0x0f,0xae,0x06,0x01,0x62})==StepResult::Faulted,
        "misaligned context B save should fault");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x70})==StepResult::Executed,
        "context B FXSAVE failed");
  cpu.xmm[0].fill(0xff);cpu.mxcsr=0;
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x60})==StepResult::Executed,
        "context A FXRSTOR failed");
  check(cpu.xmm[0][0]==0x11&&cpu.mxcsr==0x1f80 &&
        ((cpu.fpu_tags>>14)&3)==0,
        "restored A got another context XMM/MXCSR/x87");
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x70})==StepResult::Executed,
        "context B FXRSTOR failed");
  check(cpu.xmm[0][0]==0x22&&cpu.mxcsr==0x3f80 &&
        ((cpu.fpu_tags>>14)&3)==1,
        "restored B got another context XMM/MXCSR/x87");
  std::cout<<"C09R1_FXSAVE_CONTEXT_A_B_ISOLATED_PASS two_contexts=1\n";
}
void nm_guest_handler_recovery(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  const auto guest=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,guest.data(),guest.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20 service");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned n=0;n<12000;++n){
    auto r=cpu.step();
    if(r==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(r==StepResult::Halted&&cpu.is_long_mode())break;
  }
  check(cpu.is_long_mode(),"NM guest not in long64 mode");
  cpu.cr4|=0x200;cpu.cr0|=0x8;
  cpu.regs[Cpu::ESP]=0x190000;
  cpu.idtr={0x1000,0x15000};
  constexpr std::uint64_t target=0x13000;
  const auto low=(target&0xffffULL)|(std::uint64_t(0x18)<<16)|
                 (std::uint64_t(0x8e)<<40)|((target>>16)<<48);
  cpu.write64(cpu.idtr.base+7*16,low);
  cpu.write64(cpu.idtr.base+7*16+8,0);
  // Guest #NM handler executes CLTS; IRETQ retries the exact FXSAVE64.
  const std::array<std::uint8_t,3> handler{{0x0f,0x06,0xcf}};
  cpu.memory.write_block(target,handler.data(),handler.size());
  const std::array<std::uint8_t,10> main{{
     0x48,0x0f,0xae,0x04,0x25,0x00,0x40,0,0,0xf4
  }};
  cpu.memory.write_block(0xa000,main.data(),main.size());
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  check(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==target,
        "FXSAVE CR0.TS did not deliver guest #NM vector7");
  check(cpu.regs[Cpu::ESP]==0x190000-40,
        "guest #NM must stack 64-bit no-error-code frame");
  check(cpu.step()==StepResult::Executed && !(cpu.cr0&8),
        "real guest CLTS failed to clear task-switched flag");
  check(cpu.step()==StepResult::Executed&&cpu.eip==0xa000,
        "IRETQ failed to restart exact FXSAVE64 after #NM");
  check(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==0xa009,
        "guest FXSAVE64 failed after #NM handler");
  check(cpu.read16(0x4000)==0x037f && cpu.regs[Cpu::ESP]==0x190000,
        "retried FXSAVE failed state or lost guest stack");
  check(cpu.step()==StepResult::Halted,
        "real guest did not halt after completed FXSAVE64");
  std::cout<<"C09R1_GUEST_NM_CLTS_IRETQ_RETRY_PASS vector=7\n";
}
}
int main(){
  try{
    real16_fxstate();fxstate_failures();fpu_memory_and_stack();long64_fxstate();
    two_contexts_fxstate();nm_guest_handler_recovery();
    std::cout<<"REVERSE_WINDOWS_C09R1_FPU_FXSTATE_PASS checks="<<checks
             <<" windows_executed=false sse2_cpuid=false x87_cpuid=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_FPU_FXSTATE_FAIL "<<e.what()<<"\n";return 1;
  }
}
