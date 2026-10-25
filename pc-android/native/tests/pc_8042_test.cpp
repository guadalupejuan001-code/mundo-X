#include "../pc_8042.hpp"
#include "../pc_machine.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::Pc8042;
using cosmic_pc::PcMachine;
using cosmic_pc::StepResult;
using Bytes=std::vector<std::uint8_t>;

unsigned checks=0;
void check(bool good,const std::string& why){
  ++checks;
  if(!good)throw std::runtime_error(why);
}
void put16(Bytes& b,std::size_t offset,std::uint16_t value){
  b.at(offset)=std::uint8_t(value);
  b.at(offset+1)=std::uint8_t(value>>8);
}
void put32(Bytes& b,std::size_t offset,std::uint32_t value){
  for(unsigned i=0;i<4;++i)b.at(offset+i)=std::uint8_t(value>>(i*8));
}

void controller_contract(){
  bool a20=false;
  Pc8042 kbc;
  check(kbc.input(0x64,1,a20)==4u,"8042 controller boot POST status");
  check(!kbc.input(0x64,2,a20).has_value(),"no invented 16-bit port access");
  check(!kbc.output(0x64,2,0xd1,a20),"8042 rejects 16-bit commands");
  check(kbc.output(0x64,1,0xd1,a20),"D1 command to output port");
  check(kbc.pending()==Pc8042::Pending::OutputPort,"D1 queues next data byte");
  check(!kbc.output(0x64,1,0xae,a20),"next command cannot discard pending D1");
  check(kbc.input(0x64,1,a20).value() & 0x08u,"status has command/data marker");
  check(kbc.output(0x60,1,0xdf,a20),"D1 payload sent to data port");
  check(a20 && kbc.output_port(a20)==0xdf,"A20 enabled, output port latch preserved");
  check(kbc.pending()==Pc8042::Pending::None,"D1 consumed exact one payload");
  check((kbc.input(0x64,1,a20).value()&0x02u)==0,"synchronous IBF never stuck busy");
  check(kbc.output(0x64,1,0xd0,a20),"D0 read output port");
  check((kbc.input(0x64,1,a20).value()&1)!=0,"output buffer full after D0");
  check(kbc.input(0x60,1,a20)==0xdfu,"D0 returns correct A20 output port");
  check((kbc.input(0x64,1,a20).value()&1)==0,"reading data drains buffer");
  check(kbc.input(0x60,1,a20)==0xffu,"empty data port is marked open-bus");
  check(kbc.output(0x64,1,0xd1,a20),"D1 repeat for A20 disable");
  check(!kbc.output(0x60,1,0xdc,a20),"output port reset bit cannot be silently accepted");
  check(a20&&kbc.pending()==Pc8042::Pending::OutputPort,
        "invalid reset request preserves A20 and input transaction");
  check(kbc.output(0x60,1,0xddu,a20),"D1 output port valid write");
  check(!a20,"8042 D1 must disable A20 if bit 1 is clear");
  // Fast A20 via port 0x92 changes the same authoritative CPU flag:
  // 8042 D0 must observe it, not a stale duplicate latch.
  a20=true;
  check(kbc.output(0x64,1,0xd0,a20),"D0 after fast-gate A20");
  check(kbc.input(0x60,1,a20)==0xdfu,"D0 reflects external fast A20 change");
  check(kbc.output(0x64,1,0x20,a20),"read controller config");
  check(kbc.input(0x60,1,a20)==4u,"configuration starts with POST bit");
  check(kbc.output(0x64,1,0x60,a20),"write controller config");
  check(kbc.pending()==Pc8042::Pending::ConfigByte,"configuration payload pending");
  check(kbc.output(0x60,1,0x54,a20),"config write payload");
  check(kbc.config_byte()==0x54&&kbc.keyboard_disabled(),"keyboard clock masked by config");
  check(kbc.output(0x64,1,0xae,a20),"enable keyboard clock");
  check(!kbc.keyboard_disabled(),"enable keyboard clock cleared mask");
  check(kbc.output(0x64,1,0xad,a20),"disable keyboard clock");
  check(kbc.keyboard_disabled(),"disable keyboard clock set mask");
  check(kbc.output(0x64,1,0xa7,a20),"mask absent second-port clock");
  check(kbc.output(0x64,1,0xaa,a20),"8042 self test");
  // 0xFF is the all-one mask of the 0xF0..0xFF pulse family.
  // It pulses no output line, must not reset the CPU, clear A20,
  // overwrite a pending reply, or ACK an imaginary keyboard.
  const auto config_before_pulse=kbc.config_byte();
  const auto port_before_pulse=kbc.output_port(a20);
  const bool a20_before_pulse=a20;
  const auto commands_before_pulse=kbc.metrics().command_writes;
  check(kbc.has_reply(),"AA queued self-test response before FF");
  check(kbc.output(0x64,1,0xff,a20),"8042 FF no-pulse command supported");
  check(kbc.metrics().command_writes==commands_before_pulse+1 &&
        kbc.metrics().last_command==0xff,
        "FF counted as a real command, not a fabricated device reply");
  check(a20==a20_before_pulse &&
        kbc.config_byte()==config_before_pulse &&
        kbc.output_port(a20)==port_before_pulse,
        "FF must not alter A20, controller config or output port");
  check(kbc.has_reply() &&
        kbc.pending()==Pc8042::Pending::None,
        "FF must preserve queued 8042 response and not create a transaction");
  check(kbc.input(0x60,1,a20)==0x55u,"FF must preserve previous self-test reply");
  check(!kbc.has_reply(),"FF itself has no reply");
  check(kbc.output(0x64,1,0xab,a20),"8042 first port test");
  check(kbc.input(0x60,1,a20)==0u,"first port test response 00");
  check(!kbc.output(0x64,1,0xfe,a20),"CPU reset request must remain unsupported");
  check(!kbc.output(0x64,1,0xf0,a20),"pulse reset/output lines F0 must not be faked");
  check(!kbc.output(0x64,1,0xf1,a20),"pulse output lines F1 must not be faked");
  check(!kbc.output(0x60,1,0xff,a20),"keyboard RESET to port 60 must stay unsupported");
  check(kbc.output(0x64,1,0xd1,a20),"queue D1 before FF conflict");
  check(!kbc.output(0x64,1,0xff,a20),
        "FF cannot silently discard a pending D1 output-port payload");
  check(kbc.pending()==Pc8042::Pending::OutputPort,
        "rejected FF preserves incomplete D1 transaction");
  check(kbc.output(0x60,1,0xdf,a20),"finish interrupted D1 successfully");
  check(!kbc.output(0x64,1,0xd4,a20),"second-device payload cannot invent a mouse");
  check(!kbc.output(0x60,1,0xff,a20),"keyboard IRQ/reset command cannot be faked");
  check(kbc.metrics().command_writes>=10,"metrics record implemented commands");
  kbc.reset();
  check(!kbc.has_reply()&&kbc.pending()==Pc8042::Pending::None,"reset clears buffers");
  check(kbc.config_byte()==4&&kbc.metrics().command_writes==0,"reset clears registers/metrics");
}

Bytes el_torito_fixture(){
  constexpr std::size_t sector=2048;
  Bytes disk(32*sector,0);
  auto descriptor=[&](unsigned lba,unsigned type){
    std::size_t index=lba*sector;
    disk[index]=std::uint8_t(type);
    std::memcpy(disk.data()+index+1,"CD001",5);
    disk[index+6]=1;
  };
  descriptor(16,1);descriptor(17,0);descriptor(18,255);
  std::memcpy(disk.data()+17*sector+7,"EL TORITO SPECIFICATION",23);
  put32(disk,17*sector+0x47,20);
  const auto catalog=20*sector;
  disk[catalog]=1;disk[catalog+1]=0;
  disk[catalog+30]=0x55;disk[catalog+31]=0xaa;
  disk[catalog+32]=0x88;disk[catalog+33]=0;
  put16(disk,catalog+34,0x2000); // CS must be 2000, unlike default 07C0.
  put16(disk,catalog+38,8);      // Eight 512-byte El Torito boot sectors.
  put32(disk,catalog+40,24);
  unsigned sum=0;
  for(unsigned i=0;i<32;i+=2)
    sum=(sum+unsigned(disk[catalog+i])+
        (unsigned(disk[catalog+i+1])<<8)) & 0xffffu;
  put16(disk,catalog+28,std::uint16_t(0u-sum));

  const std::size_t start=24*sector;
  // Stage1 BIOS guest reproduces BOTH user-observed fault addresses:
  // CS:IP 2000:0ACE -> E6 64, with AL=D1 (8042 output-port command);
  // CS:IP 2000:0ADC -> E6 64, with AL=FF (8042 no-line-pulse command).
  // In between: write DF to 0x60 to enable A20; query controller status;
  // reject busy-bit conflicts by branching to UD2 only if bit 1 is set.
  disk[start+0]=0xb0;disk[start+1]=0xd1;
  disk[start+2]=0xe9;disk[start+3]=0xc9;disk[start+4]=0x0a;
  constexpr std::size_t ip=0x0ace;
  const std::uint8_t bytes[]={
    0xe6,0x64,  // 0ACE: OUT 0x64, AL (D1)
    0xb0,0xdf,  // 0AD0: MOV AL,DF
    0xe6,0x60,  // 0AD2: OUT 0x60, AL (DF -> A20 on)
    0xe4,0x64,  // 0AD4: IN AL,0x64
    0xa8,0x02,  // 0AD6: TEST AL,02
    0x75,0x08,  // 0AD8: JNZ 0AE2 -> UD2 (must not occur)
    0xb0,0xff,  // 0ADA: MOV AL,FF
    0xe6,0x64,  // 0ADC: OUT 0x64, AL (FF no-line pulse)
    0xf4,       // 0ADE: HLT
    0x90,0x90,0x90, // 0ADF..0AE1 padding
    0x0f,0x0b   // 0AE2: UD2 for bad busy flag
  };
  std::copy(std::begin(bytes),std::end(bytes),disk.begin()+start+ip);
  return disk;
}
void real_iso_guest(){
  PcMachine pc(4*1024*1024);
  auto disk=std::make_shared<Bytes>(el_torito_fixture());
  pc.mount([disk](std::uint64_t at,std::uint8_t* out,std::size_t len){
    if(at>disk->size()||len>disk->size()-at)return false;
    std::memcpy(out,disk->data()+at,len);
    return true;
  },disk->size(),"iso");
  check(pc.boot_kind=="iso"&&pc.cpu.segments[Cpu::CS]==0x2000
        &&pc.cpu.eip==0,"real ISO must use non-default CS:IP");
  pc.cpu.a20_enabled=false; // Bootloader must use the D1/DF sequence itself.
  check(pc.cpu.step()==StepResult::Executed,"guest MOV AL,D1");
  check(pc.cpu.step()==StepResult::Executed&&pc.cpu.eip==0x0ace,
        "guest must reach screenshot 2000:0ACE through real jump");
  check(pc.cpu.step()==StepResult::Executed&&!pc.cpu.fault,
        "original E6 64 OUT must now be accepted at exact CS:IP");
  check(pc.cpu.eip==0x0ad0,"OUT 0x64 advances IP by two bytes");
  check(pc.json().find("\"last_command\":209")!=std::string::npos,
        "D1 read from guest code recorded in structured diagnostics");
  check(pc.json().find("\"pending_payload\":2")!=std::string::npos,
        "controller awaits D1 output-port payload");
  // Six more decoded steps after E6 64 at 0ACE reach the actual 0ADC
  // address without modifying the original ISO fixture's entry point.
  pc.run(6);
  check(!pc.cpu.fault && pc.cpu.eip==0x0adc &&
        pc.cpu.reg8(Cpu::EAX)==0xff,
        "guest must reach second user fault: 2000:0ADC with AL=FF");
  check(pc.cpu.a20_enabled &&
        pc.json().find("\"last_command\":209")!=std::string::npos,
        "D1/DF sequence must already have changed A20 before 0ADC");
  const auto steps_before_ff=pc.cpu.instructions;
  check(pc.cpu.step()==StepResult::Executed && pc.cpu.eip==0x0ade &&
        pc.cpu.instructions==steps_before_ff+1 && !pc.cpu.fault,
        "real guest E6 64 with AL=FF executes and advances to 0ADE");
  check(pc.cpu.a20_enabled && pc.json().find("\"last_command\":255")!=std::string::npos,
        "FF leaves A20 enabled and records last command 255");
  check(pc.json().find("\"command_writes\":2")!=std::string::npos,
        "controller accounts for D1 and FF, not unimplemented device commands");
  pc.run(100);
  check(pc.cpu.halted&&!pc.cpu.fault,"real guest passed both fault addresses and reached HLT");
  check(pc.cpu.a20_enabled,"D1 and DF actually changed CPU A20 gate");
  check(pc.json().find("\"a20_enabled\":true")!=std::string::npos,
        "A20 state reported in machine JSON");
  check(pc.cpu.instructions>=11,"guest executed D1, DF, FF and HLT as real CPU instructions");
  check(pc.json().find("\"windows_ready\":false")!=std::string::npos,
        "controller support does not imply Windows 10 runs");
  pc.reset();
  check(pc.status()=="ready"&&pc.cpu.instructions==0,"reset rearms ISO");
  check(pc.json().find("\"command_writes\":0")!=std::string::npos,
        "reset clears 8042 counters and latches");

  // Unsupported commands still halt at their exact faulting instruction.
  Bytes raw(512,0);
  raw[0]=0xb0;raw[1]=0xfe;raw[2]=0xe6;raw[3]=0x64;raw[4]=0xf4;
  raw[510]=0x55;raw[511]=0xaa;
  auto p=std::make_shared<Bytes>(std::move(raw));
  pc.mount([p](std::uint64_t at,std::uint8_t* out,std::size_t len){
    if(at>p->size()||len>p->size()-at)return false;
    std::memcpy(out,p->data()+at,len);return true;
  },p->size(),"raw");
  pc.run(100);
  check(bool(pc.cpu.fault) && pc.cpu.fault->code==cosmic_pc::FaultCode::UnsupportedPort,
        "unsupported reset command remains explicit fault");
  check(pc.cpu.eip==0x7c02&&pc.cpu.fault->eip==0x7c02,
        "port fault preserves source instruction IP");
  check(pc.cpu.fault->reason.find("valor=0xfe")!=std::string::npos,
        "unknown 8042 command byte is visible for future ISO debugging");
}
}
int main(){
 try{
   controller_contract();
   real_iso_guest();
   std::cout<<"PC_8042_ISO_PASS checks="<<checks
            <<" guest_csip_D1=2000:0ACE"
            <<" guest_csip_FF=2000:0ADC"
            <<" command_FF=0x64 no_pulse=1"
            <<" payload_DF=0x60 a20=true windows_ready=false\n";
   return 0;
 }catch(const std::exception& e){
   std::cerr<<"PC_8042_ISO_FAIL "<<e.what()<<"\n";
   return 1;
 }
}
