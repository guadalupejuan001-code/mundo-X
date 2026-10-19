#include "mundo_x/infinite_session.hpp"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using Core = mundo_x::InfiniteCore;
namespace protocol = mundo_x::infinite_program_detail;
using protocol::decimal;
using protocol::parse_background;

struct Options {
  Core::Background background = Core::Background::Zero;
  Core::Limits limits{};
  bool explicit_background = false;
  bool help = false;
  bool version = false;
  std::string source = "-";
};

Options parse_options(int argc, char** argv) {
  Options options;
  bool have_source = false;
  bool positional = false;
  for (int i = 1; i < argc; ++i) {
    const std::string arg = argv[i];
    if (!positional && arg == "--") { positional = true; continue; }
    if (!positional && arg == "--help") { options.help = true; continue; }
    if (!positional && arg == "--version") { options.version = true; continue; }
    if (!positional && arg.rfind("--", 0) == 0) {
      if (arg != "--background" && arg != "--max-active-qubits" &&
          arg != "--max-component-qubits" && arg != "--max-state-bytes" &&
          arg != "--max-address-digits" && arg != "--max-query-terms")
        throw std::invalid_argument("unknown option: " + arg);
      if (++i == argc) throw std::invalid_argument("missing value for " + arg);
      const std::string value = argv[i];
      if (arg == "--background") {
        options.background = parse_background(value);
        options.explicit_background = true;
        continue;
      }
      const auto count = decimal<std::size_t>(value, arg.c_str());
      if (arg == "--max-active-qubits") options.limits.max_active_qubits = count;
      else if (arg == "--max-component-qubits") options.limits.max_component_qubits = count;
      else if (arg == "--max-state-bytes") options.limits.max_state_bytes = count;
      else if (arg == "--max-address-digits") options.limits.max_address_digits = count;
      else options.limits.max_query_terms = count;
    } else {
      if (have_source) throw std::invalid_argument("expected one program file or - for stdin");
      if (!positional && arg != "-" && !arg.empty() && arg.front() == '-')
        throw std::invalid_argument("unknown option: " + arg);
      options.source = arg;
      have_source = true;
    }
  }
  return options;
}

void print_help() {
  std::cout << R"(COSMIC X InfiniteCore 0.1.0 -- exact Clifford rules with finite active components
Usage: mundo_x_infinite [options] [program.xi|-]

Options:
  --background zero|bell       Default: zero
  --max-active-qubits N        Default: 4096
  --max-component-qubits N     Default: 1024
  --max-state-bytes N          Default: 67108864 (tableau payload, not total RAM)
  --max-address-digits N       Default: 4096; positive; checks input before normalization
  --max-query-terms N          Default: 4096
  --help | --version

Commands (one per line; # starts a comment):
  background zero|bell         Optional first command only
  seed UINT64
  h Q | s Q | x Q | y Q | z Q
  cx CONTROL TARGET | cz Q R
  expect [+|-] X:Q Y:R ...     Finite Pauli product; empty means identity
  probz Q                     Probability of outcome 1
  measure Q                   Sample a Z measurement
  postselect Q 0|1            Force a possible Z measurement branch
  compact                     Release components exactly equal to their background
  stats
  begin ... end               Gate-only transaction, commands on separate lines
  assert expect -1|0|1 [+|-] X:Q ...
  assert probz Q 0|0.5|1

Q and R are nonnegative ASCII decimal addresses of any length within the digit
budget. Leading zeros are normalized. Queries emit JSON lines and do not
materialize untouched background. The complete request is atomic. Errors stop
the program with a nonzero exit code and no partial output. Transport limits:
16 MiB per request/response, 1 MiB per line, 131072 commands, 65536 gates and
16 MiB of canonical address text per batch. This formal infinite domain does not
imply infinite physical resources.
)";
}

void run(std::istream& input, const Options& options) {
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
  mundo_x::InfiniteSession session(options.background, options.limits);
  // CLI, PC guest and graphical hosts use the same parser and transaction.
  // Even a late assertion failure produces no partial state or output.
  const auto output = session.execute_prepared(program,
      [](const std::string& response) { return response; }, options.explicit_background);
  std::cout << output;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    const auto options = parse_options(argc, argv);
    if (options.help) { print_help(); return 0; }
    if (options.version) { std::cout << "COSMIC X InfiniteCore 0.1.0\n"; return 0; }
    if (options.source == "-") {
#if defined(_WIN32)
      // Match binary .xi files: the Windows text CRT otherwise treats Ctrl+Z
      // as EOF and can silently accept a truncated program from stdin.
      if (_setmode(_fileno(stdin), _O_BINARY) == -1)
        throw std::runtime_error("cannot set stdin to binary mode");
#endif
      run(std::cin, options);
    }
    else {
      std::ifstream input(options.source, std::ios::binary);
      if (!input) throw std::runtime_error("cannot open program: " + options.source);
      run(input, options);
    }
    if (!std::cout) throw std::runtime_error("failed to write results");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "InfiniteCore: " << error.what() << '\n';
    return 1;
  }
}
