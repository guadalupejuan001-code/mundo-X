#pragma once
#include "x86_cpu.hpp"
#include "boot_media.hpp"
#include "demo_boot.hpp"
#include "long_mode_guest.hpp"
#include "quantum_device.hpp"
#include "quantum_guest.hpp"
#include "pc_video.hpp"
#include "graphics_demo.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <functional>
#include <iomanip>
#include <memory>
#include <sstream>
#include <utility>

namespace cosmic_pc {
// Original BIOS service model and PC integration. This is deliberately explicit
// about the hardware that exists; missing BIOS functions stop instead of lying.
class PcMachine {
 public:
  using Reader=std::function<bool(std::uint64_t,std::uint8_t*,std::size_t)>;
  Cpu cpu;
  const std::shared_ptr<mundo_x::InfiniteSession> quantum_session;
  PcQuantumDevice quantum_device;
  bool waiting_key=false;
  std::string host_error, boot_kind="none", pending;
  std::uint64_t disk_reads=0;
  // The hooks capture this address; instances must remain at a stable address.
  PcMachine(const PcMachine&)=delete;
  PcMachine& operator=(const PcMachine&)=delete;
  PcMachine(PcMachine&&)=delete;
  PcMachine& operator=(PcMachine&&)=delete;
  static std::size_t checked_ram(std::size_t bytes) {
    if(bytes<2*1024*1024||bytes>std::size_t(2048)*1024*1024)throw std::invalid_argument("RAM virtual fuera de 2..2048 MiB");
    return bytes;
  }
  static mundo_x::InfiniteCore::Limits quantum_limits() {
    mundo_x::InfiniteCore::Limits limits;
    limits.max_component_qubits = 1024;
    limits.max_state_bytes = 16 * 1024 * 1024;
    return limits;
  }
  explicit PcMachine(std::size_t ram_bytes=16*1024*1024,
                     std::size_t resident_budget_bytes=std::size_t(1024)*1024*1024)
      :cpu(checked_ram(ram_bytes),std::min(ram_bytes,resident_budget_bytes)),
       quantum_session(std::make_shared<mundo_x::InfiniteSession>(
           mundo_x::InfiniteCore::Background::Zero, quantum_limits())),
       quantum_device(quantum_session) {
    cpu.interrupt_hook=[this](Cpu&,std::uint8_t n){return bios(n);};
    cpu.in_hook=[this](std::uint16_t port,unsigned width){return input(port,width);};
    cpu.out_hook=[this](std::uint16_t port,unsigned width,std::uint32_t v){return output(port,width,v);};
    clear_screen();
    video_.reset(cpu.memory);
  }
  void boot_demo() {
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(demo_boot_disk());
    mount([bytes](std::uint64_t off,std::uint8_t* dst,std::size_t n){if(off>bytes->size()||n>bytes->size()-off)return false;std::copy_n(bytes->data()+off,n,dst);return true;},bytes->size(),"raw");
    if(host_error.empty())boot_kind="demo-x86";
  }
  void boot_long_mode_demo() {
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(long_mode_boot_disk());
    mount([bytes](std::uint64_t off,std::uint8_t* dst,std::size_t n){if(off>bytes->size()||n>bytes->size()-off)return false;std::copy_n(bytes->data()+off,n,dst);return true;},bytes->size(),"raw");
    if(host_error.empty())boot_kind="demo-x64";
  }
  void boot_quantum_demo(bool long_mode=false,bool apply_z=false) {
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(quantum_boot_disk(long_mode,true,apply_z));
    mount([bytes](std::uint64_t off,std::uint8_t* dst,std::size_t n){if(off>bytes->size()||n>bytes->size()-off)return false;std::copy_n(bytes->data()+off,n,dst);return true;},bytes->size(),"raw");
    if(host_error.empty())boot_kind=apply_z?"demo-quantum-guest-z":(long_mode?"demo-quantum-x64":"demo-quantum-x86");
  }
  void boot_graphics_demo(unsigned width=1280,unsigned height=720) {
    if(!PcVideo::available(cpu.memory))throw std::runtime_error("La salida VBE necesita la región de vídeo dentro del perfil de 2 GiB");
    auto bytes=std::make_shared<std::vector<std::uint8_t>>(graphics_boot_disk(width,height));
    mount([bytes](std::uint64_t off,std::uint8_t* dst,std::size_t n){if(off>bytes->size()||n>bytes->size()-off)return false;std::copy_n(bytes->data()+off,n,dst);return true;},bytes->size(),"raw");
    if(host_error.empty())boot_kind="demo-graphics";
  }
  void mount(Reader reader,std::uint64_t bytes,const std::string& mode="auto") {
    // Parse and validate the new medium before replacing the current machine.
    BootMedia candidate(bytes,reader);
    const auto plan=candidate.plan(mode);
    const std::uint64_t address=std::uint64_t(plan.load_segment)*16;
    if(address>cpu.memory.size()||plan.image.size()>cpu.memory.size()-address)throw std::runtime_error("La imagen de arranque excede la RAM");
    reader_=std::move(reader);media_bytes_=bytes;plan_=plan;boot_kind=plan.kind;reset();
  }
  void reset() {
    quantum_session->reset(mundo_x::InfiniteCore::Background::Zero);
    quantum_device.power_reset();
    quantum_output_.clear();quantum_error_.clear();quantum_truncated_=false;
    quantum_cache_valid_=false;
    host_error.clear();pending.clear();waiting_key=false;keys_.clear();disk_reads=0;last_disk_status_=0;
    video_mode_=3;video_.text_mode(3);pic1_=pic2_=0xff;cmos_index_=0;serial_.clear();
    if(!reader_)return;
    cpu.memory.reset_zero();
    video_.reset(cpu.memory);
    cpu.reset(plan_.initial_cs,plan_.initial_ip);cpu.a20_enabled=true;
    cpu.segments[0]=cpu.segments[2]=cpu.segments[3]=0;
    cpu.set_reg16(4,0x7c00);cpu.set_reg16(2,plan_.drive);cpu.eflags=0x202;
    cpu.memory.write_block(std::size_t(plan_.load_segment)*16,plan_.image.data(),plan_.image.size());
    // BIOS data area: video mode 3, 80 columns, page0, conventional RAM.
    cpu.write16(0x410,0x0020);cpu.write16(0x413,640);cpu.write8(0x449,3);cpu.write16(0x44a,80);cpu.write16(0x44c,4000);
    cpu.write8(0x484,24);cpu.write8(0x475,plan_.drive==0x80?1:0);clear_screen();started_=std::chrono::steady_clock::now();
  }
  void run(unsigned budget) {
    if(!reader_||!host_error.empty()||waiting_key||cpu.fault||cpu.halted)return;
    budget=std::min(budget,50000u);
    for(unsigned i=0;i<budget&&!waiting_key;++i){
      auto r=cpu.step();
      if(r==StepResult::Faulted){pending="Consulta el diagnóstico de la CPU y la memoria. Windows completo sigue pendiente.";break;}
      if(r==StepResult::Halted)break;
    }
  }
  void key(std::uint8_t ascii,std::uint8_t scan) {
    if(waiting_key){cpu.set_reg16(0,std::uint16_t(scan)<<8|ascii);waiting_key=false;}
    else if(keys_.size()<256)keys_.push_back(std::uint16_t(scan)<<8|ascii);
  }
  // Host commands use the same session as guest ports, but do not consume or
  // replace the guest's in-progress request/response. Expected errors are local.
  void quantum_command(const std::string& program) {
    try {
      if(program.size()>PcQuantumDevice::max_request_bytes)
        throw std::length_error("Quantum request exceeds 65536 bytes");
      auto output=quantum_session->execute(program);
      quantum_truncated_=output.size()>8192;
      if(quantum_truncated_)output.resize(8192);
      quantum_output_=std::move(output);quantum_error_.clear();
    } catch(const std::exception& error) { quantum_error_=error.what(); }
  }
  void quantum_reset(const std::string& background) {
    try {
      if(background!="zero"&&background!="bell")throw std::invalid_argument("Background must be zero or bell");
      quantum_session->reset(background=="zero"?mundo_x::InfiniteCore::Background::Zero:mundo_x::InfiniteCore::Background::BellPairs);
      quantum_output_.clear();quantum_error_.clear();quantum_truncated_=false;
    } catch(const std::exception& error) { quantum_error_=error.what(); }
  }
  void quantum_restore(const std::string& snapshot) {
    try {
      quantum_session->restore(snapshot);
      quantum_output_.clear();quantum_error_.clear();quantum_truncated_=false;
    } catch(const std::exception& error) { quantum_error_=error.what(); }
  }
  std::string quantum_snapshot() const { return quantum_session->snapshot(); }
  void quantum_reject(const std::string& message) { quantum_error_=message; }
  void quantum_probe(const std::string& first,const std::string& second) {
    try {
      auto a=mundo_x::infinite_detail::canonical_address(first,quantum_session->limits().max_address_digits);
      auto b=mundo_x::infinite_detail::canonical_address(second,quantum_session->limits().max_address_digits);
      if(a==b)throw std::invalid_argument("Choose two different addresses for the correlation panel");
      auto observation=quantum_session->query_batch(correlation_observables(a,b));
      quantum_first_.swap(a);quantum_second_.swap(b);
      std::swap(quantum_observation_,observation);
      quantum_cache_valid_=true;quantum_error_.clear();
    } catch(const std::exception& error) { quantum_error_=error.what(); }
  }
  std::string quantum_json() const {
    const auto stats=quantum_session->stats();
    const auto epoch=quantum_session->epoch();
    if(!quantum_cache_valid_||quantum_observation_.revision!=stats.revision||quantum_observation_.epoch!=epoch){
      quantum_observation_=quantum_session->query_batch(correlation_observables(quantum_first_,quantum_second_));
      quantum_cache_valid_=true;
    }
    const auto& observation=quantum_observation_;
    const auto limits=quantum_session->limits();
    std::ostringstream o;
    o<<"{\"engine\":\"InfiniteCore\",\"background\":"<<quote(quantum_session->background()==mundo_x::InfiniteCore::Background::Zero?"zero":"bell")
     <<",\"epoch\":"<<observation.epoch<<",\"revision\":"<<observation.revision
     <<",\"active_qubits\":"<<observation.stats.active_qubits<<",\"components\":"<<observation.stats.components
     <<",\"largest_component\":"<<observation.stats.largest_component<<",\"tableau_bytes\":"<<observation.stats.tableau_bytes
     <<",\"address_bytes\":"<<observation.stats.address_bytes
     <<",\"max_active_qubits\":"<<limits.max_active_qubits<<",\"max_component_qubits\":"<<limits.max_component_qubits
     <<",\"max_state_bytes\":"<<limits.max_state_bytes<<",\"max_address_digits\":"<<limits.max_address_digits
     <<",\"first\":"<<quote(quantum_first_)<<",\"second\":"<<quote(quantum_second_)
     <<",\"labels\":[\"XX\",\"YY\",\"ZZ\",\"ZI\",\"IZ\"],\"values\":[";
    for(std::size_t i=0;i<observation.values.size();++i){if(i)o<<',';o<<observation.values[i];}
    o<<"],\"output\":"<<quote(quantum_output_)<<",\"output_truncated\":"<<(quantum_truncated_?"true":"false")
     <<",\"error\":"<<quote(quantum_error_)<<",\"port_status\":"<<unsigned(quantum_device.status())
     <<",\"guest_requests\":"<<quantum_device.requests()<<",\"guest_errors\":"<<quantum_device.errors()
     <<",\"request_bytes\":"<<quantum_device.request_bytes()<<",\"response_bytes\":"<<quantum_device.remaining()<<"}";
    return o.str();
  }
  std::string screen() const {
    std::string out;
    for(unsigned y=0;y<25;++y){for(unsigned x=0;x<80;++x){auto c=cpu.memory[0xb8000+(y*80+x)*2];out.push_back(c>=32&&c<127?char(c):' ');}if(y!=24)out+='\n';}
    return out;
  }
  PcVideoInfo video_info() const noexcept {return video_.info();}
  void copy_vga_text(std::uint8_t* destination,std::size_t capacity) const {
    if(!destination||capacity<4000)throw std::invalid_argument("Buffer VGA insuficiente");
    cpu.memory.read_block(0xb8000,destination,4000);
  }
  void copy_video_bgrx(std::uint8_t* destination,std::size_t capacity) const {
    video_.copy_bgrx(cpu.memory,destination,capacity);
  }
  void copy_video_rgba(std::uint32_t x,std::uint32_t y,std::uint32_t width,std::uint32_t height,
                       std::uint8_t* destination,std::size_t capacity,std::size_t stride=0) const {
    video_.copy_rgba(cpu.memory,x,y,width,height,destination,capacity,stride);
  }
  std::string status() const {
    if(!host_error.empty()||cpu.fault)return "fault";
    if(!reader_)return "empty";
    if(waiting_key)return "waiting_key";
    if(cpu.halted)return "halted";
    return cpu.instructions?"running":"ready";
  }
  std::string json() const {
    std::ostringstream o;
    const auto video=video_info();
    const std::string message=!host_error.empty()?host_error:(cpu.fault?cpu.fault->describe():(!reader_?"Selecciona una ISO, disco o arranque de prueba":waiting_key?"Esperando entrada de teclado BIOS":cpu.halted?"El invitado ejecutó HLT":std::string("CPU propia: ")+cpu.mode_name()));
    o<<"{\"status\":"<<quote(status())<<",\"instructions\":"<<cpu.instructions<<",\"cs\":"<<cpu.segments[1]<<",\"ip\":"<<cpu.eip<<",\"flags\":"<<cpu.eflags<<",\"regs\":[";
    for(unsigned i=0;i<cpu.regs.size();++i){if(i)o<<',';o<<cpu.regs[i];}
    o<<"],\"regs_hex\":[";
    for(unsigned i=0;i<cpu.regs.size();++i){if(i)o<<',';o<<quote(hex64(cpu.regs[i]));}
    o<<"],\"rip_hex\":"<<quote(hex64(cpu.eip))<<",\"cpu_mode\":"<<quote(cpu.mode_name())
     <<",\"message\":"<<quote(message)<<",\"boot_kind\":"<<quote(boot_kind)<<",\"pending\":"<<quote(pending)
     <<",\"disk_reads\":"<<disk_reads<<",\"ram_bytes\":"<<cpu.memory.size()
     <<",\"ram_allocated_bytes\":"<<cpu.memory.allocated_bytes()<<",\"ram_budget_bytes\":"<<cpu.memory.budget_bytes()
     <<",\"ram_metadata_bytes\":"<<cpu.memory.metadata_bytes()<<",\"ram_allocated_pages\":"<<cpu.memory.allocated_pages()
     <<",\"video_kind\":"<<quote(video.graphics?"vbe32":"text")<<",\"video_mode\":"<<video.mode
     <<",\"video_width\":"<<video.width<<",\"video_height\":"<<video.height<<",\"video_pitch\":"<<video.pitch
     <<",\"video_bytes\":"<<video.byte_size<<",\"video_address\":"<<video.physical_address
     <<",\"video_format\":"<<quote(video.graphics?"BGRX8888":"VGA-char-attribute")
     <<",\"video_aperture_bytes\":"<<(PcVideo::available(cpu.memory)?PcVideo::kApertureBytes:0)
     <<",\"ram_policy\":\"zero-pages-on-demand\",\"engine\":\"cosmic-pc-own-x86\",\"windows_ready\":false,\"graphics_3d_ready\":false,\"quantum\":"<<quantum_json()<<"}";
    return o.str();
  }
  static std::string hex64(std::uint64_t value){std::ostringstream o;o<<std::hex<<std::uppercase<<std::setfill('0')<<std::setw(16)<<value;return o.str();}
  static std::string quote(const std::string& s){std::ostringstream o;o<<'"';for(unsigned char c:s){if(c=='"'||c=='\\')o<<'\\'<<char(c);else if(c=='\n')o<<"\\n";else if(c=='\r')o<<"\\r";else if(c=='\t')o<<"\\t";else if(c<32)o<<"\\u00"<<std::hex<<std::setw(2)<<std::setfill('0')<<unsigned(c)<<std::dec;else o<<char(c);}o<<'"';return o.str();}
 private:
  std::string quantum_first_="0",quantum_second_="1000000000";
  std::string quantum_output_,quantum_error_;
  bool quantum_truncated_=false;
  mutable bool quantum_cache_valid_=false;
  mutable mundo_x::InfiniteSession::Observation quantum_observation_;
  static std::vector<mundo_x::InfiniteSession::Observable> correlation_observables(const std::string& a,const std::string& b){
    return {{1,{{a,'X'},{b,'X'}}},{1,{{a,'Y'},{b,'Y'}}},{1,{{a,'Z'},{b,'Z'}}},{1,{{a,'Z'}}},{1,{{b,'Z'}}}};
  }
  PcVideo video_;
  Reader reader_;
  std::uint64_t media_bytes_=0;
  BootPlan plan_{};
  std::deque<std::uint16_t> keys_;
  unsigned row_=0,col_=0;
  std::uint8_t last_disk_status_=0,video_mode_=3,pic1_=0xff,pic2_=0xff,cmos_index_=0;
  std::chrono::steady_clock::time_point started_=std::chrono::steady_clock::now();
  static constexpr std::uint32_t CF=1,ZF=64;
  std::uint8_t lo(unsigned r)const{return cpu.regs[r]&255;}
  std::uint8_t hi(unsigned r)const{return(cpu.regs[r]>>8)&255;}
  void low(unsigned r,std::uint8_t v){cpu.regs[r]=(cpu.regs[r]&~std::uint64_t(255))|v;}
  void high(unsigned r,std::uint8_t v){cpu.regs[r]=(cpu.regs[r]&~std::uint64_t(0xff00))|(std::uint64_t(v)<<8);}
  void carry(bool v){cpu.eflags=(cpu.eflags&~CF)|(v?CF:0);}
  void zero(bool v){cpu.eflags=(cpu.eflags&~ZF)|(v?ZF:0);}
  std::uint32_t physical(unsigned segment,std::uint16_t off)const{return std::uint32_t(cpu.segments[segment])*16+off;}
  void clear_screen(){row_=col_=0;for(unsigned i=0;i<2000;++i){cpu.memory[0xb8000+i*2]=' ';cpu.memory[0xb8001+i*2]=7;}sync_cursor();}
  void sync_cursor(){cpu.memory[0x450]=col_;cpu.memory[0x451]=row_;}
  void emit(std::uint8_t c){
    if(c=='\r')col_=0;
    else if(c=='\n')++row_;
    else if(c==8){if(col_)--col_;}
    else if(c!=7){const auto at=0xb8000+(row_*80+col_)*2;cpu.write8(at,c);cpu.write8(at+1,7);if(++col_==80){col_=0;++row_;}}
    if(row_>=25){for(unsigned i=0;i<24*160;++i)cpu.write8(0xb8000+i,cpu.read8(0xb8000+160+i));for(unsigned i=24*80;i<25*80;++i){cpu.write8(0xb8000+i*2,' ');cpu.write8(0xb8001+i*2,7);}row_=24;}
    sync_cursor();
  }
  [[noreturn]] void missing(const std::string& s){pending=s;cpu.fail(s);}
  void disk_result(std::uint8_t err){last_disk_status_=err;high(0,err);carry(err!=0);}
  bool read_disk(std::uint64_t lba,unsigned count,std::uint32_t dest){
    if(!reader_||count==0||count>127)return false;
    const auto sector=std::uint64_t(plan_.logical_sector_size), n=sector*count;
    if(lba>media_bytes_/sector||n>media_bytes_-lba*sector||dest>cpu.memory.size()||n>cpu.memory.size()-dest)return false;
    std::vector<std::uint8_t> tmp(static_cast<std::size_t>(n));
    if(!reader_(lba*sector,tmp.data(),tmp.size()))return false;
    for(std::size_t i=0;i<tmp.size();++i)cpu.write8(dest+i,tmp[i]);
    ++disk_reads;return true;
  }
  void disk_bios(){
    if(lo(2)!=plan_.drive){disk_result(1);return;}
    const auto fn=hi(0);
    if(fn==0){disk_result(0);return;}
    if(fn==1){high(0,last_disk_status_);carry(last_disk_status_!=0);return;}
    if(fn==2){
      if(plan_.logical_sector_size!=512){disk_result(1);return;}
      const unsigned sector=lo(1)&63,cylinder=unsigned(hi(1))|((lo(1)&0xc0)<<2),head=hi(2),count=lo(0);
      if(!sector||head>=16){disk_result(4);return;}
      const bool ok=read_disk((std::uint64_t(cylinder)*16+head)*63+sector-1,count,physical(0,cpu.reg16(3)));
      disk_result(ok?0:4);low(0,ok?count:0);return;
    }
    if(fn==3||fn==0x43){disk_result(3);return;} // Read-only media in this phase.
    if(fn==8){
      const auto total=media_bytes_/512, cylinders=std::max<std::uint64_t>(1,std::min<std::uint64_t>(1024,(total+1007)/1008));
      const auto maxc=unsigned(cylinders-1);high(1,maxc&255);low(1,63|((maxc>>2)&0xc0));high(2,15);low(2,1);low(3,0);disk_result(0);return;
    }
    if(fn==0x15){high(0,3);const auto sectors=media_bytes_/plan_.logical_sector_size;cpu.set_reg16(1,(sectors>>16)&0xffff);cpu.set_reg16(2,sectors&0xffff);carry(false);return;}
    if(fn==0x41){if(cpu.reg16(3)!=0x55aa){disk_result(1);return;}cpu.set_reg16(3,0xaa55);cpu.set_reg16(1,5);high(0,0x30);carry(false);return;}
    if(fn==0x42){
      const auto dap=physical(3,cpu.reg16(6));
      if(cpu.read8(dap)<16||cpu.read8(dap+1)!=0){disk_result(1);return;}
      const auto count=cpu.read16(dap+2), off=cpu.read16(dap+4),seg=cpu.read16(dap+6);
      if(off==0xffff&&seg==0xffff){disk_result(1);return;}
      const std::uint64_t lba=std::uint64_t(cpu.read32(dap+8))|(std::uint64_t(cpu.read32(dap+12))<<32);
      const bool ok=read_disk(lba,count,std::uint32_t(seg)*16+off);if(!ok)cpu.write16(dap+2,0);disk_result(ok?0:4);return;
    }
    if(fn==0x48){
      const auto p=physical(3,cpu.reg16(6));if(cpu.read16(p)<26){disk_result(1);return;}
      const bool chs=plan_.logical_sector_size==512;
      const auto cylinders=std::max<std::uint64_t>(1,std::min<std::uint64_t>(1024,(media_bytes_/512+1007)/1008));
      cpu.write16(p,26);cpu.write16(p+2,chs?2:0);cpu.write32(p+4,chs?cylinders:0);cpu.write32(p+8,chs?16:0);cpu.write32(p+12,chs?63:0);
      const auto n=media_bytes_/plan_.logical_sector_size;cpu.write32(p+16,n&0xffffffff);cpu.write32(p+20,n>>32);cpu.write16(p+24,plan_.logical_sector_size);disk_result(0);return;
    }
    if(fn==0x4b&&plan_.logical_sector_size==2048){
      // El Torito status/terminate uses AL=1 to query; no emulation to terminate.
      if(lo(0)>1){disk_result(1);return;}
      const auto p=physical(3,cpu.reg16(6));for(unsigned i=0;i<19;++i)cpu.write8(p+i,0);cpu.write8(p,0x13);cpu.write8(p+2,plan_.drive);cpu.write32(p+4,plan_.source_offset/2048);cpu.write16(p+12,plan_.load_segment);cpu.write16(p+14,plan_.image.size()/512);disk_result(0);return;
    }
    std::ostringstream s;s<<"BIOS INT 13h función 0x"<<std::hex<<unsigned(fn)<<" pendiente";missing(s.str());
  }
  void video_bios(){
    const auto fn=hi(0);
    if(fn==0){const auto mode=lo(0)&127;if(mode!=2&&mode!=3)missing("Modo VGA pendiente; gráficos disponibles por VBE INT 10h 4F02");video_mode_=mode;video_.text_mode(mode,lo(0)&0x80);cpu.write8(0x449,mode);if(!(lo(0)&0x80))clear_screen();return;}
    if(fn==0x4f){
      const auto request=cpu.reg16(3);
      const auto mode=request&0x7fff;
      if(PcVideo::available(cpu.memory)&&cpu.reg16(0)==0x4f02&&(mode==2||mode==3)){
        video_mode_=mode;video_.text_mode(mode,request&0x8000);cpu.write8(0x449,mode);
        if(!(request&0x8000))clear_screen();
        cpu.set_reg16(0,0x004f);return;
      }
      video_.bios(cpu);
      if(cpu.reg16(0)==0x004f&&video_.info().graphics){video_mode_=0xff;cpu.write8(0x449,0xff);}
      return;
    }
    if(fn==0x0e){emit(lo(0));return;}
    if(fn==1)return; // Cursor shape has no semantic effect on the text framebuffer.
    if(fn==2){if(hi(3)!=0||hi(2)>=25||lo(2)>=80)missing("BIOS de vídeo: cursor o página inválidos");row_=hi(2);col_=lo(2);sync_cursor();return;}
    if(fn==3){if(hi(3)!=0)missing("BIOS de vídeo: página distinta de cero");cpu.set_reg16(1,0x0607);cpu.set_reg16(2,(row_<<8)|col_);return;}
    if(fn==5){if(lo(0)!=0)missing("BIOS de vídeo: páginas adicionales pendientes");return;}
    if(fn==0x0f){high(0,80);low(0,video_mode_);high(3,0);return;}
    if(fn==8){const auto p=0xb8000+(row_*80+col_)*2;cpu.set_reg16(0,cpu.read16(p));return;}
    if(fn==9||fn==0xa){const unsigned n=cpu.reg16(1),ch=lo(0),attr=lo(3);if(hi(3)!=0)missing("BIOS de vídeo: página no implementada");const unsigned start=row_*80+col_;for(unsigned i=0;i<n&&start+i<2000;++i){cpu.write8(0xb8000+(start+i)*2,ch);if(fn==9)cpu.write8(0xb8001+(start+i)*2,attr);}return;}
    if(fn==6||fn==7){
      const unsigned top=hi(1),left=lo(1),bottom=hi(2),right=lo(2),rows=lo(0),attr=hi(3);
      if(top>bottom||left>right||bottom>=25||right>=80)missing("Ventana de texto BIOS inválida");
      std::vector<std::uint16_t> temp((bottom-top+1)*(right-left+1));unsigned k=0;
      for(unsigned y=top;y<=bottom;++y)for(unsigned x=left;x<=right;++x){const int src=fn==6?int(y)+rows:int(y)-rows;temp[k++]=(rows&&src>=int(top)&&src<=int(bottom))?cpu.read16(0xb8000+(unsigned(src)*80+x)*2):std::uint16_t(attr<<8|' ');}
      k=0;for(unsigned y=top;y<=bottom;++y)for(unsigned x=left;x<=right;++x)cpu.write16(0xb8000+(y*80+x)*2,temp[k++]);return;
    }
    if(fn==0x12){high(3,0);low(3,3);cpu.set_reg16(1,0);return;}
    if(fn==0x1a){low(0,0x1a);cpu.set_reg16(3,8);return;}
    std::ostringstream s;s<<"BIOS INT 10h función 0x"<<std::hex<<unsigned(fn)<<" pendiente";missing(s.str());
  }
  void memory_bios(){
    if(cpu.regs[0]==0xe820){
      if(cpu.regs[2]!=0x534d4150||cpu.regs[1]<20){carry(true);return;}
      // Physical address space includes the reserved framebuffer aperture.
      // Never offer those pages to a guest OS as general-purpose RAM.
      const bool aperture=PcVideo::available(cpu.memory);
      const std::uint64_t after_video=PcVideo::kFramebufferAddress+PcVideo::kApertureBytes;
      const std::uint64_t starts[]={0,0xa0000,0x100000,PcVideo::kFramebufferAddress,after_video};
      const std::uint64_t lengths[]={0xa0000,0x60000,(aperture?PcVideo::kFramebufferAddress:cpu.memory.size())-0x100000,PcVideo::kApertureBytes,aperture?cpu.memory.size()-after_video:0};
      const std::uint64_t entries=aperture?(lengths[4]?5:4):3;
      const auto index=cpu.regs[3];if(index>=entries){carry(true);return;}
      const auto p=physical(0,cpu.reg16(7));const auto n=cpu.regs[1]>=24?24:20;
      cpu.write32(p,starts[index]&0xffffffff);cpu.write32(p+4,starts[index]>>32);cpu.write32(p+8,lengths[index]&0xffffffff);cpu.write32(p+12,lengths[index]>>32);cpu.write32(p+16,index==1||index==3?2:1);if(n==24)cpu.write32(p+20,1);
      cpu.regs[0]=0x534d4150;cpu.regs[1]=n;cpu.regs[3]=index+1<entries?index+1:0;carry(false);return;
    }
    if(cpu.reg16(0)==0xe801){const auto top=PcVideo::available(cpu.memory)?PcVideo::kFramebufferAddress:cpu.memory.size();const auto k=(top-0x100000)/1024;cpu.set_reg16(0,std::min<std::size_t>(15360,k));cpu.set_reg16(3,k>15360?(k-15360)/64:0);cpu.set_reg16(1,cpu.reg16(0));cpu.set_reg16(2,cpu.reg16(3));carry(false);return;}
    if(hi(0)==0x88){cpu.set_reg16(0,std::min<std::size_t>(0xfc00,(cpu.memory.size()-0x100000)/1024));carry(false);return;}
    if(hi(0)==0x24){switch(lo(0)){case 0:cpu.a20_enabled=false;high(0,0);carry(false);return;case 1:cpu.a20_enabled=true;high(0,0);carry(false);return;case 2:low(0,cpu.a20_enabled?1:0);high(0,0);carry(false);return;case 3:cpu.set_reg16(3,2);high(0,0);carry(false);return;default:break;}}
    // Unsupported BIOS extensions advertise failure as required by this API.
    high(0,0x86);carry(true);
  }
  bool bios(std::uint8_t n){
    switch(n){
      case 0x10:video_bios();return true;
      case 0x11:cpu.set_reg16(0,cpu.read16(0x410));return true;
      case 0x12:cpu.set_reg16(0,640);return true;
      case 0x13:disk_bios();return true;
      case 0x15:memory_bios();return true;
      case 0x16:{const auto fn=hi(0);if(fn==0||fn==0x10){if(keys_.empty())waiting_key=true;else{cpu.set_reg16(0,keys_.front());keys_.pop_front();}return true;}if(fn==1||fn==0x11){zero(keys_.empty());if(!keys_.empty())cpu.set_reg16(0,keys_.front());return true;}if(fn==2){low(0,0);return true;}missing("Servicio de teclado BIOS no implementado");}
      case 0x1a:if(hi(0)==0){const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started_).count();const std::uint32_t ticks=ms*182/10000;cpu.set_reg16(1,ticks>>16);cpu.set_reg16(2,ticks&0xffff);low(0,0);carry(false);return true;}carry(true);return true;
      case 0x18:missing("BIOS: el invitado no encontró un sistema arrancable (INT 18h)");
      case 0x19:missing("BIOS: el invitado solicita reiniciar el arranque (INT 19h); usa Reiniciar");
      default:{std::ostringstream s;s<<"Interrupción 0x"<<std::hex<<unsigned(n)<<" pendiente";missing(s.str());}
    }
  }
  std::optional<std::uint32_t> input(std::uint16_t p,unsigned width){
    if(p==PcQuantumDevice::data_port||p==PcQuantumDevice::control_port)return quantum_device.input(p,width);
    if(width!=1)return std::nullopt;
    if(p==0x92)return cpu.a20_enabled?2u:0u;
    if(p==0x21)return pic1_;
    if(p==0xa1)return pic2_;
    if(p==0x64)return 0u;
    if(p==0x71){if(cmos_index_==0x0a)return 0x26u;if(cmos_index_==0x0b)return 2u;if(cmos_index_==0x0d)return 0x80u;return std::nullopt;}
    if(p==0x3fd)return 0x60u;
    return std::nullopt;
  }
  bool output(std::uint16_t p,unsigned width,std::uint32_t v){
    if(p==PcQuantumDevice::data_port||p==PcQuantumDevice::control_port)return quantum_device.output(p,width,v);
    if(width!=1)return false;
    if(p==0x92){if(v&1)missing("Reinicio por puerto A20 pendiente");cpu.a20_enabled=v&2;return true;}
    if(p==0x21){pic1_=v;return true;}if(p==0xa1){pic2_=v;return true;}
    if(p==0x70){cmos_index_=v&0x7f;return true;}
    if(p==0x80)return true; // POST delay port has no guest-visible output.
    if(p==0x3f8){if(serial_.size()<65536)serial_+=char(v&255);return true;}
    return false;
  }
  std::string serial_;
};
}
