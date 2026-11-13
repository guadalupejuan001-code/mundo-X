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
void need(bool value,const char* message) {
  ++checks;if(!value)throw std::runtime_error(message);
}
void setup_ports(Cpu& cpu,std::vector<std::uint8_t>& fifo,
                 std::size_t& write_words,std::size_t& read_words) {
  fifo.clear();fifo.reserve(512);write_words=read_words=0;
  cpu.out_hook=[&](std::uint16_t p,unsigned width,std::uint32_t v)->bool{
    if(p!=0x1f0||width!=2||fifo.size()>=512)return false;
    fifo.push_back(std::uint8_t(v));fifo.push_back(std::uint8_t(v>>8));
    ++write_words;return true;
  };
  cpu.in_hook=[&](std::uint16_t p,unsigned width)->std::optional<std::uint32_t>{
    if(p!=0x1f0||width!=2||read_words>=write_words)return std::nullopt;
    const auto i=read_words++*2;
    return std::uint32_t(fifo[i]) | (std::uint32_t(fifo[i+1])<<8);
  };
}
void guest16(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.regs[Cpu::ESP]=0xf000;cpu.a20_enabled=true;
  std::array<std::uint8_t,512> source{},target{};
  for(std::size_t i=0;i<source.size();++i)source[i]=std::uint8_t(i*19+i/7);
  cpu.memory.write_block(0x8000,source.data(),source.size());
  constexpr std::array<std::uint8_t,21> program{
    0xbe,0x00,0x80, // MOV SI,8000
    0xb9,0x00,0x01, // MOV CX,256
    0xba,0xf0,0x01, // MOV DX,1F0
    0xfc,           // CLD
    0xf3,0x6f,      // REP OUTSW
    0xbf,0x00,0x90, // MOV DI,9000
    0xb9,0x00,0x01, // MOV CX,256
    0xf3,0x6d,      // REP INSW
    0xf4            // HLT
  };
  cpu.memory.write_block(0x1000,program.data(),program.size());
  std::vector<std::uint8_t> fifo;std::size_t writes=0,reads=0;
  setup_ports(cpu,fifo,writes,reads);
  bool observed_bounded=false;
  for(unsigned step=0;step<900;++step){
    const auto before=cpu.instructions;
    const auto result=cpu.step();
    if(result==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    need(cpu.instructions==before+1,"REP PIO CPU step counted multiple instructions");
    if(cpu.eip==0x100a && writes==1){
      observed_bounded=true;
      need(cpu.reg16(Cpu::ECX)==255&&cpu.reg16(Cpu::ESI)==0x8002,
           "REP OUTSW did not perform exactly one 16-bit word per bounded CPU step");
    }
    if(result==StepResult::Halted)break;
  }
  need(cpu.halted,"REP PIO guest did not halt");
  cpu.memory.read_block(0x9000,target.data(),target.size());
  need(observed_bounded&&fifo.size()==512&&writes==256&&reads==256,
       "REP OUTSW/INSW did not transport 256 guest words exactly");
  need(source==target,"real guest PIO byte-for-byte round trip failed");
  need(cpu.reg16(Cpu::ESI)==0x8200&&cpu.reg16(Cpu::EDI)==0x9200 &&
       cpu.reg16(Cpu::ECX)==0,
       "string ports did not update SI/DI/CX correctly");
  std::cout<<"C07_MUNDO_X_REAL16_REP_PIO_PASS writes="<<writes<<" reads="<<reads<<"\n";
}
void guest32(){
  Cpu cpu(16*1024*1024);
  const auto disk=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,disk.data(),disk.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20 service");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  bool reached=false;
  for(unsigned i=0;i<10000;++i){
    const auto step=cpu.step();
    if(step==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(std::string(cpu.mode_name())=="protected32"&&cpu.segments[Cpu::SS]==0x10){
      reached=true;break;
    }
  }
  need(reached,"guest did not enter protected32");
  std::array<std::uint8_t,512> source{},target{};
  for(std::size_t i=0;i<source.size();++i)source[i]=std::uint8_t(i*53+7);
  cpu.memory.write_block(0x18000,source.data(),source.size());
  constexpr std::array<std::uint8_t,31> program{
    0xbe,0x00,0x80,0x01,0x00, // MOV ESI,18000
    0xb9,0x00,0x01,0x00,0x00, // MOV ECX,256
    0x66,0xba,0xf0,0x01,      // MOV DX,1F0
    0xfc,
    0xf3,0x66,0x6f,          // REP OUTSW (66 overrides default 32bit)
    0xbf,0x00,0x90,0x01,0x00, // MOV EDI,19000
    0xb9,0x00,0x01,0x00,0x00, // MOV ECX,256
    0xf3,0x66,0x6d,          // REP INSW
    0xf4
  };
  cpu.memory.write_block(0xa000,program.data(),program.size());
  cpu.eip=0xa000;
  cpu.halted=false;cpu.fault.reset();
  std::vector<std::uint8_t> fifo;std::size_t writes=0,reads=0;
  setup_ports(cpu,fifo,writes,reads);
  for(unsigned i=0;i<1000;++i){
    const auto result=cpu.step();
    if(result==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(result==StepResult::Halted)break;
  }
  cpu.memory.read_block(0x19000,target.data(),target.size());
  need(cpu.halted&&writes==256&&reads==256&&target==source,
       "protected32 REP INSW/OUTSW did not complete correctly");
  need(cpu.regs[Cpu::ESI]==0x18200&&cpu.regs[Cpu::EDI]==0x19200,
       "protected32 full32 index address update failed");
  std::cout<<"C07_MUNDO_X_PROTECTED32_REP_PIO_PASS writes="<<writes<<"\n";
}
}
int main(){
  try{guest16();guest32();
    std::cout<<"REVERSE_WINDOWS_C07_CPU_PIO_PASS checks="<<checks
             <<" windows_booted=false\n";return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C07_CPU_PIO_FAIL "<<e.what()<<"\n";return 1;
  }
}
