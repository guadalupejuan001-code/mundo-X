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
void need(bool ok,const std::string& why) {++checks;if(!ok)throw std::runtime_error(why);}
void boot32(Cpu& cpu) {
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t vector){
    if(vector!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected BIOS request");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<10000;++i) {
    auto s=cpu.step();
    if(s==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(std::string(cpu.mode_name())=="protected32"&&cpu.segments[Cpu::SS]==0x10)return;
  }
  throw std::runtime_error("C06 real CPU did not enter protected32 mode");
}
void mmio_guest() {
  Cpu cpu(16*1024*1024);boot32(cpu);
  std::array<std::uint8_t,4096> registers{};
  constexpr std::uint64_t base=0xfee00000ULL;
  // MMIO memory is never expanded to 4 GiB or stored in the RAM fabric.
  cpu.mmio_claim_hook=[](std::uint64_t address){
    return address>=base&&address<base+4096;
  };
  cpu.mmio_read_hook=[&](std::uint64_t address)->std::optional<std::uint8_t>{
    if(address==base+0x123)return std::nullopt;
    return registers[std::size_t(address-base)];
  };
  cpu.mmio_write_hook=[&](std::uint64_t address,std::uint8_t b){
    if(address==base+0x124)return false;
    registers[std::size_t(address-base)]=b;return true;
  };
  registers[0x30]=0x78;registers[0x31]=0x56;
  registers[0x32]=0x34;registers[0x33]=0x12;
  const std::array<std::uint8_t,16> code{
    0xa1,0x30,0,0xe0,0xfe,        // MOV EAX,[0xFEE00030] via MMIO
    0xa3,0x80,0,0xe0,0xfe,        // MOV [0xFEE00080],EAX
    0x8b,0x0d,0x80,0,0xe0,0xfe}; // MOV ECX,[0xFEE00080]
  cpu.memory.write_block(0xa000,code.data(),code.size());
  cpu.eip=0xa000;
  for(int i=0;i<3;++i){
    auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
  }
  need(cpu.regs[Cpu::EAX]==0x12345678ULL&&cpu.regs[Cpu::ECX]==0x12345678ULL,
       "C06 guest MMIO read/write does not preserve LE DWORD");
  need(registers[0x80]==0x78&&registers[0x83]==0x12,"MMIO host register bytes differ");
  need(cpu.memory.allocated_bytes()<2*1024*1024,
       "MMIO should not allocate guest physical 4 GiB RAM");
  // Device refuses an unmapped register; do not manufacture RAM.
  need(!cpu.mmio_read_hook(base+0x123).has_value()&&
       !cpu.mmio_write_hook(base+0x124,1),"device must refuse unsupported MMIO");
  // Physical reads outside both RAM and MMIO must fail.
  bool failed=false;
  try{(void)cpu.read32(0xfed00000ULL);}catch(const std::exception&){failed=true;}
  need(failed,"unclaimed address must not become implicit host RAM");
  std::cout<<"C06_MMIO_REAL_X86_PASS bytes=4 no_fake_ram=1\n";
}
void external_irq_guest() {
  Cpu cpu;cpu.reset(0,0x1000);cpu.regs[Cpu::ESP]=0x9000;cpu.eflags=2;
  cpu.memory[0x1000]=0x90; // NOP at main code
  cpu.memory[0x1001]=0xf4; // HLT
  cpu.memory[0x1100]=0x40; // INC AX via authentic x86
  cpu.memory[0x1101]=0xcf; // IRET
  cpu.write16(0x20*4,0x1100);cpu.write16(0x20*4+2,0);
  need(!cpu.raise_external_irq(0x20),"masked IF must reject external IRQ");
  need(cpu.eip==0x1000&&cpu.regs[Cpu::ESP]==0x9000,"masked IRQ mutated guest");
  cpu.set_flag(Cpu::IF,true);
  need(cpu.raise_external_irq(0x20),"unmasked external IRQ not delivered");
  need(cpu.eip==0x1100&&cpu.regs[Cpu::ESP]==0x8ffa,
       "external IRQ did not create real-mode IVT stack frame");
  need(!cpu.flag(Cpu::IF),"interrupt gate must clear IF");
  for(int i=0;i<4;++i){
    auto status=cpu.step();
    if(status==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(status==StepResult::Halted)break;
  }
  need(cpu.halted&&cpu.regs[Cpu::EAX]==1&&cpu.eip==0x1002,
       "IRQ handler did not resume interrupted x86 NOP/HLT");
  need(cpu.regs[Cpu::ESP]==0x9000&&cpu.flag(Cpu::IF),
       "IRET did not restore real-mode external IRQ frame");
  std::cout<<"C06_EXTERNAL_IVT_IRQ_PASS masked=1 handler=1\n";
}
}
int main(){
  try{mmio_guest();external_irq_guest();
    std::cout<<"REVERSE_WINDOWS_C06_BUS_CPU_PASS checks="<<checks
             <<" windows_booted=false mmio=true external_irq=true\n";return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C06_BUS_CPU_FAIL "<<e.what()<<"\n";return 1;
  }
}
