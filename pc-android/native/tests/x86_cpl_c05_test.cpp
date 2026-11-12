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
void need(bool ok,const std::string& why){++checks;if(!ok)throw std::runtime_error(why);}
void code(Cpu& cpu,std::uint64_t addr,std::initializer_list<std::uint8_t> b){
  const std::vector<std::uint8_t> bytes(b);
  cpu.memory.write_block(addr,bytes.data(),bytes.size());
}
void one(Cpu& cpu){
  auto s=cpu.step();
  if(s==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
}
void boot(Cpu& cpu,bool long64){
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& guest,std::uint8_t n){
    if(n!=0x15||guest.reg16(Cpu::EAX)!=0x2401)guest.fail("unexpected BIOS request");
    guest.a20_enabled=true;guest.set_reg8(4,0);guest.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<12000;++i){
    const auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(!long64 && std::string(cpu.mode_name())=="protected32"&&cpu.segments[Cpu::SS]==0x10)return;
    if(long64 && status==StepResult::Halted && cpu.is_long_mode())return;
  }
  throw std::runtime_error("boot failed to enter requested real guest CPU mode");
}
void gdt(Cpu& cpu,bool long64){
  constexpr std::uint64_t base=0x14000,tss=0x18000;
  cpu.gdtr={0x3f,base};
  cpu.write64(base+0,0);
  cpu.write64(base+8,0x00cf9a000000ffffULL);
  cpu.write64(base+16,0x00cf92000000ffffULL);
  cpu.write64(base+24,0x00af9a000000ffffULL);
  cpu.write64(base+32,long64?0x00affa000000ffffULL:0x00cffa000000ffffULL);
  cpu.write64(base+40,0x00cff2000000ffffULL);
  const std::uint64_t limit=0x67,access=0x89;
  const auto raw=(limit&0xffffULL)|((tss&0xffffULL)<<16)|
      (((tss>>16)&0xffULL)<<32)|(access<<40)|
      (((limit>>16)&15ULL)<<48)|(((tss>>24)&255ULL)<<56);
  cpu.write64(base+48,raw);
  cpu.write64(base+56,long64 ? (tss>>32) : 0);
  cpu.write64(tss+4,0x1d0000); // TSS.RSP0 for IA-32e
  if(!long64){
    cpu.write32(tss+4,0x1d0000); // TSS.ESP0 for protected32
    cpu.write16(tss+8,0x10);     // TSS.SS0
  }
  cpu.idtr={0x1000,0x15000};
  code(cpu,0xa000,{0xb8,0x30,0x00,0x00,0x00,0x0f,0x00,0xd8});
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  one(cpu);one(cpu); // MOV EAX, 0x30; LTR AX (real guest opcodes)
  need(cpu.tr.valid&&cpu.tr.selector==0x30&&cpu.tr.base==tss,
       "real LTR failed to load TSS descriptor");
}
void gate(Cpu& cpu,unsigned vector,std::uint64_t dest,bool long64,std::uint8_t type){
  const auto off=cpu.idtr.base+vector*(long64?16U:8U);
  const std::uint64_t low=(dest&0xffffULL)|
      (std::uint64_t(long64?0x18:0x08)<<16)|
      (std::uint64_t(type)<<40)|
      (((dest>>16)&0xffffULL)<<48);
  cpu.write64(off,low);
  if(long64)cpu.write64(off+8,(dest>>32)&0xffffffffULL);
}
void user_paging(Cpu& cpu){
  // Make only user text+stack U/S=1; GDT, IDT, kernel and TSS remain U/S=0.
  cpu.write64(0x1000,0x2007);
  cpu.write64(0x2000,0x3007);
  cpu.write64(0x3000,0x4007);
  for(unsigned i=0;i<512;++i)cpu.write64(0x4000+8*i,(std::uint64_t(i)*4096)|3ULL);
  for(std::uint64_t page:{0x11000ULL,0x19f000ULL})
    cpu.write64(0x4000+(page/4096)*8,page|7);
}
void long_cpl3_tss_pf(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  boot(cpu,true);user_paging(cpu);gdt(cpu,true);
  gate(cpu,0x80,0x13000,true,0xee); // DPL3 interrupt service gate
  gate(cpu,0x81,0x13400,true,0xee); // DPL3 exit service
  gate(cpu,13,0x13200,true,0x8e); // #GP CPL3 -> CPL0
  gate(cpu,14,0x13300,true,0x8e); // #PF CPL3 -> CPL0
  // Guest kernel stack frame: SS3,RSP3,RFLAGS,CS3,RIP3, then IRETQ.
  code(cpu,0xa100,{0xb8,0x2b,0,0,0,0x50,
                  0xb8,0,0,0x1a,0,0x50,0x9c,
                  0xb8,0x23,0,0,0,0x50,
                  0xb8,0,0x10,0x01,0,0x50,0xcf});
  // User: INT80, then denied CLI and denied HLT, then read and write of
  // supervisor-only kernel data (error bits 5/7), finally INT81 to kernel.
  code(cpu,0x11000,{
       0xb8,0x44,0x33,0x22,0x11,0xcd,0x80,
       0xbb,0x88,0x77,0x66,0x55,0xfa,0xf4,
       0x48,0x8b,0x0c,0x25,0x00,0x60,0x01,0x00,
       0x48,0x89,0x04,0x25,0x00,0x60,0x01,0x00,
       0xcd,0x81});
  // Kernel INT80 handler: guest writes an observable marker, then IRETQ.
  code(cpu,0x13000,{0xb8,0xef,0xbe,0xad,0xde,
                   0xa3,0x00,0x60,0x01,0,0,0,0,0,0xcf});
  // #GP has a 64-bit error code; save it to R9, advance saved RIP one byte.
  code(cpu,0x13200,{0x4c,0x8b,0x0c,0x24,
                    0x48,0x83,0x44,0x24,0x08,0x01,
                    0x48,0x83,0xc4,0x08,0xcf});
  // #PF has a 64-bit error code; save it to RDX, skip 8-byte MOV.
  code(cpu,0x13300,{0x48,0x8b,0x14,0x24,
                    0x48,0x83,0x44,0x24,0x08,0x08,
                    0x48,0x83,0xc4,0x08,0xcf});
  code(cpu,0x13400,{0xf4});
  cpu.eflags=2|Cpu::IF;cpu.eip=0xa100;
  unsigned interrupts=0,general=0;std::vector<std::uint64_t> page_errors;
  bool entered_user=false,returned_user=false,entered_kernel=false;
  for(unsigned i=0;i<200;++i){
    auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(cpu.cpl()==3&&cpu.eip==0x11000){
      entered_user=true;
      need(cpu.regs[Cpu::ESP]==0x1a0000 && cpu.segments[Cpu::SS]==0x2b,
           "IRETQ failed to enter CPL3 with user SS:RSP");
    }
    if(cpu.cpl()==0&&cpu.eip==0x13000){
      ++interrupts;entered_kernel=true;
      need(cpu.regs[Cpu::ESP]==0x1d0000-40,"TSS RSP0 did not switch for INT80");
      need(cpu.read64(cpu.regs[Cpu::ESP]+8)==0x23,
           "INT80 did not stack user CS");
      need(cpu.read64(cpu.regs[Cpu::ESP]+24)==0x1a0000,
           "INT80 did not preserve user RSP");
    }
    if(cpu.cpl()==3&&cpu.regs[Cpu::EBX]==0x55667788ULL)returned_user=true;
    if(cpu.cpl()==0 && cpu.eip==0x13200){
      ++general;
      need(cpu.read64(cpu.regs[Cpu::ESP])==0,"#GP must carry zero error code");
    }
    if(cpu.cpl()==0 && cpu.eip==0x13300){
      page_errors.push_back(cpu.read64(cpu.regs[Cpu::ESP]));
      need(cpu.cr2==0x16000,"#PF CR2 failed to record supervisor-only address");
    }
    if(status==StepResult::Halted)break;
  }
  need(cpu.halted&&cpu.cpl()==0&&cpu.eip==0x13401,"guest exit gate did not reach kernel HLT");
  need(entered_user&&entered_kernel&&returned_user&&interrupts==1,
       "kernel -> user -> kernel -> user real transition incomplete");
  need(general==2,"CPL3 CLI and HLT must each generate #GP");
  need(page_errors.size()==2 && page_errors[0]==5 && page_errors[1]==7,
       "CPL3 supervisor read/write must set #PF P=1 and U/S=1, with W/R bit");
  need(cpu.read32(0x16000)==0xdeadbeef,"kernel marker overwritten by user");
  need(cpu.regs[Cpu::R9]==0&&cpu.regs[Cpu::EDX]==7,"fault handlers did not read error codes");
  std::cout<<"C05_LONG_RING3_TSS_PF_PASS transitions=2 gp="<<general
           <<" pf_read="<<page_errors[0]<<" pf_write="<<page_errors[1]<<"\n";
}
void protected32_cpl3(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  boot(cpu,false);gdt(cpu,false);
  gate(cpu,0x80,0x13000,false,0xee);
  gate(cpu,0x81,0x13400,false,0xee);
  code(cpu,0xa100,{0x68,0x2b,0,0,0,
                  0x68,0,0,0x1a,0,
                  0x9c,
                  0x68,0x23,0,0,0,
                  0x68,0,0x10,0x01,0,
                  0xcf});
  code(cpu,0x11000,{0xb8,0x34,0x12,0,0,0xcd,0x80,
                   0xbb,0x78,0x56,0,0,0xcd,0x81});
  // IDT entry does not automatically reload DS; the kernel must do so
  // after the outer-privilege IRET invalidates its old user-inaccessible DS.
  code(cpu,0x13000,{0xb8,0x10,0,0,0,0x8e,0xd8,
                    0xc7,0x05,0x00,0x60,0x01,0,0xef,0xbe,0xad,0xde,
                    0xcf});
  code(cpu,0x13400,{0xf4});
  cpu.regs[Cpu::ESP]=0x1e0000;
  cpu.eflags=2|Cpu::IF;cpu.eip=0xa100;cpu.halted=false;
  bool entered_user=false,service=false,returned_user=false;
  for(unsigned i=0;i<160;++i){
    auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(cpu.cpl()==3&&cpu.eip==0x11000){
      entered_user=true;
      need(cpu.segments[Cpu::DS]==0,"outer-ring IRETD did not invalidate kernel DS");
      need(cpu.regs[Cpu::ESP]==0x1a0000&&cpu.segments[Cpu::SS]==0x2b,
           "IRETD did not enter protected32 CPL3 with correct stack");
    }
    if(cpu.cpl()==0&&cpu.eip==0x13000){
      service=true;
      need(cpu.regs[Cpu::ESP]==0x1d0000-20,
           "protected32 TSS SS0:ESP0 did not switch to ring0");
      need(cpu.read32(cpu.regs[Cpu::ESP]+4)==0x23,
           "protected32 INT did not save user selector");
    }
    if(cpu.cpl()==3&&cpu.regs[Cpu::EBX]==0x5678)returned_user=true;
    if(status==StepResult::Halted)break;
  }
  need(cpu.halted&&cpu.cpl()==0&&entered_user&&service&&returned_user,
       "protected32 Ring3->Ring0->Ring3 trip failed");
  need(cpu.read32(0x16000)==0xdeadbeef,"protected32 kernel guest marker wrong");
  std::cout<<"C05_PROTECTED32_RING3_TSS_PASS\n";
}
}
int main(){
  try{
    long_cpl3_tss_pf();
    protected32_cpl3();
    std::cout<<"REVERSE_WINDOWS_C05_CPL3_PASS checks="<<checks
             <<" ring3=true tss=true page_user_bit=true windows_booted=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C05_CPL3_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
