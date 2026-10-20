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
#include <functional>
#include <iomanip>
#include <limits>
#include <optional>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include "sparse_ram.hpp"

namespace cosmic_pc {

enum class FaultCode {
  MemoryBounds, SegmentLimit, InvalidInstruction, UnsupportedOpcode,
  UnsupportedState, DivideError, UnsupportedPort, HostError, ProtectionFault,
  PageFault, MemoryBudget
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
  struct SegmentCache {
    std::uint64_t base = 0;
    std::uint32_t limit = 0xffff;
    bool valid = true, code = false, writable = true, readable = true,
         big = false, long_code = false;
  };

  std::array<std::uint64_t, 16> regs{};
  std::array<std::uint16_t, 6> segments{};
  std::uint64_t eip = 0x7c00, cr2 = 0, cr3 = 0, efer = 0, fs_base = 0, gs_base = 0;
  std::uint32_t eflags = 2, cr0 = 0x10, cr4 = 0;
  DescriptorTable gdtr{0, 0}, idtr{0x3ff, 0};
  SparseRam memory;
  bool a20_enabled = false;
  bool halted = false;
  std::optional<Fault> fault;
  std::uint64_t instructions = 0;
  // Diagnostic counter: one increment per actual IA-32e page-table walk.
  // Used to prove that multi-byte accesses do not repeat the same walk.
  std::uint64_t page_walks = 0;

  // true: the host BIOS handled INT, with EIP already past the instruction.
  // false: execute the guest IVT handler with a real interrupt stack frame.
  std::function<bool(Cpu&, std::uint8_t)> interrupt_hook;
  // Widths are BYTES (1, 2, or 4). A missing/declined hook is a fault.
  std::function<std::optional<std::uint32_t>(std::uint16_t, unsigned)> in_hook;
  std::function<bool(std::uint16_t, unsigned, std::uint32_t)> out_hook;

  explicit Cpu(std::size_t ram_bytes = 2 * 1024 * 1024)
      : Cpu(ram_bytes, SparseRam::default_budget(ram_bytes)) {}
  Cpu(std::size_t ram_bytes, std::size_t resident_budget_bytes)
      : memory(ram_bytes, resident_budget_bytes) {
    if (ram_bytes == 0) throw std::invalid_argument("RAM must not be empty");
    decoded_.reserve(15);
    journal_.reserve(64);
  }

  // Reset architectural state; deliberately preserve loaded RAM and hooks.
  void reset(std::uint16_t cs = 0, std::uint64_t ip = 0x7c00) {
    regs.fill(0); segments.fill(0); segments[CS] = cs;
    eip = ip; eflags = 2; cr0 = 0x10; cr2 = cr3 = cr4 = efer = fs_base = gs_base = 0;
    cache_.fill(SegmentCache{}); cache_[CS].code = true;
    gdtr = {0, 0}; idtr = {0x3ff, 0};
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
  std::uint8_t read8(std::uint64_t address) const { return memory[physical_index(address)]; }
  std::uint16_t read16(std::uint64_t address) const { return std::uint16_t(read_physical(address, 2)); }
  std::uint32_t read32(std::uint64_t address) const { return std::uint32_t(read_physical(address, 4)); }
  std::uint64_t read64(std::uint64_t address) const { return read_physical(address, 8); }
  void write8(std::uint64_t address, std::uint8_t value) {
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
                          cr2, cr3, efer, fs_base, gs_base, gdtr, idtr, a20_enabled, halted};
    start_ip_ = eip; start_cs_ = segments[CS]; cursor_ = eip;
    decoded_.clear(); journal_.clear(); opcode_ = 0;
    operand_override_ = address_override_ = false; rex_ = 0;
    op32_ = is_long_mode() || (!is_real_mode() && cache_[CS].big);
    addr32_ = op32_; repeat_ = 0; override_ = -1;
    ip_written_ = false; transaction_ = true;
    try {
      if ((segments[CS] & 3) && !is_real_mode())
        fail("only ring 0 execution is implemented", FaultCode::UnsupportedState);
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
        if (op == 0xf0)
          fail("LOCK prefix is not implemented", FaultCode::UnsupportedState);
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
    } catch (const SparseRam::BudgetExceeded& e) {
      rollback(before);
      fault = Fault{FaultCode::MemoryBudget, start_cs_, start_ip_, opcode_, decoded_, e.what()};
    } catch (const std::exception& e) {
      rollback(before);
      fault = Fault{FaultCode::HostError, start_cs_, start_ip_, opcode_, decoded_, e.what()};
    }
    return StepResult::Faulted;
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
    bool a20, halted;
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
  bool operand_override_ = false, address_override_ = false;
  std::uint8_t rex_ = 0;
  std::array<SegmentCache, 6> cache_{};
  int override_ = -1;
  std::uint8_t repeat_ = 0;
  std::uint16_t start_cs_ = 0, opcode_ = 0;
  std::uint64_t start_ip_ = 0, cursor_ = 0;
  std::vector<std::uint8_t> decoded_;
  std::vector<std::pair<std::size_t, std::uint8_t>> journal_;

  void rollback(const Snapshot& s) {
    // Only our architectural state and write* calls are transactional. Host
    // callbacks can have external side effects that cannot be rolled back.
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) memory.restore_byte(it->first, it->second);
    transaction_ = false; journal_.clear();
    regs = s.regs; segments = s.segments; eip = s.eip; eflags = s.eflags;
    cache_ = s.cache; cr0 = s.cr0; cr4 = s.cr4; cr2 = s.cr2; cr3 = s.cr3;
    efer = s.efer; fs_base = s.fs_base; gs_base = s.gs_base;
    gdtr = s.gdtr; idtr = s.idtr; a20_enabled = s.a20; halted = s.halted;
  }
  static void check_register(unsigned r) {
    if (r >= 16) throw std::out_of_range("register index");
  }
  enum class Access { Read, Write, Execute };
  std::size_t physical_index(std::uint64_t address) const {
    const std::uint64_t bus = a20_enabled ? address : address & ~std::uint64_t{0x100000};
    if (bus >= memory.size()) fail("physical RAM access outside allocation", FaultCode::MemoryBounds);
    return bus;
  }
  void preflight(std::uint64_t address, unsigned bytes) const {
    if (bytes == 0 || address > std::numeric_limits<std::uint64_t>::max() - (bytes - 1))
      fail("physical address overflow", FaultCode::MemoryBounds);
    for (unsigned i = 0; i < bytes; ++i) (void)physical_index(address + i);
  }
  std::uint64_t read_physical(std::uint64_t address, unsigned bytes) const {
    preflight(address, bytes);
    std::uint64_t result = 0;
    for (unsigned i = 0; i < bytes; ++i) result |= std::uint64_t(read8(address + i)) << (8U * i);
    return result;
  }
  void write_physical(std::uint64_t address, unsigned bytes, std::uint64_t value) {
    preflight(address, bytes);
    const auto first = physical_index(address), last = physical_index(address + bytes - 1);
    if (last == first + bytes - 1) memory.prepare_write(first, bytes);
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
        (void)physical_index(physical[done + i]);
      }
      done += chunk;
    }
    bool contiguous = true;
    const auto first = physical_index(physical[0]);
    for (unsigned i = 1; i < bytes; ++i)
      if (physical_index(physical[i]) != first + i) contiguous = false;
    if (contiguous) memory.prepare_write(first, bytes);
    else for (unsigned i = 0; i < bytes; ++i) memory.prepare_write(physical_index(physical[i]), 1);
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
  std::uint64_t index(unsigned r) const { return get_reg(r, address_width()); }
  void put_index(unsigned r, std::uint64_t value) { put_reg(r, address_width(), value); }
  void advance_index(unsigned r, unsigned bytes) {
    put_index(r, index(r) + (flag(DF) ? -std::uint64_t(bytes) : bytes));
  }
  std::uint32_t port_in(std::uint16_t port, unsigned bytes) {
    if (!in_hook) fail("IN has no port backend", FaultCode::UnsupportedPort);
    const auto value = in_hook(port, bytes);
    if (!value) fail("IN port not implemented: " + std::to_string(port), FaultCode::UnsupportedPort);
    return *value & mask(bytes);
  }
  void port_out(std::uint16_t port, unsigned bytes, std::uint32_t value) {
    if (!out_hook || !out_hook(port, bytes, value & mask(bytes)))
      fail("OUT port not implemented: " + std::to_string(port), FaultCode::UnsupportedPort);
  }
  void restore_flags(std::uint32_t value, unsigned bytes) {
    if (value & TF) fail("trap flag requires an unimplemented debug exception", FaultCode::UnsupportedState);
    // POPFD cannot change VM/RF; accepted bits exclude reserved flags.
    constexpr std::uint32_t writable = 0x00247fd5U;
    const auto bits = writable & mask(bytes);
    eflags = (eflags & ~bits) | (value & bits) | 2;
  }
  void interrupt(std::uint8_t vector) {
    if (!is_real_mode())
      fail("protected/long-mode IDT delivery is not implemented; BIOS hooks are real-mode only", FaultCode::UnsupportedState);
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
  if (selector & 3) fail("only ring-0 segment selectors are implemented", FaultCode::UnsupportedState);
  const std::uint32_t offset = selector & 0xfff8U;
  if (!offset || offset + 7 > gdtr.limit)
    fail("selector outside GDT or null code/stack selector", FaultCode::ProtectionFault);
  if (gdtr.base > UINT64_MAX - offset - 7)
    fail("GDT address overflow", FaultCode::ProtectionFault);
  const auto raw = read_linear(gdtr.base + offset, 8);
  const unsigned access = (raw >> 40) & 0xffU, attributes = (raw >> 52) & 15U;
  if (!(access & 0x80)) fail("segment descriptor not present", FaultCode::ProtectionFault);
  if (!(access & 0x10)) fail("system segments, call gates and task switches are not implemented", FaultCode::UnsupportedState);
  if (access & 0x60) fail("only DPL 0 segments are implemented", FaultCode::UnsupportedState);
  if (access & 4) fail("conforming code / expand-down segments are not implemented", FaultCode::UnsupportedState);
  SegmentCache result;
  result.base = ((raw >> 16) & 0xffffffU) | ((raw >> 32) & 0xff000000U);
  result.limit = (raw & 0xffffU) | ((raw >> 32) & 0xf0000U);
  if (attributes & 8) result.limit = (result.limit << 12) | 0xfffU;
  result.code = (access & 8) != 0;
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
    if (segment == SS && !is_long_mode()) fail("null protected-mode SS", FaultCode::ProtectionFault);
    if (selector & 3) fail("only ring-0 null selectors are implemented", FaultCode::UnsupportedState);
    segments[segment] = selector; cache_[segment].valid = false;
    // AMD64 preserves the FS/GS hidden base when loading a null selector.
    return;
  }
  const auto value = descriptor(selector);
  if ((segment == SS && (value.code || !value.writable)) || (value.code && !value.readable))
    fail("segment type is invalid for this data/stack register", FaultCode::ProtectionFault);
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
  segments[CS] = selector; cache_[CS] = value;
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
  constexpr std::uint32_t supported = 0x000000b0U;
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
  bool writable = true, executable = true;
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
    executable = executable && !(entry >> 63);
    const bool leaf = level == 1 || large;
    if (leaf) {
      if (access == Access::Write && !writable && (cr0 & 0x10000U))
        page_fault(address, access, true, false, "supervisor write denied by page permissions / CR0.WP");
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
    case 0x01: {
      const auto m = modrm();
      if (m.group == 4) { put(m.rm, m.rm.is_register ? bytes : 2, cr0); return; }
      if (m.group == 6) {
        const auto value = get(m.rm, 2) & 15;
        write_cr0((cr0 & ~15U) | value | (cr0 & 1)); return;
      }
      if (m.group > 3) fail("system group-7 instruction not implemented", FaultCode::UnsupportedOpcode);
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
    case 0x06: cr0 &= ~8U; return;  // CLTS; no floating-point execution is claimed.
    case 0x20: case 0x22: {
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
      write_msr(std::uint32_t(regs[ECX]), (std::uint64_t(std::uint32_t(regs[EDX])) << 32) | std::uint32_t(regs[EAX])); return;
    case 0x32: {
      const auto value = read_msr(std::uint32_t(regs[ECX]));
      put_reg(EAX, 4, value); put_reg(EDX, 4, value >> 32); return;
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
      if (!is_real_mode()) fail("protected/long-mode IRET is not implemented", FaultCode::UnsupportedState);
      const auto target = pop(bytes); const auto cs = std::uint16_t(pop(bytes)); const auto flags = pop(bytes);
      if (flags & 0x20000U) fail("IRET virtual-8086 transition is not implemented", FaultCode::UnsupportedState);
      far_transfer(cs, target, bytes); restore_flags(std::uint32_t(flags), bytes); return;
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
    case 0xf4: halted = true; return;
    case 0xf5: set_flag(CF, !flag(CF)); return;
    case 0xf6: case 0xf7: { const auto m = modrm(); group3(m, (op & 1U) ? bytes : 1); return; }
    case 0xf8: set_flag(CF, false); return;
    case 0xf9: set_flag(CF, true); return;
    case 0xfa: set_flag(IF, false); return;
    case 0xfb: set_flag(IF, true); return;
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
