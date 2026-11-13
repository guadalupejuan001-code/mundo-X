#include "mundo_x/experimental/ether_transform.hpp"

#include <algorithm>
#include <cstring>
#include <limits>
#include <new>

#if defined(__aarch64__) && defined(__linux__) && \
    defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define MUNDO_X_ETHER_NEXT_NATIVE_AARCH64 1
#include <asm/hwcap.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace mundo_x::ether_next {
namespace {
constexpr unsigned no_register = 32;
constexpr std::array<unsigned, 24> caller_saved{
    0, 1, 2, 3, 4, 5, 6, 7, 16, 17, 18, 19, 20, 21, 22, 23,
    24, 25, 26, 27, 28, 29, 30, 31};

bool binary(Op op) {
  return op >= Op::Xor && op <= Op::Sub64;
}

// AArch64 Advanced SIMD encodings; every operation has Q=1 (128 bits).
std::uint32_t operation(Op op, unsigned dest, unsigned a, unsigned b) {
  std::uint32_t base = 0;
  switch (op) {
    case Op::Xor: base = 0x6e201c00; break; // EOR Vd.16B,Vn.16B,Vm.16B
    case Op::And: base = 0x4e201c00; break;
    case Op::Or: base = 0x4ea01c00; break;
    case Op::AndNot:
      base = 0x4e601c00; // x86 PANDN = (~a)&b; BIC Vd,b,a
      std::swap(a, b);
      break;
    case Op::Add8: base = 0x4e208400; break;
    case Op::Add16: base = 0x4e608400; break;
    case Op::Add32: base = 0x4ea08400; break;
    case Op::Add64: base = 0x4ee08400; break;
    case Op::Sub8: base = 0x6e208400; break;
    case Op::Sub16: base = 0x6e608400; break;
    case Op::Sub32: base = 0x6ea08400; break;
    case Op::Sub64: base = 0x6ee08400; break;
    default: return 0;
  }
  return base | (b << 16U) | (a << 5U) | dest;
}

struct Emission {
  std::vector<std::uint32_t> words;
  std::size_t vectors = 0, loads = 0, stores = 0;
};

// This bounded linear allocator keeps only live SSA values. Pressure beyond
// the 24 caller-saved SIMD registers declines the block without emitting a
// partial executable. v8..v15, SP, LR and all other callee-saved state are untouched.
bool emit(const Transform& t, Emission& out) {
  if (t.nodes.size() < 17 || t.nodes.size() > 65535 || t.schedule.size() > 4096)
    return false;
  for (unsigned i = 0; i < 16; ++i)
    if (t.nodes[i].op != Op::Input || t.nodes[i].a != i) return false;
  if (t.nodes[16].op != Op::Zero) return false;
  for (std::size_t id = 17; id < t.nodes.size(); ++id) {
    const auto n = t.nodes[id];
    if (!binary(n.op) || n.a >= id || n.b >= id) return false;
  }

  std::vector<std::size_t> uses(t.nodes.size(), 0);
  std::vector<bool> scheduled(t.nodes.size(), false);
  unsigned previous = 15;
  for (const auto id : t.schedule) {
    if (id < 16 || id >= t.nodes.size() || id <= previous) return false;
    previous = id;
    const auto n = t.nodes[id];
    if (n.op != Op::Zero && !binary(n.op)) return false;
    if (binary(n.op)) {
      if (n.a >= id || n.b >= id) return false;
      if ((n.a >= 16 && !scheduled[n.a]) || (n.b >= 16 && !scheduled[n.b]))
        return false;
      ++uses[n.a];
      ++uses[n.b];
    }
    scheduled[id] = true;
  }
  for (unsigned i = 0; i < 16; ++i) {
    const auto id = t.outputs[i];
    if (id >= t.nodes.size() || (id >= 16 && !scheduled[id])) return false;
    if (id != i) ++uses[id];
  }

  std::vector<unsigned> physical(t.nodes.size(), no_register);
  std::array<bool, 32> occupied{};
  auto allocate = [&]() {
    for (const auto reg : caller_saved)
      if (!occupied[reg]) { occupied[reg] = true; return reg; }
    return no_register;
  };
  auto input = [&](unsigned id) {
    if (physical[id] != no_register) return true;
    if (id >= 16) return false;
    const auto reg = allocate();
    if (reg == no_register) return false;
    physical[id] = reg;
    out.words.push_back(0x3dc00000U | (id << 10U) | reg); // LDR Qt,[x0,#id*16]
    ++out.loads;
    return true;
  };
  auto consume = [&](unsigned id, unsigned result_register) {
    if (--uses[id] == 0) {
      if (physical[id] != result_register) occupied[physical[id]] = false;
      physical[id] = no_register;
    }
  };

  out.words.push_back(0xd503245fU); // BTI c; a harmless hint on pre-BTI ARMv8.
  for (const auto id : t.schedule) {
    const auto n = t.nodes[id];
    unsigned a = no_register, b = no_register, dest = no_register;
    if (binary(n.op)) {
      if (!input(n.a) || !input(n.b)) return false;
      a = physical[n.a];
      b = physical[n.b];
      // All supported vector operations permit aliasing Rd with either source.
      if (uses[n.a] == (n.a == n.b ? 2U : 1U)) dest = a;
      else if (uses[n.b] == 1) dest = b;
    }
    if (dest == no_register) dest = allocate();
    if (dest == no_register) return false;
    if (n.op == Op::Zero) out.words.push_back(0x4f00e400U | dest); // MOVI Vd.16B,#0
    else {
      out.words.push_back(operation(n.op, dest, a, b));
      consume(n.a, dest);
      consume(n.b, dest);
    }
    occupied[dest] = true;
    physical[id] = dest;
    ++out.vectors;
    // Defensive support for a caller supplying extra scheduled dead nodes.
    if (uses[id] == 0) {
      occupied[dest] = false;
      physical[id] = no_register;
    }
  }
  // Load every directly forwarded input before any store can overwrite it.
  for (unsigned i = 0; i < 16; ++i)
    if (t.outputs[i] != i && !input(t.outputs[i])) return false;
  for (unsigned i = 0; i < 16; ++i) {
    const auto id = t.outputs[i];
    if (id == i) continue;
    out.words.push_back(0x3d800000U | (i << 10U) | physical[id]); // STR Qt,[x0,#i*16]
    ++out.stores;
  }
  out.words.push_back(0xd65f03c0U); // RET
  return true;
}

bool equivalent(const Transform& a, const Transform& b) {
  if (a.outputs != b.outputs || a.schedule != b.schedule || a.nodes.size() != b.nodes.size())
    return false;
  for (std::size_t i = 0; i < a.nodes.size(); ++i)
    if (a.nodes[i].op != b.nodes[i].op || a.nodes[i].a != b.nodes[i].a ||
        a.nodes[i].b != b.nodes[i].b) return false;
  return true;
}
} // namespace

NativeCode::NativeCode(const Transform& transform) {
  try {
    Emission emitted;
    if (!emit(transform, emitted)) return;
    words_ = std::move(emitted.words);
    vector_instructions_ = emitted.vectors;
    loads_ = emitted.loads;
    stores_ = emitted.stores;
#if defined(MUNDO_X_ETHER_NEXT_NATIVE_AARCH64)
    if ((getauxval(AT_HWCAP) & HWCAP_ASIMD) == 0) return;
    const auto page = sysconf(_SC_PAGESIZE);
    if (page <= 0) return;
    const auto bytes = words_.size() * sizeof(std::uint32_t);
    const auto page_bytes = static_cast<std::size_t>(page);
    memory_bytes_ = ((bytes + page_bytes - 1) / page_bytes) * page_bytes;
    void* candidate = mmap(nullptr, memory_bytes_, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (candidate == MAP_FAILED) { memory_bytes_ = 0; return; }
    std::memcpy(candidate, words_.data(), bytes);
    if (mprotect(candidate, memory_bytes_, PROT_READ | PROT_EXEC) != 0) {
      munmap(candidate, memory_bytes_);
      memory_bytes_ = 0;
      return;
    }
    __builtin___clear_cache(static_cast<char*>(candidate), static_cast<char*>(candidate) + bytes);
    memory_ = candidate; // Publish only a complete RX mapping.
#endif
  } catch (const std::bad_alloc&) {
    // No executable has been published and no caller state has been changed.
    words_.clear();
    vector_instructions_ = loads_ = stores_ = 0;
  }
}

NativeCode::~NativeCode() {
#if defined(MUNDO_X_ETHER_NEXT_NATIVE_AARCH64)
  if (memory_) munmap(memory_, memory_bytes_);
#endif
}

bool NativeCode::available() const noexcept { return memory_ != nullptr; }

bool NativeCode::execute(Registers& values) const {
#if defined(MUNDO_X_ETHER_NEXT_NATIVE_AARCH64)
  if (!available()) return false;
  using Function = void (*)(Registers*);
  reinterpret_cast<Function>(memory_)(&values);
  return true;
#else
  (void)values;
  return false;
#endif
}

bool Cache::execute(const std::vector<std::uint8_t>& code, Registers& values,
                    const Guard& guard) {
  if (!guard.permits_pure_block() || code.empty() || code.size() > 4096) {
    ++stats_.fallbacks;
    return false;
  }
  for (const auto& entry : entries_) {
    if (entry.bytes == code) {
      ++stats_.hits;
      if (entry.native->execute(values)) return true;
      ++stats_.fallbacks;
      return false;
    }
  }
  ++stats_.misses;
  try {
    const auto transform = reduce_x86(code);
    if (!transform) { ++stats_.fallbacks; return false; }
    std::shared_ptr<const NativeCode> native;
    for (const auto& entry : entries_)
      if (equivalent(*transform, *entry.transform)) { native = entry.native; break; }
    if (!native) native = std::make_shared<NativeCode>(*transform);
    if (!native->available()) { ++stats_.fallbacks; return false; }
    if (capacity_) {
      Entry entry{code, transform, native};
      if (entries_.size() == capacity_) entries_.erase(entries_.begin());
      entries_.push_back(std::move(entry));
    }
    return native->execute(values);
  } catch (const std::bad_alloc&) {
    ++stats_.fallbacks;
    return false;
  }
}
} // namespace mundo_x::ether_next
