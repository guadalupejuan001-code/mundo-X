#pragma once
#include "x86_cpu.hpp"
#include "boot_media.hpp"
#include "boot_hotspots.hpp"
#include "demo_boot.hpp"
#include "long_mode_guest.hpp"
#include "quantum_device.hpp"
#include "mundo_x/autonomous_core_c02.hpp"
#include <future>
#include "quantum_guest.hpp"
#include "pc_video.hpp"
#include "graphics_demo.hpp"
#include "pc_8042.hpp"
#include "pc_bios_keyboard.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <functional>
#include <iomanip>
#include <limits>
#include <memory>
#include <optional>
#include <sstream>
#include <string_view>
#include <utility>

namespace cosmic_pc {
// Original BIOS service model and PC integration. This is deliberately explicit
// about the hardware that exists; missing BIOS functions stop instead of lying.
class PcMachine {
 public:
  using Reader=std::function<bool(std::uint64_t,std::uint8_t*,std::size_t)>;
  enum class GuestRestart { None, A20Port, KeyboardController, BiosBootstrap };
  struct BootModeProgress {
    std::uint64_t changes=0, step=0;
    std::string_view from, to;
  };

  // The owner may flush storage and select boot media once run() returns.
  void enable_guest_restart() noexcept { guest_restart_enabled_=true; }
  GuestRestart guest_restart() const noexcept { return guest_restart_; }
  const BootModeProgress& boot_mode_progress() const noexcept { return boot_mode_progress_; }
  // A BIOS bridge may provide DL=80h while the current boot source is DL=E0h.
  void set_bios_hard_disk_attached(bool attached) {
    bios_hard_disk_attached_=attached;
    if(reader_)sync_bios_hard_disk_count();
  }
  // One page at the unused tail of the reserved 128-MiB VBE aperture stays
  // outside normal guest RAM. Its first 256 bytes are backed by Ether Memory.
  // The maximum 8K framebuffer ends 1,507,328 bytes before this page.
  static constexpr std::uint64_t kEtherMemoryPhysicalBase =
      PcVideo::kFramebufferAddress + PcVideo::kApertureBytes - 4096;
  static constexpr std::size_t kEtherMemoryBytes = 256;
  static constexpr std::uint64_t kEtherMemoryLogicalByteBase = 1000000000000ULL;
  static constexpr std::uint64_t kMaxPhysicalBytes = 1ULL << 32;
  static constexpr std::uint64_t kIoApicAddress = 0xfec00000ULL;
  static constexpr std::uint64_t kLocalApicAddress = Cpu::kLocalApicAddress;
  static constexpr std::uint64_t max_ram_mib() noexcept {
    return std::min<std::uint64_t>(kMaxPhysicalBytes, std::numeric_limits<std::size_t>::max()) >> 20;
  }
  Cpu cpu;
  const std::shared_ptr<mundo_x::InfiniteSession> quantum_session;
  PcQuantumDevice quantum_device;
  std::shared_ptr<mundo_x::AutonomousCore> quantum_scheduler_;
  bool waiting_key=false;
  std::string host_error, boot_kind="none", pending;
  std::uint64_t disk_reads=0;
  struct BootAcceleration {
    std::uint64_t batches=0, rep_elements=0, bytes=0;
    std::uint64_t paged_rep_elements=0;
    std::uint64_t shift_loop_batches=0, shift_loop_steps=0;
    std::uint64_t zero_loop_batches=0, zero_loop_steps=0;
    std::uint64_t scan_loop_steps=0;
  };
  void enable_boot_acceleration(bool enabled=true) noexcept { boot_acceleration_enabled_=enabled; }
  bool boot_acceleration_enabled() const noexcept { return boot_acceleration_enabled_; }
  const BootAcceleration& boot_acceleration() const noexcept { return boot_acceleration_; }
  void enable_boot_hotspots(bool enabled=true) {
    if (enabled) {
      boot_hotspots_.emplace();
      boot_scalar_hotspots_.emplace(true);
    } else {
      boot_hotspots_.reset();
      boot_scalar_hotspots_.reset();
    }
  }
  const std::optional<BootHotspots>& boot_hotspots() const noexcept { return boot_hotspots_; }
  const std::optional<BootHotspots>& boot_scalar_hotspots() const noexcept { return boot_scalar_hotspots_; }
  void enable_boot_timing(bool enabled = true) {
    if (enabled) {
      boot_regions_.emplace(true, true);
      boot_timing_.emplace();
    } else {
      boot_regions_.reset();
      boot_timing_.reset();
    }
  }
  const std::optional<BootTiming>& boot_timing() const noexcept { return boot_timing_; }
  const std::optional<BootHotspots>& boot_regions() const noexcept { return boot_regions_; }
  // Optional external motherboard clock/IRQ polling hook. C06 owners must
  // operate on the SAME Cpu instance; reset() deliberately preserves hooks.
  std::function<void()> device_step_hook;
  // After the usual poll, a device can grant a batch with no observable
  // transition. The advance callback receives only the additional steps.
  std::function<unsigned(unsigned)> device_batch_limit_hook;
  std::function<void(unsigned)> device_batch_advance_hook;
  // Invoked only after reset finished reloading RAM, boot sector and BIOS BDA.
  // A motherboard can then restore ACPI tables and power-on device registers.
  std::function<void()> device_reset_hook;
  // The hooks capture this address; instances must remain at a stable address.
  PcMachine(const PcMachine&)=delete;
  PcMachine& operator=(const PcMachine&)=delete;
  PcMachine(PcMachine&&)=delete;
  PcMachine& operator=(PcMachine&&)=delete;
  static std::size_t checked_ram(std::size_t bytes) {
    if (bytes < 2 * 1024 * 1024 || std::uint64_t(bytes) > kMaxPhysicalBytes)
      throw std::invalid_argument("El espacio físico de la RAM virtual debe estar entre 2 MiB y 4 GiB.");
    return bytes;
  }
  std::uint64_t usable_ram_bytes() const noexcept {
    std::uint64_t bytes = 0;
    for (const auto& region : bios_memory_map().regions)
      if (region.type == 1) bytes += region.length;
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
    if(cpu.memory.size()>=kEtherMemoryPhysicalBase+kEtherMemoryBytes)
      cpu.memory.attach_window(
          kEtherMemoryPhysicalBase,kEtherMemoryBytes,
          [this](std::uint64_t offset){
            return quantum_session->qload8(std::to_string(kEtherMemoryLogicalByteBase+offset));
          },
          [this](std::uint64_t offset,std::uint8_t value){
            quantum_session->qstore8(std::to_string(kEtherMemoryLogicalByteBase+offset),value);
            quantum_cache_valid_=false;
          });
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
    host_error.clear();pending.clear();waiting_key=false;disk_reads=0;last_disk_status_=0;
    guest_restart_=GuestRestart::None;
    boot_mode_progress_={};
    boot_acceleration_={};
    if(boot_hotspots_)boot_hotspots_->reset();
    if(boot_scalar_hotspots_)boot_scalar_hotspots_->reset();
    if(boot_timing_)boot_timing_->reset();
    if(boot_regions_)boot_regions_->reset();
    next_ram_compaction_=kRamCompactionInterval;
    video_mode_=3;video_.text_mode(3);pic1_=pic2_=0xff;cmos_index_=0;serial_.clear();
    kbc_.reset();  // Controller, command/data latches and A20 protocol reset together.
    if(!reader_)return;
    cpu.memory.reset_zero();
    video_.reset(cpu.memory);
    cpu.reset(plan_.initial_cs,plan_.initial_ip);cpu.a20_enabled=true;
    cpu.segments[0]=cpu.segments[2]=cpu.segments[3]=0;
    cpu.set_reg16(4,0x7c00);cpu.set_reg16(2,plan_.drive);cpu.eflags=0x202;
    cpu.memory.write_block(std::size_t(plan_.load_segment)*16,plan_.image.data(),plan_.image.size());
    // BIOS data area: video mode 3, 80 columns, page0, conventional RAM.
    cpu.write16(0x410,0x0020);cpu.write16(0x413,640);cpu.write8(0x449,3);cpu.write16(0x44a,80);cpu.write16(0x44c,4000);
    PcBiosKeyboard::reset(cpu);
    sync_bios_font_geometry();video_.install_font_vectors(cpu);
    sync_bios_hard_disk_count();clear_screen();started_=std::chrono::steady_clock::now();if(device_reset_hook)device_reset_hook();
  }
  void run(unsigned budget) {
    if(!reader_||!host_error.empty()||waiting_key||cpu.fault||guest_restart_!=GuestRestart::None)return;
    if(cpu.halted&&!device_step_hook)return;
    budget=std::min(budget,50000u);
    const BootTiming::Scope run_timing(boot_timing_ ? &*boot_timing_ : nullptr, BootTimePart::Run);
    const auto start=boot_hotspots_?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
    const char* last_mode=cpu.mode_name();
    const auto observe_mode=[&]() {
      const char* mode=cpu.mode_name();
      if(mode!=last_mode) {
        if(std::string_view(mode)!=last_mode)
          boot_mode_progress_={boot_mode_progress_.changes+1,cpu.instructions,last_mode,mode};
        last_mode=mode;
      }
    };
    for(unsigned i=0;i<budget&&!waiting_key;){
      auto* sample = boot_timing_ ? boot_timing_->next_iteration() : nullptr;
      const BootTiming::Scope iteration_timing(sample, BootTimePart::Total);
      // Devices can tick and deliver an enabled IRQ between guest instructions;
      // a pending timer IRQ may wake a halted CPU without a synthetic opcode.
      if(device_step_hook) {
        {
          const BootTiming::Scope device_timing(sample, BootTimePart::Device);
          device_step_hook();
        }
        observe_mode();
      }
      if(cpu.fault) {
        pending="La placa base virtual no pudo entregar la interrupción.";
        break;
      }
      const auto cs=cpu.segments[Cpu::CS];
      const auto ip=cpu.eip;
      const auto* mode=last_mode;
      const auto before=cpu.instructions;
      if(cpu.halted)cpu.advance_halted_clock();
      if(boot_acceleration_enabled_&&budget-i>1) {
        const auto batch_start=sample ? BootTiming::Clock::now() : BootTiming::Clock::time_point{};
        const auto completed=run_boot_batch(budget-i);
        if(sample)sample->record(completed ? BootTimePart::BatchExecute : BootTimePart::BatchDispatch,
                                BootTiming::Clock::now()-batch_start);
        if(completed) {
          if(sample)sample->add_steps(0,completed);
          sample_boot(mode,cs,ip,completed);
          i+=completed;
          continue;
        }
      }
      const bool time_step=boot_scalar_hotspots_&&!cpu.halted&&boot_scalar_hotspots_->wants_sample();
      const auto step_start=time_step?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
      StepResult r;
      if(sample) {
        const auto start=BootTiming::Clock::now();
        r=cpu.step_with_timing(*sample);
        const auto elapsed=BootTiming::Clock::now()-start;
        sample->add_steps(cpu.instructions-before,0);
        sample->record(BootTimePart::Scalar,elapsed);
        if(cpu.instructions!=before)
          boot_regions_->observe_region(mode,cs,ip,std::chrono::duration_cast<std::chrono::nanoseconds>(elapsed).count());
      } else {
        r=cpu.step();
      }
      const auto elapsed_ns=time_step?std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now()-step_start).count():0;
      const auto steps=cpu.instructions-before;
      if(boot_scalar_hotspots_&&steps)
        if(const auto samples=boot_scalar_hotspots_->advance(steps))
          boot_scalar_hotspots_->record(mode,cs,ip,cpu.last_decoded_bytes(),samples,elapsed_ns);
      sample_boot(mode,cs,ip,steps);
      ++i;
      observe_mode();
      if(r==StepResult::Faulted){guest_restart_=GuestRestart::None;pending="Consulta el diagnóstico de la CPU y la memoria. Windows completo sigue pendiente.";break;}
      if(guest_restart_!=GuestRestart::None)break;
      if((r==StepResult::Halted||r==StepResult::WaitingForX87)&&!device_step_hook)break;
    }
    if(boot_acceleration_enabled_&&cpu.instructions>=next_ram_compaction_) {
      const BootTiming::Scope compaction_timing(boot_timing_ ? &*boot_timing_ : nullptr, BootTimePart::Compaction);
      cpu.memory.compact_repeating_pages(256);
      next_ram_compaction_=cpu.instructions+kRamCompactionInterval;
    }
    if(boot_hotspots_)boot_hotspots_->add_elapsed(std::chrono::steady_clock::now()-start);
  }
  void key(std::uint8_t ascii, std::uint8_t scan) { key_event(ascii, scan, true); }
  void key_event(std::uint8_t ascii, unsigned scan, bool pressed) {
    if (!reader_ || cpu.fault) return;
    const auto key = PcBiosKeyboard::host_key(cpu, ascii, scan, pressed);
    if (!key || !PcBiosKeyboard::store(cpu, *key) || !waiting_key) return;
    // An empty blocking INT has already retired. Its AH still selects the
    // original read service until a compatible key completes the call.
    if (const auto word = PcBiosKeyboard::read(cpu, hi(Cpu::EAX) == 0x10, true)) {
      cpu.set_reg16(Cpu::EAX, *word);
      waiting_key = false;
    }
  }
  // A single C01 worker owns only the dispatch policy. PC memory windows, GUI
  // and guest ports continue sharing the exact original InfiniteSession.
  void quantum_autonomous_enable(bool enabled) {
    if (enabled == bool(quantum_scheduler_)) return;
    if (enabled) {
      auto scheduler = std::make_shared<mundo_x::AutonomousCore>(quantum_session);
      quantum_device.use_scheduler(scheduler);
      quantum_scheduler_ = std::move(scheduler);
    } else {
      quantum_device.use_scheduler({});
      quantum_scheduler_->shutdown();
      quantum_scheduler_.reset();
    }
  }

  // Run 12 copies of the current program against the current *private*
  // checkpoint per worker-count; never promote workers into the live PC owner.
  void quantum_workers_benchmark(const std::string& program) {
    try {
      if (program.empty() || program.size() > PcQuantumDevice::max_request_bytes)
        throw std::length_error("Programa XI para C02 fuera de 1..65536 bytes");
      const auto original = quantum_session->snapshot();
      const auto background = quantum_session->background();
      std::ostringstream report;
      report << "C02 · XI aislado (12 tareas por configuración)";
      for (unsigned count : {1u, 2u, 4u}) {
        mundo_x::AutonomousCoreC02::Options options;
        options.workers = count;
        mundo_x::AutonomousCoreC02 pool(options, {}, {}, background, quantum_session->limits());
        pool.import_idle_snapshot(original);
        const auto begin = std::chrono::steady_clock::now();
        std::vector<std::future<mundo_x::AutonomousCoreC02::Outcome>> tasks;
        for (unsigned job = 0; job < 12; ++job)
          tasks.push_back(pool.submit(program, mundo_x::AutonomousCoreC02::Priority::Normal,
                                      mundo_x::AutonomousCoreC02::Mode::Isolated));
        std::string first_response;
        bool have_first = false;
        for (auto& task : tasks) {
          const auto result = task.get();
          if (!result.ok || result.committed || !result.isolated)
            throw std::runtime_error("C02 isolated XI failed: " + result.error);
          if (have_first && first_response != result.output)
            throw std::runtime_error("C02 workers returned different deterministic XI outputs");
          first_response = result.output;
          have_first = true;
        }
        const auto elapsed = std::chrono::duration<double, std::milli>(
            std::chrono::steady_clock::now() - begin).count();
        if (pool.snapshot() != original)
          throw std::runtime_error("C02 modified its own authority in isolated mode");
        const auto metrics = pool.telemetry();
        if (metrics.completed != 12 || metrics.active != 0 || metrics.reserved_program_bytes != 0)
          throw std::runtime_error("C02 lost tasks or memory reservations");
        report << "\n" << count << " workers: " << std::fixed << std::setprecision(2)
               << elapsed << " ms · " << (elapsed > 0 ? 12000.0 / elapsed : 0.0)
               << " tareas/s · pico " << metrics.peak_active;
      }
      if (quantum_session->snapshot() != original)
        throw std::runtime_error("PC live state changed during isolated benchmark");
      quantum_output_ = report.str();
      quantum_error_.clear();
      quantum_truncated_ = false;
    } catch (const std::exception& error) { quantum_error_ = error.what(); }
  }

  // Host commands use the same session as guest ports, but do not consume or
  // replace the guest's in-progress request/response. Expected errors are local.
  void quantum_command(const std::string& program) {
    try {
      if(program.size()>PcQuantumDevice::max_request_bytes)
        throw std::length_error("Quantum request exceeds 65536 bytes");
      std::string output;
      if (quantum_scheduler_) {
        auto task = quantum_scheduler_->submit(program, mundo_x::AutonomousCore::Priority::Interactive).get();
        if (!task.ok) throw std::runtime_error(task.error);
        output = std::move(task.output);
      } else {
        output = quantum_session->execute(program);
      }
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
    o<<"{\"engine\":\"InfiniteCore\",\"scheduler_c01\":"<<(quantum_scheduler_?"true":"false")
     <<",\"isolated_workers_c02_available\":true,\"background\":"<<quote(quantum_session->background()==mundo_x::InfiniteCore::Background::Zero?"zero":"bell")
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
    const auto kbc_metrics=kbc_.metrics();
    const std::string message=!host_error.empty()?host_error:(cpu.fault?cpu.fault->describe():(!reader_?"Selecciona una ISO, disco o arranque de prueba":waiting_key?"Esperando entrada de teclado BIOS":cpu.halted?"El invitado ejecutó HLT":std::string("CPU propia: ")+cpu.mode_name()));
    o<<"{\"status\":"<<quote(status())<<",\"instructions\":"<<cpu.instructions<<",\"cs\":"<<cpu.segments[1]<<",\"ip\":"<<cpu.eip<<",\"flags\":"<<cpu.eflags<<",\"regs\":[";
    for(unsigned i=0;i<cpu.regs.size();++i){if(i)o<<',';o<<cpu.regs[i];}
    o<<"],\"regs_hex\":[";
    for(unsigned i=0;i<cpu.regs.size();++i){if(i)o<<',';o<<quote(hex64(cpu.regs[i]));}
    o<<"],\"rip_hex\":"<<quote(hex64(cpu.eip))<<",\"cpu_mode\":"<<quote(cpu.mode_name())
     <<",\"message\":"<<quote(message)<<",\"boot_kind\":"<<quote(boot_kind)<<",\"pending\":"<<quote(pending)
     <<",\"disk_reads\":"<<disk_reads<<",\"ram_bytes\":"<<cpu.memory.size()
     <<",\"ram_usable_bytes\":"<<usable_ram_bytes()
     <<",\"ram_allocated_bytes\":"<<cpu.memory.allocated_bytes()<<",\"ram_budget_bytes\":"<<cpu.memory.budget_bytes()
     <<",\"ram_metadata_bytes\":"<<cpu.memory.metadata_bytes()<<",\"ram_allocated_pages\":"<<cpu.memory.allocated_pages()
     <<",\"ram_pattern_pages\":"<<cpu.memory.pattern_pages()
     <<",\"ether_memory_physical_base\":"<<(cpu.memory.has_window()?kEtherMemoryPhysicalBase:0)
     <<",\"ether_memory_bytes\":"<<cpu.memory.window_bytes()
     <<",\"ether_memory_logical_byte_base\":"<<(cpu.memory.has_window()?kEtherMemoryLogicalByteBase:0)
     <<",\"video_kind\":"<<quote(video.graphics?"vbe32":"text")<<",\"video_mode\":"<<video.mode
     <<",\"video_width\":"<<video.width<<",\"video_height\":"<<video.height<<",\"video_pitch\":"<<video.pitch
     <<",\"video_bytes\":"<<video.byte_size<<",\"video_address\":"<<video.physical_address
     <<",\"video_format\":"<<quote(video.graphics?"BGRX8888":"VGA-char-attribute")
     <<",\"video_aperture_bytes\":"<<(PcVideo::available(cpu.memory)?PcVideo::kApertureBytes:0)
     <<",\"ram_policy\":\"zero-pages-on-demand+ether-window\""
     <<",\"ram_repeating_compaction\":"<<(boot_acceleration_enabled_?"true":"false")
     <<",\"engine\":\"cosmic-pc-own-x86\""
     <<",\"page_walks\":"<<cpu.page_walks<<",\"instruction_page_hits\":"<<cpu.instruction_page_hits
     <<",\"page_translation_hits\":"<<cpu.page_translation_hits;
    if(cpu.fault) {
      o<<",\"fault_context\":{\"cpl\":"<<cpu.cpl()
       <<",\"ss\":"<<cpu.segments[Cpu::SS]
       <<",\"cr0_hex\":"<<quote(hex64(cpu.cr0))
       <<",\"cr2_hex\":"<<quote(hex64(cpu.cr2))
       <<",\"cr3_hex\":"<<quote(hex64(cpu.cr3))
       <<",\"cr4_hex\":"<<quote(hex64(cpu.cr4))
       <<",\"efer_hex\":"<<quote(hex64(cpu.efer))
       <<",\"idtr_base_hex\":"<<quote(hex64(cpu.idtr.base))
       <<",\"idtr_limit\":"<<cpu.idtr.limit
       <<",\"linear_address_hex\":"<<quote(hex64(cpu.fault->linear_address))
       <<",\"error_code\":"<<cpu.fault->error_code<<'}';
    }
    o
     <<",\"system_registers\":{\"ldtr_selector\":"<<cpu.ldtr.selector
     <<",\"ldtr_valid\":"<<(cpu.ldtr.valid?"true":"false")
     <<",\"ldtr_base\":"<<cpu.ldtr.base
     <<",\"ldtr_limit\":"<<cpu.ldtr.limit
     <<",\"tr_selector\":"<<cpu.tr.selector
     <<",\"tr_valid\":"<<(cpu.tr.valid?"true":"false")
     <<",\"tr_base\":"<<cpu.tr.base
     <<",\"tr_limit\":"<<cpu.tr.limit
     <<",\"task_switching_implemented\":false}"

     <<",\"i8042\":{\"command_writes\":"<<kbc_metrics.command_writes
     <<",\"data_writes\":"<<kbc_metrics.data_writes
     <<",\"status_reads\":"<<kbc_metrics.status_reads
     <<",\"data_reads\":"<<kbc_metrics.data_reads
     <<",\"last_command\":"<<unsigned(kbc_metrics.last_command)
     <<",\"command_byte\":"<<unsigned(kbc_.config_byte())
     <<",\"pending_payload\":"<<unsigned(kbc_.pending())
     <<",\"reply_ready\":"<<(kbc_.has_reply()?"true":"false")
     <<",\"a20_enabled\":"<<(cpu.a20_enabled?"true":"false")
     <<"}"
     <<",\"windows_ready\":false,\"graphics_3d_ready\":false,\"quantum\":"<<quantum_json()<<"}";
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
  Pc8042 kbc_;  // Owned subset of physical PC/AT 8042; no simulated IRQs.
  bool guest_restart_enabled_=false;
  bool bios_hard_disk_attached_=false;
  GuestRestart guest_restart_=GuestRestart::None;
  BootModeProgress boot_mode_progress_;
  BootAcceleration boot_acceleration_;
  bool boot_acceleration_enabled_=false;
  std::optional<BootHotspots> boot_hotspots_;
  std::optional<BootHotspots> boot_scalar_hotspots_;
  std::optional<BootTiming> boot_timing_;
  std::optional<BootHotspots> boot_regions_;
  static constexpr std::uint64_t kRamCompactionInterval=32768;
  std::uint64_t next_ram_compaction_=kRamCompactionInterval;
  unsigned run_boot_batch(unsigned limit) {
    const bool rep=cpu.rep_batch_candidate();
    const bool shift=!rep&&cpu.shift_loop_batch_candidate();
    const bool scan=!rep&&!shift&&cpu.scan_loop_batch_candidate();
    if(!rep&&!shift&&!scan&&!cpu.zero_loop_batch_candidate())return 0;
    if(device_step_hook) {
      if(!device_batch_limit_hook||!device_batch_advance_hook)return 0;
      limit=std::min(limit,device_batch_limit_hook(limit));
    }
    // A loop contains different opcodes. Execute the sampled step
    // individually so its address and bytes remain exact.
    if(!rep&&boot_hotspots_)limit=std::min(limit,boot_hotspots_->steps_before_sample());
    if(limit<2)return 0;
    unsigned completed=0;
    if(rep)completed=cpu.try_rep_batch(limit);
    else if(shift)completed=cpu.try_shift_loop_batch(limit);
    else if(scan)completed=cpu.try_scan_loop_batch(limit);
    else completed=cpu.try_zero_loop_batch(limit);
    if(!completed)return 0;
    if(device_step_hook)device_batch_advance_hook(completed-1);
    if(rep) {
      ++boot_acceleration_.batches;
      boot_acceleration_.rep_elements+=completed;
      if(cpu.cr0&0x80000000U)boot_acceleration_.paged_rep_elements+=completed;
      boot_acceleration_.bytes+=cpu.last_rep_batch_bytes();
    } else if(shift) {
      ++boot_acceleration_.shift_loop_batches;
      boot_acceleration_.shift_loop_steps+=completed;
    } else if(scan) {
      boot_acceleration_.scan_loop_steps+=completed;
    } else {
      ++boot_acceleration_.zero_loop_batches;
      boot_acceleration_.zero_loop_steps+=completed;
    }
    return completed;
  }
  void sample_boot(const char* mode,std::uint16_t cs,std::uint64_t ip,std::uint64_t steps) noexcept {
    if(!boot_hotspots_||!steps)return;
    if(const auto samples=boot_hotspots_->advance(steps))
      boot_hotspots_->record(mode,cs,ip,cpu.last_decoded_bytes(),samples);
  }
  Reader reader_;
  std::uint64_t media_bytes_=0;
  BootPlan plan_{};
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
  std::uint32_t physical(unsigned segment,std::uint16_t off)const{return cpu.real_mode_segment_address(segment,off);}
  void clear_screen(){row_=col_=0;for(unsigned i=0;i<2000;++i){cpu.memory[0xb8000+i*2]=' ';cpu.memory[0xb8001+i*2]=7;}sync_cursor();}
  void sync_cursor(){cpu.memory[0x450]=col_;cpu.memory[0x451]=row_;}
  template <class Write, class Scroll>
  static void emit_text(std::uint8_t character,std::uint8_t attribute,
                        unsigned& row,unsigned& column,Write write,Scroll scroll){
    switch(character){
      case '\r':column=0;break;
      case '\n':++row;break;
      case 8:if(column)--column;break;
      case 7:break;
      default:
        write(row*80+column,std::uint16_t(attribute<<8)|character);
        if(++column==80){column=0;++row;}
    }
    if(row==25){scroll();row=24;}
  }
  void emit(std::uint8_t character){
    auto row=row_,column=col_;
    emit_text(character,7,row,column,
      [&](unsigned cell,std::uint16_t value){cpu.write16(0xb8000+cell*2,value);},
      [&]{
        for(unsigned i=0;i<24*80;++i)cpu.write16(0xb8000+i*2,cpu.read16(0xb8000+(i+80)*2));
        for(unsigned i=24*80;i<25*80;++i)cpu.write16(0xb8000+i*2,0x0720);
      });
    row_=row;col_=column;
    sync_cursor();
  }
  void video_write_string(){ // INT 10h AH=13h, the supported VGA text page.
    const unsigned mode=lo(Cpu::EAX),count=cpu.reg16(Cpu::ECX);
    auto row=unsigned(hi(Cpu::EDX)),column=unsigned(lo(Cpu::EDX));
    if(video_.info().graphics||hi(Cpu::EBX)||mode>3||row>=25||column>=80)
      missing("BIOS de vídeo: modo, cursor o página inválidos para escribir una cadena");
    const unsigned byte_width=(mode&2)?2:1;
    const unsigned offset=cpu.reg16(Cpu::EBP);
    if(count*byte_width>0x10000U-offset)
      missing("BIOS de vídeo: la cadena rebasa ES:BP");

    // Compose the text update locally so repeated scrolling commits at most
    // one VGA page. Validate source bytes through the guest bus, including A20.
    std::array<std::uint16_t,80*25> cells{};
    for(unsigned i=0;i<cells.size();++i)cells[i]=cpu.read16(0xb8000+i*2);
    const auto original=cells;
    for(unsigned i=0;i<count;++i){
      const auto source=physical(Cpu::ES,std::uint16_t(offset+i*byte_width));
      const auto character=cpu.read8(source);
      const auto attribute=(mode&2)?cpu.read8(source+1):lo(Cpu::EBX);
      emit_text(character,attribute,row,column,
        [&](unsigned cell,std::uint16_t value){cells[cell]=value;},
        [&]{
          std::copy(cells.begin()+80,cells.end(),cells.begin());
          std::fill(cells.end()-80,cells.end(),0x0720);
        });
    }
    for(unsigned i=0;i<cells.size();++i)
      if(cells[i]!=original[i])cpu.write16(0xb8000+i*2,cells[i]);
    if(mode&1){row_=row;col_=column;sync_cursor();}
  }
  [[noreturn]] void missing(const std::string& s){pending=s;cpu.fail(s);}
  void sync_bios_hard_disk_count(){cpu.write8(0x475,bios_hard_disk_attached_||plan_.drive==0x80?1:0);}
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
    const auto fn=hi(0);
    const bool optical=plan_.logical_sector_size==BootMedia::kCdSectorSize;
    const bool el_torito_all=optical&&fn==0x4b&&lo(2)==0x7f;
    if(lo(2)!=plan_.drive&&!el_torito_all){disk_result(1);return;}
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
      if(optical){disk_result(1);return;}
      const auto total=media_bytes_/512, cylinders=std::max<std::uint64_t>(1,std::min<std::uint64_t>(1024,(total+1007)/1008));
      const auto maxc=unsigned(cylinders-1);high(1,maxc&255);low(1,63|((maxc>>2)&0xc0));high(2,15);low(2,1);low(3,0);disk_result(0);return;
    }
    if(fn==0x15){
      disk_result(0);
      high(0,optical?1:3);
      if(!optical){const auto sectors=std::min<std::uint64_t>(media_bytes_/512,0xffffffffULL);cpu.set_reg16(1,sectors>>16);cpu.set_reg16(2,sectors&0xffff);}
      return;
    }
    if(fn==0x41){if(cpu.reg16(3)!=0x55aa){disk_result(1);return;}disk_result(0);cpu.set_reg16(3,0xaa55);cpu.set_reg16(1,1);high(0,0x30);return;}
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
      const bool chs=!optical;
      const auto cylinders=std::max<std::uint64_t>(1,std::min<std::uint64_t>(1024,(media_bytes_/512+1007)/1008));
      cpu.write16(p,26);cpu.write16(p+2,chs?2:4);cpu.write32(p+4,chs?cylinders:0);cpu.write32(p+8,chs?16:0);cpu.write32(p+12,chs?63:0);
      const auto n=media_bytes_/plan_.logical_sector_size;cpu.write32(p+16,n&0xffffffff);cpu.write32(p+20,n>>32);cpu.write16(p+24,plan_.logical_sector_size);disk_result(0);return;
    }
    if(fn==0x4b&&optical){
      // El Torito status/terminate uses AL=1 to query; no emulation to terminate.
      if(lo(0)>1){disk_result(1);return;}
      const auto p=physical(3,cpu.reg16(6));for(unsigned i=0;i<19;++i)cpu.write8(p+i,0);cpu.write8(p,0x13);cpu.write8(p+2,plan_.drive);cpu.write32(p+4,plan_.source_offset/2048);cpu.write16(p+12,plan_.load_segment);cpu.write16(p+14,plan_.image.size()/512);disk_result(0);return;
    }
    disk_result(1); // The guest may probe optional BIOS disk services.
  }
  void video_bios(){
    const auto fn=hi(0);
    if(fn==0){const auto mode=lo(0)&127;if(mode!=2&&mode!=3)missing("Modo VGA pendiente; gráficos disponibles por VBE INT 10h 4F02");set_bios_text_mode(mode,lo(0)&0x80);return;}
    if(fn==0x4f){
      const auto request=cpu.reg16(3);
      const auto mode=request&0x7fff;
      if(PcVideo::available(cpu.memory)&&cpu.reg16(0)==0x4f02&&(mode==2||mode==3)){
        set_bios_text_mode(mode,request&0x8000);
        cpu.set_reg16(0,0x004f);return;
      }
      const bool set_mode=cpu.reg16(0)==0x4f02;
      // Reserve the BDA page before a VBE mode change publishes new geometry.
      if(set_mode&&cpu.memory.has_page_backing()&&PcVideo::available(cpu.memory))
        cpu.memory.prepare_write(0x449,1);
      video_.bios(cpu);
      if(set_mode&&cpu.reg16(0)==0x004f&&video_.info().graphics){
        video_mode_=0xff;cpu.write8(0x449,0xff);sync_bios_font_geometry();
      }
      return;
    }
    if(cpu.reg16(Cpu::EAX)==0x1130){video_.bios_font_info(cpu);return;}
    if(fn==0x0e){emit(lo(0));return;}
    if(fn==0x13){video_write_string();return;}
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
  void set_bios_text_mode(std::uint8_t mode,bool preserve){
    if(!preserve&&cpu.memory.has_page_backing())cpu.memory.prepare_write(0xb8000,4000);
    cpu.write8(0x449,mode);
    video_mode_=mode;
    video_.text_mode(mode,preserve);
    sync_bios_font_geometry();
    if(!preserve)clear_screen();
  }
  void sync_bios_font_geometry(){
    const auto info=video_.info();
    const auto rows=info.graphics?info.height/PcVideo::kBiosFontHeight:info.height;
    cpu.write8(0x484,std::uint8_t(std::min(rows-1,255U)));
    cpu.write16(0x485,PcVideo::kBiosFontHeight);
  }
  struct BiosMemoryRegion { std::uint64_t base, length; std::uint32_t type; };
  struct BiosMemoryMap {
    std::array<BiosMemoryRegion, 6> regions{};
    std::size_t count = 0;
    void append(std::uint64_t base, std::uint64_t end, std::uint32_t type) noexcept {
      if (end > base) regions[count++] = {base, end - base, type};
    }
  };
  BiosMemoryMap bios_memory_map() const noexcept {
    const auto top = cpu.memory.size();
    const auto below_mmio = std::min(top, kIoApicAddress);
    BiosMemoryMap map;
    map.append(0, 0xa0000, 1);
    map.append(0xa0000, 0x100000, 2);
    if (PcVideo::available(cpu.memory)) {
      const auto video_end = PcVideo::kFramebufferAddress + PcVideo::kApertureBytes;
      map.append(0x100000, PcVideo::kFramebufferAddress, 1);
      map.append(PcVideo::kFramebufferAddress, video_end, 2);
      map.append(video_end, below_mmio, 1);
    } else {
      map.append(0x100000, below_mmio, 1);
    }
    // The IOAPIC and LAPIC live in the reserved high MMIO area.
    map.append(below_mmio, top, 2);
    return map;
  }
  void memory_bios(){
    if(cpu.regs[0]==0xe820){
      if(cpu.regs[2]!=0x534d4150||cpu.regs[1]<20){carry(true);return;}
      const auto map = bios_memory_map();
      const auto index = cpu.regs[3];
      if (index >= map.count) { carry(true); return; }
      const auto& region = map.regions[index];
      const auto p = physical(0, cpu.reg16(7));
      const auto n = cpu.regs[1] >= 24 ? 24 : 20;
      cpu.write64(p, region.base);
      cpu.write64(p + 8, region.length);
      cpu.write32(p + 16, region.type);
      if (n == 24) cpu.write32(p + 20, 1);
      cpu.regs[0] = 0x534d4150;
      cpu.regs[1] = n;
      cpu.regs[3] = index + 1 < map.count ? index + 1 : 0;
      carry(false);
      return;
    }
    if (cpu.reg16(0) == 0xe801) {
      const auto extended_kib = bios_memory_map().regions[2].length / 1024;
      cpu.set_reg16(0, std::min<std::uint64_t>(15360, extended_kib));
      const auto above_16m = extended_kib > 15360 ? (extended_kib - 15360) / 64 : 0;
      cpu.set_reg16(3, std::min<std::uint64_t>(0xffff, above_16m));
      cpu.set_reg16(1, cpu.reg16(0));
      cpu.set_reg16(2, cpu.reg16(3));
      carry(false);
      return;
    }
    if (hi(0) == 0x88) {
      const auto extended_kib = bios_memory_map().regions[2].length / 1024;
      cpu.set_reg16(0, std::min<std::uint64_t>(0xfc00, extended_kib));
      carry(false);
      return;
    }
    if(hi(0)==0x24){switch(lo(0)){case 0:cpu.a20_enabled=false;high(0,0);carry(false);return;case 1:cpu.a20_enabled=true;high(0,0);carry(false);return;case 2:low(0,cpu.a20_enabled?1:0);high(0,0);carry(false);return;case 3:cpu.set_reg16(3,2);high(0,0);carry(false);return;default:break;}}
    // Unsupported BIOS extensions advertise failure as required by this API.
    high(0,0x86);carry(true);
  }
  void keyboard_bios() {
    const auto function = hi(Cpu::EAX);
    switch (function) {
      case 0x00: case 0x10:
        if (const auto word = PcBiosKeyboard::read(cpu, function == 0x10, true))
          cpu.set_reg16(Cpu::EAX, *word);
        else
          waiting_key = true;
        return;
      case 0x01: case 0x11: {
        const auto word = PcBiosKeyboard::read(cpu, function == 0x11, false);
        zero(!word);
        if (word) cpu.set_reg16(Cpu::EAX, *word);
        return;
      }
      case 0x02:
        low(Cpu::EAX, PcBiosKeyboard::shift_status(cpu));
        return;
      case 0x05:
        low(Cpu::EAX, PcBiosKeyboard::store(cpu, cpu.reg16(Cpu::ECX)) ? 0 : 1);
        return;
      case 0x12:
        cpu.set_reg16(Cpu::EAX, PcBiosKeyboard::extended_shift_status(cpu));
        return;
      default: {
        std::ostringstream s;
        s << "BIOS INT 16h función AH=0x" << std::hex << unsigned(function) << " no implementada";
        missing(s.str());
      }
    }
  }
  bool bios(std::uint8_t n){
    switch(n){
      case 0x10:video_bios();return true;
      case 0x11:cpu.set_reg16(0,cpu.read16(0x410));return true;
      case 0x12:cpu.set_reg16(0,640);return true;
      case 0x13:disk_bios();return true;
      case 0x15:memory_bios();return true;
      case 0x16:keyboard_bios();return true;
      case 0x1a:if(hi(0)==0){const auto ms=std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now()-started_).count();const std::uint32_t ticks=ms*182/10000;cpu.set_reg16(1,ticks>>16);cpu.set_reg16(2,ticks&0xffff);low(0,0);carry(false);return true;}carry(true);return true;
      case 0x18:missing("BIOS: el invitado no encontró un sistema arrancable (INT 18h)");
      case 0x19:
        if(!guest_restart_enabled_)
          missing("BIOS: el invitado solicita reiniciar el arranque (INT 19h); usa Reiniciar");
        guest_restart_=GuestRestart::BiosBootstrap;
        return true;
      default:{std::ostringstream s;s<<"Interrupción 0x"<<std::hex<<unsigned(n)<<" pendiente";missing(s.str());}
    }
  }
  std::optional<std::uint32_t> input(std::uint16_t p,unsigned width){
    if(p==PcQuantumDevice::data_port||p==PcQuantumDevice::control_port)return quantum_device.input(p,width);
    if(width!=1)return std::nullopt;
    if(p==0x92)return cpu.a20_enabled?2u:0u;
    if(p==0x21)return pic1_;
    if(p==0xa1)return pic2_;
    if(p==0x64 || p==0x60)return kbc_.input(p,width,cpu.a20_enabled);
    if(p==0x71){if(cmos_index_==0x0a)return 0x26u;if(cmos_index_==0x0b)return 2u;if(cmos_index_==0x0d)return 0x80u;return std::nullopt;}
    if(p==0x3fd)return 0x60u;
    return std::nullopt;
  }
  bool output(std::uint16_t p,unsigned width,std::uint32_t v){
    if(p==PcQuantumDevice::data_port||p==PcQuantumDevice::control_port)return quantum_device.output(p,width,v);
    if(width!=1)return false;
    if(p==0x64 || p==0x60) {
      bool reset_requested=false;
      if(kbc_.output(p,width,v,cpu.a20_enabled,guest_restart_enabled_?&reset_requested:nullptr)) {
        if(reset_requested)guest_restart_=GuestRestart::KeyboardController;
        return true;
      }
      std::ostringstream s;
      s<<"Controlador 8042: OUT 0x"<<std::hex<<unsigned(p)
       <<" valor=0x"<<unsigned(v & 255u)
       <<", instruccion/comando no soportado o protocolo incompleto";
      cpu.fail(s.str(),FaultCode::UnsupportedPort);
    }
    if(p==0x92){
      if(v&1) {
        if(!guest_restart_enabled_)missing("Reinicio por puerto A20 pendiente");
        guest_restart_=GuestRestart::A20Port;
      }
      cpu.a20_enabled=(v&2)!=0;
      return true;
    }
    if(p==0x21){pic1_=v;return true;}if(p==0xa1){pic2_=v;return true;}
    if(p==0x70){cmos_index_=v&0x7f;return true;}
    if(p==0x80)return true; // POST delay port has no guest-visible output.
    if(p==0x3f8){if(serial_.size()<65536)serial_+=char(v&255);return true;}
    return false;
  }
  std::string serial_;
};
}
