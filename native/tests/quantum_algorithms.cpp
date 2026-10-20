#include "mundo_x/compact_core.hpp"
#include "mundo_x/ether_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/pauli_observer.hpp"

#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::size_t checks=0;
void require(bool ok,const char* message){++checks;if(!ok)throw std::runtime_error(message);}

double local_z(const mundo_x_bridge::PauliObserver& observer,std::size_t n,std::size_t q){
  mundo_x_bridge::Pauli p;
  const std::size_t words=n/64+(n%64!=0);
  p.x.assign(words,0);p.z.assign(words,0);
  p.z[q/64]|=std::uint64_t{1}<<(q%64);
  return observer.expectation(std::move(p));
}

std::vector<bool> secret(std::size_t n){
  std::vector<bool> out(n);
  for(std::size_t q=0;q<n;++q)out[q]=((q*73+19)%101)<49;
  return out;
}

// Bernstein-Vazirani with a phase-kickback ancilla. The complete circuit is
// Clifford, so CompactCore can run large registers without a dense statevector.
double bernstein_vazirani(std::size_t input_qubits){
  const auto wanted=secret(input_qubits);
  mundo_x::CompactCore core(input_qubits+1);
  const auto ancilla=input_qubits;
  core.pauli_x(ancilla);
  for(std::size_t q=0;q<=ancilla;++q)core.hadamard(q);
  for(std::size_t q=0;q<input_qubits;++q)if(wanted[q])core.cnot(q,ancilla);
  for(std::size_t q=0;q<input_qubits;++q)core.hadamard(q);
  const mundo_x_bridge::PauliObserver observer(core);
  for(std::size_t q=0;q<input_qubits;++q){
    const double z=local_z(observer,core.qubits(),q);
    require(z==(wanted[q]?-1.0:1.0),"Bernstein-Vazirani secret bit mismatch");
  }
  return static_cast<double>(core.state_bytes());
}

// Deutsch-Jozsa for affine Boolean functions f(x)=a.x xor b.
// a=0 is constant; every non-zero a is balanced.
void deutsch_jozsa(std::size_t n,bool balanced){
  mundo_x::CompactCore core(n+1);
  const std::size_t anc=n;
  core.pauli_x(anc);
  for(std::size_t q=0;q<=anc;++q)core.hadamard(q);
  if(balanced){
    // Nonzero affine mask; b affects only a global kickback phase and is
    // deliberately irrelevant to the classification.
    for(std::size_t q=0;q<n;q+=3)core.cnot(q,anc);
  }else{
    // Constant-one oracle: flip the target. Constant-zero does nothing.
    core.pauli_x(anc);
  }
  for(std::size_t q=0;q<n;++q)core.hadamard(q);
  const mundo_x_bridge::PauliObserver observer(core);
  bool any_one=false;
  for(std::size_t q=0;q<n;++q){
    const double z=local_z(observer,core.qubits(),q);
    require(z==1.0||z==-1.0,"Deutsch-Jozsa output is not deterministic");
    any_one|=z==-1.0;
  }
  require(any_one==balanced,"Deutsch-Jozsa classification mismatch");
}

void non_clifford_interference(){
  mundo_x::ReducedExactTRepresentation state(1,16);
  state.hadamard(0);
  state.phase_t(0);
  state.hadamard(0);
  const double expected=std::sqrt(.5);
  require(std::abs(state.z_expectation(0)-expected)<1e-12,
          "H-T-H non-Clifford interference is wrong");
  require(state.t_count()==1&&state.component_count()>=1,
          "Reduced-T did not record the T resource");
  require(std::abs(state.sparse_norm()-1.0)<1e-12,
          "Reduced-T state norm drifted");
  require(std::abs(state.fidelity()-1.0)<1e-12,
          "Reduced-T disagrees with its independent dense replay");
}

void ether_cluster(){
  mundo_x::EtherCore ether;
  ether.h_all();ether.cz_neighbors();
  const std::string q="1"+std::string(1000,'0');
  auto predecessor=[](std::string x){
    for(std::size_t i=x.size();i-->0;){if(x[i]!='0'){--x[i];break;}x[i]='9';}
    if(x.size()>1&&x.front()=='0')x.erase(x.begin());return x;
  };
  auto successor=[](std::string x){
    for(std::size_t i=x.size();i-->0;){if(x[i]!='9'){++x[i];return x;}x[i]='0';}
    return std::string("1")+x;
  };
  require(ether.expectation({{predecessor(q),'Z'},{q,'X'},{successor(q),'Z'}})==1.0,
          "EtherCore remote cluster stabilizer failed");
  require(ether.stats().last_query_peak_sites<=3,
          "EtherCore remote cluster query expanded beyond its local cone");
}
}

int main(){
  try{
    const auto start=std::chrono::steady_clock::now();
    const double payload=bernstein_vazirani(512);
    deutsch_jozsa(256,false);
    deutsch_jozsa(256,true);
    non_clifford_interference();
    ether_cluster();
    const auto end=std::chrono::steady_clock::now();
    const double ms=std::chrono::duration<double,std::milli>(end-start).count();
    std::cout<<"{\"ok\":true,\"checks\":"<<checks
             <<",\"bv_input_qubits\":512"
             <<",\"dj_input_qubits\":256"
             <<",\"bv_state_bytes\":"<<payload
             <<",\"non_clifford\":\"H-T-H\""
             <<",\"non_clifford_expected_z\":"<<std::sqrt(.5)
             <<",\"ether_address_digits\":1001"
             <<",\"elapsed_ms\":"<<ms<<"}\n";
    return 0;
  }catch(const std::exception& e){
    std::cerr<<"quantum_algorithms: "<<e.what()<<"\n";return 1;
  }
}
