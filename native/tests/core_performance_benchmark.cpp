// The same file is compiled twice against frozen and candidate headers. The
// baseline comparison driver rejects any differing output/state fingerprints.
#include "mundo_x/engine_session.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <random>

namespace {
using Core = mundo_x::CompactCore;
using Session = mundo_x::EngineSession;
using Gate = Session::Gate;
using Clock = std::chrono::steady_clock;
constexpr std::uint64_t seed = 0x434f52455f504552ULL;
std::uint64_t hash = 1469598103934665603ULL;

void mix(std::uint64_t value) {
  for (unsigned i = 0; i < 8; ++i) {
    hash ^= static_cast<unsigned char>(value >> (8 * i));
    hash *= 1099511628211ULL;
  }
}

void mix_generators(const Core& core) {
  for (std::size_t r = 0; r < core.qubits(); ++r)
    for (const unsigned char c : core.generator_string(r)) {
      hash ^= c; hash *= 1099511628211ULL;
    }
}

std::vector<Gate> circuit(std::size_t n, std::size_t count, std::uint64_t salt = 0) {
  std::mt19937_64 rng(seed ^ n ^ salt);
  std::vector<Gate> result;
  result.reserve(count);
  for (std::size_t i = 0; i < count; ++i) {
    Gate gate{rng() % 7, rng() % n, rng() % n};
    if (gate.operation == MX_GATE_CX || gate.operation == MX_GATE_CZ) {
      if (n == 1) gate.operation = MX_GATE_Y;
      else if (gate.first == gate.second) gate.second = (gate.first + 1) % n;
    }
    result.push_back(gate);
  }
  return result;
}

void apply(Core& core, const Gate& gate) {
  switch (gate.operation) {
    case MX_GATE_H: core.hadamard(gate.first); break;
    case MX_GATE_S: core.phase_s(gate.first); break;
    case MX_GATE_X: core.pauli_x(gate.first); break;
    case MX_GATE_Z: core.pauli_z(gate.first); break;
    case MX_GATE_CX: core.cnot(gate.first, gate.second); break;
    case MX_GATE_CZ: core.controlled_z(gate.first, gate.second); break;
    case MX_GATE_Y: core.pauli_z(gate.first); core.pauli_x(gate.first); break;
  }
}

Core prepared(std::size_t n) {
  Core core(n);
  for (const auto gate : circuit(n, 12 * n)) apply(core, gate);
  return core;
}

std::vector<std::string> observables(const Core& core) {
  std::vector<std::string> result;
  for (std::size_t i = 0; i < 32; ++i) {
    auto text = core.generator_string(i % core.qubits());
    if (i & 1U) text[0] = text[0] == '+' ? '-' : '+';
    result.push_back(std::move(text));
  }
  return result;
}

void validate() {
  std::size_t gates = 0, queries = 0, measurements = 0;
  for (const std::size_t n : {1, 2, 6, 20, 63, 64, 65, 127, 128, 129, 256}) {
    auto core = prepared(n);
    std::mt19937_64 rng(seed ^ n);
    const auto ops = circuit(n, 4 * n + 128, 0x56414c4944415445ULL);
    for (std::size_t i = 0; i < ops.size(); ++i) {
      apply(core, ops[i]); ++gates;
      if (i % 17 != 0) continue;
      mix_generators(core);
      const mundo_x_bridge::PauliObserver observer(core);
      for (const auto& text : observables(core)) {
        mix(static_cast<unsigned>(observer.expectation(mundo_x_bridge::Pauli::parse(text, n)) + 1));
        ++queries;
      }
      const auto q = rng() % n;
      mix(static_cast<unsigned>(2 * core.probability_one_z(q))); ++queries;
      const auto result = core.measure_z(q, rng); ++measurements;
      mix(result.outcome_one); mix(result.random);
      mix(static_cast<unsigned>(2 * result.probability));
      mix_generators(core);
    }
    // Exercise deterministic GF(2) elimination, signed implicit generators,
    // canonicalization and measurement after crossing a 64-bit word boundary.
    Core basis(n);
    for (std::size_t q = 0; q < n; ++q) if (rng() & 1U) basis.pauli_x(q);
    if (n > 1) {
      for (std::size_t i = 0; i < 4 * n; ++i) {
        const auto a = rng() % n, b = (a + 1 + rng() % (n - 1)) % n;
        basis.cnot(a, b); ++gates;
      }
    }
    for (std::size_t q = 0; q < n; ++q) {
      mix(static_cast<unsigned>(2 * basis.probability_one_z(q))); ++queries;
    }
    basis.canonicalize_computational_basis();
    mix_generators(basis); mix(basis.state_bytes());
  }
  std::cout << "{\"checksum\":\"" << std::hex << hash << std::dec
            << "\",\"gates\":" << gates << ",\"queries\":" << queries
            << ",\"measurements\":" << measurements << "}\n";
}

struct Result {
  std::int64_t elapsed_ns;
  std::size_t core_bytes, observer_bytes = 0;
};

Result run(const std::string& kind, std::size_t n, std::size_t iterations) {
  auto core = prepared(n);
  const auto texts = observables(core);
  Result result{0, core.state_bytes(), 0};
  std::uint64_t accumulator = 0;
  if (kind == "observer_build") {
    const auto pauli = mundo_x_bridge::Pauli::parse(texts[0], n);
    const auto start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
      const mundo_x_bridge::PauliObserver observer(core);
      accumulator += static_cast<unsigned>(observer.expectation(pauli) + 1);
      result.observer_bytes = observer.payload_bytes();
    }
    result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
  } else if (kind == "cz_gate") {
    std::mt19937_64 rng(seed ^ n);
    std::vector<std::pair<std::size_t, std::size_t>> pairs;
    for (std::size_t i = 0; i < iterations; ++i) {
      const auto a = rng() % n, b = (a + 1 + rng() % (n - 1)) % n;
      pairs.emplace_back(a, b);
    }
    const auto start = Clock::now();
    for (const auto pair : pairs) core.controlled_z(pair.first, pair.second);
    result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
  } else if (kind == "measurement_round") {
    const auto initial = core;
    std::mt19937_64 rng(seed ^ n);
    const auto start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
      core = initial;
      for (std::size_t q = 0; q < n; ++q) {
        const auto measured = core.measure_z(q, rng);
        accumulator += static_cast<unsigned>(measured.outcome_one) +
                       2U * static_cast<unsigned>(measured.random);
      }
    }
    result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
  } else if (kind == "graphics_revision" || kind == "cached_query") {
    Session session(Session::Backend::Compact, n);
    session.apply(circuit(n, 12 * n));
    (void)session.query(texts);
    std::vector<std::vector<Gate>> updates;
    for (const auto gate : circuit(n, iterations, 0x555044415445ULL)) updates.push_back({gate});
    const auto start = Clock::now();
    for (std::size_t i = 0; i < iterations; ++i) {
      if (kind == "graphics_revision") session.apply(updates[i]);
      const auto values = session.query(texts);
      for (const auto value : values) accumulator += static_cast<unsigned>(value + 1);
    }
    result.elapsed_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(Clock::now() - start).count();
    result.observer_bytes = session.observer_bytes();
    mix(session.revision()); mix(session.rebuilds());
    for (std::size_t r = 0; r < n; ++r)
      for (const unsigned char c : session.generator(r)) {
        hash ^= c; hash *= 1099511628211ULL;
      }
  } else throw std::invalid_argument("Unknown benchmark case");
  mix(accumulator); mix_generators(core);
  return result;
}
} // namespace

int main(int argc, char** argv) {
  try {
    if (argc == 2 && std::string(argv[1]) == "--validate") { validate(); return 0; }
    if (argc != 4) throw std::invalid_argument("Expected CASE QUBITS ITERATIONS or --validate");
    const std::string kind = argv[1];
    const auto n = std::stoull(argv[2]), iterations = std::stoull(argv[3]);
    if (n < 2 || n > 256 || iterations == 0) throw std::invalid_argument("Benchmark bounds");
    (void)run(kind, n, std::min<std::size_t>(iterations, 8)); // excluded warm-up
    hash = 1469598103934665603ULL;
    const auto result = run(kind, n, iterations);
    std::cout << "{\"case\":\"" << kind << "\",\"qubits\":" << n
              << ",\"iterations\":" << iterations << ",\"elapsed_ns\":" << result.elapsed_ns
              << ",\"checksum\":\"" << std::hex << hash << std::dec
              << "\",\"core_payload_bytes\":" << result.core_bytes
              << ",\"observer_payload_bytes\":" << result.observer_bytes << "}\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL core benchmark " << error.what() << '\n';
    return 1;
  }
}
