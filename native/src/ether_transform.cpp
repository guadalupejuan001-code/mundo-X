#include "mundo_x/experimental/ether_transform.hpp"

#include <algorithm>
#include <map>
#include <tuple>

namespace mundo_x::ether_next {
namespace {
constexpr std::uint16_t zero = 16;
bool commutative(Op op) {
  return op == Op::Xor || op == Op::And || op == Op::Or ||
         (op >= Op::Add8 && op <= Op::Add64);
}
struct Builder {
  Transform t;
  std::map<std::tuple<Op, std::uint16_t, std::uint16_t>, std::uint16_t> expressions;
  Builder() {
    for (std::uint16_t i = 0; i < 16; ++i) {
      t.nodes.push_back({Op::Input, i, 0});
      t.outputs[i] = i;
    }
    t.nodes.push_back({Op::Zero, 0, 0});
  }
  std::uint16_t make(Op op, std::uint16_t a, std::uint16_t b) {
    if (op == Op::Xor) {
      if (a == b) return zero;
      if (a == zero) return b;
      if (b == zero) return a;
      for (unsigned side = 0; side < 2; ++side) {
        const auto& n = t.nodes[side ? b : a];
        const auto other = side ? a : b;
        if (n.op == Op::Xor) {
          if (n.a == other) return n.b;
          if (n.b == other) return n.a;
        }
      }
    } else if (op == Op::And || op == Op::Or) {
      if (a == b) return a;
      if (a == zero) return op == Op::And ? zero : b;
      if (b == zero) return op == Op::And ? zero : a;
    } else if (op == Op::AndNot) {
      // x86 PANDN is (~destination) & source.
      if (a == b || b == zero) return zero;
      if (a == zero) return b;
    } else if (op >= Op::Add8 && op <= Op::Add64) {
      if (a == zero) return b;
      if (b == zero) return a;
      const auto sub = static_cast<Op>(static_cast<unsigned>(op) + 4);
      if (t.nodes[a].op == sub && t.nodes[a].b == b) return t.nodes[a].a;
      if (t.nodes[b].op == sub && t.nodes[b].b == a) return t.nodes[b].a;
    } else if (op >= Op::Sub8 && op <= Op::Sub64) {
      if (a == b) return zero;
      if (b == zero) return a;
      const auto add = static_cast<Op>(static_cast<unsigned>(op) - 4);
      if (t.nodes[a].op == add) {
        if (t.nodes[a].a == b) return t.nodes[a].b;
        if (t.nodes[a].b == b) return t.nodes[a].a;
      }
    }
    if (commutative(op) && b < a) std::swap(a, b);
    const auto key = std::make_tuple(op, a, b);
    const auto found = expressions.find(key);
    if (found != expressions.end()) return found->second;
    const auto id = static_cast<std::uint16_t>(t.nodes.size());
    t.nodes.push_back({op, a, b});
    expressions.emplace(key, id);
    return id;
  }
  std::shared_ptr<const Transform> finish() {
    std::vector<bool> live(t.nodes.size(), false);
    for (const auto root : t.outputs) live[root] = true;
    for (std::size_t i = t.nodes.size(); i-- > 17;) {
      if (live[i]) { live[t.nodes[i].a] = true; live[t.nodes[i].b] = true; }
    }
    auto result = std::make_shared<Transform>();
    result->source_operations = t.source_operations;
    result->nodes.assign(t.nodes.begin(), t.nodes.begin() + 17);
    std::vector<std::uint16_t> remap(t.nodes.size());
    for (std::uint16_t i = 0; i <= zero; ++i) remap[i] = i;
    if (live[zero]) result->schedule.push_back(zero);
    for (std::size_t i = 17; i < t.nodes.size(); ++i) {
      if (!live[i]) continue;
      remap[i] = static_cast<std::uint16_t>(result->nodes.size());
      const auto& n = t.nodes[i];
      result->nodes.push_back({n.op, remap[n.a], remap[n.b]});
      result->schedule.push_back(remap[i]);
    }
    for (std::size_t i = 0; i < 16; ++i) result->outputs[i] = remap[t.outputs[i]];
    return result;
  }
};
} // namespace

std::shared_ptr<const Transform> reduce_x86(const std::vector<std::uint8_t>& code) {
  // Deliberately bounded compiler work, no guest CPU interpretation. Every
  // instruction must be a supported pure register operation in 64-bit mode.
  if (code.empty() || code.size() > 4096) return {};
  Builder builder;
  std::size_t cursor = 0;
  while (cursor < code.size()) {
    const auto prefix = code[cursor++];
    if (prefix != 0x66 && prefix != 0xf3) return {};
    std::uint8_t rex = 0;
    if (cursor < code.size() && (code[cursor] & 0xf0) == 0x40) rex = code[cursor++];
    if (cursor + 3 > code.size() || code[cursor++] != 0x0f) return {};
    const auto opcode = code[cursor++], modrm = code[cursor++];
    // Memory, segment/address-size/LOCK/duplicate prefixes and FP all fall back.
    if ((modrm & 0xc0) != 0xc0) return {};
    const auto reg = static_cast<unsigned>(((modrm >> 3) & 7) | ((rex & 4) ? 8 : 0));
    const auto rm = static_cast<unsigned>((modrm & 7) | ((rex & 1) ? 8 : 0));
    ++builder.t.source_operations;
    if (opcode == 0x6f || opcode == 0x7f) {
      const auto dst = opcode == 0x6f ? reg : rm;
      const auto src = opcode == 0x6f ? rm : reg;
      builder.t.outputs[dst] = builder.t.outputs[src];
      continue;
    }
    if (prefix != 0x66) return {};
    Op op;
    switch (opcode) {
      case 0xef: op = Op::Xor; break;
      case 0xdb: op = Op::And; break;
      case 0xeb: op = Op::Or; break;
      case 0xdf: op = Op::AndNot; break;
      case 0xfc: op = Op::Add8; break;
      case 0xfd: op = Op::Add16; break;
      case 0xfe: op = Op::Add32; break;
      case 0xd4: op = Op::Add64; break;
      case 0xf8: op = Op::Sub8; break;
      case 0xf9: op = Op::Sub16; break;
      case 0xfa: op = Op::Sub32; break;
      case 0xfb: op = Op::Sub64; break;
      default: return {};
    }
    builder.t.outputs[reg] = builder.make(op, builder.t.outputs[reg], builder.t.outputs[rm]);
  }
  return builder.finish();
}
} // namespace mundo_x::ether_next
