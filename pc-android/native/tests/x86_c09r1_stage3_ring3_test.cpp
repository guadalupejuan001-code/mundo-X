#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void need(bool yes,const std::string& why){++checks;if(!yes)throw std::runtime_error(why);}
void code(Cpu& cpu,std::uint64_t address,std::initializer_list<std::uint8_t> bytes){
  const std::vector<std::uint8_t> data(bytes);
  cpu.memory.write_block(address,data.data(),data.size());
}
void boot64(Cpu& cpu){
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20 service");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<12000;++i) {
    const auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(status==StepResult::Halted&&cpu.is_long_mode())break;
  }
  need(cpu.is_long_mode(),"guest x64 mode not reached");
}
void gate(Cpu& cpu,unsigned vector,std::uint64_t handler){
  const auto address=cpu.idtr.base+16U*vector;
  const auto gate=(handler&0xffffULL)|(std::uint64_t(0x18)<<16)|
                  (std::uint64_t(0xee)<<40)|(((handler>>16)&0xffffULL)<<48);
  cpu.write64(address,gate);cpu.write64(address+8,0);
}
void setup(Cpu& cpu){
  cpu.write64(0x1000,0x2007);
  cpu.write64(0x2000,0x3007);
  cpu.write64(0x3000,0x4007);
  for(unsigned i=0;i<512;++i)
    cpu.write64(0x4000+i*8,(std::uint64_t(i)*4096)|3ULL);
  for(std::uint64_t page:{0x11000ULL,0x19f000ULL})
    cpu.write64(0x4000+8*(page/4096),page|7ULL);
  cpu.gdtr={0x3f,0x14000};
  cpu.write64(0x14000,0);
  cpu.write64(0x14000+8,0x00cf9a000000ffffULL);
  cpu.write64(0x14000+16,0x00cf92000000ffffULL);
  cpu.write64(0x14000+24,0x00af9a000000ffffULL); // ring0 long64 code
  cpu.write64(0x14000+32,0x00affa000000ffffULL); // ring3 long64 code
  cpu.write64(0x14000+40,0x00cff2000000ffffULL); // ring3 data
  constexpr std::uint64_t tss=0x18000,limit=0x67,access=0x89;
  const auto tss_desc=(limit&0xffffULL)|((tss&0xffffULL)<<16)|
      (((tss>>16)&0xffULL)<<32)|(access<<40)|
      (((limit>>16)&15ULL)<<48)|(((tss>>24)&255ULL)<<56);
  cpu.write64(0x14000+48,tss_desc);
  cpu.write64(0x14000+56,0);
  cpu.write64(tss+4,0x1d0000); // TSS RSP0
  cpu.idtr={0x1000,0x15000};
  code(cpu,0xa000,{0xb8,0x30,0,0,0,0x0f,0x00,0xd8}); // MOV AX; LTR
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  need(cpu.step()==StepResult::Executed,"TSS selector setup failed");
  need(cpu.step()==StepResult::Executed&&cpu.tr.valid&&cpu.tr.base==tss,
       "guest LTR did not load TSS");
  cpu.cr4|=0x600U; // CR4.OSFXSR+OSXMMEXCPT
}
void lane(Cpu& cpu,unsigned r,unsigned off,std::uint64_t v){
  for(unsigned i=0;i<8;++i)cpu.xmm[r][off+i]=std::uint8_t(v>>(i*8));
}
std::uint64_t lane(const Cpu& cpu,unsigned r,unsigned off=0){
  std::uint64_t result=0;
  for(unsigned i=0;i<8;++i)result|=std::uint64_t(cpu.xmm[r][off+i])<<(i*8);
  return result;
}
void test(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  boot64(cpu);setup(cpu);
  lane(cpu,0,0,0x3ff8000000000000ULL); // 1.5
  lane(cpu,0,8,0x402a800000000000ULL); // 13.25
  lane(cpu,1,0,0x4000000000000000ULL); // 2
  lane(cpu,1,8,0x4008000000000000ULL); // 3
  cpu.mxcsr=0x1f80;
  code(cpu,0xa040,{0xd9,0xe8}); // FLD1 via actual guest x87 opcode
  cpu.eip=0xa040;cpu.halted=false;
  need(cpu.step()==StepResult::Executed,"guest FLD1 initial context failed");
  const auto saved_x87=cpu.x87[7],saved_tags=cpu.fpu_tags;
  gate(cpu,0x80,0x13000);
  gate(cpu,0x81,0x13400);
  // Ring0 uses actual instruction sequence to build an outer-IRETQ frame.
  code(cpu,0xa100,{0xb8,0x2b,0,0,0,0x50,
                   0xb8,0,0,0x1a,0,0x50,0x9c,
                   0xb8,0x23,0,0,0,0x50,
                   0xb8,0,0x10,0x01,0,0x50,0xcf});
  // User real SSE2: 1.5 + 2 = 3.5; INT80 into ring0 save+clobber+restore;
  // 3.5 * 2 = 7; INT81 exits to a genuine kernel HLT.
  code(cpu,0x11000,{0xf2,0x0f,0x58,0xc1,
                    0xcd,0x80,
                    0xf2,0x0f,0x59,0xc1,
                    0xcd,0x81});
  code(cpu,0x13000,{
      0x48,0x0f,0xae,0x04,0x25,0,0xe0,0x01,0, // FXSAVE64 [0x1e000]
      0x66,0x0f,0xef,0xc0,                     // PXOR xmm0,xmm0
      0xdb,0xe3,                              // FNINIT
      0x48,0x0f,0xae,0x0c,0x25,0,0xe0,0x01,0, // FXRSTOR64 [0x1e000]
      0xcf});                                 // IRETQ
  code(cpu,0x13400,{0xf4});
  cpu.regs[Cpu::ESP]=0x1c0000;
  cpu.eflags=2|Cpu::IF;cpu.eip=0xa100;cpu.halted=false;
  unsigned enters=0,returns=0;
  bool clobbered=false,restored=false,user_entered=false;
  for(unsigned i=0;i<120;++i){
    const auto current=cpu.eip;
    const auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(cpu.cpl()==3&&cpu.eip==0x11000){
      user_entered=true;
      need(cpu.regs[Cpu::ESP]==0x1a0000&&cpu.segments[Cpu::SS]==0x2b,
           "kernel IRETQ failed to load user SS/RSP and CPL3");
    }
    if(cpu.cpl()==0&&cpu.eip==0x13000){
      ++enters;
      need(cpu.regs[Cpu::ESP]==0x1d0000-40,
           "kernel did not switch to TSS.RSP0");
      need(lane(cpu,0)==0x400c000000000000ULL,
           "guest ADD SD did not compute user 3.5 before INT");
    }
    if(cpu.cpl()==0&&cpu.eip==0x13000+15){
      clobbered=lane(cpu,0)==0;
      need(cpu.fpu_tags==0xffff,
           "kernel FNINIT should clear x87 state before FXRSTOR");
    }
    if(cpu.cpl()==0&&cpu.eip==0x13000+24){
      restored=lane(cpu,0)==0x400c000000000000ULL;
      need(cpu.fpu_tags==saved_tags&&cpu.x87[7]==saved_x87,
           "kernel FXRSTOR lost saved user x87 context");
    }
    if(cpu.cpl()==3&&current==0x13000+24)++returns;
    if(status==StepResult::Halted)break;
  }
  need(cpu.halted&&cpu.cpl()==0&&cpu.eip==0x13401,
       "guest did not exit via kernel INT81 and HLT");
  need(user_entered&&enters==1&&returns==1&&clobbered&&restored,
       "real Ring3↔Ring0 FXSAVE context transitions incomplete");
  need(lane(cpu,0)==0x401c000000000000ULL&&
       lane(cpu,0,8)==0x402a800000000000ULL,
       "user SSE2 XMM0 state not restored and resumed to 7.0");
  need(cpu.mxcsr==0x1f80&&cpu.fpu_tags==saved_tags,
       "kernel/return leaked FPU MXCSR or x87 tags");
  std::cout<<"C09R1_RING3_RING0_FXSAVE_GUEST_CONTEXT_PASS user_kernel_user=true\n";
}
}
int main(){
  try{
    test();
    std::cout<<"REVERSE_WINDOWS_C09R1_RING3_FPU_PASS checks="<<checks
             <<" windows_iso_used=false cpuid_sse2=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_RING3_FPU_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
