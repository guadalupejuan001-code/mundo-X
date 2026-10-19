#include "mundo_x/infinite_session.hpp"

#include <atomic>
#include <iostream>
#include <random>
#include <thread>

namespace {
using Core = mundo_x::InfiniteCore;
using Session = mundo_x::InfiniteSession;
using Compact = mundo_x::CompactCore;
std::size_t checks = 0, packed_states = 0, rng_draws = 0;
std::size_t rejected_imports = 0, rollbacks = 0;

void check(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}

template<class Function>
void rejects(Function function, const char* message) {
  bool failed = false;
  try { function(); } catch (const std::exception&) { failed = true; }
  check(failed, message);
}

unsigned nibble(char value) {
  return static_cast<unsigned>(value <= '9' ? value - '0' : value - 'a' + 10);
}
std::uint64_t word(const std::string& text, std::size_t offset) {
  std::uint64_t value = 0;
  for (unsigned i = 0; i < 8; ++i) {
    const auto pos = 5 + 2 * (offset + i);
    value |= std::uint64_t{nibble(text[pos]) * 16 + nibble(text[pos + 1])} << (8 * i);
  }
  return value;
}
void byte(std::string& text, std::size_t offset, unsigned value) {
  constexpr char hex[] = "0123456789abcdef";
  text.at(5 + 2 * offset) = hex[(value >> 4) & 15];
  text.at(6 + 2 * offset) = hex[value & 15];
}
void word(std::string& text, std::size_t offset, std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) { byte(text, offset + i, static_cast<unsigned>(value)); value >>= 8; }
}

void packed_roundtrips() {
  std::mt19937_64 rng(9917);
  for (const std::size_t n : {1U, 2U, 5U, 63U, 64U, 65U, 127U, 128U, 129U}) {
    Compact source(n);
    for (std::size_t step = 0; step < 500; ++step) {
      const auto a = static_cast<std::size_t>(rng() % n);
      const auto b = static_cast<std::size_t>(rng() % n);
      switch (rng() % 6) {
        case 0: source.hadamard(a); break;
        case 1: source.phase_s(a); break;
        case 2: source.pauli_x(a); break;
        case 3: source.pauli_z(a); break;
        case 4: if (a != b) source.cnot(a, b); break;
        default: if (a != b) source.controlled_z(a, b); break;
      }
    }
    auto imported = Compact::from_packed_state(source.packed_state());
    for (std::size_t q = 0; q < n; ++q) {
      check(source.generator_string(q) == imported.generator_string(q), "packed signed row changed");
      check(source.probability_one_z(q) == imported.probability_one_z(q), "packed Z probability changed");
    }
    ++packed_states;
  }
  auto saved = Compact(2).packed_state();
  auto bad = saved;
  bad.signs[0] = 2;
  rejects([&] { Compact::from_packed_state(bad); }, "non-binary sign accepted");
  bad = saved; bad.x[0] = std::uint64_t{1} << 63;
  rejects([&] { Compact::from_packed_state(bad); }, "nonzero padding accepted");
  bad = saved; bad.x[0] = 1; bad.z[0] = 0; bad.z[1] = 1;
  rejects([&] { Compact::from_packed_state(bad); }, "anticommuting generators accepted");
  bad = saved; bad.z[1] = bad.z[0];
  rejects([&] { Compact::from_packed_state(bad); }, "dependent generators accepted");
  bad = saved; bad.z.pop_back();
  rejects([&] { Compact::from_packed_state(bad); }, "wrong packed dimensions accepted");
  bad = saved; bad.qubits = std::numeric_limits<std::size_t>::max();
  rejects([&] { Compact::from_packed_state(bad); }, "overflowing packed dimensions accepted");
  Compact basis(2); basis.pauli_x(0);
  auto canonical = Compact::from_packed_state(basis.packed_state());
  check(canonical.computational_basis_bit(0), "basis flag/sign was not derived");
  basis.cnot(0, 1);
  auto noncanonical = Compact::from_packed_state(basis.packed_state());
  rejects([&] { (void)noncanonical.computational_basis_bit(0); }, "noncanonical flag was trusted");
  check(noncanonical.probability_one_z(1) == 1, "basis import lost correlation");
}

void rng_oracle() {
  using Engine = std::mt19937_64;
  for (const std::uint64_t seed : {std::uint64_t{0}, std::uint64_t{42},
                                  std::numeric_limits<std::uint64_t>::max()}) {
    Core core;
    core.seed(seed);
    Engine reference(seed);
    for (std::size_t i = 0; i < 2500; ++i) {
      core.apply({{'H', "0", ""}});
      const auto expected = reference();
      const auto result = core.measure_z("0");
      check(result.random && result.probability == 0.5, "oracle preparation is not random");
      check(result.outcome_one == ((expected & 1) != 0), "MT19937-64 measurement sequence changed");
      const auto saved = core.snapshot();
      const auto position = word(saved, 17);
      check(position >= 1 && position <= Engine::state_size, "serialized RNG position invalid");
      auto full_output = word(saved, 25 + 8 * (static_cast<std::size_t>(position) - 1));
      full_output ^= (full_output >> Engine::tempering_u) & Engine::tempering_d;
      full_output ^= (full_output << Engine::tempering_s) & Engine::tempering_b;
      full_output ^= (full_output << Engine::tempering_t) & Engine::tempering_c;
      full_output ^= full_output >> Engine::tempering_l;
      check(full_output == expected, "full 64-bit portable MT output differs from standard engine");
      if (i % 311 == 0) {
        auto imported = Core::from_snapshot(saved, core.limits());
        check(imported.snapshot() == saved, "RNG/state snapshot roundtrip is not exact");
        core = std::move(imported);
      }
      ++rng_draws;
    }
  }
}

void snapshots() {
  const std::string far = "1" + std::string(100, '0');
  Session source(Core::Background::BellPairs);
  source.execute("seed 173\nmeasure 8\nz 0\nh " + far + "\n");
  const auto saved = source.snapshot();
  check(saved.rfind("MXI1:", 0) == 0, "snapshot has no versioned ASCII envelope");
  Core::Limits consumer_limits;
  consumer_limits.max_active_qubits = 16;
  consumer_limits.max_component_qubits = 4;
  consumer_limits.max_query_terms = 8;
  Session target(Core::Background::Zero, consumer_limits);
  target.restore(saved);
  check(target.snapshot() == saved, "snapshot failed exact current-state restoration");
  check(target.background() == Core::Background::BellPairs, "snapshot background not restored");
  check(target.limits().max_active_qubits == 16 && target.limits().max_query_terms == 8,
        "snapshot replaced consumer budgets");
  check(source.execute("measure 4\nmeasure 6\n") == target.execute("measure 4\nmeasure 6\n"),
        "snapshot RNG continuation changed");
  check(source.snapshot() == target.snapshot(), "same continued measurement state differs");
  const auto epoch = target.epoch();
  const auto current = target.snapshot();
  target.restore(current);
  check(target.snapshot() == current && target.epoch() == epoch + 1,
        "equal-revision restore did not invalidate observer epoch");

  auto reject_import = [&](const std::string& bad) {
    const auto before = target.snapshot(); const auto serial = target.epoch();
    rejects([&] { target.restore(bad); }, "invalid snapshot accepted");
    check(target.snapshot() == before && target.epoch() == serial,
          "invalid snapshot changed state/RNG/epoch");
    ++rejected_imports;
  };
  reject_import("");
  reject_import("MXI2:" + saved.substr(5));
  reject_import(saved.substr(0, saved.size() - 1));
  reject_import(saved.substr(0, saved.size() - 2));
  reject_import(saved + "00");
  auto bad = saved; bad[5] = 'g'; reject_import(bad);
  bad = saved; byte(bad, 0, 2); reject_import(bad);
  bad = saved; word(bad, 9, std::numeric_limits<std::uint64_t>::max()); reject_import(bad);
  bad = saved; word(bad, 17, 313); reject_import(bad);
  bad = saved;
  for (std::size_t i = 0; i < 312 * 8; ++i) byte(bad, 25 + i, 0);
  reject_import(bad);

  Core bell(Core::Background::BellPairs);
  bell.apply({{'Z', "0", ""}});
  const auto pair = bell.snapshot();
  constexpr std::size_t components_offset = 25 + 312 * 8;
  // The first component has n=2, id lengths 1/1, then signs, X, Z.
  bad = pair; byte(bad, components_offset + 8 + 8, '2'); reject_import(bad);
  bad = pair; byte(bad, components_offset + 8 + 8 + 1 + 8, '0'); reject_import(bad);
  bad = pair; byte(bad, components_offset + 8 + 9 + 9, 2); reject_import(bad);

  for (unsigned budget = 0; budget < 4; ++budget) {
    Core::Limits limited;
    if (budget == 0) limited.max_active_qubits = 1;
    if (budget == 1) limited.max_component_qubits = 1;
    if (budget == 2) limited.max_state_bytes = 33;
    if (budget == 3) limited.max_address_digits = 100;
    Session constrained(Core::Background::Zero, limited);
    const auto before = constrained.snapshot();
    rejects([&] { constrained.restore(budget == 3 ? saved : pair); }, "consumer import budget ignored");
    check(constrained.snapshot() == before && constrained.epoch() == 0,
          "resource rejection changed receiving session");
    ++rejected_imports;
  }

  // A full default-budget component also has to survive decoding. This
  // exercises 16 packed words per row and genuine GHZ coherence, not only Z
  // correlations that could also belong to a classical mixture.
  Session large;
  std::vector<Session::Gate> ghz{{'H', "0", ""}};
  Session::Observable coherence;
  for (std::size_t q = 0; q < 1024; ++q) {
    coherence.terms.push_back({std::to_string(q), 'X'});
    if (q) ghz.push_back({'C', std::to_string(q - 1), std::to_string(q)});
  }
  large.apply(ghz);
  const auto large_snapshot = large.snapshot();
  Session large_copy;
  large_copy.restore(large_snapshot);
  const auto view = large_copy.query_batch({coherence, mundo_x::parse_observable("Z:0 Z:1023")});
  check(view.values == std::vector<double>({1, 1}) &&
        view.stats.active_qubits == 1024 && view.stats.tableau_bytes == 263168 &&
        large_copy.probability_one("0") == 0.5, "1024-qubit GHZ snapshot lost state or coherence");
  check(view.observer_builds == 1,
        "query batch rebuilt the same 1024-qubit Pauli observer");
  check(large_copy.snapshot() == large_snapshot, "1024-qubit packed snapshot changed on import");
}

void transactions() {
  Session session;
  session.execute("seed 889\nh 0\n");
  auto rollback = [&](const std::string& program) {
    const auto before = session.snapshot(); const auto epoch = session.epoch();
    rejects([&] { (void)session.execute(program); }, "bad request accepted");
    check(session.snapshot() == before && session.epoch() == epoch,
          "request rejection changed state/RNG/epoch");
    ++rollbacks;
  };
  rollback("measure 0\nx 1\nassert probz 999 1\n");
  rollback("measure 0\nt 1\n");
  rollback("background bell\nseed 44\nmeasure 8\npostselect 9 2\n");
  rollback("begin\nx 0\nend\nbegin\nh 1\n");
  rollback("measure 0\nexpect X:1 Z:01\n");
  rollback("x 0\n" + std::string(mundo_x::infinite_program_detail::max_line_bytes + 1, '#'));
  const auto before = session.snapshot(); const auto epoch = session.epoch();
  rejects([&] {
    session.execute_prepared("measure 0\n", [](const std::string&) -> std::string {
      throw std::bad_alloc();
    });
  }, "transport allocation exception was swallowed");
  check(session.snapshot() == before && session.epoch() == epoch,
        "response allocation failure committed a measurement");
  ++rollbacks;

  const auto stats_line = session.execute("stats\n").size();
  std::string response_overflow = "measure 0\n";
  for (std::size_t i = 0; i <= mundo_x::infinite_program_detail::max_response_bytes / stats_line; ++i)
    response_overflow += "stats\n";
  rollback(response_overflow);
  check(session.execute("# comment\nassert probz 0 0.5\nbegin\nend\n").empty(),
        "empty request or assertion emitted output");
  session.query_batch({mundo_x::parse_observable("X:0"), mundo_x::parse_observable("- Y:0")});
  check(session.snapshot() == before && session.epoch() == epoch,
        "read-only operations changed saved state or epoch");

  Session remote(Core::Background::BellPairs);
  const auto implicit = remote.query_batch({mundo_x::parse_observable("X:1000000000 X:1000000001"),
                                           mundo_x::parse_observable("Y:1000000000 Y:1000000001")});
  check(implicit.values == std::vector<double>({1, -1}) && implicit.stats.active_qubits == 0 &&
        implicit.revision == 0 && implicit.epoch == 0, "implicit query materialized or changed metadata");
  remote.reset(Core::Background::Zero);
  check(remote.background() == Core::Background::Zero && remote.stats().revision == 0 &&
        remote.epoch() == 1, "reset did not establish a fresh zero session");
}

void concurrent_observations() {
  Session shared;
  shared.apply({{'H', "0", ""}});
  std::atomic<bool> valid{true};
  std::thread writer([&] {
    try { for (unsigned i = 0; i < 500; ++i) shared.apply({{'Z', "0", ""}}); }
    catch (...) { valid = false; }
  });
  for (unsigned i = 0; i < 1000; ++i) {
    const auto view = shared.query_batch({mundo_x::parse_observable("X:0"),
                                          mundo_x::parse_observable("Z:0")});
    if (view.values != std::vector<double>({view.revision % 2 ? 1.0 : -1.0, 0.0}) ||
        view.epoch != view.revision || view.stats.revision != view.revision)
      valid = false;
  }
  writer.join();
  check(valid, "query values and version metadata came from different states");
  check(shared.stats().revision == 501, "concurrent writer lost operations");
}
}  // namespace

int main() {
  try {
    packed_roundtrips(); rng_oracle(); snapshots(); transactions(); concurrent_observations();
    std::cout << "{\"suite\":\"infinite_session\",\"ok\":true,\"checks\":" << checks
              << ",\"packed_states\":" << packed_states << ",\"rng_full64_draws\":" << rng_draws
              << ",\"rejected_imports\":" << rejected_imports
              << ",\"request_rollbacks\":" << rollbacks << ",\"concurrent_observations\":1000}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "infinite_session: " << error.what() << '\n'; return 1;
  }
}
