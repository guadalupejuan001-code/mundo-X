#include <chrono>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include "mundo_x/basis_vm.hpp"

namespace vm = mundo_x::basis_vm;
namespace {
std::size_t checks = 0;
void require(bool ok, const std::string& message) {
  ++checks;
  if (!ok) throw std::runtime_error(message);
}
template<class Fn> void rejects(Fn fn, const std::string& message) {
  bool rejected = false;
  try { fn(); } catch (const std::exception&) { rejected = true; }
  require(rejected, message);
}
vm::RunResult good(const std::string& source) {
  auto r = vm::execute(source);
  require(r.status == "PASS" && r.reference_ok && r.final_state.halted,
          "program failed: " + r.message + " source=" + source);
  return r;
}
void core_invariant_tests() {
  mundo_x::CompactCore core(5);
  core.pauli_x(0); core.phase_s(2); core.pauli_z(0);
  require(core.computational_basis_bit(0), "X with S/Z preserves basis invariant");
  core.computational_basis_cnot(0, 1);
  require(core.computational_basis_bit(0) && core.computational_basis_bit(1), "basis CNOT control/target");
  core.cnot(1, 2); core.cnot(2, 3); core.cnot(3, 4);
  rejects([&]{ core.computational_basis_bit(0); }, "raw CNOT must invalidate canonical reads");
  rejects([&]{ core.computational_basis_cnot(0, 1); }, "raw CNOT must invalidate basis CNOT");
  core.canonicalize_computational_basis();
  for (unsigned q = 0; q < 5; ++q) require(core.computational_basis_bit(q), "Gaussian basis restoration");
  core.hadamard(0);
  rejects([&]{ core.computational_basis_bit(0); }, "H invalidates basis read");
  rejects([&]{ core.canonicalize_computational_basis(); }, "superposition rejected");
  require(core.measure_z_forced(0, false), "forced non-deterministic measurement");
  rejects([&]{ core.computational_basis_bit(0); }, "measurement requires canonicalization");
  core.canonicalize_computational_basis();
  require(!core.computational_basis_bit(0), "measurement sign after restoration");
  require(!core.measure_z_forced(1, false), "deterministic measurement is unchanged");
  require(core.computational_basis_bit(1), "deterministic measurement preserves invariant");
  core.hadamard(2); core.hadamard(2);
  core.canonicalize_computational_basis();
  require(core.computational_basis_bit(2), "H H restores basis state");
  rejects([&]{ core.computational_basis_cnot(0, 0); }, "reject equal control/target");
  require(!core.computational_basis_bit(0), "failed gate leaves invariant valid");
  rejects([&]{ core.computational_basis_bit(5); }, "basis read bounds");
  mundo_x::CompactCore bell(2);
  bell.hadamard(0); bell.cnot(0, 1);
  rejects([&]{ bell.canonicalize_computational_basis(); }, "entangled state rejected");
  bell.measure_z_forced(1, true); bell.canonicalize_computational_basis();
  require(bell.computational_basis_bit(0) && bell.computational_basis_bit(1), "collapsed Bell basis");
  // Exercise pivot swaps, nontrivial mixed Z rows and signs over multiple words.
  mundo_x::CompactCore wide(70);
  std::array<bool, 70> expected{};
  std::mt19937 rng(77319);
  for (unsigned i = 0; i < 150; ++i) {
    const unsigned c = rng() % 70, t = (c + 1 + rng() % 69) % 70;
    if (i % 3 == 0) { wide.pauli_x(c); expected[c] = !expected[c]; }
    wide.cnot(c, t); expected[t] = expected[t] != expected[c];
  }
  wide.canonicalize_computational_basis();
  for (unsigned q = 0; q < 70; ++q) require(wide.computational_basis_bit(q) == expected[q], "wide randomized basis canonicalization");
}
void arithmetic_pair(vm::DataPath& data, unsigned a, unsigned b) {
  for (bool subtract : {false, true}) {
    data.immediate(0, a);
    data.immediate(8, b);
    const auto flags = data.arithmetic(0, {true, 1}, subtract);
    const unsigned expected = subtract ? (a + 256u - b) & 255u : (a + b) & 255u;
    require(data.word(0) == expected && data.word(8) == b && flags.first == (expected == 0) &&
            flags.second == (subtract ? a >= b : a + b > 255),
            "ALU mismatch a=" + std::to_string(a) + " b=" + std::to_string(b) + (subtract ? " SUB" : " ADD"));
    require(data.scratch_clean(), "ALU scratch must be clean");
  }
}
void arithmetic_tests(bool exhaustive) {
  vm::DataPath data;
  if (exhaustive) {
    for (unsigned a = 0; a < 256; ++a)
      for (unsigned b = 0; b < 256; ++b) arithmetic_pair(data, a, b);
  } else {
    for (unsigned a = 0; a < 32; ++a)
      for (unsigned b = 0; b < 32; ++b) arithmetic_pair(data, a, b);
    const unsigned boundary[] = {0,1,2,7,15,16,31,63,64,127,128,129,254,255};
    for (unsigned a : boundary) for (unsigned b : boundary) arithmetic_pair(data, a, b);
    std::mt19937 rng(188751);
    for (unsigned i = 0; i < 2048; ++i) arithmetic_pair(data, rng() % 256, rng() % 256);
  }
  for (unsigned a = 0; a < 256; ++a) {
    data.immediate(0, a);
    auto flags = data.arithmetic(0, {true, 0}, false);
    require(data.word(0) == ((a * 2) & 255u) && flags.second == (a >= 128), "ADD alias");
    data.immediate(0, a);
    flags = data.arithmetic(0, {true, 0}, true, false);
    require(data.word(0) == a && flags.first && flags.second, "CMP alias without commit");
    flags = data.arithmetic(0, {true, 0}, true);
    require(data.word(0) == 0 && flags.first && flags.second, "SUB alias");
    data.immediate(0, a);
    require(data.xor_word(0, {true, 0}) && data.word(0) == 0 && data.scratch_clean(), "XOR alias");
  }
}
void program_tests() {
  auto sum = good(vm::example_source(0));
  require(sum.final_state.output == std::vector<unsigned>{55} && sum.final_state.ram[0] == 55 &&
          sum.final_state.registers[2] == 55, "sum program output/storage");
  require(sum.core_state_bytes == 9065 && sum.gate_x && sum.gate_cx && sum.gate_ccx, "actual core storage and gates");
  auto fib = good(vm::example_source(1));
  require(fib.final_state.output == std::vector<unsigned>({0,1,1,2,3,5,8,13,21,34}), "Fibonacci output");
  auto mem = good(vm::example_source(2));
  for (unsigned i = 0; i < 8; ++i) require(mem.final_state.ram[i] == 10 + i && mem.final_state.ram[i+8] == 10 + i, "memory copy");
  auto flags = good("MOV R0,255\nADD R0,1\nMOV R1,9\nSTORE [15],R1\nLOAD R2,[15]\nMOV R2,R2\nHALT");
  require(flags.final_state.zero && flags.final_state.carry && flags.final_state.registers[2] == 9, "MOV/LOAD/STORE preserve flags");
  auto branches = good("JZ bad\nJNZ start\nbad: OUT R0\nstart: MOV R0,42\nCMP R0,42\nJNZ bad\nJZ end\nOUT R0\nend: HALT");
  require(branches.final_state.output.empty() && branches.final_state.registers[0] == 42, "branches and CMP preserve register");
  auto alias = good("MOV R0,3\nSTORE [R0],R0\nMOV R1,77\nSTORE [3],R1\nLOAD R0,[R0]\nHALT");
  require(alias.final_state.registers[0] == 77 && alias.final_state.ram[3] == 77, "address/destination alias");
  auto parsed = good("# comment\r\nentry: mov r3, 0xFf # inline\n xor R3,0X0F\n OUT r3\n halt\n");
  require(parsed.final_state.output == std::vector<unsigned>{240}, "strict valid syntax");
  auto xor_flags = good("MOV R0,255\nADD R0,1\nXOR R0,1\nHALT");
  require(!xor_flags.final_state.zero && !xor_flags.final_state.carry, "XOR clears carry");
  auto budget = vm::execute("loop: JMP loop", 5);
  require(budget.status == "LIMIT" && budget.steps == 5 && budget.reference_ok, "bounded infinite loop");
  auto bound = vm::execute("MOV R0,16\nSTORE [R0],R0\nHALT");
  require(bound.status == "ERROR" && bound.reference_ok && bound.steps == 1 && bound.final_state.pc == 1, "runtime address checked before mutation");
  auto falloff = vm::execute("MOV R0,9");
  require(falloff.status == "ERROR" && falloff.reference_ok && falloff.final_state.registers[0] == 9, "PC falloff");
  auto output = vm::execute("again: OUT R0\nJMP again", 1000);
  require(output.status == "LIMIT" && output.reference_ok && output.final_state.output.size() == 256 && output.steps == 512, "output cap");
  require(output.trace.find("Trace limited to first 64 steps.") != std::string::npos, "trace cap");
  for (const std::string s : {"", "# only comment", "HALT extra", "MOV R4,1", "MOV R00,1", "MOV R0,-1", "MOV R0,256",
       "MOV R0,+1", "MOV R0,0x", "MOV R0,0x100", "MOV R0,1junk", "MOV R0,1.0", "MOV R0 1", "MOV R0,1,2",
       "LOAD R0,[16]", "STORE [255],R0", "LOAD R0,[1", "STORE 1,R0", "JMP absent", "JMP R0", "OUT 1",
       "x: HALT\nx: HALT", "JMP end\nHALT\nend:", "1bad: HALT", "HALT\x01"}) {
    const auto r = vm::execute(s);
    require(r.status == "ERROR" && r.steps == 0 && !r.reference_ok, "malformed source rejected: " + s);
  }
  require(vm::execute(std::string(vm::source_limit + 1, ' ')).status == "ERROR", "source size cap");
  std::string long_program;
  for (unsigned i = 0; i < 513; ++i) long_program += "HALT\n";
  require(vm::execute(long_program).status == "ERROR", "instruction cap");
  require(vm::execute("HALT", 0).status == "ERROR" && vm::execute("HALT", 10001).status == "ERROR", "step argument bounds");
  rejects([]{ vm::example_source(3); }, "example bounds");
  require(vm::run_json("HALT").find("\"reference_ok\":true") != std::string::npos, "JSON reference field");
  require(vm::self_test_json().find("\"status\":\"PASS\"") != std::string::npos, "bounded mobile self-test");
}
}  // namespace
int main(int argc, char** argv) {
  const bool exhaustive = argc == 2 && std::string(argv[1]) == "--exhaustive";
  if (argc > 2 || (argc == 2 && !exhaustive)) { std::cerr << "usage: basis_vm_test [--exhaustive]\n"; return 2; }
  const auto start = std::chrono::steady_clock::now();
  try {
    core_invariant_tests(); arithmetic_tests(exhaustive); program_tests();
    std::cout << "PASS " << checks << " checks; " << (exhaustive ? "all 65536 operand pairs for ADD and SUB" : "32x32 pairs, boundary pairs, 2048 seeded pairs")
              << "; elapsed_ms=" << std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() << '\n';
  } catch (const std::exception& e) { std::cerr << "FAIL after " << checks << " checks: " << e.what() << '\n'; return 1; }
}
