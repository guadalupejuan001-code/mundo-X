#include "../pc_machine.hpp"

#include <array>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::PcMachine;
using cosmic_pc::Cpu;
unsigned checks=0;
void need(bool ok,const char* reason){++checks;if(!ok)throw std::runtime_error(reason);}
template<class F> void rejected(F fn,const char* reason){
  bool caught=false;try{fn();}catch(const std::exception&){caught=true;}need(caught,reason);
}
std::array<std::uint8_t,4> pixel(const PcMachine& pc,unsigned x,unsigned y){
  std::array<std::uint8_t,4> out{};pc.copy_video_rgba(x,y,1,1,out.data(),out.size());return out;
}

constexpr std::uint64_t ether_address=PcMachine::kEtherMemoryPhysicalBase;
constexpr std::uint64_t ether_logical=PcMachine::kEtherMemoryLogicalByteBase;
constexpr std::uint64_t work_address=0xa000;
constexpr unsigned iterations=1024;
constexpr std::uint64_t seed=0x0123456789abcdefULL;

void test_exclusive_spill_cache_reads() {
  using cosmic_pc::MemoryFabric;
  using cosmic_pc::SparseRam;
  constexpr auto page_bytes = MemoryFabric::kPageSize;
  constexpr unsigned pages = 8;
  using Page = std::array<std::uint8_t, page_bytes>;
  struct Backing : SparseRam::PageBacking {
    std::array<Page, pages> data{};
    void read(std::uint64_t page, std::uint8_t* bytes) override {
      std::copy(data.at(page).begin(), data.at(page).end(), bytes);
    }
    void write(std::uint64_t page, const std::uint8_t* bytes) override {
      std::copy_n(bytes, page_bytes, data.at(page).begin());
    }
    void reset() noexcept override { data = {}; }
  };
  MemoryFabric ram(pages * page_bytes, 2 * page_bytes);
  ram.attach_page_backing(std::make_unique<Backing>());
  for (unsigned page = 0; page < pages; ++page) {
    Page data;
    for (unsigned i = 0; i < page_bytes; ++i) data[i] = std::uint8_t(i + page);
    ram.write_block(page * page_bytes, data.data(), data.size());
  }
  need(ram.metrics().spilled_pages > 0 && !ram.resident_ram_span(0, page_bytes),
       "backing fixture must spill the first guest page");

  // Both paths must read and count the same bytes, including a page boundary.
  constexpr auto start = page_bytes - 4;
  std::array<std::uint8_t, 8> expected{}, actual{}, shared{};
  ram.read_block(start, expected.data(), expected.size());
  const auto before = ram.metrics();
  {
    const MemoryFabric::ExclusiveRamReads exclusive(ram);
    ram.read_block(start, actual.data(), actual.size());
  }
  ram.read_parallel_ram_block(start, shared.data(), shared.size());
  const auto after = ram.metrics();
  need(expected == actual && actual == shared && expected[0] == 252 && expected[4] == 1,
       "exclusive spill cache changed guest bytes");
  need(after.backing_reads == before.backing_reads &&
       after.backing_cache_hits == before.backing_cache_hits + 4,
       "both spill reads must hit the same decoded pages");

  ram.fill(0, 7, page_bytes);
  const MemoryFabric::ExclusiveRamReads exclusive(ram);
  ram.read_block(start, actual.data(), actual.size());
  need(actual[0] == 7 && actual[4] == 1, "a write left stale bytes in the spill cache");
}

void test_code_page_versions() {
  using cosmic_pc::MemoryFabric;
  constexpr auto page = MemoryFabric::kPageSize;
  MemoryFabric ram(512 * page);
  need(ram.watch_code_page(0) == 0, "unrequested code watch was enabled");
  ram.enable_code_page_versions(true);
  const auto first = ram.watch_code_page(0);
  const auto second = ram.watch_code_page(page);
  need(first && second && first != second, "code pages lack distinct tokens");
  ram.write_byte(2 * page, 1);
  need(ram.code_page_version(0) == first && ram.code_page_version(page) == second,
       "unrelated write invalidated code");
  const std::uint8_t bytes[]{1,2};
  ram.write_block(page - 1, bytes, sizeof(bytes));
  need(!ram.code_page_version(0) && !ram.code_page_version(page),
       "cross-page write left a valid code token");
  const auto restored = ram.watch_code_page(0);
  need(restored && restored != first && ram.restore_byte(page - 1, 0) &&
       !ram.code_page_version(0), "rollback did not invalidate code");
  ram.watch_code_page(page);
  need(ram.try_fill_repeating_page(page, 0x0101010101010101ULL) &&
       !ram.code_page_version(page), "compact fill left a stale token");
  ram.watch_code_page(2 * page);
  need(ram.try_copy_compact_page(page, 2 * page) && !ram.code_page_version(2 * page),
       "compact copy left a stale token");
  ram.prepare_write(3 * page, 2 * page);
  ram.watch_code_page(4 * page);
  need(ram.try_copy_resident_block(3 * page, 4 * page, 8) && !ram.code_page_version(4 * page),
       "resident copy left a stale token");

  const auto original = ram.watch_code_page(0);
  for (std::uint64_t at = page; at < ram.size() && ram.code_page_version(0); at += page)
    ram.watch_code_page(at);
  need(!ram.code_page_version(0) && ram.watch_code_page(0) != original,
       "a colliding code page reused an old token");
  ram.watch_code_page(page);
  ram.fill(0, 0, 257 * page);
  need(!ram.code_page_version(0) && !ram.code_page_version(page),
       "bulk zero left tracked code versions valid");
  const auto old = ram.watch_code_page(0);
  ram.reset_zero();
  need(!ram.code_page_version(0) && ram.watch_code_page(0) != old,
       "RAM reset reused a valid code token");
  MemoryFabric replacement(ram.size());
  replacement.enable_code_page_versions(true);
  const auto fresh = replacement.watch_code_page(0);
  need(fresh && fresh != ram.code_page_version(0), "replacement RAM reused a token");
  ram = std::move(replacement);
  need(ram.code_page_version(0) == fresh, "code tokens moved separately from their RAM");
}

void prepare_x64(PcMachine& pc){
  pc.boot_long_mode_demo();
  pc.run(4000);
  need(pc.cpu.is_long_mode()&&pc.cpu.halted&&!pc.cpu.fault,"Ether test did not reach long mode");
  // An independent 2 MiB identity mapping covers the existing Ether window.
  const auto page=ether_address&~0x1fffffULL;
  pc.cpu.write64(0x2000+8*(ether_address>>30),0x4003);
  pc.cpu.write64(0x4000+8*((ether_address>>21)&0x1ff),page|0x83);
  pc.cpu.eip=work_address;
  pc.cpu.halted=false;
  pc.cpu.regs[Cpu::EBX]=ether_address;
}

struct EtherRun {
  std::array<std::uint64_t,16> registers;
  std::uint64_t cycles, ip, value, ns;
  std::uint32_t flags;
};

EtherRun ether_workload(bool bulk){
  PcMachine pc(2048ULL<<20,64ULL<<20);
  prepare_x64(pc);
  if(!bulk) {
    const auto session=pc.quantum_session;
    pc.cpu.memory.attach_window(ether_address, PcMachine::kEtherMemoryBytes,
        [session](std::uint64_t offset){return session->qload8(std::to_string(ether_logical+offset));},
        [session](std::uint64_t offset,std::uint8_t x){session->qstore8(std::to_string(ether_logical+offset),x);});
  }
  const std::uint8_t code[]{
    0x48,0x89,0x03, // MOV [RBX],RAX
    0x48,0x8b,0x13, // MOV RDX,[RBX]
    0x48,0xf7,0xd0, // NOT RAX
    0xff,0xc9,      // DEC ECX
    0x75,0xf3,      // JNZ back to the store
    0xf4,           // HLT
  };
  auto& cpu=pc.cpu;
  cpu.memory.write_block(work_address, code, sizeof(code));
  cpu.regs[Cpu::EAX]=seed;
  cpu.regs[Cpu::ECX]=iterations;
  const auto begin=std::chrono::steady_clock::now();
  pc.run(iterations*5+1);
  const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(
      std::chrono::steady_clock::now()-begin).count();
  if(cpu.fault)throw std::runtime_error(cpu.fault->describe());
  need(cpu.halted&&cpu.regs[Cpu::EDX]==~seed,"x86 loop did not observe its InfiniteCore stores");
  const auto json=pc.quantum_json();
  need(json.find(bulk?"\"ether_span_writes\":1024,":"\"ether_span_writes\":0,")!=std::string::npos,
       "diagnostics did not identify the actual store path");
  need(json.find("\"guest_requests\":0,")!=std::string::npos,"workload used the command ports");
  return {cpu.regs,cpu.instructions,cpu.eip,cpu.read64(ether_address),std::uint64_t(ns),cpu.eflags};
}

void compare_ether_workloads(){
  std::array<std::uint64_t,3> scalar_ns{}, bulk_ns{};
  for (std::size_t i=0;i<scalar_ns.size();++i) {
    const auto first=ether_workload(i%2!=0);
    const auto second=ether_workload(i%2==0);
    const auto& scalar=i%2==0?first:second;
    const auto& bulk=i%2==0?second:first;
    need(scalar.registers==bulk.registers&&scalar.cycles==bulk.cycles&&scalar.ip==bulk.ip&&
         scalar.flags==bulk.flags&&scalar.value==bulk.value&&bulk.value==~seed,
         "InfiniteCore span path diverged from scalar guest semantics");
    scalar_ns[i]=scalar.ns;
    bulk_ns[i]=bulk.ns;
  }
  std::sort(scalar_ns.begin(),scalar_ns.end());
  std::sort(bulk_ns.begin(),bulk_ns.end());
  std::cout<<"PRIME_C09_ETHER_INFINITECORE_PASS iterations="<<iterations
           <<" scalar_median_ns="<<scalar_ns[1]<<" bulk_median_ns="<<bulk_ns[1]<<'\n';
}

void test_failed_operand(){
  PcMachine pc(2048ULL<<20,64ULL<<20);
  prepare_x64(pc);
  pc.quantum_session->apply({{'H',std::to_string(8*(ether_logical+7)),{}}});
  const auto snapshot=pc.quantum_session->snapshot();
  auto& cpu=pc.cpu;
  const std::uint8_t bytes[]{0x48,0x89,0x03};
  cpu.memory.write_block(work_address, bytes, sizeof(bytes));
  cpu.regs[Cpu::EAX]=UINT64_MAX;
  need(cpu.step()==cosmic_pc::StepResult::Faulted&&cpu.eip==work_address,
       "x86 store did not reject a nonclassical byte");
  need(pc.quantum_session->snapshot()==snapshot,"failed operand changed InfiniteCore state");
  std::array<std::uint8_t,8> out;
  out.fill(0x5a);
  rejected([&]{pc.cpu.memory.read_block(ether_address,out.data(),out.size());},
           "block read measured a nonclassical byte");
  need(std::all_of(out.begin(),out.end(),[](auto x){return x==0x5a;}),
       "failed block read exposed a partial operand");
}
}

int main(){
  try{
    test_exclusive_spill_cache_reads();
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

    compare_ether_workloads();
    test_failed_operand();
    test_code_page_versions();

    std::cout<<"{\"suite\":\"pc_memory_fabric\",\"checks\":"<<checks
             <<",\"ether_window_bytes\":"<<PcMachine::kEtherMemoryBytes
             <<",\"graphics_workload\":\"guest-x86-vbe-1280x720\",\"failures\":0}\n";
    return 0;
  }catch(const std::exception& error){
    std::cerr<<"FAIL PC MemoryFabric: "<<error.what()<<'\n';return 1;
  }
}
