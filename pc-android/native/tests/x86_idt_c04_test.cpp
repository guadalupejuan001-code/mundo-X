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
  need(cpu.read64(frame+24)==(2|Cpu::IF|Cpu::RF),"#PF must set RF in saved RFLAGS");
  need(cpu.read64(frame+32)==0x190000,"#PF saved RSP mismatch");
  need(!cpu.flag(Cpu::IF) && !cpu.flag(Cpu::RF),"interrupt gate must clear live IF and RF");
  for (unsigned i=0; i<4; ++i)
    need(cpu.step()==StepResult::Executed,"#PF handler must repair PTE and execute IRETQ");
  need(cpu.eip==0xa000 && cpu.flag(Cpu::IF) && cpu.flag(Cpu::RF),
       "IRETQ must restore the interrupted RIP, IF and saved RF");
  need(cpu.step()==StepResult::Executed && !cpu.flag(Cpu::RF),
       "The resumed instruction must consume RF");
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
  // A benign #UD with an invalid gate first raises #GP.
  gate(cpu,13,0x12600,true);
  gate(cpu,8,0x12800,true);
  write_code(cpu,0xa000,{0x0f,0x0b});
  expect_next(cpu,0x12600,13);
  need(cpu.read64(cpu.regs[Cpu::ESP])==((6U<<3)|3U) &&
       cpu.read64(cpu.regs[Cpu::ESP]+8)==0xa000,
       "The replacement #GP must preserve the invalid #UD gate code and original RIP");

  // A second #GP trying to deliver #GP requires #DF.
  cpu.write64(cpu.idtr.base + 13*16,0);
  cpu.write64(cpu.idtr.base + 13*16 + 8,0);
  cpu.eip=0xa000; cpu.regs[Cpu::ESP]=0x190000;
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
void faults_during_exception_delivery() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  constexpr auto tss = 0x17000U, ist_address = tss + 36;
  for (const bool original_page_fault : {false,true}) {
    start(cpu);
    cpu.write64(0x3000,0x4003);
    for (unsigned i=0;i<512;++i) cpu.write64(0x4000+i*8,std::uint64_t(i*4096)|3U);
    cpu.write64(0x4000+(tss>>12)*8,0);
    cpu.tr.base=tss; cpu.tr.limit=0x67; cpu.tr.valid=true;
    gate(cpu,8,0x12800,true);
    gate(cpu,13,0x12600,true);
    gate(cpu,14,0x12400,true);
    cpu.write64(cpu.idtr.base+3*16,0); // Invalid INT3 gate raises the first #GP.
    cpu.write64(cpu.idtr.base+3*16+8,0);
    const unsigned vector = original_page_fault ? 14 : 13;
    const auto slot = cpu.idtr.base+vector*16;
    cpu.write64(slot,cpu.read64(slot)|(1ULL<<32)); // IST1 reads from the absent TSS page.
    if (original_page_fault) {
      cpu.write64(0x4000+11*8,0);
      write_code(cpu,0xa000,{0x48,0x8b,0x04,0x25,0x00,0xb0,0x00,0x00});
    } else {
      write_code(cpu,0xa000,{0xcc});
    }
    expect_next(cpu,original_page_fault ? 0x12800 : 0x12400,original_page_fault ? 8 : 14);
    need(cpu.cr2==ist_address && cpu.read64(cpu.regs[Cpu::ESP])==0 &&
         cpu.read64(cpu.regs[Cpu::ESP]+8)==0xa000,
         "The latest #PF address and clean original stack must survive delivery rollback");
  }
  std::cout<<"C04_NESTED_EXCEPTION_CLASSES_PASS serial=1 double=1 latest_cr2=1\n";
}
void long_cached_gate_reads() {
  Cpu cpu(4U<<20);
  start(cpu);
  cpu.enable_page_translation_cache();
  gate(cpu,3,0x12600,true);
  write_code(cpu,0xa000,{0xcc,0xeb,0xfd}); // INT3; JMP back.
  write_code(cpu,0x12600,{0x48,0xcf}); // IRETQ
  constexpr unsigned iterations=512;
  for (unsigned i=0;i<3*iterations;++i)
    if (cpu.step()!=StepResult::Executed) throw std::runtime_error("cached INT3 delivery failed");
  need(cpu.idt_bulk_reads==iterations && cpu.eip==0xa000 && cpu.regs[Cpu::ESP]==0x190000,
       "Exact 16-byte gate reads must retain complete interrupt and IRETQ behavior");
  std::cout<<"C04_ETHER_IDT_READ_PASS bulk_reads="<<cpu.idt_bulk_reads<<" gate_bytes=16\n";
}
void high_kernel_interrupt_origin() {
  Cpu cpu(4U<<20);
  start(cpu);
  constexpr auto high = 0xffff800000000000ULL;
  cpu.write64(0x1800,0x2003); // Map the low boot page tables into the high half.
  cpu.write64(0x3000,0x4003);
  for (unsigned i=0;i<512;++i) cpu.write64(0x4000+i*8,std::uint64_t(i*4096)|3U);
  cpu.write8(0x190000,0xab); // The trace crosses onto this unaccessed stack page.
  cpu.write16(0xb000,0x0fff);
  cpu.write64(0xb002,high+0x15000);
  write_code(cpu,0xa000,{0x0f,0x01,0x1d,0xf9,0x0f,0,0, 0xe8,0xf4,0,0,0});
  write_code(cpu,0xa100,{0xcc,0xc3});
  cpu.write64(0x15030,0);
  cpu.write64(0x150d0,0);
  cpu.write64(0x15080,0);
  cpu.eip=high+0xa000;
  cpu.regs[Cpu::ESP]=high+0x190000;
  cpu.regs[Cpu::ECX]=0x42;
  need(cpu.step()==StepResult::Executed && cpu.step()==StepResult::Executed,
       "64-bit LIDT and CALL fixture");
  need(cpu.step()==StepResult::Faulted && cpu.fault && cpu.fault->interrupt_origin,
       "Terminal INT3 must retain its origin evidence");
  const auto& origin=*cpu.fault->interrupt_origin;
  need(origin.last_idt_load && origin.last_idt_load->ip==high+0xa000 &&
       origin.last_idt_load->operand==high+0xb000 && origin.last_idt_load->base==high+0x15000,
       "Captured LIDT must match the instruction, operand and table");
  need(origin.call_count==1 && origin.recent_calls[0].from==high+0xa007 &&
       origin.recent_calls[0].target==origin.rip && origin.recent_calls[0].return_ip==high+0xa00c &&
       origin.recent_calls[0].argument_registers[0]==0x42 &&
       origin.rsp==high+0x18fff8 && origin.rsp==origin.recent_calls[0].rsp,
       "Captured CALL must preserve the real caller, argument and resume address");
  need(origin.code_bytes==32 && origin.code[0]==0xcc && origin.code[1]==0xc3 &&
       origin.stack_bytes==128 && origin.stack[0]==0x0c && origin.stack[1]==0xa0 &&
       origin.stack[5]==0x80 && origin.stack[6]==0xff && origin.stack[7]==0xff && origin.stack[8]==0xab,
       "Fault evidence must read guest code and the actual saved return address");
  need((cpu.read64(0x4000+(0x190000>>12)*8)&0x20)==0,
       "Fault evidence must not set the accessed bit of the next stack page");
  std::cout<<"C04_INTERRUPT_ORIGIN_PASS lidt=1 calls="<<origin.call_count
           <<" code_bytes="<<origin.code_bytes<<" stack_bytes="<<origin.stack_bytes<<"\n";
}
void split_gate_fault_prefix() {
  Cpu cpu(4U<<20);
  start(cpu);
  cpu.enable_page_translation_cache();
  cpu.write64(0x3000,0x4003);
  for (unsigned i=0;i<512;++i) cpu.write64(0x4000+i*8,std::uint64_t(i*4096)|3U);
  cpu.idtr.base=0x15fc8;
  constexpr auto low=0x00008e000018b340ULL;
  cpu.write64(0x15ff8,low);
  cpu.write64(0x4000+(0x16000>>12)*8,0);
  write_code(cpu,0xa000,{0xcc});
  need(cpu.step()==StepResult::Faulted && cpu.fault && cpu.fault->idt_attempt_count==3,
       "INT3 across an unmapped half of the IDT gate must fault precisely");
  const auto& first=cpu.fault->idt_attempts[0];
  need(!cpu.idt_bulk_reads && first.bytes_read==8 && first.low==low &&
       first.failure_vector==14 && first.failure_address==0x16000,
       "The gate fast path must leave partial reads and #PF addresses intact");
}
void long_int3_gate_faults() {
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024); start(cpu);
  constexpr std::uint64_t handler = 0x12800;
  write_code(cpu,0xa000,{0xcc});
  gate(cpu,3,0x12600,true,0x0e);
  gate(cpu,11,handler,true);
  expect_next(cpu,handler,11);
  need(cpu.read64(cpu.regs[Cpu::ESP]) == ((3U << 3) | 2U) &&
           cpu.read64(cpu.regs[Cpu::ESP] + 8) == 0xa000,
       "INT3 into a nonpresent gate must deliver #NP with its IDT selector code and restart RIP");

  cpu.eip = 0xa000;
  cpu.regs[Cpu::ESP] = 0x190000;
  const auto slot = cpu.idtr.base + 3 * 16;
  constexpr std::uint64_t low = 0x00008a000018b340ULL;
  constexpr std::uint64_t high = 0x0000000012345678ULL;
  cpu.write64(slot,low);
  cpu.write64(slot + 8,high);
  need(cpu.step() == StepResult::Faulted && cpu.fault &&
           cpu.fault->idt_attempt_count == 3, "a terminal INT3 must retain all three IDT rejections");
  const auto& trace = cpu.fault->idt_attempts;
  need(trace[0].source == cosmic_pc::IdtSource::SoftwareInterrupt && trace[0].vector == 3 &&
           trace[0].entry_address == slot && trace[0].bytes_read == 16 &&
           trace[0].low == low && trace[0].high == high && trace[0].failure_vector == 13 &&
           trace[0].failure_error == ((3U << 3) | 2U),
       "INT3 fault did not capture the bytes actually read and the gate error");
  need(trace[1].vector == 13 && trace[1].stacked_error == ((3U << 3) | 2U) &&
           trace[2].vector == 8 && trace[2].has_error && trace[2].stacked_error == 0 &&
           trace[2].failure_error == ((8U << 3) | 3U),
       "fault diagnostics must preserve the #GP and #DF attempts in execution order");
  need(cpu.regs[Cpu::ESP] == 0x190000, "a terminal exception must roll back partial frames");
  std::cout << "C04_INT3_IDT_DIAGNOSTIC_PASS attempts=3 np=1\n";
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
    faults_during_exception_delivery();
    long_cached_gate_reads();
    high_kernel_interrupt_origin();
    split_gate_fault_prefix();
    long_int3_gate_faults();
    protected32_soft_int_and_ud();
    std::cout<<"REVERSE_WINDOWS_C04_IDT_PASS checks="<<checks
             <<" windows10_booted=false ring3=false tss=false\n";
    return 0;
  }catch (const std::exception& error) {
    std::cerr<<"REVERSE_WINDOWS_C04_IDT_FAIL "<<error.what()<<"\n";
    return 1;
  }
}
