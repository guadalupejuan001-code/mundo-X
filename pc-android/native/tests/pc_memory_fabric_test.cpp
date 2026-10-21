#include "../pc_machine.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::PcMachine;
unsigned checks=0;
void need(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
template<class F> void rejected(F fn,const char* reason){
  bool caught=false;try{fn();}catch(const std::exception&){caught=true;}need(caught,reason);
}
std::array<std::uint8_t,4> pixel(const PcMachine& pc,unsigned x,unsigned y){
  std::array<std::uint8_t,4> out{};pc.copy_video_rgba(x,y,1,1,out.data(),out.size());return out;
}
}

int main(){
  try{
    constexpr std::size_t MiB=1024*1024;
    PcMachine pc(std::size_t(2048)*MiB,std::size_t(1024)*MiB);
    pc.cpu.a20_enabled=true;
    const auto base=PcMachine::kEtherMemoryPhysicalBase;
    const auto logical=PcMachine::kEtherMemoryLogicalByteBase;
    need(pc.cpu.memory.has_window(),"MemoryFabric did not attach Ether window");
    need(pc.cpu.memory.window_base()==base&&pc.cpu.memory.window_bytes()==PcMachine::kEtherMemoryBytes,
         "Ether window geometry changed");
    need(pc.cpu.read8(base)==0&&pc.quantum_session->qload8(std::to_string(logical))==0,
         "Fresh Ether byte is not implicit zero");

    const auto classical_before=pc.cpu.memory.allocated_bytes();
    pc.cpu.write8(base,0xa5);
    need(pc.cpu.read8(base)==0xa5,"x86 read did not return Ether-backed byte");
    need(pc.quantum_session->qload8(std::to_string(logical))==0xa5,
         "x86 store did not update the shared Ether session");
    need(pc.cpu.memory.allocated_bytes()==classical_before,
         "Ether byte incorrectly allocated SparseRam backing");
    need(pc.quantum_session->stats().active_qubits>0,
         "Nonzero Ether byte did not materialize its finite modifications");

    const std::array<std::uint8_t,4> bytes{{0x34,0x12,0x7f,0x80}};
    pc.cpu.memory.write_block(base,bytes.data(),bytes.size());
    std::array<std::uint8_t,4> read{};
    pc.cpu.memory.read_block(base,read.data(),read.size());
    need(read==bytes,"Ether Memory block round-trip changed bytes");
    for(std::size_t i=0;i<bytes.size();++i)
      need(pc.quantum_session->qload8(std::to_string(logical+i))==bytes[i],
           "Block write diverged from InfiniteSession");

    // A CPU-style load must never collapse a genuinely quantum byte.
    pc.quantum_session->reset(mundo_x::InfiniteCore::Background::Zero);
    const auto first_bit=std::to_string(logical*8);
    pc.quantum_session->apply({{'H',first_bit,{}}});
    rejected([&]{(void)pc.cpu.read8(base);},
             "Non-deterministic Ether byte was exposed as a classical x86 value");
    pc.quantum_session->reset(mundo_x::InfiniteCore::Background::Zero);
    need(pc.cpu.read8(base)==0,"Quantum reset did not restore zero Ether window");

    // Use the repository's guest graphics workload as a game-like integration
    // stressor. It executes guest x86 instructions and writes a real 720p VBE
    // framebuffer; it is not presented as a commercial game benchmark.
    pc.boot_graphics_demo(1280,720);
    for(unsigned slice=0;slice<100&&!pc.cpu.halted&&!pc.cpu.fault;++slice)pc.run(50000);
    if(pc.cpu.fault)throw std::runtime_error(pc.cpu.fault->describe());
    need(pc.cpu.halted,"Guest graphics workload exceeded its execution budget");
    need(pc.cpu.read32(cosmic_pc::kGraphicsDemoResultAddress)==cosmic_pc::kGraphicsDemoResult,
         "Guest graphics workload did not verify its own framebuffer");
    need(pixel(pc,0,0)==std::array<std::uint8_t,4>{{0xff,0x40,0x40,255}},
         "Guest graphics top-left marker changed");
    need(pixel(pc,1279,719)==std::array<std::uint8_t,4>{{0xf5,0xf7,0xfa,255}},
         "Guest graphics bottom-right marker changed");
    need(pc.quantum_session->qload8(std::to_string(logical))==0,
         "Graphics workload unexpectedly modified Ether Memory");
    need(pc.cpu.memory.allocated_bytes()<12*MiB,
         "Game-like graphics workload lost the sparse classical-RAM budget");

    const auto json=pc.json();
    need(json.find("\"ether_memory_bytes\":256")!=std::string::npos,
         "PC diagnostics do not expose Ether Memory window");
    need(json.find("\"ram_policy\":\"zero-pages-on-demand+ether-window\"")!=std::string::npos,
         "PC diagnostics do not identify MemoryFabric policy");

    std::cout<<"{\"suite\":\"pc_memory_fabric\",\"checks\":"<<checks
             <<",\"ether_window_bytes\":"<<PcMachine::kEtherMemoryBytes
             <<",\"graphics_workload\":\"guest-x86-vbe-1280x720\",\"failures\":0}\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"FAIL PC MemoryFabric: "<<error.what()<<'\n';return 1;
  }
}
