#include "../pc_machine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;
using cosmic_pc::StepResult;
using Bytes=std::vector<std::uint8_t>;
constexpr std::size_t Sector=2048;
unsigned checks=0;
void check(bool ok,const char* why){++checks;if(!ok)throw std::runtime_error(why);}
void write16(Bytes& v,std::size_t at,std::uint16_t x){v.at(at)=x&255;v.at(at+1)=x>>8;}
void write32(Bytes& v,std::size_t at,std::uint32_t x){for(unsigned i=0;i<4;++i)v.at(at+i)=std::uint8_t(x>>(8*i));}
void write64(Bytes& v,std::size_t at,std::uint64_t x){for(unsigned i=0;i<8;++i)v.at(at+i)=std::uint8_t(x>>(8*i));}
std::uint64_t desc(std::uint32_t base,std::uint32_t limit,unsigned access) {
    return (limit&0xffffu) | (std::uint64_t(base&0xffffffu)<<16) |
        (std::uint64_t(access)<<40) | (std::uint64_t((limit>>16)&15u)<<48) |
        (std::uint64_t((base>>24)&255u)<<56);
}
Bytes image(){
    Bytes disk(32*Sector,0);
    const auto descriptor=[&](unsigned lba,std::uint8_t type){
        const auto at=std::size_t(lba)*Sector;
        disk[at]=type;std::memcpy(disk.data()+at+1,"CD001",5);disk[at+6]=1;
    };
    descriptor(16,1);descriptor(17,0);descriptor(18,255);
    std::memcpy(disk.data()+17*Sector+7,"EL TORITO SPECIFICATION",23);
    write32(disk,17*Sector+0x47,20);
    const auto catalog=20*Sector;
    disk[catalog]=1;disk[catalog+1]=0;
    disk[catalog+30]=0x55;disk[catalog+31]=0xaa;
    disk[catalog+32]=0x88;
    write16(disk,catalog+34,0x2000); // BIOS boots CS:IP 2000:0000
    write16(disk,catalog+38,16);
    write32(disk,catalog+40,24);
    unsigned sum=0;
    for(unsigned i=0;i<32;i+=2)
        sum=(sum+unsigned(disk[catalog+i])+(unsigned(disk[catalog+i+1])<<8))&0xffffu;
    write16(disk,catalog+28,std::uint16_t(0u-sum));

    const auto boot=24*Sector;
    // In REAL mode: CLI; PUSH CS; POP DS; LGDT DS:[0200];
    // MOV EAX,CR0; OR AX,1; MOV CR0,EAX; MOV AX,0060;
    // FAR JMP 0050:0854 (16-bit protected code segment).
    const std::uint8_t stub[]={
        0xfa,0x0e,0x1f,
        0x0f,0x01,0x16,0x00,0x02,
        0x0f,0x20,0xc0,
        0x0d,0x01,0x00,
        0x0f,0x22,0xc0,
        0xb8,0x60,0x00,
        0xea,0x54,0x08,0x50,0x00
    };
    std::copy(std::begin(stub),std::end(stub),disk.begin()+boot);
    // GDTR base = 0x20280, limit = 0x6F
    write16(disk,boot+0x200,0x006f);
    write32(disk,boot+0x202,0x20280);
    // Code16 selector 0x50 has base 0x20000. Following instructions at
    // 0050:0854 will be read from the ISO image offset 0x0854.
    write64(disk,boot+0x280+0x50,desc(0x20000,0x1fff,0x9a));
    write64(disk,boot+0x280+0x58,desc(0x20000,0x1fff,0x92));
    write64(disk,boot+0x280+0x60,desc(0x25000,0x67,0x89)); // available 386 TSS
    write64(disk,boot+0x280+0x68,desc(0x21800,0x1f,0x82)); // LDT descriptor
    write64(disk,boot+0x1808,desc(0x20000,0x1fff,0x92)); // LDT selector TI=1, index=8
    const std::uint8_t guest[]={
        0x0f,0x00,0xd8, // 0050:0854 LTR AX; AX=0x60, marks TSS busy
        0x0f,0x00,0xc8, // 0050:0857 STR AX -> 0x60
        0xb8,0x58,0x00, // MOV AX,0x58
        0x0f,0x00,0xe0, // VERR AX (readable data -> ZF1)
        0x0f,0x00,0xe8, // VERW AX (writable data -> ZF1)
        0xb8,0x68,0x00, // MOV AX,0x68
        0x0f,0x00,0xd0, // LLDT AX, loads LDT
        0x0f,0x00,0xc0, // SLDT AX -> 0x68
        0xb8,0x0c,0x00, // MOV AX,0x0C (LDT selector)
        0x0f,0x00,0xe0, // VERR from LDT
        0xf4            // HLT
    };
    std::copy(std::begin(guest),std::end(guest),disk.begin()+boot+0x0854);
    return disk;
}
}
int main(){
 try {
    auto data=std::make_shared<Bytes>(image());
    std::uint64_t actual_reads=0;
    PcMachine pc(4*1024*1024);
    pc.mount([data,&actual_reads](std::uint64_t at,std::uint8_t* dst,std::size_t len){
        if(at>data->size()||len>data->size()-at)return false;
        std::memcpy(dst,data->data()+at,len);actual_reads+=len;return true;
    },data->size(),"iso");
    check(pc.boot_kind=="iso" && pc.cpu.eip==0 &&
          pc.cpu.segments[Cpu::CS]==0x2000,"ISO boots real 2000:0000");
    unsigned steps=0;
    while(!(pc.cpu.segments[Cpu::CS]==0x0050 && pc.cpu.eip==0x0854) &&
          steps<60 && !pc.cpu.fault && !pc.cpu.halted){
        check(pc.cpu.step()==StepResult::Executed,"real-mode stub fails before protected16");
        ++steps;
    }
    check(steps>5 && steps<60,"stub must enter protected mode after executing real instructions");
    check(std::string(pc.cpu.mode_name())=="protected16",
          "must reach Windows screenshot mode protected16");
    check(pc.cpu.segments[Cpu::CS]==0x50 && pc.cpu.eip==0x0854,
          "guest exactly reaches screenshot CS:IP 0050:0854");
    check(pc.cpu.reg16(Cpu::EAX)==0x60,"selector comes from executed real-mode MOV AX");
    const auto executed=pc.cpu.instructions;
    check(pc.cpu.step()==StepResult::Executed && !pc.cpu.fault,
          "0F 00 D8 LTR AX must execute inside ISO booted guest");
    check(pc.cpu.instructions==executed+1 && pc.cpu.eip==0x0857,
          "LTR advances 0F00 ModRM by three exact bytes");
    check(pc.cpu.tr.valid && pc.cpu.tr.selector==0x60 && pc.cpu.tr.base==0x25000,
          "protected16 TSS hidden descriptor correctly loaded");
    check(pc.cpu.read8(0x20280+0x60+5)==0x8b,
          "TSS descriptor busy bit set in memory written by actual ISA");
    pc.run(100);
    check(!pc.cpu.fault && pc.cpu.halted,"protected16 group6 ISO reaches HLT");
    check(pc.cpu.tr.selector==0x60 &&
          pc.cpu.ldtr.selector==0x68 && pc.cpu.ldtr.valid,
          "LTR and LLDT persist through real CPU guest instructions");
    check(pc.cpu.flag(Cpu::ZF),"VERR from LDT verified real data descriptor");
    check(pc.json().find("\"windows_ready\":false")!=std::string::npos,
          "no false Windows 10 ready claim");
    check(actual_reads<128*1024,"ISO image must be accessed in bounded reads");
    std::cout<<"WIN10_GROUP6_ISO_PROTECTED16_PASS checks="<<checks
             <<" target=0050:0854 actual_cpu=protected16"
             <<" LTR=0x60 TSS_busy=1 LDTR=0x68"
             <<" SLDT=1 STR=1 VERR=1 VERW=1"
             <<" windows_ready=false\n";
    return 0;
 } catch(const std::exception& e){
    std::cerr<<"WIN10_GROUP6_ISO_PROTECTED16_FAIL "<<e.what()<<"\n";
    return 1;
 }
}
