#include "mundo_x/quantum_runtime.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using Core = mundo_x::InfiniteCore;
using Runtime = mundo_x::QuantumRuntime;
namespace protocol = mundo_x::infinite_program_detail;
using protocol::decimal;
using protocol::parse_background;

struct Options {
  Core::Background background = Core::Background::Zero;
  Core::Limits core{};
  mundo_x::InverseMemory::Limits memory{};
  bool explicit_background = false;
  bool help = false;
  bool version = false;
  std::string source = "-";
};

Options parse_options(int argc, char** argv) {
  Options options;
  bool have_source = false, positional = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (!positional && arg == "--") { positional = true; continue; }
    if (!positional && arg == "--help") { options.help = true; continue; }
    if (!positional && arg == "--version") { options.version = true; continue; }
    if (!positional && arg.rfind("--", 0) == 0) {
      const bool known =
          arg == "--background" || arg == "--max-active-qubits" ||
          arg == "--max-component-qubits" || arg == "--max-state-bytes" ||
          arg == "--max-address-digits" || arg == "--max-query-terms" ||
          arg == "--max-ether-rules" || arg == "--max-ether-query-sites" ||
          arg == "--max-memory-overrides" || arg == "--max-memory-bindings" ||
          arg == "--max-materialize-bytes";
      if (!known) throw std::invalid_argument("unknown option: " + arg);
      if (++i == argc) throw std::invalid_argument("missing value for " + arg);
      const std::string value = argv[i];
      if (arg == "--background") {
        options.background = parse_background(value);
        options.explicit_background = true;
        continue;
      }
      const auto count = decimal<std::size_t>(value, arg.c_str());
      if (arg == "--max-active-qubits") options.core.max_active_qubits = count;
      else if (arg == "--max-component-qubits") options.core.max_component_qubits = count;
      else if (arg == "--max-state-bytes") options.core.max_state_bytes = count;
      else if (arg == "--max-address-digits") {
        options.core.max_address_digits = count;
        options.memory.max_address_digits = count;
      } else if (arg == "--max-query-terms") options.core.max_query_terms = count;
      else if (arg == "--max-ether-rules") options.core.max_ether_rules = count;
      else if (arg == "--max-ether-query-sites") options.core.max_ether_query_sites = count;
      else if (arg == "--max-memory-overrides") options.memory.max_overrides = count;
      else if (arg == "--max-memory-bindings") options.memory.max_quantum_bindings = count;
      else options.memory.max_materialize_bytes = count;
    } else {
      if (have_source) throw std::invalid_argument("expected one .xi program file or - for stdin");
      if (!positional && arg != "-" && !arg.empty() && arg.front() == '-')
        throw std::invalid_argument("unknown option: " + arg);
      options.source = arg;
      have_source = true;
    }
  }
  return options;
}

void print_help() {
  std::cout << R"(COSMIC X Quantum Runtime 0.1.0
Ether/InfiniteCore + Inverse Ether Memory, without PC/Graphics/Solar/physics.

Usage:
  mundo_x_quantum [options] [program.xi|-]

Quantum options:
  --background zero|bell
  --max-active-qubits N
  --max-component-qubits N
  --max-state-bytes N
  --max-address-digits N
  --max-query-terms N
  --max-ether-rules N
  --max-ether-query-sites N

Inverse Memory options:
  --max-memory-overrides N
  --max-memory-bindings N
  --max-materialize-bytes N

Existing XI quantum commands remain available. Additional memory commands:
  mem_reset BYTE
  mem_store ADDRESS BYTE
  mem_bind ADDRESS QBASE
  mem_unbind ADDRESS
  mem_read ADDRESS
  mem_materialize ADDRESS COUNT
  mem_stats

Inverse Memory has no dense RAM array. One background byte describes the
logical domain, finite overrides keep classical differences, and only explicit
mem_bind records expose a byte through eight selected qubit addresses.
mem_read never measures: a non-deterministic quantum byte returns a handle.
mem_materialize fails instead of collapsing such a byte. The whole program is
transactional across both the quantum state and Inverse Memory metadata.
)";
}

std::string read_program(std::istream& input) {
  std::string program;
  char buffer[8192];
  while (input) {
    input.read(buffer, sizeof(buffer));
    const auto count = static_cast<std::size_t>(input.gcount());
    if (count > protocol::max_program_bytes - program.size())
      throw std::length_error("program exceeds 16 MiB transport budget");
    program.append(buffer, count);
  }
  if (input.bad()) throw std::runtime_error("failed to read program");
  return program;
}

void run(std::istream& input, const Options& options) {
  Runtime runtime(options.background, options.core, options.memory);
  std::cout << runtime.execute(read_program(input), options.explicit_background);
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    if (options.help) { print_help(); return 0; }
    if (options.version) {
      std::cout << "COSMIC X Quantum Runtime 0.1.0\n";
      return 0;
    }
    if (options.source == "-") {
#if defined(_WIN32)
      if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        throw std::runtime_error("cannot set stdin to binary mode");
#endif
      run(std::cin, options);
    } else {
      std::ifstream input(options.source, std::ios::binary);
      if (!input) throw std::runtime_error("cannot open program: " + options.source);
      run(input, options);
    }
    if (!std::cout) throw std::runtime_error("failed to write results");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "QuantumRuntime: " << error.what() << '\n';
    return 1;
  }
}
