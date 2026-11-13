#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;
using Ext = Cpu::Extended80;
constexpr bool native_x86 =
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    true;
#else
    false;
#endif
unsigned checks = 0;
unsigned comparisons = 0;

void check(bool result,const char* what) {
    ++checks;
    if (!result) throw std::runtime_error(what);
}

void native_check(bool result,const char* what) {
    ++comparisons;
    check(result,what);
}

struct Guest {
    static constexpr unsigned code=0x1000;
    Cpu cpu{2*1024*1024};
    Guest() { reset(); }
    void reset() { cpu.reset(0,code); cpu.cr0|=0x20; }
    StepResult step(std::initializer_list<std::uint8_t> bytes) {
        cpu.eip=code;
        cpu.fault.reset();
        cpu.memory.write_block(code,bytes.begin(),bytes.size());
        return cpu.step();
    }
    void run(std::initializer_list<std::uint8_t> bytes) {
        if (step(bytes)!=StepResult::Executed)
            throw std::runtime_error(cpu.fault?cpu.fault->describe():"Guest x87 opcode did not retire");
    }
    unsigned top() const { return (cpu.fpu_status>>11)&7U; }
    const Ext& st(unsigned i=0) const { return cpu.x87[(top()+i)&7U]; }
    void put(unsigned address,const Ext& data) {
        cpu.memory.write_block(address,data.data(),data.size());
    }
    Ext get(unsigned address) const {
        Ext data{};
        cpu.memory.read_block(address,data.data(),data.size());
        return data;
    }
};

Ext ext(std::uint64_t sig,std::uint16_t exp) {
    Ext bytes{};
    for (unsigned i=0;i<8;++i) bytes[i]=std::uint8_t(sig>>(8*i));
    bytes[8]=std::uint8_t(exp);
    bytes[9]=std::uint8_t(exp>>8);
    return bytes;
}

template<class T,class A> T at(const A& bytes,unsigned off) {
    T word{};
    std::memcpy(&word,bytes.data()+off,sizeof(word));
    return word;
}

struct NativeState {
    alignas(16) std::array<std::uint8_t,512> save{};
    NativeState() {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        asm volatile("fxsave64 %0":"=m"(save)::"memory");
#endif
    }
    ~NativeState() {
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        asm volatile("fxrstor64 %0"::"m"(save):"memory");
#endif
    }
};

void wait_and_pointers() {
    Guest g;
    g.cpu.fpu_control&=~4U;
    g.run({0xd9,0xee}); // FLDZ
    g.run({0xd9,0xe8}); // FLD1
    g.run({0xd8,0xf1}); // FDIV ST0, ST1
    const auto pending=g.cpu.fpu_status;
    const auto saved_registers=g.cpu.x87;
    check(pending==0xb084,"Unmasked FDIV did not defer #MF");
    check(g.cpu.fpu_opcode==0x0f1&&g.cpu.fpu_ip==Guest::code,"FDIV did not record FOP/FIP");
    for (auto next : {std::initializer_list<std::uint8_t>{0xd9,0xe8}, {0xd9,0xd0},
                      {0xd9,0x06,0,0x40}, {0xd9,0x2e,0,0x40}, {0x9b}}) {
        check(g.step(next)==StepResult::Faulted&&g.cpu.fault&&
                  g.cpu.fault->code==FaultCode::X87FloatingPoint,
              "Waiting x87 instruction executed with pending exception");
        check(g.cpu.x87==saved_registers&&g.cpu.fpu_status==pending&&g.cpu.fpu_opcode==0x0f1,
              "Deferred fault disturbed x87 state or faulting FOP");
    }
    g.run({0xdf,0xe0}); // FNSTSW AX
    g.run({0xd9,0x3e,0,0x20}); // FNSTCW
    check(g.cpu.reg16(Cpu::EAX)==pending&&g.cpu.read16(0x2000)==0x037b,
          "Non-waiting environment queries failed during exception");
    g.run({0xd9,0x36,0x10,0x20}); // FNSTENV m14
    check(g.cpu.read16(0x2012)==pending&&g.cpu.read16(0x2018)==0x0f1,
          "FNSTENV did not publish pending FSW/FOP");
    check((g.cpu.fpu_status&0x8080)==0&&g.cpu.fpu_control==0x037f,
          "FNSTENV failed to mask pending exception");
    g.run({0xd9,0x26,0x10,0x20}); // FLDENV m14
    check(g.cpu.fpu_status==pending&&g.cpu.fpu_control==0x037b,
          "FLDENV failed to restore the pending x87 environment");
    g.run({0xdb,0xe2}); // FNCLEX
    g.run({0xd9,0xe8});
    check(g.top()==5&&g.cpu.fpu_opcode==0x1e8,"FNCLEX did not permit subsequent FLD1");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    NativeState guard;
    alignas(16) std::array<std::uint8_t,512> image{};
    const std::uint16_t cw=0x037b;
    asm volatile("fninit; fldcw %1; fldz; fld1; .byte 0xd8,0xf1; fxsave64 %0"
        :"=m"(image):"m"(cw):"memory");
    native_check(at<std::uint16_t>(image,2)==pending,"Deferred FSW differs from x86");
    native_check(at<std::uint16_t>(image,6)==0x0f1,"Deferred FOP differs from x86");
#endif
    g.reset();
    g.cpu.write32(0x4100,0x3fc00000);
    g.run({0xd9,0x06,0,0x41});
    check(g.cpu.fpu_ip==Guest::code&&g.cpu.fpu_dp==0x4100&&g.cpu.fpu_opcode==0x106,
          "FLD did not record memory operand pointer");
    g.cpu.write16(0x4104,0xffff);
    g.run({0xd9,0x2e,4,0x41});
    check(g.cpu.fpu_control==0x1f7f&&g.cpu.fpu_dp==0x4100&&g.cpu.fpu_opcode==0x106,
          "FLDCW reserved bits/metadata semantics mismatch");
}

void load_store() {
    Guest g;
    const auto number=ext(0xc000000000000000,0x3fff);
    g.put(0x2000,number);
    g.run({0xdb,0x2e,0,0x20}); // FLD m80
    g.run({0xdb,0x3e,0x10,0x20}); // FSTP m80
    check(g.get(0x2010)==number&&g.cpu.fpu_tags==0xffff,"80-bit load/store lost raw precision");
    constexpr std::uint64_t integer=0x8000000000000000ULL;
    g.cpu.write64(0x2000,integer);
    g.run({0xdf,0x2e,0,0x20}); // FILD m64
    check(g.st()==ext(integer,0xc03e),"FILD m64 failed exact signed minimum");
    g.run({0xdf,0x3e,0x10,0x20}); // FISTP m64
    check(g.cpu.read64(0x2010)==integer,"FISTP m64 failed exact signed minimum");
    g.cpu.write16(0x2000,0x8000);
    g.run({0xdf,0x06,0,0x20}); // FILD m16
    g.run({0xdf,0x1e,0x10,0x20}); // FISTP m16
    check(g.cpu.read16(0x2010)==0x8000,"FILD/FISTP m16 failed");
    const std::array<std::uint32_t,4> rounded{2,1,2,1};
    const std::array<std::uint16_t,4> status{0x0220,0x0020,0x0220,0x0020};
    for (unsigned rc=0;rc<4;++rc) {
        g.reset();g.cpu.fpu_control|=rc<<10;
        const float source=1.5f;
        g.cpu.write32(0x2000,0x3fc00000);
        g.run({0xd9,0x06,0,0x20});
        g.run({0xdb,0x1e,0x10,0x20}); // FISTP m32
        check(g.cpu.read32(0x2010)==rounded[rc]&&g.cpu.fpu_status==status[rc],
              "FISTP did not use x87 rounding mode/precision status");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        NativeState guard;
        const std::uint16_t cw=std::uint16_t(0x037f|(rc<<10));
        std::uint32_t answer=0;
        std::uint16_t sw=0;
        asm volatile("fninit; fldcw %3; flds %2; fistpl %0; fnstsw %%ax; movw %%ax,%1"
            :"=m"(answer),"=m"(sw):"m"(source),"m"(cw):"ax","memory");
        native_check(answer==g.cpu.read32(0x2010)&&sw==g.cpu.fpu_status,
                     "FISTP differs from hardware x86");
#else
        (void)source;
#endif
    }
    g.reset();g.cpu.fpu_control&=~32U;
    g.put(0x2000,ext(0x8000008000000000,0x3fff)); // 1 + 2^-24
    g.run({0xdb,0x2e,0,0x20});
    g.run({0xd9,0x1e,0x10,0x20}); // FSTP m32, tie to even
    check(g.cpu.read32(0x2010)==0x3f800000&&(g.cpu.fpu_status&0x80a0)==0x80a0&&g.top()==0,
          "Unmasked precision failed to store, pop and defer");
    g.reset();g.cpu.fpu_control&=~16U;
    g.put(0x2000,ext(UINT64_C(1)<<63,0x3f6a)); // exact minimum binary32 subnormal
    g.cpu.write32(0x2010,0xdeadbeef);
    g.run({0xdb,0x2e,0,0x20});
    g.run({0xd9,0x1e,0x10,0x20});
    check(g.cpu.read32(0x2010)==0xdeadbeef&&g.cpu.fpu_status==0xb890&&g.top()==7,
          "Unmasked tiny store did not preserve the destination/TOP");
    g.reset();g.cpu.fpu_control&=~2U;g.cpu.write32(0x2000,1);
    g.run({0xd9,0x06,0,0x20});
    check(g.st()==ext(UINT64_C(1)<<63,0x3f6a)&&g.cpu.fpu_status==0xb882,
          "FLD m32 must push a denormal before deferring #MF");
    g.reset();
    g.cpu.write32(0x2000,0x7f810000); // signaling NaN
    g.run({0xd9,0x06,0,0x20});
    check(g.st()==ext(0xc100000000000000,0x7fff)&&g.cpu.fpu_status==0x3801,
          "Masked FLD signaling NaN did not quiet/preserve payload");
}

void env_and_compare() {
    Guest g;
    g.run({0xd9,0xe8}); // FLD1
    g.run({0xd9,0xeb}); // FLDPI
    const auto live=g.cpu.fpu_status;
    const auto tags=g.cpu.fpu_tags;
    g.run({0x66,0xdd,0x36,0,0x22}); // FNSAVE m108
    check(g.cpu.fpu_tags==0xffff&&g.cpu.fpu_status==0,"FNSAVE failed to reinitialize x87");
    check(g.cpu.read16(0x2204)==live&&g.cpu.read16(0x2208)==tags,
          "FNSAVE 32-bit environment fields mismatch");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    NativeState guard;
    std::array<std::uint8_t,108> native{};
    asm volatile("fninit; fld1; fldpi; fnsave %0":"=m"(native)::"memory");
    native_check(at<std::uint16_t>(native,4)==live&&at<std::uint16_t>(native,8)==tags,
                 "FNSAVE status/tags differ from hardware");
    for (unsigned i=28;i<48;++i)
        native_check(g.cpu.read8(0x2200+i)==native[i],"FNSAVE logical register ordering differs from hardware");
#endif
    g.run({0x66,0xdd,0x26,0,0x22}); // FRSTOR m108
    check(g.cpu.fpu_tags==tags&&g.st()==ext(0xc90fdaa22168c235,0x4000),
          "FRSTOR did not reconstitute the logical register stack");
    g.run({0xdf,0xe9}); // FUCOMIP ST0,ST1
    check((g.cpu.eflags&(Cpu::CF|Cpu::ZF|Cpu::PF))==0&&g.top()==7,
          "FUCOMIP actual ST0>ST1 flags/pop incorrect");
    g.run({0xd9,0xee}); // FLDZ
    g.run({0xde,0xd9}); // FCOMPP
    check((g.cpu.fpu_status&0x4500)==0x100&&g.cpu.fpu_tags==0xffff,
          "FCOMPP actual ST0<ST1 status/pop incorrect");
    g.reset();
    g.run({0xd9,0xe8});g.run({0xd9,0xe8});
    g.run({0xde,0xc1}); // FADDP ST1,ST0
    check(g.st()==ext(0x8000000000000000,0x4000)&&g.top()==7,
          "FADDP result or pop incorrect");
    g.run({0xd9,0xe8});g.run({0xde,0xe9}); // FSUBP ST1,ST0
    check(g.st()==ext(0x8000000000000000,0x3fff)&&g.top()==7,
          "FSUBP destination/reverse operands differed");
    g.reset();g.run({0xd9,0xe8});g.run({0xd9,0xe8});
    g.run({0xd9,0xe0}); // FCHS
    g.run({0xd9,0xe1}); // FABS
    check(g.st()==ext(UINT64_C(1)<<63,0x3fff),"FCHS/FABS do not preserve magnitude");
    g.run({0xd9,0xf7}); // FINCSTP
    check(g.top()==7,"FINCSTP did not adjust logical TOP");
}

void bcd_and_extract() {
    Guest g;
    const Ext bcd{0x21,0x43,0x65,0x87,0x09,0x21,0x43,0x65,0x87,0x80};
    g.put(0x2000,bcd);
    g.run({0xdf,0x26,0,0x20}); // FBLD
    g.run({0xdf,0x36,0x10,0x20}); // FBSTP
    check(g.get(0x2010)==bcd&&g.cpu.fpu_tags==0xffff,"Negative 18-digit packed BCD did not roundtrip");
    const auto input=ext(0xc400000000000000,0xc002); // -12.25
    g.put(0x2000,input);
    g.run({0xdb,0x2e,0,0x20});
    g.run({0xd9,0xf4}); // FXTRACT -> significand, exponent
    const auto sig=ext(0xc400000000000000,0xbfff);
    const auto power=ext(0xc000000000000000,0x4000);
    check(g.st()==sig&&g.st(1)==power,"FXTRACT separated the wrong significand/exponent");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    NativeState guard;
    alignas(16) std::array<std::uint8_t,512> image{};
    asm volatile("fninit; fldt %1; fxtract; fxsave64 %0":"=m"(image):"m"(input):"memory");
    for (unsigned i=0;i<10;++i)
        native_check(image[32+i]==sig[i]&&image[48+i]==power[i],"FXTRACT disagrees with hardware x86");
#endif
    g.reset();g.run({0xd9,0xee});g.run({0xd9,0xf4});
    check(g.st()==Ext{}&&g.st(1)==ext(UINT64_C(1)<<63,0xffff)&&g.cpu.fpu_status==0x3004,
          "FXTRACT zero divide-by-zero response incorrect");
}

void round_integer_pseudo_denormal() {
    Guest g;
    const auto input=ext(UINT64_C(1)<<63,0);
    struct Fixture { std::uint16_t cw,sw; Ext result; };
    const std::array<Fixture,3> fixtures{{
        {0x037f,0x3822,{}},
        {0x037d,0xb882,input},
        {0x0b7f,0x3a22,ext(UINT64_C(1)<<63,0x3fff)}
    }};
    for (const auto& fixture:fixtures) {
        g.reset();g.cpu.fpu_control=fixture.cw;
        g.put(0x2000,input);
        g.run({0xdb,0x2e,0,0x20});
        g.run({0xd9,0xfc}); // FRNDINT
        check(g.st()==fixture.result&&g.cpu.fpu_status==fixture.sw,
              "FRNDINT pseudo-denormal result or exception status mismatch");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        NativeState guard;
        alignas(16) std::array<std::uint8_t,512> native{};
        asm volatile("fninit; fldcw %1; fldt %2; frndint; fxsave64 %0"
            :"=m"(native):"m"(fixture.cw),"m"(input):"memory");
        native_check(at<std::uint16_t>(native,2)==fixture.sw&&
                     std::memcmp(native.data()+32,fixture.result.data(),10)==0,
                     "FRNDINT pseudo-denormal disagrees with hardware x86");
#endif
    }
}

void comparison_exceptions() {
    Guest g;
    const auto one=ext(UINT64_C(1)<<63,0x3fff);
    const auto tiny=ext((UINT64_C(1)<<63)|1,0);
    const auto qnan=ext(0xc000000000000001,0x7fff);
    struct Fixture { Ext left,right; std::uint16_t cw,sw; bool ordered,test_flags; };
    const std::array<Fixture,4> fixtures{{
        {qnan,one,0x037e,0xf581,true,false},
        {tiny,one,0x037d,0xb182,true,false},
        {tiny,qnan,0x037f,0x7d00,false,false},
        {qnan,one,0x037e,0xb081,true,true}
    }};
    for(const auto& f:fixtures) {
        g.reset();
        g.put(0x2000,f.right);g.put(0x2010,f.left);
        g.run({0xdb,0x2e,0,0x20});g.run({0xdb,0x2e,0x10,0x20});
        g.cpu.fpu_control=f.cw;
        if(f.test_flags) {
            g.run({0xdf,0xf1}); // FCOMIP ST1
            check((g.cpu.eflags&0x8d5U)==0x45U,"FCOMIP invalid exception EFLAGS mismatch");
        } else if(f.ordered) g.run({0xd8,0xd9}); // FCOMP ST1
        else g.run({0xdd,0xe9}); // FUCOMP ST1
        check(g.cpu.fpu_status==f.sw,"x87 compare condition/status or deferred pop mismatch");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
        NativeState guard;
        alignas(16) std::array<std::uint8_t,512> image{};
#define NATIVE_COMPARE(opcode) \
        asm volatile("fninit; fldt %3; fldt %2; fldcw %1; " opcode "; fxsave64 %0" \
            :"=m"(image):"m"(f.cw),"m"(f.left),"m"(f.right):"memory","cc")
        if(f.test_flags) { NATIVE_COMPARE(".byte 0xdf,0xf1"); }
        else if(f.ordered) { NATIVE_COMPARE(".byte 0xd8,0xd9"); }
        else { NATIVE_COMPARE(".byte 0xdd,0xe9"); }
#undef NATIVE_COMPARE
        native_check(at<std::uint16_t>(image,2)==f.sw,"x87 compare differs from native AMD status");
#endif
    }
}

void memory_exception_priority() {
    Guest g;
    const auto qnan=ext(0xc000000000000001,0x7fff);
    g.put(0x2000,qnan);g.cpu.write32(0x2010,1);
    g.run({0xdb,0x2e,0,0x20});
    g.run({0xd8,0x06,0x10,0x20}); // FADD with binary32 denormal.
    check(g.st()==qnan&&g.cpu.fpu_status==0x3800,
          "x87 memory add must handle quiet NaN before a denormal");
    g.cpu.write32(0x2010,0x7fbff00aU);
    g.run({0xd8,0x06,0x10,0x20}); // FADD with binary32 signaling NaN.
    check(g.st()==qnan&&g.cpu.fpu_status==0x3801,
          "x87 memory add must keep quiet NaN preference and raise invalid");
    g.reset();
    g.cpu.write32(0x2010,1);g.run({0xd9,0xee});
    g.run({0xd8,0x3e,0x10,0x20}); // FDIVR [binary32 denormal], ST0=0.
    check(g.st()==ext(UINT64_C(1)<<63,0x7fff)&&g.cpu.fpu_status==0x3804,
          "x87 memory divide must handle zero denominator before a denormal");
}

} // namespace

int main() {
    try {
        wait_and_pointers();
        load_store();
        env_and_compare();
        bcd_and_extract();
        round_integer_pseudo_denormal();
        comparison_exceptions();
        memory_exception_priority();
        std::cout<<"REVERSE_WINDOWS_X87_STATE_PASS checks="<<checks
                 <<" native_x86="<<native_x86<<" native_comparisons="<<comparisons<<'\n';
        return 0;
    } catch (const std::exception& e) {
        std::cerr<<"REVERSE_WINDOWS_X87_STATE_FAIL "<<e.what()<<'\n';
        return 1;
    }
}
