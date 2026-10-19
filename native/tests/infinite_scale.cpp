#include "mundo_x/infinite_core.hpp"

#include <chrono>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Core = mundo_x::InfiniteCore;
using Clock = std::chrono::steady_clock;

void require(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

double microseconds(Clock::time_point start, Clock::time_point end) {
  return std::chrono::duration<double, std::micro>(end - start).count();
}

std::string json_string(const std::string& text) {
  std::ostringstream out;
  out << '"';
  for (const unsigned char c : text) {
    if (c == '"' || c == '\\') out << '\\' << static_cast<char>(c);
    else if (c < 0x20)
      out << "\\u00" << std::hex << std::setw(2) << std::setfill('0')
          << static_cast<unsigned>(c) << std::dec;
    else out << static_cast<char>(c);
  }
  out << '"';
  return out.str();
}

void stats_json(std::ostream& out, const Core::Stats& stats) {
  out << "{\"active_qubits\":" << stats.active_qubits
      << ",\"components\":" << stats.components
      << ",\"largest_component\":" << stats.largest_component
      << ",\"tableau_bytes\":" << stats.tableau_bytes
      << ",\"address_bytes\":" << stats.address_bytes
      << ",\"revision\":" << stats.revision << '}';
}

bool same_payload(const Core::Stats& a, const Core::Stats& b) {
  return a.active_qubits == b.active_qubits && a.components == b.components &&
         a.largest_component == b.largest_component &&
         a.tableau_bytes == b.tableau_bytes && a.address_bytes == b.address_bytes;
}

void remote_bell(std::ostream& out, std::size_t decimal_exponent) {
  const std::string target = "1" + std::string(decimal_exponent, '0');
  const auto begin = Clock::now();
  Core core;
  const auto before = core.stats();
  const auto gate_begin = Clock::now();
  core.apply({{'H', "0", ""}, {'C', "0", target}});
  const auto gate_end = Clock::now();
  const auto actual = core.stats();
  const auto query_begin = Clock::now();
  const auto p0 = core.probability_one("0"), pt = core.probability_one(target);
  const auto xx = core.expectation({{"0", 'X'}, {target, 'X'}});
  const auto yy = core.expectation({{"0", 'Y'}, {target, 'Y'}});
  const auto zz = core.expectation({{"0", 'Z'}, {target, 'Z'}});
  const auto query_end = Clock::now();
  require(before.active_qubits == 0 && before.tableau_bytes == 0,
          "remote Bell initial background was materialized");
  require(actual.active_qubits == 2 && actual.components == 1 &&
              actual.largest_component == 2 && actual.tableau_bytes == 34,
          "remote Bell did not use exactly one two-qubit tableau");
  require(actual.address_bytes == target.size() + 1,
          "remote Bell address payload does not match actual canonical ids");
  require(p0 == 0.5 && pt == 0.5 && xx == 1.0 && yy == -1.0 && zz == 1.0,
          "remote Bell observables differ from the prepared Bell state");
  require(core.stats() == actual, "remote Bell queries changed committed state");
  const auto end = Clock::now();
  out << "{\"case\":\"remote_bell\",\"background\":\"zero\","
      << "\"first_address\":\"0\",\"target_decimal_power\":" << decimal_exponent
      << ",\"target_digits\":" << target.size()
      << ",\"gates\":2,\"stats_before\":";
  stats_json(out, before);
  out << ",\"stats_after\":";
  stats_json(out, actual);
  out << ",\"p_first_one\":" << p0 << ",\"p_target_one\":" << pt
      << ",\"xx\":" << xx << ",\"yy\":" << yy << ",\"zz\":" << zz
      << ",\"apply_us\":" << microseconds(gate_begin, gate_end)
      << ",\"query_us\":" << microseconds(query_begin, query_end)
      << ",\"elapsed_us\":" << microseconds(begin, end) << ",\"ok\":true}";
}

void implicit_bell_queries(std::ostream& out) {
  constexpr std::size_t pairs = 1000;
  const auto begin = Clock::now();
  Core core(Core::Background::BellPairs);
  const auto before = core.stats();
  // 10^1000 + 2*k and its partner, generated as decimal text. These indices
  // are never converted to a machine integer or enumerated by the core.
  const std::string prefix = "1" + std::string(996, '0');
  std::vector<Core::Term> all_zz;
  all_zz.reserve(2 * pairs);
  double xx_sum = 0.0, yy_sum = 0.0, zz_sum = 0.0, p_one_sum = 0.0;
  for (std::size_t pair = 0; pair < pairs; ++pair) {
    const auto tail = std::to_string(2 * pair);
    const auto a = prefix + std::string(4 - tail.size(), '0') + tail;
    const auto b = mundo_x::infinite_detail::partner_address(a);
    const auto xx = core.expectation({{a, 'X'}, {b, 'X'}});
    const auto yy = core.expectation({{a, 'Y'}, {b, 'Y'}});
    const auto zz = core.expectation({{a, 'Z'}, {b, 'Z'}});
    const auto one = core.probability_one(a);
    require(xx == 1.0 && yy == -1.0 && zz == 1.0 && one == 0.5,
            "implicit Bell pair query disagrees with the background rule");
    xx_sum += xx;
    yy_sum += yy;
    zz_sum += zz;
    p_one_sum += one;
    all_zz.push_back({a, 'Z'});
    all_zz.push_back({b, 'Z'});
  }
  const auto global_zz = core.expectation(all_zz);
  const auto after = core.stats();
  require(global_zz == 1.0, "finite 2000-site ZZ product is incorrect");
  require(before == after && after.active_qubits == 0 &&
              after.components == 0 && after.tableau_bytes == 0,
          "analytic background queries materialized or changed the state");
  const auto end = Clock::now();
  out << "{\"case\":\"implicit_bell_queries\",\"background\":\"bell_pairs\","
      << "\"pairs\":" << pairs << ",\"address_digits\":1001,"
      << "\"queries\":" << 4 * pairs + 1 << ",\"largest_query_terms\":" << all_zz.size()
      << ",\"stats_before\":";
  stats_json(out, before);
  out << ",\"stats_after\":";
  stats_json(out, after);
  out << ",\"xx_sum\":" << xx_sum << ",\"yy_sum\":" << yy_sum
      << ",\"zz_sum\":" << zz_sum << ",\"p_one_sum\":" << p_one_sum
      << ",\"finite_all_zz\":" << global_zz
      << ",\"elapsed_us\":" << microseconds(begin, end) << ",\"ok\":true}";
}

void repeated_gates(std::ostream& out) {
  constexpr std::size_t gates = 10000;
  const auto begin = Clock::now();
  Core core;
  const auto before = core.stats();
  core.apply({{'X', "0", ""}});
  const auto materialized = core.stats();
  const std::vector<Core::Gate> remaining(gates - 1, {'X', "0", ""});
  core.apply(remaining);
  const auto after_gates = core.stats();
  const auto probability = core.probability_one("0");
  const bool payload_unchanged = same_payload(materialized, after_gates);
  require(payload_unchanged && after_gates.active_qubits == 1 &&
              after_gates.tableau_bytes == 17,
          "repeated operations grew the retained state payload");
  require(probability == 0.0, "10000 X gates failed to restore zero");
  const auto released = core.compact_background();
  const auto compacted = core.stats();
  require(released == 1 && compacted.active_qubits == 0 &&
              compacted.tableau_bytes == 0 && compacted.address_bytes == 0 &&
              core.probability_one("0") == probability,
          "exact background compaction failed to release the recovered zero");
  const auto end = Clock::now();
  out << "{\"case\":\"repeated_gates\",\"background\":\"zero\",\"gates\":" << gates
      << ",\"batches\":2,\"stats_before\":";
  stats_json(out, before);
  out << ",\"stats_after_materialization\":";
  stats_json(out, materialized);
  out << ",\"stats_after_gates\":";
  stats_json(out, after_gates);
  out << ",\"stats_after_compaction\":";
  stats_json(out, compacted);
  out << ",\"payload_unchanged\":" << (payload_unchanged ? "true" : "false")
      << ",\"p_one_before_compaction\":" << probability
      << ",\"released_qubits\":" << released
      << ",\"elapsed_us\":" << microseconds(begin, end) << ",\"ok\":true}";
}

void ghz_component(std::ostream& out) {
  constexpr std::size_t n = 1000;
  const auto begin = Clock::now();
  Core::Limits limits;
  limits.max_component_qubits = 1024;
  Core core(Core::Background::Zero, limits);
  std::vector<Core::Gate> gates;
  gates.reserve(n);
  gates.push_back({'H', "0", ""});
  for (std::size_t q = 1; q < n; ++q)
    gates.push_back({'C', std::to_string(q - 1), std::to_string(q)});
  const auto gate_begin = Clock::now();
  core.apply(gates);
  const auto gate_end = Clock::now();
  const auto actual = core.stats();
  require(actual.active_qubits == n && actual.components == 1 &&
              actual.largest_component == n && actual.tableau_bytes == 257000,
          "GHZ1000 component size or measured tableau payload is incorrect");
  const auto query_begin = Clock::now();
  const auto first = core.probability_one("0"), last = core.probability_one("999");
  const auto correlation = core.expectation({{"0", 'Z'}, {"999", 'Z'}});
  std::vector<Core::Term> all_x;
  all_x.reserve(n);
  for (std::size_t q = 0; q < n; ++q) all_x.push_back({std::to_string(q), 'X'});
  // Z correlations also hold in an incoherent mixture of |0...0> and |1...1>.
  // X on every site distinguishes the prepared coherent GHZ+ state.
  const auto coherence = core.expectation(all_x);
  const auto query_end = Clock::now();
  const auto query_us = microseconds(query_begin, query_end);
  require(query_us <= 30000000.0, "GHZ1000 query exceeded the 30-second probe budget");
  require(first == 0.5 && last == 0.5 && correlation == 1.0 && coherence == 1.0,
          "GHZ1000 queried observables are incorrect");
  require(core.stats() == actual, "GHZ1000 observation changed the state");
  const auto end = Clock::now();
  out << "{\"case\":\"ghz_component\",\"background\":\"zero\",\"qubits\":" << n
      << ",\"gates\":" << gates.size() << ",\"batches\":1,\"component_budget\":"
      << limits.max_component_qubits << ",\"stats_after\":";
  stats_json(out, actual);
  out << ",\"p_first_one\":" << first << ",\"p_last_one\":" << last
      << ",\"z_first_z_last\":" << correlation
      << ",\"x_all\":" << coherence
      << ",\"apply_us\":" << microseconds(gate_begin, gate_end)
      << ",\"query_us\":" << query_us
      << ",\"query_budget_seconds\":30,\"elapsed_us\":" << microseconds(begin, end)
      << ",\"ok\":true}";
}
} // namespace

int main() {
  std::string current_case;
  try {
    // Buffer the report so an exception emits one valid error document, not a
    // half-written array. Timings are in-process wall-clock samples; no RSS,
    // phone/GPU result, global speedup, or allocation count is inferred.
    std::ostringstream report;
    report << std::setprecision(15)
           << "{\"schema\":1,\"probe\":\"infinite_scale\",\"ok\":true,"
           << "\"scope\":\"in-process host wall time and logical retained payload; not process RAM\","
           << "\"address_pattern\":\"remote target = decimal 1 followed by target_decimal_power zeroes\","
           << "\"cases\":[";
    bool separator = false;
    for (const std::size_t exponent : {0, 9, 100, 1000}) {
      if (separator) report << ',';
      separator = true;
      current_case = "remote_bell_10^" + std::to_string(exponent);
      remote_bell(report, exponent);
    }
    report << ',';
    current_case = "implicit_bell_queries";
    implicit_bell_queries(report);
    report << ',';
    current_case = "repeated_gates";
    repeated_gates(report);
    report << ',';
    current_case = "ghz_component";
    ghz_component(report);
    report << "]}";
    std::cout << report.str() << '\n';
    return 0;
  } catch (const std::exception& error) {
    std::cout << "{\"schema\":1,\"probe\":\"infinite_scale\",\"ok\":false,\"case\":"
              << json_string(current_case) << ",\"error\":" << json_string(error.what()) << "}\n";
    return 1;
  }
}
