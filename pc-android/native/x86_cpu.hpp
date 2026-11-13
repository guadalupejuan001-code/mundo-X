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
#include <algorithm>
#include <cstring>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "memory_fabric.hpp"

namespace cosmic_pc {

enum class FaultCode {
  MemoryBounds, SegmentLimit, InvalidInstruction, UnsupportedOpcode,
  UnsupportedState, DivideError, UnsupportedPort, HostError, ProtectionFault,
  PageFault, MemoryBudget, DeviceNotAvailable
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
  };

  // CPU-owned architectural x87 and XMM state. These are exact guest bits,
  // not host floating-point SIMD registers or a second execution engine.
  struct ExtendedState {
    std::array<std::array<std::uint8_t,16>,16> xmm{};
    std::array<std::array<std::uint8_t,10>,8> st{};
    std::uint32_t mxcsr=0x1f80;
    std::uint16_t fcw=0x037f,fsw=0,fop=0;
    std::uint8_t ftw=0; // abridged FTW: physical register nonempty mask
  };
  ExtendedState fx{};
  std::array<std::uint64_t, 16> regs{};
  std::array<std::uint16_t, 6> segments{};
  std::uint64_t eip = 0x7c00, cr2 = 0, cr3 = 0, efer = 0, fs_base = 0, gs_base = 0;
  std::uint32_t eflags = 2, cr0 = 0x10, cr4 = 0;
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
    regs.fill(0); segments.fill(0); segments[CS] = cs;
    fx=ExtendedState{};
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
  unsigned cpl() const { return is_real_mode() ? 0U : unsigned(segments[CS] & 3U); }
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
                          cr2, cr3, efer, fs_base, gs_base, gdtr, idtr, ldtr, tr, a20_enabled, halted, fx};
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
                          cr2,cr3,efer,fs_base,gs_base,gdtr,idtr,ldtr,tr,a20_enabled,halted,fx};
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
    ExtendedState fx;
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

  // Internal architectural state helpers. No host SSE, no Qiskit, no second
  // CPU/FPU. 80-bit values are preserved as EXACT 10-byte guest encodings.
  static constexpr std::uint32_t kMxcsrMask=0x0000ffbfU;
  void require_x87() const {
    if(cr0 & 0x4U) fail("CR0.EM requests x87 emulation",FaultCode::DeviceNotAvailable);
    if(cr0 & 0x8U) fail("CR0.TS blocks x87 instructions",FaultCode::DeviceNotAvailable);
  }
  void require_xmm() const {
    if(!(cr4 & 0x200U) || (cr0 & 0x4U))
      fail("SSE register operations require CR4.OSFXSR and CR0.EM=0",
           FaultCode::InvalidInstruction);
    if(cr0 & 0x8U)
      fail("CR0.TS prevents SIMD state use",FaultCode::DeviceNotAvailable);
  }
  unsigned fp_top() const {return (fx.fsw>>11)&7U;}
  void fp_top_set(unsigned top) {
    fx.fsw=std::uint16_t((fx.fsw&~0x3800U)|((top&7U)<<11));
  }
  void fp_push(std::array<std::uint8_t,10> value) {
    const auto next=(fp_top()+7U)&7U;
    if(fx.ftw & (1U<<next))
      fail("x87 stack overflow requires unmodeled exception status",
           FaultCode::UnsupportedState);
    fx.st[next]=value;
    fx.ftw|=std::uint8_t(1U<<next);
    fp_top_set(next);
  }
  std::array<std::uint8_t,10> fp_pop() {
    const auto top=fp_top();
    if(!(fx.ftw&(1U<<top)))
      fail("x87 stack underflow requires unmodeled exception status",
           FaultCode::UnsupportedState);
    const auto data=fx.st[top];
    fx.ftw&=std::uint8_t(~(1U<<top));
    fp_top_set((top+1U)&7U);
    return data;
  }
  static std::array<std::uint8_t,10> fp_one() {
    std::array<std::uint8_t,10> b{};
    b[7]=0x80;b[8]=0xff;b[9]=0x3f;
    return b;
  }
  bool execute_x87(std::uint8_t opcode) {
    if(opcode<0xd8||opcode>0xdf)return false;
    require_x87();
    const auto modrm_byte=fetch8();
    if(opcode==0xdb && modrm_byte==0xe3) { // FNINIT, no WAIT prefix
      fx.fcw=0x037f;fx.fsw=0;fx.ftw=0;fx.fop=0;
      fx.st={};return true;
    }
    if(opcode==0xdb && modrm_byte==0xe2) { // FNCLEX
      fx.fsw&=std::uint16_t(~0x80ffU);return true;
    }
    if(opcode==0xdf && modrm_byte==0xe0) { // FNSTSW AX
      set_reg16(EAX,fx.fsw);return true;
    }
    if(opcode==0xd9 && modrm_byte==0xe8) {fp_push(fp_one());return true;}
    if(opcode==0xd9 && modrm_byte==0xee) {fp_push({});return true;}
    if(opcode==0xd9 && modrm_byte==0xe0) { // FCHS ST(0)
      const auto top=fp_top();
      if(!(fx.ftw&(1U<<top)))fail("FCHS empty ST0",FaultCode::UnsupportedState);
      fx.st[top][9]^=0x80;return true;
    }
    if(opcode==0xd9 && modrm_byte==0xe1) { // FABS ST(0)
      const auto top=fp_top();
      if(!(fx.ftw&(1U<<top)))fail("FABS empty ST0",FaultCode::UnsupportedState);
      fx.st[top][9]&=0x7f;return true;
    }
    // Raw FLD ST(i) / FXCH ST(i) preserve ALL 80 bits.
    if(opcode==0xd9 && (modrm_byte&0xf8)==0xc0) {
      const auto at=(fp_top()+(modrm_byte&7U))&7U;
      if(!(fx.ftw&(1U<<at)))fail("FLD empty ST(i)",FaultCode::UnsupportedState);
      fp_push(fx.st[at]);return true;
    }
    if(opcode==0xd9 && (modrm_byte&0xf8)==0xc8) {
      const auto at=(fp_top()+(modrm_byte&7U))&7U;
      if(!(fx.ftw&(1U<<at))||!(fx.ftw&(1U<<fp_top())))
        fail("FXCH empty FPU slot",FaultCode::UnsupportedState);
      std::swap(fx.st[at],fx.st[fp_top()]);return true;
    }
    // Only forms with explicit, implemented architectural semantics are
    // decoded; arithmetic is not approximated by host double/long double.
    const auto group=(modrm_byte>>3)&7U;
    const auto mode=modrm_byte>>6;
    if(mode==3)fail("x87 opcode form not implemented",FaultCode::UnsupportedOpcode);
    // We have already fetched the ModRM byte: decode displacement now, using
    // the same canonical ModRM decoder by re-reading the byte from guest code.
    --cursor_;decoded_.pop_back();
    const auto m=modrm();
    if(opcode==0xd9 && (group==5||group==7)) { // FLDCW / FNSTCW
      const auto addr=segment_linear(m.rm.segment,operand_offset(m.rm),2,
                                  group==5?Access::Read:Access::Write);
      if(group==5) fx.fcw=std::uint16_t(read_linear(addr,2));
      else write_linear(addr,2,fx.fcw);
      return true;
    }
    if(opcode==0xdb && (group==5||group==7)) { // FLD/FSTP m80
      const auto addr=segment_linear(m.rm.segment,operand_offset(m.rm),10,
                                  group==5?Access::Read:Access::Write);
      if(group==5) {
        std::array<std::uint8_t,10> image{};
        for(unsigned i=0;i<10;++i)image[i]=std::uint8_t(read_linear(addr+i,1));
        fp_push(image);
      } else {
        if(!(fx.ftw&(1U<<fp_top())))
          fail("FSTP empty ST0",FaultCode::UnsupportedState);
        const auto value=fx.st[fp_top()];
        for(unsigned i=0;i<10;++i)write_linear(addr+i,1,value[i]);
        (void)fp_pop();
      }
      return true;
    }
    fail("x87 arithmetic or opcode is not yet implemented",FaultCode::UnsupportedOpcode);
  }
  bool execute_fxstate(std::uint8_t opcode) {
    if(opcode!=0xae)return false;
    const auto m=modrm();
    require_memory(m.rm);
    if(m.group>3)fail("0F AE group opcode not implemented",FaultCode::UnsupportedOpcode);
    require_x87(); // FXSAVE/FXRSTOR are blocked by TS and EM.
    const auto n=m.group<=1?512U:4U;
    const auto addr=segment_linear(m.rm.segment,operand_offset(m.rm),
                  n,m.group==0||m.group==3?Access::Write:Access::Read);
    if(m.group<=1 && (addr&15U))
      fail("FXSAVE/FXRSTOR require 16-byte alignment",FaultCode::ProtectionFault);
    if(m.group>=2 && !(cr4&0x200U))
      fail("LDMXCSR/STMXCSR require CR4.OSFXSR",FaultCode::InvalidInstruction);
    // Reject device MMIO for extended state operations. The CPU journal
    // reverses RAM writes, but cannot reverse device side effects.
    // One translation per guest 4-KiB page, NOT one page walk per byte.
    // This preserves exact physical bytes without materializing device RAM.
    for(unsigned position=0;position<n;) {
      const auto physical=translate(addr+position,
                      m.group==0||m.group==3?Access::Write:Access::Read);
      const unsigned chunk=std::min<unsigned>(n-position,
                           4096U-unsigned((addr+position)&4095U));
      for(unsigned j=0;j<chunk;++j) {
        if(mmio_at(physical+j))fail("FXSTATE on MMIO unsupported",
                                     FaultCode::UnsupportedState);
        (void)physical_index(physical+j);
      }
      position+=chunk;
    }
    if(m.group==2) { // LDMXCSR m32: GP on reserved bits, no mutation
      const auto incoming=std::uint32_t(read_linear(addr,4));
      if(incoming & ~kMxcsrMask)
        fail("LDMXCSR reserved bits",FaultCode::ProtectionFault);
      fx.mxcsr=incoming;return true;
    }
    if(m.group==3) {write_linear(addr,4,fx.mxcsr);return true;}
    if(m.group==0) {
      // FXSAVE m512: preserve architecturally reserved/software bytes,
      // only update defined fields. Does not store MMX (not advertised).
      std::array<std::uint8_t,512> data{};
      for(unsigned i=0;i<512;i+=8) {
        const auto word=read_linear(addr+i,8);
        for(unsigned j=0;j<8;++j)data[i+j]=std::uint8_t(word>>(8*j));
      }
      const auto store16=[&](unsigned off,std::uint16_t v) {
        data[off]=std::uint8_t(v);data[off+1]=std::uint8_t(v>>8);
      };
      const auto store32=[&](unsigned off,std::uint32_t v) {
        for(unsigned i=0;i<4;++i)data[off+i]=std::uint8_t(v>>(i*8));
      };
      store16(0,fx.fcw);store16(2,fx.fsw);
      data[4]=fx.ftw;
      store16(6,fx.fop);
      for(unsigned i=0;i<8;++i)
        std::copy(fx.st[i].begin(),fx.st[i].end(),data.begin()+32+i*16);
      if(cr4&0x200U) {
        store32(24,fx.mxcsr);store32(28,kMxcsrMask);
        for(unsigned i=0;i<(is_long_mode()?16U:8U);++i)
          std::copy(fx.xmm[i].begin(),fx.xmm[i].end(),data.begin()+160+i*16);
      }
      for(unsigned i=0;i<512;i+=8) {
        std::uint64_t word=0;
        for(unsigned j=0;j<8;++j)word|=std::uint64_t(data[i+j])<<(8*j);
        write_linear(addr+i,8,word);
      }
      return true;
    }
    // FXRSTOR: read & validate a complete 512-byte state before commit.
    std::array<std::uint8_t,512> bytes{};
    for(unsigned i=0;i<512;i+=8) {
      const auto word=read_linear(addr+i,8);
      for(unsigned j=0;j<8;++j)bytes[i+j]=std::uint8_t(word>>(8*j));
    }
    ExtendedState restored=fx;
    restored.fcw=std::uint16_t(bytes[0]|(unsigned(bytes[1])<<8));
    restored.fsw=std::uint16_t(bytes[2]|(unsigned(bytes[3])<<8));
    restored.ftw=bytes[4];restored.fop=std::uint16_t(bytes[6]|(unsigned(bytes[7])<<8));
    for(unsigned i=0;i<8;++i)
      std::copy_n(bytes.data()+32+i*16,10,restored.st[i].data());
    if(cr4&0x200U) {
      const auto mxcsr=std::uint32_t(bytes[24])|(std::uint32_t(bytes[25])<<8)|
                       (std::uint32_t(bytes[26])<<16)|(std::uint32_t(bytes[27])<<24);
      if(mxcsr & ~kMxcsrMask)
        fail("FXRSTOR MXCSR reserved bits",FaultCode::ProtectionFault);
      restored.mxcsr=mxcsr;
      for(unsigned i=0;i<(is_long_mode()?16U:8U);++i)
        std::copy_n(bytes.data()+160+i*16,16,restored.xmm[i].data());
    }
    fx=std::move(restored);
    return true;
  }

  using Xmm=std::array<std::uint8_t,16>;
  static std::uint64_t lane(const Xmm& x,unsigned offset,unsigned width) {
    std::uint64_t result=0;
    for(unsigned i=0;i<width;++i)
      result|=std::uint64_t(x.at(offset+i))<<(i*8);
    return result;
  }
  static void set_lane(Xmm& x,unsigned offset,unsigned width,std::uint64_t v) {
    for(unsigned i=0;i<width;++i)
      x.at(offset+i)=std::uint8_t(v>>(i*8));
  }
  Xmm read_xmm_operand(const Operand& o,bool aligned,unsigned bytes=16) {
    Xmm result{};
    if(o.is_register) {
      result=fx.xmm.at(o.index);
      return result;
    }
    const auto address=segment_linear(o.segment,operand_offset(o),bytes,Access::Read);
    if(aligned && (address&15U))
      fail("aligned 128-bit SIMD memory operand required",
           FaultCode::ProtectionFault);
    for(unsigned position=0;position<bytes;) {
      const unsigned count=std::min(8U,bytes-position);
      const auto word=read_linear(address+position,count);
      for(unsigned i=0;i<count;++i)
        result[position+i]=std::uint8_t(word>>(8*i));
      position+=count;
    }
    return result;
  }
  void write_xmm_operand(const Operand& o,const Xmm& value,
                         bool aligned,unsigned bytes=16) {
    if(o.is_register) {
      if(bytes==16)fx.xmm.at(o.index)=value;
      else std::copy_n(value.begin(),bytes,fx.xmm.at(o.index).begin());
      return;
    }
    const auto address=segment_linear(o.segment,operand_offset(o),bytes,Access::Write);
    if(aligned && (address&15U))
      fail("aligned 128-bit SIMD memory operand required",
           FaultCode::ProtectionFault);
    // Prevalidate all memory bytes before writing. The CPU journal will
    // restore memory and SIMD state if any subsequent page walk fails.
    for(unsigned position=0;position<bytes;) {
      const auto p=translate(address+position,Access::Write);
      const unsigned count=std::min<unsigned>(bytes-position,
                                    4096U-unsigned((address+position)&4095U));
      for(unsigned i=0;i<count;++i){
        if(mmio_at(p+i))
          fail("128-bit SIMD writes to device MMIO are unsupported",
               FaultCode::UnsupportedState);
        (void)physical_index(p+i);
      }
      position+=count;
    }
    for(unsigned position=0;position<bytes;){
      const unsigned count=std::min(8U,bytes-position);
      std::uint64_t word=0;
      for(unsigned i=0;i<count;++i)
        word|=std::uint64_t(value[position+i])<<(8*i);
      write_linear(address+position,count,word);
      position+=count;
    }
  }
  bool execute_sse_subset(unsigned op) {
    const bool p66=operand_override_ && !repeat_;
    const bool pf3=repeat_==0xf3 && !operand_override_;
    const bool pf2=repeat_==0xf2 && !operand_override_;
    const bool aligned=p66&&(op==0x6f||op==0x7f||op==0x28||op==0x29);
    const bool mov128=((p66||pf3)&&(op==0x6f||op==0x7f))||
                       (p66&&(op==0x10||op==0x11||op==0x28||op==0x29));
    const bool mov_scalar=(pf2||pf3)&&(op==0x10||op==0x11);
    const bool mov_gpr=p66&&(op==0x6e||op==0x7e);
    const bool packed=p66&&
      (op==0xef||op==0xdb||op==0xeb||op==0xdf||op==0xd4||
       op==0xfc||op==0xfd||op==0xfe||op==0xf8||op==0xf9||
       op==0xfa||op==0xfb||op==0x74||op==0x75||op==0x76||
       op==0x70||op==0x73);
    if(!mov128&&!mov_scalar&&!mov_gpr&&!packed)return false;
    require_xmm();
    const auto m=modrm();
    if(m.reg>=fx.xmm.size() || (m.rm.is_register&&m.rm.index>=fx.xmm.size()))
      fail("XMM register encoding outside 0..15",FaultCode::InvalidInstruction);
    if(mov_gpr) {
      const unsigned n=(is_long_mode()&&(rex_&8U))?8U:4U;
      if(op==0x6e) {
        Xmm out{};
        set_lane(out,0,n,get(m.rm,n));
        fx.xmm[m.reg]=out;
      }else {
        put(m.rm,n,lane(fx.xmm[m.reg],0,n));
      }
      return true;
    }
    if(mov_scalar) {
      const unsigned n=pf2?8U:4U;
      if(op==0x10) {
        const auto from=read_xmm_operand(m.rm,false,n);
        auto dest=fx.xmm[m.reg];
        if(!m.rm.is_register)dest={}; // scalar memory load clears upper XMM
        std::copy_n(from.begin(),n,dest.begin());
        fx.xmm[m.reg]=dest;
      }else {
        write_xmm_operand(m.rm,fx.xmm[m.reg],false,n);
      }
      return true;
    }
    if(mov128) {
      if(op==0x6f||op==0x10||op==0x28)
        fx.xmm[m.reg]=read_xmm_operand(m.rm,aligned);
      else
        write_xmm_operand(m.rm,fx.xmm[m.reg],aligned);
      return true;
    }
    if(op==0x73) {
      if(!m.rm.is_register)
        fail("PSLLDQ/PSRLDQ require XMM register destination",FaultCode::InvalidInstruction);
      const auto shift=std::min<unsigned>(fetch8(),16);
      Xmm output{},src=fx.xmm[m.rm.index];
      if(m.group==7) {
        for(unsigned i=shift;i<16;++i)output[i]=src[i-shift];
      }else if(m.group==3) {
        for(unsigned i=0;i+shift<16;++i)output[i]=src[i+shift];
      }else fail("SSE2 shift operation unsupported",FaultCode::UnsupportedOpcode);
      fx.xmm[m.rm.index]=output;
      return true;
    }
    const auto src=read_xmm_operand(m.rm,false);
    auto output=fx.xmm[m.reg];
    if(op==0x70) {
      const auto selector=fetch8();
      for(unsigned i=0;i<4;++i) {
        const auto from=((selector>>(i*2))&3U)*4U;
        for(unsigned j=0;j<4;++j)output[4*i+j]=src[from+j];
      }
      fx.xmm[m.reg]=output;return true;
    }
    if(op==0xef||op==0xdb||op==0xeb||op==0xdf) {
      for(unsigned i=0;i<16;++i) {
        switch(op) {
          case 0xef:output[i]^=src[i];break; // PXOR
          case 0xdb:output[i]&=src[i];break; // PAND
          case 0xeb:output[i]|=src[i];break; // POR
          case 0xdf:output[i]=std::uint8_t((~output[i])&src[i]);break; // PANDN
        }
      }
    }else {
      const unsigned width=(op==0xd4||op==0xfb)?8U:
        (op==0xfe||op==0xfa||op==0x76)?4U:
        (op==0xfd||op==0xf9||op==0x75)?2U:1U;
      for(unsigned i=0;i<16;i+=width) {
        const auto lhs=lane(output,i,width),rhs=lane(src,i,width);
        const bool equal=op==0x74||op==0x75||op==0x76;
        const bool sub=op==0xf8||op==0xf9||op==0xfa||op==0xfb;
        set_lane(output,i,width,equal?(lhs==rhs?(width==8?UINT64_MAX:((1ULL<<(width*8))-1)):0):
                 sub?(lhs-rhs):(lhs+rhs));
      }
    }
    fx.xmm[m.reg]=output;
    return true;
  }

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
    a20_enabled = s.a20; halted = s.halted; fx=s.fx;
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
      const auto requested = unsigned(segments[segment] & 3U);
      if (segment == SS) {
        if (requested != privilege || cache.dpl != privilege)
          fail("protected32 SS RPL/DPL does not match CPL", FaultCode::ProtectionFault);
      } else if (!cache.conforming && std::max(privilege,requested) > cache.dpl) {
        fail("protected32 data-segment privilege check failed", FaultCode::ProtectionFault);
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
      case FaultCode::DeviceNotAvailable: return 7; // #NM
      case FaultCode::InvalidInstruction: return 6; // #UD, not missing opcodes
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
  // OSFXSR(9), OSXMMEXCPT(10) explicitly owned by the guest; neither
  // enables a CPUID SSE2 claim until the instruction subset is complete.
  constexpr std::uint32_t supported = 0x000006b0U;
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
  if(op==0xae && execute_fxstate(op))return;
  if(execute_sse_subset(op))return;
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
  switch (op) {
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
  if(op>=0xd8 && op<=0xdf) {
    (void)execute_x87(op);
    return;
  }
  if(op==0x9b) {
    if((cr0&0x8U)&&(cr0&0x2U))
      fail("FWAIT CR0.TS+MP requires #NM",FaultCode::DeviceNotAvailable);
    return;
  }
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
