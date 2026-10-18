#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <type_traits>
#include <vector>
#include "mundo_x/exact_t_experiment.hpp"

using Clock = std::chrono::steady_clock;
struct Gate { std::string op; std::size_t a, b; };
template<class T> void apply(T& s, const Gate& g) {
  if(g.op=="H") s.hadamard(g.a);
  else if(g.op=="S") s.phase_s(g.a);
  else if(g.op=="X") s.pauli_x(g.a);
  else if(g.op=="Z") s.pauli_z(g.a);
  else if(g.op=="T") {
    if constexpr (!std::is_same_v<T,mundo_x::CompactCore>) s.phase_t(g.a);
    else throw std::invalid_argument("T in compact circuit");
  }
  else if(g.op=="CX") s.cnot(g.a,g.b);
  else if(g.op=="CZ") s.controlled_z(g.a,g.b);
  else throw std::invalid_argument("unknown gate");
}
template<class T> void print_state(const T& state) {
  std::cout << '[';
  for(std::size_t i=0;i<state.size();++i) {
    if(i) std::cout << ',';
    std::cout << '[' << state[i].real() << ',' << state[i].imag() << ']';
  }
  std::cout << ']';
}
int main(int argc,char** argv) {
  if(argc!=3) return 2;
  const bool compact = std::string(argv[1])=="compact";
  std::ifstream input(argv[2]); std::size_t n,depth;
  if(!(input>>n>>depth)) return 2;
  std::vector<Gate> gates(depth);
  for(auto& g:gates) if(!(input>>g.op>>g.a>>g.b)) return 2;
  std::cout << std::setprecision(17);
  if(compact) {
    mundo_x::CompactCore state(n); std::vector<bool> outcomes;
    for(const auto& g:gates) {
      if(g.op=="M") outcomes.push_back(state.measure_z_forced(g.a,g.b));
      else apply(state,g);
    }
    std::cout << "{\"n\":" << n << ",\"nondeterministic\":[";
    for(std::size_t i=0;i<outcomes.size();++i) {
      if(i) std::cout << ',';
      std::cout << (outcomes[i]?"true":"false");
    }
    std::cout << "],\"generators\":[";
    for(std::size_t q=0;q<n;++q) {
      if(q) std::cout << ',';
      std::cout << '"' << state.generator_string(q) << '"';
    }
    std::cout << "]}\n";
  } else {
    mundo_x::ReducedExactTRepresentation state(n);
    std::size_t peak=1;
    const auto t0=Clock::now();
    for(const auto& g:gates) { apply(state,g); peak=std::max(peak,state.component_count()); }
    const auto t1=Clock::now();
    const auto reconstructed=state.dense_from_reduced();
    const auto t2=Clock::now();
    const auto dense=state.dense_reference();
    std::cout << "{\"n\":" << n << ",\"components\":" << state.component_count()
              << ",\"peak_components\":" << peak << ",\"t_count\":" << state.t_count()
              << ",\"apply_seconds\":" << std::chrono::duration<double>(t1-t0).count()
              << ",\"reconstruct_seconds\":" << std::chrono::duration<double>(t2-t1).count()
              << ",\"reduced\":";
    print_state(reconstructed); std::cout << ",\"dense_diagnostic\":"; print_state(dense);
    std::cout << "}\n";
  }
}
