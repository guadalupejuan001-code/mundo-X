#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <map>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mundo_x/compact_core.hpp"

// A small CLASSICAL, deterministic basis-state VM. Its four byte registers,
// sixteen RAM bytes and scratch bits live in CompactCore, without shadow data.
// PC, instruction decode, flags and output are ordinary host-side controller
// state. Basis-only CCX reads two deterministic controls then conditionally
// applies X. It is NOT a general quantum Toffoli and accepts no superpositions.
// This is a machine-model proof, not an Android/Linux guest or a speed claim.
namespace mundo_x { namespace basis_vm {

constexpr std::size_t register_count = 4;
constexpr std::size_t ram_size = 16;
constexpr std::size_t scratch_a = 160;
constexpr std::size_t scratch_b = 168;
constexpr std::size_t scratch_c = 176;
constexpr std::size_t qubit_count = 185;
constexpr std::size_t source_limit = 16 * 1024;
constexpr std::size_t instruction_limit = 512;
constexpr std::size_t trace_limit = 64;
constexpr std::size_t trace_instruction_limit = 200;
constexpr std::size_t output_limit = 256;

struct LimitError : std::runtime_error { using std::runtime_error::runtime_error; };

enum class Op { Mov, Add, Sub, Xor, Cmp, Load, Store, Jmp, Jz, Jnz, Out, Halt };
struct Operand { bool is_register = false; unsigned value = 0; };
struct Instruction {
  Op op = Op::Halt;
  unsigned reg = 0;
  Operand argument;
  std::size_t target = 0;
  std::size_t line = 0;
  std::string label;
  std::string text;
};

inline std::string upper(std::string s) {
  for (char& ch : s) ch = static_cast<char>(std::toupper(static_cast<unsigned char>(ch)));
  return s;
}
inline bool label_name(const std::string& s) {
  if (s.empty() || !(std::isalpha(static_cast<unsigned char>(s[0])) || s[0] == '_')) return false;
  for (char c : s)
    if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return false;
  return true;
}
inline std::vector<std::string> tokens(const std::string& s) {
  std::vector<std::string> out;
  for (std::size_t i = 0; i < s.size();) {
    const unsigned char c = static_cast<unsigned char>(s[i]);
    if (std::isspace(c)) { ++i; continue; }
    if (s[i] == '#') break;
    if (s[i] == ',' || s[i] == '[' || s[i] == ']' || s[i] == ':') {
      out.emplace_back(1, s[i++]); continue;
    }
    const auto start = i;
    while (i < s.size() && !std::isspace(static_cast<unsigned char>(s[i])) &&
           s[i] != '#' && s[i] != ',' && s[i] != '[' && s[i] != ']' && s[i] != ':') ++i;
    out.push_back(s.substr(start, i - start));
  }
  return out;
}
inline unsigned parse_register(const std::string& text) {
  const auto s = upper(text);
  if (s.size() != 2 || s[0] != 'R' || s[1] < '0' || s[1] > '3')
    throw std::invalid_argument("expected register R0..R3");
  return static_cast<unsigned>(s[1] - '0');
}
inline unsigned parse_byte(const std::string& s) {
  unsigned base = 10;
  std::size_t pos = 0;
  if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) { base = 16; pos = 2; }
  if (pos == s.size()) throw std::invalid_argument("expected integer 0..255");
  unsigned result = 0;
  for (; pos < s.size(); ++pos) {
    const char ch = s[pos];
    unsigned digit = 99;
    if (ch >= '0' && ch <= '9') digit = static_cast<unsigned>(ch - '0');
    else if (ch >= 'a' && ch <= 'f') digit = static_cast<unsigned>(ch - 'a' + 10);
    else if (ch >= 'A' && ch <= 'F') digit = static_cast<unsigned>(ch - 'A' + 10);
    if (digit >= base || result > (255u - digit) / base)
      throw std::invalid_argument("invalid integer; use decimal or 0x00..0xFF");
    result = result * base + digit;
  }
  return result;
}
inline Operand parse_operand(const std::string& s) {
  if (!s.empty() && (s[0] == 'R' || s[0] == 'r')) return {true, parse_register(s)};
  return {false, parse_byte(s)};
}

inline std::vector<Instruction> parse(const std::string& source) {
  if (source.size() > source_limit) throw std::invalid_argument("source exceeds 16 KiB");
  for (unsigned char ch : source)
    if ((ch < 32 && ch != '\n' && ch != '\r' && ch != '\t') || ch > 126)
      throw std::invalid_argument("source must use printable ASCII and whitespace");
  std::vector<Instruction> program;
  std::map<std::string, std::size_t> labels;
  std::istringstream input(source);
  std::string line;
  std::size_t line_number = 0;
  while (std::getline(input, line)) {
    ++line_number;
    try {
      auto t = tokens(line);
      if (t.empty()) continue;
      if (t.size() >= 2 && t[1] == ":") {
        if (!label_name(t[0])) throw std::invalid_argument("invalid label");
        if (!labels.emplace(t[0], program.size()).second) throw std::invalid_argument("duplicate label");
        t.erase(t.begin(), t.begin() + 2);
        if (t.empty()) continue;
      }
      if (program.size() >= instruction_limit) throw std::invalid_argument("program exceeds 512 instructions");
      Instruction ins;
      ins.line = line_number;
      ins.text = line.substr(0, line.find('#'));
      const auto op = upper(t[0]);
      if (op == "HALT") {
        if (t.size() != 1) throw std::invalid_argument("HALT takes no arguments");
        ins.op = Op::Halt;
      } else if (op == "OUT") {
        if (t.size() != 2) throw std::invalid_argument("expected OUT Rn");
        ins.op = Op::Out; ins.reg = parse_register(t[1]);
      } else if (op == "JMP" || op == "JZ" || op == "JNZ") {
        if (t.size() != 2 || !label_name(t[1])) throw std::invalid_argument("expected jump label");
        ins.op = op == "JMP" ? Op::Jmp : op == "JZ" ? Op::Jz : Op::Jnz;
        ins.label = t[1];
      } else if (op == "LOAD") {
        if (t.size() != 6 || t[2] != "," || t[3] != "[" || t[5] != "]")
          throw std::invalid_argument("expected LOAD Rn, [address]");
        ins.op = Op::Load; ins.reg = parse_register(t[1]); ins.argument = parse_operand(t[4]);
        if (!ins.argument.is_register && ins.argument.value >= ram_size)
          throw std::invalid_argument("RAM address must be 0..15");
      } else if (op == "STORE") {
        if (t.size() != 6 || t[1] != "[" || t[3] != "]" || t[4] != ",")
          throw std::invalid_argument("expected STORE [address], Rn");
        ins.op = Op::Store; ins.reg = parse_register(t[5]); ins.argument = parse_operand(t[2]);
        if (!ins.argument.is_register && ins.argument.value >= ram_size)
          throw std::invalid_argument("RAM address must be 0..15");
      } else {
        if (op == "MOV") ins.op = Op::Mov;
        else if (op == "ADD") ins.op = Op::Add;
        else if (op == "SUB") ins.op = Op::Sub;
        else if (op == "XOR") ins.op = Op::Xor;
        else if (op == "CMP") ins.op = Op::Cmp;
        else throw std::invalid_argument("unknown instruction: " + t[0]);
        if (t.size() != 4 || t[2] != ",") throw std::invalid_argument("expected OP Rn, byte-or-register");
        ins.reg = parse_register(t[1]); ins.argument = parse_operand(t[3]);
      }
      program.push_back(ins);
    } catch (const std::exception& e) {
      throw std::invalid_argument("line " + std::to_string(line_number) + ": " + e.what());
    }
  }
  if (program.empty()) throw std::invalid_argument("empty program");
  for (auto& ins : program) if (!ins.label.empty()) {
    const auto it = labels.find(ins.label);
    if (it == labels.end() || it->second >= program.size())
      throw std::invalid_argument("line " + std::to_string(ins.line) + ": undefined executable label " + ins.label);
    ins.target = it->second;
  }
  return program;
}

struct Snapshot {
  std::array<unsigned, register_count> registers{};
  std::array<unsigned, ram_size> ram{};
  std::vector<unsigned> output;
  std::size_t pc = 0;
  bool zero = false, carry = false, halted = false;
  bool operator==(const Snapshot& other) const {
    return registers == other.registers && ram == other.ram && output == other.output &&
           pc == other.pc && zero == other.zero && carry == other.carry && halted == other.halted;
  }
};

class DataPath {
 public:
  DataPath() : core_(qubit_count) {}
  bool bit(std::size_t q) const { return core_.computational_basis_bit(q); }
  void x(std::size_t q) { core_.pauli_x(q); ++gate_x; }
  void cx(std::size_t a, std::size_t b) { core_.computational_basis_cnot(a, b); ++gate_cx; }
  void ccx_basis(std::size_t a, std::size_t b, std::size_t target) {
    if (a == b || a == target || b == target) throw std::logic_error("CCX requires distinct wires");
    ++gate_ccx;
    // Both controls are read from the actual canonical CompactCore state.
    // Count this logical basis-only CCX separately from direct X requests.
    if (bit(a) && bit(b)) core_.pauli_x(target);
  }
  void clear(std::size_t start, std::size_t count = 8) {
    for (std::size_t i = 0; i < count; ++i) if (bit(start + i)) x(start + i);
  }
  unsigned word(std::size_t start) const {
    unsigned result = 0;
    for (unsigned i = 0; i < 8; ++i) if (bit(start + i)) result |= 1u << i;
    return result;
  }
  void immediate(std::size_t start, unsigned value) {
    clear(start);
    for (unsigned i = 0; i < 8; ++i) if ((value >> i) & 1u) x(start + i);
  }
  void copy(std::size_t to, std::size_t from) {
    if (to == from) return;
    clear(to);
    for (unsigned i = 0; i < 8; ++i) cx(from + i, to + i);
  }
  void operand(std::size_t to, const Operand& value) {
    if (value.is_register) copy(to, value.value * 8);
    else immediate(to, value.value);
  }
  std::pair<bool, bool> arithmetic(unsigned reg, Operand rhs, bool subtract, bool commit = true) {
    // Latch both operands in core scratch before modifying the destination;
    // this also handles ADD/SUB/CMP Rn,Rn correctly.
    copy(scratch_a, reg * 8);
    operand(scratch_b, rhs);
    clear(scratch_c, 9);
    if (subtract) {
      for (unsigned i = 0; i < 8; ++i) x(scratch_b + i);
      x(scratch_c);  // a + ~b + 1: carry-out means no borrow.
    }
    for (unsigned i = 0; i < 8; ++i) {
      const auto a = scratch_a + i, b = scratch_b + i;
      const auto c = scratch_c + i, next = c + 1;
      // Majority(a,b,c) = ab XOR ac XOR bc; next starts at zero.
      ccx_basis(a, b, next); ccx_basis(a, c, next); ccx_basis(b, c, next);
      cx(b, a); cx(c, a);
    }
    bool zero = true;
    for (unsigned i = 0; i < 8; ++i) if (bit(scratch_a + i)) zero = false;
    const bool carry = bit(scratch_c + 8);
    if (commit) copy(reg * 8, scratch_a);
    clear(scratch_a, 25);
    return {zero, carry};
  }
  bool xor_word(unsigned reg, Operand rhs) {
    operand(scratch_b, rhs);
    for (unsigned i = 0; i < 8; ++i) cx(scratch_b + i, reg * 8 + i);
    clear(scratch_b);
    return word(reg * 8) == 0;
  }
  bool scratch_clean() const {
    for (std::size_t q = scratch_a; q < qubit_count; ++q) if (bit(q)) return false;
    return true;
  }
  std::size_t state_bytes() const { return core_.state_bytes(); }
  std::uint64_t gate_x = 0, gate_cx = 0, gate_ccx = 0;
 private:
  CompactCore core_;
};

class Machine {
 public:
  DataPath data;
  std::size_t pc = 0;
  bool zero = false, carry = false, halted = false;
  std::vector<unsigned> output;
  unsigned address(Operand op) const {
    const unsigned value = op.is_register ? data.word(op.value * 8) : op.value;
    if (value >= ram_size) throw std::out_of_range("RAM address outside 0..15");
    return value;
  }
  void step(const std::vector<Instruction>& program) {
    if (halted) throw std::logic_error("machine already halted");
    if (pc >= program.size()) throw std::out_of_range("PC left program without HALT");
    const auto& ins = program[pc];
    std::size_t next = pc + 1;
    switch (ins.op) {
      case Op::Mov: data.operand(ins.reg * 8, ins.argument); break;
      case Op::Add: case Op::Sub: case Op::Cmp: {
        const auto flags = data.arithmetic(ins.reg, ins.argument, ins.op != Op::Add, ins.op != Op::Cmp);
        zero = flags.first; carry = flags.second; break;
      }
      case Op::Xor: zero = data.xor_word(ins.reg, ins.argument); carry = false; break;
      case Op::Load: { const auto a = address(ins.argument); data.copy(ins.reg * 8, 32 + a * 8); break; }
      case Op::Store: { const auto a = address(ins.argument); data.copy(32 + a * 8, ins.reg * 8); break; }
      case Op::Jmp: next = ins.target; break;
      case Op::Jz: if (zero) next = ins.target; break;
      case Op::Jnz: if (!zero) next = ins.target; break;
      case Op::Out:
        if (output.size() >= output_limit) throw LimitError("output limit of 256 values reached");
        output.push_back(data.word(ins.reg * 8)); break;
      case Op::Halt: halted = true; break;
    }
    pc = next;
  }
  Snapshot snapshot() const {
    Snapshot s;
    for (unsigned r = 0; r < register_count; ++r) s.registers[r] = data.word(r * 8);
    for (unsigned a = 0; a < ram_size; ++a) s.ram[a] = data.word(32 + a * 8);
    s.pc = pc; s.zero = zero; s.carry = carry; s.halted = halted; s.output = output;
    return s;
  }
};

// Independent scalar interpreter: no gate ALU, scratch state or Core helpers.
// Shared syntax/decoded instructions are deliberately not an independent
// parser test; dedicated malformed-source tests cover that boundary.
class ScalarReference {
 public:
  Snapshot state;
  unsigned value(Operand a) const { return a.is_register ? state.registers[a.value] : a.value; }
  void step(const std::vector<Instruction>& program) {
    if (state.halted) throw std::logic_error("machine already halted");
    if (state.pc >= program.size()) throw std::out_of_range("PC left program without HALT");
    const auto& ins = program[state.pc];
    auto& dst = state.registers[ins.reg];
    std::size_t next = state.pc + 1;
    switch (ins.op) {
      case Op::Mov: dst = value(ins.argument); break;
      case Op::Add: {
        const unsigned sum = dst + value(ins.argument);
        dst = sum & 255u; state.zero = dst == 0; state.carry = sum > 255; break;
      }
      case Op::Sub: case Op::Cmp: {
        const unsigned rhs = value(ins.argument), before = dst;
        const unsigned result = (before + 256u - rhs) & 255u;
        state.zero = result == 0; state.carry = before >= rhs;
        if (ins.op == Op::Sub) dst = result;
        break;
      }
      case Op::Xor: dst ^= value(ins.argument); state.zero = dst == 0; state.carry = false; break;
      case Op::Load: {
        const auto a = value(ins.argument);
        if (a >= ram_size) throw std::out_of_range("RAM address outside 0..15");
        dst = state.ram[a]; break;
      }
      case Op::Store: {
        const auto a = value(ins.argument);
        if (a >= ram_size) throw std::out_of_range("RAM address outside 0..15");
        state.ram[a] = dst; break;
      }
      case Op::Jmp: next = ins.target; break;
      case Op::Jz: if (state.zero) next = ins.target; break;
      case Op::Jnz: if (!state.zero) next = ins.target; break;
      case Op::Out:
        if (state.output.size() >= output_limit) throw LimitError("output limit of 256 values reached");
        state.output.push_back(dst); break;
      case Op::Halt: state.halted = true; break;
    }
    state.pc = next;
  }
};

struct RunResult {
  std::string status = "ERROR", message, trace;
  Snapshot final_state;
  std::size_t steps = 0, core_state_bytes = 0;
  std::uint64_t gate_x = 0, gate_cx = 0, gate_ccx = 0;
  double elapsed_us = 0;
  bool reference_ok = false;
};

inline RunResult execute(const std::string& source, int max_steps = 1000) {
  RunResult out;
  try {
    if (max_steps < 1 || max_steps > 10000) throw std::invalid_argument("max_steps must be 1..10000");
    const auto program = parse(source);
    Machine machine;
    // Audit snapshots are never fed back into the VM. They enable an entirely
    // separate, untimed scalar pass and comparison after EVERY instruction.
    std::vector<Snapshot> history;
    history.reserve(static_cast<std::size_t>(max_steps));
    std::ostringstream trace;
    std::string execution_error;
    bool output_limited = false;
    const auto begin = std::chrono::steady_clock::now();
    while (!machine.halted && out.steps < static_cast<std::size_t>(max_steps)) {
      const auto before = machine.pc;
      try { machine.step(program); }
      catch (const LimitError& e) { execution_error = e.what(); output_limited = true; break; }
      catch (const std::exception& e) { execution_error = e.what(); break; }
      ++out.steps;
      history.push_back(machine.snapshot());
      if (!machine.data.scratch_clean()) throw std::logic_error("scratch cleanup invariant failed");
      if (out.steps <= trace_limit) {
        const auto& s = history.back();
        const auto& instruction_text = program[before].text;
        trace << out.steps << ": PC=" << before << " " << instruction_text.substr(0, trace_instruction_limit)
              << (instruction_text.size() > trace_instruction_limit ? "..." : "")
              << " -> [" << s.registers[0] << ',' << s.registers[1] << ','
              << s.registers[2] << ',' << s.registers[3] << "] Z=" << s.zero << " C=" << s.carry << '\n';
      }
    }
    out.elapsed_us = std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now() - begin).count();
    out.final_state = machine.snapshot();
    out.core_state_bytes = machine.data.state_bytes();
    out.gate_x = machine.data.gate_x; out.gate_cx = machine.data.gate_cx; out.gate_ccx = machine.data.gate_ccx;
    if (out.steps > trace_limit) trace << "Trace limited to first 64 steps.\n";
    out.trace = trace.str();
    out.status = machine.halted ? "PASS" : !execution_error.empty() ? (output_limited ? "LIMIT" : "ERROR") : "LIMIT";
    out.message = machine.halted ? "HALT reached; every step matched the scalar reference." :
                  !execution_error.empty() ? execution_error : "step budget reached";
    ScalarReference reference;
    for (std::size_t i = 0; i < history.size(); ++i) {
      reference.step(program);
      if (!(reference.state == history[i])) {
        out.status = "MISMATCH"; out.message = "reference mismatch after step " + std::to_string(i + 1); return out;
      }
    }
    if (!execution_error.empty()) {
      std::string reference_error;
      try { reference.step(program); }
      catch (const std::exception& e) { reference_error = e.what(); }
      if (reference_error != execution_error || !(reference.state == out.final_state)) {
        out.status = "MISMATCH"; out.message = "reference disagrees on execution error"; return out;
      }
    }
    out.reference_ok = true;
  } catch (const std::exception& e) {
    out.status = "ERROR"; out.message = e.what();
  }
  return out;
}

inline std::string json_quote(const std::string& s) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (c == '\t') out << "\\t";
    else if (c < 32) out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<unsigned>(c) << std::dec;
    else out << static_cast<char>(c);
  }
  out << '"';
  return out.str();
}
template<class Values> inline void json_array(std::ostream& out, const Values& values) {
  out << '[';
  bool comma = false;
  for (auto value : values) { if (comma) out << ','; comma = true; out << value; }
  out << ']';
}
inline std::string run_json(const std::string& source, int max_steps = 1000) {
  const auto result = execute(source, max_steps);
  const auto& s = result.final_state;
  std::ostringstream out;
  out << std::boolalpha << std::setprecision(12)
      << "{\"status\":" << json_quote(result.status) << ",\"message\":" << json_quote(result.message)
      << ",\"halted\":" << s.halted << ",\"steps\":" << result.steps << ",\"pc\":" << s.pc
      << ",\"registers\":"; json_array(out, s.registers);
  out << ",\"ram\":"; json_array(out, s.ram);
  out << ",\"output\":"; json_array(out, s.output);
  out << ",\"zero\":" << s.zero << ",\"carry\":" << s.carry
      << ",\"core_state_bytes\":" << result.core_state_bytes << ",\"core_qubits\":" << qubit_count
      << ",\"gate_x\":" << result.gate_x << ",\"gate_cx\":" << result.gate_cx << ",\"gate_ccx\":" << result.gate_ccx
      << ",\"elapsed_us\":" << result.elapsed_us << ",\"reference_ok\":" << result.reference_ok
      << ",\"trace\":" << json_quote(result.trace) << '}';
  return out.str();
}

inline std::string example_source(int example) {
  if (example == 0) return
      "# Suma de 1 a 10 y lectura/escritura de RAM\n"
      "MOV R0, 0\nMOV R1, 1\nloop:\nADD R0, R1\nADD R1, 1\nCMP R1, 11\nJNZ loop\n"
      "STORE [0], R0\nLOAD R2, [0]\nOUT R2\nHALT\n";
  if (example == 1) return
      "# Primeros diez numeros de Fibonacci\n"
      "MOV R0, 0\nMOV R1, 1\nMOV R2, 10\nloop:\nOUT R0\nMOV R3, R0\n"
      "ADD R3, R1\nMOV R0, R1\nMOV R1, R3\nSUB R2, 1\nJNZ loop\nHALT\n";
  if (example == 2) return
      "# Llenar RAM[0..7] y copiar a RAM[8..15]\n"
      "MOV R0, 0\nMOV R1, 10\nfill:\nSTORE [R0], R1\nADD R1, 1\nADD R0, 1\n"
      "CMP R0, 8\nJNZ fill\nMOV R0, 0\nMOV R1, 8\ncopy:\nLOAD R2, [R0]\n"
      "STORE [R1], R2\nOUT R2\nADD R0, 1\nADD R1, 1\nCMP R0, 8\nJNZ copy\nHALT\n";
  throw std::invalid_argument("example must be 0, 1 or 2");
}

inline std::string self_test_json() {
  std::size_t total = 0, passed = 0;
  std::string failure;
  auto check = [&](bool ok, const char* name) {
    ++total;
    if (ok) ++passed;
    else if (failure.empty()) failure = name;
  };
  for (int i = 0; i < 3; ++i) {
    const auto r = execute(example_source(i));
    check(r.status == "PASS" && r.reference_ok, "example reference");
    if (i == 0) check(r.final_state.output == std::vector<unsigned>{55} && r.final_state.ram[0] == 55, "sum");
    if (i == 1) check(r.final_state.output == std::vector<unsigned>({0,1,1,2,3,5,8,13,21,34}), "Fibonacci");
    if (i == 2) {
      bool good = true;
      for (unsigned j = 0; j < 8; ++j) good = good && r.final_state.ram[j] == 10 + j && r.final_state.ram[j + 8] == 10 + j;
      check(good, "memory copy");
    }
  }
  const std::vector<std::string> programs = {
      "MOV R0,255\nADD R0,1\nHALT", "MOV R0,0\nSUB R0,1\nHALT",
      "MOV R0,128\nADD R0,R0\nHALT", "MOV R0,77\nSUB R0,R0\nHALT",
      "MOV R0,91\nXOR R0,R0\nHALT", "MOV R0,19\nCMP R0,R0\nJZ yes\nOUT R0\nyes: HALT",
      "MOV R0,3\nSTORE [R0],R0\nLOAD R0,[R0]\nHALT",
      "JMP end\nOUT R0\nend: HALT", "MOV R0,0xFE\nXOR R0,0xF0\nHALT"};
  for (const auto& p : programs) { const auto r = execute(p); check(r.status == "PASS" && r.reference_ok, "instruction check"); }
  for (const auto& p : {"MOV R0,256", "MOV R4,1", "MOV R0,1junk", "JMP absent", "LOAD R0,[16]", "HALT extra"})
    check(execute(p).status == "ERROR", "invalid input rejected");
  const auto loop = execute("loop: JMP loop", 5);
  check(loop.status == "LIMIT" && loop.steps == 5 && loop.reference_ok, "step budget");
  const auto bounds = execute("MOV R0,16\nLOAD R1,[R0]\nHALT");
  check(bounds.status == "ERROR" && bounds.reference_ok, "runtime memory bounds");
  return "{\"status\":" + json_quote(passed == total ? "PASS" : "MISMATCH") +
         ",\"message\":" + json_quote(failure.empty() ? "All bounded VM self-tests passed." : failure) +
         ",\"passed\":" + std::to_string(passed) + ",\"total\":" + std::to_string(total) + "}";
}

}}  // namespace mundo_x::basis_vm
