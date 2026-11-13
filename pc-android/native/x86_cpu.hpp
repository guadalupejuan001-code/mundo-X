#pragma once

// COSMIC PC: an original, bounded x86 interpreter.
// Instruction encodings and architectural semantics follow the Intel SDM.
// Unsupported instructions and states stop with an explicit fault. The CPU
// models a subset of x86 real, protected and long modes, including CPL0/CPL3
// privilege transitions and guest x87, MMX and XMM state.
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
#include "x86_ext80.hpp"
#include "x86_wide_integer.hpp"
#include "../../native/include/mundo_x/experimental/ether_memory_block.hpp"
#include "../../native/include/mundo_x/experimental/ether_shift_loop.hpp"
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

enum class StepResult { Executed, Halted, Faulted, WaitingForX87 };

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
    bool tss16 = false;
  };
  struct SegmentCache {
    std::uint64_t base = 0;
    std::uint32_t limit = 0xffff;
    std::uint16_t selector = 0;
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
  // False certifies that no byte in this already A20-normalized span is MMIO.
  std::function<bool(std::uint64_t, std::size_t)> mmio_range_claim_hook;
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
    tsc_offset_ = 0;
    cache_.fill(SegmentCache{}); cache_[CS].code = true;
    cache_[CS].selector = cs; cache_[CS].base = std::uint64_t(cs) << 4;
    gdtr = {0, 0}; idtr = {0x3ff, 0};
    ldtr = {}; tr = {};
    x87_ignne_ = false;
    interrupt_shadow_ = false;
    last_rep_batch_bytes_ = 0;
    a20_enabled = false; halted = false; fault.reset(); instructions = 0; page_walks = 0;
    transaction_ = false; journal_.clear(); decoded_.clear();
  }
  // This CPU asserts FERR# as soon as the unmasked condition is pending.
  // An asserted IGNNE# allows a waiting instruction to execute while the
  // existing error remains visible to the external circuit.
  bool x87_ferr_asserted() const noexcept {
    return !(cr0 & 0x20U) && (fpu_status & 0x0080U);
  }
  void set_x87_ignne(bool active) noexcept { x87_ignne_ = active; }
  const std::vector<std::uint8_t>& last_decoded_bytes() const noexcept { return decoded_; }
  unsigned last_rep_batch_bytes() const noexcept { return last_rep_batch_bytes_; }
  bool rep_batch_candidate() const noexcept {
    return repeat_ && start_ip_ == eip && start_cs_ == segments[CS] &&
           (opcode_ == 0xa4 || opcode_ == 0xa5 || opcode_ == 0xaa || opcode_ == 0xab) &&
           decoded_.size() >= 2;
  }
  // The machine must bound this budget by the next device observation. Zero
  // means call step() for the original fault/IO behavior and diagnostics.
  unsigned try_rep_batch(unsigned budget);
  bool shift_loop_batch_candidate() const noexcept {
    return opcode_ == 0xe2 && ip_written_ && start_cs_ == segments[CS] &&
           decoded_.size() == 2 && decoded_[0] == 0xe2 && decoded_[1] == 0xfa &&
           eip <= 0xfffa && start_ip_ == eip + 4 && cursor_ == eip + 6 && code_mask() == 0xffff;
  }
  // Keep device observations on their original guest instruction boundaries.
  // Returns only complete shift/rotate/LOOP iterations within the granted budget.
  unsigned try_shift_loop_batch(unsigned step_budget);
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
  std::uint32_t real_mode_segment_address(unsigned segment, std::uint16_t offset) const {
    return std::uint32_t(segment_linear(segment, offset, 1, Access::Read));
  }
  unsigned cpl() const {
    return is_real_mode() || cache_[CS].inherited_from_real_mode
        ? 0U : unsigned(segments[CS] & 3U);
  }
  const char* mode_name() const {
    if (is_real_mode()) return cache_[CS].big ? "real32" : "real16";
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
    const auto memory_writes = memory.pin_writes();
    const Snapshot before{regs, segments, cache_, eip, eflags, cr0, cr4,
                          cr2, cr3, efer, fs_base, gs_base, gdtr, idtr, ldtr, tr, a20_enabled, halted,
                          xmm,mxcsr,x87,fpu_control,fpu_status,fpu_tags,
                          fpu_opcode,fpu_cs,fpu_ds,fpu_ip,fpu_dp,tsc_offset_};
    const bool was_shadowed=interrupt_shadow_;
    interrupt_shadow_=false;
    const auto trace_mode_before=step_trace_hook?std::string(mode_name()):std::string{};
    const auto trace_walks_before=page_walks;
    int delivered_vector=-1;
    std::uint32_t delivered_error=0;
    std::uint64_t delivered_linear=0;
    bool waiting_for_x87=false;
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
                       waiting_for_x87?"waiting-for-x87":halted?"halted":"executed";
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
    op32_ = is_long_mode() || cache_[CS].big;
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
      op32_ = (is_long_mode() || cache_[CS].big) != operand_override_;
      addr32_ = is_long_mode() ? address_override_
          : (cache_[CS].big != address_override_);
      execute(op);
      if (fault) fail(fault->reason, fault->code);
      // Only the first instruction in a consecutive blocking sequence
      // inhibits IRQ delivery. A bounded REP step consumes the shadow at
      // the first interruptible element boundary.
      if(was_shadowed)interrupt_shadow_=false;
      if (!ip_written_) eip = cursor_ & code_mask();
      eflags |= 2;
      ++instructions;
      transaction_ = false; journal_.clear();
      return halted ? StepResult::Halted : StepResult::Executed;
    } catch (const WaitingForX87&) {
      rollback(before);
      waiting_for_x87=true;
      return StepResult::WaitingForX87;
    } catch (const Trap& t) {
      rollback(before);
      // Intel preserves MXCSR sticky status across #XM/#UD on an unmasked
      // SIMD arithmetic exception even when the instruction does not retire.
      // Snapshot rollback must NOT erase these architecturally sticky flags.
      if(pending_simd_flags_)mxcsr|=pending_simd_flags_;
      fault = Fault{t.code, start_cs_, start_ip_, opcode_, decoded_, t.reason, t.address, t.error};
      if (t.code == FaultCode::PageFault) cr2 = t.address;
      const int vector = exception_vector(t.code);
      const bool real_fp_exception = t.code == FaultCode::X87FloatingPoint ||
          t.code == FaultCode::SimdFloatingPoint || t.code == FaultCode::DeviceNotAvailable ||
          t.code == FaultCode::InvalidInstruction;
      if (is_real_mode() && real_fp_exception) {
        // Deliver FP/SIMD exception vectors regardless of IF or BIOS hooks.
        // An empty or inaccessible vector preserves the terminal diagnostic.
        try {
          transaction_ = true;
          const auto target = real_ivt_target(std::uint8_t(vector));
          if (target.cs || target.ip) {
            dispatch_real_interrupt(target, start_ip_);
            transaction_ = false; journal_.clear();
            ++instructions;
            delivered_vector = vector;
            fault.reset();
            return StepResult::Executed;
          }
          transaction_ = false;
        } catch (const std::exception&) {
          rollback(before);
          if(pending_simd_flags_)mxcsr|=pending_simd_flags_;
        }
      }
      // Synchronous architectural exception: restart faulting instruction
      // after the guest handler completes. Unsupported/host failures stop.
      if (vector >= 0 && !is_real_mode()) {
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
  // uses the next instruction's IP and obeys IF and the interrupt shadow.
  // Pending IRQs remain with the originating device/controller.
  bool raise_external_irq(std::uint8_t vector) {
    if (!flag(IF) || interrupt_shadow_ || fault) return false;
    const auto memory_writes = memory.pin_writes();
    const Snapshot before{regs,segments,cache_,eip,eflags,cr0,cr4,
                          cr2,cr3,efer,fs_base,gs_base,gdtr,idtr,ldtr,tr,a20_enabled,halted,
                          xmm,mxcsr,x87,fpu_control,fpu_status,fpu_tags,
                          fpu_opcode,fpu_cs,fpu_ds,fpu_ip,fpu_dp,tsc_offset_};
    start_ip_=eip;start_cs_=segments[CS];journal_.clear();transaction_=true;
    try {
      if (is_real_mode()) {
        dispatch_real_interrupt(real_ivt_target(vector), eip);
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
  struct WaitingForX87 {};
  bool x87_ignne_ = false;
  bool interrupt_shadow_ = false;
  unsigned last_rep_batch_bytes_ = 0;

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
    std::uint64_t tsc_offset;
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
  using AtomicRam = std::array<std::size_t, 16>;

  bool op32_ = false, addr32_ = false, ip_written_ = false, transaction_ = false;
  bool operand_override_ = false, address_override_ = false, lock_prefix_ = false;
  std::uint8_t rex_ = 0;
  std::array<SegmentCache, 6> cache_{};
  int override_ = -1;
  std::uint8_t repeat_ = 0;
  std::uint16_t start_cs_ = 0, opcode_ = 0;
  std::uint64_t start_ip_ = 0, cursor_ = 0;
  static constexpr std::uint32_t kTscMsr = 0x10U, kCr4Tsd = 1U << 2;
  static constexpr std::uint64_t kTscTicksPerStep = 64;
  std::uint64_t tsc_offset_ = 0;
  // A deterministic guest clock advances by 64 ticks per interpreter step.
  std::uint64_t timestamp_counter() const noexcept { return tsc_offset_ + instructions * kTscTicksPerStep; }
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
    tsc_offset_ = s.tsc_offset;
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
  bool bulk_ram_range(std::uint64_t address, unsigned bytes) const {
    if (!bytes || address > UINT64_MAX - (bytes - 1)) return false;
    const auto bus = bus_address(address);
    if (bus >= memory.size() || bytes > memory.size() - bus ||
        bus_address(address + bytes - 1) != bus + bytes - 1) return false;
    if (mmio_claim_hook && (!mmio_range_claim_hook || mmio_range_claim_hook(bus, bytes))) return false;
    return !(memory.has_window() && bus < memory.window_base() + memory.window_bytes() &&
             memory.window_base() < bus + bytes);
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
  std::uint64_t real_segment_base(unsigned segment) const noexcept {
    // Host setup can assign a public visible selector before running guest
    // code. A guest segment load always updates both representations.
    const auto& cache = cache_[segment];
    return cache.selector == segments[segment]
        ? cache.base : std::uint64_t(segments[segment]) << 4;
  }
  void reload_real_segment(unsigned segment, std::uint16_t selector) {
    auto& cache = cache_[segment];
    segments[segment] = selector;
    cache.selector = selector;
    cache.base = std::uint64_t(selector) << 4;
    cache.valid = true;
    // A real-mode load changes the base without reloading limit or D/B.
    cache.inherited_from_real_mode = true;
  }
  std::uint64_t segment_linear(unsigned segment, std::uint64_t offset,
                               unsigned bytes, Access access) const {
    if (segment >= 6) fail("segment index", FaultCode::InvalidInstruction);
    if (is_real_mode()) {
      const auto& cache = cache_[segment];
      if (!cache.valid) fail("null/unusable real-mode segment", FaultCode::ProtectionFault);
      // Keep legacy CS overrides usable in real mode. Other registers retain
      // the memory permissions of the descriptors loaded in protected mode.
      if (segment != CS && ((access == Access::Write && !cache.writable) ||
                           (access == Access::Read && !cache.readable)))
        fail("real-mode segment access denied", FaultCode::ProtectionFault);
      const auto limit = cache.limit;
      if (bytes == 0 || offset > limit || bytes - 1 > limit - offset)
        fail("real-mode segment limit exceeded", FaultCode::SegmentLimit);
      return std::uint32_t(real_segment_base(segment) + offset);
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
  unsigned stack_address_width() const { return is_long_mode() ? 8 : cache_[SS].big ? 4 : 2; }
  std::uint64_t code_mask() const { return is_long_mode() ? UINT64_MAX : cache_[CS].big ? 0xffffffffULL : 0xffffULL; }
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
  void require_lock_destination(const Operand& operand, bool modifies_memory = true) const {
    if (lock_prefix_ && (operand.is_register || !modifies_memory))
      fail("LOCK requires a writable memory destination", FaultCode::InvalidInstruction);
  }
  AtomicRam atomic_ram(std::uint64_t address, unsigned bytes) {
    AtomicRam indices{};
    // The single-vCPU board ticks between instructions. Resolve all page and
    // bus addresses before reading the operand, using write-sense access for RMW.
    for (unsigned done = 0; done < bytes;) {
      const auto linear = address + done;
      const auto physical = translate(linear, Access::Write);
      const auto chunk = std::min(bytes - done, 0x1000U - unsigned(linear & 0xfffU));
      for (unsigned i = 0; i < chunk; ++i) {
        const auto bus = physical + i;
        if (mmio_at(bus))
          fail("atomic memory operation on MMIO is unsupported", FaultCode::UnsupportedState);
        const auto index = physical_index(bus);
        if (index >= memory.window_base() && index - memory.window_base() < memory.window_bytes())
          fail("atomic memory operation on the Ether window is unsupported", FaultCode::UnsupportedState);
        indices[done + i] = index;
      }
      done += chunk;
    }
    return indices;
  }
  std::uint64_t read_atomic_ram(const AtomicRam& indices, unsigned offset, unsigned bytes) const {
    std::uint64_t value = 0;
    for (unsigned i = 0; i < bytes; ++i)
      value |= std::uint64_t(memory.read_byte(indices[offset + i])) << (8U * i);
    return value;
  }
  void write_atomic_ram(const AtomicRam& indices, unsigned offset, unsigned bytes,
                        std::uint64_t value) {
    for (unsigned i = 0; i < bytes; ++i) {
      const auto index = indices[offset + i];
      const auto previous = memory.read_byte(index);
      const auto next = std::uint8_t(value >> (8U * i));
      if (previous == next) continue;
      journal_.emplace_back(index, previous);
      memory.write_byte(index, next);
    }
  }
  template<class Update>
  void modify_operand(const Operand& operand, unsigned bytes, Update update,
                      bool implicit_atomic = false) {
    if (!operand.is_register && (lock_prefix_ || implicit_atomic)) {
      const auto address = segment_linear(operand.segment, operand_offset(operand), bytes, Access::Write);
      const auto indices = atomic_ram(address, bytes);
      write_atomic_ram(indices, 0, bytes, update(read_atomic_ram(indices, 0, bytes)));
    } else {
      put(operand, bytes, update(get(operand, bytes)));
    }
  }
  void fpu_init() {
    fpu_control=0x037f;fpu_status=0;fpu_tags=0xffff;
    fpu_opcode=fpu_cs=fpu_ds=0;fpu_ip=fpu_dp=0;
    // x87 register data becomes architecturally empty; data bits are stale.
  }
  void fpu_recompute_summary() noexcept {
    if(fpu_status&~fpu_control&0x3fU)fpu_status|=0x8080U;
    else fpu_status&=~std::uint16_t(0x8080U);
  }
  static constexpr std::uint16_t fpu_normalize_control(std::uint16_t word) noexcept {
    return std::uint16_t((word&0x1f3fU)|0x0040U);
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
  void fpu_arithmetic_result(const x87_numeric::Result& result, unsigned slot, bool pop) {
    using namespace x87_numeric;
    fpu_status=std::uint16_t((fpu_status&~0x0200U) | (result.rounded_up?0x0200U:0U) |
                             result.exceptions);
    const auto unmasked=result.exceptions&~fpu_control&0x3fU;
    if(unmasked) fpu_status|=0x8080U; // ES+B; the next waiting instruction raises #MF.
    // Invalid operands and division by zero leave the destination unchanged.
    // Range exceptions with x87 register destinations use a biased exponent.
    if(unmasked&(Invalid|Denormal|DivideByZero)) return;
    x87[slot]=result.value;
    fpu_tag_set(slot,classify_ext(result.value));
    if(pop) fpu_retire_pop();
  }
  void fpu_arithmetic(unsigned group,const Extended80& lhs,const Extended80& rhs,
                      unsigned slot,bool pop=false,bool memory_denormal=false) {
    if(((fpu_control>>8)&3U)==1U) fail("reserved x87 PC=01",FaultCode::UnsupportedState);
    fpu_arithmetic_result(x87_numeric::arithmetic(group,lhs,rhs,fpu_control,memory_denormal),slot,pop);
  }
  void fpu_sqrt() {
    if(((fpu_control>>8)&3U)==1U) fail("reserved x87 PC=01",FaultCode::UnsupportedState);
    fpu_arithmetic_result(x87_numeric::square_root(fpu_peek(),fpu_control),fpu_slot(0),false);
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
  void require_fpu() const {
    if(cr0&0xcU)fail("x87 CR0.TS/EM device unavailable",FaultCode::DeviceNotAvailable);
  }
#include "x86_x87_state.inc"
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
    // With OSFXSR clear the x87 image is still available. This CPU profile
    // omits the implementation-dependent XMM and MXCSR fields in that mode.
    const bool save_simd = (cr4 & 0x200U) != 0;
    const auto access=restore?Access::Read:Access::Write;
    const auto address=segment_linear(memory_operand.segment,operand_offset(memory_operand),512,access);
    if(address&15U)fail("FXSAVE/FXRSTOR require 16-byte aligned memory",FaultCode::ProtectionFault);
    // The architecture checks all 512 bytes, including the software tail.
    // Preflight both pages exactly once before accessing the state image.
    std::array<std::size_t,512> indices{};
    for(unsigned offset=0;offset<indices.size();) {
      const auto linear=address+offset;
      const auto physical=translate(linear,access);
      const auto count=std::min(unsigned(indices.size())-offset,0x1000U-unsigned(linear&0xfffU));
      for(unsigned i=0;i<count;++i) {
        const auto bus=physical+i;
        if(mmio_at(bus))fail("FXSAVE/FXRSTOR requires RAM",FaultCode::UnsupportedState);
        const auto index=physical_index(bus);
        if(index>=memory.window_base() && index-memory.window_base()<memory.window_bytes())
          fail("FXSAVE/FXRSTOR cannot access the Ether window",FaultCode::UnsupportedState);
        indices[offset+i]=index;
      }
      offset+=count;
    }
    std::array<std::uint8_t,512> buffer{};
    if(restore) {
      for(unsigned i=0;i<(save_simd?416U:160U);++i) {
        if(!save_simd&&i>=24&&i<32)continue;
        buffer[i]=memory.read_byte(indices[i]);
      }
      const auto candidate_mxcsr=save_simd?load32(buffer,24):mxcsr;
      if(save_simd&&(candidate_mxcsr&~kMxcsrMask))
        fail("FXRSTOR reserved MXCSR bit",FaultCode::ProtectionFault);
      // Parse and validate entire image before committing any CPU state.
      const auto new_control=fpu_normalize_control(load16(buffer,0));
      const auto new_status=load16(buffer,2);
      const auto abridged=buffer[4];
      const auto new_opcode=std::uint16_t(load16(buffer,6)&0x7ffU);
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
      if(save_simd)
        for(unsigned i=0;i<xmm_count();++i)
          for(unsigned j=0;j<16;++j)new_xmm[i][j]=buffer[160+16*i+j];
      fpu_control=new_control;fpu_status=new_status;fpu_tags=new_tags;
      fpu_recompute_summary();
      fpu_opcode=new_opcode;fpu_ip=new_ip;fpu_dp=new_dp;
      fpu_cs=new_cs;fpu_ds=new_ds;x87=new_regs;
      mxcsr=candidate_mxcsr;xmm=new_xmm;
    }else{
      store16(buffer,0,fpu_control);store16(buffer,2,fpu_status);
      for(unsigned i=0;i<8;++i)
        if(fpu_tag(i)!=3U)buffer[4]|=std::uint8_t(1U<<i);
      store16(buffer,6,fpu_opcode&0x7ffU);
      const bool wide_pointers=is_long_mode()&&(rex_&8);
      if(wide_pointers){
        store64(buffer,8,fpu_ip);store64(buffer,16,fpu_dp);
      }else {
        store32(buffer,8,std::uint32_t(fpu_ip));store16(buffer,12,fpu_cs);
        store32(buffer,16,std::uint32_t(fpu_dp));store16(buffer,20,fpu_ds);
      }
      if(save_simd) {
        store32(buffer,24,mxcsr);store32(buffer,28,kMxcsrMask);
      }
      // X87 saved data ST(i) is LOGICAL, not the physical R(i)
      // position indicated by the separate abridged tag mask.
      for(unsigned i=0;i<8;++i)
        for(unsigned j=0;j<10;++j)
          buffer[32+16*i+j]=x87[fpu_slot(i)][j];
      if(save_simd)
        for(unsigned i=0;i<xmm_count();++i)
          for(unsigned j=0;j<16;++j)buffer[160+16*i+j]=xmm[i][j];
      // Intel reserves bytes 464-511 for software; never overwrite them.
      for(unsigned i=0;i<464;++i) {
        if(!save_simd&&((i>=24&&i<32)||(i>=160&&i<416)))continue;
        write8(indices[i],buffer[i]);
      }
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
      fail("SSE2 disabled by CR0.EM",FaultCode::InvalidInstruction);
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
  void masked_store(const std::array<std::uint8_t,16>& data,
                    const std::array<std::uint8_t,16>& mask_bytes,unsigned bytes) {
    const auto segment=override_>=0?unsigned(override_):DS;
    const auto address=segment_linear(segment,get_reg(EDI,address_width()),bytes,Access::Write);
    std::array<std::uint64_t,16> physical{};
    // Match the modelled Intel behavior: even masked-off addresses are
    // checked, but only selected bytes reach the memory bus as stores.
    for(unsigned done=0;done<bytes;){
      const auto linear=address+done;
      const auto first=translate(linear,Access::Write);
      const auto count=std::min(bytes-done,0x1000U-unsigned(linear&0xfffU));
      for(unsigned i=0;i<count;++i){
        const auto bus=first+i;
        physical[done+i]=bus;
        if(mask_bytes[done+i]&0x80U){
          if(mmio_at(bus))
            fail("masked SIMD store cannot write MMIO",FaultCode::UnsupportedState);
          (void)physical_index(bus);
        }
      }
      done+=count;
    }
    for(unsigned i=0;i<bytes;++i)
      if(mask_bytes[i]&0x80U)memory.prepare_write(physical_index(physical[i]),1);
    for(unsigned i=0;i<bytes;++i)
      if(mask_bytes[i]&0x80U)write8(physical[i],data[i]);
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
  static std::uint32_t fp32_bits(float value) {
    std::uint32_t bits{};std::memcpy(&bits,&value,sizeof(bits));return bits;
  }
  static std::uint32_t sse_approximate_lane(std::uint32_t raw,bool rsqrt) {
    const auto sign=raw&0x80000000U;
    const auto magnitude=raw&0x7fffffffU;
    if(magnitude>0x7f800000U)return raw|0x00400000U;
    if(magnitude<0x00800000U)return sign|0x7f800000U; // Signed zero or denormal.
    if(rsqrt&&sign)return 0xffc00000U;
    if(magnitude==0x7f800000U)return sign;
    float value;
    std::memcpy(&value,&raw,sizeof(value));
    // The architectural accuracy bound does not prescribe a lookup table.
    // Round using nearest regardless of guest MXCSR and discard host flags.
    HostRound nearest(0x1f80);
    volatile double input=value;
    volatile float approximate=static_cast<float>(1.0/(rsqrt?std::sqrt(input):input));
    const auto result=fp32_bits(approximate);
    return (result&0x7f800000U)?result:sign; // RCP always flushes tiny results.
  }
  void sse_approximate(unsigned opcode) {
    if(operand_override_||(repeat_&&repeat_!=0xf3))
      fail("invalid reciprocal SSE prefix",FaultCode::InvalidInstruction);
    require_sse_state();
    const auto m=modrm();require_xmm(m.reg);
    const bool scalar=repeat_==0xf3,rsqrt=opcode==0x52;
    auto result=xmm[m.reg];
    if(scalar)packed_set(result,0,4,sse_approximate_lane(sse32_source(m.rm),rsqrt));
    else {
      const auto source=read_xmm_operand(m.rm,true);
      for(unsigned offset=0;offset<16;offset+=4)
        packed_set(result,offset,4,sse_approximate_lane(std::uint32_t(packed_lane(source,offset,4)),rsqrt));
    }
    xmm[m.reg]=result;
  }
  static bool fp32_denormal(std::uint32_t raw) {
    return (raw&0x7f800000U)==0 && (raw&0x007fffffU)!=0;
  }
  static bool fp32_nan(std::uint32_t raw) {
    return (raw&0x7f800000U)==0x7f800000U && (raw&0x007fffffU)!=0;
  }
  static bool fp32_snan(std::uint32_t raw) {
    return fp32_nan(raw) && !(raw&0x00400000U);
  }
  std::uint32_t fp32_apply_daz(std::uint32_t raw) const {
    return (mxcsr&0x40U)&&fp32_denormal(raw) ? raw&0x80000000U : raw;
  }
  static std::uint32_t fp32_order_key(std::uint32_t raw) {
    constexpr std::uint32_t sign=0x80000000U;
    if((raw&~sign)==0)raw=0;
    return (raw&sign)?~raw:(raw^sign);
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
  std::uint32_t sse32_source(const Operand& source) {
    if(source.is_register){require_xmm(source.index);return std::uint32_t(packed_lane(xmm[source.index],0,4));}
    return std::uint32_t(get(source,4));
  }
  template<typename Float>
  Float compute_simd_float(unsigned opcode,volatile Float a,volatile Float b) {
    switch(opcode) {
      case 0x58:{volatile Float result=a+b;return result;}
      case 0x5c:{volatile Float result=a-b;return result;}
      case 0x59:{volatile Float result=a*b;return result;}
      case 0x5e:{volatile Float result=a/b;return result;}
      case 0x51:{volatile Float result=std::sqrt(b);return result;}
      default:fail("unsupported SSE floating-point operation",FaultCode::UnsupportedOpcode);
    }
  }
  template<typename Bits>
  static std::pair<Bits,int> simd_normalized(Bits magnitude) {
    constexpr bool single=sizeof(Bits)==4;
    constexpr unsigned shift=single?23:52;
    constexpr int bias=single?127:1023;
    constexpr auto fraction=(Bits{1}<<shift)-1;
    auto exponent=int(magnitude>>shift);
    if(!exponent) {
      exponent=1;
      while(!(magnitude&(Bits{1}<<shift))){magnitude<<=1;--exponent;}
    }
    return {(Bits(bias)<<shift)|(magnitude&fraction),exponent-bias};
  }
  template<typename Float, typename Bits>
  Bits simd_float_op(unsigned opcode,Bits left_raw,Bits right_raw,std::uint32_t& status) {
    constexpr bool single=sizeof(Bits)==4;
    constexpr Bits sign=Bits{1}<<(sizeof(Bits)*8-1);
    constexpr Bits quiet=Bits{1}<<(single?22:51);
    constexpr Bits indefinite=static_cast<Bits>(single?0xffc00000ULL:0xfff8000000000000ULL);
    constexpr Bits min_normal=Bits{1}<<(single?23:52);
    constexpr Bits infinity=static_cast<Bits>(single?0x7f800000ULL:0x7ff0000000000000ULL);
    const auto nan=[](Bits raw) { if constexpr(single)return fp32_nan(raw);else return fp64_nan(raw); };
    const auto denormal=[](Bits raw) { if constexpr(single)return fp32_denormal(raw);else return fp64_subnormal(raw); };
    const auto order=[](Bits raw) { if constexpr(single)return fp32_order_key(raw);else return fp64_order_key(raw); };
    const auto daz=[&](Bits raw) { return (mxcsr&0x40U)&&denormal(raw)?raw&~(sign-1):raw; };
    left_raw=opcode==0x51?0:daz(left_raw);
    right_raw=daz(right_raw);
    const bool minmax=opcode==0x5d||opcode==0x5f;
    const bool left_nan=nan(left_raw),right_nan=nan(right_raw);
    if(left_nan||right_nan) {
      if(minmax||(left_nan&&!(left_raw&quiet))||(right_nan&&!(right_raw&quiet)))status|=1U;
      // MIN/MAX select the second operand unchanged. Arithmetic quiets
      // the first NaN in operand order, preserving its sign and payload.
      if(minmax)return right_raw;
      return (left_nan?left_raw:right_raw)|quiet;
    }
    const auto left_magnitude=left_raw&~sign,right_magnitude=right_raw&~sign;
    if(opcode==0x51&&(right_raw&sign)&&right_magnitude) {
      status|=1U;return indefinite; // Invalid precedes denormal on a negative root.
    }
    if(opcode==0x5e&&left_magnitude&&left_magnitude<infinity&&!right_magnitude) {
      status|=4U;return ((left_raw^right_raw)&sign)|infinity; // Divide-by-zero suppresses DE.
    }
    const bool denormal_source=denormal(left_raw)||denormal(right_raw);
    if(denormal_source) {
      status|=2U;
      if(!(mxcsr&(1U<<8)))return indefinite; // Unmasked DE precedes arithmetic.
    }
    if(minmax) {
      const auto l=order(left_raw),r=order(right_raw);
      if(l==r)return right_raw;
      return opcode==0x5d?(l<r?left_raw:right_raw):
                             (l>r?left_raw:right_raw);
    }
    Float left{},right{},rounded{};
    std::memcpy(&left,&left_raw,sizeof(left));
    std::memcpy(&right,&right_raw,sizeof(right));
    std::uint32_t result_status=0;
    {
      HostRound rounding(mxcsr);
      rounded=compute_simd_float(opcode,left,right);
      result_status=rounding.status();
    }
    if(result_status&1U) {
      status|=result_status;
      return indefinite;
    }
    Bits bits{};std::memcpy(&bits,&rounded,sizeof(bits));
    const auto magnitude=bits&~sign;
    bool tiny=magnitude<min_normal&&(magnitude||(result_status&16U));
    std::uint32_t normal_precision=result_status&32U;
    const bool near_tiny=magnitude<=min_normal&&(magnitude||(result_status&16U));
    const bool overflow_traps=(result_status&8U)&&!(mxcsr&(1U<<10));
    if((near_tiny||overflow_traps)&&(result_status&32U)&&
       (opcode==0x59||opcode==0x5e||overflow_traps)) {
      // Intel detects tininess from the arithmetic rounded at its precision
      // with an unbounded exponent. Rescale the original significands to a
      // portable normal operation, avoiding host underflow/tininess policy.
      constexpr int bias=single?127:1023;
      constexpr unsigned shift=single?23:52;
      auto l=simd_normalized(left_magnitude),r=simd_normalized(right_magnitude);
      if(opcode==0x58||opcode==0x5c) {
        // Overflowing finite sums cannot be produced by a zero plus a
        // representable operand. Uniform rescaling preserves their sum.
        const auto maximum=std::max(l.second,r.second);
        const auto scale=[&](auto& value) {
          const auto exponent=std::max(1,bias+value.second-maximum);
          value.first=(value.first&(min_normal-1))|(Bits(exponent)<<shift);
        };
        scale(l);scale(r);
      }
      const auto lbits=l.first|(left_raw&sign),rbits=r.first|(right_raw&sign);
      Float a{},b{};
      std::memcpy(&a,&lbits,sizeof(a));std::memcpy(&b,&rbits,sizeof(b));
      HostRound rounding(mxcsr);
      volatile Float normal=compute_simd_float(opcode,a,b);
      const Float normal_value=normal;
      Bits normalized{};std::memcpy(&normalized,&normal_value,sizeof(normalized));
      if(near_tiny) {
        const auto exponent=int((normalized&~sign)>>shift)-bias+l.second+
                            (opcode==0x59?r.second:-r.second);
        tiny=exponent<1-bias;
      }
      normal_precision=rounding.status()&32U;
    }
    if(overflow_traps)result_status=(result_status&~32U)|normal_precision;
    result_status&=~16U;
    if(tiny&&!(mxcsr&(1U<<11))) {
      result_status=(result_status&~32U)|16U|normal_precision;
    }else if((mxcsr&0x8000U)&&tiny) {
      bits&=sign;
      result_status|=16U|32U;
    }else if(tiny&&(result_status&32U))result_status|=16U;
    status|=result_status;
    return bits;
  }
  void sse_float(unsigned opcode) {
    if(operand_override_&&repeat_)
      fail("conflicting SSE floating-point prefixes",FaultCode::InvalidInstruction);
    const bool scalar=repeat_!=0;
    const bool single=repeat_==0xf3 || (!scalar&&!operand_override_);
    const unsigned n=single?4:8;
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    const auto before=xmm[m.reg];
    auto result=before;
    std::uint32_t aggregated_status=0;
    std::array<std::uint8_t,16> source{};
    if(!scalar)source=read_xmm_operand(m.rm,true);
    else if(m.rm.is_register){require_xmm(m.rm.index);source=xmm[m.rm.index];}
    else packed_set(source,0,n,get(m.rm,n));
    for(unsigned at=0;at<(scalar?n:16U);at+=n) {
      const auto left=packed_lane(before,at,n),right=packed_lane(source,at,n);
      const auto value=single?std::uint64_t(simd_float_op<float>(opcode,std::uint32_t(left),std::uint32_t(right),aggregated_status)):
                              simd_float_op<double>(opcode,left,right,aggregated_status);
      packed_set(result,at,n,value);
    }
    simd_status(aggregated_status);
    xmm[m.reg]=result;
  }
  std::uint64_t sse2_fp64_to_integer(std::uint64_t raw,unsigned bytes,
                                    bool truncate,std::uint32_t& status) {
    // These conversions honor DAZ without raising a denormal-operand
    // exception. Rounding a nonzero subnormal to an integer raises PE.
    raw=fp64_apply_daz(raw);
    if((raw&0x7ff0000000000000ULL)!=0x7ff0000000000000ULL) {
      HostRound mode(mxcsr);
      const auto value=fp64_from_bits(raw);
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
      if(repeat_)fail("invalid UCOMIS/COMIS prefix",FaultCode::InvalidInstruction);
      require_xmm(m.reg);
      std::uint32_t status=0;
      bool unordered,equal,less,signaling,denormal;
      if(operand_override_) {
        const auto left=fp64_apply_daz(packed_lane(xmm[m.reg],0,8));
        const auto right=fp64_apply_daz(sse64_source(m.rm));
        unordered=fp64_nan(left)||fp64_nan(right);
        signaling=fp64_snan(left)||fp64_snan(right);
        denormal=fp64_subnormal(left)||fp64_subnormal(right);
        equal=fp64_order_key(left)==fp64_order_key(right);
        less=fp64_order_key(left)<fp64_order_key(right);
      }else {
        const auto left=fp32_apply_daz(std::uint32_t(packed_lane(xmm[m.reg],0,4)));
        const auto right=fp32_apply_daz(sse32_source(m.rm));
        unordered=fp32_nan(left)||fp32_nan(right);
        signaling=fp32_snan(left)||fp32_snan(right);
        denormal=fp32_denormal(left)||fp32_denormal(right);
        equal=fp32_order_key(left)==fp32_order_key(right);
        less=fp32_order_key(left)<fp32_order_key(right);
      }
      if(!unordered&&denormal)status|=2U;
      if(unordered&&(opcode==0x2f||signaling))status|=1U;
      simd_status(status);
      eflags&=~std::uint32_t(CF|PF|AF|ZF|SF|OF);
      set_flag(ZF,unordered||equal);set_flag(PF,unordered);set_flag(CF,unordered||less);
      return;
    }
    if(!repeat_||operand_override_)
      fail("scalar SSE conversion requires F2 or F3",FaultCode::UnsupportedOpcode);
    const bool single=repeat_==0xf3;
    if(opcode==0x2a) { // CVTSI2SS/CVTSI2SD
      require_xmm(m.reg);
      const unsigned bytes=is_long_mode()&&(rex_&8U)?8U:4U;
      const auto integer=signed_value(get(m.rm,bytes),bytes);
      std::uint32_t status=0;
      std::uint64_t result=0;
      {
        HostRound mode(mxcsr);
        volatile std::int64_t input=integer;
        if(single) {
          volatile float converted=static_cast<float>(input);
          result=fp32_bits(converted);
        }else {
          volatile double converted=static_cast<double>(input);
          result=fp64_bits(converted);
        }
        status|=mode.status();
      }
      simd_status(status);
      packed_set(xmm[m.reg],0,single?4:8,result);
      return;
    }
    if(opcode==0x2c||opcode==0x2d) { // CVTT/CVT SS/SD to a GPR.
      const unsigned bytes=is_long_mode()&&(rex_&8U)?8U:4U;
      std::uint32_t status=0;
      const auto converted=single?sse_fp32_to_integer(sse32_source(m.rm),bytes,opcode==0x2c,status):
          sse2_fp64_to_integer(sse64_source(m.rm),bytes,opcode==0x2c,status);
      simd_status(status);
      put_reg(m.reg,bytes,converted);
      return;
    }
    fail("unsupported SSE2 scalar conversion",FaultCode::UnsupportedOpcode);
  }
  static std::uint64_t fp32_promote(std::uint32_t raw) {
    constexpr auto fraction_mask=0x007fffffU;
    const auto sign=std::uint64_t(raw&0x80000000U)<<32;
    const auto exponent=(raw>>23)&0xffU;
    auto fraction=raw&fraction_mask;
    if(exponent==0xffU)
      return sign|0x7ff0000000000000ULL|(std::uint64_t(fraction)<<29);
    if(exponent)
      return sign|(std::uint64_t(exponent+896U)<<52)|(std::uint64_t(fraction)<<29);
    if(!fraction)return sign;
    unsigned exp=897;
    while(!(fraction&0x00800000U)){fraction<<=1;--exp;}
    return sign|(std::uint64_t(exp)<<52)|(std::uint64_t(fraction&fraction_mask)<<29);
  }
  std::uint64_t sse_fp32_to_integer(std::uint32_t raw,unsigned bytes,
                                     bool truncate,std::uint32_t& status) {
    return sse2_fp64_to_integer(fp32_promote(fp32_apply_daz(raw)),bytes,truncate,status);
  }
  std::uint64_t sse2_widen_fp32(std::uint32_t raw,std::uint32_t& status) const {
    raw=fp32_apply_daz(raw);
    const auto promoted=fp32_promote(raw);
    if(fp32_snan(raw)) {
      status|=1U;
      return promoted|(1ULL<<51);
    }
    if(fp32_denormal(raw))status|=2U;
    return promoted;
  }
  std::uint32_t sse2_narrow_fp64(std::uint64_t raw,std::uint32_t& status) {
    raw=fp64_apply_daz(raw);
    const auto sign=std::uint32_t((raw>>32)&0x80000000U);
    if(fp64_nan(raw)) {
      if(fp64_snan(raw))status|=1U;
      return sign|0x7fc00000U|std::uint32_t((raw>>29)&0x003fffffU);
    }
    if(fp64_subnormal(raw))status|=2U;
    std::uint32_t result=0;
    std::uint32_t flags=0;
    {
      HostRound mode(mxcsr);
      volatile double source=fp64_from_bits(raw);
      volatile float rounded=static_cast<float>(source);
      result=fp32_bits(rounded);
      flags=mode.status();
    }
    if((flags&8U)&&!(mxcsr&(1U<<10))) {
      // The unmasked result keeps binary32 precision with an unbounded
      // exponent. Losing only exponent range does not signal precision.
      flags=(flags&~32U)|((raw&0x1fffffffU)?32U:0U);
    }
    constexpr auto single_min_normal=0x3810000000000000ULL;
    const auto magnitude=raw&0x7fffffffffffffffULL;
    if(magnitude&&magnitude<single_min_normal) {
      // x86 tests tininess after rounding to 24 significant bits with an
      // unbounded exponent, before applying the binary32 subnormal range.
      const auto rc=(mxcsr>>13)&3U;
      const bool away=(rc==1&&sign)||(rc==2&&!sign);
      const bool tiny=rc==0?magnitude<0x380ffffff0000000ULL:
                      away?magnitude<=0x380fffffe0000000ULL:true;
      flags&=~16U;
      if(tiny&&!(mxcsr&(1U<<11))) {
        // Unmasked underflow reports only precision lost in the unbounded
        // rounding, independently of rounded bits in the stored format.
        auto normalized=magnitude;
        if(fp64_subnormal(raw)) {
          while(!(normalized&(1ULL<<52)))normalized<<=1;
        }
        flags=(flags&~32U)|16U|((normalized&0x1fffffffU)?32U:0U);
      }else if(tiny&&(mxcsr&0x8000U)) {
        result=sign;
        flags|=16U|32U;
      }else if(tiny&&(flags&32U)) {
        flags|=16U;
      }
    }
    status|=flags;
    return result;
  }
  void sse2_convert_precision() { // 0F 5A: binary32/binary64
    if(operand_override_&&repeat_)
      fail("conflicting SSE2 conversion prefixes",FaultCode::InvalidInstruction);
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    const bool scalar=repeat_!=0;
    const bool widen=repeat_==0xf3||(!scalar&&!operand_override_);
    auto result=scalar?xmm[m.reg]:std::array<std::uint8_t,16>{};
    std::uint32_t status=0;
    if(widen) {
      const auto source=scalar?sse32_source(m.rm):sse64_source(m.rm);
      packed_set(result,0,8,sse2_widen_fp32(std::uint32_t(source),status));
      if(!scalar)packed_set(result,8,8,sse2_widen_fp32(std::uint32_t(source>>32),status));
    }else if(scalar) {
      packed_set(result,0,4,sse2_narrow_fp64(sse64_source(m.rm),status));
    }else{
      const auto source=read_xmm_operand(m.rm,true);
      for(unsigned lane=0;lane<2;++lane)
        packed_set(result,lane*4,4,sse2_narrow_fp64(packed_lane(source,lane*8,8),status));
    }
    simd_status(status);
    xmm[m.reg]=result;
  }
  void sse2_convert_dword() { // 0F 5B: binary32/signed32
    if(repeat_==0xf2||(operand_override_&&repeat_))
      fail("invalid SSE2 dword conversion prefix",FaultCode::InvalidInstruction);
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    const auto source=read_xmm_operand(m.rm,true);
    std::array<std::uint8_t,16> result{};
    std::uint32_t status=0;
    if(!operand_override_&&!repeat_) {
      HostRound mode(mxcsr);
      for(unsigned lane=0;lane<4;++lane) {
        volatile std::int64_t input=signed_value(packed_lane(source,lane*4,4),4);
        volatile float rounded=static_cast<float>(input);
        packed_set(result,lane*4,4,fp32_bits(rounded));
      }
      status=mode.status();
    }else{
      for(unsigned lane=0;lane<4;++lane) {
        const auto raw=std::uint32_t(packed_lane(source,lane*4,4));
        packed_set(result,lane*4,4,sse_fp32_to_integer(raw,4,repeat_==0xf3,status));
      }
    }
    simd_status(status);
    xmm[m.reg]=result;
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
  void sse_cmp_vector() {
    if(operand_override_&&repeat_)fail("conflicting SSE CMP prefixes",FaultCode::InvalidInstruction);
    const bool scalar=repeat_!=0;
    const bool single=repeat_==0xf3||(!scalar&&!operand_override_);
    const unsigned n=single?4:8;
    require_sse_state();
    const auto m=modrm();
    require_xmm(m.reg);
    // Decode imm8 first: RIP-relative address is based on the next IP.
    const auto predicate=unsigned(immediate(1))&7U;
    const auto before=xmm[m.reg];
    auto result=before;
    std::array<std::uint8_t,16> source{};
    if(scalar)packed_set(source,0,n,single?sse32_source(m.rm):sse64_source(m.rm));
    else source=read_xmm_operand(m.rm,true);
    std::uint32_t status=0;
    const bool signaling=(predicate==1U||predicate==2U||
                          predicate==5U||predicate==6U);
    for(unsigned off=0;off<(scalar?n:16U);off+=n){
      bool unordered,snan,denormal;
      std::uint64_t a,b;
      if(single) {
        const auto left=fp32_apply_daz(std::uint32_t(packed_lane(before,off,n)));
        const auto right=fp32_apply_daz(std::uint32_t(packed_lane(source,off,n)));
        unordered=fp32_nan(left)||fp32_nan(right);
        snan=fp32_snan(left)||fp32_snan(right);
        denormal=fp32_denormal(left)||fp32_denormal(right);
        a=fp32_order_key(left);b=fp32_order_key(right);
      }else {
        const auto left=fp64_apply_daz(packed_lane(before,off,n));
        const auto right=fp64_apply_daz(packed_lane(source,off,n));
        unordered=fp64_nan(left)||fp64_nan(right);
        snan=fp64_snan(left)||fp64_snan(right);
        denormal=fp64_subnormal(left)||fp64_subnormal(right);
        a=fp64_order_key(left);b=fp64_order_key(right);
      }
      if(!unordered&&denormal)status|=2U;
      if((unordered&&signaling)||snan)status|=1U;
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
      packed_set(result,off,n,matching?UINT64_MAX:0);
    }
    simd_status(status); // all lanes first, then exception before commit
    xmm[m.reg]=result;
  }
  static void shift_packed_lanes(std::array<std::uint8_t,16>& value,
                                 unsigned bytes, std::uint64_t count, unsigned operation,
                                 unsigned vector_bytes = 16) {
    const unsigned bits = 8U * bytes;
    const auto lane_mask = mask(bytes);
    // /2 = logical right, /4 = arithmetic right, /6 = logical left.
    if (operation == 4) count = std::min<std::uint64_t>(count, bits - 1);
    else if (count >= bits) { value.fill(0); return; }
    if (count == 0) return;
    for (unsigned offset = 0; offset < vector_bytes; offset += bytes) {
      const auto lane = packed_lane(value, offset, bytes);
      auto shifted = operation == 6 ? lane << count : lane >> count;
      if (operation == 4 && (lane & sign_bit(bytes)))
        shifted |= lane_mask ^ (lane_mask >> count);
      packed_set(value, offset, bytes, shifted);
    }
  }
  void sse_integer_shift_immediate(unsigned opcode) {
    if (!operand_override_ || repeat_)
      fail("packed integer immediate shift requires 66", FaultCode::UnsupportedOpcode);
    require_sse_state();
    const auto m = modrm();
    const bool byte_shift = opcode == 0x73 && (m.group == 3 || m.group == 7);
    const bool lane_shift = m.group == 2 || m.group == 6 || (opcode != 0x73 && m.group == 4);
    if (!m.rm.is_register || (!byte_shift && !lane_shift))
      fail("invalid packed integer immediate shift", FaultCode::InvalidInstruction);
    require_xmm(m.rm.index);
    const auto count = unsigned(immediate(1));
    auto& dest = xmm[m.rm.index];
    if (!byte_shift) {
      shift_packed_lanes(dest, 2U << (opcode - 0x71U), count, m.group);
      return;
    }
    const auto source = dest;
    for (unsigned i = 0; i < 16; ++i) {
      const int src = m.group == 3 ? int(i) + int(count) : int(i) - int(count);
      dest[i] = src >= 0 && src < 16 ? source[unsigned(src)] : 0;
    }
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
    xmm[m.reg]=packed_integer_result<16>(opcode,xmm[m.reg],read_xmm_operand(m.rm,true));
  }
  template<unsigned vector_bytes>
  std::array<std::uint8_t,16> packed_integer_result(unsigned opcode,
        const std::array<std::uint8_t,16>& lhs,const std::array<std::uint8_t,16>& rhs) {
    static_assert(vector_bytes == 8 || vector_bytes == 16);
    constexpr unsigned half=vector_bytes/2;
    auto result=lhs;
    switch(opcode){
      case 0xdb: case 0xdf: case 0xeb: case 0xef: { // PAND, PANDN, POR, PXOR
        for(unsigned i=0;i<vector_bytes;++i)
          result[i]=opcode==0xdb?std::uint8_t(lhs[i]&rhs[i]):
                    opcode==0xdf?std::uint8_t(~lhs[i]&rhs[i]):
                                     opcode==0xeb?std::uint8_t(lhs[i]|rhs[i]):
                                     std::uint8_t(lhs[i]^rhs[i]);
        break;
      }
      case 0x60: case 0x61: case 0x62: case 0x6c:
      case 0x68: case 0x69: case 0x6a: case 0x6d: { // PUNPCK low/high
        const bool high=(opcode==0x68||opcode==0x69||opcode==0x6a||opcode==0x6d);
        const unsigned size=(opcode==0x60||opcode==0x68)?1U:
                            (opcode==0x61||opcode==0x69)?2U:
                            (opcode==0x62||opcode==0x6a)?4U:8U;
        const unsigned first=high?half:0U;
        for(unsigned i=0;i<half/size;++i){
          for(unsigned j=0;j<size;++j) {
            result[(2*i)*size+j]=lhs[first+i*size+j];
            result[(2*i+1)*size+j]=rhs[first+i*size+j];
          }
        }
        break;
      }
      case 0x63: case 0x67: case 0x6b: { // PACKSSWB, PACKUSWB, PACKSSDW.
        const unsigned bytes = opcode == 0x6b ? 2U : 1U;
        const auto low = opcode == 0x67 ? 0 : -(std::int64_t{1} << (8U * bytes - 1));
        const auto high = opcode == 0x67 ? 255 : -low - 1;
        for (unsigned lane = 0; lane < half / bytes; ++lane) {
          const auto at = lane * bytes;
          const auto l = signed_value(packed_lane(lhs, at * 2, bytes * 2), bytes * 2);
          const auto r = signed_value(packed_lane(rhs, at * 2, bytes * 2), bytes * 2);
          packed_set(result, at, bytes, std::uint64_t(std::clamp(l, low, high)));
          packed_set(result, at + half, bytes, std::uint64_t(std::clamp(r, low, high)));
        }
        break;
      }
      case 0xd8: case 0xd9: case 0xdc: case 0xdd:
      case 0xe8: case 0xe9: case 0xec: case 0xed: { // Saturating PADD/PSUB.
        const unsigned bytes = (opcode & 1U) + 1;
        const bool is_signed = (opcode & 0x20U) != 0;
        const bool subtract = (opcode & 4U) == 0;
        const auto low = is_signed ? -(std::int64_t{1} << (8U * bytes - 1)) : 0;
        const auto high = is_signed ? -low - 1 : std::int64_t(mask(bytes));
        for (unsigned at = 0; at < vector_bytes; at += bytes) {
          const auto left = packed_lane(lhs, at, bytes), right = packed_lane(rhs, at, bytes);
          const auto l = is_signed ? signed_value(left, bytes) : std::int64_t(left);
          const auto r = is_signed ? signed_value(right, bytes) : std::int64_t(right);
          const auto answer = std::clamp(subtract ? l - r : l + r, low, high);
          packed_set(result, at, bytes, std::uint64_t(answer));
        }
        break;
      }
      case 0xd5: case 0xe4: case 0xe5: { // PMULLW, PMULHUW, PMULHW.
        for (unsigned at = 0; at < vector_bytes; at += 2) {
          const auto l = packed_lane(lhs, at, 2), r = packed_lane(rhs, at, 2);
          const auto product = opcode == 0xe5
              ? std::uint64_t(signed_value(l, 2) * signed_value(r, 2)) : l * r;
          packed_set(result, at, 2, opcode == 0xd5 ? product : product >> 16);
        }
        break;
      }
      case 0xf4: { // PMULUDQ multiplies even dwords into qwords.
        for (unsigned at = 0; at < vector_bytes; at += 8)
          packed_set(result, at, 8, packed_lane(lhs, at, 4) * packed_lane(rhs, at, 4));
        break;
      }
      case 0xf5: { // PMADDWD wraps the sum of two signed word products.
        for (unsigned at = 0; at < vector_bytes; at += 4) {
          const auto first = signed_value(packed_lane(lhs, at, 2), 2) *
                             signed_value(packed_lane(rhs, at, 2), 2);
          const auto second = signed_value(packed_lane(lhs, at + 2, 2), 2) *
                              signed_value(packed_lane(rhs, at + 2, 2), 2);
          packed_set(result, at, 4, std::uint64_t(first + second));
        }
        break;
      }
      case 0xda: case 0xde: case 0xea: case 0xee: { // PMINUB, PMAXUB, PMINSW, PMAXSW.
        const unsigned bytes = (opcode & 0x20U) ? 2U : 1U;
        const bool maximum = (opcode & 4U) != 0;
        for (unsigned at = 0; at < vector_bytes; at += bytes) {
          const auto left = packed_lane(lhs, at, bytes), right = packed_lane(rhs, at, bytes);
          const auto l = bytes == 2 ? signed_value(left, bytes) : std::int64_t(left);
          const auto r = bytes == 2 ? signed_value(right, bytes) : std::int64_t(right);
          packed_set(result, at, bytes, std::uint64_t(maximum ? std::max(l, r) : std::min(l, r)));
        }
        break;
      }
      case 0xe0: case 0xe3: { // PAVGB/PAVGW round the unsigned mean upward.
        const unsigned bytes = (opcode & 1U) + 1;
        for (unsigned at = 0; at < vector_bytes; at += bytes) {
          const auto l = packed_lane(lhs, at, bytes), r = packed_lane(rhs, at, bytes);
          packed_set(result, at, bytes, (l + r + 1) / 2);
        }
        break;
      }
      case 0xf6: { // PSADBW writes one zero-extended sum per qword.
        for (unsigned at = 0; at < vector_bytes; at += 8) {
          unsigned sum = 0;
          for (unsigned i = at; i < at + 8; ++i)
            sum += lhs[i] >= rhs[i] ? lhs[i] - rhs[i] : rhs[i] - lhs[i];
          packed_set(result, at, 8, sum);
        }
        break;
      }
      case 0xd1: case 0xd2: case 0xd3: // PSRLW/D/Q.
      case 0xe1: case 0xe2:            // PSRAW/D.
      case 0xf1: case 0xf2: case 0xf3: // PSLLW/D/Q.
        shift_packed_lanes(result, 2U << ((opcode & 3U) - 1), packed_lane(rhs, 0, 8),
                           opcode < 0xe0 ? 2 : opcode < 0xf0 ? 4 : 6,vector_bytes);
        break;
      default: { // Packed integer wrapping arithmetic and comparisons.
        const bool is_compare=(opcode>=0x74&&opcode<=0x76)||
                               (opcode>=0x64&&opcode<=0x66);
        const bool is_add=(opcode>=0xfc&&opcode<=0xfe)||opcode==0xd4;
        const bool is_sub=(opcode>=0xf8&&opcode<=0xfa)||opcode==0xfb;
        if(!is_compare&&!is_add&&!is_sub)
          fail("packed SSE2 arithmetic opcode not implemented",FaultCode::UnsupportedOpcode);
        const unsigned size=
          (opcode==0x74||opcode==0x64||opcode==0xfc||opcode==0xf8)?1U:
          (opcode==0x75||opcode==0x65||opcode==0xfd||opcode==0xf9)?2U:
          (opcode==0x76||opcode==0x66||opcode==0xfe||opcode==0xfa)?4U:8U;
        const auto lane_mask=size==8?UINT64_MAX:((1ULL<<(8*size))-1);
        for(unsigned offset=0;offset<vector_bytes;offset+=size){
          const auto l=packed_lane(lhs,offset,size);
          const auto r=packed_lane(rhs,offset,size);
          std::uint64_t next=0;
          if(is_compare) {
            const bool matches=opcode>=0x74?
                l==r:signed_value(l,size)>signed_value(r,size);
            next=matches?lane_mask:0;
          }else next=is_add?l+r:l-r;
          packed_set(result,offset,size,next);
        }
        break;
      }
    }
    return result;
  }
#include "x86_mmx.inc"
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
  struct RealInterruptTarget { std::uint16_t cs, ip; };
  RealInterruptTarget real_ivt_target(std::uint8_t vector) {
    const auto offset = std::uint32_t(vector) * 4;
    if (offset + 3 > idtr.limit || idtr.base > UINT64_MAX - offset - 3)
      fail("interrupt vector exceeds real-mode IVT limit", FaultCode::SegmentLimit);
    const auto ip = read16(idtr.base + offset);
    return {read16(idtr.base + offset + 2), ip};
  }
  void dispatch_real_interrupt(RealInterruptTarget target, std::uint64_t resume_ip) {
    // Real-mode IVT delivery saves FLAGS, CS and IP as three words.
    push(eflags, 2); push(segments[CS], 2); push(resume_ip, 2);
    eflags &= ~(IF | TF);
    far_transfer(target.cs, target.ip, 2);
  }
  void interrupt(std::uint8_t vector) {
    if (!is_real_mode()) {
      dispatch_idt(vector, cursor_, true, false, 0);
      return;
    }
    eip = cursor_ & code_mask(); ip_written_ = true;
    if (interrupt_hook && interrupt_hook(*this, vector)) return;
    dispatch_real_interrupt(real_ivt_target(vector), eip);
  }

  static SegmentCache null_stack_cache(std::uint16_t selector) noexcept {
    // 64-bit stack addressing ignores SS. Legacy and compatibility
    // code still check the cache, so the null descriptor is unusable.
    SegmentCache stack;
    stack.selector = selector;
    stack.dpl = std::uint8_t(selector & 3U);
    stack.valid = false;
    return stack;
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
    // EFER.LMA selects the IDT format, including for compatibility code.
    const bool long_gate = (efer & 0x400U) != 0;
    const auto old_cpl = cpl();
    const unsigned length = long_gate ? 16U : 8U;
    const auto slot = std::uint64_t(vector) * length;
    if (slot + length - 1 > idtr.limit || idtr.base > UINT64_MAX - slot - length)
      fail("interrupt vector outside guest IDT", FaultCode::ProtectionFault);
    const auto address = idtr.base + slot;
    if (long_gate && (!canonical(address) || !canonical(address + length - 1)))
      fail("noncanonical IDT entry", FaultCode::ProtectionFault);
    std::uint64_t low = 0, high = 0;
    {
      SupervisorAccess system(*this);
      low = read_linear(address, 8);
      if (long_gate) high = read_linear(address + 8, 8);
    }
    const auto selector = std::uint16_t((low >> 16) & 0xffffU);
    const auto ist = unsigned((low >> 32) & 7U);
    const auto reserved = unsigned((low >> 35) & 31U);
    const auto attr = unsigned((low >> 40) & 255U);
    const auto type = attr & 31U;
    const bool gate16 = !long_gate && (type == 6 || type == 7);
    if ((!gate16 && type != 0xe && type != 0xf) || !(selector & 0xfffcU))
      fail("IDT gate is not an interrupt or trap gate for this mode", FaultCode::ProtectionFault);
    if (!(attr & 0x80)) fail("IDT gate not present", FaultCode::ProtectionFault);
    if (software && old_cpl > ((attr >> 5) & 3U))
      fail("INT gate DPL check failed", FaultCode::ProtectionFault);
    if (reserved || (!long_gate && ist) || (long_gate && (high >> 32)))
      fail("IDT gate reserved fields are set", FaultCode::ProtectionFault);
    const auto target_ip = (low & 0xffffU) | (gate16 ? 0 : ((low >> 48) & 0xffffU) << 16) |
                           ((high & 0xffffffffULL) << 32);
    if (long_gate && !canonical(target_ip))
      fail("noncanonical IDT handler RIP", FaultCode::ProtectionFault);
    const auto target_cache = descriptor(selector);
    if (!target_cache.code || (long_gate && (!target_cache.long_code || target_cache.big)))
      fail("IDT destination is not a compatible code segment", FaultCode::ProtectionFault);
    const auto target_cpl = target_cache.conforming ? old_cpl : unsigned(target_cache.dpl);
    if (target_cache.dpl > old_cpl ||
        target_cpl > old_cpl || (target_cpl != 0 && target_cpl != 3))
      fail("IDT destination privilege transition not supported", FaultCode::ProtectionFault);
    const bool switch_cpl = target_cpl < old_cpl;
    const auto original_cs = segments[CS], original_ss = segments[SS];
    const auto original_rsp = regs[ESP];
    const auto original_flags = eflags;
    const unsigned width = long_gate ? 8U : gate16 ? 2U : 4U;
    if (switch_cpl || (long_gate && ist)) {
      if (!tr.valid) fail("interrupt stack switch needs a loaded TSS", FaultCode::ProtectionFault);
      SupervisorAccess system(*this);
      if (long_gate) {
        const auto tss_offset = ist ? 36U + (ist-1U)*8U : 4U;
        if (tr.limit < tss_offset+7U)
          fail("TSS does not contain the requested 64-bit stack", FaultCode::ProtectionFault);
        const auto new_rsp = read_linear(tr.base+tss_offset,8);
        if (!canonical(new_rsp) || !new_rsp)
          fail("TSS provided a noncanonical or null RSP", FaultCode::ProtectionFault);
        // IA-32e selects a null SS with the handler's RPL on a stack switch.
        segments[SS] = std::uint16_t(target_cpl);
        cache_[SS] = null_stack_cache(segments[SS]);
        regs[ESP] = new_rsp;
      } else {
        const unsigned sp_offset = tr.tss16 ? 2U : 4U;
        const unsigned sp_width = tr.tss16 ? 2U : 4U;
        const auto ss_offset = sp_offset + sp_width;
        if (tr.limit < ss_offset+1)
          fail("TSS does not contain the requested protected-mode stack", FaultCode::ProtectionFault);
        const auto new_sp = read_linear(tr.base+sp_offset,sp_width);
        const auto ss0 = std::uint16_t(read_linear(tr.base+ss_offset,2));
        const auto ss_cache = descriptor(ss0);
        if (!ss_cache.valid || ss_cache.code || !ss_cache.writable ||
            ss_cache.dpl != target_cpl || (ss0 & 3U) != target_cpl)
          fail("TSS stack selector must be writable data for the handler CPL", FaultCode::ProtectionFault);
        segments[SS] = ss0;
        cache_[SS] = ss_cache;
        put_reg(ESP,4,new_sp);
      }
    }
    // The destination stack uses the handler's address size and privilege.
    // For a compatibility-mode source, switch CS before pushing on the
    // 64-bit stack so the pushes do not use that source's 16/32-bit SS cache.
    segments[CS] = std::uint16_t((selector & ~3U) | target_cpl);
    cache_[CS] = target_cache;
    cache_[CS].selector = segments[CS];
    if (long_gate) {
      regs[ESP] &= ~std::uint64_t{15};
      // 64-bit gate pushes SS:RSP even for a same-CPL interrupt.
      push(original_ss,8); push(original_rsp,8);
    } else if (switch_cpl) {
      push(original_ss,width); push(original_rsp,width);
    }
    push(original_flags,width);push(original_cs,width);push(resume_ip,width);
    if (has_error) push(error,width);
    jump(target_ip,width);
    if (type == 6 || type == 0xe) eflags &= ~IF;
    eflags &= ~(TF | 0x34000U); // Clear NT/RF/VM after saving their image.
  }

  void string_operation(std::uint8_t op);
  void shift(const ModRM& m, unsigned bytes, unsigned count);
  void double_shift(const ModRM& m, unsigned bytes, unsigned count, bool left);
  void group3(const ModRM& m, unsigned bytes);
  void group3_wide(unsigned operation, std::uint64_t operand);
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
  result.selector = selector;
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
    reload_real_segment(segment, selector);
    return;
  }
  if ((selector & 0xfffcU) == 0) {
    if (segment == SS) {
      const auto privilege = cpl();
      if (!is_long_mode() || privilege == 3 || (selector & 3U) != privilege)
        fail("null SS requires 64-bit mode, CPL below 3, and matching RPL", FaultCode::ProtectionFault);
      segments[SS] = selector;
      cache_[SS] = null_stack_cache(selector);
      return;
    }
    segments[segment] = selector; cache_[segment].selector = selector; cache_[segment].valid = false;
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
    jump(target, bytes);
    reload_real_segment(CS, selector);
    return;
  }
  const auto value = descriptor(selector);
  if (!value.code) fail("far transfer target is not a code segment", FaultCode::ProtectionFault);
  if ((selector & 3U) > cpl() ||
      (value.conforming ? value.dpl > cpl() : value.dpl != cpl()))
    fail("far control transfer privilege check failed", FaultCode::ProtectionFault);
  segments[CS] = std::uint16_t((selector & ~3U) | cpl()); cache_[CS] = value;
  cache_[CS].selector = segments[CS];
  jump(target, value.long_code ? 8 : bytes);
}

inline void Cpu::write_cr0(std::uint64_t value) {
  constexpr std::uint32_t allowed = 0xe005003fU;
  if (value & ~std::uint64_t(allowed)) fail("reserved CR0 bits", FaultCode::ProtectionFault);
  if ((value & 0x20000000U) && !(value & 0x40000000U))
    fail("CR0.NW requires CR0.CD", FaultCode::ProtectionFault);
  if ((value & 0x80000000U) && !(value & 1))
    fail("CR0.PG requires CR0.PE", FaultCode::ProtectionFault);
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
      cache_[s].base = real_segment_base(s);
      cache_[s].selector = segments[s];
      cache_[s].inherited_from_real_mode = true;
    }
  }
  // Changing PE does not reload the visible selectors or descriptor caches.
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
  constexpr std::uint32_t supported = 0x000006b0U | kCr4Tsd; // OSFXSR and OSXMMEXCPT, no OSXSAVE
  if (value & ~std::uint64_t(supported))
    fail("CR4 requests an unimplemented feature (e.g. SIMD, PCID, LA57, SMEP or SMAP)", FaultCode::UnsupportedState);
  cr4 = std::uint32_t(value);
}

inline std::uint64_t Cpu::read_msr(std::uint32_t number) {
  if (number == kTscMsr) return timestamp_counter();
  if (number == 0xc0000080U) return efer;
  if (number == 0xc0000100U) return fs_base;
  if (number == 0xc0000101U) return gs_base;
  std::ostringstream reason;
  reason << "RDMSR register is not implemented (ECX=0x" << std::hex << number << ')';
  fail(reason.str(), FaultCode::UnsupportedState);
}

inline void Cpu::write_msr(std::uint32_t number, std::uint64_t value) {
  if (number == kTscMsr) {
    tsc_offset_ = value - instructions * kTscTicksPerStep;
    return;
  }
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
  std::ostringstream reason;
  reason << "WRMSR register is not implemented (ECX=0x" << std::hex << number << ')';
  fail(reason.str(), FaultCode::UnsupportedState);
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

inline unsigned Cpu::try_rep_batch(unsigned budget) {
  last_rep_batch_bytes_ = 0;
  if (budget < 2 || !rep_batch_candidate() || fault || halted || transaction_ ||
      interrupt_shadow_ || (eflags & (TF | 0x20000U)) || (cr0 & 0x80000000U) ||
      (cpl() != 0 && cpl() != 3) || step_trace_hook || irq_trace_hook || lock_prefix_)
    return 0;

  // Only use decoded state from a matching scalar iteration. The guest can
  // mutate its own instructions, even through an A20 alias.
  const bool long_mode = is_long_mode();
  const bool default32 = long_mode || cache_[CS].big;
  const bool current_addr32 = long_mode ? address_override_ : cache_[CS].big != address_override_;
  if (op32_ != (default32 != operand_override_) || addr32_ != current_addr32 || (rex_ && !long_mode))
    return 0;

  const auto remaining = index(ECX);
  if (remaining < 2) return 0;
  const bool backward = flag(DF);
  const bool copying = (opcode_ & 0xfeU) == 0xa4;
  const unsigned width_bytes = opcode_ & 1U ? width() : 1;
  const auto src = index(ESI), dst = index(EDI);
  const auto index_limit = mask(address_width());
  const auto page_bytes = MemoryFabric::kPageSize;
  auto elements = unsigned(std::min<std::uint64_t>(budget, remaining));

  try {
    const auto length = unsigned(decoded_.size());
    const auto linear_code = segment_linear(CS, eip, length, Access::Execute);
    if (!long_mode && length - 1 > UINT32_MAX - linear_code) return 0;
    if (!bulk_ram_range(linear_code, length)) return 0;
    const auto code = bus_address(linear_code);
    std::array<std::uint8_t, 15> observed;
    memory.read_block(code, observed.data(), length);
    if (!std::equal(decoded_.begin(), decoded_.end(), observed.begin())) return 0;

    const auto limit_from = [&](unsigned seg, std::uint64_t offset, Access access) {
      const auto linear = segment_linear(seg, offset, width_bytes, access);
      const auto within_page = bus_address(linear) % page_bytes;
      if (width_bytes > page_bytes - within_page || width_bytes - 1 > index_limit - offset)
        return std::uint64_t{0};
      const auto by_page = backward ? 1 + within_page / width_bytes :
                                     (page_bytes - within_page) / width_bytes;
      const auto extra_by_index = backward ? offset / width_bytes :
          (index_limit - offset - (width_bytes - 1)) / width_bytes;
      return 1 + std::min<std::uint64_t>(by_page - 1, extra_by_index);
    };
    elements = unsigned(std::min<std::uint64_t>(elements, limit_from(ES, dst, Access::Write)));
    if (copying)
      elements = unsigned(std::min<std::uint64_t>(elements, limit_from(data_segment(), src, Access::Read)));
    if (elements < 2) return 0;

    const auto bytes = elements * width_bytes;
    const auto back = backward ? bytes - width_bytes : 0;
    const auto dst_linear = segment_linear(ES, dst - back, bytes, Access::Write);
    if (!bulk_ram_range(dst_linear, bytes)) return 0;
    const auto target = bus_address(dst_linear);
    const auto overlaps = [](std::uint64_t first, unsigned first_bytes,
                             std::uint64_t second, unsigned second_bytes) {
      return first < second + second_bytes && second < first + first_bytes;
    };
    if (overlaps(target, bytes, code, length)) return 0;

    const auto within_page = target % page_bytes;
    const bool completes_page = backward ? within_page == 0 : within_page + bytes == page_bytes;
    if (copying) {
      const auto src_linear = segment_linear(data_segment(), src - back, bytes, Access::Read);
      if (!bulk_ram_range(src_linear, bytes)) return 0;
      const auto source = bus_address(src_linear);
      // REP has sequential copy behavior when the ranges overlap, unlike a
      // single memcpy/memmove snapshot of the source.
      if (overlaps(target, bytes, source, bytes)) return 0;
      mundo_x::ether_next::copy_ram_block(memory, source, target, bytes);
      put_index(ESI, src + (backward ? -std::uint64_t(bytes) : bytes));
    } else {
      mundo_x::ether_next::repeat_ram_value(memory, target, get_reg(EAX, width_bytes), width_bytes, bytes);
    }
    if (completes_page) memory.compact_repeating_page(target);
    put_index(EDI, dst + (backward ? -std::uint64_t(bytes) : bytes));
    put_index(ECX, remaining - elements);
    if (remaining == elements) eip = cursor_ & code_mask();
    eflags |= 2;
    instructions += elements;
    last_rep_batch_bytes_ = bytes;
    return elements;
  } catch (const std::exception&) {
    // Range checks, or the all-or-nothing preparation of one destination
    // page, can fail before altering any bytes. Let the scalar path commit
    // individually up to the precise failing element.
    return 0;
  }
}

#include "x86_shift_loop.inc"

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

inline void Cpu::group3_wide(unsigned operation, std::uint64_t operand) {
  const auto low = get_reg(EAX, 8), high = get_reg(EDX, 8);
  const auto sign = sign_bit(8);
  const bool is_signed = (operation & 1U) != 0;
  if (operation < 6) {
    auto product = wide_integer::multiply(low, operand);
    if (is_signed) {
      // Convert the unsigned high word to its two's-complement product.
      if (low & sign) product.high -= operand;
      if (operand & sign) product.high -= low;
    }
    const bool overflow = product.high != (is_signed && (product.low & sign) ? UINT64_MAX : 0);
    put_reg(EAX, 8, product.low); put_reg(EDX, 8, product.high);
    set_flag(CF, overflow); set_flag(OF, overflow);
    return;
  }
  if (!operand) fail("integer divide by zero", FaultCode::DivideError);
  const bool negative_dividend = is_signed && (high & sign);
  const bool negative_divisor = is_signed && (operand & sign);
  const bool negative_quotient = negative_dividend != negative_divisor;
  const wide_integer::DoubleWord dividend{high, low};
  const auto magnitude = negative_dividend ? wide_integer::negate(dividend) : dividend;
  const auto divisor = negative_divisor ? std::uint64_t{0} - operand : operand;
  if (magnitude.high >= divisor) fail("integer quotient overflow", FaultCode::DivideError);
  const auto result = wide_integer::divide(magnitude, divisor);
  if (is_signed && result.quotient > (negative_quotient ? sign : sign - 1))
    fail("signed integer quotient overflow", FaultCode::DivideError);
  put_reg(EAX, 8, negative_quotient ? std::uint64_t{0} - result.quotient : result.quotient);
  put_reg(EDX, 8, negative_dividend ? std::uint64_t{0} - result.remainder : result.remainder);
}

inline void Cpu::group3(const ModRM& m, unsigned bytes) {
  require_lock_destination(m.rm, m.group == 2 || m.group == 3);
  if (m.group == 2 || m.group == 3) {
    modify_operand(m.rm, bytes, [&](std::uint64_t old) {
      return m.group == 2 ? ~old : alu(5, 0, old, bytes);
    });
    return;
  }
  if (m.group == 0) {
    const auto value = arithmetic_immediate(bytes); // RIP base follows the complete instruction.
    (void)alu(4, get(m.rm, bytes), value, bytes); return;
  }
  const auto operand = get(m.rm, bytes);
  if (bytes == 8 && m.group >= 4) { group3_wide(m.group, operand); return; }
  const auto bits = bytes * 8U;
  const auto maximum = mask(bytes);
  switch (m.group) {
    case 1: fail("reserved group-3 encoding", FaultCode::InvalidInstruction);
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
  if (lock_prefix_) {
    switch (op) {
      case 0xab: case 0xb3: case 0xbb: case 0xba:
      case 0xb0: case 0xb1: case 0xc0: case 0xc1: case 0xc7:
        break;
      default: fail("LOCK prefix is invalid for this opcode", FaultCode::InvalidInstruction);
    }
  }
  if (try_mmx(op)) return;
  if (op >= 0x40 && op <= 0x4f) {  // CMOVcc
    const auto m = modrm();
    const auto source = get(m.rm, bytes); // A false condition does not suppress source faults.
    if (condition(op & 15)) put_reg(m.reg, bytes, source);
    else if (bytes == 4 && (efer & 0x400U)) put_reg(m.reg, bytes, get_reg(m.reg, bytes));
    return;
  }
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
    sse_float(op);return;
  }
  if(op==0x2a||op==0x2c||op==0x2d||op==0x2e||op==0x2f) {
    sse2_scalar_misc(op);return;
  }
  if((op==0x10||op==0x11)&&repeat_) { // MOVSS/MOVSD xmm/mem
    if(operand_override_)fail("conflicting scalar MOV prefixes",FaultCode::InvalidInstruction);
    require_sse_state();
    const auto m=modrm();require_xmm(m.reg);
    const unsigned n=repeat_==0xf3?4:8;
    if(op==0x10) {
      if(m.rm.is_register) {
        require_xmm(m.rm.index);
        packed_set(xmm[m.reg],0,n,packed_lane(xmm[m.rm.index],0,n));
      }else {
        const auto bits=get(m.rm,n);
        xmm[m.reg].fill(0);
        packed_set(xmm[m.reg],0,n,bits);
      }
    }else {
      const auto bits=packed_lane(xmm[m.reg],0,n);
      if(m.rm.is_register){require_xmm(m.rm.index);packed_set(xmm[m.rm.index],0,n,bits);}
      else put(m.rm,n,bits);
    }
    return;
  }
  switch (op) {
    case 0x5a: sse2_convert_precision();return;
    case 0x5b: sse2_convert_dword();return;
    case 0x52: case 0x53: sse_approximate(op);return;
    case 0xe6: // CVTDQ2PD, CVTPD2DQ, CVTTPD2DQ
      sse2_convert_packed();return;
    case 0x54: case 0x55: case 0x56: case 0x57: { // AND/ANDN/OR/XOR PS and PD
      if(repeat_)fail("invalid packed logical SSE prefix",FaultCode::InvalidInstruction);
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
    case 0x50: { // MOVMSKPS/MOVMSKPD r32, xmm
      if(repeat_)fail("invalid MOVMSK prefix",FaultCode::InvalidInstruction);
      require_sse_state();
      const auto m=modrm();
      if(!m.rm.is_register)fail("MOVMSK source must be XMM",FaultCode::InvalidInstruction);
      require_xmm(m.rm.index);
      const unsigned n=operand_override_?8:4;
      unsigned bits=0;
      for(unsigned i=0;i<16/n;++i)bits|=unsigned(xmm[m.rm.index][(i+1)*n-1]>>7)<<i;
      put_reg(m.reg,4,bits);
      return;
    }
    case 0xc2: { // CMPPS/CMPSS/CMPPD/CMPSD imm8
      sse_cmp_vector();
      return;
    }
    case 0xc6: { // SHUFPS/SHUFPD xmm, xmm/m128, imm8
      if(repeat_)fail("invalid SHUFPS/SHUFPD prefix",FaultCode::InvalidInstruction);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const auto order=unsigned(immediate(1));
      const auto src=read_xmm_operand(m.rm,true);
      const auto dst=xmm[m.reg];
      const unsigned n=operand_override_?8:4, bits=operand_override_?1:2;
      for(unsigned i=0;i<16/n;++i) {
        const auto lane=(order>>(bits*i))&((1U<<bits)-1);
        packed_set(xmm[m.reg],i*n,n,packed_lane(i*n<8?dst:src,lane*n,n));
      }
      return;
    }
    case 0x14: case 0x15: { // UNPCKL/UNPCKH PS and PD
      if(repeat_)fail("invalid UNPCK prefix",FaultCode::InvalidInstruction);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const auto dst=xmm[m.reg],src=read_xmm_operand(m.rm,true);
      const unsigned n=operand_override_?8:4,start=op==0x15?8:0;
      for(unsigned offset=0;offset<8;offset+=n) {
        packed_set(xmm[m.reg],2*offset,n,packed_lane(dst,start+offset,n));
        packed_set(xmm[m.reg],2*offset+n,n,packed_lane(src,start+offset,n));
      }
      return;
    }
    case 0x12: case 0x13: case 0x16: case 0x17: { // MOVL/MOVH PS and PD
      if(repeat_)fail("unsupported MOVL/MOVH prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      const unsigned at=(op==0x16||op==0x17)?8U:0U;
      if(m.rm.is_register) {
        if(operand_override_||op==0x13||op==0x17)
          fail("MOVL/MOVH requires a memory operand",FaultCode::InvalidInstruction);
        require_xmm(m.rm.index);
        packed_set(xmm[m.reg],at,8,packed_lane(xmm[m.rm.index],8-at,8));
      }else if(op==0x12||op==0x16)
        packed_set(xmm[m.reg],at,8,get(m.rm,8));
      else put(m.rm,8,packed_lane(xmm[m.reg],at,8));
      return;
    }
    case 0xdb: case 0xdf: case 0xeb: // PAND/PANDN/POR
    case 0x60: case 0x61: case 0x62: case 0x6c: // PUNPCKL
    case 0x68: case 0x69: case 0x6a: case 0x6d: // PUNPCKH
    case 0x63: case 0x67: case 0x6b: // PACKSSWB, PACKUSWB, PACKSSDW
    case 0x74: case 0x75: case 0x76: // PCMPEQ
    case 0x64: case 0x65: case 0x66: // PCMPGT
    case 0xfc: case 0xfd: case 0xfe: case 0xd4: // PADD
    case 0xf8: case 0xf9: case 0xfa: case 0xfb: // PSUB
    case 0xd8: case 0xd9: case 0xdc: case 0xdd: // Unsigned saturating PSUB/PADD
    case 0xe8: case 0xe9: case 0xec: case 0xed: // Signed saturating PSUB/PADD
    case 0xd5: case 0xe4: case 0xe5: case 0xf4: case 0xf5: // Packed multiplication
    case 0xda: case 0xde: case 0xea: case 0xee: // PMIN/PMAX
    case 0xe0: case 0xe3: case 0xf6: // PAVG/PSADBW
    case 0xd1: case 0xd2: case 0xd3: // PSRL
    case 0xe1: case 0xe2: // PSRA
    case 0xf1: case 0xf2: case 0xf3: // PSLL
      sse_integer(op); return;
    case 0x71: case 0x72: case 0x73:
      sse_integer_shift_immediate(op);
      return;
    case 0x70: { // PSHUFD, PSHUFLW, PSHUFHW xmm, xmm/m128, imm8.
      if(operand_override_ == (repeat_ != 0))
        fail("packed integer shuffle requires 66, F2 or F3",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();require_xmm(m.reg);
      // Consume the immediate BEFORE resolving RIP-relative memory operands.
      const auto order=unsigned(immediate(1));
      const auto source=read_xmm_operand(m.rm,true);
      const unsigned bytes=operand_override_?4U:2U;
      const unsigned start=repeat_==0xf3?8U:0U;
      auto result=source;
      for(unsigned lane=0;lane<4;++lane)
        packed_set(result,start+lane*bytes,bytes,
                   packed_lane(source,start+((order>>(lane*2))&3U)*bytes,bytes));
      xmm[m.reg]=result;
      return;
    }
    case 0xc4: case 0xc5: { // PINSRW / PEXTRW.
      if (!operand_override_ || repeat_)
        fail("PINSRW/PEXTRW XMM form requires 66", FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m = modrm();
      const auto lane = unsigned(immediate(1)) & 7U;
      if (op == 0xc4) {
        require_xmm(m.reg);
        packed_set(xmm[m.reg], lane * 2, 2, get(m.rm, 2));
      } else {
        if (!m.rm.is_register)
          fail("PEXTRW source must be an XMM register", FaultCode::InvalidInstruction);
        require_xmm(m.rm.index);
        put_reg(m.reg, 4, packed_lane(xmm[m.rm.index], lane * 2, 2));
      }
      return;
    }
    case 0xe7: { // MOVNTDQ m128, xmm. Cached writes share the same RAM path.
      if (!operand_override_ || repeat_)
        fail("MOVNTDQ requires 66", FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m = modrm();
      require_memory(m.rm);
      require_xmm(m.reg);
      write_xmm_operand(m.rm, xmm[m.reg], true);
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
    case 0x2b: { // MOVNTPS/MOVNTPD xmm to aligned memory
      if(repeat_)fail("invalid MOVNT prefix",FaultCode::InvalidInstruction);
      require_sse_state();
      const auto m=modrm();require_memory(m.rm);require_xmm(m.reg);
      write_xmm_operand(m.rm,xmm[m.reg],true);
      return;
    }
    case 0xc3: { // MOVNTI uses GPRs, without requiring OSFXSR/cleared TS.
      if(operand_override_||repeat_)
        fail("invalid MOVNTI prefix",FaultCode::InvalidInstruction);
      const auto m=modrm();require_memory(m.rm);
      const unsigned amount=is_long_mode()&&(rex_&8U)?8U:4U;
      put(m.rm,amount,get_reg(m.reg,amount));
      return;
    }
    case 0xf7: { // MASKMOVDQU xmm data, xmm mask, implicit DS:(R)DI.
      if(!operand_override_||repeat_)
        fail("MASKMOVDQU requires 66 prefix",FaultCode::UnsupportedOpcode);
      require_sse_state();
      const auto m=modrm();
      if(!m.rm.is_register)fail("MASKMOVDQU mask must be XMM",FaultCode::InvalidInstruction);
      require_xmm(m.reg);require_xmm(m.rm.index);
      masked_store(xmm[m.reg],xmm[m.rm.index],16);
      return;
    }
    case 0x10: case 0x11: case 0x28: case 0x29: { // MOVU/MOVA PS and PD
      if(repeat_)fail("invalid packed MOV prefix",FaultCode::InvalidInstruction);
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
    case 0xae: { // FX state, MXCSR, fences and CLFLUSH
      if(operand_override_||repeat_)
        fail("unsupported 0F AE mandatory prefix",FaultCode::InvalidInstruction);
      const auto m=modrm();
      if(m.rm.is_register&&m.group>=5)
        return; // The single-vCPU memory bus executes accesses in order.
      if(!m.rm.is_register&&m.group==7) {
        const auto linear_address=segment_linear(m.rm.segment,operand_offset(m.rm),1,Access::Read);
        // CLFLUSH checks read permissions even though this CPU has no caches to flush.
        (void)translate(linear_address,Access::Read);
        return;
      }
      require_memory(m.rm);
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
    case 0x18: { // PREFETCHNTA/T0/T1/T2, advisory memory hints.
      const auto m=modrm();require_memory(m.rm);
      if(m.group>3)fail("unsupported PREFETCH encoding",FaultCode::UnsupportedOpcode);
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
    case 0xa3: case 0xab: case 0xb3: case 0xbb: case 0xba: { // BT, BTS, BTR, BTC.
      auto m = modrm();
      const unsigned operation = op == 0xba ? m.group : 4U + ((op - 0xa3U) >> 3);
      if (operation < 4) fail("reserved bit-test encoding", FaultCode::InvalidInstruction);
      const auto index = op == 0xba ? immediate(1) : get_reg(m.reg, bytes);
      const unsigned bits = 8U * bytes, bit = unsigned(index & (bits - 1));
      require_lock_destination(m.rm, operation != 4);
      if (op != 0xba && !m.rm.is_register) {
        // Register indices name a signed bit position in a memory bit string.
        const auto element = (signed_value(index, bytes) - bit) / bits;
        m.rm.offset += std::uint64_t(element) * bytes;
      }
      const auto bit_mask = std::uint64_t{1} << bit;
      if (operation == 4) set_flag(CF, (get(m.rm, bytes) & bit_mask) != 0);
      else modify_operand(m.rm, bytes, [&](std::uint64_t old) {
        set_flag(CF, (old & bit_mask) != 0);
        if (operation == 5) return old | bit_mask;
        if (operation == 6) return old & ~bit_mask;
        return old ^ bit_mask;
      });
      return;
    }
    case 0xbc: case 0xbd: { // BSF, BSR.
      // Without BMI1 or ABM, an F3 prefix also decodes as BSF or BSR.
      const auto m = modrm();
      auto source = get(m.rm, bytes);
      set_flag(ZF, source == 0);
      // The architecture leaves the zero-input destination undefined.
      if (source == 0) return;
      unsigned index = 0;
      if (op == 0xbc) {
        while ((source & 1) == 0) {
          source >>= 1;
          ++index;
        }
      } else {
        while (source > 1) {
          source >>= 1;
          ++index;
        }
      }
      put_reg(m.reg, bytes, index);
      return;
    }
    case 0xb0: case 0xb1: { // CMPXCHG r/m, reg.
      const auto m = modrm();
      const unsigned amount = op == 0xb0 ? 1 : bytes;
      require_lock_destination(m.rm);
      const auto accumulator = get_reg(EAX, amount);
      const auto source = get_reg(m.reg, amount);
      if (m.rm.is_register) {
        const auto old = get(m.rm, amount);
        (void)alu(7, accumulator, old, amount);
        if (flag(ZF)) put(m.rm, amount, source);
        else put_reg(EAX, amount, old);
      } else {
        modify_operand(m.rm, amount, [&](std::uint64_t old) {
          (void)alu(7, accumulator, old, amount);
          if (flag(ZF)) return source;
          put_reg(EAX, amount, old);
          return old;
        }, true); // A failed comparison still requires a write-capable operand.
      }
      return;
    }
    case 0xc0: case 0xc1: { // XADD r/m, reg.
      const auto m = modrm();
      const unsigned amount = op == 0xc0 ? 1 : bytes;
      require_lock_destination(m.rm);
      const auto source = get_reg(m.reg, amount);
      modify_operand(m.rm, amount, [&](std::uint64_t old) {
        put_reg(m.reg, amount, old);
        return alu(0, old, source, amount);
      });
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
      const auto indices=atomic_ram(address,amount);
      const unsigned half=amount/2;
      const auto low=read_atomic_ram(indices,0,half);
      const auto high=read_atomic_ram(indices,half,half);
      const auto rax=get_reg(EAX,half);
      const auto rdx=get_reg(EDX,half);
      const bool equal=low==rax && high==rdx;
      if(equal) {
        write_atomic_ram(indices,0,half,get_reg(EBX,half));
        write_atomic_ram(indices,half,half,get_reg(ECX,half));
      }else{
        put_reg(EAX,half,low);
        put_reg(EDX,half,high);
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
      if (m.group == 3 && (efer & 0x400U) && type == 1u)
        fail("IA-32e LTR requires a 64-bit TSS descriptor", FaultCode::ProtectionFault);

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
      const SystemRegister next{selector, base, std::uint32_t(limit), true, type == 1u};
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
      if (m.group == 7) { // INVLPG m
        if (is_long_mode() && m.rm.is_register && (m.rm.index & 7U) == EAX)
          fail("SWAPGS is not implemented", FaultCode::UnsupportedOpcode);
        require_memory(m.rm);
        if (cpl() != 0) fail("INVLPG requires CPL0", FaultCode::ProtectionFault);
        // The operand supplies an address without accessing the segment or
        // the target page. There is no cached translation to invalidate.
        // A noncanonical address in 64-bit mode also has no effect.
        return;
      }
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
    case 0x31: {
      if ((cr4 & kCr4Tsd) && cpl() != 0)
        fail("CR4.TSD restricts RDTSC to CPL0", FaultCode::ProtectionFault);
      const auto ticks = timestamp_counter();
      put_reg(EAX, 4, ticks); put_reg(EDX, 4, ticks >> 32);
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
        // TSC, MSR, PAE, CX8, CMOV, MMX, FXSR, SSE and SSE2.
        // The full x87 instruction set remains incomplete (FPU bit 0).
        edx=(1U<<4)|(1U<<5)|(1U<<6)|(1U<<8)|(1U<<15)|(1U<<23)|(1U<<24)|(1U<<25)|(1U<<26);
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
  if (lock_prefix_ && !(op <= 0x31 && (op & 7U) <= 1)) {
    switch (op) {
      case 0x0f: case 0x80: case 0x81: case 0x83: case 0x86: case 0x87:
      case 0xf6: case 0xf7: case 0xfe: case 0xff:
        break;
      default: fail("LOCK prefix is invalid for this opcode", FaultCode::InvalidInstruction);
    }
  }
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
      require_lock_destination(m.rm);
      if (form & 2U) {
        const auto result = alu(operation, get_reg(m.reg, w), get(m.rm, w), w);
        if (operation != 7) put_reg(m.reg, w, result);
      } else if (operation == 7) {
        (void)alu(operation, get(m.rm, w), get_reg(m.reg, w), w);
      } else {
        modify_operand(m.rm, w, [&](std::uint64_t old) {
          return alu(operation, old, get_reg(m.reg, w), w);
        });
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
    case 0x17:
      load_segment(SS, std::uint16_t(pop(bytes)));
      interrupt_shadow_=true;
      return;
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
      require_lock_destination(m.rm, m.group != 7);
      if (m.group == 7) (void)alu(m.group, get(m.rm, w), value, w);
      else modify_operand(m.rm, w, [&](std::uint64_t old) { return alu(m.group, old, value, w); });
      return;
    }
    case 0x84: case 0x85: {
      const auto m = modrm(); const unsigned w = op == 0x84 ? 1 : bytes;
      (void)alu(4, get(m.rm, w), get_reg(m.reg, w), w); return;
    }
    case 0x86: case 0x87: {
      const auto m = modrm(); const unsigned w = op == 0x86 ? 1 : bytes;
      require_lock_destination(m.rm);
      const auto source = get_reg(m.reg, w);
      std::uint64_t old = 0;
      modify_operand(m.rm, w, [&](std::uint64_t value) { old = value; return source; }, true);
      put_reg(m.reg, w, old);
      return;
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
      else {
        load_segment(m.group, std::uint16_t(get(m.rm, 2)));
        if(m.group==SS)interrupt_shadow_=true;
      }
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
      require_no_pending_x87();
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
      if (long_return && operand_override_)
        fail("16-bit IRET in long mode is not implemented", FaultCode::UnsupportedState);
      const unsigned n = long_return ? 8U : bytes;
      const auto original_cpl = cpl();
      const auto target = pop(n), raw_cs = pop(n), flags = pop(n);
      if (raw_cs > 0xffffU || (flags & 0x20000U))
        fail("IRET invalid selector/virtual-8086 transition", FaultCode::ProtectionFault);
      const auto target_cpl = unsigned(raw_cs & 3U);
      if (target_cpl < original_cpl || (target_cpl != 0 && target_cpl != 3))
        fail("IRET cannot elevate privilege", FaultCode::ProtectionFault);
      if ((eflags & 0x4000U) != 0)
        fail("nested-task IRET is not implemented", FaultCode::UnsupportedState);
      const auto code = descriptor(std::uint16_t(raw_cs));
      if (!code.code || (code.conforming ? code.dpl > target_cpl : code.dpl != target_cpl) ||
          (code.long_code && (!(efer & 0x400U) || code.big)))
        fail("IRET code selector DPL/RPL/type mismatch", FaultCode::ProtectionFault);
      const bool outer = target_cpl > original_cpl;
      std::uint64_t saved_rsp = 0, saved_ss = 0;
      SegmentCache saved_stack{};
      if (long_return || outer) {
        saved_rsp = pop(n);
        saved_ss = pop(n);
        if (saved_ss > 0xffffU || (long_return && !canonical(saved_rsp)))
          fail("IRET invalid SS/RSP", FaultCode::ProtectionFault);
        if (saved_ss == 0 && long_return && target_cpl == 0 && code.long_code) {
          saved_stack = null_stack_cache(std::uint16_t(saved_ss));
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
      restore_flags(std::uint32_t(flags), n);
      if (n >= 4) eflags = (eflags & ~0x10000U) | (std::uint32_t(flags) & 0x10000U);
      segments[CS] = std::uint16_t(raw_cs);
      cache_[CS] = code;
      if (long_return || outer) {
        segments[SS] = std::uint16_t(saved_ss);
        cache_[SS] = saved_stack;
        put_reg(ESP, long_return ? 8U : n, saved_rsp);
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
            cache_[seg].selector = 0;
            cache_[seg].valid = false;
          }
        }
      }
      jump(target, n);
      return;
    }
    case 0xd8: case 0xd9: case 0xda: case 0xdb:
    case 0xdc: case 0xdd: case 0xde: case 0xdf:
      execute_x87(op);
      return;
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
      interrupt_shadow_=!flag(IF);
      set_flag(IF, true); return;
    case 0xfc: set_flag(DF, false); return;
    case 0xfd: set_flag(DF, true); return;
    case 0xfe: case 0xff: {
      const auto m = modrm(); const unsigned w = op == 0xfe ? 1 : bytes;
      require_lock_destination(m.rm, m.group < 2);
      if (m.group < 2) {
        const bool old_cf = flag(CF);
        modify_operand(m.rm, w, [&](std::uint64_t old) { return alu(m.group == 0 ? 0 : 5, old, 1, w); });
        set_flag(CF, old_cf);
        return;
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
