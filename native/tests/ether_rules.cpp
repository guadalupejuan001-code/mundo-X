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
