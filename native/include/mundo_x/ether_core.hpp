#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

// EtherCore v0.1: an exact symbolic model of a countably-infinite 1D qubit
// lattice q0,q1,q2,... for a deliberately small Clifford rule family.
//
// The core never owns one record per qubit. A global layer is one rule that
// applies to every site (or every nearest-neighbour edge), and a finite Pauli
// query is propagated backwards through those rules. Work scales with rule
// depth and the query light cone, not with a stored qubit count.
//
// This is an infinite mathematical domain, not infinite physical memory or
// throughput. v0.1 supports |0>^infinity as the base state plus global H and
// nearest-neighbour CZ layers. Both layers are self-inverse.
class EtherCore {
 public:
  struct Term { std::string address; char axis; };
  enum class Rule : std::uint8_t { HAll, CZNeighbors };

  struct Stats {
    std::size_t rules = 0;
    std::size_t last_query_sites = 0;
    std::size_t last_query_peak_sites = 0;
    std::size_t last_query_address_bytes = 0;
  };

  struct QueryLimits {
    std::size_t max_sites = std::numeric_limits<std::size_t>::max();
    std::size_t max_address_digits = std::numeric_limits<std::size_t>::max();
  };

  // A signed Hermitian Pauli, including the exact phase after conjugation.
  // Diagnostics belong to this result; pullback itself never mutates a shared
  // frame. InfiniteCore can therefore share committed frames across copies.
  struct Observable {
    std::vector<Term> terms;
    int sign = 1;
    Stats stats;
  };

  void h_all() { push_rule(Rule::HAll); }
  void cz_neighbors() { push_rule(Rule::CZNeighbors); }

  std::size_t rule_count() const noexcept { return rules_.size(); }
  const std::vector<Rule>& rules() const noexcept { return rules_; }
  Stats stats() const noexcept { return stats_; }

  void restore_rules(const std::vector<Rule>& rules,
                     std::size_t max_rules = std::numeric_limits<std::size_t>::max()) {
    if (rules.size() > max_rules)
      throw std::length_error("Ether rule budget exceeded");
    for (std::size_t i = 0; i < rules.size(); ++i) {
      if (rules[i] != Rule::HAll && rules[i] != Rule::CZNeighbors)
        throw std::invalid_argument("unknown Ether rule");
      if (i && rules[i] == rules[i - 1])
        throw std::invalid_argument("Ether snapshot contains uncancelled adjacent rules");
    }
    auto candidate = rules;
    rules_.swap(candidate);
    stats_ = Stats{};
    stats_.rules = rules_.size();
  }

  double expectation(const std::vector<Term>& terms, int sign = 1) const {
    const auto observable = pullback(terms, sign);
    stats_ = observable.stats;
    for (const auto& term : observable.terms)
      if (term.axis == 'X' || term.axis == 'Y') return 0.0;
    return static_cast<double>(observable.sign);
  }

  Observable pullback(const std::vector<Term>& terms, int sign = 1) const {
    return pullback(terms, sign, QueryLimits{});
  }

  // Return U^dagger P U. This is valid for any input state beneath the frame,
  // not only the zero background used by the standalone expectation helper.
  Observable pullback(const std::vector<Term>& terms, int sign,
                      const QueryLimits& limits) const {
    if (sign != 1 && sign != -1)
      throw std::invalid_argument("Pauli sign must be +1 or -1");

    Pauli p;
    p.phase = sign == 1 ? 0 : 2;
    std::set<std::string, AddressLess> seen;
    std::size_t address_bytes = 0;
    for (const auto& term : terms) {
      check_address(term.address, limits);
      const auto address = canonical(term.address);
      if (!seen.insert(address).second)
        throw std::invalid_argument("duplicate canonical address in Pauli query");
      if (address.size() > std::numeric_limits<std::size_t>::max() - address_bytes)
        throw std::length_error("Pauli address text overflows");
      address_bytes += address.size();
      switch (term.axis) {
        case 'I': break;
        case 'X': set_bits(p, address, true, false, limits); break;
        case 'Z': set_bits(p, address, false, true, limits); break;
        case 'Y':
          p.phase = (p.phase + 1) & 3;
          set_bits(p, address, true, true, limits);
          break;
        default: throw std::invalid_argument("Pauli axis must be I, X, Y or Z");
      }
    }

    std::size_t peak = p.cells.size();
    for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
      if (*it == Rule::HAll) conjugate_h_all(p);
      else conjugate_cz_neighbors(p, limits, peak);
      peak = std::max(peak, p.cells.size());
    }

    Observable out;
    out.terms.reserve(p.cells.size());
    unsigned y_count = 0;
    for (const auto& item : p.cells) {
      const auto bits = item.second;
      const char axis = bits.x ? (bits.z ? 'Y' : 'X') : 'Z';
      if (axis == 'Y') y_count = (y_count + 1U) & 3U;
      out.terms.push_back({item.first, axis});
    }
    const unsigned residual = (static_cast<unsigned>(p.phase) + 4U - y_count) & 3U;
    if (residual & 1U)
      throw std::logic_error("non-Hermitian phase after Clifford propagation");
    out.sign = residual == 0 ? 1 : -1;
    out.stats = {rules_.size(), p.cells.size(), peak, address_bytes};
    return out;
  }

 private:
  struct Bits { bool x=false, z=false; };
  struct AddressLess {
    bool operator()(const std::string& a, const std::string& b) const noexcept {
      if (a.size() != b.size()) return a.size() < b.size();
      return a < b;
    }
  };
  using CellMap = std::map<std::string, Bits, AddressLess>;
  struct Pauli { CellMap cells; int phase = 0; };

  std::vector<Rule> rules_;
  mutable Stats stats_;

  void push_rule(Rule r) {
    if (!rules_.empty() && rules_.back() == r) rules_.pop_back();
    else rules_.push_back(r);
    stats_.rules = rules_.size();
  }

  static std::string canonical(const std::string& input) {
    if (input.empty()) throw std::invalid_argument("address cannot be empty");
    for (const char c : input)
      if (c < '0' || c > '9')
        throw std::invalid_argument("address requires ASCII decimal digits");
    const auto first = input.find_first_not_of('0');
    return first == std::string::npos ? std::string("0") : input.substr(first);
  }

  static std::string successor(const std::string& canonical_address) {
    std::string out = canonical_address;
    for (std::size_t i = out.size(); i-- > 0;) {
      if (out[i] != '9') { ++out[i]; return out; }
      out[i] = '0';
    }
    return std::string("1") + out;
  }

  static std::string predecessor(const std::string& canonical_address) {
    if (canonical_address == "0")
      throw std::invalid_argument("q0 has no predecessor");
    std::string out = canonical_address;
    for (std::size_t i = out.size(); i-- > 0;) {
      if (out[i] != '0') { --out[i]; break; }
      out[i] = '9';
    }
    if (out.size() > 1 && out.front() == '0') out.erase(out.begin());
    return out;
  }

  static Bits get_bits(const Pauli& p, const std::string& address) {
    const auto it = p.cells.find(address);
    return it == p.cells.end() ? Bits{} : it->second;
  }

  static void check_address(const std::string& address, const QueryLimits& limits) {
    if (address.size() > limits.max_address_digits)
      throw std::length_error("Ether query address digit budget exceeded");
  }

  static void set_bits(Pauli& p, const std::string& address, bool x, bool z,
                       const QueryLimits& limits) {
    if (!x && !z) { p.cells.erase(address); return; }
    check_address(address, limits);
    if (p.cells.find(address) == p.cells.end() && p.cells.size() >= limits.max_sites)
      throw std::length_error("Ether query cone site budget exceeded");
    p.cells[address] = Bits{x,z};
  }

  static void toggle_z(Pauli& p, const std::string& address,
                       const QueryLimits& limits, std::size_t& peak) {
    auto bits = get_bits(p,address);
    bits.z = !bits.z;
    set_bits(p,address,bits.x,bits.z,limits);
    peak = std::max(peak, p.cells.size());
  }

  static void conjugate_h_all(Pauli& p) {
    for (auto& item : p.cells) {
      auto& bits = item.second;
      if (bits.x && bits.z) p.phase = (p.phase + 2) & 3;
      std::swap(bits.x,bits.z);
    }
  }

  static void conjugate_cz_neighbors(Pauli& p, const QueryLimits& limits,
                                     std::size_t& peak) {
    // CZ leaves every X bit invariant. A visited X cell therefore cannot be
    // erased by toggle_z, and std::map insertion/erasure of other cells leaves
    // this iterator valid. Newly inserted cells have X=0 and are skipped. Walk
    // the invariant support directly: no copied addresses or temporary vector,
    // including for all-Z queries. Each adjacent XX pair contributes its sign
    // once, via the right neighbour.
    for (auto it = p.cells.begin(); it != p.cells.end(); ++it) {
      if (!it->second.x) continue;
      const auto& q = it->first;
      auto next = successor(q);
      check_address(next, limits);
      if (get_bits(p,next).x) p.phase = (p.phase + 2) & 3;
      if (q != "0") toggle_z(p,predecessor(q),limits,peak);
      toggle_z(p,next,limits,peak);
    }
  }
};

} // namespace mundo_x
