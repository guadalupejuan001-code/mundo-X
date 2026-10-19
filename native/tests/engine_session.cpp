#include "mundo_x/engine_session.hpp"
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <random>
#include <thread>

namespace {
using Session = mundo_x::EngineSession;
using Gate = Session::Gate;
using Backend = Session::Backend;

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

template<class Action>
void require_failure(Action action, const std::string& context) {
  try { action(); }
  catch (const std::exception&) { return; }
  throw std::runtime_error("Expected failure: " + context);
}

std::vector<std::string> generators(const Session& core) {
  std::vector<std::string> result;
  for (std::size_t q = 0; q < core.qubits(); ++q) result.push_back(core.generator(q));
  return result;
}

bool same_snapshot(const Session::Snapshot& a, const Session::Snapshot& b) {
  return a.version == b.version && a.backend == b.backend &&
         a.qubits == b.qubits && a.revision == b.revision && a.gates == b.gates;
}

// The validation oracle is the user's own DenseCore64. Its expectation is
// calculated independently from character-by-character Pauli matrix action,
// without the production Pauli masks, GF(2) observer or bridge helper.
void reference_gate(mundo_x::DenseCore64& core, const Gate& gate) {
  switch (gate.operation) {
    case MX_GATE_H: core.hadamard(gate.first); break;
    case MX_GATE_S: core.phase_s(gate.first); break;
    case MX_GATE_X: core.pauli_x(gate.first); break;
    case MX_GATE_Z: core.pauli_z(gate.first); break;
    case MX_GATE_CX: core.cnot(gate.first, gate.second); break;
    case MX_GATE_CZ: core.controlled_z(gate.first, gate.second); break;
    case MX_GATE_Y: core.pauli_z(gate.first); core.pauli_x(gate.first); break;
    default: throw std::logic_error("Test generator produced unsupported gate");
  }
}

double reference_expectation(const mundo_x::DenseCore64& core, const std::string& text) {
  const std::size_t offset = text.size() == core.qubits() + 1 ? 1 : 0;
  const double global_sign = offset && text[0] == '-' ? -1.0 : 1.0;
  std::complex<double> sum{0.0, 0.0};
  for (std::size_t source = 0; source < core.state().size(); ++source) {
    std::size_t target = source;
    std::complex<double> phase{global_sign, 0.0};
    for (std::size_t q = 0; q < core.qubits(); ++q) {
      const auto bit = std::size_t{1} << q;
      const bool one = (source & bit) != 0;
      switch (text[offset + q]) {
        case 'I': break;
        case 'X': target ^= bit; break;
        case 'Z': if (one) phase = -phase; break;
        case 'Y': target ^= bit; phase *= std::complex<double>{0.0, one ? -1.0 : 1.0}; break;
        default: throw std::logic_error("Invalid Pauli in independent oracle");
      }
    }
    sum += std::conj(core.state()[target]) * phase * core.state()[source];
  }
  require(std::abs(sum.imag()) < 1e-11, "Independent expectation has imaginary part");
  return sum.real();
}

std::vector<std::string> all_signed_paulis(std::size_t n) {
  std::vector<std::string> result;
  const std::size_t count = std::size_t{1} << (2*n);
  for (std::size_t code = 0; code < count; ++code) {
    std::string word(n, 'I');
    for (std::size_t q = 0; q < n; ++q) word[q] = "IXYZ"[(code >> (2*q)) & 3];
    result.push_back("+" + word);
    result.push_back("-" + word);
  }
  return result;
}

void native_oracle(std::size_t& comparisons, double& max_error) {
  std::mt19937_64 rng(0x434f534d49435f58ULL);
  for (std::size_t n = 1; n <= 5; ++n) {
    Session compact(Backend::Compact, n);
    Session dense_session(Backend::DenseReference, n);
    mundo_x::DenseCore64 reference(n);
    const auto paulis = all_signed_paulis(n);
    for (std::size_t step = 0; step < 32; ++step) {
      Gate gate{rng() % 7, rng() % n, rng() % n};
      if (gate.operation == MX_GATE_CX || gate.operation == MX_GATE_CZ) {
        if (n == 1) gate.operation = MX_GATE_Y;
        else if (gate.first == gate.second) gate.second = (gate.first + 1) % n;
      }
      compact.apply({gate});
      dense_session.apply({gate});
      reference_gate(reference, gate);
      const auto before = compact.snapshot();
      const auto rows = generators(compact);
      const auto observed = compact.query(paulis);
      const auto dense_observed = dense_session.query(paulis);
      for (std::size_t i = 0; i < paulis.size(); ++i) {
        const double expected = reference_expectation(reference, paulis[i]);
        max_error = std::max(max_error, std::abs(observed[i] - expected));
        require(std::abs(observed[i] - expected) < 1e-11,
                "Compact observer disagrees with dense reference: " + paulis[i]);
        require(std::abs(dense_observed[i] - expected) < 1e-11,
                "Dense ABI observer disagrees with independent Pauli action");
        ++comparisons;
      }
      require(same_snapshot(before, compact.snapshot()) && rows == generators(compact),
              "Observation changed a core or its gate history");
      const auto rebuilds = compact.rebuilds();
      compact.query(paulis);
      require(compact.rebuilds() == rebuilds && compact.revision() == step + 1,
              "Repeated observation rebuilt an unchanged state");
    }
  }
}

void transaction_and_snapshot() {
  Session core(Backend::Compact, 3);
  core.apply({{MX_GATE_H,0,0}, {MX_GATE_CX,0,1}, {MX_GATE_S,2,42}});
  const std::vector<std::string> paulis = {"XXI", "YYI", "ZZI", "IIZ", "-III"};
  const auto values = core.query(paulis);
  const auto initial = core.snapshot();
  require(initial.gates.back().second == 0, "Unary operand was not canonicalized");
  const auto initial_rows = generators(core);
  const auto revision = core.revision(), epoch = core.epoch(), rebuilds = core.rebuilds();
  const auto observer_bytes = core.observer_bytes();
  for (const Gate bad : std::vector<Gate>{{7,0,0}, {MX_GATE_X,3,0},
                                         {MX_GATE_CX,0,0}, {MX_GATE_CZ,0,3}}) {
    require_failure([&] { core.apply({{MX_GATE_X,0,0}, bad}); }, "transactional batch");
    require(same_snapshot(initial, core.snapshot()) && initial_rows == generators(core),
            "Rejected batch partially changed state");
    require(core.revision() == revision && core.epoch() == epoch &&
            core.rebuilds() == rebuilds && core.observer_bytes() == observer_bytes,
            "Rejected batch invalidated observation state");
  }
  core.apply({});
  require(core.revision() == revision && core.epoch() == epoch,
          "Empty batch changed the revision");
  require_failure([&] { core.query({"XXI", "+IXQ"}); }, "invalid Pauli");
  require(core.rebuilds() == rebuilds && same_snapshot(core.snapshot(), initial),
          "Invalid query changed core/cache");

  auto invalid = initial;
  invalid.gates.push_back({MX_GATE_CX,0,0});
  require_failure([&] { core.restore(invalid); }, "invalid restore");
  invalid = initial;
  invalid.version = 2;
  require_failure([&] { core.restore(invalid); }, "unknown snapshot version");
  invalid = initial;
  invalid.backend = Backend::DenseReference;
  require_failure([&] { core.restore(invalid); }, "backend mismatch");
  invalid = initial;
  invalid.qubits = 4;
  require_failure([&] { core.restore(invalid); }, "qubit mismatch");
  require(core.query(paulis) == values && core.epoch() == epoch,
          "Failed restore changed the live observer");

  core.apply({{MX_GATE_Y,1,0}});
  require(core.query(paulis) != values, "Mutation did not reach borrowed observer");
  const auto changed_epoch = core.epoch();
  core.restore(initial);
  require(core.query(paulis) == values && generators(core) == initial_rows &&
          same_snapshot(initial, core.snapshot()) && core.epoch() == changed_epoch + 1,
          "Snapshot restore did not restore exact state/history");
  const auto current_epoch = core.epoch();
  core.restore(initial);
  require(core.revision() == initial.revision && core.epoch() == current_epoch + 1,
          "Same-revision import failed to invalidate external observation caches");

  auto maximum_revision = initial;
  maximum_revision.revision = std::numeric_limits<std::uint64_t>::max();
  core.restore(maximum_revision);
  require_failure([&] { core.apply({{MX_GATE_X,0,0}}); }, "revision overflow");
  require(same_snapshot(maximum_revision, core.snapshot()), "Overflow partially applied gate");
}

void wide_state() {
  Session wide(Backend::Compact, 130);
  wide.apply({{MX_GATE_H,0,0}, {MX_GATE_CX,0,65}, {MX_GATE_CX,65,129}});
  std::string xxx(130, 'I'), yyx(130, 'I'), zz(130, 'I');
  xxx[0] = xxx[65] = xxx[129] = 'X';
  yyx[0] = yyx[65] = 'Y'; yyx[129] = 'X';
  zz[0] = zz[129] = 'Z';
  const auto values = wide.query({xxx, yyx, zz});
  require(values == std::vector<double>({1.0,-1.0,1.0}), "Multiword signed GHZ query failed");
  const auto saved = wide.snapshot();
  Session restored(Backend::Compact, 130);
  restored.restore(saved);
  require(restored.query({xxx, yyx, zz}) == values && generators(wide) == generators(restored),
          "Multiword core snapshot lost state");
  require(wide.state_bytes() == 6370, "Compact payload accounting changed");
}

struct Handle {
  void* pointer;
  explicit Handle(int kind, std::size_t n) : pointer(mx_create(kind, n)) {
    require(pointer != nullptr, mx_last_error());
  }
  ~Handle() { mx_destroy(pointer); }
};

void c_abi() {
  require(mx_capabilities(MX_BACKEND_COMPACT) & MX_CAP_STABILIZER_GENERATORS,
          "Compact capability declaration missing");
  require(mx_gate_mask(MX_BACKEND_COMPACT) == 0x7f &&
          !(mx_capabilities(MX_BACKEND_COMPACT) & MX_CAP_T_OPERATIONS),
          "Clifford capabilities misdeclared");
  require(mx_capabilities(MX_BACKEND_REDUCED_T) == 0 &&
          mx_capabilities(MX_BACKEND_FACTORIZED) == 0 &&
          mx_create(MX_BACKEND_REDUCED_T, 3) == nullptr,
          "Unconnected core silently substituted another backend");
  require(mx_create(94,2) == nullptr && mx_create(MX_BACKEND_COMPACT,0) == nullptr,
          "Invalid session accepted");
  require(mx_apply(nullptr,nullptr,0) == -1 && mx_revision(nullptr) == 0 &&
          std::strlen(mx_last_error()) != 0, "Null session failure not reported");
  Handle core(MX_BACKEND_COMPACT, 2);
  const std::uint64_t gates[] = {MX_GATE_H,0,0, MX_GATE_CX,0,1};
  require(mx_apply(core.pointer,gates,2) == 0, mx_last_error());
  require(mx_qubits(core.pointer) == 2 && mx_backend(core.pointer) == MX_BACKEND_COMPACT,
          "Session identity query failed");
  const char* paulis[] = {"XX", "YY", "ZZ"};
  double out[] = {7.0,7.0,7.0};
  require(mx_query(core.pointer,paulis,3,out) == 0 && out[0] == 1.0 &&
          out[1] == -1.0 && out[2] == 1.0, "C ABI Bell expectations failed");
  const char* invalid[] = {"XX", "AX", "ZZ"};
  double sentinel[] = {7.0,8.0,9.0};
  require(mx_query(core.pointer,invalid,3,sentinel) == -1 && sentinel[0] == 7.0 &&
          sentinel[1] == 8.0 && sentinel[2] == 9.0,
          "Invalid C ABI query partially wrote output");
  require(mx_query(core.pointer,nullptr,1,out) == -1 &&
          mx_query(core.pointer,paulis,1,nullptr) == -1 &&
          mx_query(core.pointer,nullptr,0,nullptr) == 0, "Query null-buffer contract failed");
  require(mx_apply(core.pointer,nullptr,1) == -1 &&
          mx_apply(core.pointer,gates,std::numeric_limits<std::size_t>::max()) == -1,
          "Gate null/overflow validation failed");

  std::size_t count = 0;
  std::uint64_t revision = 0;
  require(mx_snapshot(core.pointer,nullptr,0,&count,&revision) == 0 && count == 2 && revision == 1,
          "Snapshot metadata query failed");
  std::uint64_t exported[] = {90,90,90,90,90,90};
  require(mx_history(core.pointer,exported,1) == -1 && exported[0] == 90 && exported[5] == 90,
          "Short snapshot capacity partially wrote data");
  require(mx_snapshot(core.pointer,exported,2,&count,&revision) == 0 &&
          std::equal(std::begin(gates),std::end(gates),std::begin(exported)),
          "Atomic history export failed");
  require(mx_history_count(core.pointer) == 2 && mx_history_bytes(core.pointer) == 48,
          "History logical size accounting failed");
  Handle restored(MX_BACKEND_COMPACT,2);
  require(mx_restore(restored.pointer,exported,count,revision) == 0 &&
          mx_revision(restored.pointer) == 1 && mx_epoch(restored.pointer) == 1,
          "C ABI restore failed");
  double back[3];
  require(mx_query(restored.pointer,paulis,3,back) == 0 &&
          std::equal(std::begin(out),std::end(out),std::begin(back)),
          "C ABI replay changed Bell state");
  const std::uint64_t bad[] = {MX_GATE_X,0,0, MX_GATE_CX,1,1};
  const auto epoch = mx_epoch(restored.pointer), rebuilds = mx_rebuilds(restored.pointer);
  require(mx_restore(restored.pointer,bad,2,500) == -1 &&
          mx_revision(restored.pointer) == 1 && mx_epoch(restored.pointer) == epoch &&
          mx_rebuilds(restored.pointer) == rebuilds, "C ABI failed restore changed state");
  char generator[4];
  require(mx_generator(core.pointer,0,generator,sizeof(generator)) == 0 &&
          std::string(generator) == "+XX", "Generator C ABI failed");
  char small[] = {'a','b'};
  require(mx_generator(core.pointer,0,small,sizeof(small)) == -1 && small[0] == 'a',
          "Generator short buffer was modified");
}

void concurrent_snapshot() {
  Handle core(MX_BACKEND_COMPACT,1);
  std::atomic<bool> started{false}, done{false}, failed{false};
  std::thread writer([&] {
    started = true;
    const std::uint64_t gate[] = {MX_GATE_X,0,0};
    for (std::size_t i = 0; i < 1000; ++i)
      if (mx_apply(core.pointer,gate,1)) failed = true;
    done = true;
  });
  std::thread reader([&] {
    while (!started.load()) std::this_thread::yield();
    const char* paulis[] = {"Z", "-Z"};
    std::vector<std::uint64_t> history(3000);
    do {
      double values[2];
      std::size_t count = 0;
      std::uint64_t revision = 0;
      if (mx_query(core.pointer,paulis,2,values) || values[0] != -values[1]) failed = true;
      if (mx_snapshot(core.pointer,history.data(),1000,&count,&revision) ||
          count != revision) failed = true;
      for (std::size_t i = 0; i < count; ++i)
        if (history[3*i] != MX_GATE_X || history[3*i+1] != 0 || history[3*i+2] != 0)
          failed = true;
    } while (!done.load());
  });
  writer.join();
  reader.join();
  require(!failed.load() && mx_revision(core.pointer) == 1000,
          "Concurrent observer or snapshot saw a partial batch");
}
} // namespace

int main() {
  try {
    std::size_t comparisons = 0;
    double max_error = 0.0;
    native_oracle(comparisons,max_error);
    transaction_and_snapshot();
    wide_state();
    c_abi();
    concurrent_snapshot();
    std::cout << "PASS shared_session signed_pauli_comparisons=" << comparisons
              << " max_abs_error=" << max_error
              << " transactional_apply_restore=PASS readonly_observation=PASS"
              << " portable_history=PASS wide_qubits=130"
              << " same_revision_cache_invalidation=PASS concurrent_snapshot=PASS\n";
  } catch (const std::exception& error) {
    std::cerr << "FAIL " << error.what() << '\n';
    return EXIT_FAILURE;
  }
}
