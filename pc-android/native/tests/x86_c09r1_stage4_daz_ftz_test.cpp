#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
static unsigned checks=0;
static void require(bool b,const char* msg){++checks;if(!b)throw std::runtime_error(msg);}
static void boot(Cpu& c){
 const auto image=cosmic_pc::long_mode_boot_disk();
 c.reset();c.memory.write_block(0x7c00,image.data(),image.size());
 c.interrupt_hook=[](Cpu& cpu,std::uint8_t n){
  if(n!=0x15||cpu.reg16(Cpu::EAX)!=0x2401)cpu.fail("unexpected BIOS");
  cpu.a20_enabled=true;cpu.set_reg8(4,0);cpu.set_flag(Cpu::CF,false);return true;
 };
 for(unsigned i=0;i<12000;++i){
  const auto r=c.step();
  if(r==StepResult::Faulted)throw std::runtime_error(c.fault->describe());
  if(r==StepResult::Halted&&c.is_long_mode())break;
 }
 require(c.is_long_mode(),"not in long64 mode");
 c.cr4|=0x600;c.halted=false;
}
static void lane(Cpu& c,unsigned r,std::uint64_t bits){
 for(unsigned i=0;i<8;++i)c.xmm[r][i]=std::uint8_t(bits>>(i*8));
}
static std::uint64_t lane(const Cpu& c,unsigned r){
 std::uint64_t bits=0;
 for(unsigned i=0;i<8;++i)bits|=std::uint64_t(c.xmm[r][i])<<(i*8);
 return bits;
}
static void run(Cpu& c,std::initializer_list<std::uint8_t> bytes){
 const std::vector<std::uint8_t> code(bytes);
 c.memory.write_block(0xa000,code.data(),code.size());
 c.eip=0xa000;c.halted=false;c.fault.reset();
 if(c.step()!=StepResult::Executed||c.fault)
   throw std::runtime_error(c.fault?c.fault->describe():"instruction not executed");
}
int main(){
 try{
  Cpu c(64ULL*1024*1024,16ULL*1024*1024);boot(c);
  const std::uint64_t tiny=1ULL,minus_tiny=0x8000000000000001ULL;
  const std::uint64_t one=0x3ff0000000000000ULL;
  const std::uint64_t half=0x3fe0000000000000ULL;
  // DAZ changes a subnormal input into signed zero; without DAZ, DE is sticky.
  c.mxcsr=0x1f80;lane(c,0,one);lane(c,1,tiny);
  run(c,{0xf2,0x0f,0x58,0xc1});
  require(lane(c,0)==one && (c.mxcsr&2),"DAZ off: subnormal input must set DE");
  c.mxcsr=0x1fc0;lane(c,0,one);lane(c,1,tiny);
  run(c,{0xf2,0x0f,0x58,0xc1});
  require(lane(c,0)==one && !(c.mxcsr&2),"DAZ on: input must not set DE");
  // Using 0 + tiny distinguishes numerical result, not just sticky flags.
  c.mxcsr=0x1f80;lane(c,0,0);lane(c,1,tiny);
  run(c,{0xf2,0x0f,0x58,0xc1});
  require(lane(c,0)==tiny && (c.mxcsr&2),"DAZ off: 0+tiny must be tiny");
  c.mxcsr=0x1fc0;lane(c,0,0);lane(c,1,tiny);
  run(c,{0xf2,0x0f,0x58,0xc1});
  require(lane(c,0)==0 && !(c.mxcsr&2),"DAZ on: 0+tiny must be +0");
  c.mxcsr=0x1fc0;lane(c,0,0);lane(c,1,minus_tiny);
  run(c,{0xf2,0x0f,0x58,0xc1});
  require(lane(c,0)==0 && !(c.mxcsr&2),"DAZ on: -tiny converted to -0");
  // FTZ acts on results, not input; with DAZ disabled 2*tiny is subnormal.
  c.mxcsr=0x1f80;lane(c,0,tiny);lane(c,1,half);
  run(c,{0xf2,0x0f,0x59,0xc1});
  require(lane(c,0)==0 && (c.mxcsr&0x30)==0x30,
          "underflow of tiny*0.5 should set UE and PE");
  c.mxcsr=0x9f80;lane(c,0,tiny);lane(c,1,one);
  run(c,{0xf2,0x0f,0x59,0xc1});
  require(lane(c,0)==0 && (c.mxcsr&0x30)==0x30,
          "FTZ should flush subnormal output and set UE/PE");
  // LDMXCSR accepts bit6 only if FXSAVE advertises DAZ support.
  c.write32(0x5000,0x1fc0);
  run(c,{0x0f,0xae,0x14,0x25,0,0x50,0,0});
  require(c.mxcsr==0x1fc0,"LDMXCSR must accept DAZ bit");
  run(c,{0x0f,0xae,0x04,0x25,0,0x60,0,0});
  require(c.read32(0x6000+28)==Cpu::kMxcsrMask &&
          (Cpu::kMxcsrMask&0x40),"FXSAVE mask must advertise DAZ");
  // Preserve existing strict rejection of reserved bits.
  c.write32(0x5000,0x10000);
  const std::vector<std::uint8_t> insn{0x0f,0xae,0x14,0x25,0,0x50,0,0};
  c.memory.write_block(0xa000,insn.data(),insn.size());
  c.eip=0xa000;c.fault.reset();
  require(c.step()==StepResult::Faulted && c.mxcsr==0x1fc0,
          "reserved MXCSR bit must fault without mutating MXCSR");
  std::cout<<"C09R1_STAGE4_DAZ_FTZ_PASS checks="<<checks
           <<" Windows_booted=false\n";
  return 0;
 }catch(const std::exception& e){
  std::cerr<<"C09R1_STAGE4_DAZ_FTZ_FAIL "<<e.what()<<"\n";return 1;
 }
}
