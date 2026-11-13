#pragma once

// COSMIC PC: an original, bounded x86 interpreter.
// Instruction encodings and architectural semantics follow the Intel SDM.
// This is not a complete x86 processor: unsupported instructions/states stop
// with an explicit fault. Ring-0 protected/long-mode execution is a limited
// subset, not a Windows-compatible CPU. No x87, APIC, privilege switching or JIT.
// Each step executes one instruction, or ONE element of a REP string operation,
// so an untrusted guest cannot monopolize a host thread inside a single step.
// instructions counts these bounded execution steps, not hardware retired ops.

#include <array>
#include <cstddef>
#include <cstdint>
#include <cmath>
#include <cfenv>
#include <algorithm>
#include <cstring>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#include <xmmintrin.h>
#endif
#include "memory_fabric.hpp"
#if defined(MUNDO_X_PC_ETHER_NEXT)
#include "mundo_x/experimental/ether_transform.hpp"
#endif

namespace cosmic_pc {

enum class FaultCode {
  MemoryBounds, SegmentLimit, InvalidInstruction, UnsupportedOpcode,
  UnsupportedState, DivideError, UnsupportedPort, HostError, ProtectionFault,
  PageFault, MemoryBudget, DeviceNotAvailable, SimdFloatingPoint, X87FloatingPoint
};

struct Fault {
  FaultCode code = FaultCode::HostError;
  std::uint16_t cs = 0;
  std::uint64_t eip = 0;
  std::uint16_t opcode = 0;
  std::vector<std::uint8_t> bytes;
  std::string reason;
  std::uint64_t linear_address = 0;
  std::uint32_t error_code = 0;
  std::string describe() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(4) << cs << ':'
        << std::setw(4) << eip << " opcode=" << std::setw(opcode > 255 ? 4 : 2)
        << opcode << " bytes=";
    for (const auto b : bytes) out << std::setw(2) << unsigned(b) << ' ';
    out << "(" << reason << ')';
    if (code == FaultCode::PageFault) out << " linear=" << linear_address << " error=" << error_code;
    return out.str();
  }
};

enum class StepResult { Executed, Halted, Faulted };

class Cpu {
 public:
  enum Register : unsigned { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI, R8, R9, R10, R11, R12, R13, R14, R15 };
  enum Segment : unsigned { ES, CS, SS, DS, FS, GS };
  static constexpr std::uint32_t CF = 0x0001, PF = 0x0004, AF = 0x0010,
      ZF = 0x0040, SF = 0x0080, TF = 0x0100, IF = 0x0200, DF = 0x0400,
      OF = 0x0800;
  struct DescriptorTable { std::uint16_t limit; std::uint64_t base; };
  // LDTR/TR have visible selectors AND hidden descriptor caches. The
  // emulator does not claim hardware task switching or a full TSS engine.
  struct SystemRegister {
    std::uint16_t selector = 0;
    std::uint64_t base = 0;
    std::uint32_t limit = 0;
    bool valid = false;
  };
  struct SegmentCache {
    std::uint64_t base = 0;
    std::uint32_t limit = 0xffff;
    bool valid = true, code = false, writable = true, readable = true,
         big = false, long_code = false, conforming = false;
    std::uint8_t dpl = 0;
    bool inherited_from_real_mode = false;
  };

  std::array<std::uint64_t, 16> regs{};
  std::array<std::uint16_t, 6> segments{};
  std::uint64_t eip = 0x7c00, cr2 = 0, cr3 = 0, efer = 0, fs_base = 0, gs_base = 0;
  std::uint32_t eflags = 2, cr0 = 0x10, cr4 = 0;
  // Architectural XMM/MXCSR owned by the original guest CPU.
  std::array<std::array<std::uint8_t,16>,16> xmm{};
  std::uint32_t mxcsr=0x1f80;
  static constexpr std::uint32_t kMxcsrMask=0x0000ffffU; // DAZ bit6, FTZ bit15, all status/masks
#if defined(MUNDO_X_PC_ETHER_NEXT)
  struct EtherNextStats {
    std::uint64_t native_instructions = 0;
    mundo_x::ether_next::Cache::Stats cache;
  };
  // An explicit host policy for a subset already implemented by the
  // interpreter. Guest-visible CPUID is independent of this optimization.
  void enable_ether_next(bool enabled = true) noexcept {
    ether_next_enabled_ = enabled;
    if (!enabled) ether_next_cache_.invalidate();
  }
  bool ether_next_enabled() const noexcept { return ether_next_enabled_; }
  static constexpr bool ether_next_native_host() noexcept {
#if defined(__aarch64__) && defined(__linux__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    return true;
#else
    return false;
#endif
  }
  EtherNextStats ether_next_stats() const noexcept {
    return {ether_next_native_instructions_, ether_next_cache_.stats()};
  }
#endif
  // Eight physical 80-bit x87 registers. Full x87 tags are stored in
  // physical order (R0..R7); status[13:11] records TOP. No host stack.
  using Extended80 = std::array<std::uint8_t,10>;
  std::array<Extended80,8> x87{};
  std::uint16_t fpu_control=0x037f, fpu_status=0, fpu_tags=0xffff;
  std::uint16_t fpu_opcode=0, fpu_cs=0, fpu_ds=0;
  std::uint64_t fpu_ip=0, fpu_dp=0;
  DescriptorTable gdtr{0, 0}, idtr{0x3ff, 0};
  SystemRegister ldtr{}, tr{};
  MemoryFabric memory;
  bool a20_enabled = false;
  bool halted = false;
  std::optional<Fault> fault;
  std::uint64_t instructions = 0;
  // Diagnostic counter: one increment per actual IA-32e page-table walk.
  // Used to prove that multi-byte accesses do not repeat the same walk.
  std::uint64_t page_walks = 0;

  struct StepTrace {
    std::uint64_t sequence=0, rip_before=0, rip_after=0;
    std::uint16_t cs_before=0, cs_after=0, opcode=0;
    std::uint32_t flags_before=0, flags_after=0;
    std::uint64_t rsp_before=0, rsp_after=0, cr2=0, cr3=0;
    std::uint64_t page_walks_delta=0;
    std::string mode_before,mode_after,outcome;
    std::vector<std::uint8_t> decoded_bytes;
    int delivered_vector=-1;
    std::uint32_t delivered_error=0;
    std::uint64_t delivered_linear=0;
    std::optional<Fault> terminal_fault;
  };
  // Observation only: callback runs AFTER a bounded guest CPU step finishes.
  // When unset, it does not decode extra instructions or allocate trace data.
  std::function<void(const StepTrace&)> step_trace_hook;
  struct IrqTrace {
    std::uint8_t vector=0;
    std::uint16_t cs_before=0,cs_after=0;
    std::uint64_t rip_before=0,rip_after=0;
    std::uint64_t sequence=0;
    bool delivered=false;
  };
  std::function<void(const IrqTrace&)> irq_trace_hook;

  // true: the host BIOS handled INT, with EIP already past the instruction.
  // false: execute the guest IVT handler with a real interrupt stack frame.
  std::function<bool(Cpu&, std::uint8_t)> interrupt_hook;
  // Widths are BYTES (1, 2, or 4). A missing/declined hook is a fault.
  std::function<std::optional<std::uint32_t>(std::uint16_t, unsigned)> in_hook;
  std::function<bool(std::uint16_t, unsigned, std::uint32_t)> out_hook;
  // A device claims individual physical bus bytes, including MMIO above RAM.
  // A claimed but unreadable/unwritable address faults rather than becoming RAM.
  std::function<bool(std::uint64_t)> mmio_claim_hook;
  std::function<std::optional<std::uint8_t>(std::uint64_t)> mmio_read_hook;
  std::function<bool(std::uint64_t, std::uint8_t)> mmio_write_hook;

  explicit Cpu(std::size_t ram_bytes = 2 * 1024 * 1024)
      : Cpu(ram_bytes, MemoryFabric::default_budget(ram_bytes)) {}
  Cpu(std::size_t ram_bytes, std::size_t resident_budget_bytes)
      : memory(ram_bytes, resident_budget_bytes) {
    if (ram_bytes == 0) throw std::invalid_argument("RAM must not be empty");
    decoded_.reserve(15);
    journal_.reserve(64);
  }

  // Reset architectural state; deliberately preserve loaded RAM and hooks.
  void reset(std::uint16_t cs = 0, std::uint64_t ip = 0x7c00) {
    regs.fill(0); xmm={}; mxcsr=0x1f80;
    x87={};fpu_control=0x037f;fpu_status=0;fpu_tags=0xffff;
    fpu_opcode=fpu_cs=fpu_ds=0;fpu_ip=fpu_dp=0;
    segments.fill(0); segments[CS] = cs;
    eip = ip; eflags = 2; cr0 = 0x10; cr2 = cr3 = cr4 = efer = fs_base = gs_base = 0;
    cache_.fill(SegmentCache{}); cache_[CS].code = true;
    gdtr = {0, 0}; idtr = {0x3ff, 0};
    ldtr = {}; tr = {};
    a20_enabled = false; halted = false; fault.reset(); instructions = 0; page_walks = 0;
    transaction_ = false; journal_.clear(); decoded_.clear();
  }
  std::uint16_t reg16(unsigned r) const { check_register(r); return std::uint16_t(regs[r]); }
  void set_reg16(unsigned r, std::uint16_t value) {
    check_register(r); regs[r] = (regs[r] & ~std::uint64_t{0xffff}) | value;
  }
  std::uint8_t reg8(unsigned r) const {
    if (r >= 8) throw std::out_of_range("legacy byte register index");
    return std::uint8_t(regs[r & 3U] >> (r < 4 ? 0 : 8));
  }
  void set_reg8(unsigned r, std::uint8_t value) {
    if (r >= 8) throw std::out_of_range("legacy byte register index");
    const unsigned shift = r < 4 ? 0 : 8;
    auto& target = regs[r & 3U];
    target = (target & ~(std::uint64_t{0xff} << shift)) | (std::uint64_t(value) << shift);
  }
  bool is_long_mode() const { return (efer & 0x400) && cache_[CS].long_code; }
  bool is_real_mode() const { return !(cr0 & 1); }
  unsigned cpl() const {
    return is_real_mode() || cache_[CS].inherited_from_real_mode
        ? 0U : unsigned(segments[CS] & 3U);
  }
  const char* mode_name() const {
    if (is_real_mode()) return "real16";
    if (is_long_mode()) return "long64";
    if (efer & 0x400) return cache_[CS].big ? "compat32" : "compat16";
    return cache_[CS].big ? "protected32" : "protected16";
  }
  bool flag(std::uint32_t bit) const { return (eflags & bit) != 0; }
  void set_flag(std::uint32_t bit, bool value) {
    if (value) eflags |= bit; else eflags &= ~bit;
    eflags |= 2;
  }
  [[noreturn]] void fail(const std::string& reason,
                         FaultCode code = FaultCode::HostError) const {
    throw Trap{code, reason};
  }

  // Physical BUS accesses: A20 masking applies. Direct memory[] access is for
  // host image loading and bypasses CPU bus semantics and the write journal.
  std::uint8_t read8(std::uint64_t address) const {
    const auto bus=bus_address(address);
    if (mmio_claim_hook && mmio_claim_hook(bus)) {
      if (mmio_read_hook) {
        const auto byte=mmio_read_hook(bus);
        if (byte) return *byte;
      }
      fail("claimed MMIO read is unsupported",FaultCode::UnsupportedPort);
    }
    return memory[physical_index(address)];
  }
  std::uint16_t read16(std::uint64_t address) const { return std::uint16_t(read_physical(address, 2)); }
  std::uint32_t read32(std::uint64_t address) const { return std::uint32_t(read_physical(address, 4)); }
  std::uint64_t read64(std::uint64_t address) const { return read_physical(address, 8); }
  void write8(std::uint64_t address, std::uint8_t value) {
    const auto bus=bus_address(address);
    if (mmio_claim_hook && mmio_claim_hook(bus)) {
      if (!mmio_write_hook || !mmio_write_hook(bus,value))
        fail("claimed MMIO write is unsupported",FaultCode::UnsupportedPort);
      return; // device-side MMIO writes cannot be reversed by RAM journaling
    }
    const auto index = physical_index(address);
    memory.prepare_write(index, 1);
    if (transaction_) journal_.emplace_back(index, std::uint8_t(memory[index]));
    memory[index] = value;
  }
  void write16(std::uint64_t address, std::uint16_t value) { write_physical(address, 2, value); }
  void write32(std::uint64_t address, std::uint32_t value) { write_physical(address, 4, value); }
  void write64(std::uint64_t address, std::uint64_t value) { write_physical(address, 8, value); }
  std::uint32_t linear(std::uint16_t segment, std::uint32_t offset,
                       unsigned bytes = 1) const {
    if (bytes == 0 || std::uint64_t(offset) + bytes > 0x10000ULL)
      fail("real-mode segment limit (64 KiB) exceeded", FaultCode::SegmentLimit);
    return (std::uint32_t(segment) << 4U) + offset;
  }

  StepResult step() {
    if (fault) return StepResult::Faulted;
    if (halted) return StepResult::Halted;
    const Snapshot before{regs, segments, cache_, eip, eflags, cr0, cr4,
                          cr2, cr3, efer, fs_base, gs_base, gdtr, idtr, ldtr, tr, a20_enabled, halted,
                          xmm,mxcsr,x87,fpu_control,fpu_status,fpu_tags,
                          fpu_opcode,fpu_cs,fpu_ds,fpu_ip,fpu_dp};
    const auto trace_mode_before=step_trace_hook?std::string(mode_name()):std::string{};
    const auto trace_walks_before=page_walks;
    int delivered_vector=-1;
    std::uint32_t delivered_error=0;
    std::uint64_t delivered_linear=0;
    auto publish_trace=[&]() noexcept {
      if(!step_trace_hook)return;
      try {
        StepTrace record;
        record.sequence=instructions;
        record.rip_before=before.eip;record.rip_after=eip;
        record.cs_before=before.segments[CS];record.cs_after=segments[CS];
        record.opcode=opcode_;record.flags_before=before.eflags;record.flags_after=eflags;
        record.rsp_before=before.regs[ESP];record.rsp_after=regs[ESP];
        record.cr2=cr2;record.cr3=cr3;
        record.page_walks_delta=page_walks-trace_walks_before;
        record.mode_before=trace_mode_before;record.mode_after=mode_name();
        record.outcome=fault?"fault":delivered_vector>=0?"exception-delivered":
                       halted?"halted":"executed";
        record.decoded_bytes=decoded_;
        record.delivered_vector=delivered_vector;
        record.delivered_error=delivered_error;
        record.delivered_linear=delivered_linear;
        record.terminal_fault=fault;
        step_trace_hook(record);
      }catch(...){
        // Instrumentation is never allowed to mutate or fail the guest step.
      }
    };
    struct TraceGuard {
      decltype(publish_trace)& callback;
      ~TraceGuard() noexcept {callback();}
    } trace_guard{publish_trace};
    start_ip_ = eip; start_cs_ = segments[CS]; cursor_ = eip;
    decoded_.clear(); journal_.clear(); opcode_ = 0;
    operand_override_ = address_override_ = lock_prefix_ = false; rex_ = 0;
    op32_ = is_long_mode() || (!is_real_mode() && cache_[CS].big);
    addr32_ = op32_; repeat_ = 0; override_ = -1;
    ip_written_ = false; transaction_ = true;
    pending_simd_flags_=0;
    try {
      if (!is_real_mode() && cpl() != 0 && cpl() != 3)
        fail("only CPL0/CPL3 are modeled", FaultCode::UnsupportedState);
      if (eflags & (TF | 0x20000U))
        fail("single-step trap / virtual-8086 state is not implemented", FaultCode::UnsupportedState);
      std::uint8_t op;
      for (;;) {
        op = fetch8();
        if (is_long_mode() && op >= 0x40 && op <= 0x4f) { rex_ = op; continue; }
        if (op == 0x66) { operand_override_ = true; rex_ = 0; continue; }
        if (op == 0x67) { address_override_ = true; rex_ = 0; continue; }
        if (op == 0xf2 || op == 0xf3) { repeat_ = op; rex_ = 0; continue; }
        if (op == 0x26) { override_ = ES; rex_ = 0; continue; }
        if (op == 0x2e) { override_ = CS; rex_ = 0; continue; }
        if (op == 0x36) { override_ = SS; rex_ = 0; continue; }
        if (op == 0x3e) { override_ = DS; rex_ = 0; continue; }
        if (op == 0x64) { override_ = FS; rex_ = 0; continue; }
        if (op == 0x65) { override_ = GS; rex_ = 0; continue; }
        opcode_ = op;
        if (op == 0xf0) {
          if (lock_prefix_) fail("duplicate LOCK prefix", FaultCode::InvalidInstruction);
          lock_prefix_=true;rex_=0;continue;
        }
        break;
      }
      op32_ = (is_long_mode() || (!is_real_mode() && cache_[CS].big)) != operand_override_;
      addr32_ = is_long_mode() ? address_override_
          : ((!is_real_mode() && cache_[CS].big) != address_override_);
      execute(op);
      if (fault) fail(fault->reason, fault->code);
      if (!ip_written_) eip = cursor_ & code_mask();
      eflags |= 2;
      ++instructions;
      transaction_ = false; journal_.clear();
      return halted ? StepResult::Halted : StepResult::Executed;
    } catch (const Trap& t) {
      rollback(before);
      // Intel preserves MXCSR sticky status across #XM/#UD on an unmasked
      // SIMD arithmetic exception even when the instruction does not retire.
      // Snapshot rollback must NOT erase these architecturally sticky flags.
      if(pending_simd_flags_)mxcsr|=pending_simd_flags_;
      fault = Fault{t.code, start_cs_, start_ip_, opcode_, decoded_, t.reason, t.address, t.error};
      if (t.code == FaultCode::PageFault) cr2 = t.address;
      const int vector = exception_vector(t.code);
      // Synchronous architectural exception: restart faulting instruction
      // after the guest handler completes. Unsupported/host failures stop.
      if (vector >= 0 && !is_real_mode() &&
          (is_long_mode() || (!(efer & 0x400) && cache_[CS].big))) {
        auto deliver = [&](unsigned number, bool with_error, std::uint32_t code) -> bool {
          try {
            transaction_ = true;
            dispatch_idt(std::uint8_t(number), start_ip_, false, with_error, code);
            transaction_ = false; journal_.clear();
            ++instructions; // bounded interpreter step, not retired opcode
            delivered_vector=int(number);
            delivered_error=code;
            delivered_linear=t.address;
            return true;
          } catch (const std::exception&) {
            rollback(before); // Undo any partial frame/descriptor writes.
            if(pending_simd_flags_)mxcsr|=pending_simd_flags_;
            if (t.code == FaultCode::PageFault) cr2 = t.address;
            return false;
          }
        };
        if (deliver(unsigned(vector), vector == 14 || vector == 13, t.error) ||
            deliver(8, true, 0)) { // Nested delivery failure -> #DF.
          fault.reset(); // A guest-handled event must not leave a host fault latched.
          return StepResult::Executed;
        }
        fault = Fault{t.code, start_cs_, start_ip_, opcode_, decoded_,
                      t.reason + " (guest IDT delivery failed; triple fault)", t.address, t.error};
        return StepResult::Faulted;
      }
    } catch (const MemoryFabric::BudgetExceeded& e) {
      rollback(before);
      fault = Fault{FaultCode::MemoryBudget, start_cs_, start_ip_, opcode_, decoded_, e.what()};
    } catch (const std::exception& e) {
      rollback(before);
      fault = Fault{FaultCode::HostError, start_cs_, start_ip_, opcode_, decoded_, e.what()};
    }
    return StepResult::Faulted;
  }

  // External hardware IRQ is not a software INT: it ignores gate DPL,
  // uses the next instruction's IP and obeys IF. Pending IRQs remain with PIC.
  bool raise_external_irq(std::uint8_t vector) {
    if (!flag(IF) || fault) return false;
    const Snapshot before{regs,segments,cache_,eip,eflags,cr0,cr4,
                          cr2,cr3,efer,fs_base,gs_base,gdtr,idtr,ldtr,tr,a20_enabled,halted,
                          xmm,mxcsr,x87,fpu_control,fpu_status,fpu_tags,
                          fpu_opcode,fpu_cs,fpu_ds,fpu_ip,fpu_dp};
    start_ip_=eip;start_cs_=segments[CS];journal_.clear();transaction_=true;
    try {
      if (is_real_mode()) {
        const auto entry=std::uint32_t(vector)*4;
        if(entry+3>idtr.limit)
          fail("hardware IRQ vector is outside real-mode IVT",FaultCode::SegmentLimit);
        const auto target=read16(idtr.base+entry);
        const auto segment=read16(idtr.base+entry+2);
        push(eflags,2);push(segments[CS],2);push(eip,2);
        eflags &= ~(IF|TF);
        segments[CS]=segment;
        jump(target,2);
      } else {
        dispatch_idt(vector,eip,false,false,0);
      }
      halted=false;
      transaction_=false;journal_.clear();
      if(irq_trace_hook) {
        try {
          irq_trace_hook(IrqTrace{vector,before.segments[CS],segments[CS],
                                 before.eip,eip,instructions,true});
        }catch(...){ /* IRQ observation must not alter the guest */ }
      }
      return true;
    } catch (const Trap& t) {
      rollback(before);
      fault=Fault{t.code,start_cs_,start_ip_,0,{},"hardware IRQ delivery: "+t.reason,t.address,t.error};
      if(t.code==FaultCode::PageFault)cr2=t.address;
    } catch(const std::exception& e) {
      rollback(before);
      fault=Fault{FaultCode::HostError,start_cs_,start_ip_,0,{},std::string("hardware IRQ delivery: ")+e.what()};
    }
    return false;
  }

 private:
  struct Trap : std::runtime_error {
    FaultCode code;
    std::string reason;
    std::uint64_t address = 0;
    std::uint32_t error = 0;
    Trap(FaultCode c, std::string message)
        : std::runtime_error(message), code(c), reason(std::move(message)) {}
  };
  struct Snapshot {
    std::array<std::uint64_t, 16> regs;
    std::array<std::uint16_t, 6> segments;
    std::array<SegmentCache, 6> cache;
    std::uint64_t eip;
    std::uint32_t eflags, cr0, cr4;
    std::uint64_t cr2, cr3, efer, fs_base, gs_base;
    DescriptorTable gdtr, idtr;
    SystemRegister ldtr, tr;
    bool a20, halted;
    std::array<std::array<std::uint8_t,16>,16> xmm;
    std::uint32_t mxcsr;
    std::array<Extended80,8> x87;
    std::uint16_t fpu_control,fpu_status,fpu_tags,fpu_opcode,fpu_cs,fpu_ds;
    std::uint64_t fpu_ip,fpu_dp;
  };
  struct Operand {
    bool is_register = false;
    unsigned index = 0;
    unsigned segment = DS;
    std::uint64_t offset = 0;
    bool uses_esp = false;
    bool rip_relative = false;
  };
  struct ModRM { unsigned reg; Operand rm; unsigned group = 0; };

  bool op32_ = false, addr32_ = false, ip_written_ = false, transaction_ = false;
  bool operand_override_ = false, address_override_ = false, lock_prefix_ = false;
  std::uint8_t rex_ = 0;
  std::array<SegmentCache, 6> cache_{};
  int override_ = -1;
  std::uint8_t repeat_ = 0;
  std::uint16_t start_cs_ = 0, opcode_ = 0;
  std::uint64_t start_ip_ = 0, cursor_ = 0;
  std::vector<std::uint8_t> decoded_;
  std::vector<std::pair<std::size_t, std::uint8_t>> journal_;
  // Architectural descriptor/IDT/TSS reads and a supervisor stack switch are
  // performed by the virtual CPU at privilege 0, not by guest CPL3 code.
  bool supervisor_internal_ = false;
  std::uint32_t pending_simd_flags_=0;
#if defined(MUNDO_X_PC_ETHER_NEXT)
  mundo_x::ether_next::Cache ether_next_cache_{32};
  std::uint64_t ether_next_native_instructions_ = 0;
  bool ether_next_enabled_ = false;
#endif
  struct SupervisorAccess {
    Cpu& cpu;
    bool old;
    explicit SupervisorAccess(Cpu& owner) : cpu(owner), old(owner.supervisor_internal_) {
      cpu.supervisor_internal_ = true;
    }
    ~SupervisorAccess() { cpu.supervisor_internal_ = old; }
  };

  void rollback(const Snapshot& s) {
    // Only our architectural state and write* calls are transactional. Host
    // callbacks can have external side effects that cannot be rolled back.
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) memory.restore_byte(it->first, it->second);
    transaction_ = false; journal_.clear();
    regs = s.regs; segments = s.segments; eip = s.eip; eflags = s.eflags;
    cache_ = s.cache; cr0 = s.cr0; cr4 = s.cr4; cr2 = s.cr2; cr3 = s.cr3;
    efer = s.efer; fs_base = s.fs_base; gs_base = s.gs_base;
    gdtr = s.gdtr; idtr = s.idtr;
    ldtr = s.ldtr; tr = s.tr;
    xmm=s.xmm;mxcsr=s.mxcsr;x87=s.x87;
    fpu_control=s.fpu_control;fpu_status=s.fpu_status;fpu_tags=s.fpu_tags;
    fpu_opcode=s.fpu_opcode;fpu_cs=s.fpu_cs;fpu_ds=s.fpu_ds;
    fpu_ip=s.fpu_ip;fpu_dp=s.fpu_dp;
    a20_enabled = s.a20; halted = s.halted;
  }
  static void check_register(unsigned r) {
    if (r >= 16) throw std::out_of_range("register index");
  }
  enum class Access { Read, Write, Execute };
  std::uint64_t bus_address(std::uint64_t address) const {
    return a20_enabled ? address : address & ~std::uint64_t{0x100000};
  }
  bool mmio_at(std::uint64_t address) const {
    return mmio_claim_hook && mmio_claim_hook(bus_address(address));
  }
  std::size_t physical_index(std::uint64_t address) const {
    const std::uint64_t bus = bus_address(address);
    if (bus >= memory.size()) fail("physical RAM access outside allocation", FaultCode::MemoryBounds);
    return bus;
  }
  void preflight(std::uint64_t address, unsigned bytes) const {
    if (bytes == 0 || address > std::numeric_limits<std::uint64_t>::max() - (bytes - 1))
      fail("physical address overflow", FaultCode::MemoryBounds);
    for (unsigned i = 0; i < bytes; ++i)
      if (!mmio_at(address + i)) (void)physical_index(address + i);
  }
  std::uint64_t read_physical(std::uint64_t address, unsigned bytes) const {
    preflight(address, bytes);
    std::uint64_t result = 0;
    for (unsigned i = 0; i < bytes; ++i) result |= std::uint64_t(read8(address + i)) << (8U * i);
    return result;
  }
  void write_physical(std::uint64_t address, unsigned bytes, std::uint64_t value) {
    preflight(address, bytes);
    bool ram_only=true;
    for (unsigned i=0;i<bytes;++i) ram_only &= !mmio_at(address+i);
    if (ram_only) {
      const auto first = physical_index(address), last = physical_index(address + bytes - 1);
      if (last == first + bytes - 1) memory.prepare_write(first, bytes);
    }
    for (unsigned i = 0; i < bytes; ++i) write8(address + i, std::uint8_t(value >> (8U * i)));
  }
  static bool canonical(std::uint64_t value) {
    return (value >> 48) == ((value & (std::uint64_t{1} << 47)) ? 0xffffU : 0U);
  }
  std::uint64_t segment_linear(unsigned segment, std::uint64_t offset,
                               unsigned bytes, Access access) const {
    if (segment >= 6) fail("segment index", FaultCode::InvalidInstruction);
    if (is_real_mode()) {
      if (offset > 0xffffU || bytes == 0 || offset + bytes > 0x10000ULL)
        fail("real-mode segment limit (64 KiB) exceeded", FaultCode::SegmentLimit);
      return (std::uint64_t(segments[segment]) << 4) + offset;
    }
    if (is_long_mode()) {
      const auto base = segment == FS ? fs_base : segment == GS ? gs_base : 0;
      const auto address = base + offset;
      if (!canonical(address) || bytes == 0 || address > UINT64_MAX - (bytes - 1) || !canonical(address + bytes - 1))
        fail("non-canonical long-mode address", FaultCode::ProtectionFault);
      return address;
    }
    const auto& cache = cache_[segment];
    if (!cache.valid) fail("null/unusable protected-mode segment", FaultCode::ProtectionFault);
    if (!supervisor_internal_ && segment != CS) {
      const auto privilege = cpl();
      // On entry to protected mode the visible selector is still a real-mode
      // segment value. Its low bits become RPL only after a descriptor load.
      const auto requested = cache.inherited_from_real_mode
          ? 0U : unsigned(segments[segment] & 3U);
      if (segment == SS) {
        if (requested != privilege || cache.dpl != privilege)
          fail("protected-mode SS RPL/DPL does not match CPL", FaultCode::ProtectionFault);
      } else if (!cache.conforming && std::max(privilege,requested) > cache.dpl) {
        fail("protected-mode data-segment privilege check failed", FaultCode::ProtectionFault);
      }
    }
    if (bytes == 0 || offset > cache.limit || bytes - 1 > cache.limit - offset)
      fail("protected-mode segment limit exceeded", FaultCode::SegmentLimit);
    if ((access == Access::Write && (cache.code || !cache.writable)) ||
        (access == Access::Read && cache.code && !cache.readable) ||
        (access == Access::Execute && !cache.code))
      fail("protected-mode segment access denied", FaultCode::ProtectionFault);
    return std::uint32_t(cache.base + offset);
  }
  std::uint64_t translate(std::uint64_t address, Access access);
  std::uint64_t read_linear(std::uint64_t address, unsigned bytes, Access access = Access::Read) {
    if (bytes == 0 || bytes > 8 || address > UINT64_MAX - (bytes - 1))
      fail("invalid linear read width", FaultCode::InvalidInstruction);
    std::uint64_t result = 0;
    unsigned done = 0;
    while (done < bytes) {
      const auto linear = address + done;
      // One page-table walk validates the whole contiguous remainder of this
      // 4-KiB page. No persistent TLB is kept, so guest page-table writes
      // cannot leave stale translations behind.
      const auto physical = translate(linear, access);
      const unsigned chunk = std::min<unsigned>(bytes - done, 0x1000U - unsigned(linear & 0xfffU));
      for (unsigned i = 0; i < chunk; ++i)
        result |= std::uint64_t(read8(physical + i)) << (8U * (done + i));
      done += chunk;
    }
    return result;
  }
  void write_linear(std::uint64_t address, unsigned bytes, std::uint64_t value) {
    std::array<std::uint64_t, 8> physical{};
    if (bytes == 0 || bytes > physical.size() || address > UINT64_MAX - (bytes - 1))
      fail("invalid write width", FaultCode::InvalidInstruction);
    // Resolve one translation per touched page, then validate every physical
    // byte before committing any guest write. This preserves the previous
    // all-or-nothing preflight semantics across page boundaries.
    unsigned done = 0;
    while (done < bytes) {
      const auto linear = address + done;
      const auto first_physical = translate(linear, Access::Write);
      const unsigned chunk = std::min<unsigned>(bytes - done, 0x1000U - unsigned(linear & 0xfffU));
      for (unsigned i = 0; i < chunk; ++i) {
        physical[done + i] = first_physical + i;
        if (!mmio_at(physical[done + i])) (void)physical_index(physical[done + i]);
      }
      done += chunk;
    }
    bool contiguous=true,ram_only=true;
    for (unsigned i=0;i<bytes;++i) if (mmio_at(physical[i])) ram_only=false;
    if (ram_only) {
      const auto first=physical_index(physical[0]);
      for(unsigned i=1;i<bytes;++i)
        if(physical_index(physical[i])!=first+i)contiguous=false;
      if(contiguous)memory.prepare_write(first,bytes);
      else for(unsigned i=0;i<bytes;++i)memory.prepare_write(physical_index(physical[i]),1);
    }
    for (unsigned i = 0; i < bytes; ++i)
      write8(physical[i], std::uint8_t(value >> (8U * i)));
  }
  std::uint64_t read_segment(unsigned segment, std::uint64_t offset, unsigned bytes, Access access = Access::Read) {
    return read_linear(segment_linear(segment, offset, bytes, access), bytes, access);
  }
  void write_segment(unsigned segment, std::uint64_t offset, unsigned bytes, std::uint64_t value) {
    write_linear(segment_linear(segment, offset, bytes, Access::Write), bytes, value);
  }
  unsigned width() const { return is_long_mode() && (rex_ & 8) ? 8 : op32_ ? 4 : 2; }
  unsigned address_width() const { return is_long_mode() ? (addr32_ ? 4 : 8) : addr32_ ? 4 : 2; }
  unsigned stack_operand_width() const { return is_long_mode() ? (operand_override_ && !(rex_ & 8) ? 2 : 8) : width(); }
  unsigned stack_address_width() const { return is_long_mode() ? 8 : !is_real_mode() && cache_[SS].big ? 4 : 2; }
  std::uint64_t code_mask() const { return is_long_mode() ? UINT64_MAX : !is_real_mode() && cache_[CS].big ? 0xffffffffULL : 0xffffULL; }
  static std::uint64_t mask(unsigned bytes) { return bytes == 8 ? UINT64_MAX : ((std::uint64_t{1} << (8U * bytes)) - 1); }
  static std::uint64_t sign_bit(unsigned bytes) { return std::uint64_t{1} << (8U * bytes - 1); }
  static std::int64_t signed_value(std::uint64_t value, unsigned bytes) {
    const std::uint64_t v = value & mask(bytes);
    if (bytes == 8) return (v & sign_bit(8)) ? -1 - std::int64_t(~v) : std::int64_t(v);
    return (v & sign_bit(bytes)) ? std::int64_t(v) - (std::int64_t{1} << (8 * bytes)) : std::int64_t(v);
  }
  std::uint8_t fetch8() {
    if (decoded_.size() >= 15) fail("instruction exceeds 15 bytes", FaultCode::InvalidInstruction);
    const auto value = std::uint8_t(read_segment(CS, cursor_, 1, Access::Execute));
    ++cursor_; decoded_.push_back(value);
    return value;
  }
  std::uint64_t immediate(unsigned bytes) {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= std::uint64_t(fetch8()) << (8 * i);
    return value;
  }
  std::uint64_t arithmetic_immediate(unsigned bytes) {
    return bytes == 8 ? std::uint64_t(signed_value(immediate(4), 4)) : immediate(bytes);
  }
  std::uint64_t get_reg(unsigned r, unsigned bytes) const {
    check_register(r);
    if (bytes == 1) return rex_ ? regs[r] & 0xffU : reg8(r);
    return regs[r] & mask(bytes);
  }
  void put_reg(unsigned r, unsigned bytes, std::uint64_t value) {
    check_register(r);
    if (bytes == 1 && rex_) regs[r] = (regs[r] & ~std::uint64_t{0xff}) | (value & 0xffU);
    else if (bytes == 1) set_reg8(r, std::uint8_t(value));
    else if (bytes == 2) set_reg16(r, std::uint16_t(value));
    else regs[r] = value & mask(bytes);  // 32-bit register writes zero the upper half.
  }
  unsigned data_segment(unsigned fallback = DS) const {
    return override_ < 0 ? fallback : unsigned(override_);
  }
  ModRM modrm() {
    const auto encoded = fetch8();
    const unsigned mod = encoded >> 6, group = (encoded >> 3) & 7, rm = encoded & 7;
    const unsigned reg = group + ((rex_ & 4) ? 8 : 0), rm_reg = rm + ((rex_ & 1) ? 8 : 0);
    if (mod == 3) return {reg, Operand{true, rm_reg, DS, 0, false, false}, group};
    std::uint64_t offset = 0;
    unsigned default_segment = DS;
    bool uses_esp = false, rip_relative = false;
    if (address_width() == 2) {
      switch (rm) {
        case 0: offset = reg16(EBX) + reg16(ESI); break;
        case 1: offset = reg16(EBX) + reg16(EDI); break;
        case 2: offset = reg16(EBP) + reg16(ESI); default_segment = SS; break;
        case 3: offset = reg16(EBP) + reg16(EDI); default_segment = SS; break;
        case 4: offset = reg16(ESI); break;
        case 5: offset = reg16(EDI); break;
        case 6:
          if (mod == 0) offset = immediate(2);
          else { offset = reg16(EBP); default_segment = SS; }
          break;
        case 7: offset = reg16(EBX); break;
      }
      if (mod == 1) offset += std::uint64_t(signed_value(immediate(1), 1));
      if (mod == 2) offset += immediate(2);
      offset &= 0xffffU;
    } else {
      if (rm == 4) {
        const auto sib = fetch8();
        const unsigned scale = sib >> 6, index = ((sib >> 3) & 7) + ((rex_ & 2) ? 8 : 0);
        const unsigned base_low = sib & 7, base = base_low + ((rex_ & 1) ? 8 : 0);
        if (index != 4) offset += regs[index] << scale;
        if (base_low == 5 && mod == 0) offset += std::uint64_t(signed_value(immediate(4), 4));
        else {
          offset += regs[base];
          if (base_low == ESP || base_low == EBP) default_segment = SS;
          uses_esp = base == ESP;
        }
      } else if (rm == 5 && mod == 0) {
        offset = std::uint64_t(signed_value(immediate(4), 4)); rip_relative = is_long_mode();
      } else { offset = regs[rm_reg]; if (rm == EBP) default_segment = SS; }
      if (mod == 1) offset += std::uint64_t(signed_value(immediate(1), 1));
      if (mod == 2) offset += std::uint64_t(signed_value(immediate(4), 4));
    }
    offset &= mask(address_width());
    return {reg, Operand{false, 0, data_segment(default_segment), offset, uses_esp, rip_relative}, group};
  }
  std::uint64_t operand_offset(const Operand& operand) const {
    return (operand.offset + (operand.rip_relative ? cursor_ : 0)) & mask(address_width());
  }
  std::uint64_t get(const Operand& operand, unsigned bytes) {
    return operand.is_register ? get_reg(operand.index, bytes)
        : read_segment(operand.segment, operand_offset(operand), bytes);
  }
  void put(const Operand& operand, unsigned bytes, std::uint64_t value) {
    if (operand.is_register) put_reg(operand.index, bytes, value);
    else write_segment(operand.segment, operand_offset(operand), bytes, value);
  }
  void fpu_init() {
    fpu_control=0x037f;fpu_status=0;fpu_tags=0xffff;
    fpu_opcode=fpu_cs=fpu_ds=0;fpu_ip=fpu_dp=0;
    // x87 register data becomes architecturally empty; data bits are stale.
  }
  unsigned fpu_top() const {return unsigned((fpu_status>>11)&7U);}
  unsigned fpu_slot(unsigned logical) const {return (fpu_top()+logical)&7U;}
  unsigned fpu_tag(unsigned slot) const {return (fpu_tags>>(2*slot))&3U;}
  void fpu_tag_set(unsigned slot,unsigned tag) {
    fpu_tags=std::uint16_t((fpu_tags&~(3U<<(2*slot)))|((tag&3U)<<(2*slot)));
  }
  static std::uint64_t ext_significand(const Extended80& value) {
    std::uint64_t v=0;for(unsigned i=0;i<8;++i)v|=std::uint64_t(value[i])<<(8*i);
    return v;
  }
  static unsigned ext_biased_exp(const Extended80& value) {
    return unsigned(value[8])|((unsigned(value[9])&0x7fU)<<8);
  }
  static unsigned classify_ext(const Extended80& value) {
    const auto exp=ext_biased_exp(value);
    const auto sig=ext_significand(value);
    if(!exp&&!sig)return 1U; // zero
    if(exp==0x7fff||!exp||!(sig&(1ULL<<63)))return 2U; // special
    return 0U; // valid finite normal
  }
  // IEEE binary32 / binary64 -> 80-bit extended is always exact. Decode
  // exponent and fraction directly, independent of whether Android's
  // long double is IEEE binary128 or the x86 host's extended80 format.
  static Extended80 extended_from_ieee(std::uint64_t raw,unsigned width) {
    if(width!=4&&width!=8)
      throw std::runtime_error("x87 FLD IEEE source width invalid");
    const unsigned frac_bits=width==4?23U:52U;
    const unsigned exp_bits=width==4?8U:11U;
    const int bias=width==4?127:1023;
    const auto frac_mask=(std::uint64_t{1}<<frac_bits)-1U;
    const auto exp_mask=(1U<<exp_bits)-1U;
    const auto fraction=raw&frac_mask;
    const auto exponent=unsigned((raw>>frac_bits)&exp_mask);
    const auto sign=((raw>>(frac_bits+exp_bits))&1U)!=0;
    std::uint64_t significand=0;
    unsigned extended_exp=0;
    if(exponent==exp_mask) {
      extended_exp=0x7fff;
      significand=(1ULL<<63)|(fraction<<(63-frac_bits));
      // Do not pretend to model the pending unmasked sNaN x87 flags yet.
      if(fraction && !(fraction&(1ULL<<(frac_bits-1))))
        throw std::runtime_error("signaling NaN in x87 FLD requires exception model");
    }else if(exponent||fraction) {
      int e=exponent?int(exponent)-bias:1-bias;
      significand=(exponent?(1ULL<<frac_bits):0ULL)|fraction;
      if(!exponent)
        while(!(significand&(1ULL<<frac_bits))) {
          significand<<=1;--e;
        }
      extended_exp=unsigned(e+16383);
      significand<<=(63-frac_bits);
    }
    Extended80 out{};
    for(unsigned byte=0;byte<8;++byte)
      out[byte]=std::uint8_t(significand>>(byte*8));
    out[8]=std::uint8_t(extended_exp);
    out[9]=std::uint8_t((extended_exp>>8)|(sign?0x80U:0U));
    return out;
  }
  static Extended80 extended_from_long_double(long double value) {
    Extended80 bytes{};
    const bool negative=std::signbit(value);
    const auto magnitude=std::fabs(value);
    std::uint64_t sig=0;unsigned exponent=0;
    if(std::isnan(value)) {exponent=0x7fff;sig=0xc000000000000000ULL;}
    else if(std::isinf(value)) {exponent=0x7fff;sig=0x8000000000000000ULL;}
    else if(magnitude!=0) {
      int binexp=0;
      const auto fraction=std::frexp(magnitude,&binexp);
      const auto biased=binexp-1+16383;
      if(biased<=0||biased>=0x7fff)
        throw std::runtime_error("x87 extended exponent outside finite implementation");
      exponent=unsigned(biased);
      sig=static_cast<std::uint64_t>(std::ldexp(fraction,64));
    }
    for(unsigned i=0;i<8;++i)bytes[i]=std::uint8_t(sig>>(8*i));
    bytes[8]=std::uint8_t(exponent);
    bytes[9]=std::uint8_t((exponent>>8)|(negative?0x80U:0));
    return bytes;
  }
  static long double extended_to_long_double(const Extended80& value) {
    const auto exponent=ext_biased_exp(value);
    const auto significand=ext_significand(value);
    const bool sign=(value[9]&0x80U)!=0;
    long double result=0;
    if(exponent==0x7fff) {
      result=significand==0x8000000000000000ULL
          ?std::numeric_limits<long double>::infinity()
          :std::numeric_limits<long double>::quiet_NaN();
    }else if(exponent||significand) {
      const auto e=(exponent?int(exponent):1)-16383-63;
      result=std::ldexp(static_cast<long double>(significand),e);
    }
    return sign?-result:result;
  }
  static Extended80 x87_indefinite() {
    Extended80 value{};
    value[7]=0xc0;value[8]=0xff;value[9]=0xff;
    return value; // sign=1, exponent=all ones, QNaN integer/quiet bits
  }
  void fpu_stack_fault(bool overflow) {
    // Native x87 preserves a deferred IE+SF fault when unmasked. C1 reflects
    // overflow (1) versus underflow (0). The TOP remains unchanged until
    // a masked stack operation commits; #MF may later be raised by FWAIT.
    fpu_status=std::uint16_t((fpu_status&~0x0200U)|0x0041U|
                              (overflow?0x0200U:0U));
    if(!(fpu_control&1U))fpu_status|=0x8080U; // ES+B
  }
  void round_x87_precision(Extended80& bits,std::uint32_t& flags) {
    // C1 describes this operation's rounding, while PE is sticky.
    fpu_status=std::uint16_t(fpu_status&~0x0200U);
    const unsigned pc=(fpu_control>>8)&3U;
    if(pc==1)fail("reserved x87 PC=01",FaultCode::UnsupportedState);
    const unsigned precision=pc==0?24U:pc==2?53U:64U;
    if(precision==64)return;
    auto exponent=ext_biased_exp(bits);
    auto significand=ext_significand(bits);
    if(exponent==0x7fff||!significand)return; // NaN, infinity or zero
    if(!exponent)fail("extended subnormal x87 arithmetic rounding pending",
                      FaultCode::UnsupportedState);
    const unsigned drop=64-precision;
    const auto lowmask=(std::uint64_t{1}<<drop)-1;
    const auto lost=significand&lowmask;
    if(!lost)return;
    const auto half=std::uint64_t{1}<<(drop-1);
    auto keep=significand>>drop;
    const unsigned rc=(fpu_control>>10)&3U;
    const bool negative=(bits[9]&0x80U)!=0;
    const bool increment=rc==0?(lost>half||(lost==half&&(keep&1))):
                         rc==1?negative:rc==2?!negative:false;
    flags|=32U; // x87 PE: result changed by precision narrowing
    // Intel C1 indicates the last rounded result was rounded upwards.
    if(increment)fpu_status|=0x0200U;
    if(increment){
      ++keep;
      if(keep==(std::uint64_t{1}<<precision)){
        keep>>=1;++exponent;
        if(exponent>=0x7fff)
          fail("narrow precision x87 overflow pending",FaultCode::UnsupportedState);
      }
    }
    significand=keep<<drop;
    for(unsigned byte=0;byte<8;++byte)
      bits[byte]=std::uint8_t(significand>>(byte*8));
    bits[8]=std::uint8_t(exponent);
    bits[9]=std::uint8_t((exponent>>8)|(negative?0x80U:0));
  }
  void fpu_push(Extended80 value) {
    const auto top=(fpu_top()+7U)&7U;
    if(fpu_tag(top)!=3U) {
      fpu_stack_fault(true);
      if(!(fpu_control&1U))return; // unmasked: no TOP/data update
      value=x87_indefinite(); // masked: push Intel indefinite 80-bit
    } else fpu_status=std::uint16_t(fpu_status&~0x0200U);
    x87[top]=value;
    fpu_tag_set(top,classify_ext(value));
    fpu_status=std::uint16_t((fpu_status&~0x3800U)|(top<<11));
  }
  Extended80 fpu_peek(unsigned logical=0) const {
    const auto slot=fpu_slot(logical);
    if(fpu_tag(slot)==3U)
      fail("x87 stack underflow exception handling not implemented",FaultCode::UnsupportedState);
    return x87[slot];
  }
  void fpu_pop() {
    const auto top=fpu_top();
    if(fpu_tag(top)==3U) {
      fpu_stack_fault(false);
      if(!(fpu_control&1U))return;
      // Underflow masked: pop empty register, preserve all other tags.
    }else fpu_status=std::uint16_t(fpu_status&~0x0200U);
    fpu_tag_set(top,3);
    fpu_status=std::uint16_t((fpu_status&~0x3800U)|(((top+1U)&7U)<<11));
  }
  void require_fpu() const {
    if(cr0&0xcU)fail("x87 CR0.TS/EM device unavailable",FaultCode::DeviceNotAvailable);
  }
  static std::uint16_t load16(const std::array<std::uint8_t,512>& src,unsigned off) {
    return std::uint16_t(src[off])|(std::uint16_t(src[off+1])<<8);
  }
  static std::uint32_t load32(const std::array<std::uint8_t,512>& src,unsigned off) {
    std::uint32_t result=0;
    for(unsigned i=0;i<4;++i)result|=std::uint32_t(src[off+i])<<(i*8);
    return result;
  }
  static std::uint64_t load64(const std::array<std::uint8_t,512>& src,unsigned off) {
    return std::uint64_t(load32(src,off))|(std::uint64_t(load32(src,off+4))<<32);
  }
  static void store16(std::array<std::uint8_t,512>& dst,unsigned off,std::uint16_t value) {
    dst[off]=std::uint8_t(value);dst[off+1]=std::uint8_t(value>>8);
  }
  static void store32(std::array<std::uint8_t,512>& dst,unsigned off,std::uint32_t value) {
    for(unsigned i=0;i<4;++i)dst[off+i]=std::uint8_t(value>>(8*i));
  }
  static void store64(std::array<std::uint8_t,512>& dst,unsigned off,std::uint64_t value) {
    store32(dst,off,std::uint32_t(value));store32(dst,off+4,std::uint32_t(value>>32));
  }
  void fxstate(const Operand& memory_operand,bool restore) {
    require_memory(memory_operand);
    require_fpu();
    if(!(cr4&0x200U))
      fail("FXSAVE/FXRSTOR require OSFXSR in this CPU profile",FaultCode::InvalidInstruction);
    const auto address=segment_linear(memory_operand.segment,operand_offset(memory_operand),
                                      512,restore?Access::Read:Access::Write);
    if(address&15U)fail("FXSAVE/FXRSTOR require 16-byte aligned memory",FaultCode::ProtectionFault);
    // Validate the ENTIRE 512-byte window before changing CPU state or RAM.
    // Explicit MMIO rejection: these operations are not device DMA commands.
    for(unsigned i=0;i<512;++i) {
      const auto p=translate(address+i,restore?Access::Read:Access::Write);
      if(mmio_at(p))
        fail("FXSAVE/FXRSTOR on MMIO cannot be atomic",FaultCode::UnsupportedState);
      (void)physical_index(p);
    }
    std::array<std::uint8_t,512> buffer{};
    if(restore) {
      for(unsigned i=0;i<416;++i)
        buffer[i]=std::uint8_t(read_linear(address+i,1));
      const auto candidate_mxcsr=load32(buffer,24);
      if(candidate_mxcsr&~kMxcsrMask)
        fail("FXRSTOR reserved MXCSR bit",FaultCode::ProtectionFault);
      // Parse and validate entire image before committing any CPU state.
      const auto new_control=load16(buffer,0);
      auto new_status=load16(buffer,2);
      const auto abridged=buffer[4];
      const auto new_opcode=load16(buffer,6);
      const bool wide_pointers=is_long_mode()&&(rex_&8);
      const auto new_ip=wide_pointers?load64(buffer,8):load32(buffer,8);
      const auto new_dp=wide_pointers?load64(buffer,16):load32(buffer,16);
      const auto new_cs=wide_pointers?std::uint16_t{0}:load16(buffer,12);
      const auto new_ds=wide_pointers?std::uint16_t{0}:load16(buffer,20);
      std::array<Extended80,8> new_regs{};
      std::uint16_t new_tags=0xffff;
      // FXSAVE stores DATA in logical ST(0..7) order, while FTW bits
      // describe PHYSICAL R(0..7) tags. Recover the physical data slots
      // using the TOP field from the saved x87 status word.
      const auto new_top=(new_status>>11)&7U;
      for(unsigned i=0;i<8;++i) {
        const unsigned physical=(new_top+i)&7U;
        for(unsigned j=0;j<10;++j)
          new_regs[physical][j]=buffer[32+16*i+j];
      }
      for(unsigned physical=0;physical<8;++physical)
        if(abridged&(1U<<physical))
          new_tags=std::uint16_t(
              (new_tags&~(3U<<(2*physical)))|
              (classify_ext(new_regs[physical])<<(2*physical)));

      std::array<std::array<std::uint8_t,16>,16> new_xmm=xmm;
      for(unsigned i=0;i<xmm_count();++i)
        for(unsigned j=0;j<16;++j)new_xmm[i][j]=buffer[160+16*i+j];
      fpu_control=new_control;fpu_status=new_status;fpu_tags=new_tags;
      fpu_opcode=new_opcode;fpu_ip=new_ip;fpu_dp=new_dp;
      fpu_cs=new_cs;fpu_ds=new_ds;x87=new_regs;
      mxcsr=candidate_mxcsr;xmm=new_xmm;
    }else{
      store16(buffer,0,fpu_control);store16(buffer,2,fpu_status);
      for(unsigned i=0;i<8;++i)
        if(fpu_tag(i)!=3U)buffer[4]|=std::uint8_t(1U<<i);
      store16(buffer,6,fpu_opcode);
      const bool wide_pointers=is_long_mode()&&(rex_&8);
      if(wide_pointers){
        store64(buffer,8,fpu_ip);store64(buffer,16,fpu_dp);
      }else {
        store32(buffer,8,std::uint32_t(fpu_ip));store16(buffer,12,fpu_cs);
        store32(buffer,16,std::uint32_t(fpu_dp));store16(buffer,20,fpu_ds);
      }
      store32(buffer,24,mxcsr);store32(buffer,28,kMxcsrMask);
      // X87 saved data ST(i) is LOGICAL, not the physical R(i)
      // position indicated by the separate abridged tag mask.
      for(unsigned i=0;i<8;++i)
        for(unsigned j=0;j<10;++j)
          buffer[32+16*i+j]=x87[fpu_slot(i)][j];
      for(unsigned i=0;i<xmm_count();++i)
        for(unsigned j=0;j<16;++j)buffer[160+16*i+j]=xmm[i][j];
      // Intel reserves bytes 464-511 for software; never overwrite them.
      for(unsigned i=0;i<464;++i)write_linear(address+i,1,buffer[i]);
    }
  }
  unsigned xmm_count() const {return is_long_mode()?16U:8U;}
  void require_xmm(unsigned index) const {
    if(index>=xmm_count())fail("XMM register index unavailable",FaultCode::InvalidInstruction);
  }
  void require_sse_state() const {
    if(!(cr4&0x200U))
      fail("SSE2 requires CR4.OSFXSR",FaultCode::InvalidInstruction);
    if(cr0&0x4U)
      fail("SSE2 disabled by CR0.EM",FaultCode::DeviceNotAvailable);
    if(cr0&0x8U)
      fail("SSE2 task-switched CR0.TS",FaultCode::DeviceNotAvailable);
  }
#if defined(MUNDO_X_PC_ETHER_NEXT)
  bool try_ether_next(const ModRM& operands) {
    if (!ether_next_native_host() || !ether_next_enabled_ ||
        !operands.rm.is_register || !is_long_mode() || override_ >= 0 ||
        address_override_ || lock_prefix_ || step_trace_hook || irq_trace_hook)
      return false;
    // The caller already validated this specific SSE2 register opcode and
    // its architectural state. Reuse the bytes fetched for this instruction.
    // Cpu::step still owns RIP, retirement, rollback, and device boundaries.
    mundo_x::ether_next::Guard guard;
    guard.sse2_enabled = true;
    guard.osfxsr = (cr4 & 0x200U) != 0;
    guard.cr0_em = (cr0 & 4U) != 0;
    guard.cr0_ts = (cr0 & 8U) != 0;
    guard.single_step = flag(TF);
    if (!ether_next_cache_.execute(decoded_, xmm, guard)) return false;
    ++ether_next_native_instructions_;
    return true;
  }
#endif
  std::array<std::uint8_t,16> read_xmm_operand(const Operand& m,bool aligned) {
    if(m.is_register){require_xmm(m.index);return xmm[m.index];}
    const auto address=segment_linear(m.segment,operand_offset(m),16,Access::Read);
    if(aligned&&(address&15U))fail("aligned SSE2 operand requires 16-byte alignment",FaultCode::ProtectionFault);
    std::array<std::uint8_t,16> result{};
    for(unsigned i=0;i<16;++i)result[i]=std::uint8_t(read_linear(address+i,1));
    return result;
  }
  void write_xmm_operand(const Operand& m,const std::array<std::uint8_t,16>& value,bool aligned) {
    if(m.is_register){require_xmm(m.index);xmm[m.index]=value;return;}
    const auto address=segment_linear(m.segment,operand_offset(m),16,Access::Write);
    if(aligned&&(address&15U))fail("aligned SSE2 operand requires 16-byte alignment",FaultCode::ProtectionFault);
    // Preflight the entire destination before the first guest byte write.
    for(unsigned i=0;i<16;++i){
      const auto p=translate(address+i,Access::Write);
      if(mmio_at(p))fail("128-bit SSE2 MMIO write unsupported",FaultCode::UnsupportedState);
      (void)physical_index(p);
    }
    for(unsigned i=0;i<16;++i)write_linear(address+i,1,value[i]);
  }
  static std::uint64_t packed_lane(const std::array<std::uint8_t,16>& value,
                                   unsigned offset,unsigned size) {
    std::uint64_t result=0;
    for(unsigned i=0;i<size;++i)result|=std::uint64_t(value[offset+i])<<(8*i);
    return result;
  }
  static void packed_set(std::array<std::uint8_t,16>& value,
                         unsigned offset,unsigned size,std::uint64_t bits) {
    for(unsigned i=0;i<size;++i)value[offset+i]=std::uint8_t(bits>>(8*i));
  }
  // The host rounding environment is borrowed for ONE guest operation and
  // restored even on exceptions. Volatile arithmetic forces a runtime FP op
  // at -O2; oracle tests compare status/results to real x86 hardware.
  struct HostRound {
    std::fenv_t saved{};
    explicit HostRound(std::uint32_t csr) {
      if(std::feholdexcept(&saved)!=0)
        throw std::runtime_error("host non-trapping floating environment unavailable");
      const int mode=((csr>>13)&3U)==0?FE_TONEAREST:
                     ((csr>>13)&3U)==1?FE_DOWNWARD:
                     ((csr>>13)&3U)==2?FE_UPWARD:FE_TOWARDZERO;
      if(std::fesetround(mode)!=0) {
        (void)std::fesetenv(&saved);
        throw std::runtime_error("host IEEE rounding mode unsupported");
      }
      // Compute with gradual underflow. Guest DAZ/FTZ are applied explicitly.
#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
      _mm_setcsr(_mm_getcsr()&~0x8040U); // MXCSR FTZ and DAZ.
#elif defined(__aarch64__) && (defined(__GNUC__) || defined(__clang__))
      std::uint64_t control=0;
      asm volatile("mrs %0, fpcr":"=r"(control)::"memory");
      control&=~((1ULL<<24)|3ULL); // FPCR FZ, AH and FIZ.
      asm volatile("msr fpcr, %0"::"r"(control):"memory");
#endif
    }
    ~HostRound() noexcept {(void)std::fesetenv(&saved);}
    std::uint32_t status() const {
      const int f=std::fetestexcept(FE_ALL_EXCEPT);
      return ((f&FE_INVALID)?1U:0U)|((f&FE_DIVBYZERO)?4U:0U)|
             ((f&FE_OVERFLOW)?8U:0U)|((f&FE_UNDERFLOW)?16U:0U)|
             ((f&FE_INEXACT)?32U:0U);
    }
  };
  static double fp64_from_bits(std::uint64_t bits) {
    double result{};std::memcpy(&result,&bits,sizeof(result));return result;
  }
  static std::uint64_t fp64_bits(double value) {
    std::uint64_t bits{};std::memcpy(&bits,&value,sizeof(bits));return bits;
  }
  static bool fp64_subnormal(std::uint64_t raw) {
    return (raw&0x7ff0000000000000ULL)==0 &&
           (raw&0x000fffffffffffffULL)!=0;
  }
  static bool fp64_nan(std::uint64_t raw) {
    return (raw&0x7ff0000000000000ULL)==0x7ff0000000000000ULL &&
           (raw&0x000fffffffffffffULL)!=0;
  }
  static bool fp64_snan(std::uint64_t raw) {
    return fp64_nan(raw) && !(raw&(1ULL<<51));
  }
  static std::uint64_t fp64_order_key(std::uint64_t raw) {
    // For ordered operands the sign-adjusted bit pattern sorts numerically.
    // The two signed zeros compare equal.
    constexpr auto sign=0x8000000000000000ULL;
    if((raw&~sign)==0)raw=0;
    return (raw&sign)?~raw:(raw^sign);
  }
  std::uint64_t fp64_apply_daz(std::uint64_t raw) const {
    if((mxcsr&0x40U)&&fp64_subnormal(raw))
      return raw&0x8000000000000000ULL;
    return raw;
  }
  static std::uint32_t fp64_denormal_status(std::uint64_t left,std::uint64_t right) {
    return fp64_subnormal(left)||fp64_subnormal(right)?2U:0U;
  }
  void simd_status(std::uint32_t flags) {
    flags&=0x3fU;
    // These exceptions are detected before the rounded result is formed.
    const auto unmasked=flags&~(mxcsr>>7);
    if(unmasked&7U)flags&=7U;
    if(!flags)return;
    mxcsr|=flags;
    if(unmasked) {
      pending_simd_flags_|=flags;
      if(!(cr4&0x400U))
        fail("unmasked SSE2 exception requires CR4.OSXMMEXCPT (#UD)",
             FaultCode::InvalidInstruction);
      fail("unmasked SSE2 SIMD floating point exception (#XM)",
           FaultCode::SimdFloatingPoint);
    }
  }
  std::uint64_t sse64_source(const Operand& source) {
    if(source.is_register){
      require_xmm(source.index);
      return packed_lane(xmm[source.index],0,8);
    }
    return read_segment(source.segment,operand_offset(source),8);
  }
  void sse64_store(const Operand& destination,std::uint64_t bits) {
    if(destination.is_register) {
      require_xmm(destination.index);
      packed_set(xmm[destination.index],0,8,bits);
    }else write_segment(destination.segment,operand_offset(destination),8,bits);
  }
  // Returns a binary64 result and accumulates its status for the instruction.
  std::uint64_t simd_double_op(std::uint8_t opcode,
                               std::uint64_t left_raw,std::uint64_t right_raw,
                               std::uint32_t& status) {
    // Aggregate status across BOTH packed lanes before deciding whether #XM
    // is raised; an unmasked exception must not commit a partial vector.
    left_raw=opcode==0x51?0:fp64_apply_daz(left_raw);
    right_raw=fp64_apply_daz(right_raw);
    const bool minmax=opcode==0x5d||opcode==0x5f;
    if(fp64_nan(left_raw)||fp64_nan(right_raw)) {
      if(minmax||fp64_snan(left_raw)||fp64_snan(right_raw))status|=1U;
      // MIN/MAX select the second operand unchanged. Arithmetic quiets
      // the first NaN in operand order, preserving its sign and payload.
      if(minmax)return right_raw;
      return (fp64_nan(left_raw)?left_raw:right_raw)|(1ULL<<51);
    }
    status|=fp64_denormal_status(left_raw,right_raw);
    if(minmax) {
      const auto l=fp64_order_key(left_raw),r=fp64_order_key(right_raw);
      if(l==r)return right_raw;
      return opcode==0x5d?(l<r?left_raw:right_raw):
                             (l>r?left_raw:right_raw);
    }
    const double left=fp64_from_bits(left_raw);
    const double right=fp64_from_bits(right_raw);
    double rounded=0;
    std::uint32_t result_status=0;
    {
      HostRound rounding(mxcsr);
      volatile double a=left,b=right;
      switch(opcode) {
        case 0x58:{volatile double t=a+b;rounded=t;break;} // ADD
        case 0x5c:{volatile double t=a-b;rounded=t;break;} // SUB
        case 0x59:{volatile double t=a*b;rounded=t;break;} // MUL
        case 0x5e:{volatile double t=a/b;rounded=t;break;} // DIV
        case 0x51:{volatile double t=std::sqrt(b);rounded=t;break;} // SQRT(src)
        default:fail("unsupported SSE2 binary64 operation",FaultCode::UnsupportedOpcode);
      }
      result_status=rounding.status();
    }
    if(result_status&1U) {
      status|=result_status;
      return 0xfff8000000000000ULL; // Intel's indefinite result.
    }
    auto bits=fp64_bits(rounded);
    const bool tiny=fp64_subnormal(bits);
    if(!(mxcsr&(1U<<11))&&(tiny||(result_status&16U))) {
      // Exact tiny results also trap. Rounding to a subnormal is part of
      // the masked response, so its precision loss is not reported here.
      result_status=(result_status&~32U)|16U;
    }else if((mxcsr&0x8000U)&&tiny) {
      bits&=0x8000000000000000ULL;
      result_status|=16U|32U;
    }
    status|=result_status;
    return bits;
  }
  void sse2_float(unsigned opcode) {
    const bool scalar=repeat_==0xf2 && !operand_override_;
    const bool packed=operand_override_ && repeat_==0;
    if(!scalar&&!packed)
      fail("SSE2 binary64 requires F2 scalar or 66 packed prefix",
           FaultCode::UnsupportedOpcode);
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    const auto before=xmm[m.reg];
    auto result=before;
    std::uint32_t aggregated_status=0;
    if(scalar) {
      const auto src=sse64_source(m.rm);
      const auto dest=packed_lane(before,0,8);
      packed_set(result,0,8,simd_double_op(std::uint8_t(opcode),
                                           dest,src,aggregated_status));
    }else{
      const auto rhs=read_xmm_operand(m.rm,true);
      // Stage both lanes before commit. An unmasked exception must not
      // modify the destination even if lane0 completed successfully.
      for(unsigned lane=0;lane<2;++lane) {
        const auto at=lane*8U;
        packed_set(result,at,8,simd_double_op(std::uint8_t(opcode),
                    packed_lane(before,at,8),packed_lane(rhs,at,8),
                    aggregated_status));
      }
    }
    simd_status(aggregated_status);
    xmm[m.reg]=result;
  }
  std::uint64_t sse2_fp64_to_integer(std::uint64_t raw,unsigned bytes,
                                    bool truncate,std::uint32_t& status) {
    // These conversions honor DAZ without raising a denormal-operand
    // exception. Rounding a nonzero subnormal to an integer raises PE.
    const auto value=fp64_from_bits(fp64_apply_daz(raw));
    if(std::isfinite(value)) {
      HostRound mode(mxcsr);
      volatile double input=value;
      volatile double rounded=truncate?std::trunc(input):std::rint(input);
      const double limit=bytes==8?0x1p63:0x1p31;
      if(rounded>=-limit && rounded<limit) {
        if(rounded!=value)status|=32U;
        return std::uint64_t(static_cast<std::int64_t>(rounded));
      }
    }
    status|=1U;
    return std::uint64_t{1}<<(8*bytes-1); // Integer indefinite.
  }
  void sse2_scalar_misc(unsigned opcode) {
    require_sse_state();
    const auto m=modrm();
    const bool compare=opcode==0x2e||opcode==0x2f;
    if(compare) {
      if(!operand_override_||repeat_)
        fail("UCOMISD/COMISD require 66",FaultCode::UnsupportedOpcode);
      require_xmm(m.reg);
      std::uint32_t status=0;
      const auto left_raw=fp64_apply_daz(packed_lane(xmm[m.reg],0,8));
      const auto right_raw=fp64_apply_daz(sse64_source(m.rm));
      const bool unordered=fp64_nan(left_raw)||fp64_nan(right_raw);
      if(!unordered)status=fp64_denormal_status(left_raw,right_raw);
      if(unordered && (opcode==0x2f||
                       fp64_snan(left_raw)||fp64_snan(right_raw)))
        status|=1U;
      simd_status(status);
      const auto lhs=fp64_order_key(left_raw),rhs=fp64_order_key(right_raw);
      const bool z=unordered||lhs==rhs;
      const bool p=unordered;
      const bool c=unordered||lhs<rhs;
      eflags&=~std::uint32_t(CF|PF|AF|ZF|SF|OF);
      set_flag(ZF,z);set_flag(PF,p);set_flag(CF,c);
      return;
    }
    if(repeat_!=0xf2||operand_override_)
      fail("F2 SSE2 scalar conversion prefix required",FaultCode::UnsupportedOpcode);
    if(opcode==0x2a) { // CVTSI2SD xmm, r/m32 or r/m64 with REX.W
      require_xmm(m.reg);
      const unsigned bytes=is_long_mode()&&(rex_&8U)?8U:4U;
      const auto integer=signed_value(get(m.rm,bytes),bytes);
      std::uint32_t status=0;
      double rounded=0;
      {
        HostRound mode(mxcsr);
        volatile std::int64_t input=integer;
        volatile double converted=static_cast<double>(input);
        rounded=converted;
        status|=mode.status();
      }
      simd_status(status);
      packed_set(xmm[m.reg],0,8,fp64_bits(rounded));
      return;
    }
    if(opcode==0x2c||opcode==0x2d) { // CVTTSD2SI / CVTSD2SI
      const unsigned bytes=is_long_mode()&&(rex_&8U)?8U:4U;
      std::uint32_t status=0;
      const auto converted=sse2_fp64_to_integer(sse64_source(m.rm),bytes,
                                                opcode==0x2c,status);
      simd_status(status);
      put_reg(m.reg,bytes,converted);
      return;
    }
    fail("unsupported SSE2 scalar conversion",FaultCode::UnsupportedOpcode);
  }
  void sse2_convert_packed() { // F3 CVTDQ2PD, F2 CVTPD2DQ, 66 CVTTPD2DQ
    const bool dq_to_pd=repeat_==0xf3 && !operand_override_;
    const bool pd_to_dq=repeat_==0xf2 && !operand_override_;
    const bool pd_to_dq_trunc=operand_override_ && repeat_==0;
    if(!dq_to_pd&&!pd_to_dq&&!pd_to_dq_trunc)
      fail("SSE2 packed integer/FP conversion requires F3/F2/66",
           FaultCode::UnsupportedOpcode);
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    std::array<std::uint8_t,16> result{};
    if(dq_to_pd) {
      std::uint64_t low=0;
      if(m.rm.is_register) {
        require_xmm(m.rm.index);
        low=packed_lane(xmm[m.rm.index],0,8);
      }else low=get(m.rm,8);
      for(unsigned i=0;i<2;++i) {
        const auto raw=(low>>(i*32))&0xffffffffULL;
        const auto signed_int=(raw&0x80000000ULL)?
            std::int64_t(raw)-0x100000000LL:std::int64_t(raw);
        packed_set(result,i*8,8,fp64_bits(static_cast<double>(signed_int)));
      }
    }else{
      const auto source=read_xmm_operand(m.rm,true);
      std::uint32_t flags=0;
      for(unsigned i=0;i<2;++i) {
        const auto converted=sse2_fp64_to_integer(packed_lane(source,i*8,8),4,
                                                  pd_to_dq_trunc,flags);
        packed_set(result,i*4,4,converted);
      }
      simd_status(flags); // full packed conversion must be atomic on #XM
    }
    xmm[m.reg]=result; // upper64 zero for packed FP->DQ
  }
  void sse2_cmp_vector(bool scalar) {
    if((scalar&&(repeat_!=0xf2||operand_override_))||
       (!scalar&&(!operand_override_||repeat_)))
      fail("CMPPD/CMPSD require F2 or 66 prefix",FaultCode::UnsupportedOpcode);
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    // Decode imm8 first: RIP-relative address is based on the next IP.
    const auto predicate=unsigned(immediate(1))&7U;
    const auto before=xmm[m.reg];
    auto result=before;
    std::array<std::uint8_t,16> source{};
    if(scalar)packed_set(source,0,8,sse64_source(m.rm));
    else source=read_xmm_operand(m.rm,true);
    std::uint32_t status=0;
    const bool signaling=(predicate==1U||predicate==2U||
                          predicate==5U||predicate==6U);
    for(unsigned lane=0;lane<(scalar?1U:2U);++lane){
      const unsigned off=lane*8U;
      const auto left=fp64_apply_daz(packed_lane(before,off,8));
      const auto right=fp64_apply_daz(packed_lane(source,off,8));
      const bool unordered=fp64_nan(left)||fp64_nan(right);
      if(!unordered)status|=fp64_denormal_status(left,right);
      if((unordered&&signaling)||fp64_snan(left)||fp64_snan(right))
        status|=1U;
      const auto a=fp64_order_key(left),b=fp64_order_key(right);
      bool matching=false;
      switch(predicate){
        case 0:matching=!unordered&&a==b;break;          // EQ
        case 1:matching=!unordered&&a<b;break;           // LT
        case 2:matching=!unordered&&a<=b;break;          // LE
        case 3:matching=unordered;break;                 // UNORD
        case 4:matching=unordered||a!=b;break;           // NEQ
        case 5:matching=unordered||!(a<b);break;          // NLT
        case 6:matching=unordered||!(a<=b);break;         // NLE
        case 7:matching=!unordered;break;                // ORD
      }
      packed_set(result,off,8,matching?UINT64_MAX:0);
    }
    simd_status(status); // all lanes first, then exception before commit
    xmm[m.reg]=result;
  }
  void sse_integer(unsigned opcode) {
    if(!operand_override_||repeat_)
      fail("packed integer SSE2 requires 66 prefix",FaultCode::UnsupportedOpcode);
    require_sse_state();
    const auto m=modrm();require_xmm(m.reg);
#if defined(MUNDO_X_PC_ETHER_NEXT)
    switch (opcode) {
      case 0xdb: case 0xdf: case 0xeb: // PAND/PANDN/POR
      case 0xfc: case 0xfd: case 0xfe: case 0xd4: // PADD
      case 0xf8: case 0xf9: case 0xfa: case 0xfb: // PSUB
        if (try_ether_next(m)) return;
        break;
      default: break;
    }
#endif
    const auto lhs=xmm[m.reg],rhs=read_xmm_operand(m.rm,true);
    auto result=lhs;
    switch(opcode){
      case 0xdb: case 0xdf: case 0xeb: { // PAND, PANDN, POR
        for(unsigned i=0;i<16;++i)
          result[i]=opcode==0xdb?std::uint8_t(lhs[i]&rhs[i]):
                    opcode==0xdf?std::uint8_t(~lhs[i]&rhs[i]):
                                     std::uint8_t(lhs[i]|rhs[i]);
        break;
      }
      case 0x60: case 0x61: case 0x62: case 0x6c:
      case 0x68: case 0x69: case 0x6a: case 0x6d: { // PUNPCK low/high
        const bool high=(opcode==0x68||opcode==0x69||opcode==0x6a||opcode==0x6d);
        const unsigned size=(opcode==0x60||opcode==0x68)?1U:
                            (opcode==0x61||opcode==0x69)?2U:
                            (opcode==0x62||opcode==0x6a)?4U:8U;
        const unsigned first=high?8U:0U;
        for(unsigned i=0;i<8U/size;++i){
          for(unsigned j=0;j<size;++j) {
            result[(2*i)*size+j]=lhs[first+i*size+j];
            result[(2*i+1)*size+j]=rhs[first+i*size+j];
          }
        }
        break;
      }
      default: { // Packed integer arithmetic, comparisons, logical shifts
        const bool is_compare=(opcode>=0x74&&opcode<=0x76)||
                               (opcode>=0x64&&opcode<=0x66);
        const bool is_add=(opcode>=0xfc&&opcode<=0xfe)||opcode==0xd4;
        const bool is_sub=(opcode>=0xf8&&opcode<=0xfa)||opcode==0xfb;
        const bool is_shift=(opcode>=0xd1&&opcode<=0xd3)||
                             (opcode>=0xf1&&opcode<=0xf3);
        if(!is_compare&&!is_add&&!is_sub&&!is_shift)
          fail("packed SSE2 arithmetic opcode not implemented",FaultCode::UnsupportedOpcode);
        const unsigned size=
          (opcode==0x74||opcode==0x64||opcode==0xfc||opcode==0xf8)?1U:
          (opcode==0x75||opcode==0x65||opcode==0xfd||opcode==0xf9||
           opcode==0xd1||opcode==0xf1)?2U:
          (opcode==0x76||opcode==0x66||opcode==0xfe||opcode==0xfa||
           opcode==0xd2||opcode==0xf2)?4U:8U;
        const auto lane_mask=size==8?UINT64_MAX:((1ULL<<(8*size))-1);
        const auto count=is_shift?packed_lane(rhs,0,8):0ULL;
        for(unsigned offset=0;offset<16;offset+=size){
          const auto l=packed_lane(lhs,offset,size);
          const auto r=packed_lane(rhs,offset,size);
          std::uint64_t next=0;
          if(is_compare) {
            const bool matches=opcode>=0x74?
                l==r:signed_value(l,size)>signed_value(r,size);
            next=matches?lane_mask:0;
          }else if(is_add)next=(l+r)&lane_mask;
          else if(is_sub)next=(l-r)&lane_mask;
          else if(count>=size*8)next=0;
          else if(opcode>=0xf1&&opcode<=0xf3)next=(l<<count)&lane_mask;
          else next=l>>count;
          packed_set(result,offset,size,next);
        }
        break;
      }
    }
    xmm[m.reg]=result; // packed integer SSE2 leaves EFLAGS unchanged
  }
  void require_memory(const Operand& operand) const {
    if (operand.is_register) fail("instruction requires a memory operand", FaultCode::InvalidInstruction);
  }
  void szp(std::uint64_t value, unsigned bytes) {
    value &= mask(bytes);
    set_flag(SF, (value & sign_bit(bytes)) != 0);
    set_flag(ZF, value == 0);
    unsigned parity = value & 0xffU;
    parity ^= parity >> 4; parity ^= parity >> 2; parity ^= parity >> 1;
    set_flag(PF, (parity & 1U) == 0);
  }
  // Group 1: ADD, OR, ADC, SBB, AND, SUB, XOR, CMP.
  std::uint64_t alu(unsigned operation, std::uint64_t a, std::uint64_t b, unsigned bytes) {
    const auto m = mask(bytes), sign = sign_bit(bytes);
    a &= m; b &= m;
    std::uint64_t result;
    if (operation == 1 || operation == 4 || operation == 6) {
      result = operation == 1 ? a | b : operation == 4 ? a & b : a ^ b;
      set_flag(CF, false); set_flag(OF, false);
      // AF is architecturally undefined for logical operations: preserve it.
    } else {
      const unsigned carry = (operation == 2 || operation == 3) && flag(CF) ? 1 : 0;
      const bool subtract = operation == 3 || operation == 5 || operation == 7;
      if (subtract) {
        result = (a - b - carry) & m;
        set_flag(CF, a < b || (carry && a == b));
        set_flag(OF, ((a ^ b) & (a ^ result) & sign) != 0);
      } else {
        const auto partial = a + b, full = partial + carry;
        result = full & m;
        set_flag(CF, bytes == 8 ? (partial < a || full < partial) : full > m);
        set_flag(OF, (~(a ^ b) & (a ^ result) & sign) != 0);
      }
      set_flag(AF, ((a ^ b ^ result) & 0x10U) != 0);
    }
    szp(result, bytes);
    return result;
  }
  bool condition(unsigned cc) const {
    const bool of = flag(OF), cf = flag(CF), zf = flag(ZF), sf = flag(SF), pf = flag(PF);
    switch (cc) {
      case 0: return of; case 1: return !of; case 2: return cf; case 3: return !cf;
      case 4: return zf; case 5: return !zf; case 6: return cf || zf; case 7: return !cf && !zf;
      case 8: return sf; case 9: return !sf; case 10: return pf; case 11: return !pf;
      case 12: return sf != of; case 13: return sf == of;
      case 14: return zf || sf != of; default: return !zf && sf == of;
    }
  }
  void jump(std::uint64_t target, unsigned bytes) {
    target &= mask(bytes);
    (void)segment_linear(CS, target, 1, Access::Execute);
    eip = target; ip_written_ = true;
  }
  void relative(std::int64_t displacement, unsigned bytes) {
    jump(cursor_ + std::uint64_t(displacement), is_long_mode() ? 8 : bytes);
  }
  void push(std::uint64_t value, unsigned bytes) {
    const auto n = stack_address_width();
    const auto sp = (get_reg(ESP, n) - bytes) & mask(n);
    write_segment(SS, sp, bytes, value);
    put_reg(ESP, n, sp);
  }
  std::uint64_t pop(unsigned bytes) {
    const auto n = stack_address_width();
    const auto sp = get_reg(ESP, n);
    const auto value = read_segment(SS, sp, bytes);
    put_reg(ESP, n, sp + bytes);
    return value;
  }
  void enter_frame(std::uint16_t allocation, unsigned nesting, unsigned bytes) {
    // Operand size selects the saved frame-pointer slots. SS.B (or long mode)
    // independently selects the stack address width; 67 and segment overrides
    // never redirect these implicit SS accesses. The lexical level is imm8%32,
    // bounding this instruction to at most 32 pushes.
    const auto n = stack_address_width();
    auto previous = get_reg(EBP, n);
    push(get_reg(EBP, bytes), bytes);
    const auto frame = get_reg(ESP, bytes);
    for (unsigned level = 1; level < nesting; ++level) {
      previous = (previous - bytes) & mask(n);
      push(read_segment(SS, previous, bytes), bytes);
    }
    if (nesting != 0) push(frame, bytes);

    const auto sp = (get_reg(ESP, n) - allocation) & mask(n);
    // ENTER validates a write at the final stack pointer even though reserving
    // locals does not write their contents. Keep the probe on the CPU's normal
    // segment/page path, without allocating or modifying the reserved RAM.
    const auto address = segment_linear(SS, sp, bytes, Access::Write);
    for (unsigned i = 0; i < bytes; ++i)
      (void)physical_index(translate(address + i, Access::Write));
    put_reg(EBP, bytes, frame);
    put_reg(ESP, n, sp);
  }
  std::uint64_t index(unsigned r) const { return get_reg(r, address_width()); }
  void put_index(unsigned r, std::uint64_t value) { put_reg(r, address_width(), value); }
  void advance_index(unsigned r, unsigned bytes) {
    put_index(r, index(r) + (flag(DF) ? -std::uint64_t(bytes) : bytes));
  }
  std::uint32_t port_in(std::uint16_t port, unsigned bytes) {
    if (cpl() > ((eflags >> 12) & 3U))
      fail("CPL exceeds IOPL for IN (TSS I/O bitmap not modeled)", FaultCode::ProtectionFault);
    if (!in_hook) fail("IN has no port backend", FaultCode::UnsupportedPort);
    const auto value = in_hook(port, bytes);
    if (!value) fail("IN port not implemented: " + std::to_string(port), FaultCode::UnsupportedPort);
    return *value & mask(bytes);
  }
  void port_out(std::uint16_t port, unsigned bytes, std::uint32_t value) {
    if (cpl() > ((eflags >> 12) & 3U))
      fail("CPL exceeds IOPL for OUT (TSS I/O bitmap not modeled)", FaultCode::ProtectionFault);
    if (!out_hook || !out_hook(port, bytes, value & mask(bytes)))
      fail("OUT port not implemented: " + std::to_string(port), FaultCode::UnsupportedPort);
  }
  void restore_flags(std::uint32_t value, unsigned bytes) {
    if (value & TF) fail("trap flag requires an unimplemented debug exception", FaultCode::UnsupportedState);
    // POPFD cannot change VM/RF; accepted bits exclude reserved flags.
    std::uint32_t writable = 0x00247fd5U;
    // POPF at CPL3 cannot change IOPL; IF is preserved unless CPL<=IOPL.
    if (cpl() != 0) writable &= ~0x3000U;
    if (cpl() > ((eflags >> 12) & 3U)) writable &= ~IF;
    const auto bits = writable & mask(bytes);
    eflags = (eflags & ~bits) | (value & bits) | 2;
  }
  void interrupt(std::uint8_t vector) {
    if (!is_real_mode()) {
      dispatch_idt(vector, cursor_, true, false, 0);
      return;
    }
    eip = cursor_ & 0xffffU; ip_written_ = true;
    if (interrupt_hook && interrupt_hook(*this, vector)) return;
    const auto table_offset = std::uint32_t(vector) * 4;
    if (table_offset + 3 > idtr.limit) fail("interrupt vector exceeds IDTR limit", FaultCode::SegmentLimit);
    const auto new_ip = read16(idtr.base + table_offset);
    const auto new_cs = read16(idtr.base + table_offset + 2);
    // Real-mode IVT delivery always saves FLAGS, CS and IP as three words;
    // unlike CALL, an operand-size prefix does not widen this frame.
    push(eflags, 2); push(segments[CS], 2); push(eip, 2);
    eflags &= ~(IF | TF);
    segments[CS] = new_cs; jump(new_ip, 2);
  }

  static int exception_vector(FaultCode code) {
    switch (code) {
      case FaultCode::DivideError: return 0;      // #DE
      case FaultCode::InvalidInstruction: return 6;
      case FaultCode::DeviceNotAvailable: return 7; // #NM
      case FaultCode::SimdFloatingPoint: return 19; // #XM/#XF
      case FaultCode::X87FloatingPoint: return 16; // #MF deferred x87 error
      case FaultCode::ProtectionFault:
      case FaultCode::SegmentLimit: return 13;    // #GP
      case FaultCode::PageFault: return 14;        // #PF
      default: return -1; // Unsupported opcodes and host/resource failures.
    }
  }
  void dispatch_idt(std::uint8_t vector, std::uint64_t resume_ip,
                    bool software, bool has_error, std::uint32_t error) {
    // CPU, not the guest, reads the IDT/GDT/TSS with supervisor privileges.
    // Guest code still obeys its actual CPL and page U/S permissions.
    const bool long_gate = is_long_mode();
    if (!long_gate && (!cache_[CS].big || (efer & 0x400)))
      fail("protected16/compatibility IDT gates not implemented", FaultCode::UnsupportedState);
    const auto old_cpl = cpl();
    SupervisorAccess system(*this);
    const unsigned length = long_gate ? 16U : 8U;
    const auto slot = std::uint64_t(vector) * length;
    if (slot + length - 1 > idtr.limit || idtr.base > UINT64_MAX - slot - length)
      fail("interrupt vector outside guest IDT", FaultCode::ProtectionFault);
    const auto address = idtr.base + slot;
    if (long_gate && (!canonical(address) || !canonical(address + length - 1)))
      fail("noncanonical IDT entry", FaultCode::ProtectionFault);
    const auto low = read_linear(address, 8);
    const auto high = long_gate ? read_linear(address + 8, 8) : 0;
    const auto selector = std::uint16_t((low >> 16) & 0xffffU);
    const auto ist = unsigned((low >> 32) & 7U);
    const auto reserved = unsigned((low >> 35) & 31U);
    const auto attr = unsigned((low >> 40) & 255U);
    const auto type = attr & 31U;
    if ((type != 0xe && type != 0xf) || !(selector & 0xfffcU))
      fail("IDT gate is not a 32/64 interrupt or trap gate", FaultCode::ProtectionFault);
    if (!(attr & 0x80)) fail("IDT gate not present", FaultCode::ProtectionFault);
    if (software && old_cpl > ((attr >> 5) & 3U))
      fail("INT gate DPL check failed", FaultCode::ProtectionFault);
    if (reserved || (!long_gate && ist) || (long_gate && (high >> 32)))
      fail("IDT gate reserved fields are set", FaultCode::ProtectionFault);
    const auto target_ip = (low & 0xffffU) | (((low >> 48) & 0xffffU) << 16) |
                           ((high & 0xffffffffULL) << 32);
    if (long_gate && !canonical(target_ip))
      fail("noncanonical IDT handler RIP", FaultCode::ProtectionFault);
    const auto target_cache = descriptor(selector);
    if (!target_cache.code || (long_gate && !target_cache.long_code) ||
        (!long_gate && !target_cache.big))
      fail("IDT destination is not a compatible code segment", FaultCode::ProtectionFault);
    const auto target_cpl = target_cache.conforming ? old_cpl : unsigned(target_cache.dpl);
    if (target_cache.dpl > old_cpl ||
        target_cpl > old_cpl || (target_cpl != 0 && target_cpl != 3))
      fail("IDT destination privilege transition not supported", FaultCode::ProtectionFault);
    const bool switch_cpl = target_cpl < old_cpl;
    const auto original_cs = segments[CS], original_ss = segments[SS];
    const auto original_rsp = regs[ESP];
    const auto original_flags = eflags;
    const unsigned width = long_gate ? 8U : 4U;
    if (switch_cpl || (long_gate && ist)) {
      if (!tr.valid) fail("interrupt stack switch needs a loaded TSS", FaultCode::ProtectionFault);
      std::uint64_t new_rsp = 0;
      if (long_gate) {
        const auto tss_offset = ist ? 36U + (ist-1U)*8U : 4U;
        if (tr.limit < tss_offset+7U)
          fail("TSS does not contain the requested 64-bit stack", FaultCode::ProtectionFault);
        new_rsp = read_linear(tr.base+tss_offset,8);
        if (!canonical(new_rsp) || !new_rsp)
          fail("TSS provided a noncanonical or null RSP", FaultCode::ProtectionFault);
        // Null ring0 SS is allowed during the IA-32e interrupt stack switch.
        segments[SS] = 0;
        cache_[SS] = SegmentCache{};
        regs[ESP] = new_rsp;
      } else {
        if (!switch_cpl || target_cpl != 0 || tr.limit < 9)
          fail("protected32 stack switch requires TSS SS0:ESP0", FaultCode::UnsupportedState);
        new_rsp = read_linear(tr.base+4,4);
        const auto ss0 = std::uint16_t(read_linear(tr.base+8,2));
        const auto ss_cache = descriptor(ss0);
        if (!ss_cache.valid || ss_cache.code || !ss_cache.writable ||
            ss_cache.dpl != 0 || (ss0 & 3U))
          fail("protected32 TSS SS0 must be writable ring0 data", FaultCode::ProtectionFault);
        segments[SS] = ss0;
        cache_[SS] = ss_cache;
        put_reg(ESP,4,new_rsp);
      }
    }
    if (long_gate) {
      // 64-bit gate pushes SS:RSP even for a same-CPL interrupt.
      push(original_ss,8); push(original_rsp,8);
    } else if (switch_cpl) {
      push(original_ss,4); push(original_rsp,4);
    }
    push(original_flags,width);push(original_cs,width);push(resume_ip,width);
    if (has_error) push(error,width);
    segments[CS] = std::uint16_t((selector & ~3U) | target_cpl);
    cache_[CS] = target_cache;
    jump(target_ip,width);
    if (type == 0xe) eflags &= ~IF;
    eflags &= ~TF;
  }

  void string_operation(std::uint8_t op);
  void shift(const ModRM& m, unsigned bytes, unsigned count);
  void double_shift(const ModRM& m, unsigned bytes, unsigned count, bool left);
  void group3(const ModRM& m, unsigned bytes);
  void multiply_signed(unsigned destination, std::uint64_t a, std::uint64_t b, unsigned bytes);
  SegmentCache descriptor(std::uint16_t selector);
  void load_segment(unsigned segment, std::uint16_t selector);
  void far_transfer(std::uint16_t selector, std::uint64_t target, unsigned bytes);
  void write_cr0(std::uint64_t value);
  void write_cr3(std::uint64_t value);
  void write_cr4(std::uint64_t value);
  void write_msr(std::uint32_t number, std::uint64_t value);
  std::uint64_t read_msr(std::uint32_t number);
  [[noreturn]] void page_fault(std::uint64_t address, Access access,
                              bool present, bool reserved, const std::string& reason) const;
  void extended();
  void execute(std::uint8_t op);
};

inline Cpu::SegmentCache Cpu::descriptor(std::uint16_t selector) {
  if (selector & 4) fail("LDT selectors are not implemented", FaultCode::UnsupportedState);
  SupervisorAccess system(*this);
  const std::uint32_t offset = selector & 0xfff8U;
  if (!offset || offset + 7 > gdtr.limit)
    fail("selector outside GDT or null code/stack selector", FaultCode::ProtectionFault);
  if (gdtr.base > UINT64_MAX - offset - 7)
    fail("GDT address overflow", FaultCode::ProtectionFault);
  const auto raw = read_linear(gdtr.base + offset, 8);
  const unsigned access = (raw >> 40) & 0xffU, attributes = (raw >> 52) & 15U;
  if (!(access & 0x80)) fail("segment descriptor not present", FaultCode::ProtectionFault);
  if (!(access & 0x10)) fail("system segments, call gates and task switches are not implemented", FaultCode::UnsupportedState);
  // DPL0..3 are valid; caller enforces CPL/RPL privilege rules.
  if (access & 4) fail("conforming code / expand-down segments are not implemented", FaultCode::UnsupportedState);
  SegmentCache result;
  result.base = ((raw >> 16) & 0xffffffU) | ((raw >> 32) & 0xff000000U);
  result.limit = (raw & 0xffffU) | ((raw >> 32) & 0xf0000U);
  if (attributes & 8) result.limit = (result.limit << 12) | 0xfffU;
  result.code = (access & 8) != 0;
  result.dpl = std::uint8_t((access >> 5) & 3U);
  result.conforming = result.code && (access & 4U);
  result.readable = !result.code || (access & 2);
  result.writable = !result.code && (access & 2);
  result.big = (attributes & 4) != 0;
  result.long_code = result.code && (attributes & 2);
  if ((result.long_code && result.big) || (!result.code && (attributes & 2)))
    fail("invalid segment L/D attributes", FaultCode::ProtectionFault);
  if (result.long_code && !(efer & 0x400))
    fail("64-bit code descriptor requires active IA-32e paging", FaultCode::ProtectionFault);
  // Loading a segment sets its accessed bit. This write obeys paging/WP.
  if (!(access & 1)) write_linear(gdtr.base + offset + 5, 1, access | 1U);
  return result;
}

inline void Cpu::load_segment(unsigned segment, std::uint16_t selector) {
  if (segment >= 6 || segment == CS) fail("invalid segment load", FaultCode::InvalidInstruction);
  if (is_real_mode()) {
    segments[segment] = selector;
    cache_[segment] = SegmentCache{}; cache_[segment].base = std::uint64_t(selector) << 4;
    return;
  }
  if ((selector & 0xfffcU) == 0) {
    if (segment == SS) fail("null SS is not implemented for this privilege profile", FaultCode::ProtectionFault);
    segments[segment] = selector; cache_[segment].valid = false;
    // AMD64 preserves the FS/GS hidden base when loading a null selector.
    return;
  }
  const auto value = descriptor(selector);
  if ((segment == SS && (value.code || !value.writable)) || (value.code && !value.readable))
    fail("segment type is invalid for this data/stack register", FaultCode::ProtectionFault);
  const auto requested = unsigned(selector & 3U), privilege = cpl();
  if (segment == SS) {
    if (requested != privilege || value.dpl != privilege)
      fail("stack selector RPL/DPL must equal CPL", FaultCode::ProtectionFault);
  } else if (!value.conforming && std::max(requested, privilege) > value.dpl) {
    fail("data selector DPL/RPL access denied", FaultCode::ProtectionFault);
  }
  segments[segment] = selector; cache_[segment] = value;
  if (segment == FS) fs_base = value.base;
  if (segment == GS) gs_base = value.base;
}

inline void Cpu::far_transfer(std::uint16_t selector, std::uint64_t target, unsigned bytes) {
  if (is_real_mode()) {
    segments[CS] = selector; jump(target, bytes); return;
  }
  const auto value = descriptor(selector);
  if (!value.code) fail("far transfer target is not a code segment", FaultCode::ProtectionFault);
  if ((selector & 3U) > cpl() ||
      (value.conforming ? value.dpl > cpl() : value.dpl != cpl()))
    fail("far control transfer privilege check failed", FaultCode::ProtectionFault);
  segments[CS] = std::uint16_t((selector & ~3U) | cpl()); cache_[CS] = value;
  jump(target, value.long_code ? 8 : bytes);
}

inline void Cpu::write_cr0(std::uint64_t value) {
  constexpr std::uint32_t allowed = 0xe005003fU;
  if (value & ~std::uint64_t(allowed)) fail("reserved CR0 bits", FaultCode::ProtectionFault);
  if ((value & 0x20000000U) && !(value & 0x40000000U))
    fail("CR0.NW requires CR0.CD", FaultCode::ProtectionFault);
  if ((value & 0x80000000U) && !(value & 1))
    fail("CR0.PG requires CR0.PE", FaultCode::ProtectionFault);
  if ((cr0 & 1) && !(value & 1))
    fail("return from protected mode to real/unreal mode is not implemented", FaultCode::UnsupportedState);
  const bool enabling_paging = !(cr0 & 0x80000000U) && (value & 0x80000000U);
  if (enabling_paging) {
    if (!(efer & 0x100)) fail("legacy non-IA-32e paging is not implemented", FaultCode::UnsupportedState);
    if (!(cr4 & 0x20)) fail("long-mode paging requires CR4.PAE", FaultCode::ProtectionFault);
    if (cache_[CS].long_code) fail("paging cannot be enabled while CS.L is set", FaultCode::ProtectionFault);
    if (cr3 & 0xfe7U) fail("PML4 CR3 address is misaligned", FaultCode::ProtectionFault);
  }
  if ((cr0 & 0x80000000U) && !(value & 0x80000000U) && is_long_mode())
    fail("leave 64-bit code before disabling paging", FaultCode::ProtectionFault);
  if (!(cr0 & 1) && (value & 1)) {
    for (unsigned s = 0; s < 6; ++s) {
      cache_[s] = SegmentCache{}; cache_[s].base = std::uint64_t(segments[s]) << 4;
      cache_[s].code = s == CS; cache_[s].writable = s != CS;
      cache_[s].inherited_from_real_mode = true;
    }
  }
  cr0 = std::uint32_t(value) | 0x10U;
  if (enabling_paging) efer |= 0x400U;
  else if (!(cr0 & 0x80000000U)) efer &= ~std::uint64_t{0x400};
}

inline void Cpu::write_cr3(std::uint64_t value) {
  // This implementation has four-level paging, a 52-bit physical address
  // field and no PCID. PWT/PCD are retained; the bus has no cache timing model.
  if (value & ~std::uint64_t{0x000ffffffffff018ULL})
    fail("unsupported/reserved CR3 address or PCID bits", FaultCode::ProtectionFault);
  cr3 = value;
}

inline void Cpu::write_cr4(std::uint64_t value) {
  if ((efer & 0x400) && !(value & 0x20))
    fail("cannot clear CR4.PAE while long mode is active", FaultCode::ProtectionFault);
  // PSE, PAE, PGE. With no TLB, PGE has no cached translations to retain.
  constexpr std::uint32_t supported = 0x000006b0U; // OSFXSR and OSXMMEXCPT, no OSXSAVE
  if (value & ~std::uint64_t(supported))
    fail("CR4 requests an unimplemented feature (e.g. SIMD, PCID, LA57, SMEP or SMAP)", FaultCode::UnsupportedState);
  cr4 = std::uint32_t(value);
}

inline std::uint64_t Cpu::read_msr(std::uint32_t number) {
  if (number == 0xc0000080U) return efer;
  if (number == 0xc0000100U) return fs_base;
  if (number == 0xc0000101U) return gs_base;
  fail("RDMSR register is not implemented", FaultCode::UnsupportedState);
}

inline void Cpu::write_msr(std::uint32_t number, std::uint64_t value) {
  if (number == 0xc0000080U) {
    constexpr std::uint64_t supported = 0xd00U; // LME, read-only LMA, NXE; no SYSCALL.
    if (value & ~supported) fail("EFER requests an unimplemented feature or reserved bit", FaultCode::UnsupportedState);
    if ((cr0 & 0x80000000U) && ((value ^ efer) & 0x100))
      fail("cannot change EFER.LME while paging is enabled", FaultCode::ProtectionFault);
    efer = (value & ~std::uint64_t{0x400}) | (efer & 0x400);
    return;
  }
  if (number == 0xc0000100U || number == 0xc0000101U) {
    if (!canonical(value)) fail("non-canonical FS/GS base MSR", FaultCode::ProtectionFault);
    if (number == 0xc0000100U) { fs_base = value; cache_[FS].base = value; }
    else { gs_base = value; cache_[GS].base = value; }
    return;
  }
  fail("WRMSR register is not implemented", FaultCode::UnsupportedState);
}

inline void Cpu::page_fault(std::uint64_t address, Access access,
                            bool present, bool reserved, const std::string& reason) const {
  Trap trap{FaultCode::PageFault, reason};
  trap.address = address;
  trap.error = (present ? 1U : 0U) | (access == Access::Write ? 2U : 0U) |
               ((cpl() == 3 && !supervisor_internal_) ? 4U : 0U) |
               (reserved ? 8U : 0U) | (access == Access::Execute ? 16U : 0U);
  throw trap;
}

inline std::uint64_t Cpu::translate(std::uint64_t address, Access access) {
  if (!(cr0 & 0x80000000U)) return address;
  ++page_walks;
  if (!(efer & 0x400) || !(cr4 & 0x20))
    fail("only four-level IA-32e paging is implemented", FaultCode::UnsupportedState);
  if (!canonical(address)) fail("non-canonical page-walk address", FaultCode::ProtectionFault);
  constexpr std::uint64_t address_mask = 0x000ffffffffff000ULL;
  std::uint64_t table = cr3 & address_mask;
  bool writable = true, executable = true, user_access = true;
  const bool user_mode = cpl() == 3 && !supervisor_internal_;
  for (unsigned level = 4; level != 0; --level) {
    const unsigned shift = 12 + 9 * (level - 1);
    const auto entry_address = table + (((address >> shift) & 0x1ffU) * 8);
    const auto entry = read64(entry_address);
    if (!(entry & 1)) page_fault(address, access, false, false, "page-table entry is not present");
    if ((entry >> 63) && !(efer & 0x800))
      page_fault(address, access, true, true, "NX bit is reserved while EFER.NXE is clear");
    if (level == 4 && (entry & 0x80))
      page_fault(address, access, true, true, "PML4 page-size bit is reserved");
    if (level == 3 && (entry & 0x80))
      fail("1-GiB pages are not implemented", FaultCode::UnsupportedState);
    const bool large = level == 2 && (entry & 0x80);
    if (large && (entry & 0x1fe000U))
      page_fault(address, access, true, true, "2-MiB page physical base is misaligned");
    writable = writable && (entry & 2);
    user_access = user_access && (entry & 4);
    executable = executable && !(entry >> 63);
    const bool leaf = level == 1 || large;
    if (leaf) {
      if (user_mode && !user_access)
        page_fault(address, access, true, false, "CPL3 access denied by U/S page permissions");
      if (access == Access::Write && !writable && (user_mode || (cr0 & 0x10000U)))
        page_fault(address, access, true, false, "page is read-only for this privilege");
      if (access == Access::Execute && !executable)
        page_fault(address, access, true, false, "instruction fetch denied by NX page permissions");
    }
    const auto marked = entry | 0x20U | (leaf && access == Access::Write ? 0x40U : 0U);
    if (marked != entry) write64(entry_address, marked);
    if (leaf) {
      const std::uint64_t offset_mask = large ? 0x1fffffU : 0xfffU;
      return (entry & address_mask & ~offset_mask) | (address & offset_mask);
    }
    table = entry & address_mask;
  }
  fail("invalid page walker state", FaultCode::HostError);
}

inline void Cpu::string_operation(std::uint8_t op) {
  if (repeat_ && index(ECX) == 0) return;
  const unsigned bytes = (op & 1U) ? (op < 0x70 ? (op32_ ? 4 : 2) : width()) : 1;
  const auto src = index(ESI), dst = index(EDI);
  bool compare = false;
  switch (op & 0xfeU) {
    case 0xa4: {
      const auto value = read_segment(data_segment(), src, bytes);
      write_segment(ES, dst, bytes, value);
      advance_index(ESI, bytes); advance_index(EDI, bytes); break;
    }
    case 0xa6:
      (void)alu(7, read_segment(data_segment(), src, bytes),
                read_segment(ES, dst, bytes), bytes);
      advance_index(ESI, bytes); advance_index(EDI, bytes); compare = true; break;
    case 0xaa:
      write_segment(ES, dst, bytes, get_reg(EAX, bytes));
      advance_index(EDI, bytes); break;
    case 0xac:
      put_reg(EAX, bytes, read_segment(data_segment(), src, bytes));
      advance_index(ESI, bytes); break;
    case 0xae:
      (void)alu(7, get_reg(EAX, bytes), read_segment(ES, dst, bytes), bytes);
      advance_index(EDI, bytes); compare = true; break;
    case 0x6c: {
      const auto address = segment_linear(ES, dst, bytes, Access::Write);
      // Validate and reserve destination backing before consuming port input.
      for (unsigned i = 0; i < bytes; ++i)
        memory.prepare_write(physical_index(translate(address + i, Access::Write)), 1);
      write_linear(address, bytes, port_in(reg16(EDX), bytes));
      advance_index(EDI, bytes); break;
    }
    case 0x6e: {
      const auto value = read_segment(data_segment(), src, bytes);
      port_out(reg16(EDX), bytes, value);
      advance_index(ESI, bytes); break;
    }
    default: fail("unknown string instruction", FaultCode::InvalidInstruction);
  }
  if (repeat_) {
    put_index(ECX, index(ECX) - 1);
    if (index(ECX) != 0 && (!compare || (repeat_ == 0xf3 ? flag(ZF) : !flag(ZF)))) {
      // Decode again on the next bounded step, permitting host cancellation.
      eip = start_ip_; ip_written_ = true;
    }
  }
}

inline void Cpu::shift(const ModRM& m, unsigned bytes, unsigned count) {
  count &= bytes == 8 ? 63 : 31;
  const auto original = get(m.rm, bytes);
  // The destination is still written for a zero count. In long mode an r32
  // destination therefore clears its high half, while RFLAGS stay unchanged.
  if (count == 0) { put(m.rm, bytes, original); return; }
  const auto bits = 8U * bytes;
  const auto mask_value = mask(bytes), sign = sign_bit(bytes);
  auto value = original;
  const auto operation = m.group;
  unsigned effective = count;
  if (operation <= 1) effective %= bits;
  if (operation == 2 || operation == 3) effective %= (bits + 1);
  if ((operation == 2 || operation == 3) && effective == 0) { put(m.rm, bytes, original); return; }
  for (unsigned i = 0; i < effective; ++i) {
    const bool old_cf = flag(CF);
    switch (operation) {
      case 0: value = ((value << 1) | ((value & sign) ? 1U : 0U)) & mask_value; break;
      case 1: value = (value >> 1) | ((value & 1) ? sign : 0U); break;
      case 2: set_flag(CF, (value & sign) != 0); value = ((value << 1) | (old_cf ? 1U : 0U)) & mask_value; break;
      case 3: set_flag(CF, (value & 1) != 0); value = (value >> 1) | (old_cf ? sign : 0U); break;
      case 4: case 6: set_flag(CF, (value & sign) != 0); value = (value << 1) & mask_value; break;
      case 5: set_flag(CF, (value & 1) != 0); value >>= 1; break;
      case 7: set_flag(CF, (value & 1) != 0); value = (value >> 1) | (value & sign); break;
    }
  }
  if (operation == 0) set_flag(CF, (value & 1) != 0);
  if (operation == 1) set_flag(CF, (value & sign) != 0);
  if (operation >= 4) szp(value, bytes);
  if (count == 1) {
    if (operation == 0 || operation == 2 || operation == 4 || operation == 6)
      set_flag(OF, ((value & sign) != 0) != flag(CF));
    else if (operation == 1 || operation == 3)
      set_flag(OF, ((value & sign) != 0) != ((value & (sign >> 1)) != 0));
    else if (operation == 5) set_flag(OF, (original & sign) != 0);
    else set_flag(OF, false);
  }
  // OF for a multi-bit shift/rotate, and AF for shifts, are undefined; preserve.
  put(m.rm, bytes, value);
}

inline void Cpu::double_shift(const ModRM& m, unsigned bytes, unsigned count, bool left) {
  // Intel SDM Vol. 2B, SHLD/SHRD: the mask depends on operand size, not on
  // address size. Fetch/decode the entire instruction before resolving RIP.
  // https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html
  count &= bytes == 8 ? 63 : 31;
  const auto original = get(m.rm, bytes);
  // A zero count preserves flags but still writes the destination, including
  // zero-extension of an r32 destination in long mode and memory protection.
  if (count == 0) { put(m.rm, bytes, original); return; }
  const auto source = get_reg(m.reg, bytes);
  const unsigned bits = bytes * 8U;
  std::uint64_t value;
  if (count > bits) {
    // Only the 16-bit form can reach this case after masking. Intel leaves its
    // result and arithmetic flags undefined. Our deterministic policy rotates
    // the concatenated 32-bit pair and preserves those flags; callers must not
    // rely on this policy as an architectural guarantee. Counts are 17..31,
    // so neither expression can invoke an oversized or negative C++ shift.
    const auto pair = std::uint32_t(left ? (original << 16) | source
                                         : (source << 16) | original);
    const auto rotated = left ? std::uint32_t((pair << count) | (pair >> (32U - count)))
                              : std::uint32_t((pair >> count) | (pair << (32U - count)));
    put(m.rm, bytes, left ? rotated >> 16 : rotated & 0xffffU);
    return;
  }
  // A full-width count is possible only for 16-bit operands. Handle it
  // separately so a 64-bit C++ operand is never shifted by 64.
  if (count == bits) value = source;
  else value = left ? (original << count) | (source >> (bits - count))
                    : (original >> count) | (source << (bits - count));
  value &= mask(bytes);
  set_flag(CF, ((original >> (left ? bits - count : count - 1)) & 1U) != 0);
  szp(value, bytes);
  if (count == 1) set_flag(OF, ((original ^ value) & sign_bit(bytes)) != 0);
  // AF is undefined for every nonzero shift; OF is undefined for count > 1.
  // Preserve both wherever the architecture does not define their value.
  put(m.rm, bytes, value);
}

inline void Cpu::multiply_signed(unsigned destination, std::uint64_t a,
                                 std::uint64_t b, unsigned bytes) {
  const auto value_mask = mask(bytes);
  const auto sign_bit = std::uint64_t{1} << (bytes * 8U - 1);
  a &= value_mask;
  b &= value_mask;
  // Unsigned magnitudes also represent INT64_MIN without negating a signed
  // minimum. Check the mathematical product before its modular multiplication;
  // no overflowing signed arithmetic or compiler-specific intrinsic is needed.
  const auto magnitude_a = (a & sign_bit) ? ((std::uint64_t{0} - a) & value_mask) : a;
  const auto magnitude_b = (b & sign_bit) ? ((std::uint64_t{0} - b) & value_mask) : b;
  const auto limit = ((a ^ b) & sign_bit) ? sign_bit : sign_bit - 1;
  const bool overflow = magnitude_b != 0 && magnitude_a > limit / magnitude_b;
  const auto low = (a * b) & value_mask;
  put_reg(destination, bytes, low);
  set_flag(CF, overflow); set_flag(OF, overflow);
}

inline void Cpu::group3(const ModRM& m, unsigned bytes) {
  if (m.group == 0) {
    const auto value = arithmetic_immediate(bytes); // RIP base follows the complete instruction.
    (void)alu(4, get(m.rm, bytes), value, bytes); return;
  }
  if (bytes == 8 && m.group >= 4)
    fail("64-bit implicit MUL/IMUL/DIV/IDIV are not implemented", FaultCode::UnsupportedOpcode);
  const auto operand = get(m.rm, bytes);
  const auto bits = bytes * 8U;
  const auto maximum = mask(bytes);
  switch (m.group) {
    case 1: fail("reserved group-3 encoding", FaultCode::InvalidInstruction);
    case 2: put(m.rm, bytes, ~operand); return;
    case 3: put(m.rm, bytes, alu(5, 0, operand, bytes)); return;
    case 4: case 5: {
      std::uint64_t product;
      bool overflow;
      if (m.group == 4) {
        product = std::uint64_t(get_reg(EAX, bytes)) * operand;
        overflow = (product >> bits) != 0;
      } else {
        const auto signed_product = signed_value(get_reg(EAX, bytes), bytes) * signed_value(operand, bytes);
        product = std::uint64_t(signed_product);
        overflow = signed_value(std::uint32_t(product) & maximum, bytes) != signed_product;
      }
      if (bytes == 1) set_reg16(EAX, std::uint16_t(product));
      else { put_reg(EAX, bytes, std::uint32_t(product)); put_reg(EDX, bytes, std::uint32_t(product >> bits)); }
      set_flag(CF, overflow); set_flag(OF, overflow); return;
    }
    case 6: case 7: {
      if (operand == 0) fail("integer divide by zero", FaultCode::DivideError);
      const std::uint64_t dividend = bytes == 1 ? reg16(EAX)
          : (std::uint64_t(get_reg(EDX, bytes)) << bits) | get_reg(EAX, bytes);
      std::uint32_t quotient, remainder;
      if (m.group == 6) {
        const auto q = dividend / operand;
        if (q > maximum) fail("integer quotient overflow", FaultCode::DivideError);
        quotient = std::uint32_t(q); remainder = std::uint32_t(dividend % operand);
      } else {
        std::int64_t signed_dividend;
        if (bytes == 1) signed_dividend = signed_value(std::uint32_t(dividend), 2);
        else if (bytes == 2) signed_dividend = signed_value(std::uint32_t(dividend), 4);
        else {
          // Avoid implementation-defined conversion of unsigned > INT64_MAX.
          signed_dividend = (dividend & (std::uint64_t{1} << 63))
              ? -1 - std::int64_t(~dividend) : std::int64_t(dividend);
        }
        const auto divisor = signed_value(operand, bytes);
        if (signed_dividend == std::numeric_limits<std::int64_t>::min() && divisor == -1)
          fail("signed integer quotient overflow", FaultCode::DivideError);
        const auto q = signed_dividend / divisor;
        const auto min_q = -(std::int64_t{1} << (bits - 1));
        const auto max_q = (std::int64_t{1} << (bits - 1)) - 1;
        if (q < min_q || q > max_q) fail("signed integer quotient overflow", FaultCode::DivideError);
        quotient = std::uint32_t(q); remainder = std::uint32_t(signed_dividend % divisor);
      }
      if (bytes == 1) { set_reg8(0, std::uint8_t(quotient)); set_reg8(4, std::uint8_t(remainder)); }
      else { put_reg(EAX, bytes, quotient); put_reg(EDX, bytes, remainder); }
      // All arithmetic status flags are undefined after DIV/IDIV: preserve.
      return;
    }
  }
}

inline void Cpu::extended() {
  const auto op = fetch8(); opcode_ = std::uint16_t(0x0f00U | op);
  const auto bytes = width();
  if (lock_prefix_ && op != 0xc7)
    fail("LOCK prefix is supported only on 0F C7 /1 CMPXCHG8B/16B",
         FaultCode::UnsupportedState);
  if (op >= 0x80 && op <= 0x8f) {
    const auto displacement_width = is_long_mode() ? 4 : bytes;
    const auto disp = signed_value(immediate(displacement_width), displacement_width);
    if (condition(op & 15)) relative(disp, bytes);
    return;
  }
  if (op >= 0x90 && op <= 0x9f) { const auto m = modrm(); put(m.rm, 1, condition(op & 15) ? 1 : 0); return; }
  if (op >= 0xc8 && op <= 0xcf) {
    if (bytes == 2) fail("16-bit BSWAP has no defined result", FaultCode::UnsupportedState);
    const unsigned r = (op & 7) + ((rex_ & 1) ? 8 : 0);
    const auto value = get_reg(r, bytes);
    std::uint64_t swapped = 0;
    for (unsigned i = 0; i < bytes; ++i) swapped = (swapped << 8) | ((value >> (8 * i)) & 0xff);
    put_reg(r, bytes, swapped);
    return;
  }
  if(op==0x58||op==0x59||op==0x5c||op==0x5e||
     op==0x51||op==0x5d||op==0x5f) {
    sse2_float(op);return;
  }
  if(op==0x2a||op==0x2c||op==0x2d||op==0x2e||op==0x2f) {
    sse2_scalar_misc(op);return;
  }
  if((op==0x10||op==0x11)&&repeat_==0xf2) { // MOVSD xmm/m64
    if(operand_override_)fail("conflicting MOVSD prefixes",FaultCode::InvalidInstruction);
    require_sse_state();
    const auto m=modrm();require_xmm(m.reg);
    if(op==0x10) {
      const auto bits=sse64_source(m.rm);
      if(!m.rm.is_register)xmm[m.reg].fill(0); // MOVSD memory load zeros high64
      packed_set(xmm[m.reg],0,8,bits);
    }else sse64_store(m.rm,packed_lane(xmm[m.reg],0,8));
    return;
  }
  switch (op) {
    case 0xe6: // CVTDQ2PD, CVTPD2DQ, CVTTPD2DQ
      sse2_convert_packed();return;
    case 0x54: case 0x55: case 0x56: case 0x57: { // ANDPD/ANDNPD/ORPD/XORPD
      if(!operand_override_||repeat_)
        fail("packed logical SSE2 double requires 66",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const auto source=read_xmm_operand(m.rm,true);
      for(unsigned i=0;i<16;++i){
        const auto lhs=xmm[m.reg][i],rhs=source[i];
        xmm[m.reg][i]=op==0x54?(lhs&rhs):
                       op==0x55?(std::uint8_t(~lhs)&rhs):
                       op==0x56?(lhs|rhs):std::uint8_t(lhs^rhs);
      }
      return;
    }
    case 0x50: { // MOVMSKPD r32, xmm (XMM source register only)
      if(!operand_override_||repeat_)
        fail("MOVMSKPD requires 66",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();
      if(!m.rm.is_register)fail("MOVMSKPD source must be XMM",FaultCode::InvalidInstruction);
      require_xmm(m.rm.index);
      const auto bits=((packed_lane(xmm[m.rm.index],0,8)>>63)&1U)|
                      (((packed_lane(xmm[m.rm.index],8,8)>>63)&1U)<<1);
      put_reg(m.reg,4,bits);
      return;
    }
    case 0xc2: { // CMPPD / CMPSD imm8
      sse2_cmp_vector(repeat_==0xf2);
      return;
    }
    case 0xc6: { // SHUFPD xmm, xmm/m128, imm8
      if(!operand_override_||repeat_)
        fail("SHUFPD requires 66",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const auto order=unsigned(immediate(1));
      const auto src=read_xmm_operand(m.rm,true);
      const auto dst=xmm[m.reg];
      packed_set(xmm[m.reg],0,8,
                 packed_lane(dst,(order&1U)?8:0,8));
      packed_set(xmm[m.reg],8,8,
                 packed_lane(src,(order&2U)?8:0,8));
      return;
    }
    case 0x12: case 0x13: case 0x16: case 0x17: { // MOVLPD/MOVHPD m64
      if(!operand_override_||repeat_)
        fail("MOVLPD/MOVHPD require 66",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      if(m.rm.is_register)
        fail("MOVLPD/MOVHPD require memory operand",FaultCode::InvalidInstruction);
      const unsigned at=(op==0x16||op==0x17)?8U:0U;
      if(op==0x12||op==0x16)
        packed_set(xmm[m.reg],at,8,get(m.rm,8));
      else put(m.rm,8,packed_lane(xmm[m.reg],at,8));
      return;
    }
    case 0xdb: case 0xdf: case 0xeb: // PAND/PANDN/POR
    case 0x60: case 0x61: case 0x62: case 0x6c: // PUNPCKL
    case 0x68: case 0x69: case 0x6a: case 0x6d: // PUNPCKH
    case 0x74: case 0x75: case 0x76: // PCMPEQ
    case 0x64: case 0x65: case 0x66: // PCMPGT
    case 0xfc: case 0xfd: case 0xfe: case 0xd4: // PADD
    case 0xf8: case 0xf9: case 0xfa: case 0xfb: // PSUB
    case 0xd1: case 0xd2: case 0xd3: // PSRL
    case 0xf1: case 0xf2: case 0xf3: // PSLL
      sse_integer(op); return;
    case 0x73: { // PSRLDQ /3, PSLLDQ /7
      if(!operand_override_||repeat_)
        fail("byte shift requires mandatory 66",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();
      if(!m.rm.is_register||(m.group!=3&&m.group!=7))
        fail("unsupported 0F 73 group opcode",FaultCode::UnsupportedOpcode);
      require_xmm(m.rm.index);
      const auto count=unsigned(immediate(1));
      const auto source=xmm[m.rm.index];
      auto& dest=xmm[m.rm.index];
      for(unsigned i=0;i<16;++i) {
        const int src=m.group==3?int(i)+int(count):int(i)-int(count);
        dest[i]=src>=0&&src<16?source[unsigned(src)]:0;
      }
      return;
    }
    case 0x70: { // PSHUFD xmm, xmm/m128, imm8
      if(!operand_override_||repeat_)
        fail("PSHUFD requires 66 prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      // Consume the immediate BEFORE resolving RIP-relative memory operands.
      const auto order=unsigned(immediate(1));
      const auto source=read_xmm_operand(m.rm,true);
      for(unsigned lane=0;lane<4;++lane)
        packed_set(xmm[m.reg],lane*4,4,
                   packed_lane(source,((order>>(lane*2))&3U)*4,4));
      return;
    }
    case 0xd7: { // PMOVMSKB r32, xmm
      if(!operand_override_||repeat_)
        fail("PMOVMSKB requires 66 prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();
      if(!m.rm.is_register)
        fail("PMOVMSKB source must be XMM register",FaultCode::InvalidInstruction);
      require_xmm(m.rm.index);
      std::uint32_t mask=0;
      for(unsigned i=0;i<16;++i)mask|=std::uint32_t(xmm[m.rm.index][i]>>7)<<i;
      put_reg(m.reg,4,mask);
      return;
    }
    case 0x6e: case 0x7e: { // 66 MOVD/MOVQ, F3 MOVQ xmm load
      if(!operand_override_&&!(op==0x7e&&repeat_==0xf3))
        fail("MOVD/MOVQ mandatory prefix missing",FaultCode::UnsupportedOpcode);
      if(operand_override_&&repeat_)
        fail("conflicting MOVD/MOVQ prefixes",FaultCode::InvalidInstruction);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const unsigned n=(is_long_mode()&&(rex_&8))||repeat_==0xf3?8U:4U;
      if(op==0x6e||(op==0x7e&&repeat_==0xf3)) {
        const auto value=(op==0x7e&&repeat_==0xf3&&m.rm.is_register)
          ?(require_xmm(m.rm.index),packed_lane(xmm[m.rm.index],0,8))
          :get(m.rm,n);
        xmm[m.reg].fill(0);
        packed_set(xmm[m.reg],0,n,value);
      }else{
        put(m.rm,n,packed_lane(xmm[m.reg],0,n));
      }
      return;
    }
    case 0xd6: { // 66 MOVQ xmm/m64, xmm low64
      if(!operand_override_||repeat_)
        fail("MOVQ xmm store requires 66 prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const auto low=packed_lane(xmm[m.reg],0,8);
      if(m.rm.is_register) {
        require_xmm(m.rm.index);
        xmm[m.rm.index].fill(0);
        packed_set(xmm[m.rm.index],0,8,low);
      }else put(m.rm,8,low);
      return;
    }
    case 0x10: case 0x11: case 0x28: case 0x29: { // MOVUPD/MOVAPD
      if(!operand_override_||repeat_)
        fail("MOVUPD/MOVAPD requires 66 prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const bool aligned=op==0x28||op==0x29;
      if(op==0x10||op==0x28)xmm[m.reg]=read_xmm_operand(m.rm,aligned);
      else write_xmm_operand(m.rm,xmm[m.reg],aligned);
      return;
    }
    case 0x6f: case 0x7f: { // 66 MOVDQA / F3 MOVDQU
      if(!operand_override_&&repeat_!=0xf3)
        fail("SSE2 MOVDQA/MOVDQU requires 66/F3 prefix",FaultCode::UnsupportedOpcode);
      if(operand_override_&&repeat_)
        fail("conflicting SSE2 mandatory prefixes",FaultCode::InvalidInstruction);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
#if defined(MUNDO_X_PC_ETHER_NEXT)
      if (try_ether_next(m)) return;
#endif
      const bool aligned=operand_override_;
      if(op==0x6f)xmm[m.reg]=read_xmm_operand(m.rm,aligned);
      else write_xmm_operand(m.rm,xmm[m.reg],aligned);
      return;
    }
    case 0xef: { // 66 PXOR xmm, xmm/m128
      if(!operand_override_||repeat_)
        fail("PXOR xmm requires 66 prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
#if defined(MUNDO_X_PC_ETHER_NEXT)
      if (try_ether_next(m)) return;
#endif
      const auto rhs=read_xmm_operand(m.rm,true);
      for(unsigned i=0;i<16;++i)xmm[m.reg][i]^=rhs[i];
      return;
    }
    case 0xae: { // FXSAVE/FXRSTOR m512byte and LDMXCSR/STMXCSR
      const auto m=modrm();require_memory(m.rm);
      if(m.group==0||m.group==1){fxstate(m.rm,m.group==1);return;}
      if(m.group!=2&&m.group!=3)
        fail("0F AE operation not implemented",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto address=segment_linear(m.rm.segment,operand_offset(m.rm),4,
                                       m.group==2?Access::Read:Access::Write);
      if(m.group==2){
        const auto candidate=std::uint32_t(read_linear(address,4));
        if(candidate&~kMxcsrMask)
          fail("LDMXCSR reserved bits",FaultCode::ProtectionFault);
        mxcsr=candidate;
      }else write_linear(address,4,mxcsr);
      return;
    }
    case 0x0d: { // AMD PREFETCH/PREFETCHW advisory hints.
      const auto m=modrm();
      require_memory(m.rm);
      if(m.group!=1)
        fail("only PREFETCHW /1 is modeled", FaultCode::UnsupportedOpcode);
      // PREFETCHW is a non-faulting hint. Decode the effective address but
      // do NOT read or materialize any RAM, page tables, QSSD or MMIO.
      (void)operand_offset(m.rm);
      return;
    }
    case 0xc7: { // CMPXCHG8B / CMPXCHG16B, /1 memory only.
      const auto m=modrm();
      if(m.group!=1 || m.rm.is_register)
        fail("0F C7 requires /1 memory operand", FaultCode::InvalidInstruction);
      const bool large=is_long_mode() && (rex_&8);
      const unsigned amount=large?16U:8U;
      const auto address=segment_linear(m.rm.segment,operand_offset(m.rm),
                                        amount,Access::Write);
      if(large && (address&15U))
        fail("CMPXCHG16B requires 16-byte aligned address",
             FaultCode::ProtectionFault);
      // Resolve and preflight every byte before ANY write to preserve the
      // observable all-or-nothing comparison on our single guest vCPU.
      // Claimed MMIO cannot provide a 128-bit atomic transaction; reject it.
      for(unsigned offset=0;offset<amount;++offset) {
        // CMPXCHG8B may straddle a 4-KiB page. Translate each byte
        // before accessing memory; never guess physical contiguity.
        const auto p=translate(address+offset,Access::Write);
        if(mmio_at(p))
          fail("atomic memory operation on MMIO is unsupported",
               FaultCode::UnsupportedState);
        (void)physical_index(p);
      }
      const auto low=read_linear(address,large?8:4);
      const auto high=read_linear(address+(large?8:4),large?8:4);
      const auto rax=get_reg(EAX,large?8:4);
      const auto rdx=get_reg(EDX,large?8:4);
      const bool equal=low==rax && high==rdx;
      if(equal) {
        // CPU::step() journals both writes; a failure in the second half
        // rolls back the first half. No additional CPU or host atomic claim.
        write_linear(address,large?8:4,get_reg(EBX,large?8:4));
        write_linear(address+(large?8:4),large?8:4,get_reg(ECX,large?8:4));
      }else{
        put_reg(EAX,large?8:4,low);
        put_reg(EDX,large?8:4,high);
      }
      set_flag(ZF,equal);
      return;
    }
    case 0x00: { // Group 6: SLDT, STR, LLDT, LTR, VERR, VERW
      const auto m = modrm(); // Always decode ModRM before any unsupported fault.
      if (is_real_mode())
        fail("0F 00 group-6 requires protected mode", FaultCode::InvalidInstruction);
      if (m.group >= 6)
        fail("0F 00 group-6 reserved /6 or /7", FaultCode::InvalidInstruction);
      if (m.group <= 1) {
        // SLDT/STR write a 16-bit selector to memory; for register forms,
        // the operand-size attribute controls 16/32/64-bit zero-extension.
        const auto selector = m.group == 0 ? ldtr.selector : tr.selector;
        put(m.rm, m.rm.is_register ? bytes : 2, selector);
        return;
      }

      const auto selector = std::uint16_t(get(m.rm, 2));
      if (m.group >= 4) {
        // VERR/VERW never fault merely because a selector is invalid; only
        // invalid memory addressing of the *operand* may fault.
        bool permitted = false;
        if ((selector & 0xfffcu) != 0) {
          const bool local = (selector & 4u) != 0;
          const auto table_base = local ? ldtr.base : gdtr.base;
          const auto table_limit = local ? ldtr.limit : std::uint32_t(gdtr.limit);
          const auto offset = std::uint32_t(selector & 0xfff8u);
          if ((!local || ldtr.valid) &&
              offset <= table_limit && table_limit - offset >= 7u &&
              table_base <= UINT64_MAX - offset - 7u) {
            const auto descriptor_value = read_linear(table_base + offset, 8);
            const unsigned access = unsigned(descriptor_value >> 40) & 0xffu;
            const bool segment = (access & 0x10u) != 0;
            const bool present = (access & 0x80u) != 0;
            const bool code = (access & 8u) != 0;
            const bool conforming = code && (access & 4u);
            const bool accessible = ((access >> 5) & 3u) >= std::max<unsigned>(cpl(),selector & 3u);
            const bool privilege_ok = conforming || accessible;
            const bool read_ok = !code || (access & 2u);
            const bool write_ok = !code && (access & 2u);
            permitted = segment && present && privilege_ok &&
                        (m.group == 4 ? read_ok : write_ok);
          }
        }
        set_flag(ZF, permitted);
        return;
      }

      // LLDT/LTR require CPL0. The current CPU has no ring3 execution,
      // so the guard remains explicit rather than promising task switching.
      if (segments[CS] & 3u)
        fail("LLDT/LTR privileged instruction requires CPL0", FaultCode::ProtectionFault);
      if (m.group == 2 && (selector & 0xfffcu) == 0) {
        ldtr = {};  // Architectural null LLDT invalidates the descriptor cache.
        return;
      }
      if ((selector & 0xfffcu) == 0)
        fail("LTR null task selector", FaultCode::ProtectionFault);
      if (selector & 4u)
        fail("LLDT/LTR selector must reference global descriptor table",
             FaultCode::ProtectionFault);
      if (selector & 3u)
        fail("LLDT/LTR nonzero selector RPL not modeled in ring0",
             FaultCode::UnsupportedState);

      const auto offset = std::uint32_t(selector & 0xfff8u);
      if (offset > gdtr.limit || unsigned(gdtr.limit) - offset < 7u ||
          gdtr.base > UINT64_MAX - offset - 7u)
        fail("LLDT/LTR selector exceeds GDT limit", FaultCode::ProtectionFault);
      const auto descriptor_address = gdtr.base + offset;
      const auto raw = read_linear(descriptor_address, 8);
      const auto access = unsigned(raw >> 40) & 0xffu;
      const auto type = access & 0xfu;
      if (access & 0x10u)
        fail("LLDT/LTR requires a system descriptor, not code/data",
             FaultCode::ProtectionFault);
      if (!(access & 0x80u))
        fail("LLDT/LTR descriptor is not present", FaultCode::ProtectionFault);
      if (m.group == 2 && type != 2u)
        fail("LLDT requires an LDT descriptor (type 2)",
             FaultCode::ProtectionFault);
      if (m.group == 3 && type != 1u && type != 9u)
        fail("LTR requires an available TSS descriptor (type 1 or 9)",
             FaultCode::ProtectionFault);

      std::uint64_t base = ((raw >> 16) & 0xffffffu) |
                           ((raw >> 32) & 0xff000000u);
      std::uint64_t limit = (raw & 0xffffu) | ((raw >> 32) & 0xf0000u);
      if (raw & (std::uint64_t{1} << 55)) limit = (limit << 12) | 0xfffu;
      if (m.group == 3 && limit < (type == 1u ? 0x2bu : 0x67u))
        fail("TSS limit below architectural minimum", FaultCode::ProtectionFault);
      if (efer & 0x400u) {
        // In IA-32e, TSS and LDT descriptors occupy two GDT slots.
        if (offset > gdtr.limit || unsigned(gdtr.limit) - offset < 15u ||
            gdtr.base > UINT64_MAX - offset - 15u)
          fail("IA-32e system descriptor requires 16 GDT bytes",
               FaultCode::ProtectionFault);
        const auto upper = read_linear(descriptor_address + 8, 8);
        if (upper >> 32)
          fail("reserved upper half of 64-bit system descriptor set",
               FaultCode::ProtectionFault);
        base |= (upper & 0xffffffffu) << 32;
        if (!canonical(base))
          fail("noncanonical 64-bit system descriptor base",
               FaultCode::ProtectionFault);
      }
      if (limit > UINT32_MAX)
        fail("system descriptor limit outside representable size",
             FaultCode::ProtectionFault);
      const SystemRegister next{selector, base, std::uint32_t(limit), true};
      if (m.group == 2) {
        ldtr = next;
      } else {
        // LTR marks an AVAILABLE TSS busy without actually switching tasks.
        // GDT writes follow normal guest paging and transactional rollback.
        write_linear(descriptor_address + 5, 1, access | 2u);
        tr = next;
      }
      return;
    }
    case 0x01: {
      const auto m = modrm();
      if (m.group == 4) { put(m.rm, m.rm.is_register ? bytes : 2, cr0); return; }
      if (m.group == 6) {
        if (cpl() != 0) fail("LMSW requires CPL0", FaultCode::ProtectionFault);
        const auto value = get(m.rm, 2) & 15;
        write_cr0((cr0 & ~15U) | value | (cr0 & 1)); return;
      }
      if (m.group > 3) fail("system group-7 instruction not implemented", FaultCode::UnsupportedOpcode);
      if (cpl() != 0 && m.group >= 2)
        fail("LGDT/LIDT require CPL0", FaultCode::ProtectionFault);
      require_memory(m.rm);
      const unsigned base_bytes = is_long_mode() ? 8 : 4;
      const auto address = segment_linear(m.rm.segment, operand_offset(m.rm), base_bytes + 2,
                                           m.group < 2 ? Access::Write : Access::Read);
      auto& table = (m.group & 1U) ? idtr : gdtr;
      if (m.group < 2) {
        write_linear(address, 2, table.limit);
        write_linear(address + 2, base_bytes,
                     table.base & (is_long_mode() ? UINT64_MAX : op32_ ? 0xffffffffULL : 0x00ffffffULL));
      }
      else {
        const auto limit = std::uint16_t(read_linear(address, 2));
        const auto base = read_linear(address + 2, base_bytes) &
            (is_long_mode() ? UINT64_MAX : op32_ ? 0xffffffffULL : 0x00ffffffULL);
        if (is_long_mode() && !canonical(base)) fail("non-canonical descriptor-table base", FaultCode::ProtectionFault);
        table = {limit, base};
      }
      return;
    }
    case 0x06:
      if (cpl() != 0) fail("CLTS requires CPL0", FaultCode::ProtectionFault);
      cr0 &= ~8U; return;  // no floating-point execution is claimed.
    case 0x20: case 0x22: {
      if (cpl() != 0) fail("MOV CR requires CPL0", FaultCode::ProtectionFault);
      const auto m = modrm();
      if (!m.rm.is_register) fail("MOV CR requires a register operand", FaultCode::InvalidInstruction);
      if (m.reg != 0 && m.reg != 2 && m.reg != 3 && m.reg != 4)
        fail("control register is not implemented", FaultCode::UnsupportedState);
      const unsigned n = is_long_mode() ? 8 : 4;
      if (op == 0x20) put_reg(m.rm.index, n, m.reg == 0 ? cr0 : m.reg == 2 ? cr2 : m.reg == 3 ? cr3 : cr4);
      else {
        const auto value = get_reg(m.rm.index, n);
        if (m.reg == 0) write_cr0(value);
        else if (m.reg == 2) cr2 = value;
        else if (m.reg == 3) write_cr3(value);
        else write_cr4(value);
      }
      return;
    }
    case 0x30:
      if (cpl() != 0) fail("WRMSR requires CPL0", FaultCode::ProtectionFault);
      write_msr(std::uint32_t(regs[ECX]), (std::uint64_t(std::uint32_t(regs[EDX])) << 32) | std::uint32_t(regs[EAX])); return;
    case 0x32: {
      if (cpl() != 0) fail("RDMSR requires CPL0", FaultCode::ProtectionFault);
      const auto value = read_msr(std::uint32_t(regs[ECX]));
      put_reg(EAX, 4, value); put_reg(EDX, 4, value >> 32); return;
    }
    case 0x31: {  // RDTSC: deterministic synthetic guest cycle counter.
      // This counter reflects decoded work, not host CPU cycles or a clock.
      const auto cycles = std::uint64_t(instructions) * 64u;
      put_reg(EAX, 4, std::uint32_t(cycles));
      put_reg(EDX, 4, std::uint32_t(cycles >> 32));
      return;
    }
    case 0xa2: {  // CPUID: a truthful, deliberately conservative emulated CPU.
      const auto leaf = std::uint32_t(regs[EAX]);
      const auto subleaf = std::uint32_t(regs[ECX]);
      std::uint32_t eax=0,ebx=0,ecx=0,edx=0;
      if(leaf==0) {
        eax=7;
        // "MUNDOX PC   " is a 12-character emulated CPU vendor identifier.
        const char vendor[13]="MUNDOX PC   ";
        auto word=[&](unsigned start){
          return std::uint32_t(std::uint8_t(vendor[start]))
              |(std::uint32_t(std::uint8_t(vendor[start+1]))<<8)
              |(std::uint32_t(std::uint8_t(vendor[start+2]))<<16)
              |(std::uint32_t(std::uint8_t(vendor[start+3]))<<24);
        };
        ebx=word(0);edx=word(4);ecx=word(8);
      }else if(leaf==1){
        eax=0x00000663U; // modeled family/model/stepping
        // TSC, MSR, PAE, and tested atomic CMPXCHG16B only.
        // Never claim SSE2/x87/APIC until guest instructions really work.
        edx=(1U<<4)|(1U<<5)|(1U<<6)|(1U<<8); // CMPXCHG8B
        ecx=(1U<<13); // CMPXCHG16B guest primitive

      }else if(leaf==7 && subleaf==0){
        // Extended feature bits remain zero: no SMEP/AVX2/etc.
      }else if(leaf==0x80000000U){
        eax=0x80000008U;
      }else if(leaf==0x80000001U){
        edx=(1U<<20)|(1U<<29); // NX and modeled IA-32e long mode
        ecx=(1U<<0)|(1U<<8); // tested LAHF/SAHF and non-faulting PREFETCHW
      }else if(leaf==0x80000008U){
        eax=32U|(48U<<8); // physical-address and virtual-address bit sizes
      }
      put_reg(EAX, 4, eax);put_reg(EBX, 4, ebx);
      put_reg(ECX, 4, ecx);put_reg(EDX, 4, edx);
      return;
    }
    case 0xa0: push(segments[FS], stack_operand_width()); return;
    case 0xa1: load_segment(FS, std::uint16_t(pop(stack_operand_width()))); return;
    case 0xa4: case 0xa5: case 0xac: case 0xad: {
      const auto m = modrm();
      const unsigned count = (op & 1U) ? reg8(ECX) : fetch8();
      double_shift(m, bytes, count, (op & 8U) == 0); return;
    }
    case 0xa8: push(segments[GS], stack_operand_width()); return;
    case 0xa9: load_segment(GS, std::uint16_t(pop(stack_operand_width()))); return;
    case 0xaf: {
      const auto m = modrm(); multiply_signed(m.reg, get_reg(m.reg, bytes), get(m.rm, bytes), bytes); return;
    }
    case 0xb2: case 0xb4: case 0xb5: {
      const auto m = modrm(); require_memory(m.rm);
      const auto address = segment_linear(m.rm.segment, operand_offset(m.rm), bytes + 2, Access::Read);
      const auto value = read_linear(address, bytes);
      const auto segment = std::uint16_t(read_linear(address + bytes, 2));
      load_segment(op == 0xb2 ? SS : op == 0xb4 ? FS : GS, segment);
      put_reg(m.reg, bytes, value);
      return;
    }
    case 0xb6: case 0xb7: case 0xbe: case 0xbf: {
      const auto m = modrm(); const unsigned source_width = (op & 1U) ? 2 : 1;
      const auto value = get(m.rm, source_width);
      put_reg(m.reg, bytes, (op & 8U) ? std::uint64_t(signed_value(value, source_width)) : value); return;
    }
    case 0x0b: fail("UD2 invalid opcode", FaultCode::InvalidInstruction);
    default: fail("two-byte opcode is not implemented", FaultCode::UnsupportedOpcode);
  }
}

inline void Cpu::execute(std::uint8_t op) {
  if(lock_prefix_ && op!=0x0f)
    fail("LOCK prefix on unsupported instruction", FaultCode::UnsupportedState);
  const unsigned bytes = width();
  const unsigned stack_bytes = stack_operand_width();
  const unsigned encoded_register = (op & 7U) + ((rex_ & 1) ? 8 : 0);
  if (is_long_mode()) {
    switch (op) {
      case 0x06: case 0x07: case 0x0e: case 0x16: case 0x17: case 0x1e: case 0x1f:
      case 0x60: case 0x61: case 0x9a: case 0xc4: case 0xc5: case 0xce: case 0xea:
        fail("instruction encoding is invalid in 64-bit mode", FaultCode::InvalidInstruction);
      case 0xca: case 0xcb:
        fail("64-bit far return is not implemented", FaultCode::UnsupportedState);
      default: break;
    }
  }
  if (op <= 0x3d && (op & 7U) <= 5) {
    const unsigned operation = op >> 3, form = op & 7U;
    const unsigned w = (op & 1U) ? bytes : 1;
    if (form >= 4) {
      const auto result = alu(operation, get_reg(EAX, w), arithmetic_immediate(w), w);
      if (operation != 7) put_reg(EAX, w, result);
    } else {
      const auto m = modrm();
      const auto a = (form & 2U) ? get_reg(m.reg, w) : get(m.rm, w);
      const auto b = (form & 2U) ? get(m.rm, w) : get_reg(m.reg, w);
      const auto result = alu(operation, a, b, w);
      if (operation != 7) {
        if (form & 2U) put_reg(m.reg, w, result); else put(m.rm, w, result);
      }
    }
    return;
  }
  if (op >= 0x40 && op <= 0x4f) {
    const bool old_cf = flag(CF);
    put_reg(op & 7, bytes, alu((op & 8) ? 5 : 0, get_reg(op & 7, bytes), 1, bytes));
    set_flag(CF, old_cf); return;
  }
  if (op >= 0x50 && op <= 0x57) { push(get_reg(encoded_register, stack_bytes), stack_bytes); return; }
  if (op >= 0x58 && op <= 0x5f) { const auto value = pop(stack_bytes); put_reg(encoded_register, stack_bytes, value); return; }
  if (op >= 0x70 && op <= 0x7f) {
    const auto disp = signed_value(immediate(1), 1);
    if (condition(op & 15)) relative(disp, bytes);
    return;
  }
  if (op >= 0x90 && op <= 0x97) {
    if (encoded_register == 0) return; // NOP/PAUSE preserves the entire RAX.
    const auto value = get_reg(EAX, bytes);
    put_reg(EAX, bytes, get_reg(encoded_register, bytes)); put_reg(encoded_register, bytes, value); return;
  }
  if (op >= 0xb0 && op <= 0xb7) { put_reg(encoded_register, 1, immediate(1)); return; }
  if (op >= 0xb8 && op <= 0xbf) { put_reg(encoded_register, bytes, immediate(bytes)); return; }
  if ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf) || (op >= 0x6c && op <= 0x6f)) {
    string_operation(op); return;
  }
  switch (op) {
    case 0x06: push(segments[ES], bytes); return;
    case 0x07: load_segment(ES, std::uint16_t(pop(bytes))); return;
    case 0x0e: push(segments[CS], bytes); return;
    case 0x0f: extended(); return;
    case 0x16: push(segments[SS], bytes); return;
    case 0x17: load_segment(SS, std::uint16_t(pop(bytes))); return;
    case 0x1e: push(segments[DS], bytes); return;
    case 0x1f: load_segment(DS, std::uint16_t(pop(bytes))); return;
    case 0x60: {
      const auto old_sp = get_reg(ESP, bytes);
      for (unsigned r = 0; r < 8; ++r) push(r == ESP ? old_sp : get_reg(r, bytes), bytes);
      return;
    }
    case 0x61:
      for (int r = 7; r >= 0; --r) { const auto value = pop(bytes); if (r != ESP) put_reg(unsigned(r), bytes, value); }
      return;
    case 0x63: {
      if (!is_long_mode() || bytes != 8)
        fail("only REX.W MOVSXD form is implemented (ARPL is not implemented)", FaultCode::UnsupportedOpcode);
      const auto m = modrm(); put_reg(m.reg, 8, std::uint64_t(signed_value(get(m.rm, 4), 4))); return;
    }
    case 0x68: push(arithmetic_immediate(stack_bytes), stack_bytes); return;
    case 0x6a: push(std::uint64_t(signed_value(immediate(1), 1)), stack_bytes); return;
    case 0x69: case 0x6b: {
      const auto m = modrm();
      const auto value = op == 0x6b ? std::uint64_t(signed_value(immediate(1), 1)) : arithmetic_immediate(bytes);
      multiply_signed(m.reg, get(m.rm, bytes), value, bytes); return;
    }
    case 0x80: case 0x81: case 0x83: {
      const auto m = modrm(); const unsigned w = op == 0x80 ? 1 : bytes;
      const auto value = op == 0x83 ? std::uint64_t(signed_value(immediate(1), 1)) : arithmetic_immediate(w);
      const auto result = alu(m.group, get(m.rm, w), value, w);
      if (m.group != 7) put(m.rm, w, result);
      return;
    }
    case 0x84: case 0x85: {
      const auto m = modrm(); const unsigned w = op == 0x84 ? 1 : bytes;
      (void)alu(4, get(m.rm, w), get_reg(m.reg, w), w); return;
    }
    case 0x86: case 0x87: {
      const auto m = modrm(); const unsigned w = op == 0x86 ? 1 : bytes;
      const auto old = get(m.rm, w); put(m.rm, w, get_reg(m.reg, w)); put_reg(m.reg, w, old); return;
    }
    case 0x88: case 0x89: case 0x8a: case 0x8b: {
      const auto m = modrm(); const unsigned w = (op & 1U) ? bytes : 1;
      if (op & 2U) put_reg(m.reg, w, get(m.rm, w)); else put(m.rm, w, get_reg(m.reg, w));
      return;
    }
    case 0x8c: case 0x8e: {
      const auto m = modrm();
      if (m.group > GS || (op == 0x8e && m.group == CS))
        fail("invalid segment register encoding", FaultCode::InvalidInstruction);
      if (op == 0x8c) put(m.rm, m.rm.is_register ? bytes : 2, segments[m.group]);
      else load_segment(m.group, std::uint16_t(get(m.rm, 2)));
      return;
    }
    case 0x8d: { const auto m = modrm(); require_memory(m.rm); put_reg(m.reg, bytes, operand_offset(m.rm)); return; }
    case 0x8f: {
      auto m = modrm();
      if (m.group != 0) fail("reserved POP encoding", FaultCode::InvalidInstruction);
      const auto old_esp = regs[ESP]; const auto value = pop(stack_bytes);
      if (!m.rm.is_register && m.rm.uses_esp) m.rm.offset += regs[ESP] - old_esp;
      put(m.rm, stack_bytes, value); return;
    }
    case 0x90: return;  // NOP / F3 90 PAUSE (no timing guarantee).
    case 0x98:
      put_reg(EAX, bytes, std::uint64_t(signed_value(get_reg(EAX, bytes / 2), bytes / 2))); return;
    case 0x99: put_reg(EDX, bytes, (get_reg(EAX, bytes) & sign_bit(bytes)) ? mask(bytes) : 0); return;
    case 0x9b: { // FWAIT: x87 deferred unmasked exceptions.
      if((cr0&0x8U)&&(cr0&0x2U))
        fail("FWAIT with TS and MP",FaultCode::DeviceNotAvailable);
      if(fpu_status&0x0080U) {
        if(cr0&0x20U)fail("pending unmasked x87 exception (#MF)",FaultCode::X87FloatingPoint);
        fail("legacy x87 FERR#/IRQ13 delivery not modeled",FaultCode::UnsupportedState);
      }
      return;
    }
    case 0x9a: {
      const auto target = immediate(bytes); const auto cs = std::uint16_t(immediate(2));
      push(segments[CS], bytes); push(cursor_ & mask(bytes), bytes); far_transfer(cs, target, bytes); return;
    }
    case 0x9c: push(eflags & ~0x30000U, stack_bytes); return;
    case 0x9d: { const auto value = pop(stack_bytes); restore_flags(std::uint32_t(value), stack_bytes); return; }
    case 0x9e:
      eflags = (eflags & ~0xd5U) | (reg8(4) & 0xd5U) | 2; return;
    case 0x9f: set_reg8(4, std::uint8_t((eflags & 0xd5U) | 2)); return;
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
      const auto address = immediate(address_width()); const unsigned w = (op & 1U) ? bytes : 1;
      if (op & 2U) write_segment(data_segment(), address, w, get_reg(EAX, w));
      else put_reg(EAX, w, read_segment(data_segment(), address, w));
      return;
    }
    case 0xa8: case 0xa9: {
      const unsigned w = (op & 1U) ? bytes : 1; (void)alu(4, get_reg(EAX, w), arithmetic_immediate(w), w); return;
    }
    case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
      const auto m = modrm(); const unsigned w = (op & 1U) ? bytes : 1;
      const unsigned count = op < 0xd0 ? immediate(1) : op < 0xd2 ? 1 : reg8(ECX);
      shift(m, w, count); return;
    }
    case 0xc2: case 0xc3: {
      const auto adjustment = op == 0xc2 ? immediate(2) : 0;
      const unsigned n = is_long_mode() ? 8 : bytes;
      if (is_long_mode() && operand_override_)
        fail("operand-size override on a 64-bit near return is not implemented", FaultCode::UnsupportedState);
      const auto target = pop(n); jump(target, n);
      put_reg(ESP, stack_address_width(), get_reg(ESP, stack_address_width()) + adjustment); return;
    }
    case 0xc4: case 0xc5: {
      const auto m = modrm(); require_memory(m.rm);
      const auto address = segment_linear(m.rm.segment, operand_offset(m.rm), bytes + 2, Access::Read);
      const auto value = read_linear(address, bytes); const auto segment = std::uint16_t(read_linear(address + bytes, 2));
      load_segment(op == 0xc4 ? ES : DS, segment); put_reg(m.reg, bytes, value); return;
    }
    case 0xc6: case 0xc7: {
      const auto m = modrm();
      if (m.group != 0) fail("reserved MOV immediate encoding", FaultCode::InvalidInstruction);
      const unsigned w = (op & 1U) ? bytes : 1; put(m.rm, w, arithmetic_immediate(w)); return;
    }
    case 0xc8: {
      const auto allocation = std::uint16_t(immediate(2));
      const auto nesting = unsigned(immediate(1)) & 31U;
      enter_frame(allocation, nesting, stack_bytes); return;
    }
    case 0xc9:
      put_reg(ESP, stack_address_width(), get_reg(EBP, stack_address_width()));
      put_reg(EBP, stack_bytes, pop(stack_bytes)); return;
    case 0xca: case 0xcb: {
      const auto adjustment = op == 0xca ? immediate(2) : 0;
      const auto target = pop(bytes); const auto cs = std::uint16_t(pop(bytes));
      far_transfer(cs, target, bytes);
      put_reg(ESP, stack_address_width(), get_reg(ESP, stack_address_width()) + adjustment); return;
    }
    case 0xcc: interrupt(3); return;
    case 0xcd: { const auto vector = fetch8(); interrupt(vector); return; }
    case 0xce: if (flag(OF)) interrupt(4); return;
    case 0xcf: {
      if (is_real_mode()) {
        const auto target = pop(bytes); const auto cs = std::uint16_t(pop(bytes)); const auto flags = pop(bytes);
        if (flags & 0x20000U) fail("IRET virtual-8086 transition is not implemented", FaultCode::UnsupportedState);
        far_transfer(cs, target, bytes); restore_flags(std::uint32_t(flags), bytes); return;
      }
      const bool long_return = is_long_mode();
      if (!long_return && (!cache_[CS].big || (efer & 0x400)))
        fail("protected16/compatibility IRET not implemented", FaultCode::UnsupportedState);
      if (long_return && operand_override_)
        fail("16-bit IRET in long mode is not implemented", FaultCode::UnsupportedState);
      const unsigned n = long_return ? 8U : 4U;
      const auto original_cpl = cpl();
      const auto target = pop(n), raw_cs = pop(n), flags = pop(n);
      if (raw_cs > 0xffffU || (flags & 0x20000U))
        fail("IRET invalid selector/virtual-8086 transition", FaultCode::ProtectionFault);
      const auto target_cpl = unsigned(raw_cs & 3U);
      if (target_cpl < original_cpl || (target_cpl != 0 && target_cpl != 3))
        fail("IRET cannot elevate privilege", FaultCode::ProtectionFault);
      const auto code = descriptor(std::uint16_t(raw_cs));
      if (!code.code || (code.conforming ? code.dpl > target_cpl : code.dpl != target_cpl) ||
          (long_return ? !code.long_code : !code.big))
        fail("IRET code selector DPL/RPL/type mismatch", FaultCode::ProtectionFault);
      const bool outer = target_cpl > original_cpl;
      std::uint64_t saved_rsp = 0, saved_ss = 0;
      SegmentCache saved_stack{};
      if (long_return || outer) {
        saved_rsp = pop(n);
        saved_ss = pop(n);
        if (saved_ss > 0xffffU || (long_return && !canonical(saved_rsp)))
          fail("IRET invalid SS/RSP", FaultCode::ProtectionFault);
        if (saved_ss == 0 && long_return && target_cpl == 0) {
          saved_stack = SegmentCache{}; // null SS is valid for IA-32e ring0
        } else {
          if ((saved_ss & 3U) != target_cpl)
            fail("IRET SS selector RPL differs from return CPL", FaultCode::ProtectionFault);
          saved_stack = descriptor(std::uint16_t(saved_ss));
          if (saved_stack.code || !saved_stack.writable || saved_stack.dpl != target_cpl)
            fail("IRET stack selector DPL/type mismatch", FaultCode::ProtectionFault);
        }
      }
      // The IRET instruction is still executing at its originating CPL.
      // Restore privileged IF/IOPL before switching the CS CPL to ring3.
      restore_flags(std::uint32_t(flags), 4);
      segments[CS] = std::uint16_t(raw_cs);
      cache_[CS] = code;
      if (long_return || outer) {
        segments[SS] = std::uint16_t(saved_ss);
        cache_[SS] = saved_stack;
        put_reg(ESP, long_return ? 8U : 4U, saved_rsp);
      }
      if (outer) {
        // IRET to a less-privileged ring invalidates inaccessible DS/ES/FS/GS.
        // FS/GS MSR bases persist across selector invalidation.
        for (unsigned seg : {unsigned(DS),unsigned(ES),unsigned(FS),unsigned(GS)}) {
          const auto& descriptor_cache = cache_[seg];
          if (!descriptor_cache.valid) continue;
          const auto rpl = unsigned(segments[seg] & 3U);
          if (!descriptor_cache.conforming &&
              std::max(target_cpl,rpl) > descriptor_cache.dpl) {
            segments[seg] = 0;
            cache_[seg].valid = false;
          }
        }
      }
      jump(target, n);
      return;
    }
    case 0xd8: case 0xd9: case 0xdb: case 0xdc:
    case 0xdd: case 0xde: case 0xdf: { // Foundational x87 FPU opcodes
      require_fpu();
      const auto encoding=fetch8();
      const auto group=(encoding>>3)&7U;
      if(encoding>=0xc0) {
        const auto reg=unsigned(encoding&7U);
        if(op==0xdb && encoding==0xe3){ // FNINIT
          fpu_init();return;
        }
        if(op==0xdb && encoding==0xe2){ // FNCLEX
          fpu_status=std::uint16_t(fpu_status&~0x80ffU);return;
        }
        if(op==0xd9 && encoding==0xd0)return; // FNOP
        if(op==0xd9 && (encoding==0xe8||encoding==0xee)) {
          // FLD1 / FLDZ (exact 80-bit architectural constants).
          Extended80 value{};
          if(encoding==0xe8) {
            value[7]=0x80;value[8]=0xff;value[9]=0x3f;
          }
          fpu_push(value);fpu_ip=start_ip_;fpu_cs=start_cs_;return;
        }
        if(op==0xd9 && (encoding&0xf8U)==0xc0) {
          fpu_push(fpu_peek(reg));fpu_ip=start_ip_;return; // FLD ST(i)
        }
        if(op==0xd9 && (encoding&0xf8U)==0xc8) { // FXCH ST(i)
          const auto first=fpu_slot(0),other=fpu_slot(reg);
          if(fpu_tag(first)==3U||fpu_tag(other)==3U)
            fail("FXCH empty register handling pending",FaultCode::UnsupportedState);
          std::swap(x87[first],x87[other]);
          const auto first_tag=fpu_tag(first),other_tag=fpu_tag(other);
          fpu_tag_set(first,other_tag);fpu_tag_set(other,first_tag);
          return;
        }
        if(op==0xdd && (encoding&0xf8U)==0xd8) { // FSTP ST(i)
          if(reg==0){fpu_pop();return;} // handles empty TOP stack fault
          const auto value=fpu_peek();
          const auto slot=fpu_slot(reg);
          x87[slot]=value;fpu_tag_set(slot,classify_ext(value));fpu_pop();
          return;
        }
        if(op==0xdf && encoding==0xe0) { // FNSTSW AX
          set_reg16(EAX,fpu_status);return;
        }
        if(op==0xd8 && ((encoding&0xf8U)==0xc0 ||
                       (encoding&0xf8U)==0xc8 ||
                       (encoding&0xf8U)==0xe0 ||
                       (encoding&0xf8U)==0xf0)) { // FADD/FMUL/FSUB/FDIV ST0,STi
          if(fpu_status&0x0080U)
            fail("x87 pending exception must be serviced before arithmetic",
                 (cr0&0x20U)?FaultCode::X87FloatingPoint:FaultCode::UnsupportedState);
          if(((fpu_control>>8)&3U)==1U)
            fail("reserved x87 precision mode PC=01",FaultCode::UnsupportedState);
          // x87 PC=24/53/64 and RC=nearest/down/up/zero. The 80-bit
          // significand is narrowed below, independent of GPU or QSSD.
          // Arithmetic still relies on host long double until a complete
          // software extended80 oracle is integrated on ARM64.
          const auto lhs=extended_to_long_double(fpu_peek());
          const auto rhs=extended_to_long_double(fpu_peek(reg));
          long double answer=0;
          std::uint32_t flags=0;
          {
            HostRound rounding(0x1f80U|(((fpu_control>>10)&3U)<<13));
            volatile long double a=lhs,b=rhs;
            if((encoding&0xf8U)==0xc0) {
              volatile long double result=a+b;answer=result;
            }else if((encoding&0xf8U)==0xc8) {
              volatile long double result=a*b;answer=result;
            }else if((encoding&0xf8U)==0xe0) {
              volatile long double result=a-b;answer=result;
            }else{
              volatile long double result=a/b;answer=result;
            }
            flags|=rounding.status();
          }
          auto converted=extended_from_long_double(answer);
          round_x87_precision(converted,flags);
          // Intel keeps ST0 unchanged when a detected exception is unmasked.
          // #MF is deferred to FWAIT; the sticky flags remain observable.
          fpu_status|=std::uint16_t(flags);
          if(flags&~std::uint32_t(fpu_control)&0x3fU){
            fpu_status|=0x8080U; // ES+B; FNSTSW exposes sticky condition.
            return;
          }
          const auto slot=fpu_slot(0);
          x87[slot]=converted;fpu_tag_set(slot,classify_ext(converted));
          fpu_ip=start_ip_;return;
        }
        fail("x87 register opcode pending for full FPU",FaultCode::UnsupportedOpcode);
      }
      // Decoding a legacy memory ModRM after consuming its byte: rewind so
      // the canonical ModRM parser handles displacement/SIB and segment.
      --cursor_;decoded_.pop_back();
      const auto m=modrm();
      require_memory(m.rm);
      const auto data_width=(op==0xd9||op==0xd8)?4U:
                            (op==0xdd||op==0xdc)?8U:2U;
      if((op==0xd9||op==0xdd)&&group==0) { // FLD m32real/m64real
        const auto raw=get(m.rm,data_width);
        fpu_push(extended_from_ieee(raw,data_width));
        fpu_ip=start_ip_;fpu_cs=start_cs_;
        fpu_dp=operand_offset(m.rm);fpu_ds=segments[m.rm.segment];
        return;
      }
      if((op==0xd9||op==0xdd)&&group==3) { // FSTP m32real/m64real
        if((fpu_control&0x0c00U)!=0)
          fail("non-nearest x87 rounding pending",FaultCode::UnsupportedState);
        const auto number=extended_to_long_double(fpu_peek());
        if(data_width==4){
          const auto converted=static_cast<float>(number);
          std::uint32_t bits=0;std::memcpy(&bits,&converted,sizeof(bits));
          put(m.rm,4,bits);
        }else{
          const auto converted=static_cast<double>(number);
          std::uint64_t bits=0;std::memcpy(&bits,&converted,sizeof(bits));
          put(m.rm,8,bits);
        }
        fpu_pop();
        fpu_ip=start_ip_;fpu_cs=start_cs_;
        fpu_dp=operand_offset(m.rm);fpu_ds=segments[m.rm.segment];
        return;
      }
      if(op==0xd9&&group==5) { // FLDCW m16
        fpu_control=std::uint16_t(get(m.rm,2));
        return;
      }
      if(op==0xd9&&group==7) { // FNSTCW m16
        put(m.rm,2,fpu_control);return;
      }
      if(op==0xdd&&group==7) { // FNSTSW m16
        put(m.rm,2,fpu_status);return;
      }
      fail("x87 memory opcode pending for full FPU",FaultCode::UnsupportedOpcode);
    }
    case 0xd7: {
      const auto address = (index(EBX) + reg8(EAX)) & mask(address_width());
      set_reg8(EAX, std::uint8_t(read_segment(data_segment(), address, 1))); return;
    }
    case 0xe0: case 0xe1: case 0xe2: case 0xe3: {
      const auto disp = signed_value(immediate(1), 1);
      bool take;
      if (op == 0xe3) take = index(ECX) == 0;
      else {
        put_index(ECX, index(ECX) - 1);
        take = index(ECX) != 0 && (op == 0xe2 || (op == 0xe1 ? flag(ZF) : !flag(ZF)));
      }
      if (take) relative(disp, bytes);
      return;
    }
    case 0xe4: case 0xe5: case 0xec: case 0xed: {
      const auto port = std::uint16_t(op < 0xec ? immediate(1) : reg16(EDX));
      const unsigned w = (op & 1U) ? (op32_ ? 4 : 2) : 1; put_reg(EAX, w, port_in(port, w)); return;
    }
    case 0xe6: case 0xe7: case 0xee: case 0xef: {
      const auto port = std::uint16_t(op < 0xee ? immediate(1) : reg16(EDX));
      const unsigned w = (op & 1U) ? (op32_ ? 4 : 2) : 1; port_out(port, w, get_reg(EAX, w)); return;
    }
    case 0xe8: {
      if (is_long_mode() && operand_override_)
        fail("operand-size override on a 64-bit near call is not implemented", FaultCode::UnsupportedState);
      const unsigned n = is_long_mode() ? 4 : bytes, target_bytes = is_long_mode() ? 8 : bytes;
      const auto disp = signed_value(immediate(n), n);
      const auto return_ip = cursor_ & mask(target_bytes); relative(disp, target_bytes); push(return_ip, target_bytes); return;
    }
    case 0xe9: { const unsigned n = is_long_mode() ? 4 : bytes; const auto disp = signed_value(immediate(n), n); relative(disp, bytes); return; }
    case 0xea: {
      const auto target = immediate(bytes); const auto cs = std::uint16_t(immediate(2));
      far_transfer(cs, target, bytes); return;
    }
    case 0xeb: { const auto disp = signed_value(immediate(1), 1); relative(disp, bytes); return; }
    case 0xf4:
      if (cpl() != 0) fail("HLT requires CPL0", FaultCode::ProtectionFault);
      halted = true; return;
    case 0xf5: set_flag(CF, !flag(CF)); return;
    case 0xf6: case 0xf7: { const auto m = modrm(); group3(m, (op & 1U) ? bytes : 1); return; }
    case 0xf8: set_flag(CF, false); return;
    case 0xf9: set_flag(CF, true); return;
    case 0xfa:
      if (cpl() > ((eflags >> 12) & 3U)) fail("CLI requires IOPL", FaultCode::ProtectionFault);
      set_flag(IF, false); return;
    case 0xfb:
      if (cpl() > ((eflags >> 12) & 3U)) fail("STI requires IOPL", FaultCode::ProtectionFault);
      set_flag(IF, true); return;
    case 0xfc: set_flag(DF, false); return;
    case 0xfd: set_flag(DF, true); return;
    case 0xfe: case 0xff: {
      const auto m = modrm(); const unsigned w = op == 0xfe ? 1 : bytes;
      if (m.group < 2) {
        const bool old_cf = flag(CF); const auto value = alu(m.group == 0 ? 0 : 5, get(m.rm, w), 1, w);
        put(m.rm, w, value); set_flag(CF, old_cf); return;
      }
      if (op == 0xfe || m.group == 7) fail("reserved group-4/group-5 encoding", FaultCode::InvalidInstruction);
      if (m.group == 6) { const auto value = get(m.rm, stack_bytes); push(value, stack_bytes); return; }
      if (m.group == 2 || m.group == 4) {
        if (is_long_mode() && operand_override_)
          fail("operand-size override on a 64-bit indirect branch is not implemented", FaultCode::UnsupportedState);
        const unsigned n = is_long_mode() ? 8 : bytes;
        const auto target = get(m.rm, n); jump(target, n);
        if (m.group == 2) push(cursor_ & mask(n), n);
        return;
      }
      if (is_long_mode()) fail("64-bit indirect far branch is not implemented", FaultCode::UnsupportedState);
      require_memory(m.rm);
      const auto address = segment_linear(m.rm.segment, operand_offset(m.rm), bytes + 2, Access::Read);
      const auto target = read_linear(address, bytes); const auto cs = std::uint16_t(read_linear(address + bytes, 2));
      if (m.group == 3) { push(segments[CS], bytes); push(cursor_ & mask(bytes), bytes); }
      far_transfer(cs, target, bytes); return;
    }
    default: fail("opcode is not implemented", FaultCode::UnsupportedOpcode);
  }
}

}  // namespace cosmic_pc
