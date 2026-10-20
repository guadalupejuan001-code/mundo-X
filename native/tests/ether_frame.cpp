#include "mundo_x/infinite_core.hpp"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

using mundo_x::EtherCore;
using mundo_x::InfiniteCore;
static std::size_t checks = 0;
static void require(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F action, const char* message) {
  bool failed = false;
  try { action(); } catch (const std::exception&) { failed = true; }
  require(failed, message);
}

int main() {
  try {
    EtherCore frame;
    frame.h_all();
    const auto y = frame.pullback({{"0", 'Y'}});
    require(y.sign == -1 && y.terms.size() == 1 && y.terms[0].axis == 'Y',
            "H pullback lost the Y sign");
    require(frame.pullback({{"0", 'Y'}}, -1).sign == 1,
            "external sign did not compose with pullback phase");
    require(frame.stats().last_query_peak_sites == 0,
            "pullback mutated shared frame diagnostics");
    frame.cz_neighbors();
    const auto boundary = frame.pullback({{"0", 'X'}, {"1", 'Z'}});
    require(boundary.sign == 1 && boundary.terms.size() == 1 &&
            boundary.terms[0].address == "0" && boundary.terms[0].axis == 'Z',
            "boundary stabilizer did not pull back to Z0");
    const auto negative = frame.pullback({{"0", 'Y'}, {"1", 'X'}, {"2", 'Y'}, {"3", 'Z'}});
    require(negative.sign == -1, "cluster pullback lost its intrinsic negative sign");
    for (const auto& term : negative.terms)
      require(term.axis == 'Z', "negative cluster stabilizer contains a non-Z factor");
    require(frame.expectation({{"0", 'Y'}, {"1", 'X'}, {"2", 'Y'}, {"3", 'Z'}}) == -1,
            "standalone Ether expectation changed");
    EtherCore::QueryLimits cone;
    cone.max_sites = 2;
    rejects([&] { frame.pullback({{"4", 'X'}}, 1, cone); }, "cone budget was not enforced");
    cone.max_sites = 16;
    cone.max_address_digits = 1;
    rejects([&] { frame.pullback({{"9", 'X'}}, 1, cone); }, "expanded address digit cap was ignored");
    EtherCore imported;
    imported.restore_rules(frame.rules(), 2);
    require(imported.expectation({{"0", 'X'}, {"1", 'Z'}}) == 1, "frame import changed state");
    rejects([&] { imported.restore_rules({EtherCore::Rule::HAll, EtherCore::Rule::HAll}); },
            "noncanonical frame accepted");
    rejects([&] { imported.restore_rules({static_cast<EtherCore::Rule>(2)}); },
            "unknown frame rule accepted");
    require(imported.rule_count() == 2, "failed frame import mutated committed rules");

    InfiniteCore fused;
    const auto legacy = fused.snapshot();
    require(legacy.compare(0, 5, "MXI1:") == 0, "empty frame broke legacy snapshot envelope");
    fused.h_all(); fused.cz_neighbors();
    require(fused.stats().ether_rules == 2 && fused.ether_rule_count() == 2,
            "fused state lost its frame");
    require(fused.stats().active_qubits == 0, "global rules materialized the domain");
    const std::vector<InfiniteCore::Term> k0{{"0", 'X'}, {"1", 'Z'}};
    const std::vector<InfiniteCore::Term> k1{{"0", 'Z'}, {"1", 'X'}, {"2", 'Z'}};
    require(fused.expectation(k0) == 1 && fused.expectation(k1) == 1,
            "global cluster was not queried through the shared state");
    const auto before_query = fused.snapshot();
    require(fused.expectation(k1, -1) == -1, "signed physical query failed");
    require(fused.snapshot() == before_query, "query changed persistent state or RNG");
    require(fused.ether_stats().last_query_peak_sites == 3, "missing per-owner cone diagnostics");
    auto independent_copy = fused;
    independent_copy.expectation(k0);
    require(fused.ether_stats().last_query_peak_sites == 3,
            "copy query overwrote another owner's diagnostics");
    fused.apply({{'Z', "1", {}}});
    require(fused.expectation(k1) == -1 && fused.expectation(k0) == 1,
            "local gate did not mutate the same Ether state");
    fused.apply({{'Z', "1", {}}});
    require(fused.expectation(k1) == 1, "physical Pauli roundtrip failed");
    require(fused.compact_background() == 1 && fused.stats().active_qubits == 0,
            "frame-coordinate background compaction failed");
    require(fused.expectation(k0) == 1, "compaction changed physical Ether correlations");

    // An interleaved global layer acts on already existing finite components.
    fused.apply({{'X', "2", {}}});
    fused.h_all();
    require(fused.ether_rule_count() == 3, "global layer discarded local state frame");
    fused.seed(1234);
    const auto checkpoint = fused.snapshot();
    require(checkpoint.compare(0, 5, "MXI2:") == 0, "nonempty frame was not persisted");
    auto restored = InfiniteCore::from_snapshot(checkpoint, fused.limits());
    require(restored.snapshot() == checkpoint, "fused snapshot roundtrip changed bytes");
    for (const auto& id : {"2", "5", "9", "30"}) {
      const auto a = fused.measure_z(id), b = restored.measure_z(id);
      require(a.outcome_one == b.outcome_one && a.probability == b.probability && a.random == b.random,
              "restored physical measurement/RNG sequence diverged");
      require(fused.probability_one(id) == (a.outcome_one ? 1.0 : 0.0),
              "physical measurement did not collapse its target");
    }
    require(fused.snapshot() == restored.snapshot(), "measurement replay changed persisted frame/state/RNG");

    InfiniteCore forced;
    forced.h_all(); forced.cz_neighbors(); forced.h_all();
    forced.seed(777);
    const auto before_force = forced.snapshot();
    require(forced.force_measure_z("2", true) == 0.5, "Pauli postselection probability is wrong");
    require(forced.probability_one("2") == 1.0, "postselection sign/pivot mapping is wrong");
    constexpr std::size_t rng_start = 5 + 2 * (1 + 8 + 8);
    constexpr std::size_t rng_text_size = 2 * (8 + std::mt19937_64::state_size * 8);
    require(before_force.substr(rng_start, rng_text_size) == forced.snapshot().substr(rng_start, rng_text_size),
            "forced measurement consumed portable RNG state");
    const auto committed = forced.snapshot();
    rejects([&] { forced.force_measure_z("2", false); }, "impossible physical postselection accepted");
    require(forced.snapshot() == committed, "impossible postselection changed state");

    InfiniteCore::Limits limits;
    limits.max_ether_rules = 1;
    InfiniteCore bounded(InfiniteCore::Background::Zero, limits);
    bounded.h_all();
    const auto full = bounded.snapshot();
    rejects([&] { bounded.cz_neighbors(); }, "Ether rule cap ignored");
    require(bounded.snapshot() == full, "rejected global rule changed state");
    bounded.h_all();
    require(bounded.ether_rule_count() == 0 && bounded.snapshot().compare(0, 5, "MXI1:") == 0,
            "cancellation at rule capacity was rejected");
    limits.max_ether_rules = 4;
    limits.max_ether_query_sites = 2;
    InfiniteCore small_cone(InfiniteCore::Background::Zero, limits);
    small_cone.h_all(); small_cone.cz_neighbors();
    const auto before_bad_gate = small_cone.snapshot();
    rejects([&] { small_cone.apply({{'H', "4", {}}}); }, "local conjugation exceeded cone budget");
    require(small_cone.snapshot() == before_bad_gate, "partial conjugated gate escaped transaction");
    limits.max_query_terms = 0;
    limits.max_ether_query_sites = 4;
    InfiniteCore fixed_query(InfiniteCore::Background::Zero, limits);
    fixed_query.h_all();
    require(fixed_query.probability_one("0") == 0.5, "fixed probz incorrectly used general query cap");
    require(fixed_query.force_measure_z("0", false) == 0.5,
            "fixed postselection incorrectly used general query cap");

    auto old = InfiniteCore::from_snapshot(legacy, InfiniteCore::Limits{});
    require(old.ether_rule_count() == 0 && old.snapshot() == legacy, "MXI1 restore changed old state");
    auto bad = before_query;
    constexpr std::size_t first_rule = 5 + 2 * (1 + 8 + 8 + 8 + std::mt19937_64::state_size * 8 + 8);
    bad.replace(first_rule, 2, "02");
    const auto before_restore = old.snapshot();
    rejects([&] { old.restore(bad); }, "snapshot accepted unknown Ether byte");
    require(old.snapshot() == before_restore, "bad Ether snapshot changed destination");
    std::cout << "ether_frame: " << checks << " checks passed\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "ether_frame: " << e.what() << '\n';
    return 1;
  }
}
