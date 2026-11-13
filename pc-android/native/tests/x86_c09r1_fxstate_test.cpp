#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"
#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <vector>
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
namespace {
unsigned checks=0;
void check(bool value,const char* why){++checks;if(!value)throw std::runtime_error(why);}
StepResult execute(Cpu& cpu,std::initializer_list<std::uint8_t> bytes){
  const std::vector<std::uint8_t> code(bytes);
  cpu.memory.write_block(0x1000,code.data(),code.size());
  cpu.eip=0x1000;cpu.halted=false;cpu.fault.reset();
  return cpu.step();
}
void real16_fxstate() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  for(unsigned i=0;i<16;++i)cpu.xmm[0][i]=std::uint8_t(0xa0+i);
  for(unsigned i=0;i<512;++i)cpu.memory[0x4000+i]=0x77;
  check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,"FLD1 failed");
  check(execute(cpu,{0xd9,0xee})==StepResult::Executed,"FLDZ failed");
  check(((cpu.fpu_status>>11)&7U)==6&&((cpu.fpu_tags>>12)&3U)==1&&((cpu.fpu_tags>>14)&3U)==0,
        "x87 physical stack and zero/valid tags are wrong");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Executed,
        "FXSAVE m512 legacy not executed");
  check(cpu.read16(0x4000)==0x037f&&cpu.read16(0x4002)==0x3000,
        "FXSAVE control/status or TOP incorrect");
  check(cpu.memory[0x4004]==0xc0,
        "FXSAVE must store abridged tags in physical R6/R7 order");
  check(cpu.read32(0x4000+24)==0x1f80&&
        cpu.read32(0x4000+28)==Cpu::kMxcsrMask,
        "FXSAVE MXCSR and mask fields invalid");
  check(cpu.read64(0x4000+32)==0&&
        cpu.read64(0x4000+48)==0x8000000000000000ULL&&
        cpu.read16(0x4000+48+8)==0x3fff,
        "FXSAVE must store logical ST0=0, ST1=+1 despite physical FTW c0");
#if defined(__x86_64__) || defined(__i386__)
  // Independent x86 hardware oracle. The host state is saved and restored;
  // no Windows executable, ISO or other CPU model is used.
  alignas(16)std::array<std::uint8_t,512> host_saved{},host_fx{};
  asm volatile("fxsave %0" : "=m"(host_saved) :: "memory");
  asm volatile("fninit; fld1; fldz; fxsave %0" : "=m"(host_fx) :: "memory");
  asm volatile("fxrstor %0" :: "m"(host_saved) : "memory");
  for(unsigned i=0;i<5;++i)
    check(host_fx[i]==cpu.memory[0x4000+i],
          "x87 FXSAVE control/status/abridged tag differs from real CPU");
  for(unsigned st=0;st<2;++st)
    for(unsigned byte=0;byte<10;++byte)
      check(host_fx[32+st*16+byte]==cpu.memory[0x4000+32+st*16+byte],
            "x87 FXSAVE logical ST data slot differs from real CPU");
  std::cout<<"C09R1_X87_FXSAVE_REAL_HARDWARE_ORACLE_PASS logical_ST=1 physical_FTW=1\n";
#endif
  for(unsigned i=0;i<16;++i)
    check(cpu.memory[0x4000+160+i]==std::uint8_t(0xa0+i),
          "FXSAVE XMM0 slot bytes wrong");
  for(unsigned i=464;i<512;++i)
    check(cpu.memory[0x4000+i]==0x77,
          "FXSAVE must not overwrite software-owned bytes 464..511");
  check(execute(cpu,{0xdb,0xe3})==StepResult::Executed,
        "FNINIT did not reset x87");
  check(cpu.fpu_status==0&&cpu.fpu_tags==0xffff,
        "FNINIT failed to empty x87 stack");
  cpu.mxcsr=0;
  cpu.xmm[0].fill(0);
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Executed,
        "FXRSTOR legacy not executed");
  check(cpu.fpu_control==0x037f&&((cpu.fpu_status>>11)&7U)==6&&cpu.fpu_tags!=0xffff&&
        cpu.mxcsr==0x1f80,
        "FXRSTOR did not restore FPU TOP, tags, FCW and MXCSR");
  for(unsigned i=0;i<16;++i)
    check(cpu.xmm[0][i]==std::uint8_t(0xa0+i),
          "FXRSTOR lost XMM0 bytes");
  check(execute(cpu,{0xdf,0xe0})==StepResult::Executed &&
        cpu.reg16(Cpu::EAX)==0x3000,
        "FNSTSW AX did not expose restored TOP");
  check(execute(cpu,{0xdd,0x1e,0x00,0x50})==StepResult::Executed,
        "FSTP m64 real of ST0 zero failed");
  check(cpu.read64(0x5000)==0 && ((cpu.fpu_status>>11)&7U)==7,
        "FSTP did not pop zero or encode IEEE64 +0");
  check(execute(cpu,{0xdd,0x1e,0x08,0x50})==StepResult::Executed,
        "FSTP m64 real of ST0 one failed");
  check(cpu.read64(0x5008)==0x3ff0000000000000ULL&&
        cpu.fpu_tags==0xffff&&((cpu.fpu_status>>11)&7U)==0,
        "x87 FLD1/FSTP did not roundtrip exact IEEE +1");
  std::cout<<"C09R1_FXSTATE_REAL16_ROUNDTRIP_PASS software_tail_untouched=1\n";
}
void fxstate_failures() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,
        "FLD1 fixture failed");
  check(execute(cpu,{0x0f,0xae,0x06,0x01,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,
        "misaligned FXSAVE must be #GP");
  check(((cpu.fpu_status>>11)&7U)==7&&((cpu.fpu_tags>>14)&3U)==0,
        "misaligned FXSAVE modified live x87 state");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0xff})==StepResult::Faulted,
        "512 byte FXSAVE crossing segment boundary must fault");
  check(((cpu.fpu_status>>11)&7U)==7,"failed save corrupted the x87 stack");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Executed,
        "FXSAVE fixture failed");
  cpu.memory[0x4000+27]=0x80; // MXCSR reserved high bit
  cpu.xmm[0].fill(0x5a);
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::ProtectionFault,
        "FXRSTOR must reject reserved MXCSR bits with #GP");
  check(cpu.mxcsr==0x1f80&&cpu.xmm[0][0]==0x5a&&((cpu.fpu_status>>11)&7U)==7,
        "FXRSTOR invalid image changed registers despite rollback");
  cpu.cr0|=8; // TS
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::DeviceNotAvailable,
        "FXSAVE CR0.TS must produce #NM");
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::DeviceNotAvailable,
        "FXRSTOR CR0.TS must produce #NM before validating the image");
  cpu.cr0=0x14; // EM set, TS clear.
  for(std::uint8_t form:{0x06,0x0e})
    check(execute(cpu,{0x0f,0xae,form,0x00,0x40})==StepResult::Faulted&&
          cpu.fault->code==cosmic_pc::FaultCode::DeviceNotAvailable,
          "Both FX instructions must signal #NM with CR0.EM");
  cpu.cr0=0x10;
  for(const auto modrm:{0xc0,0xc8})
    check(execute(cpu,{0x0f,0xae,std::uint8_t(modrm)})==StepResult::Faulted&&
          cpu.fault->code==cosmic_pc::FaultCode::InvalidInstruction,
          "FX state instructions require a memory operand");
  check(execute(cpu,{0xf0,0x0f,0xae,0x06,0x00,0x40})==StepResult::Faulted&&
        cpu.fault->code==cosmic_pc::FaultCode::InvalidInstruction,
        "LOCK FXSAVE must signal #UD");
  cpu.cr0&=~8U;cpu.cr4=0;
  check(execute(cpu,{0x66,0x0f,0x6f,0x06,0x00,0x40})==StepResult::Faulted,
        "SSE2 cannot operate without CR4.OSFXSR");
  std::cout<<"C09R1_FXSTATE_FAULTS_PASS alignment=1 mxcsr=1 nm=1\n";
}
void fxstate_exception_image() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;cpu.cr0|=0x20;
  for(const bool pending:{false,true}) {
    const auto control=std::uint16_t(pending?0:0xffffU);
    const auto status=std::uint16_t(pending?4:0x8080U);
    const auto expected_control=std::uint16_t(pending?0x0040U:0x1f7fU);
    const auto expected_status=std::uint16_t(pending?0x8084U:0);
    cpu.memory.fill(0x4000,0,512);
    cpu.write16(0x4000,control);cpu.write16(0x4002,status);cpu.write16(0x4006,0xfabc);
    cpu.write32(0x4018,0x1f80);
    check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x40})==StepResult::Executed,
          "FXRSTOR must load an image with pending unmasked x87 status");
    check(execute(cpu,{0x0f,0xae,0x06,0x00,0x41})==StepResult::Executed,
          "FXSAVE must save an image with pending unmasked x87 status");
    check(cpu.read16(0x4100)==expected_control&&cpu.read16(0x4102)==expected_status&&
          cpu.read16(0x4106)==0x02bc,
          "FXRSTOR failed to normalize reserved bits and derive x87 exception summary");
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    alignas(16) std::array<std::uint8_t,512> previous{},source{},observed{};
    cpu.memory.read_block(0x4000,source.data(),source.size());
    asm volatile("fxsave64 %[old]; fxrstor64 %[src]; fxsave64 %[out]; fnclex; fxrstor64 %[old]"
        : [old] "+m"(previous),[out] "=m"(observed) : [src] "m"(source) : "memory");
    for(unsigned i:{0U,1U,2U,3U})
      check(cpu.read8(0x4100+i)==observed[i],"FXRSTOR control/status normalization differs from hardware");
    if(pending)
      check(cpu.read16(0x4106)==(std::uint16_t(observed[7])<<8|observed[6]),
            "FXRSTOR exception opcode normalization differs from hardware");
#endif
    if(pending)
      check(execute(cpu,{0x9b})==StepResult::Faulted&&
            cpu.fault->code==cosmic_pc::FaultCode::X87FloatingPoint,
            "FWAIT must deliver pending x87 exception established by FXRSTOR");
    else
      check(execute(cpu,{0x9b})==StepResult::Executed,"FWAIT must ignore stale exception summary bits");
  }
}
void fpu_memory_and_stack() {
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;
  cpu.write32(0x5000,0x3fc00000U); // float32 +1.5
  cpu.write64(0x5010,0x4000000000000000ULL); // float64 +2.0
  check(execute(cpu,{0xd9,0x06,0x00,0x50})==StepResult::Executed,
        "FLD m32real failed");
  check(execute(cpu,{0xdd,0x06,0x10,0x50})==StepResult::Executed,
        "FLD m64real failed");
  check(execute(cpu,{0xd8,0xc1})==StepResult::Executed,
        "FADD ST0,ST1 failed");
  check(execute(cpu,{0xdd,0x1e,0x20,0x50})==StepResult::Executed,
        "FSTP m64real of exact sum failed");
  check(cpu.read64(0x5020)==0x400c000000000000ULL,
        "FPU exact 2.0 + 1.5 must store 3.5");
  check(execute(cpu,{0xd9,0x1e,0x30,0x50})==StepResult::Executed,
        "FSTP m32real failed");
  check(cpu.read32(0x5030)==0x3fc00000U,
        "FLD/FSTP m32real did not preserve 1.5");
  check(cpu.fpu_tags==0xffff,
        "x87 stack not empty after two FSTPs");
  check(execute(cpu,{0xdf,0xe0})==StepResult::Executed &&
        cpu.reg16(Cpu::EAX)==0,
        "FNSTSW AX incorrect after x87 operations");
  std::cout<<"C09R1_X87_REAL_MEMORY_ARITHMETIC_PASS add_1_5_plus_2=3_5\n";
}
void long64_fxstate(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  const auto image=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,image.data(),image.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned n=0;n<12000;++n){
    const auto result=cpu.step();
    if(result==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(result==StepResult::Halted&&cpu.is_long_mode())break;
  }
  check(cpu.is_long_mode(),"FXSAVE64 guest did not reach long64 mode");
  cpu.cr4|=0x200;
  cpu.mxcsr=0x1f80;
  for(unsigned i=0;i<16;++i){
    cpu.xmm[0][i]=std::uint8_t(i+3);
    cpu.xmm[15][i]=std::uint8_t(i+0x40);
  }
  cpu.fpu_ip=0x00007fff12345678ULL;cpu.fpu_dp=0x00007fff98765430ULL;
  const std::array<std::uint8_t,9> save{{
    0x48,0x0f,0xae,0x04,0x25,0x00,0x40,0,0 // FXSAVE64 [0x4000]
  }};
  cpu.memory.write_block(0xa000,save.data(),save.size());
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  check(cpu.step()==StepResult::Executed,
        "64-bit REX.W FXSAVE failed");
  check(cpu.read64(0x4008)==cpu.fpu_ip&&
        cpu.read64(0x4010)==cpu.fpu_dp,
        "FXSAVE64 did not save 64-bit FIP/FDP");
  for(unsigned i=0;i<16;++i)
    check(cpu.memory[0x4000+400+i]==std::uint8_t(0x40+i),
          "FXSAVE64 did not save XMM15");
  cpu.xmm[0].fill(0);cpu.xmm[15].fill(0);cpu.mxcsr=0;
  cpu.fpu_ip=0;cpu.fpu_dp=0;
  const std::array<std::uint8_t,9> restore{{
    0x48,0x0f,0xae,0x0c,0x25,0x00,0x40,0,0 // FXRSTOR64 [0x4000]
  }};
  cpu.memory.write_block(0xa000,restore.data(),restore.size());
  cpu.eip=0xa000;
  check(cpu.step()==StepResult::Executed&&cpu.mxcsr==0x1f80,
        "64-bit FXRSTOR did not recover SSE context");
  check(cpu.fpu_ip==0x00007fff12345678ULL&&
        cpu.fpu_dp==0x00007fff98765430ULL,
        "FXRSTOR64 lost 64-bit FIP/FDP");
  for(unsigned i=0;i<16;++i)
    check(cpu.xmm[0][i]==std::uint8_t(i+3)&&
          cpu.xmm[15][i]==std::uint8_t(i+0x40),
          "FXRSTOR64 failed XMM0/XMM15 state restore");
  const auto run64=[&](std::initializer_list<std::uint8_t> code) {
    cpu.memory.write_block(0xa000,code.begin(),code.size());
    cpu.eip=0xa000;cpu.fault.reset();cpu.halted=false;
    return cpu.step();
  };
  const auto legacy=std::initializer_list<std::uint8_t>{0x0f,0xae,0x04,0x25,0,0x60,0,0};
  check(run64(legacy)==StepResult::Executed&&
        cpu.read32(0x6008)==std::uint32_t(cpu.fpu_ip)&&
        cpu.read32(0x6010)==std::uint32_t(cpu.fpu_dp)&&cpu.read8(0x6000+415)==0x4f,
        "Unpromoted FXSAVE in long mode must save 32-bit pointers and all sixteen XMMs");

  // The chosen OSFXSR-clear behavior leaves the SIMD portion untouched in
  // both long-mode formats, even if the image contains invalid MXCSR bits.
  cpu.cr4&=~0x200U;
  cpu.fpu_cs=0x33;cpu.fpu_ds=0x2b;cpu.fpu_control=0x077f;
  cpu.memory.fill(0x6000,0xa5,512);
  cpu.memory.fill(0x6200,0xa5,512);
  check(run64(legacy)==StepResult::Executed&&
        cpu.read32(0x6008)==std::uint32_t(cpu.fpu_ip)&&cpu.read16(0x600c)==0x33&&
        cpu.read32(0x6010)==std::uint32_t(cpu.fpu_dp)&&cpu.read16(0x6014)==0x2b,
        "Unpromoted FXSAVE with OSFXSR clear must retain x87 offsets and selectors");
  check(run64({0x48,0x0f,0xae,0x04,0x25,0x00,0x62,0,0})==StepResult::Executed&&
        cpu.read64(0x6208)==cpu.fpu_ip&&cpu.read64(0x6210)==cpu.fpu_dp,
        "FXSAVE64 with OSFXSR clear must retain 64-bit x87 pointers");
  for(const unsigned area:{0x6000U,0x6200U}) {
    check(cpu.read64(area+24)==0xa5a5a5a5a5a5a5a5ULL,
          "FXSAVE with OSFXSR clear changed the MXCSR image");
    for(unsigned offset=160;offset<416;offset+=8)
      check(cpu.read64(area+offset)==0xa5a5a5a5a5a5a5a5ULL,
            "FXSAVE with OSFXSR clear changed the XMM image");
  }
  const auto simd_before=cpu.xmm;
  cpu.fpu_ip=0;cpu.fpu_dp=0;cpu.fpu_control=0x037f;cpu.mxcsr=0x9fc0;
  check(run64({0x48,0x0f,0xae,0x0c,0x25,0x00,0x62,0,0})==StepResult::Executed&&
        cpu.fpu_ip==0x00007fff12345678ULL&&cpu.fpu_dp==0x00007fff98765430ULL&&
        cpu.fpu_control==0x077f&&cpu.xmm==simd_before&&cpu.mxcsr==0x9fc0,
        "FXRSTOR64 with OSFXSR clear must restore x87 and ignore invalid SIMD state");
  check(run64({0x0f,0xae,0x0c,0x25,0x00,0x60,0,0})==StepResult::Executed&&
        cpu.fpu_ip==0x12345678ULL&&cpu.fpu_dp==0x98765430ULL&&
        cpu.fpu_cs==0x33&&cpu.fpu_ds==0x2b&&cpu.xmm==simd_before&&cpu.mxcsr==0x9fc0,
        "Unpromoted FXRSTOR with OSFXSR clear must restore x87 state alone");
  cpu.cr4|=0x200U;
  std::cout<<"C02_LONG_FXSTATE_OSFXSR_CLEAR_PASS rex_w_0_1=1 simd_preserved=1\n";

  // At offset E30, only the software-owned 48-byte tail crosses the page.
  for(unsigned i=0;i<512;++i)cpu.write64(0x4000+i*8,(std::uint64_t(i)<<12)|3);
  cpu.write64(0x3000,0x4003);
  cpu.write64(0x4000+15*8,0x12003);
  cpu.write64(0x4000+16*8,0);
  cpu.idtr={0,0};
  cpu.memory.fill(0x12e30,0x55,464);
  const auto bad_save=std::initializer_list<std::uint8_t>{0x0f,0xae,0x04,0x25,0x30,0xfe,0,0};
  const auto bad_restore=std::initializer_list<std::uint8_t>{0x0f,0xae,0x0c,0x25,0x30,0xfe,0,0};
  check(run64(bad_save)==StepResult::Faulted&&cpu.fault->code==cosmic_pc::FaultCode::PageFault&&
        cpu.cr2==0x10000&&cpu.fault->error_code==2&&cpu.read8(0x12e30)==0x55,
        "Second page check on FX save must be write-sense and precede RAM mutation");
  const auto xmm_before=cpu.xmm;
  check(run64(bad_restore)==StepResult::Faulted&&cpu.fault->code==cosmic_pc::FaultCode::PageFault&&
        cpu.cr2==0x10000&&cpu.fault->error_code==0&&cpu.xmm==xmm_before,
        "Second page check on FX restore must be read-sense and leave XMM unchanged");
  cpu.write64(0x4000+16*8,0x15003);
  cpu.memory.fill(0x15000,0x77,64);
  const auto walks=cpu.page_walks;
  check(run64({0x0f,0xae,0x04,0x25,0x40,0xfe,0,0})==StepResult::Executed&&
        cpu.page_walks-walks==legacy.size()+2&&cpu.read16(0x12e40)==cpu.fpu_control&&
        cpu.read8(0x15000+15)==0&&
        cpu.read8(0x15000+16)==0x77&&cpu.read8(0x15000+63)==0x77,
        "Cross-page FX save must use two data walks and leave software-owned bytes untouched");
  std::cout<<"C09R1_FXSAVE64_XMM15_CONTEXT_PASS xmm16=1\n";
}
void two_contexts_fxstate(){
  Cpu cpu(2*1024*1024);
  cpu.reset(0,0x1000);cpu.a20_enabled=true;cpu.cr4=0x200;
  cpu.mxcsr=0x1f80;
  cpu.xmm[0].fill(0x11);
  check(execute(cpu,{0xd9,0xe8})==StepResult::Executed,
        "context A FLD1 failed");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x60})==StepResult::Executed,
        "context A FXSAVE failed");
  check(execute(cpu,{0xdb,0xe3})==StepResult::Executed,
        "context B FNINIT failed");
  cpu.xmm[0].fill(0x22);
  cpu.mxcsr=0x3f80; // distinct legal MXCSR rounding control
  check(execute(cpu,{0xd9,0xee})==StepResult::Executed,
        "context B FLDZ failed");
  check(execute(cpu,{0x0f,0xae,0x06,0x01,0x62})==StepResult::Faulted,
        "misaligned context B save should fault");
  check(execute(cpu,{0x0f,0xae,0x06,0x00,0x70})==StepResult::Executed,
        "context B FXSAVE failed");
  cpu.xmm[0].fill(0xff);cpu.mxcsr=0;
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x60})==StepResult::Executed,
        "context A FXRSTOR failed");
  check(cpu.xmm[0][0]==0x11&&cpu.mxcsr==0x1f80 &&
        ((cpu.fpu_tags>>14)&3)==0,
        "restored A got another context XMM/MXCSR/x87");
  check(execute(cpu,{0x0f,0xae,0x0e,0x00,0x70})==StepResult::Executed,
        "context B FXRSTOR failed");
  check(cpu.xmm[0][0]==0x22&&cpu.mxcsr==0x3f80 &&
        ((cpu.fpu_tags>>14)&3)==1,
        "restored B got another context XMM/MXCSR/x87");
  std::cout<<"C09R1_FXSAVE_CONTEXT_A_B_ISOLATED_PASS two_contexts=1\n";
}
void nm_guest_handler_recovery(){
  Cpu cpu(64ULL*1024*1024,16ULL*1024*1024);
  const auto guest=cosmic_pc::long_mode_boot_disk();
  cpu.reset();cpu.memory.write_block(0x7c00,guest.data(),guest.size());
  cpu.interrupt_hook=[](Cpu& c,std::uint8_t n){
    if(n!=0x15||c.reg16(Cpu::EAX)!=0x2401)c.fail("unexpected A20 service");
    c.a20_enabled=true;c.set_reg8(4,0);c.set_flag(Cpu::CF,false);return true;
  };
  for(unsigned n=0;n<12000;++n){
    auto r=cpu.step();
    if(r==StepResult::Faulted)throw std::runtime_error(cpu.fault->describe());
    if(r==StepResult::Halted&&cpu.is_long_mode())break;
  }
  check(cpu.is_long_mode(),"NM guest not in long64 mode");
  cpu.cr4|=0x200;cpu.cr0|=0x8;
  cpu.regs[Cpu::ESP]=0x190000;
  cpu.idtr={0x1000,0x15000};
  constexpr std::uint64_t target=0x13000;
  const auto low=(target&0xffffULL)|(std::uint64_t(0x18)<<16)|
                 (std::uint64_t(0x8e)<<40)|((target>>16)<<48);
  cpu.write64(cpu.idtr.base+7*16,low);
  cpu.write64(cpu.idtr.base+7*16+8,0);
  // Guest #NM handler executes CLTS; IRETQ retries the exact FXSAVE64.
  const std::array<std::uint8_t,3> handler{{0x0f,0x06,0xcf}};
  cpu.memory.write_block(target,handler.data(),handler.size());
  const std::array<std::uint8_t,10> main{{
     0x48,0x0f,0xae,0x04,0x25,0x00,0x40,0,0,0xf4
  }};
  cpu.memory.write_block(0xa000,main.data(),main.size());
  cpu.eip=0xa000;cpu.halted=false;cpu.fault.reset();
  check(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==target,
        "FXSAVE CR0.TS did not deliver guest #NM vector7");
  check(cpu.regs[Cpu::ESP]==0x190000-40,
        "guest #NM must stack 64-bit no-error-code frame");
  check(cpu.step()==StepResult::Executed && !(cpu.cr0&8),
        "real guest CLTS failed to clear task-switched flag");
  check(cpu.step()==StepResult::Executed&&cpu.eip==0xa000,
        "IRETQ failed to restart exact FXSAVE64 after #NM");
  check(cpu.step()==StepResult::Executed&&!cpu.fault&&cpu.eip==0xa009,
        "guest FXSAVE64 failed after #NM handler");
  check(cpu.read16(0x4000)==0x037f && cpu.regs[Cpu::ESP]==0x190000,
        "retried FXSAVE failed state or lost guest stack");
  check(cpu.step()==StepResult::Halted,
        "real guest did not halt after completed FXSAVE64");
  std::cout<<"C09R1_GUEST_NM_CLTS_IRETQ_RETRY_PASS vector=7\n";
}
}
int main(){
  try{
    real16_fxstate();fxstate_failures();fxstate_exception_image();fpu_memory_and_stack();long64_fxstate();
    two_contexts_fxstate();nm_guest_handler_recovery();
    std::cout<<"REVERSE_WINDOWS_C09R1_FPU_FXSTATE_PASS checks="<<checks
             <<" windows_executed=false\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"REVERSE_WINDOWS_C09R1_FPU_FXSTATE_FAIL "<<e.what()<<"\n";return 1;
  }
}
