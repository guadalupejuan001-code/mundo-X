#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <iostream>
#include <stdexcept>

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#include <emmintrin.h>
#define HAS_NATIVE_X86 1
#else
#define HAS_NATIVE_X86 0
#endif

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::FaultCode;
using cosmic_pc::StepResult;
using Vector = std::array<std::uint8_t,16>;
using Lanes = std::array<std::uint32_t,4>;
constexpr auto pc=0xa000U;
constexpr auto status_flags=Cpu::CF|Cpu::PF|Cpu::ZF|Cpu::OF|2U;
unsigned checks=0, native_checks=0;

void check(bool pass,const char* context) {
  ++checks;
  if(!pass)throw std::runtime_error(context);
}

void lanes(Cpu& cpu,unsigned reg,const Lanes& values) {
  for(unsigned lane=0;lane<4;++lane)
    for(unsigned byte=0;byte<4;++byte)cpu.xmm[reg][lane*4+byte]=std::uint8_t(values[lane]>>(byte*8));
}

Lanes lanes(const Cpu& cpu,unsigned reg) {
  Lanes values{};
  for(unsigned lane=0;lane<4;++lane)
    for(unsigned byte=0;byte<4;++byte)values[lane]|=std::uint32_t(cpu.xmm[reg][lane*4+byte])<<(byte*8);
  return values;
}

float fp32(std::uint32_t bits) {
  float value;
  std::memcpy(&value,&bits,sizeof(value));
  return value;
}

StepResult program(Cpu& cpu,std::initializer_list<std::uint8_t> code) {
  cpu.memory.write_block(pc,code.begin(),code.size());
  cpu.eip=pc;cpu.halted=false;cpu.fault.reset();
  return cpu.step();
}

void retire(Cpu& cpu,std::initializer_list<std::uint8_t> code) {
  if(program(cpu,code)!=StepResult::Executed)
    throw std::runtime_error(cpu.fault?cpu.fault->describe():"expected SSE instruction to retire");
}

void trap(Cpu& cpu,std::initializer_list<std::uint8_t> code,FaultCode fault) {
  check(program(cpu,code)==StepResult::Faulted&&cpu.fault&&cpu.fault->code==fault&&cpu.eip==pc,
        "expected SSE fault and rollback");
}

void real_mode(Cpu& cpu) {
  cpu.reset(0,pc);cpu.a20_enabled=true;cpu.cr4=0x600;cpu.eflags=status_flags;
}

void relative_accuracy(std::uint32_t bits,double wanted,const char* context) {
  const auto result=double(fp32(bits));
  check(std::abs((result-wanted)/wanted)<=1.5*0x1p-12,context);
}

void reciprocals() {
  Cpu cpu(2*1024*1024);real_mode(cpu);
  constexpr Lanes values{0x40400000,0xc0c00000,0x3e800000,0x42c80000}; // 3, -6, 1/4, 100.
  constexpr Lanes signed_special{0,0x80000000,0x00000001,0xff800000};
  constexpr Lanes rcp_special{0x7f800000,0xff800000,0x7f800000,0x80000000};
  constexpr Lanes sqrt_special{0x7f800000,0xff800000,0x7f800000,0xffc00000};
  constexpr Lanes nan_values{0x7fa12345,0xffd12345,0x80800000,0x7f7fffff};
  constexpr Lanes rcp_nan{0x7fe12345,0xffd12345,0xfe800000,0};
  Lanes from_nearest{};
  for(unsigned rc=0;rc<4;++rc) {
    cpu.mxcsr=0x1f80|(rc<<13)|0x20;
    const auto before=cpu.mxcsr;
    lanes(cpu,1,values);
    retire(cpu,{0x0f,0x53,0xc1});
    const auto result=lanes(cpu,0);
    if(!rc)from_nearest=result;
    check(result==from_nearest&&cpu.mxcsr==before&&cpu.eflags==status_flags,
          "RCPPS must ignore rounding mode and preserve flags");
    for(unsigned lane=0;lane<4;++lane)
      relative_accuracy(result[lane],1.0/double(fp32(values[lane])),"RCPPS exceeds 1.5*2^-12 error");
  }
  lanes(cpu,1,signed_special);retire(cpu,{0x0f,0x53,0xc1});
  check(lanes(cpu,0)==rcp_special,"RCPPS signed zero/denormal/infinity mismatch");
  retire(cpu,{0x0f,0x52,0xc1});
  check(lanes(cpu,0)==sqrt_special,"RSQRTPS signed zero/denormal/infinity mismatch");
  lanes(cpu,1,nan_values);retire(cpu,{0x0f,0x53,0xc1});
  auto result=lanes(cpu,0);
  check(result[0]==rcp_nan[0]&&result[1]==rcp_nan[1]&&result[3]==rcp_nan[3],"RCPPS NaN/tiny result mismatch");
  relative_accuracy(result[2],-0x1p126,"RCPPS near min-normal source exceeded its accuracy bound");
  lanes(cpu,1,values);retire(cpu,{0x0f,0x52,0xc1});
  result=lanes(cpu,0);
  for(const unsigned lane:{0U,2U,3U})
    relative_accuracy(result[lane],1.0/std::sqrt(double(fp32(values[lane]))),"RSQRTPS exceeds 1.5*2^-12 error");
  check(result[1]==0xffc00000,"RSQRTPS negative result must be indefinite");
  lanes(cpu,0,{0,0x10101010,0xabcdefed,0x01234567});
  const auto prior=lanes(cpu,0);
  cpu.write32(0x2001,0x40800000U);
  retire(cpu,{0xf3,0x0f,0x52,0x06,0x01,0x20});
  result=lanes(cpu,0);
  relative_accuracy(result[0],0.5,"RSQRTSS unaligned scalar source mismatch");
  check(result[1]==prior[1]&&result[2]==prior[2]&&result[3]==prior[3],"RSQRTSS modified upper lanes");
  retire(cpu,{0xf3,0x0f,0x53,0xc1});
  relative_accuracy(lanes(cpu,0)[0],1.0/3,"RCPSS register source mismatch");
  trap(cpu,{0x0f,0x53,0x06,0x01,0x20},FaultCode::ProtectionFault);
  trap(cpu,{0x66,0x0f,0x52,0xc1},FaultCode::InvalidInstruction);
#if HAS_NATIVE_X86
  const auto native=[&](const Lanes& src,bool sqrt) {
    Lanes out{};
    auto saved=_mm_getcsr();
    _mm_setcsr(0x3fa0U);
    if(sqrt)asm volatile("movups %1,%%xmm0; rsqrtps %%xmm0,%%xmm0; movups %%xmm0,%0":"=m"(out):"m"(src):"xmm0");
    else asm volatile("movups %1,%%xmm0; rcpps %%xmm0,%%xmm0; movups %%xmm0,%0":"=m"(out):"m"(src):"xmm0");
    const auto csr=_mm_getcsr();
    _mm_setcsr(saved);
    check(csr==0x3fa0U,"hardware SSE approximation changed MXCSR");
    ++native_checks;
    return out;
  };
  check(native(signed_special,false)==rcp_special,"physical RCPPS special values differ");
  check(native(signed_special,true)==sqrt_special,"physical RSQRTPS special values differ");
  const auto floats=native(values,false),roots=native(values,true);
  for(unsigned lane=0;lane<4;++lane)
    relative_accuracy(floats[lane],1.0/double(fp32(values[lane])),"physical RCPPS accuracy");
  for(const unsigned lane:{0U,2U,3U})
    relative_accuracy(roots[lane],1.0/std::sqrt(double(fp32(values[lane]))),"physical RSQRTPS accuracy");
#endif
}

void reciprocal_order_and_underflow() {
  Cpu cpu(2*1024*1024);real_mode(cpu);
  constexpr std::uint32_t first=0x00800000U,last=0x7f800000U,step=0x0002c37fU;
  cpu.mxcsr=0x6f25; // Directed rounding, sticky flags, unmasked exceptions.
  for(const auto opcode:{0x52U,0x53U}) {
    std::uint32_t previous=0xffffffffU;
    for(std::uint32_t raw=first;raw<last;raw+=step) {
      lanes(cpu,1,{raw,0,0,0});
      retire(cpu,{0xf3,0x0f,std::uint8_t(opcode),0xc1});
      const auto result=lanes(cpu,0)[0];
      check(result<=previous&&cpu.mxcsr==0x6f25,"reciprocal was nonmonotonic or changed MXCSR");
      if((raw<0x7e7fe800U)||opcode==0x52U) {
        const auto value=double(fp32(raw));
        relative_accuracy(result,1.0/(opcode==0x52U?std::sqrt(value):value),"reciprocal approximation sweep exceeded error bound");
      }
      previous=result;
    }
  }
  lanes(cpu,1,{0x7e7fe000U,0x7e7fffffU,0x7e800001U,0xfe800001U});
  retire(cpu,{0x0f,0x53,0xc1});
  const auto result=lanes(cpu,0);
  check(result[0]>=0x00800000U&&result[1]>=0x00800000U&&result[2]==0&&result[3]==0x80000000U,
        "RCPPS boundary must flush genuinely subnormal results with their signs");
}

void masked_and_non_temporal_stores() {
  Cpu cpu(2*1024*1024);real_mode(cpu);
  constexpr auto destination=0x2003U;
  Vector src{},mask{},before{};
  for(unsigned i=0;i<16;++i){src[i]=std::uint8_t(0x80+i);mask[i]=i%3==0?0x81:0x7f;before[i]=std::uint8_t(i);}
  cpu.xmm[0]=src;cpu.xmm[1]=mask;cpu.regs[Cpu::EDI]=0xcafe0003U;cpu.segments[Cpu::DS]=0x200;
  cpu.memory.write_block(destination,before.data(),before.size());
  retire(cpu,{0x66,0x0f,0xf7,0xc1});
  Vector after{};
  cpu.memory.read_block(destination,after.data(),after.size());
  Vector expected=before;
  for(unsigned i=0;i<16;++i)if(mask[i]&0x80U)expected[i]=src[i];
  check(after==expected&&cpu.regs[Cpu::EDI]==0xcafe0003U&&cpu.xmm[0]==src&&cpu.xmm[1]==mask&&
        cpu.eflags==status_flags,"MASKMOVDQU must write only selected bytes and preserve registers");
#if HAS_NATIVE_X86
  auto native=before;
  __m128i xsrc,xmask;
  std::memcpy(&xsrc,src.data(),16);std::memcpy(&xmask,mask.data(),16);
  _mm_maskmoveu_si128(xsrc,xmask,reinterpret_cast<char*>(native.data()));_mm_sfence();
  check(after==native,"MASKMOVDQU differs from physical x86");++native_checks;
#endif
  cpu.segments[Cpu::FS]=0x400;
  cpu.regs[Cpu::EDI]=0x1003U;
  retire(cpu,{0x64,0x67,0x66,0x0f,0xf7,0xc1});
  check(cpu.read8(0x5003)==0x80&&cpu.read8(0x5004)==0,"MASKMOVDQU ignored FS/32-bit address override");
  trap(cpu,{0x66,0x0f,0xf7,0x06,0,0x20},FaultCode::InvalidInstruction);
  cpu.xmm[1].fill(0);cpu.regs[Cpu::EDI]=0xfff0;
  retire(cpu,{0x66,0x0f,0xf7,0xc1});
  check(cpu.read8(0x11ff0)==0,"zero mask wrote at the last valid segment span");
  cpu.regs[Cpu::EDI]=0xfff1;
  trap(cpu,{0x66,0x0f,0xf7,0xc1},FaultCode::SegmentLimit);
  cpu.xmm[1]=mask;
  cpu.regs[Cpu::EDI]=0xfff8;
  trap(cpu,{0x66,0x0f,0xf7,0xc1},FaultCode::SegmentLimit);
  check(cpu.read8(0x11ff8)==0,"MASKMOVDQU partially wrote through segment fault");
  unsigned device_writes=0;
  cpu.regs[Cpu::EDI]=3;
  cpu.mmio_claim_hook=[](std::uint64_t address){return address==destination+3;};
  cpu.mmio_write_hook=[&](std::uint64_t,std::uint8_t){++device_writes;return true;};
  cpu.write8(destination,0);
  trap(cpu,{0x66,0x0f,0xf7,0xc1},FaultCode::UnsupportedState);
  check(device_writes==0&&cpu.read8(destination)==0,"MASKMOVDQU partially committed an MMIO store");
  cpu.mmio_claim_hook={};cpu.mmio_write_hook={};
  cpu.segments[Cpu::DS]=0;cpu.cr4=0;cpu.cr0|=0x0c;
  cpu.regs[Cpu::EAX]=0x12345678;
  retire(cpu,{0x0f,0xc3,0x06,0x01,0x40});
  check(cpu.read32(0x4001)==0x12345678&&cpu.eflags==status_flags,
        "MOVNTI m32 must store full GPR without requiring SSE enabled state");
  trap(cpu,{0x0f,0xc3,0xc1},FaultCode::InvalidInstruction);
  trap(cpu,{0x66,0x0f,0xc3,0x06,0x01,0x40},FaultCode::InvalidInstruction);
#if HAS_NATIVE_X86
  std::uint32_t native32=0;
  asm volatile("movnti %1,%0; sfence":"=m"(native32):"r"(0x12345678U):"memory");
  check(cpu.read32(0x4001)==native32,"MOVNTI differs from physical x86");++native_checks;
#endif
  retire(cpu,{0x0f,0xae,0xef}); // LFENCE, ignored r/m=7.
  retire(cpu,{0x0f,0xae,0xf7}); // MFENCE, ignored r/m=7.
  retire(cpu,{0x0f,0xae,0xff}); // SFENCE, ignored r/m=7.
  trap(cpu,{0xf3,0x0f,0xae,0x06,0,0x40},FaultCode::InvalidInstruction); // F3 FXSAVE.
  trap(cpu,{0xf2,0x0f,0xae,0x16,0,0x40},FaultCode::InvalidInstruction); // F2 LDMXCSR.
  trap(cpu,{0x66,0x0f,0xae,0xe8},FaultCode::InvalidInstruction); // 66 LFENCE.
}

void conversion_preserves_host_environment() {
  Cpu cpu(2*1024*1024);real_mode(cpu);
  lanes(cpu,1,{0x12345678,0x7ff10000,0,0}); // Signaling binary64 NaN.
#if HAS_NATIVE_X86
  const auto saved=_mm_getcsr();
  _mm_setcsr(saved&~0x3fU);
#endif
  retire(cpu,{0xf2,0x0f,0x2d,0xc1});
#if HAS_NATIVE_X86
  const auto after=_mm_getcsr();
  _mm_setcsr(saved);
  check(after==(saved&~0x3fU),"guest signaling NaN polluted host MXCSR");
#endif
  check(cpu.regs[Cpu::EAX]==0x80000000U&&(cpu.mxcsr&1),"CVTSD2SI signaling NaN guest response");
}

template<class Bits>
void exceptions_and_ftz_boundaries() {
  constexpr bool single=sizeof(Bits)==4;
  constexpr std::uint8_t prefix=single?0xf3:0xf2;
  constexpr Bits sign=Bits{1}<<(sizeof(Bits)*8-1);
  constexpr Bits min_normal=Bits{1}<<(single?23:52);
  constexpr Bits one=Bits(single?0x3f800000:0x3ff0000000000000ULL);
  constexpr Bits half=one-min_normal;
  constexpr Bits infinite=Bits(single?0x7f800000:0x7ff0000000000000ULL);
  constexpr Bits indefinite=Bits(single?0xffc00000:0xfff8000000000000ULL);
  struct Case {std::uint8_t opcode;Bits left,right,out;std::uint32_t csr,flags;bool faults;};
  const Case cases[]{
    {0x59,min_normal,one-1,min_normal,0x1f80,0x30,false},
    {0x59,min_normal,one-1,0,0x9f80,0x30,false},
    {0x59,min_normal|sign,one-1,sign,0x9f80,0x30,false},
    {0x59,min_normal+1,one-1,min_normal,0x9f80,0x20,false},
    {0x59,min_normal,half,min_normal/2,0x1f80,0,false},
    {0x59,min_normal,half,0,0x9f80,0x30,false},
    {0x59,min_normal,one-1,0,0x1780,0x10,true},
    {0x5e,min_normal,one+1,0,0x1780,0x30,true},
    {0x5e,1,sign,infinite|sign,0x1e80,0x04,false},
    {0x5e,1,sign,0,0x1d80,0x04,true},
    {0x51,Bits{0},sign|1,indefinite,0x1e80,0x01,false},
    {0x51,Bits{0},sign|1,0,0x1f00,0x01,true},
    {0x51,Bits{0},sign|1,sign,0x1fc0,0,false},
    {0x59,1,half,0,0x1e80,0x02,true}
  };
  Cpu cpu(2*1024*1024);real_mode(cpu);
  const auto unpack=[](Bits low) {
    if constexpr(single)return Lanes{std::uint32_t(low),0x11223344,0x55667788,0x99aabbcc};
    else return Lanes{std::uint32_t(low),std::uint32_t(low>>32),0x55667788,0x99aabbcc};
  };
  for(const auto& entry:cases) {
    const auto left=unpack(entry.left),right=unpack(entry.right);
    lanes(cpu,0,left);lanes(cpu,1,right);cpu.mxcsr=entry.csr;
    const auto before=cpu.instructions;
    if(entry.faults)
      trap(cpu,{prefix,0x0f,entry.opcode,0xc1},FaultCode::SimdFloatingPoint);
    else retire(cpu,{prefix,0x0f,entry.opcode,0xc1});
    check(lanes(cpu,0)==(entry.faults?left:unpack(entry.out))&&cpu.eflags==status_flags,
          "SIMD exception or FTZ boundary retired the wrong value/state");
    check(cpu.mxcsr==(entry.csr|entry.flags)&&cpu.instructions==before+(entry.faults?0:1),
          "SIMD exception priority or retirement mismatch");
#if HAS_NATIVE_X86
    if(!entry.faults) {
      Lanes native{};
      const auto saved=_mm_getcsr();
      _mm_setcsr(entry.csr);
#define ARITHMETIC_PROBE(mnemonic) asm volatile( \
    "movups %1,%%xmm0;movups %2,%%xmm1;" mnemonic " %%xmm1,%%xmm0;movups %%xmm0,%0" \
    :"=m"(native):"m"(left),"m"(right):"xmm0","xmm1")
      if(entry.opcode==0x59) {
        if constexpr(single) ARITHMETIC_PROBE("mulss"); else ARITHMETIC_PROBE("mulsd");
      }else if(entry.opcode==0x5e) {
        if constexpr(single) ARITHMETIC_PROBE("divss"); else ARITHMETIC_PROBE("divsd");
      }else {
        if constexpr(single) ARITHMETIC_PROBE("sqrtss"); else ARITHMETIC_PROBE("sqrtsd");
      }
#undef ARITHMETIC_PROBE
      const auto native_csr=_mm_getcsr();_mm_setcsr(saved);
      check(native==unpack(entry.out)&&native_csr==(entry.csr|entry.flags),
            "physical x86 SIMD exception or FTZ golden mismatch");
      ++native_checks;
    }
#endif
  }
}

void long_mode(Cpu& cpu) {
  cpu.reset();
  const auto image=cosmic_pc::long_mode_boot_disk();
  cpu.memory.write_block(0x7c00,image.data(),image.size());
  cpu.interrupt_hook=[](Cpu& current,std::uint8_t vector){
    if(vector!=0x15||current.reg16(Cpu::EAX)!=0x2401)return false;
    current.a20_enabled=true;current.set_reg8(4,0);current.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned i=0;i<12000&&!cpu.is_long_mode();++i)
    if(cpu.step()!=StepResult::Executed)throw std::runtime_error("SSE fixture failed to reach long mode");
  check(cpu.is_long_mode(),"long-mode fixture did not reach IA-32e");
  cpu.idtr={0,0};
  cpu.cr4|=0x200;
}

void cross_page_and_movnti64() {
  Cpu cpu(4*1024*1024);long_mode(cpu);
  cpu.regs[Cpu::EDI]=0x20001;cpu.regs[Cpu::EAX]=0xfedcba9876543210ULL;
  retire(cpu,{0x48,0x0f,0xc3,0x07});
  check(cpu.read64(0x20001)==0xfedcba9876543210ULL,"MOVNTI REX.W did not store 64 bits");
  for(unsigned i=0;i<512;++i)cpu.write64(0x4000+i*8,(std::uint64_t(i)<<12)|3);
  cpu.write64(0x3000,0x4003);
  cpu.write64(0x4000+(0x21000>>12)*8,0);
  cpu.xmm[0].fill(0x7e);cpu.xmm[1].fill(0);
  cpu.regs[Cpu::EDI]=0x20fff;
  cpu.write8(0x20fff,0x13);
  trap(cpu,{0x66,0x0f,0xf7,0xc1},FaultCode::PageFault);
  check(cpu.fault->linear_address==0x21000&&cpu.fault->error_code==2,"zero mask did not validate the full write span");
  cpu.xmm[1][0]=0xff;
  trap(cpu,{0x66,0x0f,0xf7,0xc1},FaultCode::PageFault);
  check(cpu.read8(0x20fff)==0x13,"MASKMOVDQU committed selected first byte before page fault");
}
} // namespace

int main() {
  try {
    reciprocals();reciprocal_order_and_underflow();masked_and_non_temporal_stores();
    conversion_preserves_host_environment();
    exceptions_and_ftz_boundaries<std::uint32_t>();exceptions_and_ftz_boundaries<std::uint64_t>();
    cross_page_and_movnti64();
    std::cout<<"REVERSE_WINDOWS_SSE_LEGACY_FORMS_PASS checks="<<checks
             <<" native_x86_sets="<<native_checks<<'\n';
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"REVERSE_WINDOWS_SSE_LEGACY_FORMS_FAIL "<<error.what()<<'\n';
    return 1;
  }
}
