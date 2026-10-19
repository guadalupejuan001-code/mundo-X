#pragma once

// COSMIC PC: an original, bounded x86 real-address-mode interpreter.
// Instruction encodings and architectural semantics follow the Intel SDM.
// This is not a complete x86 processor: unsupported instructions/states stop
// with an explicit fault. There is no protected mode, paging, x87, APIC or JIT.
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

namespace cosmic_pc {

enum class FaultCode {
  MemoryBounds, SegmentLimit, InvalidInstruction, UnsupportedOpcode,
  UnsupportedState, DivideError, UnsupportedPort, HostError
};

struct Fault {
  FaultCode code = FaultCode::HostError;
  std::uint16_t cs = 0;
  std::uint32_t eip = 0;
  std::uint16_t opcode = 0;
  std::vector<std::uint8_t> bytes;
  std::string reason;
  std::string describe() const {
    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(4) << cs << ':'
        << std::setw(4) << eip << " opcode=" << std::setw(opcode > 255 ? 4 : 2)
        << opcode << " bytes=";
    for (const auto b : bytes) out << std::setw(2) << unsigned(b) << ' ';
    out << "(" << reason << ')';
    return out.str();
  }
};

enum class StepResult { Executed, Halted, Faulted };

class Cpu {
 public:
  enum Register : unsigned { EAX, ECX, EDX, EBX, ESP, EBP, ESI, EDI };
  enum Segment : unsigned { ES, CS, SS, DS, FS, GS };
  static constexpr std::uint32_t CF = 0x0001, PF = 0x0004, AF = 0x0010,
      ZF = 0x0040, SF = 0x0080, TF = 0x0100, IF = 0x0200, DF = 0x0400,
      OF = 0x0800;
  struct DescriptorTable { std::uint16_t limit; std::uint32_t base; };

  std::array<std::uint32_t, 8> regs{};
  std::array<std::uint16_t, 6> segments{};
  std::uint32_t eip = 0x7c00, eflags = 2, cr0 = 0x10;
  DescriptorTable gdtr{0, 0}, idtr{0x3ff, 0};
  std::vector<std::uint8_t> memory;
  bool a20_enabled = false;
  bool halted = false;
  std::optional<Fault> fault;
  std::uint64_t instructions = 0;

  // true: the host BIOS handled INT, with EIP already past the instruction.
  // false: execute the guest IVT handler with a real interrupt stack frame.
  std::function<bool(Cpu&, std::uint8_t)> interrupt_hook;
  // Widths are BYTES (1, 2, or 4). A missing/declined hook is a fault.
  std::function<std::optional<std::uint32_t>(std::uint16_t, unsigned)> in_hook;
  std::function<bool(std::uint16_t, unsigned, std::uint32_t)> out_hook;

  explicit Cpu(std::size_t ram_bytes = 2 * 1024 * 1024) : memory(ram_bytes, 0) {
    if (ram_bytes == 0) throw std::invalid_argument("RAM must not be empty");
    decoded_.reserve(15);
    journal_.reserve(64);
  }

  // Reset architectural state; deliberately preserve loaded RAM and hooks.
  void reset(std::uint16_t cs = 0, std::uint32_t ip = 0x7c00) {
    regs.fill(0); segments.fill(0); segments[CS] = cs;
    eip = ip; eflags = 2; cr0 = 0x10;
    gdtr = {0, 0}; idtr = {0x3ff, 0};
    a20_enabled = false; halted = false; fault.reset(); instructions = 0;
    transaction_ = false; journal_.clear(); decoded_.clear();
  }
  std::uint16_t reg16(unsigned r) const { check_register(r); return std::uint16_t(regs[r]); }
  void set_reg16(unsigned r, std::uint16_t value) {
    check_register(r); regs[r] = (regs[r] & 0xffff0000U) | value;
  }
  std::uint8_t reg8(unsigned r) const {
    check_register(r);
    return std::uint8_t(regs[r & 3U] >> (r < 4 ? 0 : 8));
  }
  void set_reg8(unsigned r, std::uint8_t value) {
    check_register(r);
    const unsigned shift = r < 4 ? 0 : 8;
    auto& target = regs[r & 3U];
    target = (target & ~(0xffU << shift)) | (std::uint32_t(value) << shift);
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
  std::uint8_t read8(std::uint32_t address) const { return memory[physical_index(address)]; }
  std::uint16_t read16(std::uint32_t address) const { return std::uint16_t(read_physical(address, 2)); }
  std::uint32_t read32(std::uint32_t address) const { return read_physical(address, 4); }
  void write8(std::uint32_t address, std::uint8_t value) {
    const auto index = physical_index(address);
    if (transaction_) journal_.emplace_back(index, memory[index]);
    memory[index] = value;
  }
  void write16(std::uint32_t address, std::uint16_t value) { write_physical(address, 2, value); }
  void write32(std::uint32_t address, std::uint32_t value) { write_physical(address, 4, value); }
  std::uint32_t linear(std::uint16_t segment, std::uint32_t offset,
                       unsigned bytes = 1) const {
    if (bytes == 0 || std::uint64_t(offset) + bytes > 0x10000ULL)
      fail("real-mode segment limit (64 KiB) exceeded", FaultCode::SegmentLimit);
    return (std::uint32_t(segment) << 4U) + offset;
  }

  StepResult step() {
    if (fault) return StepResult::Faulted;
    if (halted) return StepResult::Halted;
    const Snapshot before{regs, segments, eip, eflags, cr0, gdtr, idtr,
                          a20_enabled, halted};
    start_ip_ = eip; start_cs_ = segments[CS]; cursor_ = eip;
    decoded_.clear(); journal_.clear(); opcode_ = 0;
    op32_ = false; addr32_ = false; repeat_ = 0; override_ = -1;
    ip_written_ = false; transaction_ = true;
    try {
      if ((cr0 & 0x80000001U) != 0)
        fail("protected mode or paging is not implemented", FaultCode::UnsupportedState);
      if (eflags & (TF | 0x20000U))
        fail("single-step trap / virtual-8086 state is not implemented", FaultCode::UnsupportedState);
      std::uint8_t op;
      for (;;) {
        op = fetch8();
        if (op == 0x66) { op32_ = true; continue; }
        if (op == 0x67) { addr32_ = true; continue; }
        if (op == 0xf2 || op == 0xf3) { repeat_ = op; continue; }
        if (op == 0x26) { override_ = ES; continue; }
        if (op == 0x2e) { override_ = CS; continue; }
        if (op == 0x36) { override_ = SS; continue; }
        if (op == 0x3e) { override_ = DS; continue; }
        if (op == 0x64) { override_ = FS; continue; }
        if (op == 0x65) { override_ = GS; continue; }
        opcode_ = op;
        if (op == 0xf0)
          fail("LOCK prefix is not implemented", FaultCode::UnsupportedState);
        break;
      }
      execute(op);
      if (fault) fail(fault->reason, fault->code);
      if (!ip_written_) eip = cursor_ & 0xffffU;
      eflags |= 2;
      ++instructions;
      transaction_ = false; journal_.clear();
      return halted ? StepResult::Halted : StepResult::Executed;
    } catch (const Trap& t) {
      rollback(before);
      fault = Fault{t.code, start_cs_, start_ip_, opcode_, decoded_, t.reason};
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
    Trap(FaultCode c, std::string message)
        : std::runtime_error(message), code(c), reason(std::move(message)) {}
  };
  struct Snapshot {
    std::array<std::uint32_t, 8> regs;
    std::array<std::uint16_t, 6> segments;
    std::uint32_t eip, eflags, cr0;
    DescriptorTable gdtr, idtr;
    bool a20, halted;
  };
  struct Operand {
    bool is_register = false;
    unsigned index = 0;
    std::uint16_t segment = 0;
    std::uint32_t offset = 0;
    bool uses_esp = false;
  };
  struct ModRM { unsigned reg; Operand rm; };

  bool op32_ = false, addr32_ = false, ip_written_ = false, transaction_ = false;
  int override_ = -1;
  std::uint8_t repeat_ = 0;
  std::uint16_t start_cs_ = 0, opcode_ = 0;
  std::uint32_t start_ip_ = 0, cursor_ = 0;
  std::vector<std::uint8_t> decoded_;
  std::vector<std::pair<std::size_t, std::uint8_t>> journal_;

  void rollback(const Snapshot& s) {
    // Only our architectural state and write* calls are transactional. Host
    // callbacks can have external side effects that cannot be rolled back.
    for (auto it = journal_.rbegin(); it != journal_.rend(); ++it) memory[it->first] = it->second;
    transaction_ = false; journal_.clear();
    regs = s.regs; segments = s.segments; eip = s.eip; eflags = s.eflags;
    cr0 = s.cr0; gdtr = s.gdtr; idtr = s.idtr; a20_enabled = s.a20; halted = s.halted;
  }
  static void check_register(unsigned r) {
    if (r >= 8) throw std::out_of_range("register index");
  }
  std::size_t physical_index(std::uint32_t address) const {
    const std::uint32_t bus = a20_enabled ? address : address & ~0x100000U;
    if (bus >= memory.size()) fail("physical RAM access outside allocation", FaultCode::MemoryBounds);
    return bus;
  }
  void preflight(std::uint32_t address, unsigned bytes) const {
    if (bytes == 0 || std::uint64_t(address) + bytes > 0x100000000ULL)
      fail("physical address overflow", FaultCode::MemoryBounds);
    for (unsigned i = 0; i < bytes; ++i) (void)physical_index(address + i);
  }
  std::uint32_t read_physical(std::uint32_t address, unsigned bytes) const {
    preflight(address, bytes);
    std::uint32_t result = 0;
    for (unsigned i = 0; i < bytes; ++i) result |= std::uint32_t(read8(address + i)) << (8U * i);
    return result;
  }
  void write_physical(std::uint32_t address, unsigned bytes, std::uint32_t value) {
    preflight(address, bytes);
    for (unsigned i = 0; i < bytes; ++i) write8(address + i, std::uint8_t(value >> (8U * i)));
  }
  std::uint32_t read_segment(std::uint16_t segment, std::uint32_t offset, unsigned bytes) const {
    return read_physical(linear(segment, offset, bytes), bytes);
  }
  void write_segment(std::uint16_t segment, std::uint32_t offset, unsigned bytes, std::uint32_t value) {
    write_physical(linear(segment, offset, bytes), bytes, value);
  }
  unsigned width() const { return op32_ ? 4 : 2; }
  static std::uint32_t mask(unsigned bytes) { return bytes == 4 ? 0xffffffffU : ((1U << (8U * bytes)) - 1); }
  static std::uint32_t sign_bit(unsigned bytes) { return 1U << (8U * bytes - 1); }
  static std::int64_t signed_value(std::uint32_t value, unsigned bytes) {
    const std::uint64_t v = value & mask(bytes);
    return (v & sign_bit(bytes)) ? std::int64_t(v) - (std::int64_t{1} << (8 * bytes)) : std::int64_t(v);
  }
  std::uint8_t fetch8() {
    if (decoded_.size() >= 15) fail("instruction exceeds 15 bytes", FaultCode::InvalidInstruction);
    const auto value = std::uint8_t(read_segment(segments[CS], cursor_, 1));
    ++cursor_; decoded_.push_back(value);
    return value;
  }
  std::uint32_t immediate(unsigned bytes) {
    std::uint32_t value = 0;
    for (unsigned i = 0; i < bytes; ++i) value |= std::uint32_t(fetch8()) << (8 * i);
    return value;
  }
  std::uint32_t get_reg(unsigned r, unsigned bytes) const {
    return bytes == 1 ? reg8(r) : bytes == 2 ? reg16(r) : regs[r];
  }
  void put_reg(unsigned r, unsigned bytes, std::uint32_t value) {
    if (bytes == 1) set_reg8(r, std::uint8_t(value));
    else if (bytes == 2) set_reg16(r, std::uint16_t(value));
    else regs[r] = value;
  }
  std::uint16_t data_segment(unsigned fallback = DS) const {
    return segments[override_ < 0 ? fallback : unsigned(override_)];
  }
  ModRM modrm() {
    const auto encoded = fetch8();
    const unsigned mod = encoded >> 6, reg = (encoded >> 3) & 7, rm = encoded & 7;
    if (mod == 3) return {reg, Operand{true, rm, 0, 0, false}};
    std::uint32_t offset = 0;
    unsigned default_segment = DS;
    bool uses_esp = false;
    if (!addr32_) {
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
      if (mod == 1) offset += std::uint32_t(signed_value(immediate(1), 1));
      if (mod == 2) offset += immediate(2);
      offset &= 0xffffU;
    } else {
      if (rm == 4) {
        const auto sib = fetch8();
        const unsigned scale = sib >> 6, index = (sib >> 3) & 7, base = sib & 7;
        if (index != 4) offset += regs[index] << scale;
        if (base == 5 && mod == 0) offset += immediate(4);
        else {
          offset += regs[base];
          if (base == ESP || base == EBP) default_segment = SS;
          uses_esp = base == ESP;
        }
      } else if (rm == 5 && mod == 0) offset = immediate(4);
      else { offset = regs[rm]; if (rm == EBP) default_segment = SS; }
      if (mod == 1) offset += std::uint32_t(signed_value(immediate(1), 1));
      if (mod == 2) offset += immediate(4);
    }
    return {reg, Operand{false, 0, data_segment(default_segment), offset, uses_esp}};
  }
  std::uint32_t get(const Operand& operand, unsigned bytes) const {
    return operand.is_register ? get_reg(operand.index, bytes)
        : read_segment(operand.segment, operand.offset, bytes);
  }
  void put(const Operand& operand, unsigned bytes, std::uint32_t value) {
    if (operand.is_register) put_reg(operand.index, bytes, value);
    else write_segment(operand.segment, operand.offset, bytes, value);
  }
  void require_memory(const Operand& operand) const {
    if (operand.is_register) fail("instruction requires a memory operand", FaultCode::InvalidInstruction);
  }
  void szp(std::uint32_t value, unsigned bytes) {
    value &= mask(bytes);
    set_flag(SF, (value & sign_bit(bytes)) != 0);
    set_flag(ZF, value == 0);
    unsigned parity = value & 0xffU;
    parity ^= parity >> 4; parity ^= parity >> 2; parity ^= parity >> 1;
    set_flag(PF, (parity & 1U) == 0);
  }
  // Group 1: ADD, OR, ADC, SBB, AND, SUB, XOR, CMP.
  std::uint32_t alu(unsigned operation, std::uint32_t a, std::uint32_t b, unsigned bytes) {
    const auto m = mask(bytes), sign = sign_bit(bytes);
    a &= m; b &= m;
    std::uint32_t result;
    if (operation == 1 || operation == 4 || operation == 6) {
      result = operation == 1 ? a | b : operation == 4 ? a & b : a ^ b;
      set_flag(CF, false); set_flag(OF, false);
      // AF is architecturally undefined for logical operations: preserve it.
    } else {
      const unsigned carry = (operation == 2 || operation == 3) && flag(CF) ? 1 : 0;
      const bool subtract = operation == 3 || operation == 5 || operation == 7;
      if (subtract) {
        result = std::uint32_t(std::uint64_t(a) - b - carry) & m;
        set_flag(CF, std::uint64_t(a) < std::uint64_t(b) + carry);
        set_flag(OF, ((a ^ b) & (a ^ result) & sign) != 0);
      } else {
        const auto full = std::uint64_t(a) + b + carry;
        result = std::uint32_t(full) & m;
        set_flag(CF, full > m);
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
  void jump(std::uint32_t target, unsigned bytes) {
    target &= mask(bytes);
    if (target > 0xffffU) fail("code target outside real-mode CS limit", FaultCode::SegmentLimit);
    eip = target; ip_written_ = true;
  }
  void relative(std::int64_t displacement, unsigned bytes) {
    jump(std::uint32_t(std::int64_t(cursor_) + displacement), bytes);
  }
  void push(std::uint32_t value, unsigned bytes) {
    const auto sp = std::uint16_t(reg16(ESP) - bytes);
    write_segment(segments[SS], sp, bytes, value);
    set_reg16(ESP, sp);
  }
  std::uint32_t pop(unsigned bytes) {
    const auto value = read_segment(segments[SS], reg16(ESP), bytes);
    set_reg16(ESP, std::uint16_t(reg16(ESP) + bytes));
    return value;
  }
  std::uint32_t index(unsigned r) const { return addr32_ ? regs[r] : reg16(r); }
  void put_index(unsigned r, std::uint32_t value) {
    if (addr32_) regs[r] = value; else set_reg16(r, std::uint16_t(value));
  }
  void advance_index(unsigned r, unsigned bytes) {
    put_index(r, index(r) + (flag(DF) ? -std::uint32_t(bytes) : bytes));
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
  void group3(const ModRM& m, unsigned bytes);
  void multiply_signed(unsigned destination, std::uint32_t a, std::uint32_t b, unsigned bytes);
  void extended();
  void execute(std::uint8_t op);
};

inline void Cpu::string_operation(std::uint8_t op) {
  if (repeat_ && index(ECX) == 0) return;
  const unsigned bytes = (op & 1U) ? width() : 1;
  const auto src = index(ESI), dst = index(EDI);
  bool compare = false;
  switch (op & 0xfeU) {
    case 0xa4: {
      const auto value = read_segment(data_segment(), src, bytes);
      write_segment(segments[ES], dst, bytes, value);
      advance_index(ESI, bytes); advance_index(EDI, bytes); break;
    }
    case 0xa6:
      (void)alu(7, read_segment(data_segment(), src, bytes),
                read_segment(segments[ES], dst, bytes), bytes);
      advance_index(ESI, bytes); advance_index(EDI, bytes); compare = true; break;
    case 0xaa:
      write_segment(segments[ES], dst, bytes, get_reg(EAX, bytes));
      advance_index(EDI, bytes); break;
    case 0xac:
      put_reg(EAX, bytes, read_segment(data_segment(), src, bytes));
      advance_index(ESI, bytes); break;
    case 0xae:
      (void)alu(7, get_reg(EAX, bytes), read_segment(segments[ES], dst, bytes), bytes);
      advance_index(EDI, bytes); compare = true; break;
    case 0x6c: {
      const auto address = linear(segments[ES], dst, bytes);
      preflight(address, bytes);  // Do not consume I/O if the destination faults.
      write_physical(address, bytes, port_in(reg16(EDX), bytes));
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
  count &= 31;
  if (count == 0) return;
  const auto bits = 8U * bytes, mask_value = mask(bytes), sign = sign_bit(bytes);
  const auto original = get(m.rm, bytes);
  auto value = original;
  const auto operation = m.reg;
  unsigned effective = count;
  if (operation <= 1) effective %= bits;
  if (operation == 2 || operation == 3) effective %= (bits + 1);
  if ((operation == 2 || operation == 3) && effective == 0) return;
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

inline void Cpu::multiply_signed(unsigned destination, std::uint32_t a,
                                 std::uint32_t b, unsigned bytes) {
  const auto result = signed_value(a, bytes) * signed_value(b, bytes);
  const auto low = std::uint32_t(std::uint64_t(result)) & mask(bytes);
  const bool overflow = signed_value(low, bytes) != result;
  put_reg(destination, bytes, low);
  set_flag(CF, overflow); set_flag(OF, overflow);
}

inline void Cpu::group3(const ModRM& m, unsigned bytes) {
  const auto operand = get(m.rm, bytes);
  const auto bits = bytes * 8U, maximum = mask(bytes);
  switch (m.reg) {
    case 0: (void)alu(4, operand, immediate(bytes), bytes); return;
    case 1: fail("reserved group-3 encoding", FaultCode::InvalidInstruction);
    case 2: put(m.rm, bytes, ~operand); return;
    case 3: put(m.rm, bytes, alu(5, 0, operand, bytes)); return;
    case 4: case 5: {
      std::uint64_t product;
      bool overflow;
      if (m.reg == 4) {
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
      if (m.reg == 6) {
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
    const auto disp = signed_value(immediate(bytes), bytes);
    if (condition(op & 15)) relative(disp, bytes);
    return;
  }
  if (op >= 0x90 && op <= 0x9f) { const auto m = modrm(); put(m.rm, 1, condition(op & 15) ? 1 : 0); return; }
  if (op >= 0xc8 && op <= 0xcf) {
    auto& value = regs[op & 7];
    value = ((value & 0xffU) << 24) | ((value & 0xff00U) << 8) |
            ((value >> 8) & 0xff00U) | (value >> 24);
    return;
  }
  switch (op) {
    case 0x01: {
      const auto m = modrm();
      if (m.reg == 4) { put(m.rm, m.rm.is_register ? bytes : 2, cr0 & 0xffffU); return; }
      if (m.reg == 6) {
        const auto value = get(m.rm, 2) & 15;
        if (value & 1) fail("LMSW requests protected mode; not implemented", FaultCode::UnsupportedState);
        cr0 = (cr0 & ~15U) | value; return;
      }
      if (m.reg > 3) fail("system group-7 instruction not implemented", FaultCode::UnsupportedOpcode);
      require_memory(m.rm);
      const auto address = linear(m.rm.segment, m.rm.offset, 6);
      preflight(address, 6);
      auto& table = (m.reg & 1U) ? idtr : gdtr;
      if (m.reg < 2) { write16(address, table.limit); write32(address + 2, table.base); }
      else {
        const auto limit = read16(address);
        const auto base = read32(address + 2) & (op32_ ? 0xffffffffU : 0x00ffffffU);
        table = {limit, base};
      }
      return;
    }
    case 0x06: cr0 &= ~8U; return;  // CLTS; no floating-point execution is claimed.
    case 0x20: case 0x22: {
      const auto m = modrm();
      if (!m.rm.is_register) fail("MOV CR requires a register operand", FaultCode::InvalidInstruction);
      if (m.reg != 0) fail("only CR0 is implemented", FaultCode::UnsupportedState);
      if (op == 0x20) regs[m.rm.index] = cr0;
      else {
        const auto value = regs[m.rm.index];
        if (value & 0x80000001U)
          fail("MOV CR0 requests protected mode/paging; not implemented", FaultCode::UnsupportedState);
        cr0 = value | 0x10U;
      }
      return;
    }
    case 0xa0: push(segments[FS], bytes); return;
    case 0xa1: segments[FS] = std::uint16_t(pop(bytes)); return;
    case 0xa8: push(segments[GS], bytes); return;
    case 0xa9: segments[GS] = std::uint16_t(pop(bytes)); return;
    case 0xaf: {
      const auto m = modrm(); multiply_signed(m.reg, get_reg(m.reg, bytes), get(m.rm, bytes), bytes); return;
    }
    case 0xb2: case 0xb4: case 0xb5: {
      const auto m = modrm(); require_memory(m.rm);
      const auto address = linear(m.rm.segment, m.rm.offset, bytes + 2);
      const auto value = read_physical(address, bytes);
      const auto segment = read16(address + bytes);
      put_reg(m.reg, bytes, value);
      segments[op == 0xb2 ? SS : op == 0xb4 ? FS : GS] = segment; return;
    }
    case 0xb6: case 0xb7: case 0xbe: case 0xbf: {
      const auto m = modrm(); const unsigned source_width = (op & 1U) ? 2 : 1;
      const auto value = get(m.rm, source_width);
      put_reg(m.reg, bytes, (op & 8U) ? std::uint32_t(signed_value(value, source_width)) : value); return;
    }
    case 0x0b: fail("UD2 invalid opcode", FaultCode::InvalidInstruction);
    default: fail("two-byte opcode is not implemented", FaultCode::UnsupportedOpcode);
  }
}

inline void Cpu::execute(std::uint8_t op) {
  const unsigned bytes = width();
  if (op <= 0x3d && (op & 7U) <= 5) {
    const unsigned operation = op >> 3, form = op & 7U;
    const unsigned w = (op & 1U) ? bytes : 1;
    if (form >= 4) {
      const auto result = alu(operation, get_reg(EAX, w), immediate(w), w);
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
  if (op >= 0x50 && op <= 0x57) { push(get_reg(op & 7, bytes), bytes); return; }
  if (op >= 0x58 && op <= 0x5f) { const auto value = pop(bytes); put_reg(op & 7, bytes, value); return; }
  if (op >= 0x70 && op <= 0x7f) {
    const auto disp = signed_value(immediate(1), 1);
    if (condition(op & 15)) relative(disp, bytes);
    return;
  }
  if (op >= 0x91 && op <= 0x97) {
    const auto value = get_reg(EAX, bytes);
    put_reg(EAX, bytes, get_reg(op & 7, bytes)); put_reg(op & 7, bytes, value); return;
  }
  if (op >= 0xb0 && op <= 0xb7) { put_reg(op & 7, 1, immediate(1)); return; }
  if (op >= 0xb8 && op <= 0xbf) { put_reg(op & 7, bytes, immediate(bytes)); return; }
  if ((op >= 0xa4 && op <= 0xa7) || (op >= 0xaa && op <= 0xaf) || (op >= 0x6c && op <= 0x6f)) {
    string_operation(op); return;
  }
  switch (op) {
    case 0x06: push(segments[ES], bytes); return;
    case 0x07: segments[ES] = std::uint16_t(pop(bytes)); return;
    case 0x0e: push(segments[CS], bytes); return;
    case 0x0f: extended(); return;
    case 0x16: push(segments[SS], bytes); return;
    case 0x17: segments[SS] = std::uint16_t(pop(bytes)); return;
    case 0x1e: push(segments[DS], bytes); return;
    case 0x1f: segments[DS] = std::uint16_t(pop(bytes)); return;
    case 0x60: {
      const auto old_sp = get_reg(ESP, bytes);
      for (unsigned r = 0; r < 8; ++r) push(r == ESP ? old_sp : get_reg(r, bytes), bytes);
      return;
    }
    case 0x61:
      for (int r = 7; r >= 0; --r) { const auto value = pop(bytes); if (r != ESP) put_reg(unsigned(r), bytes, value); }
      return;
    case 0x68: push(immediate(bytes), bytes); return;
    case 0x6a: push(std::uint32_t(signed_value(immediate(1), 1)), bytes); return;
    case 0x69: case 0x6b: {
      const auto m = modrm(); const auto source = get(m.rm, bytes);
      const auto value = op == 0x6b ? std::uint32_t(signed_value(immediate(1), 1)) : immediate(bytes);
      multiply_signed(m.reg, source, value, bytes); return;
    }
    case 0x80: case 0x81: case 0x83: {
      const auto m = modrm(); const unsigned w = op == 0x80 ? 1 : bytes;
      const auto value = op == 0x83 ? std::uint32_t(signed_value(immediate(1), 1)) : immediate(w);
      const auto result = alu(m.reg, get(m.rm, w), value, w);
      if (m.reg != 7) put(m.rm, w, result);
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
      if (m.reg > GS || (op == 0x8e && m.reg == CS))
        fail("invalid segment register encoding", FaultCode::InvalidInstruction);
      if (op == 0x8c) put(m.rm, m.rm.is_register ? bytes : 2, segments[m.reg]);
      else segments[m.reg] = std::uint16_t(get(m.rm, 2));
      return;
    }
    case 0x8d: { const auto m = modrm(); require_memory(m.rm); put_reg(m.reg, bytes, m.rm.offset); return; }
    case 0x8f: {
      auto m = modrm();
      if (m.reg != 0) fail("reserved POP encoding", FaultCode::InvalidInstruction);
      const auto old_esp = regs[ESP]; const auto value = pop(bytes);
      if (!m.rm.is_register && m.rm.uses_esp) m.rm.offset += regs[ESP] - old_esp;
      put(m.rm, bytes, value); return;
    }
    case 0x90: return;  // NOP / F3 90 PAUSE (no timing guarantee).
    case 0x98:
      put_reg(EAX, bytes, std::uint32_t(signed_value(get_reg(EAX, bytes / 2), bytes / 2))); return;
    case 0x99: put_reg(EDX, bytes, (get_reg(EAX, bytes) & sign_bit(bytes)) ? mask(bytes) : 0); return;
    case 0x9a: {
      const auto target = immediate(bytes); const auto cs = std::uint16_t(immediate(2));
      jump(target, bytes); push(segments[CS], bytes); push(cursor_ & mask(bytes), bytes); segments[CS] = cs; return;
    }
    case 0x9c: push(eflags & ~0x30000U, bytes); return;
    case 0x9d: { const auto value = pop(bytes); restore_flags(value, bytes); return; }
    case 0x9e:
      eflags = (eflags & ~0xd5U) | (reg8(4) & 0xd5U) | 2; return;
    case 0x9f: set_reg8(4, std::uint8_t((eflags & 0xd5U) | 2)); return;
    case 0xa0: case 0xa1: case 0xa2: case 0xa3: {
      const auto address = immediate(addr32_ ? 4 : 2); const unsigned w = (op & 1U) ? bytes : 1;
      if (op & 2U) write_segment(data_segment(), address, w, get_reg(EAX, w));
      else put_reg(EAX, w, read_segment(data_segment(), address, w));
      return;
    }
    case 0xa8: case 0xa9: {
      const unsigned w = (op & 1U) ? bytes : 1; (void)alu(4, get_reg(EAX, w), immediate(w), w); return;
    }
    case 0xc0: case 0xc1: case 0xd0: case 0xd1: case 0xd2: case 0xd3: {
      const auto m = modrm(); const unsigned w = (op & 1U) ? bytes : 1;
      const unsigned count = op < 0xd0 ? immediate(1) : op < 0xd2 ? 1 : reg8(ECX);
      shift(m, w, count); return;
    }
    case 0xc2: case 0xc3: {
      const auto adjustment = op == 0xc2 ? immediate(2) : 0;
      const auto target = pop(bytes); jump(target, bytes);
      set_reg16(ESP, std::uint16_t(reg16(ESP) + adjustment)); return;
    }
    case 0xc4: case 0xc5: {
      const auto m = modrm(); require_memory(m.rm);
      const auto address = linear(m.rm.segment, m.rm.offset, bytes + 2);
      const auto value = read_physical(address, bytes); const auto segment = read16(address + bytes);
      put_reg(m.reg, bytes, value); segments[op == 0xc4 ? ES : DS] = segment; return;
    }
    case 0xc6: case 0xc7: {
      const auto m = modrm();
      if (m.reg != 0) fail("reserved MOV immediate encoding", FaultCode::InvalidInstruction);
      const unsigned w = (op & 1U) ? bytes : 1; put(m.rm, w, immediate(w)); return;
    }
    case 0xc9: set_reg16(ESP, reg16(EBP)); put_reg(EBP, bytes, pop(bytes)); return;
    case 0xca: case 0xcb: {
      const auto adjustment = op == 0xca ? immediate(2) : 0;
      const auto target = pop(bytes); const auto cs = std::uint16_t(pop(bytes));
      jump(target, bytes); segments[CS] = cs;
      set_reg16(ESP, std::uint16_t(reg16(ESP) + adjustment)); return;
    }
    case 0xcc: interrupt(3); return;
    case 0xcd: { const auto vector = fetch8(); interrupt(vector); return; }
    case 0xce: if (flag(OF)) interrupt(4); return;
    case 0xcf: {
      const auto target = pop(bytes); const auto cs = std::uint16_t(pop(bytes)); const auto flags = pop(bytes);
      if (flags & 0x20000U) fail("IRET virtual-8086 transition is not implemented", FaultCode::UnsupportedState);
      jump(target, bytes); restore_flags(flags, bytes); segments[CS] = cs; return;
    }
    case 0xd7: {
      const auto address = (index(EBX) + reg8(EAX)) & (addr32_ ? 0xffffffffU : 0xffffU);
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
      const unsigned w = (op & 1U) ? bytes : 1; put_reg(EAX, w, port_in(port, w)); return;
    }
    case 0xe6: case 0xe7: case 0xee: case 0xef: {
      const auto port = std::uint16_t(op < 0xee ? immediate(1) : reg16(EDX));
      const unsigned w = (op & 1U) ? bytes : 1; port_out(port, w, get_reg(EAX, w)); return;
    }
    case 0xe8: {
      const auto disp = signed_value(immediate(bytes), bytes);
      const auto return_ip = cursor_ & mask(bytes); relative(disp, bytes); push(return_ip, bytes); return;
    }
    case 0xe9: { const auto disp = signed_value(immediate(bytes), bytes); relative(disp, bytes); return; }
    case 0xea: {
      const auto target = immediate(bytes); const auto cs = std::uint16_t(immediate(2));
      jump(target, bytes); segments[CS] = cs; return;
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
      if (m.reg < 2) {
        const bool old_cf = flag(CF); const auto value = alu(m.reg == 0 ? 0 : 5, get(m.rm, w), 1, w);
        put(m.rm, w, value); set_flag(CF, old_cf); return;
      }
      if (op == 0xfe || m.reg == 7) fail("reserved group-4/group-5 encoding", FaultCode::InvalidInstruction);
      if (m.reg == 6) { const auto value = get(m.rm, bytes); push(value, bytes); return; }
      if (m.reg == 2 || m.reg == 4) {
        const auto target = get(m.rm, bytes); jump(target, bytes);
        if (m.reg == 2) push(cursor_ & mask(bytes), bytes);
        return;
      }
      require_memory(m.rm);
      const auto address = linear(m.rm.segment, m.rm.offset, bytes + 2);
      const auto target = read_physical(address, bytes); const auto cs = read16(address + bytes);
      jump(target, bytes);
      if (m.reg == 3) { push(segments[CS], bytes); push(cursor_ & mask(bytes), bytes); }
      segments[CS] = cs; return;
    }
    default: fail("opcode is not implemented", FaultCode::UnsupportedOpcode);
  }
}

}  // namespace cosmic_pc
