#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void need(bool ok,const char* what){++checks;if(!ok)throw std::runtime_error(what);}
void real_mode_trace(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);
  cpu.a20_enabled=true;
  const std::uint8_t code[]{0xb8,0x34,0x12,0xf4};
  cpu.memory.write_block(0x1000,code,sizeof(code));
  std::vector<Cpu::StepTrace> events;
  cpu.step_trace_hook=[&](const Cpu::StepTrace& event){events.push_back(event);};
  need(cpu.step()==StepResult::Executed,"x86 MOV AX failed");
  need(cpu.step()==StepResult::Halted,"x86 HLT failed");
  need(events.size()==2,"trace did not emit exactly one event per bounded instruction");
  need(events[0].outcome=="executed"&&events[0].sequence==1&&
       events[0].cs_before==0&&events[0].rip_before==0x1000&&
       events[0].rip_after==0x1003&&events[0].opcode==0xb8&&
       events[0].mode_before=="real16"&&events[0].mode_after=="real16"&&
       events[0].decoded_bytes==std::vector<std::uint8_t>({0xb8,0x34,0x12}),
       "precise executed opcode/CS:IP/decoded bytes missing");
  need(events[1].outcome=="halted"&&events[1].decoded_bytes.size()==1&&
       events[1].decoded_bytes[0]==0xf4&&events[1].rip_after==0x1004,
       "trace did not describe final x86 HLT");
  // Disabled trace must not intercept or mutate x86 state.
  cpu.step_trace_hook={};
  cpu.reset(0,0x1000);
  need(cpu.step()==StepResult::Executed&&cpu.reg16(Cpu::EAX)==0x1234,
       "disabling trace changed execution");
  need(events.size()==2,"disabled trace unexpectedly emitted records");
  std::cout<<"C09_X86_REAL_INSTRUCTION_TRACE_PASS events=2\n";
}
void fault_trace(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1200);
  cpu.a20_enabled=true;
  cpu.memory[0x1200]=0xf0; cpu.memory[0x1201]=0x90; // LOCK NOP explicitly unsupported.
  std::vector<Cpu::StepTrace> events;
  cpu.step_trace_hook=[&](const Cpu::StepTrace& event){events.push_back(event);};
  need(cpu.step()==StepResult::Faulted&&cpu.fault.has_value(),
       "LOCK prefix must remain a reported terminal fault");
  need(events.size()==1&&events[0].outcome=="fault"&&
       events[0].terminal_fault.has_value()&&
       events[0].terminal_fault->code==cosmic_pc::FaultCode::UnsupportedState &&
       events[0].decoded_bytes==std::vector<std::uint8_t>({0xf0,0x90}) &&
       events[0].rip_before==0x1200&&events[0].rip_after==0x1200,
       "fault trace lost exact opcode bytes / original fault RIP");
  std::cout<<"C09_REAL_UNSUPPORTED_OPCODE_TRACE_PASS bytes=f0\n";
}
void handled_ud_trace(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected BIOS A20 request");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<12000;++i){
    auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(status==StepResult::Halted&&cpu.is_long_mode())break;
  }
  need(cpu.is_long_mode(),"guest didn't enter long64");
  cpu.idtr={0x1000,0x15000};
  cpu.regs[Cpu::ESP]=0x190000;
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  const auto at=cpu.idtr.base+6*16;
  const std::uint64_t dest=0x12200;
  const std::uint64_t gate=(dest&0xffffULL)|(std::uint64_t(0x18)<<16)|
      (std::uint64_t(0x8e)<<40)|(((dest>>16)&0xffffULL)<<48);
  cpu.write64(at,gate);cpu.write64(at+8,0);
  const std::uint8_t code[]{0x0f,0x0b,0xf4};
  const std::uint8_t handler[]{0x48,0x83,0x04,0x24,0x02,0xcf};
  cpu.memory.write_block(0xa000,code,sizeof(code));
  cpu.memory.write_block(dest,handler,sizeof(handler));
  std::vector<Cpu::StepTrace> events;
  cpu.step_trace_hook=[&](const Cpu::StepTrace& event){events.push_back(event);};
  need(cpu.step()==StepResult::Executed && !cpu.fault&&cpu.eip==dest,
       "UD2 did not enter guest x64 IDT handler");
  need(events.size()==1&&events[0].outcome=="exception-delivered"&&
       events[0].delivered_vector==6&&events[0].delivered_error==0&&
       events[0].mode_before=="long64"&&events[0].mode_after=="long64"&&
       events[0].cs_before==0x18 && events[0].rip_before==0xa000&&
       events[0].rip_after==dest&&
       events[0].decoded_bytes==std::vector<std::uint8_t>({0x0f,0x0b}),
       "delivered guest exception trace lacks handler/vector/opcode");
  for(unsigned i=0;i<15;++i){
    const auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(status==StepResult::Halted)break;
  }
  need(cpu.halted&&cpu.eip==0xa003&&events.size()>=4,
       "guest IDT handler did not return into original x64 instruction stream");
  std::cout<<"C09_LONG64_DELIVERED_EXCEPTION_TRACE_PASS vector=6 bytes=0f0b\n";
}
void external_irq_trace(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  cpu.regs[Cpu::ESP]=0x9000;cpu.eflags=2|Cpu::IF;
  cpu.write16(0x20*4,0x1200);cpu.write16(0x20*4+2,0);
  cpu.memory[0x1200]=0xcf; // IRET inside actual IVT ISR
  cpu.memory[0x1000]=0xf4;
  std::vector<Cpu::IrqTrace> irqs;
  cpu.irq_trace_hook=[&](const Cpu::IrqTrace& irq){irqs.push_back(irq);};
  need(cpu.raise_external_irq(0x20)&&cpu.eip==0x1200,
       "IRQ20 did not enter guest real-mode IVT handler");
  need(irqs.size()==1&&irqs[0].delivered&&
       irqs[0].vector==0x20&&irqs[0].cs_before==0 &&
       irqs[0].rip_before==0x1000&&irqs[0].rip_after==0x1200,
       "C09 trace invented or lost actual hardware IRQ vector");
  need(cpu.step()==StepResult::Executed&&cpu.eip==0x1000,
       "IRQ handler did not return to interrupted IP");
  cpu.irq_trace_hook=[](const Cpu::IrqTrace&){throw std::runtime_error("sink failed");};
  need(cpu.raise_external_irq(0x20),"hardware IRQ callback exception affected delivery");
  std::cout<<"C09_EXTERNAL_IRQ_VECTOR_TRACE_PASS vector=32\n";
}
void callback_is_observation_only(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.memory[0x1000]=0xf4;
  cpu.step_trace_hook=[](const Cpu::StepTrace&){throw std::runtime_error("trace sink failure");};
  need(cpu.step()==StepResult::Halted&&!cpu.fault,
       "trace output exception changed guest execution");
  std::cout<<"C09_OBSERVATION_ONLY_TRACE_PASS\n";
}
}
int main(){
  try{
    real_mode_trace();fault_trace();handled_ud_trace();external_irq_trace();callback_is_observation_only();
    std::cout<<"REVERSE_WINDOWS_C09_CPU_TRACE_PASS checks="<<checks
             <<" windows_booted=false\n";return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09_CPU_TRACE_FAIL "<<e.what()<<"\n";
    return 1;
  }
}
