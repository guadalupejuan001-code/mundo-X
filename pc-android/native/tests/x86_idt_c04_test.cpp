#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using cosmic_pc::Cpu;
using cosmic_pc::StepResult;

namespace {
unsigned checks = 0;
void need(bool ok, const std::string& message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
void write_code(Cpu& cpu, std::uint64_t address, const std::vector<std::uint8_t>& bytes) {
  cpu.memory.write_block(address, bytes.data(), bytes.size());
}
void write_code(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> bytes) {
  const auto data=std::vector<std::uint8_t>(bytes);
  write_code(cpu,address,data);
}
void run(Cpu& cpu, unsigned budget=100) {
  for (unsigned i=0;i<budget;++i) {
    const auto result=cpu.step();
    if (result==StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
    if (result==StepResult::Halted) return;
  }
  throw std::runtime_error("C04 guest exceeded instruction budget");
}
void boot(Cpu& cpu,bool only32=false) {
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();
  cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& guest,std::uint8_t vector){
    if (vector != 0x15 || guest.reg16(Cpu::EAX)!=0x2401)
      guest.fail("unexpected boot BIOS request");
    guest.a20_enabled=true;guest.set_reg8(4,0);guest.set_flag(Cpu::CF,false);
    return true;
  };
  for (unsigned step=0;step<12000;++step) {
    const auto result=cpu.step();
    if (result==StepResult::Faulted) throw std::runtime_error(cpu.fault->describe());
    if (only32 && std::string(cpu.mode_name())=="protected32" && cpu.segments[Cpu::SS]==0x10)
      return;
    if (!only32 && result==StepResult::Halted && cpu.is_long_mode()) return;
  }
  throw std::runtime_error("C04 boot did not reach requested guest CPU mode");
}
void gate(Cpu& cpu,unsigned vector,std::uint64_t target,bool long_mode,
          std::uint8_t attr=0x8e) {
  const auto offset=cpu.idtr.base+vector*(long_mode?16:8);
  const std::uint64_t low=(target&0xffffULL) |
      (std::uint64_t(long_mode?0x18:0x08)<<16) |
      (std::uint64_t(attr)<<40) |
      (((target>>16)&0xffffULL)<<48);
  cpu.write64(offset,low);
  if (long_mode) cpu.write64(offset+8,(target>>32)&0xffffffffULL);
}
void start(Cpu& cpu,bool long_mode=true) {
  boot(cpu,!long_mode);
  cpu.idtr={0x1000,0x15000};
  cpu.regs[Cpu::ESP]=0x190000;
  cpu.eip=0xa000;
  cpu.halted=false;cpu.fault.reset();
}
void expect_next(Cpu& cpu,std::uint64_t handler,unsigned vector) {
  const auto before=cpu.instructions;
  const auto result=cpu.step();
  need(result==StepResult::Executed && !cpu.fault,
       "guest exception failed IDT vector "+std::to_string(vector)+
       (cpu.fault ? ": "+cpu.fault->describe() : ": no CPU fault"));
  need(cpu.eip==handler,"unexpected IDT handler vector "+std::to_string(vector)+
       " actual RIP="+std::to_string(cpu.eip)+" mode="+cpu.mode_name());
  need(cpu.instructions==before+1,"guest exception did not advance interpreter step");
}
void long_pf_resume() {
  Cpu cpu(64ULL*1024*1024, 16ULL*1024*1024); start(cpu);
  // Expand the boot guest's one 2-MiB identity page into 512 4-KiB pages.
  cpu.write64(0x3000,0x4003);
  for(unsigned i=0;i<512;++i) cpu.write64(0x4000+i*8,std::uint64_t(i*4096)|3U);
  constexpr std::uint64_t target=0xb000, handler=0x12000, pte=0x4000+11*8;
  cpu.write64(target,0x1122334455667788ULL); // host fixture, guest page not present
  cpu.write64(pte,0); // the guest must repair this PTE itself
  gate(cpu,14,handler,true);
  gate(cpu,8,0x12800,true);
  write_code(cpu,0x12800,{0xf4}); // unexpected #DF must not masquerade as PASS
  // MOV RAX, [absolute disp32 B000]; HLT.
  write_code(cpu,0xa000,{0x48,0x8b,0x04,0x25,0x00,0xb0,0x00,0x00,0xf4});
  // Guest handler: MOV RAX, PTE value; MOV [PTE],RAX; ADD RSP,8; IRETQ.
  write_code(cpu,handler,{0x48,0xb8,0x03,0xb0,0,0,0,0,0,0,
                          0x48,0xa3,0x58,0x40,0,0,0,0,0,0,
                          0x48,0x83,0xc4,0x08,0x48,0xcf});
  cpu.eflags=2|Cpu::IF;
  expect_next(cpu,handler,14);
  need(cpu.cr2==target,"#PF did not set CR2 to faulting linear address");
  need(cpu.regs[Cpu::ESP]==0x190000-48,"#PF 64-bit frame must contain SS/RSP/RFLAGS/CS/RIP/error");
  const auto frame=cpu.regs[Cpu::ESP];
  need(cpu.read64(frame)==0,"#PF error must indicate not-present read at CPL0");
  need(cpu.read64(frame+8)==0xa000,"#PF restart RIP mismatch");
  need(cpu.read64(frame+16)==0x18,"#PF saved CS mismatch");
  need(cpu.read64(frame+24)==(2|Cpu::IF),"#PF saved RFLAGS mismatch");
  need(cpu.read64(frame+32)==0x190000,"#PF saved RSP mismatch");
  need(!cpu.flag(Cpu::IF),"interrupt gate must clear IF on entry");
  run(cpu,20);
  need(cpu.regs[Cpu::EAX]==0x1122334455667788ULL,"guest handler failed to repair PTE/retry MOV");
  need(cpu.eip==0xa009 && cpu.regs[Cpu::ESP]==0x190000,"IRETQ did not restore RIP/RSP");
  need(cpu.flag(Cpu::IF),"IRETQ did not restore IF");
  need((cpu.read64(pte)&3U)==3,"guest handler did not install a present writable PTE");
  std::cout<<"C04_LONG_PF_RETRY_PASS cr2="<<cpu.cr2<<"\n";
}
void long_ud_skip() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024); start(cpu);
  gate(cpu,6,0x12200,true);
  write_code(cpu,0xa000,{0x0f,0x0b,0xb8,0x34,0x12,0,0,0xf4});
  // ADD QWORD PTR [RSP],2; IRETQ: #UD carries no error code.
  write_code(cpu,0x12200,{0x48,0x83,0x04,0x24,0x02,0x48,0xcf});
  expect_next(cpu,0x12200,6);
  need(cpu.regs[Cpu::ESP]==0x190000-40,"#UD must not push an error code");
  run(cpu);
  need(cpu.regs[Cpu::EAX]==0x1234 && cpu.regs[Cpu::ESP]==0x190000,
       "UD2 guest handler did not skip opcode and restore stack");
  std::cout<<"C04_LONG_UD_PASS\n";
}
void long_de_retry() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024); start(cpu);
  gate(cpu,0,0x12400,true);
  // MOV EAX,16; XOR EDX,EDX; MOV ECX,0; DIV ECX; HLT.
  write_code(cpu,0xa000,{0xb8,0x10,0,0,0,0x31,0xd2,0xb9,0,0,0,0,
                          0xf7,0xf1,0xf4});
  // Guest handler repairs the divisor and returns to the original DIV.
  write_code(cpu,0x12400,{0xb9,0x01,0,0,0,0x48,0xcf});
  run(cpu,25);
  need(cpu.regs[Cpu::EAX]==16 && cpu.regs[Cpu::ECX]==1 &&
       cpu.regs[Cpu::ESP]==0x190000,"#DE IRETQ did not retry DIV correctly");
  std::cout<<"C04_LONG_DE_PASS\n";
}
void long_gp_skip() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024); start(cpu);
  gate(cpu,13,0x12600,true);
  // Invalid high bit in CR0 -> #GP(0), then guest advances saved RIP by 3.
  write_code(cpu,0xa000,{0x48,0xb8,0,0,0,0,1,0,0,0,
                          0x0f,0x22,0xc0,0xb8,0x78,0x56,0,0,0xf4});
  write_code(cpu,0x12600,{0x48,0x83,0x44,0x24,0x08,0x03,
                           0x48,0x83,0xc4,0x08,0x48,0xcf});
  need(cpu.step()==StepResult::Executed,"MOV RAX fixture");
  expect_next(cpu,0x12600,13);
  need(cpu.read64(cpu.regs[Cpu::ESP])==0,"#GP must supply zero selector error code");
  run(cpu,20);
  need(cpu.regs[Cpu::EAX]==0x5678 && cpu.regs[Cpu::ESP]==0x190000,
       "guest #GP handler did not skip invalid control register write");
  std::cout<<"C04_LONG_GP_PASS\n";
}
void long_double_fault_and_rejection() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024); start(cpu);
  // Missing #UD IDT gate, but valid #DF gate: reject first delivery and enter #DF.
  gate(cpu,8,0x12800,true);
  write_code(cpu,0xa000,{0x0f,0x0b});
  write_code(cpu,0x12800,{0xf4});
  expect_next(cpu,0x12800,8);
  need(cpu.read64(cpu.regs[Cpu::ESP])==0,"#DF pushes zero error code");
  cpu.halted=false; cpu.fault.reset();
  // Now remove #DF; without any valid gate, the machine must NOT pretend a return.
  cpu.idtr={0,0x15000};
  cpu.eip=0xa000; cpu.regs[Cpu::ESP]=0x190000;
  need(cpu.step()==StepResult::Faulted && cpu.fault &&
       cpu.fault->reason.find("triple fault")!=std::string::npos,
       "invalid/missing IDT must be a visible terminal triple fault");
  std::cout<<"C04_DOUBLE_AND_TRIPLE_FAULT_PASS\n";
}
void protected32_soft_int_and_ud() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024); start(cpu,false);
  gate(cpu,0x33,0x13000,false);
  gate(cpu,6,0x13100,false);
  // INT 0x33; MOV EAX,0x33; UD2; HLT.
  write_code(cpu,0xa000,{0xcd,0x33,0xb8,0x33,0,0,0,0x0f,0x0b,0xf4});
  // Handler writes a visible guest marker before IRETD.
  write_code(cpu,0x13000,{0xc7,0x05,0x00,0xf0,0,0,0x44,0,0,0,0xcf});
  write_code(cpu,0x13100,{0x83,0x04,0x24,0x02,0xcf});
  cpu.eflags=2|Cpu::IF;
  expect_next(cpu,0x13000,0x33);
  need(cpu.regs[Cpu::ESP]==0x190000-12,"protected32 INT frame must use 12 bytes");
  need(!cpu.flag(Cpu::IF),"protected32 interrupt gate did not clear IF");
  run(cpu,30);
  need(cpu.read32(0xf000)==0x44,"protected32 guest handler never executed");
  need(cpu.regs[Cpu::EAX]==0x33 && cpu.regs[Cpu::ESP]==0x190000,
       "IRETD or protected32 UD2 guest exception failed");
  need(cpu.flag(Cpu::IF),"IRETD failed to restore IF");
  std::cout<<"C04_PROTECTED32_INT_UD_PASS\n";
}
}
int main() {
  try {
    long_pf_resume();
    long_ud_skip();
    long_de_retry();
    long_gp_skip();
    long_double_fault_and_rejection();
    protected32_soft_int_and_ud();
    std::cout<<"REVERSE_WINDOWS_C04_IDT_PASS checks="<<checks
             <<" windows10_booted=false ring3=false tss=false\n";
    return 0;
  }catch (const std::exception& error) {
    std::cerr<<"REVERSE_WINDOWS_C04_IDT_FAIL "<<error.what()<<"\n";
    return 1;
  }
}
