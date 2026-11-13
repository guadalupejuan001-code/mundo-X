#include "../x86_cpu.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
#include <xmmintrin.h>
#endif
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void need(bool yes,const char* what){++checks;if(!yes)throw std::runtime_error(what);}
void setlane(Cpu& cpu,unsigned reg,unsigned lane,std::uint64_t raw){
 for(unsigned i=0;i<8;++i)cpu.xmm[reg][lane*8+i]=std::uint8_t(raw>>(i*8));
}
std::uint64_t lane(const Cpu& cpu,unsigned reg,unsigned lane){
 std::uint64_t r=0;
 for(unsigned i=0;i<8;++i)r|=std::uint64_t(cpu.xmm[reg][lane*8+i])<<(8*i);
 return r;
}
void instruction(Cpu& cpu,std::initializer_list<std::uint8_t> bytes){
 std::vector<std::uint8_t> body(bytes);
 cpu.memory.write_block(0x1000,body.data(),body.size());
 cpu.eip=0x1000;cpu.halted=false;cpu.fault.reset();
 const auto result=cpu.step();
 if(result!=StepResult::Executed)throw std::runtime_error(
    cpu.fault?cpu.fault->describe():"x86 opcode not executed");
}
void compares(){
 Cpu cpu(2*1024*1024);cpu.reset(0,0x1000);cpu.cr4=0x600;cpu.a20_enabled=true;
 setlane(cpu,0,0,0x3ff0000000000000ULL); // 1
 setlane(cpu,0,1,0x4000000000000000ULL); // 2
 setlane(cpu,1,0,0x3ff0000000000000ULL); // 1
 setlane(cpu,1,1,0x4008000000000000ULL); // 3
 cpu.mxcsr=0x1f80;
 instruction(cpu,{0x66,0x0f,0xc2,0xc1,0x00}); // CMPPD EQ
 need(lane(cpu,0,0)==UINT64_MAX&&lane(cpu,0,1)==0,
      "CMPPD EQ mask must compare two independent FP64 lanes");
#if defined(__x86_64__) || defined(__i386__)
 alignas(16) std::array<std::uint64_t,2> lhs{
      0x3ff0000000000000ULL,0x4000000000000000ULL};
 alignas(16) std::array<std::uint64_t,2> rhs{
      0x3ff0000000000000ULL,0x4008000000000000ULL};
 alignas(16) std::array<std::uint64_t,2> observed{};
 asm volatile("movapd %1,%%xmm0; movapd %2,%%xmm1; cmppd $0,%%xmm1,%%xmm0; movapd %%xmm0,%0"
              :"=m"(observed):"m"(lhs),"m"(rhs):"xmm0","xmm1","memory");
 need(observed[0]==lane(cpu,0,0)&&observed[1]==lane(cpu,0,1),
      "CMPPD result masks differ from native x86 SSE2");
#endif
 cpu.mxcsr=0x1f80;
 setlane(cpu,0,0,0x3ff0000000000000ULL);
 setlane(cpu,0,1,0x4018000000000000ULL);
 setlane(cpu,1,0,0x7ff8000000000000ULL); // QNaN
 instruction(cpu,{0xf2,0x0f,0xc2,0xc1,0x03}); // CMPSD UNORD
 need(lane(cpu,0,0)==UINT64_MAX&&lane(cpu,0,1)==0x4018000000000000ULL,
      "CMPSD UNORD must preserve high64 and return true for QNaN");
 need((cpu.mxcsr&1U)==0,"quiet unordered QNaN compare must not set IE");
 setlane(cpu,0,0,0x3ff0000000000000ULL);
 instruction(cpu,{0xf2,0x0f,0xc2,0xc1,0x01}); // CMPSD LT: signaling
 need((cpu.mxcsr&1U)!=0,"signaling CMPSD predicate with QNaN must set IE");
 cpu.mxcsr=0x1f80&~(1U<<7);
 setlane(cpu,0,0,0x3ff0000000000000ULL);
 const auto before=cpu.xmm[0];
 std::vector<std::uint8_t> code{0xf2,0x0f,0xc2,0xc1,0x01};
 cpu.memory.write_block(0x1000,code.data(),code.size());
 cpu.eip=0x1000;cpu.fault.reset();
 const auto result=cpu.step();
 need(result==StepResult::Faulted&&cpu.fault&&
      cpu.fault->code==cosmic_pc::FaultCode::SimdFloatingPoint,
      "unmasked compare IE must raise #XM");
 need((cpu.mxcsr&1U)&&cpu.xmm[0]==before,
      "unmasked compare must keep sticky IE but preserve XMM destination");
 std::cout<<"C09R1_STAGE4_CMPPD_CMPSD_NAN_X86_PASS predicates=1\n";
}
void moves_and_logic(){
 Cpu cpu(2*1024*1024);cpu.reset(0,0x1000);cpu.cr4=0x600;cpu.a20_enabled=true;
 setlane(cpu,0,0,0x1111111111111111ULL);
 setlane(cpu,0,1,0x2222222222222222ULL);
 setlane(cpu,1,0,0x3333333333333333ULL);
 setlane(cpu,1,1,0x4444444444444444ULL);
 instruction(cpu,{0x66,0x0f,0xc6,0xc1,0x02}); // SHUFPD select dst low, src high
 need(lane(cpu,0,0)==0x1111111111111111ULL&&
      lane(cpu,0,1)==0x4444444444444444ULL,
      "SHUFPD source/dest lane selection wrong");
 setlane(cpu,1,0,0x8000000000000000ULL);
 setlane(cpu,1,1,0x0000000000000000ULL);
 instruction(cpu,{0x66,0x0f,0x50,0xc1}); // MOVMSKPD EAX,XMM1
 need(cpu.regs[Cpu::EAX]==1,"MOVMSKPD must return both FP64 sign bits");
 setlane(cpu,0,0,0xf0f0f0f0f0f0f0f0ULL);
 setlane(cpu,0,1,0xffff0000ffff0000ULL);
 setlane(cpu,1,0,0x0f0f0f0f0f0f0f0fULL);
 setlane(cpu,1,1,0x0000ffff0000ffffULL);
 instruction(cpu,{0x66,0x0f,0x54,0xc1}); // ANDPD
 need(lane(cpu,0,0)==0&&lane(cpu,0,1)==0,"ANDPD bitwise result wrong");
 instruction(cpu,{0x66,0x0f,0x56,0xc1}); // ORPD
 need(lane(cpu,0,0)==0x0f0f0f0f0f0f0f0fULL,
      "ORPD bitwise result wrong");
 instruction(cpu,{0x66,0x0f,0x57,0xc1}); // XORPD
 need(lane(cpu,0,0)==0,"XORPD bitwise result wrong");
 cpu.write64(0x5000,0x1234567890abcdefULL);
 cpu.write64(0x5008,0xfedcba0987654321ULL);
 setlane(cpu,0,0,0x1111111111111111ULL);
 setlane(cpu,0,1,0x2222222222222222ULL);
 instruction(cpu,{0x66,0x0f,0x12,0x06,0x00,0x50}); // MOVLPD xmm0,[5000]
 need(lane(cpu,0,0)==0x1234567890abcdefULL&&
      lane(cpu,0,1)==0x2222222222222222ULL,
      "MOVLPD load must preserve high64");
 instruction(cpu,{0x66,0x0f,0x16,0x06,0x08,0x50}); // MOVHPD xmm0,[5008]
 need(lane(cpu,0,1)==0xfedcba0987654321ULL,
      "MOVHPD high64 load wrong");
 instruction(cpu,{0x66,0x0f,0x13,0x06,0x10,0x50}); // MOVLPD [5010],xmm0
 need(cpu.read64(0x5010)==0x1234567890abcdefULL,
      "MOVLPD memory store wrong");
 instruction(cpu,{0x66,0x0f,0x17,0x06,0x18,0x50}); // MOVHPD [5018],xmm0
 need(cpu.read64(0x5018)==0xfedcba0987654321ULL,
      "MOVHPD memory store wrong");
 std::cout<<"C09R1_STAGE4_SHUFPD_MOVMSKPD_ANDPD_MOVHPD_PASS\n";
}
}
int main(){
 try{
   compares();moves_and_logic();
   std::cout<<"REVERSE_WINDOWS_C09R1_STAGE4_SSE2_MISC_PASS checks="<<checks
            <<" cpuid_sse2=false windows_iso_used=false\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"REVERSE_WINDOWS_C09R1_STAGE4_SSE2_MISC_FAIL "<<e.what()<<"\n";return 1;
 }
}
