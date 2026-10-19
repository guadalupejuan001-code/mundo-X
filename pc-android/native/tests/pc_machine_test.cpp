#include "../pc_machine.hpp"
#include <iostream>
#include <stdexcept>
namespace {
unsigned checks=0;
void need(bool b,const char* reason){++checks;if(!b)throw std::runtime_error(reason);}
void drain(cosmic_pc::PcMachine& pc){for(unsigned i=0;i<1000&&!pc.cpu.halted&&!pc.cpu.fault&&!pc.waiting_key;++i)pc.run(1000);}
void mount(cosmic_pc::PcMachine& pc,std::vector<std::uint8_t> b){auto p=std::make_shared<std::vector<std::uint8_t>>(std::move(b));pc.mount([p](std::uint64_t o,std::uint8_t* d,std::size_t n){if(o>p->size()||n>p->size()-o)return false;std::copy_n(p->data()+o,n,d);return true;},p->size(),"raw");}
std::vector<std::uint8_t> boot(std::initializer_list<std::uint8_t> code){std::vector<std::uint8_t>b(512);std::copy(code.begin(),code.end(),b.begin());b[510]=0x55;b[511]=0xaa;return b;}
}
int main(){try{
  cosmic_pc::PcMachine pc;
  need(pc.status()=="empty","no medium is not runnable");pc.run(1000);need(pc.cpu.instructions==0,"empty machine does not execute");
  pc.reset();need(pc.status()=="empty","reset without a medium still asks for a boot source");
  pc.boot_demo();need(pc.status()=="ready","demo boot ready");need(pc.cpu.memory[0x7c00]==0xfa,"bootsector copied");
  need(pc.cpu.read8(0x475)==1,"raw hard disk in BDA");
  drain(pc);
  if(pc.cpu.fault)throw std::runtime_error(pc.cpu.fault->describe());
  need(pc.waiting_key,"BIOS keyboard waits");need(!pc.cpu.halted,"waiting is not halt");
  need(pc.screen().find("ALU + RAM + lectura de disco: OK")!=std::string::npos,"guest computed success");
  need(pc.cpu.read16(0x9000)==0x1336,"guest arithmetic stored in RAM");
  need(pc.cpu.read16(0x8000)==0x5843,"disk sector copied into guest RAM");need(pc.disk_reads==1,"one actual disk read");
  const auto count=pc.cpu.instructions;pc.run(1000);need(pc.cpu.instructions==count,"waiting does not spin");
  pc.key('K',0x25);drain(pc);need(pc.cpu.halted,"guest HLT");need(!pc.cpu.fault,"guest finished without fault");
  need(pc.screen().find("Pulsa una tecla: K")!=std::string::npos,"guest echoed key");need(pc.screen().find("Entrada BIOS: OK")!=std::string::npos,"guest returned from INT16");
  need(pc.json().find("\"windows_ready\":false")!=std::string::npos,"no Windows claim");
  const auto successful=pc.screen();pc.reset();drain(pc);pc.key('K',0x25);drain(pc);need(pc.screen()==successful,"reset deterministic screen");
  // Fault must identify unsupported execution, not fabricate a successful boot.
  mount(pc,boot({0x0f,0x0b}));drain(pc);need(bool(pc.cpu.fault),"UD2 stops");need(pc.status()=="fault","fault state");need(pc.cpu.eip==0x7c00,"fault address retained");
  // INT13 read beyond EOF reports BIOS CF and does not write the destination.
  mount(pc,boot({0xb8,0x01,0x02,0xbb,0x00,0x80,0xb9,0x02,0x00,0xba,0x80,0x00,0xcd,0x13,0xf4}));drain(pc);
  need(!pc.cpu.fault,"disk EOF is BIOS error");need((pc.cpu.eflags&1)!=0,"disk error carry");need(pc.cpu.read16(0x8000)==0,"failed read leaves destination");
  // E820 memory map: guest requests SMAP using actual 32-bit instruction prefix.
  mount(pc,boot({0x66,0xb8,0x20,0xe8,0,0,0x66,0xba,0x50,0x41,0x4d,0x53,0x66,0xb9,0x18,0,0,0,0x66,0x31,0xdb,0xbf,0x00,0x90,0xcd,0x15,0xf4}));drain(pc);
  need(!pc.cpu.fault,"E820 instruction stream");need(pc.cpu.regs[0]==0x534d4150,"E820 signature");need(pc.cpu.read32(0x9010)==1,"E820 available memory");need(pc.cpu.read32(0x9008)==0xa0000,"E820 conventional length");
  // BIOS text output is not hard-coded to the demo string.
  mount(pc,boot({0xb8,'Z',0x0e,0xcd,0x10,0xf4}));drain(pc);need(pc.screen()[0]=='Z',"arbitrary guest text");
  bool invalid=false;try{mount(pc,std::vector<std::uint8_t>(512));}catch(const std::runtime_error&){invalid=true;}need(invalid,"invalid boot rejected");need(pc.screen()[0]=='Z',"invalid mount preserves machine");
  mount(pc,boot({0xcd,0x11,0xf4}));drain(pc);need(pc.cpu.reg16(0)==pc.cpu.read16(0x410),"equipment call matches BDA");need((pc.cpu.reg16(0)&3)==0,"no advertised floppy or x87");
  std::cout<<"PC machine: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
