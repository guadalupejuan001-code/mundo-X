#include "../pc_machine.hpp"
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
unsigned checks=0;
void need(bool value,const char* reason){++checks;if(!value)throw std::runtime_error(reason);}
using Device=cosmic_pc::PcQuantumDevice;
using Machine=cosmic_pc::PcMachine;
using Session=mundo_x::InfiniteSession;
void drain(Machine& pc){
  for(unsigned i=0;i<1000&&!pc.cpu.fault&&!pc.cpu.halted&&!pc.waiting_key;++i)pc.run(5000);
  if(pc.cpu.fault)throw std::runtime_error(pc.cpu.fault->describe());
  need(pc.cpu.halted||pc.waiting_key,"guest must make bounded progress");
}
void mount(Machine& pc,std::vector<std::uint8_t> bytes){
  auto data=std::make_shared<std::vector<std::uint8_t>>(std::move(bytes));
  pc.mount([data](std::uint64_t off,std::uint8_t* dst,std::size_t n){
    if(off>data->size()||n>data->size()-off)return false;
    std::copy_n(data->data()+off,n,dst);return true;
  },data->size(),"raw");
}
void send(Device& device,const std::string& command){
  need(device.output(Device::control_port,1,Device::clear_channel),"clear port handled");
  for(unsigned char byte:command)device.output(Device::data_port,1,byte);
  need(device.output(Device::control_port,1,Device::submit_request),"submit port handled");
}
std::string receive(Device& device){
  std::string result;
  while(device.status()&Device::response_ready){
    const auto value=device.input(Device::data_port,1);
    need(bool(value),"response byte available");result+=char(*value);
    if(result.size()>16*1024*1024)throw std::runtime_error("unbounded response");
  }
  return result;
}
double expect(Machine& pc,const std::string& expression){
  return pc.quantum_session->query_batch({mundo_x::parse_observable(expression)}).values.at(0);
}
void guest_cases(){
  for(bool long_mode:{false,true})for(bool valid:{false,true}){
    Machine pc;
    mount(pc,cosmic_pc::quantum_boot_disk(long_mode,valid));
    need(pc.cpu.is_real_mode(),"both demos begin in real mode");
    drain(pc);
    need(pc.cpu.halted,"quantum guest executes HLT");
    need(pc.host_error.empty()&&!pc.cpu.fault,"quantum result never masquerades as CPU fault");
    need(pc.cpu.is_long_mode()==long_mode,"guest executed requested mode transition");
    need(pc.disk_reads==1,"stage two was read by guest BIOS call");
    need(pc.quantum_device.requests()==1,"guest submitted one real port request");
    need(pc.screen().find(valid?": OK":": ERROR")!=std::string::npos,"guest branches on device result");
    need(bool(pc.quantum_device.status()&Device::request_error)==!valid,"guest gets computed assertion status");
    need(expect(pc,"Z:0 Z:1000000000")==1,"actual ZZ correlation");
    need(expect(pc,"X:0 X:1000000000")==(valid?1:0),"failed whole program rolls gates back");
    need(expect(pc,"Y:0 Y:1000000000")==(valid?-1:0),"signed Y correlation survives guest transport");
    need(pc.quantum_session->stats().active_qubits==(valid?2u:0u),"only committed guest state materializes");
    const auto before=pc.quantum_session->snapshot();
    const auto image=pc.screen();const auto steps=pc.cpu.instructions;
    const auto pending=pc.quantum_device.remaining();
    const auto visible=pc.quantum_json();
    need(visible.find("\"labels\":[\"XX\",\"YY\",\"ZZ\",\"ZI\",\"IZ\"]")!=std::string::npos,"renderer receives labeled observables");
    need(pc.quantum_session->snapshot()==before,"panel observation is read-only");
    need(pc.screen()==image&&pc.cpu.instructions==steps,"panel preserves CPU and video state");
    need(pc.quantum_device.remaining()==pending,"panel leaves guest response untouched");
  }
}
void protocol_and_host_cases(){
  Machine pc;
  auto& device=pc.quantum_device;
  need(pc.status()=="empty","quantum device needs no boot medium");
  need(device.input(Device::control_port,1).value()==Device::present,"device can be discovered");
  need(!device.input(0x5852,1).has_value(),"unrelated port remains unsupported");
  need(!device.output(0x5852,1,0),"unrelated port untouched");
  const auto original=pc.quantum_session->snapshot();
  send(device,"h 0\ncx 0 1000000000\nassert expect 1 X:0 X:1000000000\nexpect Y:0 Y:1000000000\n");
  need(!(device.status()&Device::request_error),"valid request accepted");
  need(receive(device).find("-1")!=std::string::npos,"response derives signed expectation");
  need(device.input(Device::data_port,1).value()==0,"empty FIFO read defined");
  const auto committed=pc.quantum_session->snapshot();
  const auto revision=pc.quantum_session->stats().revision;
  send(device,"x 5\nassert expect 1 Z:5\n");
  need(device.status()&Device::request_error,"bad assertion returned in protocol");
  need(receive(device).find("\"ok\":false")!=std::string::npos,"protocol has structured error");
  need(pc.quantum_session->snapshot()==committed&&pc.quantum_session->stats().revision==revision,"failed request retains state and RNG");
  send(device,std::string("x 5\n")+std::string(1,'\0')+"stats\n");
  need(device.status()&Device::request_error,"NUL not silently truncated");
  need(pc.quantum_session->snapshot()==committed,"invalid bytes are atomic");
  send(device,"x 5\n");
  need(!(device.status()&Device::request_error)&&expect(pc,"Z:5")==-1,"next request recovers");
  device.output(Device::control_port,1,Device::clear_channel);
  const auto before_overflow=pc.quantum_session->snapshot();
  const std::string prefix="x 6\n";
  for(unsigned char byte:prefix)device.output(Device::data_port,1,byte);
  for(std::size_t i=prefix.size();i<=Device::max_request_bytes;++i)device.output(Device::data_port,1,' ');
  device.output(Device::control_port,1,Device::submit_request);
  need(device.status()&Device::request_overflow,"oversized request explicitly reported");
  need(pc.quantum_session->snapshot()==before_overflow,"overflow cannot execute valid prefix");
  device.output(Device::control_port,1,Device::clear_channel);
  need(device.status()==Device::present,"clear recovers transport without resetting core");
  need(device.output(Device::data_port,2,0x4142),"wrong width remains a device response");
  need(device.status()&Device::request_error,"wrong width reported");
  need(pc.quantum_session->snapshot()==before_overflow,"wrong width leaves core unchanged");
  device.output(Device::control_port,1,Device::clear_channel);
  for(unsigned char c:std::string("x 7\n"))device.output(Device::data_port,1,c);
  pc.quantum_command("x 8\n");
  pc.quantum_probe("0007","8");
  need(device.request_bytes()==4,"host leaves unfinished guest request alone");
  device.output(Device::control_port,1,Device::submit_request);
  need(expect(pc,"Z:7")==-1&&expect(pc,"Z:8")==-1,"host and guest share one session");
  pc.quantum_command("x -9\n");
  need(pc.status()=="empty"&&pc.host_error.empty()&&!pc.cpu.fault,"host quantum error does not fault PC");
  need(pc.quantum_json().find("\"error\":\"\"")==std::string::npos,"host error visible separately");
  pc.quantum_command("x 9\n");
  need(expect(pc,"Z:9")==-1,"host command recovers after validation error");
  const auto saved=pc.quantum_snapshot();
  const auto epoch=pc.quantum_session->epoch();
  pc.quantum_reset("bell");
  need(expect(pc,"Y:0 Y:1")==-1,"host reset sets rule background");
  pc.quantum_restore(saved);
  need(pc.quantum_session->epoch()>epoch,"restore invalidates observers by epoch");
  need(expect(pc,"X:0 X:1000000000")==1&&expect(pc,"Z:9")==-1,"snapshot restores state");
  const auto restored=pc.quantum_snapshot();
  pc.quantum_restore(saved+"corrupt");
  need(pc.quantum_snapshot()==restored,"invalid snapshot leaves live state intact");
  const std::string huge="1"+std::string(100,'0');
  pc.quantum_command("x "+huge+"\n");pc.quantum_probe(huge,"0");
  need(expect(pc,"Z:"+huge)==-1,"addresses over 64 bits stay exact strings");
  need(pc.quantum_json().find(huge)!=std::string::npos,"panel preserves decimal address");
  need(original!=pc.quantum_snapshot(),"work was performed on live state");
}
void ether_memory_cases(){
  Machine pc;
  auto& device=pc.quantum_device;
  send(device,"qload8 12345\n");
  auto output=receive(device);
  need(output.find("\"type\":\"qload8\"")!=std::string::npos&&
       output.find("\"value\":0")!=std::string::npos,
       "untouched Ether byte reads as zero");
  need(pc.quantum_session->stats().active_qubits==0,
       "zero-background qload must not materialize memory");

  send(device,"qstore8 12345 165\nqload8 12345\n");
  output=receive(device);
  need(!(device.status()&Device::request_error)&&output.find("\"value\":165")!=std::string::npos,
       "guest transport round-trips an Ether byte");
  need(pc.quantum_session->stats().active_qubits==4,
       "0xA5 materializes only its four one bits");

  send(device,"qstore8 12345 0\ncompact\nqload8 12345\n");
  output=receive(device);
  need(output.find("\"value\":0")!=std::string::npos,
       "Ether byte can return to its zero rule");
  need(pc.quantum_session->stats().active_qubits==0,
       "explicit compact releases bytes that returned exactly to the background");

  const auto before=pc.quantum_session->snapshot();
  send(device,"h 98760\nqload8 12345\n");
  need(device.status()&Device::request_error,
       "classical qload rejects a superposed memory bit");
  receive(device);
  need(pc.quantum_session->snapshot()==before,
       "failed mixed quantum/classical memory request rolls back atomically");

  const std::string huge="1"+std::string(100,'0');
  send(device,"qstore8 "+huge+" 3\nqload8 "+huge+"\n");
  output=receive(device);
  need(output.find("\"value\":3")!=std::string::npos,
       "Ether byte addresses keep arbitrary decimal precision");
}

void preservation_cases(){
  Machine pc;pc.boot_demo();drain(pc);
  const auto screen=pc.screen();const auto steps=pc.cpu.instructions;const auto disk_reads=pc.disk_reads;
  const auto ip=pc.cpu.eip;const auto regs=pc.cpu.regs;
  pc.quantum_command("h 0\ncx 0 1000000000\n");
  const auto snapshot=pc.quantum_snapshot();
  need(pc.waiting_key&&pc.screen()==screen&&pc.cpu.instructions==steps&&pc.cpu.eip==ip&&pc.cpu.regs==regs,"host command preserves paused guest");
  need(pc.disk_reads==disk_reads&&pc.cpu.read16(0x9000)==0x1336&&pc.cpu.read16(0x8000)==0x5843,"host command preserves RAM and disk path");
  bool rejected=false;try{mount(pc,std::vector<std::uint8_t>(512));}catch(const std::exception&){rejected=true;}
  need(rejected&&pc.quantum_snapshot()==snapshot&&pc.screen()==screen,"invalid mount preserves quantum and PC state");
  pc.key('K',0x25);drain(pc);need(pc.screen().find("Entrada BIOS: OK")!=std::string::npos,"original keyboard boot still completes");
  pc.reset();
  need(pc.quantum_session->stats().active_qubits==0&&pc.quantum_device.requests()==0,"PC reset resets device");
  need(pc.quantum_session->background()==mundo_x::InfiniteCore::Background::Zero,"PC reset uses zero background");
  drain(pc);pc.key('K',0x25);drain(pc);
  need(pc.screen().find("Entrada BIOS: OK")!=std::string::npos,"original boot repeats after reset");
  // A 16-bit OUT to our byte device returns an error but the CPU still prints W.
  std::vector<std::uint8_t> disk(512);
  const std::uint8_t code[]={0xba,0x50,0x58,0xb8,0x41,0x42,0xef,0xb8,'W',0x0e,0xcd,0x10,0xf4};
  std::copy(std::begin(code),std::end(code),disk.begin());disk[510]=0x55;disk[511]=0xaa;
  mount(pc,std::move(disk));drain(pc);
  need(pc.screen()[0]=='W'&&!pc.cpu.fault,"protocol errors do not stop guest instruction stream");
  need(pc.quantum_device.status()&Device::request_error,"wide OUT error remains inspectable");
  pc.boot_quantum_demo(false,true);drain(pc);
  need(pc.screen().find("InfiniteCore guest Z: OK")!=std::string::npos,"phase demo really executes guest Z");
  need(expect(pc,"X:0 X:1000000000")==-1&&expect(pc,"Y:0 Y:1000000000")==1,"guest Z flips signed Bell phase");
  need(pc.quantum_device.requests()==1&&pc.quantum_device.errors()==0,"phase demo uses successful port transaction");
}
}
int main(){try{
  guest_cases();protocol_and_host_cases();ether_memory_cases();preservation_cases();
  std::cout<<"{\"suite\":\"pc_quantum\",\"checks\":"<<checks<<",\"guest_modes\":[\"real16\",\"long64\"],\"success_and_failure_guests\":4,\"failures\":0}\n";
  return 0;
}catch(const std::exception& error){std::cerr<<"FAIL PC quantum: "<<error.what()<<'\n';return 1;}}
