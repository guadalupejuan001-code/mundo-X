#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iterator>
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

  // With two involutions, cancellation of equal neighbours leaves an
  // alternating word. Its first letter and length describe every rule
  // exactly, without storing or copying a vector proportional to its depth.
  struct RuleRun { Rule first = Rule::HAll; std::size_t count = 0; };
  class RuleView {
   public:
    class iterator {
     public:
      using iterator_category = std::bidirectional_iterator_tag;
      using value_type = Rule;
      using difference_type = std::ptrdiff_t;
      using pointer = void;
      using reference = Rule;
      iterator() = default;
      Rule operator*() const { return at(first_, index_); }
      iterator& operator++() { ++index_; return *this; }
      iterator operator++(int) { auto old = *this; ++*this; return old; }
      iterator& operator--() { --index_; return *this; }
      iterator operator--(int) { auto old = *this; --*this; return old; }
      bool operator==(const iterator& other) const {
        return first_ == other.first_ && index_ == other.index_;
      }
      bool operator!=(const iterator& other) const { return !(*this == other); }
     private:
      friend class RuleView;
      iterator(Rule first, std::size_t index) : first_(first), index_(index) {}
      Rule first_ = Rule::HAll;
      std::size_t index_ = 0;
    };
    std::size_t size() const noexcept { return run_.count; }
    bool empty() const noexcept { return size() == 0; }
    Rule operator[](std::size_t index) const { return at(run_.first, index); }
    Rule front() const {
      if (empty()) throw std::out_of_range("empty Ether rule sequence");
      return run_.first;
    }
    Rule back() const {
      if (empty()) throw std::out_of_range("empty Ether rule sequence");
      return (*this)[size() - 1];
    }
    iterator begin() const { return iterator(run_.first, 0); }
    iterator end() const { return iterator(run_.first, size()); }
    std::reverse_iterator<iterator> rbegin() const { return std::make_reverse_iterator(end()); }
    std::reverse_iterator<iterator> rend() const { return std::make_reverse_iterator(begin()); }
   private:
    friend class EtherCore;
    explicit RuleView(RuleRun run) : run_(run) {}
    static Rule at(Rule first, std::size_t index) {
      return static_cast<Rule>(static_cast<unsigned>(first) ^ (index & 1U));
    }
    RuleRun run_;
  };

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

  std::size_t rule_count() const noexcept { return run_.count; }
  RuleRun rule_run() const noexcept { return run_; }
  RuleView rules() const noexcept { return RuleView(run_); }
  Stats stats() const noexcept { return stats_; }

  void restore_rule_run(Rule first, std::size_t count,
                        std::size_t max_rules = std::numeric_limits<std::size_t>::max()) {
    validate_rule(first);
    if (count > max_rules) throw std::length_error("Ether rule budget exceeded");
    run_ = {count ? first : Rule::HAll, count};
    stats_ = Stats{};
    stats_.rules = count;
  }

  // Compose another alternating word, including cancellation across the
  // boundary. Check arithmetic and the resulting depth before changing state.
  void append_alternating(Rule first, std::size_t count,
                          std::size_t max_rules = std::numeric_limits<std::size_t>::max()) {
    validate_rule(first);
    auto candidate = run_;
    if (candidate.count && count && rules().back() == first) {
      const auto cancelled = std::min(candidate.count, count);
      candidate.count -= cancelled;
      count -= cancelled;
      first = RuleView::at(first, cancelled);
    }
    if (count > std::numeric_limits<std::size_t>::max() - candidate.count)
      throw std::overflow_error("Ether rule count overflow");
    if (!candidate.count) candidate.first = count ? first : Rule::HAll;
    candidate.count += count;
    if (candidate.count > max_rules) throw std::length_error("Ether rule budget exceeded");
    run_ = candidate;
    stats_.rules = rule_count();
  }

  void restore_rules(RuleView rules,
                     std::size_t max_rules = std::numeric_limits<std::size_t>::max()) {
    restore_rule_run(rules.empty() ? Rule::HAll : rules.front(), rules.size(), max_rules);
  }

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
    restore_rule_run(rules.empty() ? Rule::HAll : rules.front(), rules.size(), max_rules);
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

    // A single X or Z has an exact closed pullback on the half-line. Avoid
    // replaying a deep alternating word (and rebuilding its cone each layer).
    // Mixed/Y observables retain the signed general evaluator below.
    if (rule_count() >= 8 && p.cells.size() == 1) {
      const auto& cell = *p.cells.begin();
      if (cell.second.x != cell.second.z)
        return pullback_generator(cell.first, cell.second.x,
                                  p.phase == 0 ? 1 : -1, address_bytes, limits);
    }

    std::size_t peak = p.cells.size();
    const auto sequence = rules();
    for (auto it = sequence.rbegin(); !p.cells.empty() && it != sequence.rend(); ++it) {
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
    out.stats = {rule_count(), p.cells.size(), peak, address_bytes};
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

  RuleRun run_;
  mutable Stats stats_;

  static void validate_rule(Rule r) {
    if (r != Rule::HAll && r != Rule::CZNeighbors)
      throw std::invalid_argument("unknown Ether rule");
  }

  void push_rule(Rule r) {
    append_alternating(r, 1);
  }

  // F_0(A)=0, F_1(A)=I, F_(k+1)(A)=A F_k(A)+F_(k-1)(A), over GF(2),
  // where A is adjacency of q0--q1--... . On a basis site q,
  // F_(r+1)(A)e_q has support |q-r|, |q-r|+2, ..., q+r. The reflection
  // at q0 cancels the missing negative half-line exactly. Each X/Z image
  // has X and Z on opposite parities, hence no Y or intrinsic minus sign.
  struct SupportRun { std::string next; std::size_t remaining = 0; };

  static std::string add_offset(std::string address, std::size_t offset) {
    for (std::size_t i = address.size(); i-- > 0 && offset;) {
      const unsigned digit = static_cast<unsigned>(address[i] - '0') + offset % 10;
      offset /= 10;
      if (digit >= 10) ++offset;
      address[i] = static_cast<char>('0' + digit % 10);
    }
    if (offset) address.insert(0, std::to_string(offset));
    return address;
  }

  static std::string subtract_offset(std::string address, std::size_t offset) {
    unsigned borrow = 0;
    for (std::size_t i = address.size(); i-- > 0 && (offset || borrow);) {
      int digit = (address[i] - '0') - static_cast<int>(offset % 10) - borrow;
      offset /= 10;
      borrow = digit < 0;
      if (borrow) digit += 10;
      address[i] = static_cast<char>('0' + digit);
    }
    if (offset || borrow) throw std::logic_error("negative Ether offset");
    const auto first = address.find_first_not_of('0');
    return first == std::string::npos ? std::string("0") : address.substr(first);
  }

  static SupportRun generator_support(const std::string& address, std::size_t order,
                                      const QueryLimits& limits) {
    if (order == 0) return {};
    const auto radius = order - 1;
    check_address(add_offset(address, radius), limits);
    const auto r = std::to_string(radius);
    if (!AddressLess{}(address, r))
      return {subtract_offset(address, radius), radius + 1};
    // address < radius, so conversion is bounded by size_t, irrespective of
    // the length allowed for addresses in the rest of the register.
    std::size_t q = 0;
    for (const auto c : address) q = q * 10 + static_cast<unsigned>(c - '0');
    return {std::to_string(radius - q), q + 1};
  }

  static void advance_two(std::string& address) {
    unsigned carry = 2;
    for (std::size_t i = address.size(); i-- > 0 && carry;) {
      const unsigned digit = static_cast<unsigned>(address[i] - '0') + carry;
      address[i] = static_cast<char>('0' + digit % 10);
      carry = digit / 10;
    }
    if (carry) address.insert(address.begin(), '1');
  }

  Observable pullback_generator(const std::string& address, bool x, int sign,
                                std::size_t address_bytes, const QueryLimits& limits) const {
    auto depth = rule_count();
    if (rules().back() == Rule::HAll) { x = !x; --depth; }
    const auto pairs = depth / 2;
    std::size_t x_order, z_order;
    if (depth % 2) {
      x_order = x ? pairs + 1 : pairs;
      z_order = x_order + 1;
    } else if (x) {
      x_order = pairs + 1; z_order = pairs;
    } else {
      x_order = pairs; z_order = pairs ? pairs - 1 : 1;
    }
    auto xs = generator_support(address, x_order, limits);
    auto zs = generator_support(address, z_order, limits);
    if (xs.remaining > limits.max_sites || zs.remaining > limits.max_sites - xs.remaining)
      throw std::length_error("Ether query cone site budget exceeded");
    const auto count = xs.remaining + zs.remaining;
    Observable out;
    out.sign = sign;
    out.stats = {rule_count(), count, count, address_bytes};
    out.terms.reserve(count);
    while (xs.remaining || zs.remaining) {
      const bool take_x = !zs.remaining ||
                         (xs.remaining && AddressLess{}(xs.next, zs.next));
      auto& run = take_x ? xs : zs;
      out.terms.push_back({run.next, take_x ? 'X' : 'Z'});
      if (--run.remaining) advance_two(run.next);
    }
    return out;
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
