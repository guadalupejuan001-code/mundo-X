// Standalone scale worker. Build with -std=c++17 -O3 -DNDEBUG and an include
// directory from the revision being measured. No DenseCore runs in a timer.
#include "mundo_x/exact_t_experiment.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdint>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <vector>

namespace {
using Complex = std::complex<double>;
using Clock = std::chrono::steady_clock;
struct Gate { char op; std::size_t a, b = 0; };
struct Options {
  std::string method = "reduced", family = "product_ht";
  std::size_t n = 8, t_count = 32;
  std::uint64_t seed = 20261017;
  std::uint64_t memory_bytes = 1536ULL*1024*1024;
  std::size_t cpu_seconds = 15;
};

std::string quote(const std::string& s) {
  std::ostringstream out;
  out << '"';
  for (unsigned char c : s) {
    if (c == '"' || c == '\\') out << '\\' << c;
    else if (c == '\n') out << "\\n";
    else if (c == '\r') out << "\\r";
    else if (c == '\t') out << "\\t";
    else if (c < 32) out << "\\u" << std::hex << std::setw(4)
                         << std::setfill('0') << static_cast<int>(c) << std::dec;
    else out << c;
  }
  out << '"';
  return out.str();
}

std::uint64_t splitmix(std::uint64_t& state) {
  std::uint64_t z = (state += 0x9e3779b97f4a7c15ULL);
  z = (z ^ (z >> 30U)) * 0xbf58476d1ce4e5b9ULL;
  z = (z ^ (z >> 27U)) * 0x94d049bb133111ebULL;
  return z ^ (z >> 31U);
}

std::vector<Gate> make_circuit(const Options& o) {
  if (o.n == 0 || o.n > 65536) throw std::invalid_argument("qubits outside 1..65536");
  std::vector<Gate> g;
  auto emit = [&](char op, std::size_t a, std::size_t b = 0) { g.push_back({op,a,b}); };
  if (o.family == "product_ht" || o.family == "product_t2") {
    for (std::size_t q = 0; q < o.n; ++q) emit('H',q);
    for (std::size_t q = 0; q < o.n; ++q) {
      emit('T',q);
      if (o.family == "product_t2") emit('T',q);
    }
  } else if (o.family == "ghz" || o.family == "cli") {
    emit('H',0);
    for (std::size_t q = 1; q < o.n; ++q) {
      emit('C',q-1,q);
      if (o.family == "cli" && (q & 1U) == 0) emit('S',q);
    }
    if (o.family == "cli") {
      // Same gate pattern as run_reduced_exact_t_experiment in the repository.
      for (std::size_t k = 0; k < o.t_count; ++k) {
        const auto a = k % o.n, b = (a + 1) % o.n;
        emit('T',a); emit('H',a);
        if (o.n > 1) emit('C',a,b);
        if ((k & 1U) != 0) emit('S',b);
      }
    }
  } else if (o.family == "random") {
    std::uint64_t rng = o.seed;
    for (std::size_t q = 0; q < o.n; ++q) emit('H',q);
    for (std::size_t k = 0; k < o.t_count; ++k) {
      auto pick = [&]() { return static_cast<std::size_t>(splitmix(rng) % o.n); };
      emit('T',pick()); emit('H',pick()); emit('S',pick());
      if (o.n > 1) {
        const auto a = pick();
        auto b = static_cast<std::size_t>(splitmix(rng) % (o.n-1));
        if (b >= a) ++b;
        emit((splitmix(rng) & 1U) ? 'C' : 'Z',a,b);
      }
    }
  } else if (o.family == "boundary") {
    emit('H',0); emit('T',0);
    if (o.n > 1) { emit('H',o.n-1); emit('T',o.n-1); emit('C',o.n-1,0); }
  } else throw std::invalid_argument("unknown circuit family");
  return g;
}

std::string circuit_hash(const Options& o, const std::vector<Gate>& g) {
  std::uint64_t h = 14695981039346656037ULL;
  auto word = [&](std::uint64_t v) {
    for (unsigned k = 0; k < 8; ++k) { h ^= (v >> (8*k)) & 255; h *= 1099511628211ULL; }
  };
  word(o.n);
  for (const auto& x : g) { word(static_cast<unsigned char>(x.op)); word(x.a); word(x.b); }
  std::ostringstream s; s << std::hex << std::setw(16) << std::setfill('0') << h;
  return s.str();
}

// Independent statevector implementation: no calls into the project's DenseCore.
class IndependentDense {
 public:
  explicit IndependentDense(std::size_t n) {
    if (n == 0 || n >= std::numeric_limits<std::size_t>::digits)
      throw std::invalid_argument("dense shift outside addressable range");
    const std::size_t dim = std::size_t{1} << n;
    if (dim > state.max_size()) throw std::bad_alloc();
    state.assign(dim,Complex{}); state[0] = 1.0;
  }
  void apply(const Gate& g) {
    const auto bit = std::size_t{1} << g.a;
    if (g.op == 'H') {
      const double s = 1/std::sqrt(2.0);
      for (std::size_t base = 0; base < state.size(); base += 2*bit)
        for (std::size_t j = 0; j < bit; ++j) {
          const auto a = state[base+j], b = state[base+j+bit];
          state[base+j] = (a+b)*s; state[base+j+bit] = (a-b)*s;
        }
    } else if (g.op == 'T' || g.op == 'S') {
      const Complex phase = g.op == 'S' ? Complex{0,1} : Complex{1/std::sqrt(2.0),1/std::sqrt(2.0)};
      for (std::size_t base = 0; base < state.size(); base += 2*bit)
        for (std::size_t j = 0; j < bit; ++j) state[base+j+bit] *= phase;
    } else if (g.op == 'C') {
      const auto target = std::size_t{1} << g.b;
      for (std::size_t i = 0; i < state.size(); ++i)
        if ((i & bit) && !(i & target)) std::swap(state[i],state[i|target]);
    } else if (g.op == 'Z') {
      const auto target = std::size_t{1} << g.b;
      for (std::size_t i = 0; i < state.size(); ++i)
        if ((i & bit) && (i & target)) state[i] = -state[i];
    } else throw std::invalid_argument("unknown dense gate");
  }
  std::vector<Complex> state;
};

void apply(mundo_x::ReducedExactTRepresentation& q, const Gate& g) {
  switch (g.op) {
    case 'H': q.hadamard(g.a); break;
    case 'S': q.phase_s(g.a); break;
    case 'T': q.phase_t(g.a); break;
    case 'C': q.cnot(g.a,g.b); break;
    case 'Z': q.controlled_z(g.a,g.b); break;
    default: throw std::invalid_argument("unknown reduced gate");
  }
}
void apply(mundo_x::CompactCore& q, const Gate& g) {
  switch (g.op) {
    case 'H': q.hadamard(g.a); break;
    case 'S': q.phase_s(g.a); break;
    case 'C': q.cnot(g.a,g.b); break;
    case 'Z': q.controlled_z(g.a,g.b); break;
    default: throw std::invalid_argument("compact worker needs Clifford gates");
  }
}
double elapsed(Clock::time_point a, Clock::time_point b) {
  return std::chrono::duration<double,std::milli>(b-a).count();
}
void evolution_complete(double initialization_ms, double evolution_ms) {
  // Flush outside measured evolution so a timeout in a later check can be
  // distinguished from a timeout in the simulator itself.
  std::cerr << std::setprecision(17)
            << "{\"phase\":\"evolution_complete\",\"initialization_ms\":"
            << initialization_ms << ",\"evolution_ms\":" << evolution_ms
            << "}" << std::endl;
}
std::uint64_t getrusage_rss_bytes() {
  rusage u{}; if (getrusage(RUSAGE_SELF,&u) != 0) return 0;
#ifdef __APPLE__
  return static_cast<std::uint64_t>(u.ru_maxrss);
#else
  return static_cast<std::uint64_t>(u.ru_maxrss)*1024;
#endif
}
std::uint64_t rss_bytes() {
#ifdef __linux__
  // ru_maxrss can retain the Python supervisor's pre-exec high water mark.
  // VmHWM belongs to the current executable's memory map and avoids that floor.
  std::ifstream status("/proc/self/status");
  std::string field;
  while (status >> field) {
    if (field == "VmHWM:") { std::uint64_t kb=0; status >> kb; return kb*1024; }
    status.ignore(std::numeric_limits<std::streamsize>::max(),'\n');
  }
#endif
  return getrusage_rss_bytes();
}
Options options(int argc, char** argv) {
  Options o;
  for (int k = 1; k < argc; ++k) {
    const std::string key = argv[k];
    if (k+1 == argc) throw std::invalid_argument("missing option value");
    const std::string val = argv[++k];
    if (key == "--method") o.method=val;
    else if (key == "--family") o.family=val;
    else if (key == "--qubits") o.n=std::stoull(val);
    else if (key == "--t-count") o.t_count=std::stoull(val);
    else if (key == "--seed") o.seed=std::stoull(val);
    else if (key == "--memory-bytes") o.memory_bytes=std::stoull(val);
    else if (key == "--cpu-seconds") o.cpu_seconds=std::stoull(val);
    else throw std::invalid_argument("unknown option: "+key);
  }
  return o;
}

void limits(const Options& o) {
  if (o.memory_bytes == 0 || o.memory_bytes > 1536ULL*1024*1024 || o.cpu_seconds == 0 || o.cpu_seconds > 15)
    throw std::invalid_argument("worker memory/CPU budget exceeds safety ceiling");
  const rlimit address{static_cast<rlim_t>(o.memory_bytes),static_cast<rlim_t>(o.memory_bytes)};
  const rlimit cpu{static_cast<rlim_t>(o.cpu_seconds),static_cast<rlim_t>(o.cpu_seconds+1)};
  const rlimit core{0,0};
  if (setrlimit(RLIMIT_AS,&address) || setrlimit(RLIMIT_CPU,&cpu) || setrlimit(RLIMIT_CORE,&core))
    throw std::runtime_error("cannot install worker resource limits");
}
}  // namespace

int main(int argc, char** argv) {
  Options o;
  try {
    o = options(argc,argv);
    // Apply inside the exec'ed C++ worker. Python preexec_fn would fork a copy
    // of supervisor pages and contaminate ru_maxrss on very small cases.
    limits(o);
    const auto g = make_circuit(o);
    const auto hash = circuit_hash(o,g);
    const std::size_t t_gates = std::count_if(g.begin(),g.end(),[](const Gate& x){return x.op=='T';});
    double init_ms = 0, evolution_ms = 0;
    std::size_t payload = 0, components = 0, peak_components = 0, primitive_count=g.size();
    double norm = 0, fidelity = 0, max_error = 0;
    bool verified = true;
    std::string payload_kind, validation = "not_run";
    if (o.method == "reduced") {
      auto start=Clock::now();
      mundo_x::ReducedExactTRepresentation q(o.n);
      auto ready=Clock::now();
      peak_components=q.component_count();
      for (const auto& x : g) { apply(q,x); peak_components=std::max(peak_components,q.component_count()); }
      auto done=Clock::now();
      init_ms=elapsed(start,ready); evolution_ms=elapsed(ready,done);
      evolution_complete(init_ms,evolution_ms);
      payload=q.logical_payload_bytes(); components=q.component_count();
      payload_kind="declared_logical_payload_excludes_hash_buckets_allocator_and_capacity";
    } else if (o.method == "dense") {
      auto start=Clock::now(); IndependentDense q(o.n); auto ready=Clock::now();
      for (const auto& x : g) q.apply(x);
      auto done=Clock::now();
      init_ms=elapsed(start,ready); evolution_ms=elapsed(ready,done);
      evolution_complete(init_ms,evolution_ms);
      payload=q.state.size()*sizeof(Complex);
      for (const auto& a : q.state) norm += std::norm(a);
      verified=std::abs(norm-1)<1e-9;
      validation="independent_double_norm_only";
      payload_kind="statevector_size_times_complex_double_excludes_allocator";
    } else if (o.method == "compact") {
      if (o.family != "ghz" && o.family != "product_t2")
        throw std::invalid_argument("compact supports ghz or known T*T=S normalization");
      auto start=Clock::now(); mundo_x::CompactCore q(o.n); auto ready=Clock::now();
      if (o.family == "product_t2") {
        for (std::size_t a=0; a<o.n; ++a) q.hadamard(a);
        for (std::size_t a=0; a<o.n; ++a) q.phase_s(a);
        primitive_count=2*o.n;
      } else for (const auto& x : g) apply(q,x);
      auto done=Clock::now();
      init_ms=elapsed(start,ready); evolution_ms=elapsed(ready,done);
      evolution_complete(init_ms,evolution_ms);
      payload=q.state_bytes(); components=peak_components=1;
      if (o.family == "ghz") { verified=q.is_ghz_signature(); validation="ghz_signature"; }
      else validation="known_identity_T_squared_equals_S_no_dense_validation";
      payload_kind="tableau_arrays_only_excludes_allocator";
    } else if (o.method == "validate") {
      if (o.n > 12) throw std::invalid_argument("untimed validation limited to 12 qubits");
      mundo_x::ReducedExactTRepresentation r(o.n);
      IndependentDense d(o.n);
      for (const auto& x : g) { apply(r,x); d.apply(x); }
      // Public reconstruction is isolated from scale workers. The historical
      // baseline replays the base in float; the precision-fixed candidate uses double.
      const auto reconstructed=r.dense_from_reduced();
      Complex overlap{};
      double reference_norm=0;
      for (std::size_t i=0;i<d.state.size();++i) {
        norm += std::norm(reconstructed[i]); reference_norm += std::norm(d.state[i]);
        overlap += std::conj(d.state[i])*reconstructed[i];
        max_error=std::max(max_error,std::abs(d.state[i]-reconstructed[i]));
      }
      fidelity=norm>0 && reference_norm>0 ? std::norm(overlap)/(norm*reference_norm) : 0;
      verified=std::isfinite(fidelity) && std::abs(fidelity-1)<3e-5 && std::abs(norm-1)<3e-5 && max_error<3e-5;
      components=r.component_count(); payload=r.logical_payload_bytes();
      validation="independent_complex_double_vs_public_reconstruction_untimed";
      payload_kind="validation_worker_contains_both_methods_not_a_memory_benchmark";
    } else throw std::invalid_argument("unknown method");
    std::cout << std::setprecision(17)
      << "{\"status\":" << quote(verified?"ok":"correctness_failure")
      << ",\"method\":" << quote(o.method) << ",\"family\":" << quote(o.family)
      << ",\"qubits\":" << o.n << ",\"seed\":" << o.seed
      << ",\"gate_count\":" << g.size() << ",\"executed_primitive_count\":" << primitive_count
      << ",\"t_gate_count\":" << t_gates << ",\"circuit_fnv1a64\":" << quote(hash)
      << ",\"initialization_ms\":" << init_ms << ",\"evolution_ms\":" << evolution_ms
      << ",\"peak_rss_bytes\":" << rss_bytes() << ",\"declared_payload_bytes\":" << payload
      << ",\"getrusage_peak_rss_bytes\":" << getrusage_rss_bytes()
      << ",\"peak_rss_source\":\"linux_proc_self_VmHWM_or_getrusage_fallback\""
      << ",\"payload_kind\":" << quote(payload_kind) << ",\"component_count\":" << components
      << ",\"peak_component_count\":" << peak_components
      << ",\"arithmetic\":\"complex_double_coefficients_or_statevector_and_exact_binary_tableau\""
      << ",\"dense_validation_in_evolution_timer\":false,\"validation\":" << quote(validation)
      << ",\"norm\":" << norm << ",\"fidelity\":" << fidelity << ",\"max_amplitude_error\":" << max_error
      << "}\n";
    return verified?0:2;
  } catch (const std::bad_alloc& e) {
    std::cout << "{\"status\":\"allocation_failure\",\"error\":" << quote(e.what())
              << ",\"peak_rss_bytes\":" << rss_bytes() << "}\n";
    return 3;
  } catch (const std::invalid_argument& e) {
    const bool boundary=o.method=="reduced" && o.family=="boundary" && o.n==64;
    std::cout << "{\"status\":" << quote(boundary?"expected_rejection":"invalid_argument")
              << ",\"error\":" << quote(e.what()) << ",\"peak_rss_bytes\":" << rss_bytes() << "}\n";
    return boundary?0:4;
  } catch (const std::exception& e) {
    std::cout << "{\"status\":\"runtime_failure\",\"error\":" << quote(e.what())
              << ",\"peak_rss_bytes\":" << rss_bytes() << "}\n";
    return 4;
  }
}
