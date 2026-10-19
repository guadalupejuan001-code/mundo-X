#include "pauli_observer.hpp"
#include "vendor/mundo_x/dense_core.hpp"
#include <complex>
#include <cstring>
#include <memory>

namespace {
thread_local std::string last_error;
struct Handle {
  explicit Handle(int kind, std::size_t n) : n(n) {
    if (kind == 0) compact = std::make_unique<mundo_x::CompactCore>(n);
    else if (kind == 1) dense = std::make_unique<mundo_x::DenseCore64>(n);
    else throw std::invalid_argument("Unknown backend");
  }
  std::size_t n;
  std::uint64_t revision = 0, rebuilds = 0;
  std::unique_ptr<mundo_x::CompactCore> compact;
  std::unique_ptr<mundo_x::DenseCore64> dense;
  std::unique_ptr<mundo_x_bridge::PauliObserver> observer;
};

Handle& handle(void* p) {
  if (!p) throw std::invalid_argument("Null handle");
  return *static_cast<Handle*>(p);
}

template<class Core>
void gate(Core& core, std::uint64_t op, std::size_t a, std::size_t b) {
  switch (op) {
    case 0: core.hadamard(a); break;
    case 1: core.phase_s(a); break;
    case 2: core.pauli_x(a); break;
    case 3: core.pauli_z(a); break;
    case 4: core.cnot(a,b); break;
    case 5: core.controlled_z(a,b); break;
    // Y = i X Z. The omitted global phase has no observable effect.
    case 6: core.pauli_z(a); core.pauli_x(a); break;
    default: throw std::invalid_argument("Unsupported gate; Clifford bridge only");
  }
}

double dense_expectation(const mundo_x::DenseCore64& core,
                         const mundo_x_bridge::Pauli& p) {
  const auto& state = core.state();
  const auto xm = p.x[0], zm = p.z[0];
  const std::complex<double> phases[] = {{1,0},{0,1},{-1,0},{0,-1}};
  const auto factor = phases[p.phase];
  std::complex<double> sum{0,0};
  for (std::size_t i = 0; i < state.size(); ++i) {
    const double sign = __builtin_parityll(i & zm) ? -1.0 : 1.0;
    sum += std::conj(state[i ^ xm]) * (factor * sign) * state[i];
  }
  if (std::abs(sum.imag()) > 1e-8) throw std::logic_error("Imaginary expectation");
  return sum.real();
}
} // namespace

extern "C" {
const char* mx_last_error() { return last_error.c_str(); }
void* mx_create(int kind, std::size_t n) {
  try { return new Handle(kind, n); }
  catch (const std::exception& e) { last_error = e.what(); return nullptr; }
}
void mx_destroy(void* p) { delete static_cast<Handle*>(p); }

// Batch is validated in full before any state mutation.
int mx_apply(void* p, const std::uint64_t* gates, std::size_t count) {
  try {
    auto& h = handle(p);
    if (count && !gates) throw std::invalid_argument("Null gate batch");
    if (count > std::numeric_limits<std::size_t>::max()/3)
      throw std::invalid_argument("Gate batch dimensions overflow");
    for (std::size_t j = 0; j < count; ++j) {
      const auto op=gates[3*j], a=gates[3*j+1], b=gates[3*j+2];
      if (op > 6 || a >= h.n || ((op == 4 || op == 5) && (b >= h.n || a == b)))
        throw std::invalid_argument("Invalid/unsupported gate in batch");
    }
    if (count) h.observer.reset();
    for (std::size_t j = 0; j < count; ++j) {
      const auto op=gates[3*j], a=gates[3*j+1], b=gates[3*j+2];
      if (h.compact) gate(*h.compact, op, a, b);
      else gate(*h.dense, op, a, b);
    }
    if (count) ++h.revision;
    return 0;
  } catch (const std::exception& e) { last_error=e.what(); return -1; }
}

int mx_query(void* p, const char* const* strings, std::size_t count, double* out) {
  try {
    auto& h = handle(p);
    if (count && (!strings || !out)) throw std::invalid_argument("Null query buffer");
    std::vector<mundo_x_bridge::Pauli> paulis;
    paulis.reserve(count);
    for (std::size_t j = 0; j < count; ++j) {
      if (!strings[j]) throw std::invalid_argument("Null Pauli operator");
      paulis.push_back(mundo_x_bridge::Pauli::parse(strings[j], h.n));
    }
    if (h.compact && !h.observer) {
      h.observer = std::make_unique<mundo_x_bridge::PauliObserver>(*h.compact);
      ++h.rebuilds;
    }
    for (std::size_t j = 0; j < count; ++j) {
      out[j] = h.compact ? h.observer->expectation(std::move(paulis[j]))
                         : dense_expectation(*h.dense, paulis[j]);
    }
    return 0;
  } catch (const std::exception& e) { last_error=e.what(); return -1; }
}

int mx_generator(void* p, std::size_t row, char* out, std::size_t capacity) {
  try {
    auto& h = handle(p);
    if (!h.compact) throw std::invalid_argument("Generator requires CompactCore");
    if (!out) throw std::invalid_argument("Null generator buffer");
    const auto s = h.compact->generator_string(row);
    if (capacity <= s.size()) throw std::invalid_argument("Small output buffer");
    std::memcpy(out, s.c_str(), s.size()+1);
    return 0;
  } catch (const std::exception& e) { last_error=e.what(); return -1; }
}

std::uint64_t mx_revision(void* p) { return static_cast<Handle*>(p)->revision; }
std::uint64_t mx_rebuilds(void* p) { return static_cast<Handle*>(p)->rebuilds; }
std::size_t mx_state_bytes(void* p) {
  auto& h=*static_cast<Handle*>(p);
  return h.compact ? h.compact->state_bytes() : h.dense->state_bytes();
}
std::size_t mx_observer_bytes(void* p) {
  auto& h=*static_cast<Handle*>(p);
  return h.observer ? h.observer->payload_bytes() : 0;
}
}
