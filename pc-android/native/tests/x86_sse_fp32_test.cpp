#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(__SSE__) && (defined(__GNUC__) || defined(__clang__))
#define HAS_X86_SSE_ORACLE 1
#else
#define HAS_X86_SSE_ORACLE 0
#endif

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
using Vector = std::array<std::uint32_t,4>;
unsigned checks = 0, native_checks = 0;
constexpr unsigned address = 0x1000;
constexpr unsigned operand = 0x3001;

void check(bool pass, const char* message) {
    ++checks;
    if (!pass) throw std::runtime_error(message);
}

void load(Cpu& cpu, unsigned reg, const Vector& value) {
    for (unsigned i=0;i<4;++i)
        for (unsigned j=0;j<4;++j) cpu.xmm[reg][i*4+j]=std::uint8_t(value[i]>>(j*8));
}

Vector read(const Cpu& cpu, unsigned reg) {
    Vector value{};
    for (unsigned i=0;i<4;++i)
        for (unsigned j=0;j<4;++j) value[i]|=std::uint32_t(cpu.xmm[reg][i*4+j])<<(j*8);
    return value;
}

StepResult run(Cpu& cpu, std::initializer_list<std::uint8_t> code) {
    cpu.eip=address;
    cpu.fault.reset();
    cpu.memory.write_block(address,code.begin(),code.size());
    return cpu.step();
}

void execute(Cpu& cpu, std::initializer_list<std::uint8_t> code) {
    if (run(cpu,code)!=StepResult::Executed)
        throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "SSE fixture did not execute");
}

void arithmetic(Cpu& cpu) {
    const Vector lhs{0x3fc00000,0xc0000000,0x40400000,0x40800000};
    const Vector rhs{0x40000000,0x3f800000,0xc0400000,0x3f800000};
    const Vector expected{0x40600000,0xbf800000,0,0x40a00000};
    load(cpu,0,lhs);load(cpu,1,rhs);
    execute(cpu,{0x0f,0x58,0xc1});     // ADDPS
    check(read(cpu,0)==expected,"ADDPS did not add the four independent lanes");
#if HAS_X86_SSE_ORACLE
    Vector native{};
    asm volatile("movups %[left], %%xmm0; movups %[right], %%xmm1; addps %%xmm1, %%xmm0; movups %%xmm0, %[out]"
        : [out] "=m"(native) : [left] "m"(lhs), [right] "m"(rhs) : "xmm0", "xmm1");
    check(read(cpu,0)==native,"ADDPS disagrees with physical x86");
    ++native_checks;
#endif
    load(cpu,0,lhs);load(cpu,1,rhs);
    execute(cpu,{0xf3,0x0f,0x58,0xc1}); // ADDSS
    check(read(cpu,0)==Vector{expected[0],lhs[1],lhs[2],lhs[3]},"ADDSS changed upper lanes");
    execute(cpu,{0xf3,0x0f,0x5c,0xc1}); // SUBSS
    check(read(cpu,0)==lhs,"SUBSS did not reverse ADDSS for exact operands");
    execute(cpu,{0xf3,0x0f,0x59,0xc1}); // MULSS
    check(read(cpu,0)[0]==0x40400000,"MULSS result mismatch");
    execute(cpu,{0xf3,0x0f,0x5e,0xc1}); // DIVSS
    check(read(cpu,0)==lhs,"DIVSS result mismatch");
    load(cpu,1,{0x40800000,0,0,0});
    execute(cpu,{0xf3,0x0f,0x51,0xc1}); // SQRTSS
    check(read(cpu,0)==Vector{0x40000000,lhs[1],lhs[2],lhs[3]},"SQRTSS result mismatch");
    for (auto op : {0x5d,0x5f}) {
        load(cpu,0,{0x80000000,0,0,0});load(cpu,1,{});
        execute(cpu,{0xf3,0x0f,std::uint8_t(op),0xc1});
        check(read(cpu,0)[0]==0,"MINSS/MAXSS did not select the second signed zero");
    }
    load(cpu,0,{0x3f800000,0,0,0});load(cpu,1,{0x33800000,0,0,0});
    cpu.mxcsr=0x5f80; // round up
    execute(cpu,{0xf3,0x0f,0x58,0xc1});
    check(read(cpu,0)[0]==0x3f800001&&(cpu.mxcsr&0x3f)==32,"ADDSS rounding/precision mismatch");
    const auto saved=read(cpu,0);
    load(cpu,1,{0,0,0,0});cpu.mxcsr=0x1d80; // unmask divide by zero
    check(run(cpu,{0xf3,0x0f,0x5e,0xc1})==StepResult::Faulted&&cpu.fault&&
             cpu.fault->code==cosmic_pc::FaultCode::SimdFloatingPoint,"DIVSS did not report #XM");
    check(read(cpu,0)==saved&&(cpu.mxcsr&4),"DIVSS exception partially committed");
    cpu.mxcsr=0x1f80;
    load(cpu,0,{0x7f812345,0,0,0});load(cpu,1,{0x3f800000,0,0,0});
    execute(cpu,{0xf3,0x0f,0x58,0xc1});
    check(read(cpu,0)[0]==0x7fc12345&&(cpu.mxcsr&1),"ADDSS did not quiet the signaling NaN");
}

void moves_and_comparisons(Cpu& cpu) {
    cpu.mxcsr=0x1f80;
    const Vector a{0x3f800000,0x40000000,0x40400000,0x40800000};
    const Vector b{0xc0000000,0x40800000,0xc0a00000,0x40c00000};
    load(cpu,0,a);load(cpu,1,b);
    execute(cpu,{0xf3,0x0f,0x10,0xc1});
    check(read(cpu,0)==Vector{b[0],a[1],a[2],a[3]},"MOVSS register form changed upper lanes");
    for (unsigned i=0;i<4;++i) cpu.write32(operand+i*4,b[i]);
    execute(cpu,{0xf3,0x0f,0x10,0x06,0x01,0x30});
    check(read(cpu,0)==Vector{b[0],0,0,0},"MOVSS memory form left upper lanes dirty");
    execute(cpu,{0x0f,0x10,0x06,0x01,0x30});
    check(read(cpu,0)==b,"MOVUPS rejected or altered unaligned memory");
    check(run(cpu,{0x0f,0x28,0x06,0x01,0x30})==StepResult::Faulted&&cpu.fault&&
             cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,"MOVAPS accepted unaligned memory");
    execute(cpu,{0x0f,0x50,0xc1});
    check(cpu.regs[Cpu::EAX]==5,"MOVMSKPS lane ordering mismatch");
    load(cpu,0,a);
    execute(cpu,{0x0f,0x14,0xc1});
    check(read(cpu,0)==Vector{a[0],b[0],a[1],b[1]},"UNPCKLPS lane ordering mismatch");
    load(cpu,0,a);
    execute(cpu,{0x66,0x0f,0x15,0xc1});
    check(read(cpu,0)==Vector{a[2],a[3],b[2],b[3]},"UNPCKHPD lane ordering mismatch");
    load(cpu,0,a);
    execute(cpu,{0x0f,0xc6,0xc1,0x1b});
    check(read(cpu,0)==Vector{a[3],a[2],b[1],b[0]},"SHUFPS lane ordering mismatch");
    load(cpu,0,a);
    execute(cpu,{0xf3,0x0f,0xc2,0xc1,1}); // CMPLTSS
    check(read(cpu,0)==Vector{0,a[1],a[2],a[3]},"CMPSS modified upper lanes");
    load(cpu,0,{0x7fc12345,0,0,0});
    execute(cpu,{0x0f,0x2e,0xc1});
    check((cpu.eflags&(Cpu::CF|Cpu::PF|Cpu::ZF))==(Cpu::CF|Cpu::PF|Cpu::ZF)&&!(cpu.mxcsr&1),
          "UCOMISS quiet NaN flags mismatch");
    execute(cpu,{0x0f,0x2f,0xc1});
    check((cpu.mxcsr&1)!=0,"COMISS quiet NaN did not record invalid operation");
    cpu.mxcsr=0x1f80;cpu.regs[Cpu::EAX]=0xfffffff9U;
    execute(cpu,{0xf3,0x0f,0x2a,0xc0});
    check(read(cpu,0)[0]==0xc0e00000,"CVTSI2SS signed integer mismatch");
    execute(cpu,{0xf3,0x0f,0x2c,0xc0});
    check(std::uint32_t(cpu.regs[Cpu::EAX])==0xfffffff9U,"CVTTSS2SI signed integer mismatch");
    execute(cpu,{0x0f,0xae,0xe8});execute(cpu,{0x0f,0xae,0xf0});execute(cpu,{0x0f,0xae,0xf8});
    execute(cpu,{0x0f,0xae,0x3e,0x00,0x30}); // CLFLUSH, no device read
}
} // namespace

int main() {
    try {
        Cpu cpu(2*1024*1024);
        cpu.reset(0,address);cpu.cr4=0x600;
        arithmetic(cpu);
        moves_and_comparisons(cpu);
        std::cout << "REVERSE_WINDOWS_SSE_FP32_PASS checks=" << checks
                  << " native_x86=" << native_checks << '\n';
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "REVERSE_WINDOWS_SSE_FP32_FAIL " << error.what() << '\n';
        return 1;
    }
}
