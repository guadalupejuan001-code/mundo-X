#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mundo_x/mxlang.hpp"
#include "mundo_x/xasm.hpp"

namespace {

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

mundo_x::MxCompiledProgram compile(const std::string& source) {
  std::istringstream input(source);
  return mundo_x::compile_mx(input);
}

mundo_x::XasmRunResult run(const std::string& source) {
  std::istringstream input(source);
  return mundo_x::run_xasm(input);
}

void rejects(const std::string& source) {
  try {
    compile(source);
  } catch (const std::exception& error) {
    require(std::string(error.what()).find("line ") != std::string::npos,
            "diagnostic must include a source location: " +
                std::string(error.what()));
    return;
  }
  throw std::runtime_error("source should not compile: " + source.substr(0, 160));
}

void test_manual_expansion() {
  const auto program = compile(
      "backend compact; let N = 2 + 2; qubits N;\n"
      "circuit link(a: qubit, b: qubit) { cx(a, b); }\n"
      "h(q[0]); repeat N - 1 as i { link(q[i], q[i + 1]); }\n"
      "show; bytes; assert ghz;\n");
  const std::string manual = "backend compact\nqubits 4\nh 0\ncx 0 1\n"
                             "cx 1 2\ncx 2 3\nshow\nbytes\nassert ghz\n";
  require(program.xasm == manual, "GHZ expansion differs from handwritten XASM");
  require(program.backend == "compact" && program.qubits == 4 &&
              program.instructions == 4 && program.t_gates == 0,
          "compiled program metrics");
  const auto actual = run(program.xasm);
  const auto expected = run(manual);
  require(actual.assertions_passed && actual.output == expected.output &&
              actual.state_bytes == expected.state_bytes,
          "compiled GHZ execution differs from handwritten circuit");
}

void test_reduced_expansion() {
  const auto program = compile(
      "backend reduced; qubits 2;\n"
      "circuit layer(a: qubit, b: qubit) { h(a); t(a); cx(a,b); s(b); }\n"
      "repeat 3 { layer(q[0], q[1]); }\n"
      "cz(q[0],q[1]); assert fidelity; assert norm; show; bytes;\n");
  std::string manual = "backend reduced\nqubits 2\n";
  for (int i = 0; i < 3; ++i) manual += "h 0\nt 0\ncx 0 1\ns 1\n";
  manual += "cz 0 1\nassert fidelity\nassert norm\nshow\nbytes\n";
  require(program.xasm == manual && program.instructions == 13 &&
              program.t_gates == 3,
          "repeated Clifford+T circuit lowering");
  const auto actual = run(program.xasm);
  const auto expected = run(manual);
  require(actual.assertions_passed && actual.output == expected.output &&
              actual.components == expected.components,
          "reduced execution differs from handwritten circuit");
}

void test_scopes_and_types() {
  const auto lexical = compile(
      "qubits 2; let target = q[0];\n"
      "circuit flip(a: qubit) { x(target); z(a); }\n"
      "repeat 1 { let target = q[1]; flip(q[1]); }\n");
  require(lexical.xasm == "backend compact\nqubits 2\nx 0\nz 1\n",
          "circuit must capture its lexical globals, not caller locals");
  const auto typed = compile(
      "qubits 2; circuit flips(a: qubit, times: count) { repeat times { x(a); } }\n"
      "let a = q[(1 + 2) * 0]; flips(a, 2); repeat 0 { h(q[1]); }\n");
  require(typed.xasm == "backend compact\nqubits 2\nx 0\nx 0\n",
          "typed parameters, arithmetic precedence and zero repeat");
  for (const auto& source : {
           "qubits 1; h(0);", "qubits 1; let a=q[0]; repeat a {}",
           "qubits 1; let a=q[0]; let b=a+1;", "qubits 1; h(missing);",
           "qubits 1; repeat 1 as i {} h(q[i]);",
           "qubits 1; let n=1; let n=2;",
           "qubits 1; circuit f(a: qubit) { h(a); } f(0);",
           "qubits 1; circuit f(a: qubit) { h(a); } f();",
           "qubits 1; circuit f(a: qubit) { h(missing); }",
           "qubits 1; circuit f(a: qubit) { g(a); } circuit g(a: qubit) { f(a); }",
           "qubits 1; repeat 1 { backend compact; }",
           "qubits 1; circuit f() { qubits 2; }",
       }) rejects(source);
}

void test_diagnostics_and_mapping() {
  const auto program = compile(
      "# positions include comments\nqubits 1;\n  h(q[0]); // first gate\n"
      "repeat 2 {\n    z(q[0]);\n}\n");
  require(program.source_map.size() ==
              static_cast<std::size_t>(std::count(program.xasm.begin(),
                                                  program.xasm.end(), '\n')),
          "one source location per emitted XASM line");
  require(program.source_map.size() == 5 &&
              program.source_map[1].line == 2 &&
              program.source_map[2].line == 3 &&
              program.source_map[2].column == 3 &&
              program.source_map[3].line == 5 &&
              program.source_map[4].line == 5,
          "expanded instructions retain original source positions");
  try {
    compile("qubits 1;\n  h(q[1]);\n");
    throw std::runtime_error("out-of-range qubit should be rejected");
  } catch (const std::exception& error) {
    require(std::string(error.what()).find("line 2:") != std::string::npos,
            "diagnostic must identify the original .mx line");
  }
  // Compilation must not evaluate assertions or construct the quantum state.
  const auto failed_assertion = compile("qubits 1; assert one;");
  require(!run(failed_assertion.xasm).assertions_passed,
          "assertions belong to execution, not compilation");
  require(compile("qubits 10000;").qubits == 10000,
          "compile-only compact limit should not allocate a tableau");
}

void test_arithmetic_and_backend_limits() {
  for (const auto& source : {
           "qubits 1; let a=18446744073709551616;",
           "qubits 1; let a=18446744073709551615+1;",
           "qubits 1; let a=18446744073709551615*2;",
           "qubits 1; let a=0-1;", "qubits 1; let a=-1;",
           "qubits 0;", "qubits 10001;", "backend reduced; qubits 13;",
           "qubits 1; t(q[0]);", "qubits 1; assert fidelity;",
           "backend reduced; qubits 1; assert one;",
           "qubits 2; cx(q[0],q[0]);", "qubits 2; cz(q[0],q[2]);",
           "qubits 1; h(q[0])", "qubits 1; repeat 2 { x(q[0]);",
       }) rejects(source);
  const auto max_t = compile("backend reduced; qubits 1; repeat 128 { t(q[0]); }");
  require(max_t.t_gates == 128 && max_t.instructions == 128,
          "128 expanded T gates must compile");
  rejects("backend reduced; qubits 1; repeat 129 { t(q[0]); }");
}

void test_expansion_budgets() {
  rejects("qubits 1; repeat 18446744073709551615 {}");
  rejects("qubits 1; repeat 1000 { repeat 1000 {} }");
  rejects("qubits 1; repeat 10001 { x(q[0]); }");
  rejects("qubits 1; repeat 10001 { show; }");
  rejects("qubits 1;" + std::string(1024 * 1024, ' '));
  std::string deep = "qubits 1; let n=";
  deep += std::string(80, '(') + "1" + std::string(80, ')') + ";";
  rejects(deep);
  std::string flat = "qubits 1; let n=1";
  for (int i = 0; i < 80; ++i) flat += "+1";
  rejects(flat + ";");
  std::string calls = "qubits 1;";
  for (int i = 0; i < 35; ++i) {
    calls += "circuit f" + std::to_string(i) + "() { ";
    calls += i == 34 ? "x(q[0]);" : "f" + std::to_string(i + 1) + "();";
    calls += " }";
  }
  rejects(calls + "f0();");
}

}  // namespace

int main() {
  const std::vector<std::pair<std::string, std::function<void()>>> tests{
      {"manual GHZ expansion", test_manual_expansion},
      {"reduced circuit expansion", test_reduced_expansion},
      {"lexical scopes and parameter types", test_scopes_and_types},
      {"source diagnostics and compile-only behavior", test_diagnostics_and_mapping},
      {"checked arithmetic and backend limits", test_arithmetic_and_backend_limits},
      {"bounded expansion", test_expansion_budgets},
  };
  std::size_t failures = 0;
  for (const auto& test : tests) {
    try {
      test.second();
      std::cout << "PASS " << test.first << '\n';
    } catch (const std::exception& error) {
      ++failures;
      std::cerr << "FAIL " << test.first << ": " << error.what() << '\n';
    }
  }
  return failures == 0 ? 0 : 1;
}
