#include "../x86_cpu.hpp"
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
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

void emit(Cpu& cpu, std::uint64_t address, std::initializer_list<std::uint8_t> bytes) {
    cpu.memory.write_block(address, bytes.begin(), bytes.size());
}

void step(Cpu& cpu, const char* label) {
    if (cpu.step() != StepResult::Executed)
        throw std::runtime_error(std::string(label) + ": " +
                                 (cpu.fault ? cpu.fault->describe() : "did not execute"));
}

std::uint64_t tsc_registers(const Cpu& cpu) {
    return (std::uint64_t(std::uint32_t(cpu.regs[Cpu::EDX])) << 32) |
           std::uint32_t(cpu.regs[Cpu::EAX]);
}

void boot_protected32(Cpu& cpu) {
    cpu.reset(0, 0x1000);
    cpu.a20_enabled = true;
    cpu.gdtr = {0x2f, 0x3000};
    cpu.write64(0x3008, 0x00cffa000000ffffULL); // Ring 3, flat code.
    cpu.write64(0x3010, 0x00cff2000000ffffULL); // Ring 3, flat data and stack.
    cpu.write64(0x3020, 0x00cf9a000000ffffULL); // 0020h, ring 0, flat code.
    cpu.write64(0x3028, 0x00cf92000000ffffULL); // Ring 0, flat data and stack.
    emit(cpu, 0x1000, {
        0xb8,0x11,0x00,0x0f,0x22,0xc0,
        0x66,0xea,0x00,0x20,0x00,0x00,0x20,0x00 // Enter protected32 at 0020:2000.
    });
    emit(cpu, 0x2000, {
        0x66,0xb8,0x28,0x00,0x8e,0xd0, // Load SS=0028h.
        0xbc,0x00,0x00,0x06,0x00       // MOV ESP,00060000h
    });
    for (unsigned i = 0; i < 6; ++i) step(cpu, "protected32 setup");
    need(std::string(cpu.mode_name()) == "protected32" && cpu.cpl() == 0 &&
         cpu.segments[Cpu::CS] == 0x20, "fixture did not enter protected32 ring 0");
}

void timestamp_counter() {
    Cpu cpu(8 * 1024 * 1024);
    boot_protected32(cpu);
    constexpr std::uint32_t reported_ip = 0x00446056;
    cpu.eip = reported_ip;
    cpu.regs[Cpu::ECX] = 0x10;
    cpu.regs[Cpu::EAX] = 0xdeadbeef;
    cpu.regs[Cpu::EDX] = 0xcafef00d;
    cpu.eflags = 2 | Cpu::CF;
    emit(cpu, reported_ip, {0x0f,0x32,0x0f,0x31,0x0f,0x30,0x0f,0x32,0x0f,0x31});
    const auto first_ticks = cpu.instructions * 64;
    step(cpu, "reported protected32 RDMSR IA32_TSC at 0020:00446056");
    need(cpu.eip == reported_ip + 2 && tsc_registers(cpu) == first_ticks,
         "RDMSR IA32_TSC must return the pre-instruction counter in EDX:EAX");
    step(cpu, "RDTSC immediately after RDMSR IA32_TSC");
    need(tsc_registers(cpu) == first_ticks + 64,
         "RDMSR and RDTSC must see the same clock advancing by one step");
    need(cpu.eflags == (2 | Cpu::CF) && cpu.regs[Cpu::ECX] == 0x10,
         "both timestamp instructions must preserve FLAGS and ECX");

    // Start just below the 32-bit carry so the read must split both halves.
    constexpr std::uint64_t rebased = 0x12345678ffffffe0ULL;
    cpu.regs[Cpu::EAX] = std::uint32_t(rebased);
    cpu.regs[Cpu::EDX] = std::uint32_t(rebased >> 32);
    step(cpu, "WRMSR IA32_TSC");
    step(cpu, "RDMSR rebased IA32_TSC");
    const auto after_write = tsc_registers(cpu);
    need(after_write == rebased + 64, "WRMSR must rebase the complete 64-bit TSC");
    need(cpu.regs[Cpu::EDX] == 0x12345679 && cpu.regs[Cpu::EAX] == 0x20,
         "RDMSR IA32_TSC must split the correct low and high halves after carry");
    step(cpu, "RDTSC after rebasing IA32_TSC");
    need(tsc_registers(cpu) == after_write + 64, "RDTSC must observe the MSR rebase");
    need(cpu.eflags == (2 | Cpu::CF), "WRMSR and reads must preserve FLAGS");

    cpu.reset(0, 0x1000);
    cpu.a20_enabled = true;
    emit(cpu, 0x1000, {0x0f,0x31});
    step(cpu, "RDTSC after CPU reset");
    need(tsc_registers(cpu) == 0, "CPU reset must discard the previous TSC rebase");
}

void timestamp_privilege_and_unknown() {
    Cpu cpu(2 * 1024 * 1024);
    boot_protected32(cpu);
    emit(cpu, 0x2100, {0x0f,0x32,0x0f,0x30});
    cpu.eip = 0x2100;
    cpu.regs[Cpu::ECX] = 0xdeadbeefU;
    cpu.regs[Cpu::EAX] = 0x12345678;
    cpu.regs[Cpu::EDX] = 0x87654321;
    const auto initial = cpu.regs;
    const auto before = cpu.instructions;
    for (unsigned i = 0; i < 2; ++i) {
        const auto ip = cpu.eip;
        need(cpu.step() == StepResult::Faulted && cpu.fault &&
             cpu.fault->code == FaultCode::UnsupportedState &&
             cpu.fault->reason.find("deadbeef") != std::string::npos && cpu.eip == ip,
             "unknown MSR must still fault at the instruction start");
        need(cpu.regs == initial && cpu.instructions == before,
             "unknown MSR must preserve registers and instruction count");
        cpu.fault.reset();
        cpu.eip += 2;
    }

    emit(cpu, 0x2200, {
        0x6a,0x13,                   // Ring 3 SS.
        0x68,0x00,0x00,0x05,0x00,   // Ring 3 ESP.
        0x9c,                        // EFLAGS.
        0x6a,0x0b,                   // Ring 3 CS.
        0x68,0x00,0x40,0x00,0x00,   // Ring 3 EIP.
        0xcf                         // IRETD.
    });
    emit(cpu, 0x4000, {0x0f,0x32,0x0f,0x30,0x0f,0x31});
    cpu.eip = 0x2200;
    for (unsigned i = 0; i < 6; ++i) step(cpu, "protected32 IRETD to ring 3");
    need(cpu.cpl() == 3 && cpu.eip == 0x4000, "fixture did not reach ring 3");
    cpu.idtr = {0, 0}; // Observe faults without an installed exception handler.
    cpu.regs[Cpu::ECX] = 0x10;
    const auto before_user = cpu.instructions;
    const auto user_regs = cpu.regs;
    for (const char* expected : {"RDMSR requires CPL0", "WRMSR requires CPL0"}) {
        const auto ip = cpu.eip;
        need(cpu.step() == StepResult::Faulted && cpu.fault &&
             cpu.fault->code == FaultCode::ProtectionFault &&
             cpu.fault->reason.find(expected) != std::string::npos,
             "ring 3 must not access IA32_TSC using RDMSR or WRMSR");
        need(cpu.eip == ip && cpu.regs == user_regs && cpu.instructions == before_user,
             "failed privilege check must leave instruction position and registers intact");
        cpu.fault.reset();
        cpu.eip += 2;
    }
    step(cpu, "ring 3 RDTSC with CR4.TSD clear");
    need(tsc_registers(cpu) == before_user * 64,
         "unprivileged RDTSC with CR4.TSD clear must read the clock without changing it");

    boot_protected32(cpu);
    emit(cpu, 0x2100, {
        0xb8,0x04,0x00,0x00,0x00,0x0f,0x22,0xe0, // MOV CR4,EAX sets TSD.
        0x0f,0x31                                // Ring 0 RDTSC is still allowed.
    });
    cpu.eip = 0x2100;
    for (unsigned i = 0; i < 3; ++i) step(cpu, "set and exercise CR4.TSD at CPL0");
    need((cpu.cr4 & 4) != 0, "guest MOV CR4 must accept TSD");
    cpu.eip = 0x2200;
    for (unsigned i = 0; i < 6; ++i) step(cpu, "IRETD to ring 3 with TSD set");
    cpu.eip = 0x4004;
    const auto before_tsd = cpu.instructions;
    const auto tsd_regs = cpu.regs;
    need(cpu.cpl() == 3 && cpu.step() == StepResult::Faulted && cpu.fault &&
         cpu.fault->code == FaultCode::ProtectionFault &&
         cpu.fault->reason.find("RDTSC") != std::string::npos,
         "CR4.TSD must forbid ring-3 RDTSC");
    need(cpu.eip == 0x4004 && cpu.regs == tsd_regs && cpu.instructions == before_tsd,
         "blocked RDTSC must preserve the instruction position and registers");
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
        need((cpu.regs[Cpu::EDX]&(1U<<8))!=0,"CMPXCHG8B must be advertised after test");
        need((cpu.regs[Cpu::ECX]&(1U<<13))!=0,"CMPXCHG16B required for Windows x64");
        constexpr std::uint32_t simd=(1U<<23)|(1U<<24)|(1U<<25)|(1U<<26);
        need((cpu.regs[Cpu::EDX]&simd)==simd,"verified MMX/FXSR/SSE/SSE2 capability bits");
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
        need((cpu.regs[Cpu::ECX]&(1U<<0))!=0,"LAHF/SAHF long64 bit expected");
        need((cpu.regs[Cpu::ECX]&(1U<<8))!=0,"PREFETCHW declared after x86 opcode implementation");
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
        timestamp_counter();
        timestamp_privilege_and_unknown();
        std::cout<<"REVERSE_WINDOWS_IA32_TSC_PASS rdmsr=0f32 wrmsr=0f30 rdtsc=0f31 "
                     "protected32=1 cpl_guard=1 tsd=1 reset=1 unknown_msr=1\n";
        std::cout<<"WIN10_CPU_PROFILE_PASS checks="<<checks
                 <<" MMX=1 FXSR=1 SSE=1 SSE2=1 x87=0 TSC_synthetic=1 windows_ready=0\n";
        return 0;
    }catch(const std::exception& ex){
        std::cerr<<"WIN10_CPU_PROFILE_FAIL "<<ex.what()<<"\n";
        return 1;
    }
}
