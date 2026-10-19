#pragma once

#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <limits>
#include <locale>
#include <memory>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/pauli_observer.hpp"
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
  // Final scene only. Commands never emit a partial export.
  std::string scene_text;
  std::string backend = "compact";
  std::size_t logical_payload_bytes = 0;
  std::size_t components = 0;
  std::size_t t_gates = 0;
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

inline double read_expected_value(std::istringstream& input) {
  std::string token;
  double value = 0.0;
  if (input >> token) {
    // Use a complete, locale-independent token. Reject NaN/Inf, overflow,
    // partially parsed numbers and suffixes rather than silently accepting them.
    std::istringstream number(token);
    number.imbue(std::locale::classic());
    number >> std::noskipws >> value;
    if (number && number.eof() && std::isfinite(value)) return value;
  }
  throw std::runtime_error("expected assert expectation <Pauli> <finite value>");
}

inline std::string precise_value(double value) {
  std::ostringstream text;
  text.imbue(std::locale::classic());
  text << std::setprecision(std::numeric_limits<double>::max_digits10) << value;
  return text.str();
}
}  // namespace xasm_detail

inline XasmRunResult run_xasm(std::istream& input) {
  XasmRunResult result;
  std::unique_ptr<CompactCore> core;
  std::unique_ptr<VisualScene> scene;
  std::unique_ptr<ReducedExactTRepresentation> reduced;
  std::unique_ptr<mundo_x_bridge::PauliObserver> observer;
  bool backend_declared = false;
  std::ostringstream out;
  // Reproducible pseudorandom sampling, not a source of physical randomness.
  std::mt19937_64 random(0);

  const auto expectation = [&](const std::string& text) {
    auto pauli = mundo_x_bridge::Pauli::parse(text, result.qubits);
    if (core) {
      // A read-only index is shared until the next state operation. In
      // particular, a long multiword Pauli never becomes a dense statevector.
      if (!observer) observer = std::make_unique<mundo_x_bridge::PauliObserver>(*core);
      return observer->expectation(std::move(pauli));
    }
    // Reduced supports at most 63 qubits, so its Pauli masks fit one word.
    // Its raw expectation is unnormalized; expose a physical expectation here.
    const double norm = reduced->sparse_norm();
    if (!(norm > 0.0) || !std::isfinite(norm))
      throw std::runtime_error("non-finite or zero reduced norm");
    const auto value = reduced->pauli_expectation(pauli.x[0], pauli.z[0],
                                                 static_cast<std::uint8_t>(pauli.phase)) / norm;
    if (!std::isfinite(value.real()) || !std::isfinite(value.imag()) ||
        std::abs(value.imag()) > 1e-10)
      throw std::runtime_error("invalid Hermitian Pauli expectation");
    return value.real();
  };

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
        if (!core) throw std::runtime_error("line " + std::to_string(line_number) + ": scene_probz requires backend compact qubits");
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

    try {
      const auto read_size = [&](const std::string& usage) {
        std::string token;
        if (!(in >> token) || token.empty())
          throw std::runtime_error("expected " + usage);
        std::size_t value = 0;
        for (const char digit : token) {
          if (digit < '0' || digit > '9')
            throw std::runtime_error("expected " + usage + " (unsigned decimal integer)");
          const auto part = static_cast<std::size_t>(digit - '0');
          if (value > (std::numeric_limits<std::size_t>::max() - part) / 10)
            throw std::runtime_error("integer out of range in " + usage);
          value = value * 10 + part;
        }
        return value;
      };
      const auto require_end = [&]() {
        std::string extra;
        if (in >> extra)
          throw std::runtime_error("unexpected argument '" + extra + "'");
      };

      if (op == "backend") {
        if (core || reduced)
          throw std::runtime_error("backend must be declared before qubits");
        if (backend_declared) throw std::runtime_error("backend already declared");
        std::string backend;
        if (!(in >> backend) || (backend != "compact" && backend != "reduced"))
          throw std::runtime_error("expected backend <compact|reduced>");
        require_end();
        result.backend = backend;
        backend_declared = true;
        continue;
      }

      if (op == "qubits") {
        if (core || reduced) throw std::runtime_error("qubits already declared");
        const auto n = read_size("qubits <n>");
        require_end();
        if (result.backend == "reduced") {
          if (n == 0 || n > 63)
            throw std::runtime_error("reduced XASM supports 1..63 qubits");
          // Width is independent of non-Clifford component growth. Stop at
          // this explicit budget; never truncate components or fall back dense.
          reduced = std::make_unique<ReducedExactTRepresentation>(n, 65536);
        } else {
          if (n == 0) throw std::runtime_error("expected qubits <n> with n > 0");
          core = std::make_unique<CompactCore>(n);
        }
        result.qubits = n;
        continue;
      }

      if (!core && !reduced) throw std::runtime_error("declare qubits first");

      if (op == "h" || op == "x" || op == "z" || op == "s") {
        const auto q = read_size(op + " <q>");
        require_end();
        const auto apply = [&](auto& state) {
          if (op == "h") state.hadamard(q);
          else if (op == "x") state.pauli_x(q);
          else if (op == "z") state.pauli_z(q);
          else state.phase_s(q);
        };
        if (reduced) apply(*reduced);
        else apply(*core);
        observer.reset();
        ++result.instructions;
      } else if (op == "cx" || op == "cz") {
        const auto control = read_size(op + " <control> <target>");
        const auto target = read_size(op + " <control> <target>");
        require_end();
        const auto apply = [&](auto& state) {
          if (op == "cx") state.cnot(control, target);
          else state.controlled_z(control, target);
        };
        if (reduced) apply(*reduced);
        else apply(*core);
        observer.reset();
        ++result.instructions;
      } else if (op == "t") {
        const auto q = read_size("t <q>");
        require_end();
        if (!reduced) throw std::runtime_error("t requires backend reduced before qubits");
        if (reduced->t_count() >= 128)
          throw std::runtime_error("reduced XASM safety limit is 128 T gates");
        reduced->phase_t(q);
        ++result.instructions;
      } else if (op == "seed") {
        std::string token;
        std::uint64_t seed = 0;
        if (!(in >> token)) throw std::runtime_error("expected seed <uint64>");
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), seed);
        if (parsed.ec != std::errc{} || parsed.ptr != token.data() + token.size())
          throw std::runtime_error("expected seed <uint64>");
        require_end();
        random.seed(seed);
      } else if (op == "probz") {
        const auto q = read_size("probz <q>");
        require_end();
        if (!core) throw std::runtime_error("probz requires backend compact");
        const auto p1 = core->probability_one_z(q);
        out << "PROB_Z q=" << q << " p0=" << (1.0 - p1) << " p1=" << p1 << "\n";
      } else if (op == "measure") {
        const auto q = read_size("measure <q>");
        require_end();
        if (!core) throw std::runtime_error("measure requires backend compact");
        const auto measured = core->measure_z(q, random);
        out << "MEASURE_Z q=" << q << " outcome=" << measured.outcome_one
            << " probability=" << measured.probability << " random=" << measured.random << "\n";
        observer.reset();
        ++result.instructions;
      } else if (op == "expect") {
        std::string pauli;
        if (!(in >> pauli)) throw std::runtime_error("expected expect <Pauli>");
        require_end();
        const double value = expectation(pauli);
        out << "EXPECT pauli=" << pauli << " value=" << xasm_detail::precise_value(value) << "\n";
      } else if (op == "show") {
        require_end();
        out << "STATE\n";
        if (reduced) {
          out << "backend=reduced\n"
              << "components=" << reduced->component_count() << "\n"
              << "t_gates=" << reduced->t_count() << "\n"
              << "logical_payload_bytes=" << reduced->logical_payload_bytes() << "\n";
        } else {
          for (std::size_t r = 0; r < core->qubits(); ++r)
            out << core->generator_string(r) << "\n";
        }
      } else if (op == "bytes") {
        require_end();
        if (reduced) out << "LOGICAL_PAYLOAD_BYTES " << reduced->logical_payload_bytes() << "\n";
        else out << "STATE_BYTES " << core->state_bytes() << "\n";
      } else if (op == "assert") {
        std::string what;
        if (!(in >> what)) throw std::runtime_error("expected assert <ghz|one|fidelity|norm|expectation>");

        bool ok = false;
        std::string details;
        if (what == "expectation") {
          std::string pauli;
          if (!(in >> pauli)) throw std::runtime_error("expected assert expectation <Pauli> <finite value>");
          const double expected = xasm_detail::read_expected_value(in);
          require_end();
          const double value = expectation(pauli);
          ok = std::abs(value - expected) <= 1e-10;
          details = " pauli=" + pauli + " value=" + xasm_detail::precise_value(value) +
                    " expected=" + xasm_detail::precise_value(expected) + " tolerance=1e-10";
        } else {
          require_end();
          if (what == "ghz" || what == "one") {
            if (!core) throw std::runtime_error("assert " + what + " requires backend compact");
            ok = what == "ghz" ? core->is_ghz_signature() : core->hello_is_one();
          } else if (what == "fidelity" || what == "norm") {
            if (!reduced) throw std::runtime_error("assert " + what + " requires backend reduced");
            if (what == "fidelity" && result.qubits > 12)
              throw std::runtime_error("assert fidelity uses a dense reference limited to 12 qubits; use assert norm and assert expectation for larger registers");
            const double value = what == "fidelity" ? reduced->fidelity() : reduced->sparse_norm();
            // Keep the established XASM v0.2 acceptance tolerance. FP64 accuracy
            // is covered independently by the native precision regressions.
            ok = std::isfinite(value) && std::abs(value - 1.0) <= 3e-5;
          } else {
            throw std::runtime_error("unknown assertion '" + what + "'");
          }
        }

        out << "ASSERT " << what << " " << (ok ? "PASS" : "FAIL") << details << "\n";
        result.assertions_passed = result.assertions_passed && ok;
      } else {
        throw std::runtime_error("unknown instruction '" + op + "'");
      }
    } catch (const std::exception& e) {
      throw std::runtime_error("line " + std::to_string(line_number) + ": " + e.what());
    }
  }

  if (input.bad() || (input.fail() && !input.eof()))
    throw std::runtime_error("failed to read XASM program");
  if (!core && !reduced && !scene) throw std::runtime_error("program did not declare qubits or scene");

  if (reduced) {
    result.logical_payload_bytes = reduced->logical_payload_bytes();
    result.components = reduced->component_count();
    result.t_gates = reduced->t_count();
  } else if (core) {
    result.state_bytes = core->state_bytes();
  }
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
