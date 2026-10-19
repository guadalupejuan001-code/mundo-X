#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/adaptive_phase_core.hpp"
#include "mundo_x/mxlang.hpp"
#include "mundo_x/xasm.hpp"

namespace {
int checks = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
mundo_x::XasmRunResult run(const std::string& source) {
  std::istringstream input(source);
  return mundo_x::run_xasm(input);
}
std::string compile(const std::string& source) {
  std::istringstream input(source);
  return mundo_x::compile_mx(input).xasm;
}

void typed_circuits_and_visual_measurement() {
  const auto bell = compile(
      "backend compact; qubits 2;\n"
      "circuit bell(a: qubit, b: qubit) { h(a); cx(a, b); }\n"
      "bell(q[0], q[1]); assert ghz;\n");
  const std::string seed = "seed 18446744073709551615\n";
  const std::string measurements = "measure 0\nmeasure 1\n";
  const auto plain = run(bell + seed + measurements);
  const auto visual = run(bell + seed +
      "scene 1024 -2026\nscene_probz 13 0\nmeasure 0\n"
      "scene_probz 14 1\nmeasure 1\n");
  require(plain.output == visual.output,
          "visual probability snapshots consumed RNG or changed typed circuit state");
  const auto scene = mundo_x::VisualScene::import_text(visual.scene_text);
  require(scene.count() == 1024 && scene.edit_state(13) == 3,
          "compiled Bell marginal did not reach the visual scene");
  const bool one = visual.output.find("MEASURE_Z q=0 outcome=1") != std::string::npos;
  require(scene.edit_state(14) == (one ? 6U : 0U),
          "conditional Bell marginal was not preserved after measurement");
  require(visual.instructions == plain.instructions && visual.visual_instructions == 2,
          "quantum and visual operations lost independent accounting");

  const auto reduced = compile(
      "backend reduced; qubits 2; h(q[0]); t(q[0]); cx(q[0], q[1]);\n"
      "assert fidelity; assert norm;\n");
  const auto reduced_scene = run(reduced +
      "scene 65537 -2147483648\nscene_edit 65536 7\nscene_info\n");
  require(reduced_scene.backend == "reduced" && reduced_scene.t_gates == 1 &&
              reduced_scene.assertions_passed && reduced_scene.scene_objects == 65537,
          "reduced typed circuits and scene commands did not coexist");
  require(mundo_x::VisualScene::import_text(reduced_scene.scene_text).edit_state(65536) == 7,
          "scene export lost its high object ID beside the reduced backend");

  // This command deliberately keeps its exact stabilizer 0, 1/2, 1 mapping.
  // An unsupported backend must be reported, never dereferenced as CompactCore.
  bool rejected = false;
  try { run(reduced + "scene 8 0\nscene_probz 0 0\n"); }
  catch (const std::exception& error) {
    rejected = std::string(error.what()).find("backend compact") != std::string::npos;
  }
  require(rejected, "unsupported reduced scene_probz did not explain its backend requirement");
}

void phase_import_keeps_fp64_reference() {
  using Complex = std::complex<double>;
  using Pair = mundo_x::ReducedExactTRepresentation::ProductPair;
  const std::vector<Pair> product{
      Pair{Complex{0.123456789012345, 0.234567890123456},
           Complex{-0.345678901234567, 0.456789012345678}},
      Pair{Complex{0.567890123456789, -0.678901234567890},
           Complex{0.789012345678901, 0.890123456789012}}};
  mundo_x::DenseCore64 dense(2);
  dense.initialize_product(product);
  for (std::size_t i = 0; i < 4; ++i) {
    const auto expected = product[0][i & 1] * product[1][(i >> 1) & 1];
    require(std::abs(dense.state()[i] - expected) < 1e-14,
            "product initializer silently truncated FP64 amplitudes to float");
  }
  const auto before = dense.state();
  const auto factor = std::polar(1.0, 0.381234567890123);
  dense.phase(0, 0.381234567890123);
  for (std::size_t i = 0; i < 4; ++i)
    require(std::abs(dense.state()[i] - before[i] * ((i & 1) ? factor : Complex{1, 0})) < 1e-14,
            "general phase silently truncated FP64 rotation to float");

  auto reduced = mundo_x::ReducedExactTRepresentation::from_product(product);
  reduced.hadamard(0);
  reduced.phase(0, 0.381234567890123);
  reduced.cnot(0, 1);
  reduced.controlled_phase(1, 0, -0.227123456789012);
  const auto actual = reduced.dense_from_reduced();
  const auto reference = reduced.dense_reference();
  for (std::size_t i = 0; i < actual.size(); ++i)
    require(std::abs(actual[i] - reference[i]) < 1e-12,
            "phase branch restored the obsolete FP32 reduced reference");
}
}  // namespace

int main() {
  try {
    typed_circuits_and_visual_measurement();
    phase_import_keeps_fp64_reference();
    std::cout << "integrated runtime checks=" << checks << " failures=0\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
