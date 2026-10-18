#pragma once

#include <charconv>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/visual_scene.hpp"

namespace mundo_x {

struct XasmRunResult {
  std::size_t qubits = 0;
  std::size_t state_bytes = 0;
  std::size_t instructions = 0;
  std::size_t visual_instructions = 0;
  std::size_t scene_objects = 0;
  std::size_t scene_rule_bytes = 0;
  bool assertions_passed = true;
  std::string output;
  // Final scene only. Commands never write files or emit a partial export.
  std::string scene_text;
};

inline std::string trim(const std::string& s) {
  const auto first = s.find_first_not_of(" \t\r\n");
  if (first == std::string::npos) return "";
  const auto last = s.find_last_not_of(" \t\r\n");
  return s.substr(first, last - first + 1);
}

namespace xasm_detail {
template <typename UInt>
inline UInt read_unsigned(std::istringstream& input, std::size_t line,
                          const std::string& syntax) {
  std::string token;
  UInt value = 0;
  if (input >> token) {
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size())
      return value;
  }
  throw std::runtime_error("line " + std::to_string(line) + ": expected " + syntax);
}

inline void require_end(std::istringstream& input, std::size_t line) {
  std::string extra;
  if (input >> extra)
    throw std::runtime_error("line " + std::to_string(line) + ": unexpected argument '" + extra + "'");
}

inline std::int32_t read_scene_seed(std::istringstream& input, std::size_t line) {
  std::string token;
  std::int32_t value = 0;
  if (input >> token) {
    const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
    if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size()) return value;
  }
  throw std::runtime_error("line " + std::to_string(line) + ": expected scene <count> <int32 seed>");
}
}  // namespace xasm_detail

inline XasmRunResult run_xasm(std::istream& input) {
  XasmRunResult result;
  std::unique_ptr<CompactCore> core;
  std::unique_ptr<VisualScene> scene;
  std::ostringstream out;
  // Reproducible pseudorandom sampling, not a source of physical randomness.
  std::mt19937_64 random(0);

  std::string line;
  std::size_t line_number = 0;

  while (std::getline(input, line)) {
    ++line_number;

    const auto comment = line.find('#');
    if (comment != std::string::npos) line = line.substr(0, comment);
    line = trim(line);
    if (line.empty()) continue;

    std::istringstream in(line);
    std::string op;
    in >> op;

    if (op == "scene") {
      if (scene) throw std::runtime_error("line " + std::to_string(line_number) + ": scene already declared");
      const auto count = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene <count> <int32 seed>");
      const auto seed = xasm_detail::read_scene_seed(in, line_number);
      xasm_detail::require_end(in, line_number);
      scene = std::make_unique<VisualScene>(count, seed);
      continue;
    }

    if (op == "scene_edit" || op == "scene_clear" || op == "scene_info" || op == "scene_probz") {
      if (!scene) throw std::runtime_error("line " + std::to_string(line_number) + ": declare scene first");
      if (op == "scene_edit") {
        const auto id = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene_edit <id> <state 0..7>");
        const auto state = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene_edit <id> <state 0..7>");
        xasm_detail::require_end(in, line_number);
        scene->edit(id, state);
        ++result.visual_instructions;
      } else if (op == "scene_clear") {
        xasm_detail::require_end(in, line_number);
        scene->clear_edits();
        ++result.visual_instructions;
      } else if (op == "scene_info") {
        xasm_detail::require_end(in, line_number);
        out << "SCENE objects=" << scene->count() << " edits=" << scene->edit_count()
            << " logical_rule_bytes=" << scene->logical_rule_bytes()
            << " version=" << scene->version() << "\n";
      } else {
        const auto id = xasm_detail::read_unsigned<std::uint32_t>(in, line_number, "scene_probz <id> <q>");
        const auto q = xasm_detail::read_unsigned<std::size_t>(in, line_number, "scene_probz <id> <q>");
        xasm_detail::require_end(in, line_number);
        if (!core) throw std::runtime_error("line " + std::to_string(line_number) + ": scene_probz requires qubits");
        scene->edit_state(id);  // Validate the destination before the query.
        const double p1 = core->probability_one_z(q);
        // A snapshot of this marginal only: no measurement, RNG consumption,
        // continuous binding, or claim that heights encode the full state.
        std::uint32_t state;
        if (p1 == 0.0) state = 0;
        else if (p1 == 0.5) state = 3;
        else if (p1 == 1.0) state = 6;
        else throw std::runtime_error("unsupported stabilizer probability for scene_probz");
        scene->edit(id, state);
        ++result.visual_instructions;
      }
      continue;
    }

    if (op == "qubits") {
      if (core) throw std::runtime_error("line " + std::to_string(line_number) + ": qubits already declared");
      const auto n = xasm_detail::read_unsigned<std::size_t>(in, line_number, "qubits <n>");
      xasm_detail::require_end(in, line_number);
      if (n == 0) {
        throw std::runtime_error("line " + std::to_string(line_number) + ": expected qubits <n>");
      }
      core = std::make_unique<CompactCore>(n);
      result.qubits = n;
      continue;
    }

    if (!core) {
      throw std::runtime_error("line " + std::to_string(line_number) + ": declare qubits first");
    }

    if (op == "h" || op == "x" || op == "z" || op == "s") {
      const auto q = xasm_detail::read_unsigned<std::size_t>(in, line_number, op + " <q>");
      xasm_detail::require_end(in, line_number);
      if (op == "h") core->hadamard(q);
      else if (op == "x") core->pauli_x(q);
      else if (op == "z") core->pauli_z(q);
      else core->phase_s(q);
      ++result.instructions;
    } else if (op == "cx") {
      const auto c = xasm_detail::read_unsigned<std::size_t>(in, line_number, "cx <control> <target>");
      const auto t = xasm_detail::read_unsigned<std::size_t>(in, line_number, "cx <control> <target>");
      xasm_detail::require_end(in, line_number);
      core->cnot(c, t);
      ++result.instructions;
    } else if (op == "seed") {
      const auto seed = xasm_detail::read_unsigned<std::uint64_t>(in, line_number, "seed <uint64>");
      xasm_detail::require_end(in, line_number);
      random.seed(seed);
    } else if (op == "probz") {
      const auto q = xasm_detail::read_unsigned<std::size_t>(in, line_number, "probz <q>");
      xasm_detail::require_end(in, line_number);
      const auto p1 = core->probability_one_z(q);
      out << "PROB_Z q=" << q << " p0=" << (1.0 - p1) << " p1=" << p1 << "\n";
    } else if (op == "measure") {
      const auto q = xasm_detail::read_unsigned<std::size_t>(in, line_number, "measure <q>");
      xasm_detail::require_end(in, line_number);
      const auto measured = core->measure_z(q, random);
      out << "MEASURE_Z q=" << q << " outcome=" << measured.outcome_one
          << " probability=" << measured.probability << " random=" << measured.random << "\n";
      ++result.instructions;
    } else if (op == "show") {
      xasm_detail::require_end(in, line_number);
      out << "STATE\n";
      for (std::size_t r = 0; r < core->qubits(); ++r) {
        out << core->generator_string(r) << "\n";
      }
    } else if (op == "bytes") {
      xasm_detail::require_end(in, line_number);
      out << "STATE_BYTES " << core->state_bytes() << "\n";
    } else if (op == "assert") {
      std::string what;
      in >> what;
      xasm_detail::require_end(in, line_number);

      bool ok = false;
      if (what == "ghz") {
        ok = core->is_ghz_signature();
      } else if (what == "one") {
        ok = core->hello_is_one();
      } else {
        throw std::runtime_error("line " + std::to_string(line_number) + ": unknown assertion");
      }

      out << "ASSERT " << what << " " << (ok ? "PASS" : "FAIL") << "\n";
      result.assertions_passed = result.assertions_passed && ok;
    } else {
      throw std::runtime_error("line " + std::to_string(line_number) + ": unknown instruction '" + op + "'");
    }
  }

  if (input.bad() || (input.fail() && !input.eof()))
    throw std::runtime_error("failed to read XASM program");
  if (!core && !scene) throw std::runtime_error("program did not declare qubits or scene");

  if (core) result.state_bytes = core->state_bytes();
  if (scene) {
    result.scene_objects = scene->count();
    result.scene_rule_bytes = scene->logical_rule_bytes();
    result.scene_text = scene->export_text();
  }
  result.output = out.str();
  return result;
}

inline XasmRunResult run_xasm_file(const std::string& path) {
  std::ifstream file(path);
  if (!file) throw std::runtime_error("cannot open XASM program: " + path);
  return run_xasm(file);
}

}  // namespace mundo_x
