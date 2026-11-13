#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;
using Register = std::array<std::uint32_t,4>;

enum class Conversion {
    ps_to_pd, pd_to_ps, ss_to_sd, sd_to_ss, dq_to_ps, ps_to_dq, ps_to_dq_truncated
};

unsigned checks=0;
constexpr Register initial={0x11223344,0x55667788,0x99aabbcc,0xddeeff00};
constexpr std::uint32_t default_csr=0x1f80;

void check(bool condition,const std::string& message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}

void set_register(Cpu& cpu,unsigned reg,const Register& input) {
    for(unsigned lane=0;lane<4;++lane)
        for(unsigned byte=0;byte<4;++byte)
            cpu.xmm[reg][lane*4+byte]=std::uint8_t(input[lane]>>(byte*8));
}

Register read_register(const Cpu& cpu,unsigned reg) {
    Register out{};
    for(unsigned lane=0;lane<4;++lane)
        for(unsigned byte=0;byte<4;++byte)
            out[lane]|=std::uint32_t(cpu.xmm[reg][lane*4+byte])<<(byte*8);
    return out;
}

StepResult execute(Cpu& cpu,std::initializer_list<std::uint8_t> code) {
    cpu.memory.write_block(0x1000,code.begin(),code.size());
    cpu.eip=0x1000;
    cpu.fault.reset();
    cpu.halted=false;
    return cpu.step();
}

StepResult convert(Cpu& cpu,Conversion kind) {
    switch(kind) {
        case Conversion::ps_to_pd: return execute(cpu,{0x0f,0x5a,0xc1});
        case Conversion::pd_to_ps: return execute(cpu,{0x66,0x0f,0x5a,0xc1});
        case Conversion::ss_to_sd: return execute(cpu,{0xf3,0x0f,0x5a,0xc1});
        case Conversion::sd_to_ss: return execute(cpu,{0xf2,0x0f,0x5a,0xc1});
        case Conversion::dq_to_ps: return execute(cpu,{0x0f,0x5b,0xc1});
        case Conversion::ps_to_dq: return execute(cpu,{0x66,0x0f,0x5b,0xc1});
        case Conversion::ps_to_dq_truncated: return execute(cpu,{0xf3,0x0f,0x5b,0xc1});
    }
    throw std::logic_error("unknown conversion");
}

void comparison(Cpu& cpu,const char* name,Conversion kind,const Register& source,
                std::uint32_t csr,const Register& expected,std::uint32_t flags) {
    set_register(cpu,0,initial);
    set_register(cpu,1,source);
    cpu.mxcsr=csr;
    check(convert(cpu,kind)==StepResult::Executed,name);
    check(read_register(cpu,0)==expected,std::string(name)+" result");
    check(cpu.mxcsr==(csr|flags),std::string(name)+" status");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    alignas(16) Register actual{};
    const auto saved=_mm_getcsr();
    _mm_setcsr(csr);
#define NATIVE_CONVERSION(op) asm volatile("movdqu %1,%%xmm0; movdqu %2,%%xmm1; " \
    op " %%xmm1,%%xmm0; movdqu %%xmm0,%0" \
    : "=m"(actual) : "m"(initial),"m"(source) : "xmm0","xmm1","memory")
    switch(kind) {
        case Conversion::ps_to_pd: NATIVE_CONVERSION("cvtps2pd"); break;
        case Conversion::pd_to_ps: NATIVE_CONVERSION("cvtpd2ps"); break;
        case Conversion::ss_to_sd: NATIVE_CONVERSION("cvtss2sd"); break;
        case Conversion::sd_to_ss: NATIVE_CONVERSION("cvtsd2ss"); break;
        case Conversion::dq_to_ps: NATIVE_CONVERSION("cvtdq2ps"); break;
        case Conversion::ps_to_dq: NATIVE_CONVERSION("cvtps2dq"); break;
        case Conversion::ps_to_dq_truncated: NATIVE_CONVERSION("cvttps2dq"); break;
    }
#undef NATIVE_CONVERSION
    const auto actual_csr=_mm_getcsr();
    _mm_setcsr(saved);
    check(actual==expected,std::string(name)+" native x86 result");
    check(actual_csr==(csr|flags),std::string(name)+" native x86 status");
#endif
}

void conversion_results(Cpu& cpu) {
    constexpr auto daz=0x40U;
    const Register single_denorm={1,0x80000001U,0,0};
    comparison(cpu,"CVTPS2PD subnormal",Conversion::ps_to_pd,single_denorm,default_csr,
                {0,0x36a00000U,0,0xb6a00000U},2);
    comparison(cpu,"CVTPS2PD DAZ signed zeros",Conversion::ps_to_pd,single_denorm,default_csr|daz,
                {0,0,0,0x80000000U},0);
    comparison(cpu,"CVTSS2SD NaN payload",Conversion::ss_to_sd,{0xff800001U,0,0,0},default_csr,
                {0x20000000U,0xfff80000U,initial[2],initial[3]},1);
    constexpr Register double_subnormal_result={0,0x36a00000U,0,0xb6a00000U};
    comparison(cpu,"CVTPD2PS exact tiny",Conversion::pd_to_ps,double_subnormal_result,default_csr,
                {1,0x80000001U,0,0},0);
    comparison(cpu,"CVTPD2PS FTZ signed zeros",Conversion::pd_to_ps,double_subnormal_result,default_csr|0x8000U,
                {0,0x80000000U,0,0},0x30);
    comparison(cpu,"CVTSD2SS FTZ before minimum normal",Conversion::sd_to_ss,{0xe0000000U,0x380fffffU,0,0},
                default_csr|0x8000U,{0,initial[1],initial[2],initial[3]},0x30);
    comparison(cpu,"CVTSD2SS FTZ at minimum normal transition",Conversion::sd_to_ss,{0xf0000000U,0x380fffffU,0,0},
                default_csr|0x8000U,{0x00800000U,initial[1],initial[2],initial[3]},0x20);
    comparison(cpu,"CVTSD2SS rounding",Conversion::sd_to_ss,{0x10000000U,0x3ff00000U,0,0},
                default_csr|0x4000U,{0x3f800001U,initial[1],initial[2],initial[3]},0x20);
    comparison(cpu,"CVTSD2SS NaN payload",Conversion::sd_to_ss,{0x20000000U,0xfff00000U,0,0},
                default_csr,{0xffc00001U,initial[1],initial[2],initial[3]},1);
    comparison(cpu,"CVTDQ2PS integer rounding",Conversion::dq_to_ps,{0x01000001U,0xfeffffffU,1,0},
                default_csr|0x4000U,{0x4b800001U,0xcb800000U,0x3f800000U,0},0x20);
    comparison(cpu,"CVTPS2DQ mixed IE and PE",Conversion::ps_to_dq,{0x40200000U,0x7fc00001U,0xc0200000U,1},
                default_csr,{2,0x80000000U,0xfffffffeU,0},0x21);
    comparison(cpu,"CVTTPS2DQ ignores RC",Conversion::ps_to_dq_truncated,{0x3ff00000U,0xbff00000U,1,0x80000001U},
                default_csr|daz|0x4000U,{1,0xffffffffU,0,0},0x20);
    set_register(cpu,0,initial);
    set_register(cpu,1,{0x7fc00001U,0x40200000U,0,0});
    cpu.mxcsr=default_csr&~(1U<<7);
    check(convert(cpu,Conversion::ps_to_dq)==StepResult::Faulted&&cpu.fault&&
          cpu.fault->code==FaultCode::SimdFloatingPoint,"unmasked CVTPS2DQ invalid must deliver #XM");
    check(read_register(cpu,0)==initial&&(cpu.mxcsr&0x3fU)==1,
          "unmasked invalid conversion must keep XMM and suppress PE");
    for(const auto fraction:{0U,1U}) {
        set_register(cpu,0,initial);
        set_register(cpu,1,{fraction,0x36a00000U,0,0});
        cpu.mxcsr=default_csr&~(1U<<11);
        check(convert(cpu,Conversion::sd_to_ss)==StepResult::Faulted&&cpu.fault&&
              cpu.fault->code==FaultCode::SimdFloatingPoint,"unmasked narrowing underflow must deliver #XM");
        check(read_register(cpu,0)==initial&&(cpu.mxcsr&0x3fU)==(fraction?0x30U:0x10U),
              "unmasked narrowing precision only reflects rounding with an unbounded exponent");
    }
    for(const auto fraction:{0U,1U}) {
        set_register(cpu,0,initial);
        set_register(cpu,1,{fraction,0x47f00000U,0,0}); // 2^128 and its next binary64 value.
        cpu.mxcsr=default_csr&~(1U<<10);
        check(convert(cpu,Conversion::sd_to_ss)==StepResult::Faulted&&cpu.fault&&
              cpu.fault->code==FaultCode::SimdFloatingPoint,"unmasked narrowing overflow must deliver #XM");
        check(read_register(cpu,0)==initial&&(cpu.mxcsr&0x3fU)==(fraction?0x28U:0x08U),
              "unmasked narrowing overflow must leave PE clear when unbounded rounding is exact");
    }
}

void state_faults(Cpu& cpu) {
    cpu.cr0|=4;
    check(execute(cpu,{0xf2,0x0f,0x58,0xc1})==StepResult::Faulted&&cpu.fault&&
          cpu.fault->code==FaultCode::InvalidInstruction,"SSE2 with CR0.EM must deliver #UD");
    check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Faulted&&cpu.fault&&
          cpu.fault->code==FaultCode::DeviceNotAvailable,"FXSAVE with CR0.EM must deliver #NM");
    cpu.cr0&=~4U;
    cpu.cr4=0;
    check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,"FLD1 before no-OSFXSR save");
    cpu.write32(0x4018,0xfeedfaceU);
    cpu.write32(0x40a0,0x11223344U);
    check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Executed,
          "FXSAVE with OSFXSR disabled must save the x87 area");
    check(cpu.read16(0x4002)==0x3800&&cpu.read8(0x4004)==0x80&&
          cpu.read32(0x4018)==0xfeedfaceU&&cpu.read32(0x40a0)==0x11223344U,
          "FXSAVE with OSFXSR disabled must leave XMM and MXCSR images untouched");
    check(execute(cpu,{0xdb,0xe3})==StepResult::Executed,"FNINIT before no-OSFXSR restore");
    cpu.mxcsr=default_csr;
    set_register(cpu,0,initial);
    check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Executed,
          "FXRSTOR with OSFXSR disabled must ignore reserved MXCSR bits");
    check(cpu.fpu_status==0x3800&&cpu.mxcsr==default_csr&&read_register(cpu,0)==initial,
          "FXRSTOR with OSFXSR disabled restores only x87 state");
}
} // namespace

int main() {
    try {
        Cpu cpu(2*1024*1024);
        cpu.reset(0,0x1000);
        cpu.a20_enabled=true;
        cpu.cr4=0x600;
        conversion_results(cpu);
        state_faults(cpu);
        std::cout<<"REVERSE_WINDOWS_C09R1_STAGE5_CVT_FXSR_PASS checks="<<checks<<"\n";
        return 0;
    } catch(const std::exception& error) {
        std::cerr<<"REVERSE_WINDOWS_C09R1_STAGE5_CVT_FXSR_FAIL "<<error.what()<<"\n";
        return 1;
    }
}
