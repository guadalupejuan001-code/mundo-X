#include "../x86_cpu.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;
unsigned checks=0;
void check(bool yes, const std::string& message) {
  ++checks;
  if(!yes) throw std::runtime_error(message);
}
std::uint64_t descriptor(std::uint32_t base, std::uint32_t limit,
                         unsigned access, unsigned flags=0) {
  return (std::uint64_t(limit & 0xffffu))
    | (std::uint64_t(base & 0xffffffu) << 16)
    | (std::uint64_t(access & 255u) << 40)
    | (std::uint64_t((limit >> 16) & 15u) << 48)
    | (std::uint64_t(flags & 15u) << 52)
    | (std::uint64_t((base >> 24) & 255u) << 56);
}
void protected_cpu(Cpu& cpu) {
  cpu.memory.reset_zero();
  cpu.reset(0,0x1000);
  cpu.cr0|=1; // 16-bit protected mode with a code cache copied from reset.
  cpu.gdtr={0x47,0x3000};
  cpu.write64(0x3008,descriptor(0x7000,0xffff,0x92)); // data, DPL0
  cpu.write64(0x3010,descriptor(0x9000,0x1f,0x82));   // LDT, 4 descriptors
  cpu.write64(0x3018,descriptor(0x8000,0x67,0x89));  // available 32-bit TSS
  cpu.write64(0x3020,descriptor(0x0000,0xffff,0x92)); // data DPL0
  cpu.write64(0x3028,descriptor(0x0000,0xffff,0x9a)); // readable code DPL0
  cpu.write64(0x3030,descriptor(0x0000,0xffff,0xf2)); // data DPL3
  cpu.write64(0x3038,descriptor(0x0000,0xffff,0x98)); // execute-only code
  cpu.write64(0x3040,descriptor(0x0000,0xffff,0x12)); // not-present data
  cpu.write64(0x9008,descriptor(0x0000,0xffff,0x92)); // LDT data at TI selector 0x0C
}
void program(Cpu& cpu,std::initializer_list<std::uint8_t> bytes) {
  cpu.eip=0x1000;cpu.fault.reset();cpu.halted=false;
  std::size_t off=0x1000;
  for(auto b:bytes)cpu.memory[off++]=b;
}
void one(Cpu& cpu,const char* title) {
  const auto result=cpu.step();
  check(result==StepResult::Executed,
        std::string(title)+" should execute: "+(cpu.fault?cpu.fault->describe():"unknown failure"));
  check(!cpu.fault,title);
}
void fail(Cpu& cpu,FaultCode code,const char* explanation) {
  const auto initial_ip=cpu.eip;
  const auto initial_steps=cpu.instructions;
  const auto initial_eflags=cpu.eflags;
  const auto before_ldtr=cpu.ldtr.selector, before_tr=cpu.tr.selector;
  check(cpu.step()==StepResult::Faulted,explanation);
  check(cpu.fault && cpu.fault->code==code,
        std::string(explanation)+": "+(cpu.fault?cpu.fault->describe():"missing fault"));
  check(cpu.eip==initial_ip && cpu.fault->eip==initial_ip &&
        cpu.instructions==initial_steps, "fault must keep IP and step count unchanged");
  check(cpu.eflags==initial_eflags && cpu.ldtr.selector==before_ldtr &&
        cpu.tr.selector==before_tr, "fault must preserve FLAGS and system registers");
}
void check_protected_entry_stack() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0x2000,0x0830);
  cpu.gdtr={0x17,0x3000};
  cpu.write64(0x3008,descriptor(0x20000,0xffff,0x9a)); // Protected CS, 16-bit.
  cpu.write64(0x3010,descriptor(0x40000,0xffff,0x92)); // Protected SS, 16-bit.
  const std::vector<std::uint8_t> enter{
      0xfa,                           // CLI
      0xb8,0x13,0x03,                 // MOV AX,0313 (real-mode SS, low bits 3)
      0x8e,0xd0,                      // MOV SS,AX
      0xbc,0x4c,0x0c,                 // MOV SP,0c4c
      0xb8,0x11,0x00,                 // MOV AX,0011 (ET, PE)
      0x0f,0x22,0xc0,                 // MOV CR0,EAX
      0x6a,0x50,                      // PUSH 0x50 on the inherited stack
      0xea,0x60,0x08,0x08,0x00};      // JMP FAR 0008:0860
  // The first PUSH is at 2000:083f.
  cpu.memory.write_block(0x20830,enter.data(),enter.size());
  const std::vector<std::uint8_t> protected_code{
      0x5b,                           // POP BX on the same inherited stack
      0xb8,0x13,0x00,                 // MOV AX,0013 (RPL3, descriptor DPL0)
      0x8e,0xd0,                      // MOV SS,AX must still fail
      0xb8,0x10,0x00,                 // MOV AX,0010 (valid ring0 stack)
      0x8e,0xd0,                      // MOV SS,AX
      0x6a,0x51};                     // PUSH on the protected descriptor's base
  cpu.memory.write_block(0x20860,protected_code.data(),protected_code.size());
  for (unsigned i=0;i<6;++i) one(cpu,"real-to-protected setup");
  check(cpu.eip==0x083f && std::string(cpu.mode_name())=="protected16" &&
        cpu.cpl()==0 && cpu.segments[Cpu::SS]==0x0313,
        "MOV CR0.PE enters protected16 at CPL0 with inherited SS");
  one(cpu,"PUSH imm8 immediately after setting PE");
  check(cpu.reg16(Cpu::ESP)==0x0c4a && cpu.read16(0x03130+0x0c4a)==0x0050,
        "PUSH uses inherited real-mode SS base and 16-bit SP");
  one(cpu,"protected16 far jump");
  one(cpu,"POP after CS reload with inherited SS");
  check(cpu.reg16(Cpu::EBX)==0x0050 && cpu.reg16(Cpu::ESP)==0x0c4c,
        "Far jump leaves inherited SS in use until an explicit reload");
  one(cpu,"load invalid protected stack selector");
  fail(cpu,FaultCode::ProtectionFault,"MOV SS must reject protected RPL3 at CPL0");
  check(cpu.segments[Cpu::SS]==0x0313,
        "A rejected protected SS reload keeps the inherited selector");
  cpu.fault.reset();
  cpu.eip+=2;
  one(cpu,"load valid protected stack selector");
  one(cpu,"reload SS from the GDT");
  one(cpu,"PUSH after reloading protected SS");
  check(cpu.segments[Cpu::SS]==0x10 && cpu.reg16(Cpu::ESP)==0x0c4a &&
        cpu.read16(0x40000+0x0c4a)==0x0051 && cpu.read16(0x03130+0x0c4a)==0x0050,
        "A valid protected SS reload uses its GDT base and preserves old stack memory");

  cpu.reset(0x2003,0x0830); // Real-mode CS low bits must not become CPL3.
  cpu.memory.write_block(0x20860,enter.data(),enter.size());
  for (unsigned i=0;i<6;++i) one(cpu,"real-to-protected setup with nonzero CS low bits");
  check(cpu.cpl()==0 && cpu.segments[Cpu::CS]==0x2003,
        "Inherited real-mode CS does not set protected CPL");
  cpu.set_reg16(Cpu::ESP,1);
  const auto boundary_byte=cpu.read8(0x03130+0xffff);
  fail(cpu,FaultCode::SegmentLimit,"Inherited stack still checks the 64 KiB segment limit");
  check(cpu.reg16(Cpu::ESP)==1 && cpu.read8(0x03130+0xffff)==boundary_byte,
        "Out-of-limit PUSH must not move SP or write memory");
}
void check_group6() {
  Cpu cpu(2*1024*1024);
  program(cpu,{0x0f,0x00,0xc0});
  fail(cpu,FaultCode::InvalidInstruction,"SLDT not valid in real mode");
  check(cpu.fault->bytes==std::vector<std::uint8_t>({0x0f,0x00,0xc0}),
        "real-mode group6 diagnostic includes ModRM, not just 0f00");

  protected_cpu(cpu);
  check(std::string(cpu.mode_name())=="protected16","test enters protected16");
  cpu.regs[Cpu::EAX]=0x1234beef;
  program(cpu,{0x0f,0x00,0xc0}); // SLDT AX
  one(cpu,"SLDT AX");
  check(cpu.regs[Cpu::EAX]==0x12340000,"SLDT AX zero selector preserves upper bits");
  check(cpu.eip==0x1003,"SLDT advances three decoded bytes");
  cpu.regs[Cpu::EAX]=0x11223344u;
  program(cpu,{0x0f,0x00,0xc8}); // STR AX
  one(cpu,"STR AX");
  check(cpu.regs[Cpu::EAX]==0x11220000,"STR AX initially zero");

  cpu.regs[Cpu::EAX]=0x0010;
  program(cpu,{0x0f,0x00,0xd0}); // LLDT AX from valid GDT descriptor
  one(cpu,"LLDT LDT selector");
  check(cpu.ldtr.valid && cpu.ldtr.selector==0x10 &&
        cpu.ldtr.base==0x9000 && cpu.ldtr.limit==0x1f,
        "LLDT must load visible selector and hidden base/limit");
  program(cpu,{0x0f,0x00,0xc0}); // SLDT AX
  one(cpu,"SLDT after LLDT");
  check(cpu.reg16(Cpu::EAX)==0x10,"SLDT reflects loaded selector");

  cpu.regs[Cpu::EAX]=0x0018;
  program(cpu,{0x0f,0x00,0xd8}); // LTR AX
  one(cpu,"LTR TSS");
  check(cpu.tr.valid && cpu.tr.selector==0x18 &&
        cpu.tr.base==0x8000 && cpu.tr.limit==0x67,
        "LTR must load TSS selector and hidden descriptor");
  check(((cpu.read64(0x3018)>>40)&0xffu)==0x8bu,
        "LTR marks available 32-bit TSS busy in guest GDT");
  cpu.regs[Cpu::EAX]=0x12345678;
  program(cpu,{0x0f,0x00,0xc8}); // STR AX
  one(cpu,"STR after LTR");
  check(cpu.regs[Cpu::EAX]==0x12340018,"STR AX exposes correct TR selector");
  cpu.regs[Cpu::EAX]=0xdeadbeefU;
  program(cpu,{0x66,0x0f,0x00,0xc8}); // STR EAX
  one(cpu,"STR EAX");
  check(cpu.regs[Cpu::EAX]==0x18,"STR with 66 prefix zero extends to 32-bit register");
  program(cpu,{0x0f,0x00,0x0e,0x00,0x18}); // STR word [1800]
  one(cpu,"STR memory");
  check(cpu.read16(0x1800)==0x18,"STR memory writes exactly 16 bits");

  // Verify segment read/write conditions and privilege checks.
  cpu.regs[Cpu::EAX]=0x20;
  program(cpu,{0x0f,0x00,0xe0}); // VERR AX
  one(cpu,"VERR data");
  check(cpu.flag(Cpu::ZF),"VERR readable data sets ZF");
  program(cpu,{0x0f,0x00,0xe8}); // VERW AX
  one(cpu,"VERW data");
  check(cpu.flag(Cpu::ZF),"VERW writable data sets ZF");
  cpu.regs[Cpu::EAX]=0x28;
  program(cpu,{0x0f,0x00,0xe0}); // VERR code
  one(cpu,"VERR code");
  check(cpu.flag(Cpu::ZF),"VERR readable code success");
  program(cpu,{0x0f,0x00,0xe8});
  one(cpu,"VERW code");
  check(!cpu.flag(Cpu::ZF),"VERW code never allows writes");
  cpu.regs[Cpu::EAX]=0x38;
  program(cpu,{0x0f,0x00,0xe0}); // execute-only
  one(cpu,"VERR execute-only");
  check(!cpu.flag(Cpu::ZF),"VERR rejects execute-only code");
  cpu.regs[Cpu::EAX]=0x23;
  program(cpu,{0x0f,0x00,0xe8}); // data DPL0, RPL3
  one(cpu,"VERW RPL3 DPL0");
  check(!cpu.flag(Cpu::ZF),"DPL0 inaccessible with RPL3");
  cpu.regs[Cpu::EAX]=0x33;
  program(cpu,{0x0f,0x00,0xe8}); // data DPL3, RPL3
  one(cpu,"VERW RPL3 DPL3");
  check(cpu.flag(Cpu::ZF),"DPL3 accessible with RPL3");
  cpu.regs[Cpu::EAX]=0x18;
  program(cpu,{0x0f,0x00,0xe0}); // system descriptor TSS
  one(cpu,"VERR system descriptor");
  check(!cpu.flag(Cpu::ZF),"system descriptor is not readable segment");
  cpu.regs[Cpu::EAX]=0x40;
  program(cpu,{0x0f,0x00,0xe0});
  one(cpu,"VERR not-present");
  check(!cpu.flag(Cpu::ZF),"not-present descriptor sets ZF=0 without fault");
  cpu.regs[Cpu::EAX]=0;
  program(cpu,{0x0f,0x00,0xe0});
  one(cpu,"VERR null selector");
  check(!cpu.flag(Cpu::ZF),"null selector ZF=0 without fault");
  cpu.regs[Cpu::EAX]=0xfff8;
  program(cpu,{0x0f,0x00,0xe0});
  one(cpu,"VERR outside GDT");
  check(!cpu.flag(Cpu::ZF),"selector exceeding GDT sets ZF=0, no CPU fault");
  cpu.regs[Cpu::EAX]=0x0c; // LDT TI=1, index=8
  program(cpu,{0x0f,0x00,0xe0});
  one(cpu,"VERR LDT segment");
  check(cpu.flag(Cpu::ZF),"VERR resolves a valid segment in loaded LDTR");
  cpu.regs[Cpu::EAX]=0;
  program(cpu,{0x0f,0x00,0xd0}); // null LLDT
  one(cpu,"LLDT null");
  check(!cpu.ldtr.valid && cpu.ldtr.selector==0,"LLDT null marks LDTR invalid");
  cpu.regs[Cpu::EAX]=0x0c;
  program(cpu,{0x0f,0x00,0xe0});
  one(cpu,"VERR absent LDTR");
  check(!cpu.flag(Cpu::ZF),"VERR with unloaded LDT sets ZF=0");

  // Operand forms and rollback guards.
  cpu.memory[0x1800]=0x10;
  cpu.memory[0x1801]=0;
  program(cpu,{0x0f,0x00,0x16,0x00,0x18}); // LLDT [1800] (group 2, mod 00 rm 6)
  one(cpu,"LLDT memory operand");
  check(cpu.ldtr.selector==0x10,"LLDT m16 selector");
  program(cpu,{0x0f,0x00,0x06,0x02,0x18}); // SLDT [1802]
  one(cpu,"SLDT memory operand");
  check(cpu.read16(0x1802)==0x10,"SLDT m16 selector");
  cpu.memory[0x1804]=0x20;
  cpu.memory[0x1805]=0;
  program(cpu,{0x0f,0x00,0x26,0x04,0x18}); // VERR [1804]
  one(cpu,"VERR memory operand");
  check(cpu.flag(Cpu::ZF),"VERR m16 uses actual guest memory operand");

  cpu.regs[Cpu::EAX]=0x18;
  program(cpu,{0x0f,0x00,0xd8}); // busy TSS invalid
  fail(cpu,FaultCode::ProtectionFault,"LTR must reject busy TSS");
  check(cpu.fault->bytes.size()==3 &&
        cpu.fault->opcode==0x0f00,
        "fault identifies complete 0F00 opcode and ModRM");
  check(((cpu.read64(0x3018)>>40)&0xffu)==0x8bu,
        "failed LTR does not change guest descriptor");
  program(cpu,{0x0f,0x00,0xf0});
  fail(cpu,FaultCode::InvalidInstruction,"0F00 /6 reserved encoding");
  check(cpu.fault->bytes.size()==3,"reserved group includes ModRM");
  program(cpu,{0x0f,0x00,0xf8});
  fail(cpu,FaultCode::InvalidInstruction,"0F00 /7 reserved encoding");

  protected_cpu(cpu);
  cpu.regs[Cpu::EAX]=0;
  program(cpu,{0x0f,0x00,0xd8});
  fail(cpu,FaultCode::ProtectionFault,"LTR null selector rejected");
  cpu.regs[Cpu::EAX]=0x20; // regular data descriptor, not TSS
  program(cpu,{0x0f,0x00,0xd8});
  fail(cpu,FaultCode::ProtectionFault,"LTR must not accept regular data descriptor");
  cpu.regs[Cpu::EAX]=0x001c; // TI=1, illegal for task register
  program(cpu,{0x0f,0x00,0xd8});
  fail(cpu,FaultCode::ProtectionFault,"LTR only loads GDT selectors");
  cpu.regs[Cpu::EAX]=0x38; // code segment, wrong for LDT
  program(cpu,{0x0f,0x00,0xd0});
  fail(cpu,FaultCode::ProtectionFault,"LLDT requires system LDT descriptor");
  cpu.regs[Cpu::EAX]=0x40; // not-present descriptor
  program(cpu,{0x0f,0x00,0xd0});
  fail(cpu,FaultCode::ProtectionFault,"LLDT rejects not-present descriptor");

  // 16-bit TSS valid descriptor also marks busy, with correct min limit.
  cpu.write64(0x3018,descriptor(0x8800,0x2b,0x81));
  cpu.regs[Cpu::EAX]=0x18;
  program(cpu,{0x0f,0x00,0xd8});
  one(cpu,"LTR 286-compatible 16-bit available TSS");
  check(cpu.tr.valid && cpu.tr.limit==0x2b &&
        ((cpu.read64(0x3018)>>40)&0xffu)==0x83u,
        "LTR supports 16-bit available TSS and sets busy type 3");
  program(cpu,{0x0f,0x00,0xc8});
  one(cpu,"STR after 286 TSS");
  check(cpu.reg16(Cpu::EAX)==0x18,"STR after 286 TSS");
  protected_cpu(cpu);
  cpu.write64(0x3018,descriptor(0x8800,0x2a,0x81));
  cpu.regs[Cpu::EAX]=0x18;
  program(cpu,{0x0f,0x00,0xd8});
  fail(cpu,FaultCode::ProtectionFault,"LTR enforces 286 TSS min limit");
  check(((cpu.read64(0x3018)>>40)&255u)==0x81u,
        "invalid TSS never sets busy bit");
  cpu.reset();
  check(!cpu.ldtr.valid && !cpu.tr.valid &&
        cpu.ldtr.selector==0 && cpu.tr.selector==0,
        "CPU reset clears both system registers");
}
}
int main() {
 try {
    check_protected_entry_stack();
    check_group6();
    std::cout<<"WIN10_GROUP6_0F00_PASS checks="<<checks
             <<" protected16=1 ModRM=1 SLDT=1 STR=1 LLDT=1 LTR=1"
             <<" VERR=1 VERW=1 TSS_busy=1 LDT=1 rollback=1"
             <<" protected_entry_stack=1 windows_ready=0\n";
    return 0;
 } catch(const std::exception& e) {
    std::cerr<<"WIN10_GROUP6_0F00_FAIL: "<<e.what()<<'\n';
    return 1;
 }
}
