#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iterator>
#include <limits>
#include <map>
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
// query is propagated backwards through those rules. Deep words compose
// signed interval images before expanding the required finite output;
// short words retain direct propagation. No total qubit array is stored.
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
      // Values are generated, not references into contiguous rule storage.
      using iterator_category = std::input_iterator_tag;
      using value_type = Rule;
      using difference_type = std::ptrdiff_t;
      using pointer = void;
      using reference = Rule;
      iterator() = default;
      Rule operator*() const { return at(first_, reverse_ ? index_ - 1 : index_); }
      iterator& operator++() { reverse_ ? --index_ : ++index_; return *this; }
      iterator operator++(int) { auto old = *this; ++*this; return old; }
      iterator& operator--() { reverse_ ? ++index_ : --index_; return *this; }
      iterator operator--(int) { auto old = *this; --*this; return old; }
      bool operator==(const iterator& other) const {
        return first_ == other.first_ && index_ == other.index_ && reverse_ == other.reverse_;
      }
      bool operator!=(const iterator& other) const { return !(*this == other); }
     private:
      friend class RuleView;
      iterator(Rule first, std::size_t index, bool reverse = false)
          : first_(first), index_(index), reverse_(reverse) {}
      Rule first_ = Rule::HAll;
      std::size_t index_ = 0;
      bool reverse_ = false;
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
    iterator rbegin() const { return iterator(run_.first, size(), true); }
    iterator rend() const { return iterator(run_.first, 0, true); }
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

  std::size_t rule_count() const noexcept { return rule_count_; }
  RuleRun rule_run() const noexcept {
    return {rule_count_ ? RuleView::at(last_rule_, rule_count_ - 1) : Rule::HAll, rule_count_};
  }
  RuleView rules() const noexcept { return RuleView(rule_run()); }
  Stats stats() const noexcept { return stats_; }

  void restore_rule_run(Rule first, std::size_t count,
                        std::size_t max_rules = std::numeric_limits<std::size_t>::max()) {
    validate_rule(first);
    if (count > max_rules) throw std::length_error("Ether rule budget exceeded");
    rule_count_ = count;
    last_rule_ = count ? RuleView::at(first, count - 1) : Rule::HAll;
    stats_ = Stats{};
    stats_.rules = count;
  }

  // Compose another alternating word, including cancellation across the
  // boundary. Check arithmetic and the resulting depth before changing state.
  void append_alternating(Rule first, std::size_t count,
                          std::size_t max_rules = std::numeric_limits<std::size_t>::max()) {
    validate_rule(first);
    auto candidate = rule_run();
    if (candidate.count && count && last_rule_ == first) {
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
    rule_count_ = candidate.count;
    last_rule_ = candidate.count ? RuleView::at(candidate.first, candidate.count - 1) : Rule::HAll;
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
    std::size_t address_bytes = 0, active_sites = 0;
    for (const auto& term : terms) {
      check_address(term.address, limits);
      const auto address = canonical(term.address);
      // Retain identity entries until validation finishes, so they also catch
      // aliases without a second tree of every address. Erase them below.
      auto inserted = p.cells.emplace(address, Bits{});
      if (!inserted.second)
        throw std::invalid_argument("duplicate canonical address in Pauli query");
      if (address.size() > std::numeric_limits<std::size_t>::max() - address_bytes)
        throw std::length_error("Pauli address text overflows");
      address_bytes += address.size();
      auto& bits = inserted.first->second;
      switch (term.axis) {
        case 'I': break;
        case 'X': bits = {true, false}; break;
        case 'Z': bits = {false, true}; break;
        case 'Y':
          p.phase = (p.phase + 1) & 3;
          bits = {true, true};
          break;
        default: throw std::invalid_argument("Pauli axis must be I, X, Y or Z");
      }
      if (bits.x || bits.z) {
        if (active_sites == limits.max_sites)
          throw std::length_error("Ether query cone site budget exceeded");
        ++active_sites;
      }
    }
    for (auto it = p.cells.begin(); it != p.cells.end();)
      if (!it->second.x && !it->second.z) it = p.cells.erase(it); else ++it;

    // A single X or Z has an exact closed pullback on the half-line. Avoid
    // replaying a deep alternating word (and rebuilding its cone each layer).
    // Mixed/Y observables compose symbolic intervals below.
    if (rule_count() >= 8 && p.cells.size() == 1) {
      const auto& cell = *p.cells.begin();
      if (cell.second.x != cell.second.z)
        return pullback_generator(cell.first, cell.second.x,
                                  p.phase == 0 ? 1 : -1, address_bytes, limits);
    }

    std::size_t peak = p.cells.size();
    if (!p.cells.empty() && rule_count() && !pullback_intervals(p, limits, peak)) {
      auto left = rule_count();
      if (last_rule_ == Rule::HAll) { conjugate_h_all(p); --left; }
      // The remaining word starts with CZ in pullback order. H cannot grow
      // support; CZ already records every transient peak, including sites
      // which disappear again within that same layer.
      while (left >= 2) {
        conjugate_cz_neighbors(p, limits, peak);
        conjugate_h_all(p);
        left -= 2;
      }
      if (left) conjugate_cz_neighbors(p, limits, peak);
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

  // Keep the tail internally so the frequent one-rule update compares a
  // stored enum directly. The public first/count descriptor is derived in O(1).
  std::size_t rule_count_ = 0;
  Rule last_rule_ = Rule::HAll;
  mutable Stats stats_;

  static void validate_rule(Rule r) {
    if (r != Rule::HAll && r != Rule::CZNeighbors)
      throw std::invalid_argument("unknown Ether rule");
  }

  void push_rule(Rule r) {
    // Fixed one-letter entry points need neither bulk composition nor rule
    // validation: their callers supply one of the two known enum constants.
    if (rule_count_ && last_rule_ == r) {
      last_rule_ = --rule_count_ ? RuleView::at(last_rule_, 1) : Rule::HAll;
    } else {
      if (rule_count_ == std::numeric_limits<std::size_t>::max())
        throw std::overflow_error("Ether rule count overflow");
      last_rule_ = r;
      ++rule_count_;
    }
    stats_.rules = rule_count();
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

  std::pair<SupportRun, SupportRun> generator_runs(const std::string& address, bool x,
                                                  const QueryLimits& limits,
                                                  bool explicit_support = true) const {
    auto depth = rule_count();
    if (last_rule_ == Rule::HAll) { x = !x; --depth; }
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
    if (explicit_support &&
        (xs.remaining > limits.max_sites || zs.remaining > limits.max_sites - xs.remaining))
      throw std::length_error("Ether query cone site budget exceeded");
    return {std::move(xs), std::move(zs)};
  }

  Observable pullback_generator(const std::string& address, bool x, int sign,
                                std::size_t address_bytes, const QueryLimits& limits) const {
    auto runs = generator_runs(address, x, limits);
    auto& xs = runs.first;
    auto& zs = runs.second;
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

  // Half-open intervals on either the even or odd sublattice. Their interior
  // is never enumerated until all generator images have cancelled exactly.
  struct Interval { std::string begin, end; };
  static Interval interval(SupportRun run) {
    if (!run.remaining) return {};
    // Two additions avoid overflowing size_t at 2 * remaining. The exclusive
    // endpoint may need one more decimal digit than every physical site.
    auto end = add_offset(add_offset(run.next, run.remaining - 1), run.remaining - 1);
    return {std::move(run.next), add_offset(std::move(end), 2)};
  }
  static unsigned sublattice(const std::string& address) {
    return static_cast<unsigned>(address.back() - '0') & 1U;
  }
  static unsigned half_parity(const std::string& address) {
    unsigned last = static_cast<unsigned>(address.back() - '0');
    if (address.size() > 1) last += 10U * static_cast<unsigned>(address[address.size() - 2] - '0');
    return (last >> 1U) & 1U;
  }
  struct IntervalIndex {
    std::vector<std::string> points;
    std::vector<unsigned char> events, prefix;
    void prepare() {
      std::sort(points.begin(), points.end(), AddressLess{});
      points.erase(std::unique(points.begin(), points.end()), points.end());
      events.assign(points.size(), 0);
      prefix.assign(points.size() + 1, 0);
    }
    std::size_t find(const std::string& address) const {
      return static_cast<std::size_t>(std::lower_bound(points.begin(), points.end(), address,
                                                       AddressLess{}) - points.begin());
    }
    void toggle_z(std::size_t at) {
      const unsigned delta = 1U | (half_parity(points[at]) << 1U);
      // Fenwick XOR tree: prefix count parity and endpoint-coordinate parity.
      for (auto i = at + 1; i < prefix.size(); i += i & (~i + 1)) prefix[i] ^= delta;
    }
    unsigned integral(std::size_t at) const {
      unsigned value = 0;
      for (auto i = at; i; i &= i - 1) value ^= prefix[i];
      // XOR_e<t (t-e mod 2), with t=(address-parity)/2. This is the parity
      // of covered sites before t; no integer address conversion is needed.
      return ((value & 1U) & half_parity(points[at])) ^ (value >> 1U);
    }
  };

  bool pullback_intervals(Pauli& source, const QueryLimits& limits, std::size_t& peak) const {
    // Short words need less scratch with direct propagation. Reserve the
    // interval index for depths where avoiding layer replay pays for it.
    if (rule_count() < 32) return false;
    std::vector<std::pair<Interval, Interval>> images;
    std::array<IntervalIndex, 2> index;
    // Symbolic endpoints are not materialized Pauli sites. Validate digits and
    // support on the combined output, allowing cancelled images to stay implicit.
    const QueryLimits symbolic;
    for (const bool x : {true, false}) {
      for (const auto& item : source.cells) {
        if (!(x ? item.second.x : item.second.z)) continue;
        auto runs = generator_runs(item.first, x, symbolic, false);
        images.emplace_back(interval(std::move(runs.first)), interval(std::move(runs.second)));
        for (const auto* run : {&images.back().first, &images.back().second}) {
          if (run->begin.empty()) continue;
          auto& points = index[sublattice(run->begin)].points;
          points.push_back(run->begin); points.push_back(run->end);
        }
      }
    }
    for (auto& table : index) table.prepare();
    Pauli result;
    result.phase = source.phase;
    // Canonical input is i^phase (product X) (product Z). Preserve the order
    // and add 2 * (current Z dot incoming X) mod 4 at each multiplication.
    for (const auto& image : images) {
      for (const bool x : {true, false}) {
        const auto& run = x ? image.first : image.second;
        if (run.begin.empty()) continue;
        auto& table = index[sublattice(run.begin)];
        const auto a = table.find(run.begin), b = table.find(run.end);
        if (x) result.phase = (result.phase + 2 * (table.integral(a) ^ table.integral(b))) & 3;
        else { table.toggle_z(a); table.toggle_z(b); }
        table.events[a] ^= x ? 1U : 2U;
        table.events[b] ^= x ? 1U : 2U;
      }
    }
    for (const auto& table : index) {
      unsigned bits = 0;
      for (std::size_t i = 0; i + 1 < table.points.size(); ++i) {
        bits ^= table.events[i];
        if (!bits) continue;
        const auto left = limits.max_sites - result.cells.size();
        const auto bound = add_offset(add_offset(table.points[i], left), left);
        if (AddressLess{}(bound, table.points[i + 1]))
          throw std::length_error("Ether query cone site budget exceeded");
        for (auto address = table.points[i]; address != table.points[i + 1]; advance_two(address))
          set_bits(result, address, bits & 1U, bits & 2U, limits);
      }
    }
    peak = std::max(peak, result.cells.size());
    source = std::move(result);
    return true;
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
