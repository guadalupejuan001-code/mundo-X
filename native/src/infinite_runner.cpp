#include "mundo_x/infinite_core.hpp"
#include "mundo_x/infinite_address.hpp"

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
constexpr std::size_t max_line_bytes = 1024 * 1024;
constexpr std::size_t max_batch_gates = 65536;
constexpr std::size_t max_batch_address_bytes = 16 * 1024 * 1024;

struct Options {
  Core::Background background = Core::Background::Zero;
  Core::Limits limits{};
  bool explicit_background = false;
  bool help = false;
  bool version = false;
  std::string source = "-";
};

std::string lower_ascii(std::string text) {
  for (auto& ch : text)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
  return text;
}

template <typename UInt>
UInt decimal(const std::string& text, const char* name) {
  if (text.empty()) throw std::invalid_argument(std::string(name) + " is empty");
  UInt value = 0;
  for (const char ch : text) {
    if (ch < '0' || ch > '9')
      throw std::invalid_argument(std::string(name) + " requires decimal digits");
    const auto digit = static_cast<UInt>(ch - '0');
    if (value > (std::numeric_limits<UInt>::max() - digit) / 10)
      throw std::out_of_range(std::string(name) + " exceeds its integer range");
    value = static_cast<UInt>(value * 10 + digit);
  }
  return value;
}

Core::Background parse_background(const std::string& name) {
  if (name == "zero") return Core::Background::Zero;
  if (name == "bell") return Core::Background::BellPairs;
  throw std::invalid_argument("background must be zero or bell");
}

const char* background_name(Core::Background background) {
  return background == Core::Background::Zero ? "zero" : "bell";
}

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
materialize untouched background. A failed batch commits no gates. Errors stop
the program with a nonzero exit code. CLI limits: 1 MiB per line, 65536 gates and
16 MiB of canonical address text per batch. This formal infinite domain does not
imply infinite physical resources.
)";
}

bool read_line(std::istream& input, std::string& line) {
  line.clear();
  for (;;) {
    const auto byte = input.get();
    if (byte == std::char_traits<char>::eof()) {
      if (input.bad()) throw std::runtime_error("failed to read program");
      return !line.empty();
    }
    if (byte == '\n') return true;
    if (line.size() == max_line_bytes)
      throw std::length_error("program line exceeds 1 MiB");
    line.push_back(static_cast<char>(byte));
  }
}

std::vector<std::string> tokens(std::string line) {
  const auto comment = line.find('#');
  if (comment != std::string::npos) line.resize(comment);
  std::istringstream input(line);
  std::vector<std::string> words;
  for (std::string word; input >> word;) words.push_back(std::move(word));
  return words;
}

void require_count(const std::vector<std::string>& words, std::size_t count) {
  if (words.size() != count)
    throw std::invalid_argument("wrong argument count for " + words.front());
}

bool is_gate(const std::string& command) {
  return command == "h" || command == "s" || command == "x" ||
         command == "y" || command == "z" || command == "cx" || command == "cz";
}

Core::Gate parse_gate(const std::vector<std::string>& words) {
  const auto command = lower_ascii(words.front());
  const bool binary = command == "cx" || command == "cz";
  require_count(words, binary ? 3 : 2);
  const char op = binary ? (command == "cx" ? 'C' : 'D')
                        : static_cast<char>(command.front() - ('a' - 'A'));
  return {op, words[1], binary ? words[2] : std::string{}};
}

struct Observable {
  std::vector<Core::Term> terms;
  int sign = 1;
};

Observable parse_observable(const std::vector<std::string>& words, std::size_t start,
                            std::size_t max_terms) {
  Observable observable;
  if (start < words.size() && (words[start] == "+" || words[start] == "-")) {
    observable.sign = words[start] == "-" ? -1 : 1;
    ++start;
  }
  if (words.size() - start > max_terms)
    throw std::length_error("observable exceeds the query-term budget");
  observable.terms.reserve(words.size() - start);
  for (; start < words.size(); ++start) {
    const auto& word = words[start];
    if (word.size() < 3 || word[1] != ':' ||
        (word[0] != 'I' && word[0] != 'X' && word[0] != 'Y' && word[0] != 'Z'))
      throw std::invalid_argument("expected a Pauli term such as X:0");
    observable.terms.push_back({word.substr(2), word.front()});
  }
  return observable;
}

double expected_value(const std::string& text, bool probability) {
  if (text == "0") return 0.0;
  if (text == "1") return 1.0;
  if (!probability && text == "-1") return -1.0;
  if (probability && text == "0.5") return 0.5;
  throw std::invalid_argument(probability ? "expected probability 0, 0.5 or 1"
                                          : "expected expectation -1, 0 or 1");
}

std::string canonical(const Core& core, const std::string& address) {
  return mundo_x::infinite_detail::canonical_address(address, core.limits().max_address_digits);
}

void print_stats(const Core& core) {
  const auto stats = core.stats();
  const auto limits = core.limits();
  std::cout << "{\"type\":\"stats\",\"background\":\"" << background_name(core.background())
            << "\",\"domain\":\"nonnegative_decimal_addresses\",\"active_qubits\":" << stats.active_qubits
            << ",\"components\":" << stats.components
            << ",\"largest_component\":" << stats.largest_component
            << ",\"tableau_bytes\":" << stats.tableau_bytes
            << ",\"address_bytes\":" << stats.address_bytes
            << ",\"revision\":" << stats.revision
            << ",\"limits\":{\"max_active_qubits\":" << limits.max_active_qubits
            << ",\"max_component_qubits\":" << limits.max_component_qubits
            << ",\"max_state_bytes\":" << limits.max_state_bytes
            << ",\"max_address_digits\":" << limits.max_address_digits
            << ",\"max_query_terms\":" << limits.max_query_terms << "}}\n";
}

void execute(const std::vector<std::string>& words, Core& core) {
  const auto command = lower_ascii(words.front());
  if (is_gate(command)) { core.apply({parse_gate(words)}); return; }
  if (command == "stats") { require_count(words, 1); print_stats(core); return; }
  if (command == "seed") {
    require_count(words, 2);
    core.seed(decimal<std::uint64_t>(words[1], "seed"));
    return;
  }
  if (command == "expect") {
    const auto observable = parse_observable(words, 1, core.limits().max_query_terms);
    const auto value = core.expectation(observable.terms, observable.sign);
    std::cout << "{\"type\":\"expectation\",\"value\":" << value << "}\n";
    return;
  }
  if (command == "probz") {
    require_count(words, 2);
    const auto address = canonical(core, words[1]);
    const auto probability = core.probability_one(address);
    std::cout << "{\"type\":\"probz\",\"address\":\"" << address
              << "\",\"probability_one\":" << probability << "}\n";
    return;
  }
  if (command == "measure") {
    require_count(words, 2);
    const auto address = canonical(core, words[1]);
    const auto result = core.measure_z(address);
    std::cout << "{\"type\":\"measurement\",\"address\":\"" << address
              << "\",\"outcome_one\":" << (result.outcome_one ? "true" : "false")
              << ",\"probability\":" << result.probability
              << ",\"random\":" << (result.random ? "true" : "false") << "}\n";
    return;
  }
  if (command == "postselect") {
    require_count(words, 3);
    if (words[2] != "0" && words[2] != "1")
      throw std::invalid_argument("postselect outcome must be 0 or 1");
    const auto address = canonical(core, words[1]);
    const bool outcome = words[2] == "1";
    const auto probability = core.force_measure_z(address, outcome);
    std::cout << "{\"type\":\"postselection\",\"address\":\"" << address
              << "\",\"outcome_one\":" << (outcome ? "true" : "false")
              << ",\"probability\":" << probability << "}\n";
    return;
  }
  if (command == "compact") {
    require_count(words, 1);
    const auto released = core.compact_background();
    std::cout << "{\"type\":\"compact\",\"released_qubits\":" << released << "}\n";
    return;
  }
  if (command == "assert") {
    if (words.size() < 3) throw std::invalid_argument("incomplete assertion");
    const auto subject = lower_ascii(words[1]);
    double actual = 0.0;
    double expected = 0.0;
    if (subject == "expect") {
      expected = expected_value(words[2], false);
      const auto observable = parse_observable(words, 3, core.limits().max_query_terms);
      actual = core.expectation(observable.terms, observable.sign);
    } else if (subject == "probz") {
      require_count(words, 4);
      expected = expected_value(words[3], true);
      actual = core.probability_one(words[2]);
    } else throw std::invalid_argument("assert supports expect and probz");
    if (actual != expected) {
      std::ostringstream error;
      error << "assertion failed: expected " << expected << ", received " << actual;
      throw std::runtime_error(error.str());
    }
    return;
  }
  throw std::invalid_argument("unknown command: " + words.front());
}

void run(std::istream& input, const Options& options) {
  auto core = std::make_unique<Core>(options.background, options.limits);
  bool first_command = true;
  bool in_batch = false;
  std::size_t line_number = 0;
  std::size_t batch_address_bytes = 0;
  std::vector<Core::Gate> batch;
  try {
    std::string line;
    for (;;) {
      ++line_number;
      if (!read_line(input, line)) break;
      const auto words = tokens(std::move(line));
      if (words.empty()) continue;
      const auto command = lower_ascii(words.front());
      if (command == "background") {
        require_count(words, 2);
        if (!first_command) throw std::invalid_argument("background must be the first command");
        const auto background = parse_background(words[1]);
        if (options.explicit_background && background != options.background)
          throw std::invalid_argument("program background conflicts with --background");
        core = std::make_unique<Core>(background, options.limits);
        first_command = false;
        continue;
      }
      first_command = false;
      if (command == "begin") {
        require_count(words, 1);
        if (in_batch) throw std::invalid_argument("nested batches are not supported");
        in_batch = true;
        batch.clear();
        batch_address_bytes = 0;
      } else if (command == "end") {
        require_count(words, 1);
        if (!in_batch) throw std::invalid_argument("end has no matching begin");
        core->apply(batch);
        batch.clear();
        in_batch = false;
      } else if (in_batch) {
        if (!is_gate(command)) throw std::invalid_argument("a batch accepts gates only");
        if (batch.size() == max_batch_gates)
          throw std::length_error("batch exceeds 65536 gates");
        auto gate = parse_gate(words);
        gate.first = canonical(*core, gate.first);
        if (!gate.second.empty()) gate.second = canonical(*core, gate.second);
        const auto bytes = gate.first.size() + gate.second.size();
        if (bytes > max_batch_address_bytes - batch_address_bytes)
          throw std::length_error("batch address text exceeds 16 MiB");
        batch.push_back(std::move(gate));
        batch_address_bytes += bytes;
      } else execute(words, *core);
    }
    if (in_batch) throw std::invalid_argument("unterminated batch: expected end");
  } catch (const std::exception& error) {
    throw std::runtime_error("line " + std::to_string(line_number) + ": " + error.what());
  }
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
