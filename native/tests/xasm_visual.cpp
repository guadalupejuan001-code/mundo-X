#include <cstddef>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <streambuf>
#include <string>
#include <vector>

#include "mundo_x/visual_scene.hpp"
#include "mundo_x/xasm.hpp"

namespace {
std::size_t checks = 0;

void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

mundo_x::XasmRunResult run(const std::string& source) {
  std::istringstream input(source);
  return mundo_x::run_xasm(input);
}

void rejects(const std::string& source) {
  bool rejected = false;
  try { (void)run(source); }
  catch (const std::exception&) { rejected = true; }
  require(rejected, "invalid XASM was accepted: " + source);
}

class FailingInput : public std::streambuf {
 public:
  explicit FailingInput(const std::string& prefix) : prefix_(prefix) {}
  bool failure_triggered() const { return failed_; }

 protected:
  int_type underflow() override {
    if (offset_ == prefix_.size()) {
      failed_ = true;
      throw std::runtime_error("simulated input read failure");
    }
    return traits_type::to_int_type(prefix_[offset_]);
  }
  int_type uflow() override {
    const auto value = underflow();
    ++offset_;
    return value;
  }

 private:
  std::string prefix_;
  std::size_t offset_ = 0;
  bool failed_ = false;
};

void stream_completion() {
  const std::string prefix = "scene 2 0\nscene_edit 0 3\n";
  FailingInput buffer(prefix);
  std::istream input(&buffer);
  // Default istream behavior catches streambuf exceptions and sets badbit.
  // A parser checking only getline's return value could export this prefix.
  mundo_x::XasmRunResult result;
  bool rejected = false;
  try { result = mundo_x::run_xasm(input); }
  catch (const std::runtime_error&) { rejected = true; }
  require(buffer.failure_triggered() && input.bad() && !input.eof(),
          "stream regression must reach the injected read failure after a valid scene prefix");
  require(rejected && result.scene_text.empty(),
          "an input read failure must reject the program without returning its partial scene");

  const auto complete = run(prefix);
  const auto no_final_newline = run("scene 2 0\nscene_edit 0 3");
  require(no_final_newline.scene_text == complete.scene_text &&
              no_final_newline.scene_text ==
                  "MUNDOX_OBJECTS_V1\ncount=2\nseed=0\nversion=1\nedits=1\n0:3\n",
          "clean EOF after a final instruction must accept the complete scene without a terminal newline");
  require(no_final_newline.visual_instructions == 1 &&
              no_final_newline.scene_rule_bytes == 40,
          "the final instruction before clean EOF must execute exactly once");
}

void scene_only() {
  const auto empty = run("# Visual declarations need no quantum allocation.\n"
                         "scene 16 -7\nscene_info\n");
  require(empty.qubits == 0 && empty.state_bytes == 0 && empty.instructions == 0,
          "scene-only execution must not allocate or count a quantum state");
  require(empty.visual_instructions == 0 && empty.assertions_passed,
          "declaration and information query are not visual mutations");
  require(empty.scene_objects == 16 && empty.scene_rule_bytes == 32,
          "scene-only result must report logical rule size separately");
  require(empty.output == "SCENE objects=16 edits=0 logical_rule_bytes=32 version=0\n",
          "scene_info must describe the current scene");
  require(empty.scene_text == "MUNDOX_OBJECTS_V1\ncount=16\nseed=-7\n"
                              "version=0\nedits=0\n",
          "the final scene must be exported in the Android interchange format");
  const auto imported = mundo_x::VisualScene::import_text(empty.scene_text);
  require(imported.count() == 16 && imported.seed() == -7 && imported.edit_count() == 0,
          "scene-only export must be importable as a visual scene");

  const auto edits = run("scene 32 2026\nscene_edit 31 7\nscene_edit 0 3\n"
                         "scene_edit 31 7\nscene_info\nscene_edit 0 0\n"
                         "scene_info\nscene_clear\nscene_clear\nscene_info\n");
  require(edits.instructions == 0 && edits.visual_instructions == 6,
          "visual commands count independently, including valid no-op commands");
  require(edits.output ==
              "SCENE objects=32 edits=2 logical_rule_bytes=48 version=2\n"
              "SCENE objects=32 edits=1 logical_rule_bytes=40 version=3\n"
              "SCENE objects=32 edits=0 logical_rule_bytes=32 version=4\n",
          "edits, restores and clearing must report actual changes and logical size");
  const auto cleared = mundo_x::VisualScene::import_text(edits.scene_text);
  require(cleared.version() == 4 && cleared.edit_count() == 0 &&
              edits.scene_rule_bytes == 32,
          "final result must describe the cleared scene, not its peak size");

  const auto maximum_seed = run("scene 1 2147483647 # signed maximum\n");
  require(mundo_x::VisualScene::import_text(maximum_seed.scene_text).seed() == 2147483647,
          "the full positive signed scene seed must survive export");
}

void declaration_orders_and_legacy_output() {
  const std::string body = "seed 2026\nh 0\ncx 0 1\nassert ghz\n"
                           "probz 0\nshow\nbytes\nmeasure 0\nmeasure 1\n";
  const auto legacy = run("qubits 2\n" + body);
  const auto first = run("scene 8 -1\nqubits 2\n" + body);
  const auto second = run("qubits 2\nscene 8 -1\n" + body);
  require(first.output == legacy.output && second.output == legacy.output,
          "visual declarations must not change existing quantum text output");
  require(first.instructions == legacy.instructions && first.qubits == legacy.qubits &&
              first.state_bytes == legacy.state_bytes && first.assertions_passed &&
              second.instructions == legacy.instructions && second.assertions_passed,
          "both declaration orders preserve quantum accounting and assertions");
  require(first.scene_text == second.scene_text && first.visual_instructions == 0 &&
              second.visual_instructions == 0,
          "scene declarations may appear on either side of qubits");
  require(legacy.scene_text.empty() && legacy.scene_objects == 0 &&
              legacy.scene_rule_bytes == 0 && legacy.visual_instructions == 0,
          "legacy programs must not acquire a phantom visual scene");

  const auto after_gate = run("qubits 1\nx 0\nscene 1 0\nscene_probz 0 0\nassert one\n");
  require(after_gate.assertions_passed && after_gate.instructions == 1 &&
              mundo_x::VisualScene::import_text(after_gate.scene_text).edit_state(0) == 6,
          "a later scene declaration must retain already computed quantum state");
}

void probability_snapshots() {
  const auto mapping = run("scene 4 0\nqubits 3\nx 2\nh 1\n"
                           "scene_edit 0 7\nscene_probz 0 0\n"
                           "scene_probz 1 1\nscene_probz 2 2\nscene_info\n");
  const auto scene = mundo_x::VisualScene::import_text(mapping.scene_text);
  require(scene.edit_state(0) == 0 && scene.edit_state(1) == 3 &&
              scene.edit_state(2) == 6 && scene.edit_state(3) == 0,
          "Z probabilities 0, one half, and 1 must map to visual states 0, 3, and 6");
  require(scene.edit_count() == 2 && scene.version() == 4 &&
              mapping.visual_instructions == 4 && mapping.instructions == 2,
          "probability snapshots use sparse edits and independent accounting");
  require(mapping.output == "SCENE objects=4 edits=2 logical_rule_bytes=48 version=4\n",
          "probability mapping must be silent unless scene information is requested");

  // A query that measured/collapsed the state would destroy H-H interference.
  const auto interference = run("qubits 1\nscene 1 0\nh 0\nscene_probz 0 0\n"
                                "h 0\nprobz 0\nmeasure 0\n");
  require(interference.output ==
              "PROB_Z q=0 p0=1 p1=0\n"
              "MEASURE_Z q=0 outcome=0 probability=1 random=0\n",
          "a visual probability snapshot must not collapse a superposition");
  require(mundo_x::VisualScene::import_text(interference.scene_text).edit_state(0) == 3,
          "a visual snapshot must retain the sampled probability after later gates");

  const auto refreshed = run("qubits 1\nscene 2 0\nh 0\nscene_probz 0 0\n"
                            "h 0\nx 0\nscene_probz 0 0\nscene_probz 1 0\n"
                            "x 0\nscene_probz 0 0\n");
  const auto refresh_scene = mundo_x::VisualScene::import_text(refreshed.scene_text);
  require(refresh_scene.edit_state(0) == 0 && refresh_scene.edit_state(1) == 6 &&
              refresh_scene.edit_count() == 1 && refresh_scene.version() == 4,
          "explicit refresh must replace or remove only the addressed snapshot");
  require(refreshed.visual_instructions == 4 && refreshed.instructions == 4,
          "probability snapshots do not count as quantum gates");

  const auto failed_assertion = run("qubits 1\nscene 1 0\nassert one\n"
                                    "scene_probz 0 0\nx 0\nassert one\nscene_clear\n");
  require(!failed_assertion.assertions_passed && failed_assertion.output ==
              "ASSERT one FAIL\nASSERT one PASS\n",
          "visual operations must not clear or mask a failed quantum assertion");
}

std::string sampling_program(std::uint64_t seed, bool visual) {
  std::ostringstream program;
  program << "qubits 8\nseed " << seed << '\n';
  if (visual) program << "scene 16 -2026\n";
  for (unsigned q = 0; q < 8; ++q) {
    program << "h " << q << '\n';
    if (visual) program << "scene_probz " << q << ' ' << q << '\n';
  }
  program << "show\n";
  for (unsigned q = 0; q < 8; ++q) {
    program << "probz " << q << "\nmeasure " << q << '\n';
    if (visual) program << "scene_probz " << q + 8 << ' ' << q << '\n';
  }
  program << "show\n";
  return program.str();
}

void no_random_consumption() {
  bool observed_zero = false, observed_one = false;
  for (std::uint64_t seed = 0; seed < 32; ++seed) {
    const auto reference = run(sampling_program(seed, false));
    const auto visual = run(sampling_program(seed, true));
    require(visual.output == reference.output,
            "visual queries changed quantum generators, probabilities or seeded measurements");
    require(visual.instructions == 16 && visual.visual_instructions == 16 &&
                visual.qubits == reference.qubits && visual.state_bytes == reference.state_bytes,
            "mixed execution must keep quantum and visual operation counts separate");
    const auto scene = mundo_x::VisualScene::import_text(visual.scene_text);
    for (unsigned q = 0; q < 8; ++q) {
      const auto prefix = "MEASURE_Z q=" + std::to_string(q) + " outcome=";
      const bool one = reference.output.find(prefix + "1 probability=0.5 random=1\n")
                       != std::string::npos;
      const bool zero = reference.output.find(prefix + "0 probability=0.5 random=1\n")
                        != std::string::npos;
      require(one != zero, "every independent superposition must be measured randomly once");
      observed_one = observed_one || one;
      observed_zero = observed_zero || zero;
      require(scene.edit_state(q) == 3 && scene.edit_state(q + 8) == (one ? 6u : 0u),
              "premeasurement snapshots must persist and postmeasurement snapshots must agree");
    }
  }
  require(observed_zero && observed_one, "RNG-preservation checks must exercise both outcomes");
}

void million_object_sparse_scene() {
  const auto result = run("scene 1048576 -2147483648\n"
                          "scene_edit 1048575 7\nscene_edit 65536 6\n"
                          "scene_edit 65535 3\nscene_edit 0 1\nscene_info\n");
  require(result.scene_text ==
              "MUNDOX_OBJECTS_V1\ncount=1048576\nseed=-2147483648\nversion=4\n"
              "edits=4\n0:1\n65535:3\n65536:6\n1048575:7\n",
          "maximum population export must preserve sorted sparse IDs across 16-bit boundaries");
  require(result.scene_objects == 1048576 && result.scene_rule_bytes == 64 &&
              result.visual_instructions == 4 && result.state_bytes == 0,
          "a million scene objects with four edits must retain a 64-byte logical rule");
  const auto scene = mundo_x::VisualScene::import_text(result.scene_text);
  require(scene.count() == 1048576 && scene.edit_count() == 4 &&
              scene.edit_state(1048574) == 0 && scene.edit_state(1048575) == 7,
          "maximum population result must be independently importable without losing the last ID");

  std::ostringstream full;
  full << "scene 1048576 0\n";
  for (unsigned id = 0; id < 4096; ++id) full << "scene_edit " << id << " 1\n";
  const auto at_limit = run(full.str());
  require(at_limit.scene_rule_bytes == 32800 && at_limit.visual_instructions == 4096 &&
              mundo_x::VisualScene::import_text(at_limit.scene_text).edit_count() == 4096,
          "XASM must preserve all 4096 supported sparse exceptions");
  rejects(full.str() + "scene_edit 4096 1\n");
  const auto reuse = run(full.str() + "scene_edit 0 0\nscene_edit 1048575 7\n");
  const auto reused_scene = mundo_x::VisualScene::import_text(reuse.scene_text);
  require(reused_scene.edit_count() == 4096 && reused_scene.edit_state(0) == 0 &&
              reused_scene.edit_state(1048575) == 7,
          "removing an exception must make its sparse capacity reusable");
}

void invalid_programs() {
  const std::vector<std::string> declarations = {
      "", "# no declarations\n", "scene\n", "scene 1\n", "scene 0 0\n",
      "scene -1 0\n", "scene 1048577 0\n", "scene 4294967295 0\n",
      "scene 4294967296 0\n", "scene 18446744073709551616 0\n",
      "scene 1 -2147483649\n", "scene 1 2147483648\n", "scene 1 4294967295\n",
      "scene 1 seed\n", "scene 1 1x\n", "scene 1 0x1\n", "scene 1 0.5\n",
      "scene 1x 0\n", "scene 0x10 0\n", "scene 1 0 extra\n",
      "scene 1 0\nscene 2 1\n", "scene 1 0\nqubits 1\nscene 1 0\n",
      "scene 1 0\nqubits 1\nqubits 2\n",
      "scene_info\n", "scene_clear\n", "scene_edit 0 1\n", "scene_probz 0 0\n",
      "qubits 1\nscene_info\n", "qubits 1\nscene_clear\n",
      "qubits 1\nscene_edit 0 1\n", "qubits 1\nscene_probz 0 0\n",
      "scene 1 0\nscene_probz 0 0\n", "scene 1 0\nh 0\n",
      "scene 1 0\nmeasure 0\n", "scene 1 0\nprobz 0\n",
      "scene 1 0\nassert one\n", "scene 1 0\nseed 0\n",
      "scene 1 0\nunknown\n"};
  for (const auto& source : declarations) rejects(source);

  const std::string ready = "qubits 2\nscene 8 0\n";
  const std::vector<std::string> commands = {
      "scene_edit", "scene_edit 0", "scene_edit -1 1", "scene_edit 8 1",
      "scene_edit 4294967296 1", "scene_edit 0 -1", "scene_edit 0 8",
      "scene_edit 0 4294967296", "scene_edit 0x1 1", "scene_edit 0 1x",
      "scene_edit 0 1 extra", "scene_clear extra", "scene_info extra",
      "scene_probz", "scene_probz 0", "scene_probz -1 0", "scene_probz 8 0",
      "scene_probz 4294967296 0", "scene_probz 0 -1", "scene_probz 0 2",
      "scene_probz 0 18446744073709551616", "scene_probz 0 1x",
      "scene_probz 0 0 extra"};
  for (const auto& command : commands) rejects(ready + command + '\n');
}
}  // namespace

int main() {
  try {
    stream_completion();
    scene_only();
    declaration_orders_and_legacy_output();
    probability_snapshots();
    no_random_consumption();
    million_object_sparse_scene();
    invalid_programs();
    std::cout << "checks=" << checks << " failures=0\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL after " << checks << " checks: " << error.what() << '\n';
    return 1;
  }
}
