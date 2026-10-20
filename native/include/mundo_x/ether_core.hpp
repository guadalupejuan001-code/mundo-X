#pragma once

#include <algorithm>
#include <cstdint>
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

  void h_all() { push_rule(Rule::HAll); }
  void cz_neighbors() { push_rule(Rule::CZNeighbors); }

  std::size_t rule_count() const noexcept { return rules_.size(); }
  const std::vector<Rule>& rules() const noexcept { return rules_; }
  Stats stats() const noexcept { return stats_; }

  double expectation(const std::vector<Term>& terms, int sign = 1) const {
    if (sign != 1 && sign != -1)
      throw std::invalid_argument("Pauli sign must be +1 or -1");

    Pauli p;
    p.phase = sign == 1 ? 0 : 2;
    std::set<std::string, AddressLess> seen;
    std::size_t address_bytes = 0;
    for (const auto& term : terms) {
      const auto address = canonical(term.address);
      if (!seen.insert(address).second)
        throw std::invalid_argument("duplicate canonical address in Pauli query");
      address_bytes += address.size();
      switch (term.axis) {
        case 'I': break;
        case 'X': set_bits(p, address, true, false); break;
        case 'Z': set_bits(p, address, false, true); break;
        case 'Y':
          p.phase = (p.phase + 1) & 3;
          set_bits(p, address, true, true);
          break;
        default: throw std::invalid_argument("Pauli axis must be I, X, Y or Z");
      }
    }

    std::size_t peak = p.cells.size();
    for (auto it = rules_.rbegin(); it != rules_.rend(); ++it) {
      if (*it == Rule::HAll) conjugate_h_all(p);
      else conjugate_cz_neighbors(p);
      peak = std::max(peak, p.cells.size());
    }

    for (const auto& item : p.cells)
      if (item.second.x) {
        update_stats(p.cells.size(), peak, address_bytes);
        return 0.0;
      }
    if (p.phase & 1)
      throw std::logic_error("non-Hermitian phase after Clifford propagation");
    update_stats(p.cells.size(), peak, address_bytes);
    return p.phase == 0 ? 1.0 : -1.0;
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

  static void set_bits(Pauli& p, const std::string& address, bool x, bool z) {
    if (!x && !z) { p.cells.erase(address); return; }
    p.cells[address] = Bits{x,z};
  }

  static void toggle_z(Pauli& p, const std::string& address) {
    auto bits = get_bits(p,address);
    bits.z = !bits.z;
    set_bits(p,address,bits.x,bits.z);
  }

  static void conjugate_h_all(Pauli& p) {
    for (auto& item : p.cells) {
      auto& bits = item.second;
      if (bits.x && bits.z) p.phase = (p.phase + 2) & 3;
      std::swap(bits.x,bits.z);
    }
  }

  static void conjugate_cz_neighbors(Pauli& p) {
    std::set<std::pair<std::string,std::string>> edges;
    for (const auto& item : p.cells) if (item.second.x) {
      const auto& q = item.first;
      if (q != "0") edges.emplace(predecessor(q), q);
      edges.emplace(q, successor(q));
    }
    for (const auto& edge : edges) {
      const bool xa = get_bits(p,edge.first).x;
      const bool xb = get_bits(p,edge.second).x;
      if (xa && xb) p.phase = (p.phase + 2) & 3;
      if (xb) toggle_z(p,edge.first);
      if (xa) toggle_z(p,edge.second);
    }
  }

  void update_stats(std::size_t sites, std::size_t peak,
                    std::size_t address_bytes) const {
    stats_.rules = rules_.size();
    stats_.last_query_sites = sites;
    stats_.last_query_peak_sites = peak;
    stats_.last_query_address_bytes = address_bytes;
  }
};

} // namespace mundo_x
