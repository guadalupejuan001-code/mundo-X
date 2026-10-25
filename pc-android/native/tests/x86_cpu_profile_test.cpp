#include "../x86_cpu.hpp"
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
unsigned checks=0;
void need(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void start(Cpu& cpu,std::uint32_t leaf,std::uint32_t subleaf=0){
    cpu.reset(0,0x1000);
    cpu.memory.write_byte(0x1000,0x0f);
    cpu.memory.write_byte(0x1001,0xa2);
    cpu.memory.write_byte(0x1002,0xf4);
    cpu.regs[Cpu::EAX]=leaf;
    cpu.regs[Cpu::ECX]=subleaf;
    const auto step=cpu.step();
    need(step==StepResult::Executed,"CPUID must execute without fault");
    need(cpu.eip==0x1002,"CPUID must advance by two bytes");
    need(cpu.eflags==2,"CPUID must preserve flags");
}
std::string vendor(Cpu& cpu) {
    std::string result;
    for(const auto value:{cpu.regs[Cpu::EBX],cpu.regs[Cpu::EDX],cpu.regs[Cpu::ECX]})
        for(unsigned i=0;i<4;++i)result+=char((value>>(8*i))&255U);
    return result;
}
}
int main(){
    try{
        Cpu cpu;
        start(cpu,0);
        need(cpu.regs[Cpu::EAX]==7,"CPUID max standard leaf");
        need(vendor(cpu)=="MUNDOX PC   ","CPUID vendor string is not stable");
        start(cpu,1);
        need((cpu.regs[Cpu::EDX]&(1U<<4))!=0,"TSC declared feature");
        need((cpu.regs[Cpu::EDX]&(1U<<6))!=0,"PAE declared for IA-32e");
        need((cpu.regs[Cpu::EDX]&(1U<<26))==0,"must not claim unimplemented SSE2");
        need((cpu.regs[Cpu::EDX]&(1U<<0))==0,"must not claim x87");
        need((cpu.regs[Cpu::ECX]&(1U<<28))==0,"must not claim AVX");
        start(cpu,7);
        need(cpu.regs[Cpu::EBX]==0 && cpu.regs[Cpu::ECX]==0,
             "unsupported extended features must remain zero");
        start(cpu,0x80000000U);
        need(cpu.regs[Cpu::EAX]==0x80000008U,"extended maximum leaf");
        start(cpu,0x80000001U);
        need((cpu.regs[Cpu::EDX]&(1U<<29))!=0,"64-bit modeled feature");
        need((cpu.regs[Cpu::EDX]&(1U<<20))!=0,"NX modeled feature");
        start(cpu,0x80000008U);
        need((cpu.regs[Cpu::EAX]&255)==32,"physical bits");
        need(((cpu.regs[Cpu::EAX]>>8)&255)==48,"virtual bits");
        start(cpu,0xdeadbeefU);
        need(cpu.regs[Cpu::EAX]==0 && cpu.regs[Cpu::EBX]==0&&
             cpu.regs[Cpu::ECX]==0 && cpu.regs[Cpu::EDX]==0,
             "unsupported leaf must return zero");
        cpu.reset(0,0x1000);
        for(unsigned i=0;i<4;++i)cpu.memory.write_byte(0x1000+i, i%2==0?0x0f:0x31);
        auto r=cpu.step();
        need(r==StepResult::Executed&&cpu.regs[Cpu::EAX]==0&&cpu.regs[Cpu::EDX]==0,
             "RDTSC first deterministic 0 cycle");
        r=cpu.step();
        need(r==StepResult::Executed&&cpu.regs[Cpu::EAX]==64&&cpu.regs[Cpu::EDX]==0,
             "RDTSC second deterministic cycle");
        need(cpu.eflags==2,"RDTSC must preserve flags");
        std::cout<<"WIN10_CPU_PROFILE_PASS checks="<<checks
                 <<" SSE2_advertised=0 TSC_synthetic=1 windows_ready=0\n";
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"WIN10_CPU_PROFILE_FAIL "<<ex.what()<<"\n";
        return 1;
    }
}
