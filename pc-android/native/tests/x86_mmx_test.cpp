#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"

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
constexpr std::uint64_t left=0x80007ffffef47f01ULL,right=0x807f8000fffb7f02ULL,count=3;
constexpr std::uint32_t flags=2U|Cpu::CF|Cpu::AF|Cpu::PF|Cpu::OF|Cpu::IF;
constexpr unsigned address=0x1000, buffer=0x3000, image=0x4000;
unsigned checks=0, hardware_checks=0;

void check(bool condition,const std::string& message) {
    ++checks;
    if(!condition)throw std::runtime_error(message);
}

StepResult run(Cpu& cpu,std::initializer_list<std::uint8_t> instruction,unsigned ip=address) {
    cpu.eip=ip;
    cpu.fault.reset();
    cpu.memory.write_block(ip,instruction.begin(),instruction.size());
    return cpu.step();
}

void step(Cpu& cpu,std::initializer_list<std::uint8_t> instruction,unsigned ip=address) {
    if(run(cpu,instruction,ip)!=StepResult::Executed)
        throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "MMX instruction did not execute");
}

std::uint64_t mm(const Cpu& cpu,unsigned slot) {
    std::uint64_t result=0;
    for(unsigned i=0;i<8;++i)result|=std::uint64_t(cpu.x87[slot][i])<<(8U*i);
    return result;
}

std::uint64_t xmm(const Cpu& cpu,unsigned reg,unsigned offset=0) {
    std::uint64_t result=0;
    for(unsigned i=0;i<8;++i)result|=std::uint64_t(cpu.xmm[reg][offset+i])<<(8U*i);
    return result;
}

void load_mmx(Cpu& cpu,std::uint64_t a,std::uint64_t b) {
    cpu.write64(buffer,a);
    cpu.write64(buffer+8,b);
    step(cpu,{0x0f,0x6f,0x06,0x00,0x30});
    step(cpu,{0x0f,0x6f,0x0e,0x08,0x30});
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
using Oracle=std::uint64_t(*)(std::uint64_t,std::uint64_t);
#define MMX_CASE(name,opcode,input,expected) {#name,opcode,input,expected,+[](std::uint64_t a,std::uint64_t b) -> std::uint64_t { \
    std::uint64_t output; \
    asm volatile("movq %1,%%mm0; movq %2,%%mm1; " #name " %%mm1,%%mm0; movq %%mm0,%0; emms" \
        : "=m"(output) : "m"(a),"m"(b) : "mm0","mm1","memory"); \
    return output; }}
#else
#define MMX_CASE(name,opcode,input,expected) {#name,opcode,input,expected}
#endif
struct PackedCase {
    const char* name;
    std::uint8_t opcode;
    std::uint64_t source,expected;
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    Oracle oracle;
#endif
};

// These operand/result pairs are checked with physical MMX on x86 and reused
// verbatim on ARM64. The destination uses both signed and unsigned extremes.
const PackedCase packed_cases[]{
    MMX_CASE(punpcklbw,0x60,right,0xfffefbf47f7f0201ULL),
    MMX_CASE(punpcklwd,0x61,right,0xfffbfef47f027f01ULL),
    MMX_CASE(punpckldq,0x62,right,0xfffb7f02fef47f01ULL),
    MMX_CASE(packsswb,0x63,right,0x8080fb7f807f807fULL),
    MMX_CASE(pcmpgtb,0x64,right,0x0000ff0000000000ULL),
    MMX_CASE(pcmpgtw,0x65,right,0x0000ffff00000000ULL),
    MMX_CASE(pcmpgtd,0x66,right,0x0000000000000000ULL),
    MMX_CASE(packuswb,0x67,right,0x000000ff00ff00ffULL),
    MMX_CASE(punpckhbw,0x68,right,0x80807f00807f00ffULL),
    MMX_CASE(punpckhwd,0x69,right,0x807f800080007fffULL),
    MMX_CASE(punpckhdq,0x6a,right,0x807f800080007fffULL),
    MMX_CASE(packssdw,0x6b,right,0x8000800080008000ULL),
    MMX_CASE(pcmpeqb,0x74,right,0xff0000000000ff00ULL),
    MMX_CASE(pcmpeqw,0x75,right,0x0000000000000000ULL),
    MMX_CASE(pcmpeqd,0x76,right,0x0000000000000000ULL),
    MMX_CASE(psrlw,0xd1,count,0x10000fff1fde0fe0ULL),
    MMX_CASE(psrld,0xd2,count,0x10000fff1fde8fe0ULL),
    MMX_CASE(psrlq,0xd3,count,0x10000fffffde8fe0ULL),
    MMX_CASE(paddq,0xd4,right,0x00800000feeffe03ULL),
    MMX_CASE(pmullw,0xd5,right,0x80008000053c7d02ULL),
    MMX_CASE(psubusb,0xd8,right,0x000000ff00000000ULL),
    MMX_CASE(psubusw,0xd9,right,0x0000000000000000ULL),
    MMX_CASE(pminub,0xda,right,0x80007f00fef47f01ULL),
    MMX_CASE(pand,0xdb,right,0x80000000fef07f00ULL),
    MMX_CASE(paddusb,0xdc,right,0xff7ffffffffffe03ULL),
    MMX_CASE(paddusw,0xdd,right,0xfffffffffffffe03ULL),
    MMX_CASE(pmaxub,0xde,right,0x807f80fffffb7f02ULL),
    MMX_CASE(pandn,0xdf,right,0x007f8000010b0002ULL),
    MMX_CASE(pavgb,0xe0,right,0x80408080fff87f02ULL),
    MMX_CASE(psraw,0xe1,count,0xf0000fffffde0fe0ULL),
    MMX_CASE(psrad,0xe2,count,0xf0000fffffde8fe0ULL),
    MMX_CASE(pavgw,0xe3,right,0x80408000ff787f02ULL),
    MMX_CASE(pmulhuw,0xe4,right,0x403f3ffffeef3f02ULL),
    MMX_CASE(pmulhw,0xe5,right,0x3fc0c00000003f02ULL),
    MMX_CASE(psubsb,0xe8,right,0x00817ffffff900ffULL),
    MMX_CASE(psubsw,0xe9,right,0xff817ffffef9ffffULL),
    MMX_CASE(pminsw,0xea,right,0x80008000fef47f01ULL),
    MMX_CASE(por,0xeb,right,0x807fffffffff7f03ULL),
    MMX_CASE(paddsb,0xec,right,0x807ffffffdef7f03ULL),
    MMX_CASE(paddsw,0xed,right,0x8000fffffeef7fffULL),
    MMX_CASE(pmaxsw,0xee,right,0x807f7ffffffb7f02ULL),
    MMX_CASE(pxor,0xef,right,0x007fffff010f0003ULL),
    MMX_CASE(psllw,0xf1,count,0x0000fff8f7a0f808ULL),
    MMX_CASE(pslld,0xf2,count,0x0003fff8f7a3f808ULL),
    MMX_CASE(psllq,0xf3,count,0x0003fffff7a3f808ULL),
    MMX_CASE(pmuludq,0xf4,right,0xfef002b7cde57d02ULL),
    MMX_CASE(pmaddwd,0xf5,right,0xffc100003f02823eULL),
    MMX_CASE(psadbw,0xf6,right,0x0000000000000188ULL),
    MMX_CASE(psubb,0xf8,right,0x0081fffffff900ffULL),
    MMX_CASE(psubw,0xf9,right,0xff81fffffef9ffffULL),
    MMX_CASE(psubd,0xfa,right,0xff80fffffef8ffffULL),
    MMX_CASE(psubq,0xfb,right,0xff80fffefef8ffffULL),
    MMX_CASE(paddb,0xfc,right,0x007ffffffdeffe03ULL),
    MMX_CASE(paddw,0xfd,right,0x007ffffffeeffe03ULL),
    MMX_CASE(paddd,0xfe,right,0x007ffffffeeffe03ULL),
};
#undef MMX_CASE

void packed(Cpu& cpu) {
    cpu.cr4=0; // MMX-only integer operations have no XMM state dependency.
    for(const auto& test:packed_cases) {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        check(test.oracle(left,test.source)==test.expected,
              std::string(test.name)+" x86 hardware baseline changed");
        ++hardware_checks;
#endif
        load_mmx(cpu,left,test.source);
        cpu.eflags=flags;
        cpu.mxcsr=0x1fa0;
        step(cpu,{0x0f,test.opcode,0xc1});
        check(mm(cpu,0)==test.expected,std::string(test.name)+" register result");
        check(mm(cpu,1)==test.source,std::string(test.name)+" modified source");
        check(cpu.fpu_tags==0&&!(cpu.fpu_status&0x3800)&&cpu.x87[0][8]==0xff&&cpu.x87[0][9]==0xff,
              std::string(test.name)+" did not enter MMX state");
        check(cpu.eflags==flags&&cpu.mxcsr==0x1fa0,std::string(test.name)+" modified FLAGS or MXCSR");
        cpu.write64(buffer+1,test.source);
        cpu.write64(buffer+16,left);
        step(cpu,{0x0f,0x6f,0x06,0x10,0x30});
        step(cpu,{0x0f,test.opcode,0x06,0x01,0x30});
        check(mm(cpu,0)==test.expected,std::string(test.name)+" memory result");
    }
}

void state_and_moves(Cpu& cpu) {
    cpu.cr4=0x600;
    cpu.fpu_status=0x7d00;
    cpu.fpu_tags=0xff3f;
    cpu.x87[3][8]=0x12;cpu.x87[3][9]=0x34;
    cpu.regs[Cpu::EAX]=0xffffaaaaff001122ULL;
    step(cpu,{0x0f,0x6e,0xc0}); // MOVD MM0,EAX.
    check(mm(cpu,0)==0xff001122ULL,"MOVD did not zero the upper dword");
    check(cpu.fpu_status==(0x7d00U&~0x3800U)&&cpu.fpu_tags==0,"MMX TOP or tags incorrect");
    check(cpu.x87[3][8]==0x12&&cpu.x87[3][9]==0x34,"MMX changed unrelated register exponent");
    step(cpu,{0x0f,0x7e,0xc0});
    check(cpu.regs[Cpu::EAX]==0xff001122ULL,"MOVD MM0 store did not zero-extend EAX");
    load_mmx(cpu,left,right);
    step(cpu,{0x0f,0x7f,0xc2});
    check(mm(cpu,2)==left,"MOVQ register store result");
    step(cpu,{0x0f,0x7f,0x06,0x01,0x30});
    check(cpu.read64(buffer+1)==left,"unaligned MOVQ memory result");
    step(cpu,{0x0f,0xe7,0x0e,0x01,0x30});
    check(cpu.read64(buffer+1)==right,"unaligned MOVNTQ memory result");
    step(cpu,{0x0f,0x70,0xc1,0x1b});
    check(mm(cpu,0)==0x7f02fffb8000807fULL,"PSHUFW lane permutation");
    step(cpu,{0x0f,0x71,0xe1,0xff});
    check(mm(cpu,1)==0xffffffffffff0000ULL,"PSRAW immediate oversized arithmetic count");
    step(cpu,{0x0f,0x72,0xd0,0xff});
    check(mm(cpu,0)==0,"PSRLD immediate oversized logical count");
    step(cpu,{0x0f,0xc4,0x06,0x02,0x30,7});
    check(mm(cpu,0)==0xfb7f000000000000ULL,"PINSRW from m16 or immediate mask");
    step(cpu,{0x0f,0xc5,0xc8,7});
    check(cpu.regs[Cpu::ECX]==0xfb7f,"PEXTRW did not zero-extend the lane");
    step(cpu,{0x0f,0xd7,0xc8});
    check(cpu.regs[Cpu::ECX]==0x80,"PMOVMSKB did not extract eight sign bits");
    cpu.xmm[0].fill(0x7a);
    step(cpu,{0xf3,0x0f,0xd6,0xc0});
    check(xmm(cpu,0)==mm(cpu,0)&&xmm(cpu,0,8)==0,"MOVQ2DQ did not zero the upper qword");
    step(cpu,{0xf2,0x0f,0xd6,0xd0});
    check(mm(cpu,2)==mm(cpu,0),"MOVDQ2Q did not transfer the low qword");
    const auto data=cpu.x87;
    const auto status=cpu.fpu_status;
    step(cpu,{0x0f,0xae,0x06,0x00,0x40});
    check(cpu.read8(image+4)==0xff&&cpu.read64(image+32)==mm(cpu,0)&&cpu.read16(image+40)==0xffff,
          "FXSAVE did not save MMX through the x87 image");
    step(cpu,{0x0f,0x77});
    check(cpu.fpu_tags==0xffff&&cpu.fpu_status==status&&cpu.x87==data,"EMMS changed data or flags");
    step(cpu,{0x0f,0xae,0x0e,0x00,0x40});
    check(cpu.x87==data&&cpu.fpu_tags!=0xffff,"FXRSTOR did not recover MMX physical register data");
    step(cpu,{0x0f,0x77});
    step(cpu,{0xd9,0xe8});
    check(((cpu.fpu_status>>11)&7U)==7&&cpu.fpu_tags!=0xffff,"x87 load did not see cleared MMX stack");
}

void native_mmx_state() {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    alignas(16) std::array<std::uint8_t,512> saved{}, after_read{}, after_emms{};
    std::uint64_t copy=0;
    asm volatile("fxsave %[saved]; fninit; fld1; movq %%mm7,%[copy]; "
                 "fxsave %[after_read]; emms; fxsave %[after_emms]; fxrstor %[saved]"
        : [saved] "=m"(saved), [copy] "=m"(copy), [after_read] "=m"(after_read),
          [after_emms] "=m"(after_emms)
        : : "memory");
    check(copy==0x8000000000000000ULL,"hardware MM7 did not alias ST(0) at TOP=7");
    check((after_read[3]&0x38U)==0&&after_read[4]==0xffU,"hardware MMX transition did not reset TOP and occupy all slots");
    check(after_read[32+7*16+8]==0xffU&&after_read[32+7*16+9]==0x3fU,
          "hardware MMX source-only operation changed the exponent");
    check((after_emms[3]&0x38U)==0&&after_emms[4]==0,"hardware EMMS did not clear TOP and tags");
    ++hardware_checks;
#endif
    Cpu cpu;
    cpu.reset(0,address);
    step(cpu,{0xd9,0xe8}); // FLD1 at physical slot 7.
    cpu.write64(buffer,0);
    step(cpu,{0x0f,0x7f,0x3e,0x00,0x30}); // MOVQ [3000], MM7.
    check(cpu.read64(buffer)==0x8000000000000000ULL,"MM7 did not alias ST(0) at TOP=7");
    check(!(cpu.fpu_status&0x3800U)&&cpu.fpu_tags==0,"MMX source-only operation did not enter MMX state");
    check(cpu.x87[7][8]==0xffU&&cpu.x87[7][9]==0x3fU,"MMX source-only operation changed the exponent");
    step(cpu,{0x0f,0x77});
    check(!(cpu.fpu_status&0x3800U)&&cpu.fpu_tags==0xffffU,"EMMS did not clear TOP and tags");
}

void conversion(Cpu& cpu) {
    cpu.cr4=0x600;
    cpu.mxcsr=0x1f80;
    cpu.fpu_status=0x2000;
    cpu.fpu_tags=0xf3ff;
    cpu.xmm[0].fill(0x57);
    cpu.write64(buffer,0x00000002fffffffdULL);
    step(cpu,{0x0f,0x2a,0x06,0x00,0x30});
    check(xmm(cpu,0)==0x40000000c0400000ULL&&xmm(cpu,0,8)==0x5757575757575757ULL,
          "CVTPI2PS m64 result or high lanes");
    check(cpu.fpu_status==0x2000&&cpu.fpu_tags==0xf3ff,"CVTPI2PS m64 incorrectly entered MMX");
    step(cpu,{0x66,0x0f,0x2a,0x0e,0x00,0x30});
    check(xmm(cpu,1)==0xc008000000000000ULL&&xmm(cpu,1,8)==0x4000000000000000ULL,
          "CVTPI2PD m64 result");
    check(cpu.fpu_status==0x2000&&cpu.fpu_tags==0xf3ff,"CVTPI2PD m64 incorrectly entered MMX");
    load_mmx(cpu,0x00000002fffffffdULL,0);
    step(cpu,{0x66,0x0f,0x2a,0xc0});
    check(xmm(cpu,0)==0xc008000000000000ULL&&xmm(cpu,0,8)==0x4000000000000000ULL,
          "CVTPI2PD MM register result");
    step(cpu,{0x66,0x0f,0x2d,0xc8});
    check(mm(cpu,1)==0x00000002fffffffdULL,"CVTPD2PI XMM register result");
    cpu.write64(buffer,0xc03000003fe00000ULL); // 1.75,-2.75.
    step(cpu,{0x0f,0x2c,0x06,0x00,0x30});
    check(mm(cpu,0)==0xfffffffe00000001ULL&&(cpu.mxcsr&32U),"CVTTPS2PI m64 result or precision");
    step(cpu,{0x0f,0x2d,0x06,0x00,0x30});
    check(mm(cpu,0)==0xfffffffd00000002ULL,"CVTPS2PI nearest-even conversion");
    cpu.write64(buffer,0x7fc000004f000000ULL);
    cpu.mxcsr=0x1f80;
    step(cpu,{0x0f,0x2d,0x06,0x00,0x30});
    check(mm(cpu,0)==0x8000000080000000ULL&&(cpu.mxcsr&1U),"invalid CVTPS2PI masked response");
    step(cpu,{0x0f,0x77});
    const auto prior=cpu.x87;
    cpu.mxcsr=0x1f00;
    check(run(cpu,{0x0f,0x2d,0x06,0x00,0x30})==StepResult::Faulted&&cpu.fault&&
          cpu.fault->code==FaultCode::SimdFloatingPoint,"unmasked CVTPS2PI invalid #XM");
    check(cpu.x87==prior&&cpu.fpu_tags==0xffff&&(cpu.mxcsr&1U),"CVTPS2PI #XM partly committed MMX state");
}

void memory_and_faults(Cpu& cpu) {
    cpu.mxcsr=0x1f80;
    load_mmx(cpu,0x8899aabbccddeeffULL,0x8000800080008000ULL);
    cpu.regs[Cpu::EDI]=buffer+32;
    cpu.write64(buffer+32,0x4444444444444444ULL);
    step(cpu,{0x0f,0xf7,0xc1});
    check(cpu.read64(buffer+32)==0x8844aa44cc44ee44ULL,"MASKMOVQ did not preserve unselected bytes");
    step(cpu,{0x0f,0x77});
    const auto data=cpu.x87;
    cpu.cr0=0x14;
    check(run(cpu,{0x0f,0x77})==StepResult::Faulted&&cpu.fault&&cpu.fault->code==FaultCode::InvalidInstruction,
          "CR0.EM MMX #UD");
    cpu.cr0=0x18;
    check(run(cpu,{0x0f,0xfd,0xc1})==StepResult::Faulted&&cpu.fault&&cpu.fault->code==FaultCode::DeviceNotAvailable,
          "CR0.TS MMX #NM");
    cpu.cr0=0x30;
    cpu.fpu_status|=0x84;
    cpu.fpu_control&=~4;
    check(run(cpu,{0x0f,0xfd,0xc1})==StepResult::Faulted&&cpu.fault&&cpu.fault->code==FaultCode::X87FloatingPoint,
          "pending x87 MMX #MF");
    check(cpu.x87==data&&cpu.fpu_tags==0xffff,"MMX fault corrupted x87 register data or tags");
    cpu.fpu_status=0;cpu.fpu_control=0x037f;
    for (auto code : {std::initializer_list<std::uint8_t>{0x0f,0x73,0xd8,0x08},
                      {0x0f,0xd7,0x06,0x00,0x30},{0x0f,0xe7,0xc0},{0xf0,0x0f,0xfd,0xc1}}) {
        check(run(cpu,code)==StepResult::Faulted&&cpu.fault&&cpu.fault->code==FaultCode::InvalidInstruction,
              "invalid MMX form did not raise #UD");
    }
    // Low unpack reads exactly m32; the high variant must validate all m64.
    cpu.write32(0xfffc,0x01020304);
    step(cpu,{0x0f,0x60,0x06,0xfc,0xff});
    check(run(cpu,{0x0f,0x68,0x06,0xfc,0xff})==StepResult::Faulted&&cpu.fault&&
          cpu.fault->code==FaultCode::SegmentLimit,"high unpack accepted an invalid m64 segment span");
}

void long_mode() {
    Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
    cpu.reset();
    const auto disk=cosmic_pc::long_mode_boot_disk();
    cpu.memory.write_block(0x7c00,disk.data(),disk.size());
    cpu.interrupt_hook=[](Cpu& c,std::uint8_t vector) {
        if(vector!=0x15||c.reg16(Cpu::EAX)!=0x2401)return false;
        c.a20_enabled=true;c.set_flag(Cpu::CF,false);return true;
    };
    for(unsigned i=0;i<12000&&!cpu.is_long_mode();++i)
        if(cpu.step()==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    check(cpu.is_long_mode(),"long mode fixture did not enter IA-32e");
    cpu.cr4|=0x600;
    constexpr unsigned ip=0xa000;
    cpu.regs[Cpu::R8]=0xaf013469578de0caULL;
    step(cpu,{0x4d,0x0f,0x6e,0xc0},ip); // REX.R ignored by MMX; W,B used by GPR.
    check(mm(cpu,0)==cpu.regs[Cpu::R8],"REX.W MOVQ from extended GPR or MMX field alias");
    step(cpu,{0x4d,0x0f,0x7e,0xc1},ip);
    check(cpu.regs[Cpu::R9]==mm(cpu,0),"REX.W MOVQ to extended GPR");
    step(cpu,{0xf3,0x44,0x0f,0xd6,0xc0},ip);
    check(xmm(cpu,8)==mm(cpu,0)&&!xmm(cpu,8,8),"MOVQ2DQ extended XMM destination");
    step(cpu,{0xf2,0x41,0x0f,0xd6,0xc8},ip);
    check(mm(cpu,1)==xmm(cpu,8),"MOVDQ2Q extended XMM source");
}
} // namespace

int main() {
    try {
        Cpu cpu(2*1024*1024);
        cpu.reset(0,address);
        packed(cpu);
        state_and_moves(cpu);
        native_mmx_state();
        conversion(cpu);
        memory_and_faults(cpu);
        long_mode();
        std::cout << "REVERSE_WINDOWS_MMX_PASS checks=" << checks << " native_x86=" << hardware_checks << '\n';
        return 0;
    } catch(const std::exception& error) {
        std::cerr << "REVERSE_WINDOWS_MMX_FAIL " << error.what() << '\n';
        return 1;
    }
}
