#include "mundo_x/ether_core.hpp"

#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>

using E = mundo_x::EtherCore;
using R = E::Rule;
static std::size_t checks = 0;
static void require(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
template<class F> static void rejects(F action, const char* message) {
  bool rejected = false;
  try { action(); } catch (const std::exception&) { rejected = true; }
  require(rejected, message);
}
static R flip(R rule) { return rule == R::HAll ? R::CZNeighbors : R::HAll; }
static void push(std::vector<R>& stack, R rule) {
  if (!stack.empty() && stack.back() == rule) stack.pop_back();
  else stack.push_back(rule);
}
static void compare_word(const E& frame, const std::vector<R>& stack) {
  const auto view = frame.rules();
  require(view.size() == stack.size(), "normal form length differs from stack reduction");
  require(std::vector<R>(view.begin(), view.end()) == stack, "forward rule view differs");
  require(std::vector<R>(view.rbegin(), view.rend()) == std::vector<R>(stack.rbegin(), stack.rend()),
          "reverse rule view differs");
}

// Independent finite-window oracle: visit every site/edge at every layer.
// Its right edge is beyond the causal cone; q0 is the actual left boundary.
static E::Observable reference(R first, std::size_t depth, const std::vector<E::Term>& terms, int sign) {
  std::size_t width = depth + 3;
  for (const auto& term : terms) width = std::max(width, std::size_t(std::stoull(term.address)) + depth + 3);
  std::vector<unsigned> x(width), z(width);
  unsigned phase = sign == -1 ? 2 : 0;
  for (const auto& term : terms) {
    const auto q = std::stoull(term.address);
    x[q] = term.axis == 'X' || term.axis == 'Y';
    z[q] = term.axis == 'Z' || term.axis == 'Y';
    phase += term.axis == 'Y';
  }
  for (std::size_t n = depth; n-- > 0;) {
    const auto rule = (n & 1) ? flip(first) : first;
    if (rule == R::HAll) {
      for (std::size_t q = 0; q < width; ++q) {
        phase += 2 * x[q] * z[q]; std::swap(x[q], z[q]);
      }
    } else {
      for (std::size_t q = 0; q + 1 < width; ++q) {
        phase += 2 * x[q] * x[q + 1];
        z[q] ^= x[q + 1]; z[q + 1] ^= x[q];
      }
    }
  }
  E::Observable result;
  for (std::size_t q = 0; q < width; ++q) {
    if (!x[q] && !z[q]) continue;
    const char a = x[q] ? (z[q] ? 'Y' : 'X') : 'Z';
    if (a == 'Y') --phase;
    result.terms.push_back({std::to_string(q), a});
  }
  require(!(phase & 1), "oracle produced a non-Hermitian observable");
  result.sign = (phase & 2) ? -1 : 1;
  return result;
}
static void same(const E::Observable& a, const E::Observable& b) {
  require(a.sign == b.sign && a.terms.size() == b.terms.size(), "generator sign/support differs");
  for (std::size_t i = 0; i < a.terms.size(); ++i)
    require(a.terms[i].address == b.terms[i].address && a.terms[i].axis == b.terms[i].axis,
            "generator term differs");
}

int main() {
  try {
    for (unsigned depth = 0; depth <= 15; ++depth) {
      for (unsigned word = 0; word < (1U << depth); ++word) {
        E frame; std::vector<R> stack;
        for (unsigned j = 0; j < depth; ++j) {
          const auto rule = (word & (1U << j)) ? R::HAll : R::CZNeighbors;
          frame.append_alternating(rule, 1); push(stack, rule);
        }
        compare_word(frame, stack);
      }
    }
    std::mt19937_64 rng(42);
    E frame; std::vector<R> stack;
    for (unsigned trial = 0; trial < 5000; ++trial) {
      const auto first = rng() & 1 ? R::HAll : R::CZNeighbors;
      const auto count = rng() % 128;
      frame.append_alternating(first, count);
      for (std::size_t i = 0; i < count; ++i) push(stack, (i & 1) ? flip(first) : first);
      compare_word(frame, stack);
    }
    const auto held = frame.rules();
    const auto held_word = std::vector<R>(held.begin(), held.end());
    frame.h_all(); frame.cz_neighbors();
    require(std::vector<R>(held.begin(), held.end()) == held_word, "value view changed after mutation");
    frame.restore_rules(held);
    compare_word(frame, held_word);
    const auto before = frame.rule_run();
    rejects([&] { frame.append_alternating(static_cast<R>(7), 0); }, "invalid zero-length rule accepted");
    rejects([&] { frame.restore_rules({R::HAll, R::HAll}); }, "noncanonical import accepted");
    require(frame.rule_run().first == before.first && frame.rule_count() == before.count,
            "rejected operation changed frame");
    frame.restore_rule_run(R::CZNeighbors, 12);
    rejects([&] { frame.append_alternating(R::CZNeighbors, 1, 12); }, "depth budget ignored");
    require(frame.rule_count() == 12, "budget failure changed frame");
    frame.append_alternating(R::HAll, 12, 0);
    require(frame.rule_count() == 0, "bulk inverse did not cancel at capacity");

    for (const auto first : {R::HAll, R::CZNeighbors}) {
      for (std::size_t depth = 0; depth <= 96; ++depth) {
        frame.restore_rule_run(first, depth);
        for (std::size_t q = 0; q < 20; ++q) {
          for (const auto axis : {'X', 'Y', 'Z'}) {
            for (const int sign : {-1, 1})
              same(frame.pullback({{std::to_string(q), axis}}, sign),
                   reference(first, depth, {{std::to_string(q), axis}}, sign));
          }
        }
      }
    }
    for (unsigned trial = 0; trial < 1000; ++trial) {
      const auto first = rng() & 1 ? R::HAll : R::CZNeighbors;
      const auto depth = 32 + rng() % 129;
      const auto offset = rng() % 32;
      const int sign = rng() & 1 ? -1 : 1;
      std::vector<E::Term> terms;
      for (unsigned j = 0; j < 8; ++j)
        terms.push_back({std::to_string(offset + j), "IXYZ"[rng() % 4]});
      frame.restore_rule_run(first, depth);
      same(frame.pullback(terms, sign), reference(first, depth, terms, sign));
    }
    // Products larger than the former 16-site sparse route, including mixed
    // Y phases, disconnected intervals and both sides of decimal carries.
    for (unsigned trial = 0; trial < 320; ++trial) {
      const auto first = rng() & 1 ? R::HAll : R::CZNeighbors;
      const auto depth = 32 + rng() % 481;
      const auto count = 17 + rng() % 112;
      const auto offset = rng() % 128;
      std::vector<E::Term> terms;
      for (std::size_t j = 0; j < count; ++j)
        terms.push_back({std::to_string(offset + j * (trial % 3 + 1)), "IXYZ"[rng() % 4]});
      const int sign = rng() & 1 ? -1 : 1;
      frame.restore_rule_run(first, depth);
      same(frame.pullback(terms, sign), reference(first, depth, terms, sign));
    }
    // This self-inverse frame maps a two-site string through enormously large
    // intermediate generator images. Cancellation must precede expansion.
    // Increasing logical depth must not enumerate those intervals.
    for (std::size_t depth : {199ULL, 1000000000001ULL}) {
      frame.restore_rule_run(R::HAll, depth);
      const auto image = frame.pullback({{"0", 'Z'}});
      E::QueryLimits small; small.max_sites = 2;
      const auto restored = frame.pullback(image.terms, image.sign, small);
      same(restored, reference(R::HAll, 0, {{"0", 'Z'}}, 1));
      require(restored.stats.last_query_peak_sites <= 2,
              "cancelled symbolic interior was materialized");
    }
    frame.restore_rule_run(R::CZNeighbors, 512);
    std::vector<E::Term> bounded_terms;
    for (unsigned q = 0; q < 64; ++q)
      bounded_terms.push_back({std::to_string(1000 + q), "XYZ"[q % 3]});
    const auto bounded_result = frame.pullback(bounded_terms, -1);
    E::QueryLimits exact_budget; exact_budget.max_sites = bounded_result.terms.size();
    same(frame.pullback(bounded_terms, -1, exact_budget), bounded_result);
    --exact_budget.max_sites;
    rejects([&] { frame.pullback(bounded_terms, -1, exact_budget); },
            "mixed interval output exceeded explicit support budget");
    // High decimal addresses share long prefixes but remain independent.
    // Translation away from q0 commutes with the finite cone evolution.
    for (unsigned trial = 0; trial < 40; ++trial) {
      const auto first = rng() & 1 ? R::HAll : R::CZNeighbors;
      const std::size_t depth = 32 + rng() % 97;
      const std::string prefix = "1" + std::string(trial % 2 ? 4090 : 122, '0');
      std::vector<E::Term> small, large;
      for (unsigned j = 0; j < 24; ++j) {
        auto id = std::to_string(2000 + j * 3);
        const auto axis = "XYZ"[rng() % 3];
        small.push_back({id, axis}); large.push_back({prefix + id, axis});
      }
      auto expected = reference(first, depth, small, -1);
      // All result addresses are four digits in this deliberately bounded cone.
      for (auto& term : expected.terms) term.address = prefix + term.address;
      frame.restore_rule_run(first, depth);
      same(frame.pullback(large, -1), expected);
    }
    // An odd alternating word is its own inverse. The combined observable
    // moves left from q98/q99, while separate generator images would reach
    // three-digit addresses. The optional sparse path must fall back exactly.
    frame.restore_rule_run(R::HAll, 199);
    const auto prepared = frame.pullback({{"0", 'Z'}});
    E::QueryLimits fallback; fallback.max_address_digits = 2;
    same(frame.pullback(prepared.terms, prepared.sign, fallback), reference(R::HAll, 0, {{"0", 'Z'}}, 1));
    fallback.max_address_digits = 4096; fallback.max_sites = 4;
    same(frame.pullback(prepared.terms, prepared.sign, fallback), reference(R::HAll, 0, {{"0", 'Z'}}, 1));
    frame.restore_rule_run(R::HAll, 4096);
    E::QueryLimits tiny; tiny.max_sites = 2;
    const auto boundary = frame.pullback({{"0", 'X'}}, 1, tiny);
    require(boundary.terms.size() == 2 && boundary.stats.last_query_peak_sites == 2,
            "closed boundary query retained layer-replay scratch");
    tiny.max_sites = 1;
    rejects([&] { frame.pullback({{"0", 'X'}}, 1, tiny); }, "output support cap bypassed");
    tiny.max_sites = 10000; tiny.max_address_digits = 1;
    rejects([&] { frame.pullback({{"0", 'X'}}, 1, tiny); }, "expanded address cap bypassed");
    rejects([&] { frame.pullback({{"0", 'I'}, {"00", 'I'}}); }, "empty alias query accepted");
    rejects([&] { frame.pullback({{"0", '?'}}); }, "invalid axis accepted");

    const auto maximum = std::numeric_limits<std::size_t>::max();
    frame.restore_rule_run(R::HAll, maximum);
    const auto tail = frame.rules().back();
    rejects([&] { frame.append_alternating(flip(tail), 1); }, "rule count overflow accepted");
    require(frame.rule_count() == maximum, "overflow changed frame");
    require(frame.pullback({}, -1).sign == -1, "identity query replayed/changed huge frame");
    require(frame.pullback({{"0", 'Z'}}).terms.size() <= 2, "huge boundary query failed");
    frame.append_alternating(tail, maximum);
    require(frame.rule_count() == 0, "maximum-length inverse failed");
    frame.restore_rule_run(R::CZNeighbors, maximum);
    rejects([&] { frame.pullback({{std::to_string(maximum) + "0", 'X'}}); },
            "closed support size overflow accepted");
    std::cout << "ether_rules: " << checks << " checks passed; seed=42\n";
  } catch (const std::exception& e) {
    std::cerr << "ether_rules: " << e.what() << '\n'; return 1;
  }
}
