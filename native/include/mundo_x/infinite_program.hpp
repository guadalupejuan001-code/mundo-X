#pragma once

#include "mundo_x/infinite_core.hpp"
#include "mundo_x/inverse_memory.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <locale>
#include <sstream>

namespace mundo_x {

struct InfiniteObservable {
  int sign = 1;
  std::vector<InfiniteCore::Term> terms;
};

// One grammar is shared by CLI, host, emulated guest, JNI and C ABI. These
// transport/work budgets supplement the owner's separate tableau budgets.
namespace infinite_program_detail {
inline constexpr std::size_t max_program_bytes = std::size_t{16} * 1024 * 1024;
inline constexpr std::size_t max_response_bytes = std::size_t{16} * 1024 * 1024;
inline constexpr std::size_t max_line_bytes = 1024 * 1024;
inline constexpr std::size_t max_commands = 131072;
inline constexpr std::size_t max_batch_gates = 65536;
inline constexpr std::size_t max_batch_address_bytes = std::size_t{16} * 1024 * 1024;
inline constexpr std::size_t max_observables = 4096;
inline constexpr std::size_t max_batch_query_terms = 65536;

inline std::string lower_ascii(std::string text) {
  for (auto& ch : text)
    if (ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch + ('a' - 'A'));
  return text;
}

template <typename UInt>
inline UInt decimal(const std::string& text, const char* name) {
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

// Ether Memory v0.1 maps one logical byte to eight consecutive qubit addresses.
// Byte a uses q(8a+0)..q(8a+7), little-endian by bit. Decimal arithmetic stays
// textual so large logical addresses preserve InfiniteCore's exact addressing.
inline std::string qbyte_address(std::string byte_address, unsigned bit,
                                 std::size_t max_digits) {
  if (bit >= 8) throw std::out_of_range("qbyte bit index");
  byte_address = infinite_detail::canonical_address(byte_address, max_digits);
  unsigned carry = bit;
  for (std::size_t i = byte_address.size(); i-- > 0;) {
    const unsigned value = static_cast<unsigned>(byte_address[i] - '0') * 8U + carry;
    byte_address[i] = static_cast<char>('0' + value % 10U);
    carry = value / 10U;
  }
  while (carry) {
    byte_address.insert(byte_address.begin(), static_cast<char>('0' + carry % 10U));
    carry /= 10U;
  }
  return infinite_detail::canonical_address(byte_address, max_digits);
}

inline std::array<std::string, 8> qbyte_addresses(const InfiniteCore& core,
                                                  const std::string& byte_address) {
  std::array<std::string, 8> result;
  for (unsigned bit = 0; bit < result.size(); ++bit)
    result[bit] = qbyte_address(byte_address, bit, core.limits().max_address_digits);
  return result;
}

// Non-destructive classical read. A superposed/entangled bit is rejected
// instead of being measured silently by a CPU-like load.
inline std::uint8_t qload8(const InfiniteCore& core, const std::string& byte_address) {
  const auto addresses = qbyte_addresses(core, byte_address);
  std::uint8_t value = 0;
  for (unsigned bit = 0; bit < addresses.size(); ++bit) {
    const double one = core.probability_one(addresses[bit]);
    if (one != 0.0 && one != 1.0)
      throw std::runtime_error("qload8 requires a classical byte; bit " +
                               std::to_string(bit) + " is quantum");
    if (one == 1.0) value = static_cast<std::uint8_t>(value | (1U << bit));
  }
  return value;
}

// Exact classical store over Ether. It never measures a quantum bit. All eight
// bits are checked first; then the necessary X gates commit as one core batch.
// Untouched zero-background bytes therefore require no materialized qubits.
inline void qstore8(InfiniteCore& core, const std::string& byte_address,
                    std::uint8_t value) {
  const auto addresses = qbyte_addresses(core, byte_address);
  std::vector<InfiniteCore::Gate> flips;
  flips.reserve(8);
  for (unsigned bit = 0; bit < addresses.size(); ++bit) {
    const double one = core.probability_one(addresses[bit]);
    if (one != 0.0 && one != 1.0)
      throw std::runtime_error("qstore8 requires a classical byte; bit " +
                               std::to_string(bit) + " is quantum");
    const bool desired = (value & (1U << bit)) != 0;
    if ((one == 1.0) != desired)
      flips.push_back({'X', addresses[bit], {}});
  }
  core.apply(flips);
}

inline InfiniteCore::Background parse_background(const std::string& text) {
  if (text == "zero") return InfiniteCore::Background::Zero;
  if (text == "bell") return InfiniteCore::Background::BellPairs;
  throw std::invalid_argument("background must be zero or bell");
}

inline const char* background_name(InfiniteCore::Background background) {
  return background == InfiniteCore::Background::Zero ? "zero" : "bell";
}

inline std::vector<std::string> tokens(std::string line) {
  const auto comment = line.find('#');
  if (comment != std::string::npos) line.resize(comment);
  std::istringstream input(line);
  input.imbue(std::locale::classic());
  std::vector<std::string> words;
  for (std::string word; input >> word;) words.push_back(std::move(word));
  return words;
}

inline void require_count(const std::vector<std::string>& words, std::size_t count) {
  if (words.size() != count)
    throw std::invalid_argument("wrong argument count for " + words.front());
}

inline bool is_gate(const std::string& command) {
  return command == "h" || command == "s" || command == "x" ||
         command == "y" || command == "z" || command == "cx" || command == "cz";
}

inline InfiniteCore::Gate parse_gate(const std::vector<std::string>& words) {
  const auto command = lower_ascii(words.front());
  const bool binary = command == "cx" || command == "cz";
  require_count(words, binary ? 3 : 2);
  const char op = binary ? (command == "cx" ? 'C' : 'D')
                        : static_cast<char>(command.front() - ('a' - 'A'));
  return {op, words[1], binary ? words[2] : std::string{}};
}

inline InfiniteObservable parse_observable_words(const std::vector<std::string>& words,
                                                 std::size_t start,
                                                 std::size_t max_terms) {
  InfiniteObservable observable;
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

inline double expected_value(const std::string& text, bool probability) {
  if (text == "0") return 0.0;
  if (text == "1") return 1.0;
  if (!probability && text == "-1") return -1.0;
  if (probability && text == "0.5") return 0.5;
  throw std::invalid_argument(probability ? "expected probability 0, 0.5 or 1"
                                          : "expected expectation -1, 0 or 1");
}

inline std::string canonical(const InfiniteCore& core, const std::string& address) {
  return infinite_detail::canonical_address(address, core.limits().max_address_digits);
}

inline void write_stats(std::ostream& out, const InfiniteCore& core) {
  const auto stats = core.stats();
  const auto& limits = core.limits();
  out << "{\"type\":\"stats\",\"background\":\"" << background_name(core.background())
      << "\",\"domain\":\"nonnegative_decimal_addresses\",\"active_qubits\":" << stats.active_qubits
      << ",\"components\":" << stats.components
      << ",\"largest_component\":" << stats.largest_component
      << ",\"tableau_bytes\":" << stats.tableau_bytes
      << ",\"address_bytes\":" << stats.address_bytes
      << ",\"ether_rules\":" << stats.ether_rules
      << ",\"revision\":" << stats.revision
      << ",\"limits\":{\"max_active_qubits\":" << limits.max_active_qubits
      << ",\"max_component_qubits\":" << limits.max_component_qubits
      << ",\"max_state_bytes\":" << limits.max_state_bytes
      << ",\"max_address_digits\":" << limits.max_address_digits
      << ",\"max_query_terms\":" << limits.max_query_terms
      << ",\"max_ether_rules\":" << limits.max_ether_rules
      << ",\"max_ether_query_sites\":" << limits.max_ether_query_sites << "}}\n";
}

struct ProgramResult { std::string output; bool explicit_mutation = false; };

inline unsigned memory_byte_value(const std::string& text, const char* name) {
  const auto value = decimal<unsigned>(text, name);
  if (value > 255U) throw std::out_of_range(std::string(name) + " exceeds one byte");
  return value;
}

inline void write_memory_stats(std::ostream& out, const InverseMemory& memory) {
  const auto stats = memory.stats();
  const auto& limits = memory.limits();
  out << "{\"type\":\"memory_stats\",\"model\":\"inverse-ether-memory\","
      << "\"background_byte\":" << unsigned(stats.background_byte)
      << ",\"overrides\":" << stats.overrides
      << ",\"quantum_bindings\":" << stats.quantum_bindings
      << ",\"revision\":" << stats.revision
      << ",\"limits\":{\"max_address_digits\":" << limits.max_address_digits
      << ",\"max_overrides\":" << limits.max_overrides
      << ",\"max_quantum_bindings\":" << limits.max_quantum_bindings
      << ",\"max_materialize_bytes\":" << limits.max_materialize_bytes << "}}\n";
}

inline void execute_line(const std::vector<std::string>& words, InfiniteCore& core,
                         InverseMemory* memory, std::ostream& out,
                         bool& explicit_mutation) {
  const auto command = lower_ascii(words.front());
  if (is_gate(command)) { core.apply({parse_gate(words)}); return; }
  if (command == "h_all" || command == "cz_neighbors") {
    require_count(words, 1);
    if (command == "h_all") core.h_all(); else core.cz_neighbors();
    return;
  }
  if (command == "stats") { require_count(words, 1); write_stats(out, core); return; }
  if (command == "seed") {
    require_count(words, 2);
    core.seed(decimal<std::uint64_t>(words[1], "seed"));
    explicit_mutation = true;
    return;
  }
  if (command == "expect") {
    const auto observable = parse_observable_words(words, 1, core.limits().max_query_terms);
    out << "{\"type\":\"expectation\",\"value\":"
        << core.expectation(observable.terms, observable.sign) << "}\n";
    return;
  }
  if (command == "probz") {
    require_count(words, 2);
    const auto address = canonical(core, words[1]);
    const auto probability = core.probability_one(address);
    out << "{\"type\":\"probz\",\"address\":\"" << address
        << "\",\"probability_one\":" << probability << "}\n";
    return;
  }
  if (command == "qload8") {
    if (memory)
      throw std::invalid_argument("qload8 is legacy MemoryFabric syntax; use mem_read");
    require_count(words, 2);
    const auto address = canonical(core, words[1]);
    const auto value = qload8(core, address);
    out << "{\"type\":\"qload8\",\"address\":\"" << address
        << "\",\"value\":" << static_cast<unsigned>(value) << "}\n";
    return;
  }
  if (command == "qstore8") {
    if (memory)
      throw std::invalid_argument("qstore8 is legacy MemoryFabric syntax; use mem_store/mem_bind");
    require_count(words, 3);
    const auto address = canonical(core, words[1]);
    const auto parsed = decimal<unsigned>(words[2], "qstore8 value");
    if (parsed > 255U) throw std::out_of_range("qstore8 value exceeds 255");
    qstore8(core, address, static_cast<std::uint8_t>(parsed));
    out << "{\"type\":\"qstore8\",\"address\":\"" << address
        << "\",\"value\":" << parsed
        << ",\"active_qubits\":" << core.stats().active_qubits << "}\n";
    return;
  }
  if (command == "measure") {
    require_count(words, 2);
    const auto address = canonical(core, words[1]);
    const auto result = core.measure_z(address);
    out << "{\"type\":\"measurement\",\"address\":\"" << address
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
    out << "{\"type\":\"postselection\",\"address\":\"" << address
        << "\",\"outcome_one\":" << (outcome ? "true" : "false")
        << ",\"probability\":" << probability << "}\n";
    return;
  }
  if (command == "compact") {
    require_count(words, 1);
    const auto released = core.compact_background();
    out << "{\"type\":\"compact\",\"released_qubits\":" << released << "}\n";
    return;
  }
  if (command == "mem_reset") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 2);
    memory->reset(static_cast<std::uint8_t>(memory_byte_value(words[1], "memory background")));
    return;
  }
  if (command == "mem_store") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 3);
    memory->store(words[1], static_cast<std::uint8_t>(memory_byte_value(words[2], "memory byte")), core);
    return;
  }
  if (command == "mem_bind") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 3);
    memory->bind_quantum_byte(words[1], words[2], core);
    return;
  }
  if (command == "mem_unbind") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 2);
    memory->unbind_quantum_byte(words[1]);
    return;
  }
  if (command == "mem_read") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 2);
    const auto address = infinite_detail::canonical_address(words[1], memory->limits().max_address_digits);
    const auto value = memory->read(address, core);
    out << "{\"type\":\"memory_read\",\"address\":\"" << address << "\",";
    if (value.deterministic)
      out << "\"deterministic\":true,\"value\":" << unsigned(value.value);
    else
      out << "\"deterministic\":false,\"handle\":\"" << value.handle << "\"";
    out << "}\n";
    return;
  }
  if (command == "mem_materialize") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 3);
    const auto count = decimal<std::size_t>(words[2], "materialization count");
    const auto bytes = memory->materialize(words[1], count, core);
    static constexpr char hex[] = "0123456789abcdef";
    std::string packed;
    if (bytes.size() > (max_response_bytes / 2))
      throw std::length_error("materialized memory response exceeds transport budget");
    packed.reserve(bytes.size() * 2);
    for (const auto byte : bytes) {
      packed.push_back(hex[byte >> 4]);
      packed.push_back(hex[byte & 15]);
    }
    out << "{\"type\":\"memory_materialization\",\"address\":\"" << words[1]
        << "\",\"bytes\":" << bytes.size() << ",\"hex\":\"" << packed << "\"}\n";
    return;
  }
  if (command == "mem_stats") {
    if (!memory) throw std::invalid_argument("Inverse Memory is not enabled in this runtime");
    require_count(words, 1);
    write_memory_stats(out, *memory);
    return;
  }
  if (command == "assert") {
    if (words.size() < 3) throw std::invalid_argument("incomplete assertion");
    const auto subject = lower_ascii(words[1]);
    double actual = 0, expected = 0;
    if (subject == "expect") {
      expected = expected_value(words[2], false);
      const auto observable = parse_observable_words(words, 3, core.limits().max_query_terms);
      actual = core.expectation(observable.terms, observable.sign);
    } else if (subject == "probz") {
      require_count(words, 4);
      expected = expected_value(words[3], true);
      actual = core.probability_one(words[2]);
    } else throw std::invalid_argument("assert supports expect and probz");
    if (actual != expected) {
      std::ostringstream error;
      error.imbue(std::locale::classic());
      error << "assertion failed: expected " << expected << ", received " << actual;
      throw std::runtime_error(error.str());
    }
    return;
  }
  throw std::invalid_argument("unknown command: " + words.front());
}

// This function acts on a private candidate owned by InfiniteSession. All
// output is buffered and bounded before the session commits the candidate.
inline ProgramResult run(InfiniteCore& core, InverseMemory* memory,
                         const std::string& program,
                         bool require_matching_background = false) {
  if (program.size() > max_program_bytes)
    throw std::length_error("program exceeds 16 MiB transport budget");
  ProgramResult result;
  bool first_command = true, in_batch = false;
  std::size_t cursor = 0, line_number = 0, command_count = 0, address_bytes = 0;
  std::vector<InfiniteCore::Gate> batch;
  try {
    while (cursor < program.size()) {
      ++line_number;
      const auto newline = program.find('\n', cursor);
      const auto end = newline == std::string::npos ? program.size() : newline;
      if (end - cursor > max_line_bytes)
        throw std::length_error("program line exceeds 1 MiB");
      auto words = tokens(program.substr(cursor, end - cursor));
      cursor = newline == std::string::npos ? program.size() : newline + 1;
      if (words.empty()) continue;
      if (++command_count > max_commands)
        throw std::length_error("program exceeds 131072 commands");
      const auto command = lower_ascii(words.front());
      if (command == "background") {
        require_count(words, 2);
        if (!first_command) throw std::invalid_argument("background must be the first command");
        const auto background = parse_background(words[1]);
        if (require_matching_background && background != core.background())
          throw std::invalid_argument("program background conflicts with --background");
        core = InfiniteCore(background, core.limits());
        result.explicit_mutation = true;
        first_command = false;
        continue;
      }
      first_command = false;
      if (command == "begin") {
        require_count(words, 1);
        if (in_batch) throw std::invalid_argument("nested batches are not supported");
        in_batch = true; batch.clear(); address_bytes = 0;
      } else if (command == "end") {
        require_count(words, 1);
        if (!in_batch) throw std::invalid_argument("end has no matching begin");
        core.apply(batch); batch.clear(); in_batch = false;
      } else if (in_batch) {
        if (!is_gate(command)) throw std::invalid_argument("a batch accepts gates only");
        if (batch.size() == max_batch_gates)
          throw std::length_error("batch exceeds 65536 gates");
        auto gate = parse_gate(words);
        gate.first = canonical(core, gate.first);
        if (!gate.second.empty()) gate.second = canonical(core, gate.second);
        const auto bytes = gate.first.size() + gate.second.size();
        if (bytes > max_batch_address_bytes - address_bytes)
          throw std::length_error("batch address text exceeds 16 MiB");
        batch.push_back(std::move(gate)); address_bytes += bytes;
      } else {
        std::ostringstream line;
        line.imbue(std::locale::classic());
        line.exceptions(std::ios::badbit | std::ios::failbit);
        line << std::setprecision(17);
        execute_line(words, core, memory, line, result.explicit_mutation);
        const auto output = line.str();
        if (output.size() > max_response_bytes - result.output.size())
          throw std::length_error("response exceeds 16 MiB transport budget");
        result.output += output;
      }
    }
    if (in_batch) throw std::invalid_argument("unterminated batch: expected end");
  } catch (const std::exception& error) {
    throw std::runtime_error("line " + std::to_string(line_number) + ": " + error.what());
  }
  return result;
}

inline ProgramResult run(InfiniteCore& core, const std::string& program,
                         bool require_matching_background = false) {
  return run(core, nullptr, program, require_matching_background);
}
}  // namespace infinite_program_detail

inline InfiniteObservable parse_observable(const std::string& text,
                                           std::size_t max_terms = 4096) {
  if (text.size() > infinite_program_detail::max_line_bytes)
    throw std::length_error("observable exceeds 1 MiB text budget");
  return infinite_program_detail::parse_observable_words(
      infinite_program_detail::tokens(text), 0, max_terms);
}

}  // namespace mundo_x
