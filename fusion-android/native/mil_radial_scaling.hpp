#pragma once

#include "mundo_x/exact_t_experiment.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace mundo_x {

/**
 * Scaling investigation for the SAME sparse radial tridiagonal Hamiltonian.
 *
 * Exactly decomposes H = diag(d) + hopping * (nearest-neighbor path):
 *  - all-Z diagonal coefficients via Walsh-Hadamard transform O(N log N);
 *  - off-diagonal binary-carry transitions via sum_{r=0}^{n-1} 2^r
 *    XY-type Pauli strings, rather than enumerating all 4^n strings.
 *
 * For N=2^n grid points and UNIFORM real neighbor hopping:
 * term count <= N + (N-1) = 2N-1.
 *
 * Verification recomposes EVERY H_{row,col} mathematically before any
 * quantum run (n<=8; 256x256 tiny audit). No Pauli truncation except
 * explicitly bounded <1e-12 cancellation of float arithmetic.
 *
 * The MIL state is genuinely evolved by the preexisting
 * ReducedExactTRepresentation (H/S/CNOT/phase). RK4 on the ORIGINAL H
 * is a separate reference; it never supplies MIL amplitudes or gates.
 *
 * This is a bounded engineering benchmark, not a nuclear reaction solver,
 * 1000-amplitude dense shortcut, or physical speedup measurement.
 */
class MilRadialScaling {
 public:
  using Complex=std::complex<double>;
  static constexpr std::size_t kMaxQubits=8;
  static constexpr std::size_t kMaxSites=256;
  static constexpr std::size_t kMaxComponents=1024;
  static constexpr std::size_t kMaxGates=200000;
  static constexpr double kMinCoefficient=1e-12;

  struct Term {
    std::array<char,kMaxQubits> pauli{};
    double weight_mev=0.;
  };
  struct Profile {
    std::size_t qubits=0, sites=0, diagonal_terms=0, hopping_terms=0;
    std::size_t total_terms=0,gate_estimate_per_strang_step=0;
    std::size_t verification_entries=0;
    double reconstruction_max_error=0.;
  };
  struct Result {
    Profile profile{};
    std::size_t initial_site=0,steps=0,gates=0,peak_components=0;
    double time_ms=0.,tau_inverse_mev=0.;
    double norm=0.,reference_norm=0.,fidelity=0.,probability_l1=0.;
    std::vector<double> mil,reference;
  };

  static std::vector<Term> decompose(const std::vector<double>& diagonal,
                                    double uniform_hopping,
                                    Profile* profile=nullptr) {
    const auto n=checked(diagonal,uniform_hopping);
    const std::size_t N=diagonal.size();
    Profile p; p.qubits=n;p.sites=N;
    std::vector<Term> terms;
    terms.reserve(2*N-1);

    // Normalized fast Walsh-Hadamard transform for all diagonal Z strings.
    auto amplitudes=diagonal;
    for(std::size_t gap=1;gap<N;gap*=2)
      for(std::size_t offset=0;offset<N;offset+=2*gap)
        for(std::size_t i=0;i<gap;i++) {
          const auto a=amplitudes[offset+i];
          const auto b=amplitudes[offset+i+gap];
          amplitudes[offset+i]=a+b;
          amplitudes[offset+i+gap]=a-b;
        }
    for(std::size_t mask=0;mask<N;mask++) {
      const double c=amplitudes[mask]/static_cast<double>(N);
      if(std::abs(c)<=kMinCoefficient)continue;
      Term term=identity();
      for(std::size_t q=0;q<n;q++)
        if((mask>>q)&1U)term.pauli[q]='Z';
      term.weight_mev=c;terms.push_back(term);++p.diagonal_terms;
    }

    // The only bit patterns for consecutive integers j,j+1 are:
    // XOR(j,j+1) = 2^(r+1)-1, where j ends in r one-bits.
    // The Hermitian transition is a product of raising/lowering operators:
    // exactly 2^r Pauli strings with even numbers of Y's.
    if(uniform_hopping!=0.) {
      for(std::size_t r=0;r<n;r++) {
        const std::size_t width=r+1;
        const std::size_t yChoices=std::size_t{1}<<width;
        const std::size_t lowOnes=(std::size_t{1}<<r)-1;
        for(std::size_t ymask=0;ymask<yChoices;ymask++) {
          const unsigned numberOfY=bitcount(ymask);
          if((numberOfY&1U)!=0U)continue;
          Term term=identity();
          for(std::size_t q=0;q<width;q++)
            term.pauli[q]=(ymask&(std::size_t{1}<<q))?'Y':'X';
          // coefficient = 2*Tr_edge(P)/2^(r+1).  The source bit string
          // has r lower bits = 1 and bit r = 0.
          const unsigned signIndex=numberOfY/2 + bitcount(ymask&lowOnes);
          term.weight_mev=((signIndex&1U)!=0U?-1.:1.)*
                          uniform_hopping/static_cast<double>(std::size_t{1}<<r);
          terms.push_back(term);
          ++p.hopping_terms;
        }
      }
    }
    p.total_terms=terms.size();
    if(p.total_terms>2*N-1)
      throw std::logic_error("tridiagonal Pauli term budget incorrect");
    p.gate_estimate_per_strang_step=0;
    for(const auto& t:terms) {
      bool nonidentity=false;
      for(char c:t.pauli)nonidentity|=c!='I';
      if(nonidentity) {
        const auto cost=rotation_gates(t,n);
        if(cost>kMaxGates)throw std::length_error("term budget exceeded");
        p.gate_estimate_per_strang_step+=2*cost;
      }
    }

    // Unit/integrity oracle. Reconstruct every matrix element from the
    // symbolic Pauli action; neither MIL nor any dense statevector is used.
    for(std::size_t ket=0;ket<N;ket++) {
      for(std::size_t bra=0;bra<N;bra++) {
        Complex element{};
        for(const auto& t:terms) {
          const auto operation=act(t,n,ket);
          if(operation.first==bra)
            element+=t.weight_mev*operation.second;
        }
        const double expected=bra==ket?diagonal[ket]:
           ((bra+1==ket || ket+1==bra)?uniform_hopping:0.);
        p.reconstruction_max_error=
            std::max(p.reconstruction_max_error,std::abs(element-Complex{expected,0.}));
        ++p.verification_entries;
        if(std::abs(element-Complex{expected,0.})>
           3e-9*std::max(1.,std::abs(expected)))
          throw std::logic_error("radial sparse-H Pauli reconstruction failed");
      }
    }
    if(profile)*profile=p;
    return terms;
  }

  static Result evolve(const std::vector<double>& diagonal,
                       double off, std::size_t initial,
                       double tau, std::size_t steps) {
    Profile profile;
    const auto terms=decompose(diagonal,off,&profile);
    const auto N=diagonal.size(),n=profile.qubits;
    if(initial>=N)throw std::invalid_argument("invalid initial radial site");
    if(!std::isfinite(tau)||tau<0||tau>.12)
      throw std::invalid_argument("invalid time in MeV^-1");
    if(steps<1||steps>12)
      throw std::invalid_argument("invalid bounded Strang step count");
    if(profile.gate_estimate_per_strang_step>
       (kMaxGates-n)/steps)
      throw std::length_error("projected MIL gate budget exceeded");

    ReducedExactTRepresentation mil(n,kMaxComponents);
    std::size_t gates=0,peak=1;
    auto mark=[&]() {
      ++gates;
      peak=std::max(peak,mil.component_count());
      if(gates>kMaxGates||peak>kMaxComponents)
        throw std::length_error("MIL memory or gate limit exceeded");
    };
    for(std::size_t bit=0;bit<n;bit++)
      if((initial>>bit)&1U){mil.pauli_x(bit);mark();}

    std::vector<Term> active;
    active.reserve(terms.size());
    for(const auto& t:terms){
      bool identityTerm=true;
      for(std::size_t q=0;q<n;q++) if(t.pauli[q]!='I') identityTerm=false;
      if(!identityTerm)active.push_back(t);
    }
    const auto begin=std::chrono::steady_clock::now();
    for(std::size_t step=0;step<steps;step++){
      const double half=tau/(2.*steps);
      for(const auto& term:active)rotation(mil,term,n,half,mark);
      for(auto it=active.rbegin();it!=active.rend();it++)
        rotation(mil,*it,n,half,mark);
    }
    const auto elapsed=std::chrono::steady_clock::now()-begin;
    const auto milState=mil.dense_from_reduced(); // <=256 validation output
    const auto reference=reference_rk4(diagonal,off,initial,tau);

    Result result;
    result.profile=profile;
    result.initial_site=initial;result.steps=steps;result.gates=gates;
    result.peak_components=peak;
    result.tau_inverse_mev=tau;
    result.time_ms=std::chrono::duration<double,std::milli>(elapsed).count();
    result.mil.resize(N);result.reference.resize(N);
    Complex overlap{};
    for(std::size_t i=0;i<N;i++){
      const auto pm=std::norm(milState[i]);
      const auto pr=std::norm(reference[i]);
      result.mil[i]=pm;result.reference[i]=pr;
      result.norm+=pm;result.reference_norm+=pr;
      result.probability_l1+=std::abs(pm-pr);
      overlap+=std::conj(reference[i])*milState[i];
    }
    if(!(result.norm>0.)||!(result.reference_norm>0.)||
       !std::isfinite(result.norm)||!std::isfinite(result.reference_norm))
      throw std::runtime_error("MIL norm invalid");
    result.fidelity=std::norm(overlap)/(result.norm*result.reference_norm);
    if(!std::isfinite(result.fidelity)||result.fidelity<0.||
       result.fidelity>1.0000001)
      throw std::runtime_error("MIL/reference fidelity invalid");
    return result;
  }

 private:
  static Term identity() {
    Term t;t.pauli.fill('I');return t;
  }
  static unsigned bitcount(std::size_t value) {
    unsigned count=0;
    while(value){value&=value-1;count++;}
    return count;
  }
  static std::size_t checked(const std::vector<double>& d,double off) {
    const std::size_t N=d.size();
    if(N<4||N>kMaxSites||(N&(N-1))!=0)
      throw std::invalid_argument("radial sites must be power of two 4..256");
    if(!std::isfinite(off)||std::abs(off)>1000.)
      throw std::invalid_argument("non-finite or oversized hopping");
    for(double val:d)
      if(!std::isfinite(val)||std::abs(val)>2000.)
        throw std::invalid_argument("non-finite or oversized H diagonal");
    std::size_t bits=0;
    for(std::size_t dim=N;dim>1;dim>>=1)bits++;
    return bits;
  }
  static std::pair<std::size_t,Complex> act(
       const Term& term,std::size_t qubits,std::size_t ket){
    std::size_t bra=ket;
    Complex phase{1.,0.};
    const Complex I{0.,1.};
    for(std::size_t q=0;q<qubits;q++) {
      const bool bit=((ket>>q)&1U)!=0;
      switch(term.pauli[q]){
        case 'I':break;
        case 'X':bra^=std::size_t{1}<<q;break;
        case 'Y':bra^=std::size_t{1}<<q;phase*=bit?-I:I;break;
        case 'Z':if(bit)phase=-phase;break;
        default:throw std::logic_error("invalid symbolic Pauli");
      }
    }
    return {bra,phase};
  }
  static std::size_t rotation_gates(const Term& t,std::size_t qubits){
    std::size_t nontrivial=0,nX=0,nY=0;
    for(std::size_t q=0;q<qubits;q++){
      if(t.pauli[q]!='I')++nontrivial;
      if(t.pauli[q]=='X')++nX;
      if(t.pauli[q]=='Y')++nY;
    }
    return nontrivial?1+2*(nontrivial-1)+2*nX+6*nY:0;
  }

  template<class Mark>
  static void rotation(ReducedExactTRepresentation& mil,
                       const Term& t,std::size_t qubits,double dt,Mark& mark){
    std::array<std::size_t,kMaxQubits> sites{};
    std::size_t count=0;
    for(std::size_t q=0;q<qubits;q++){
      const char op=t.pauli[q];
      if(op=='I')continue;
      sites[count++]=q;
      if(op=='X'){mil.hadamard(q);mark();}
      else if(op=='Y'){
        for(int k=0;k<3;k++){mil.phase_s(q);mark();}
        mil.hadamard(q);mark();
      }
    }
    if(count==0)return;
    const auto target=sites[count-1];
    for(std::size_t k=0;k+1<count;k++){
      mil.cnot(sites[k],target);mark();
    }
    mil.phase(target,2.*t.weight_mev*dt);mark();
    for(std::size_t k=count-1;k>0;k--){
      mil.cnot(sites[k-1],target);mark();
    }
    for(std::size_t q=qubits;q-->0;){
      if(t.pauli[q]=='X'){mil.hadamard(q);mark();}
      else if(t.pauli[q]=='Y'){
        mil.hadamard(q);mark();mil.phase_s(q);mark();
      }
    }
  }

  static std::vector<Complex> h_action(const std::vector<double>& d,double off,
                                       const std::vector<Complex>& psi){
    std::vector<Complex> out(psi.size());
    for(std::size_t k=0;k<psi.size();k++){
      out[k]=d[k]*psi[k];
      if(k>0)out[k]+=off*psi[k-1];
      if(k+1<psi.size())out[k]+=off*psi[k+1];
    }
    return out;
  }
  static std::vector<Complex> reference_rk4(
      const std::vector<double>& d,double off,std::size_t initial,double tau) {
    std::vector<Complex> state(d.size(),Complex{});
    state[initial]=Complex{1.,0.};
    if(tau==0.)return state;
    double hnorm=2.*std::abs(off);
    for(double v:d)hnorm=std::max(hnorm,std::abs(v)+2.*std::abs(off));
    const double estimated=std::ceil(hnorm*tau/.003);
    if(!std::isfinite(estimated)||estimated>15000)
      throw std::length_error("RK4 reference exceeds integration-step budget");
    const std::size_t steps=std::clamp<std::size_t>(
        static_cast<std::size_t>(estimated),200,15000);
    const double dt=tau/steps;
    const Complex minusI{0.,-1.};
    for(std::size_t s=0;s<steps;s++){
      const auto k1=h_action(d,off,state);
      std::vector<Complex> scratch(state.size());
      for(std::size_t i=0;i<state.size();i++)
        scratch[i]=state[i]+minusI*(.5*dt)*k1[i];
      const auto k2=h_action(d,off,scratch);
      for(std::size_t i=0;i<state.size();i++)
        scratch[i]=state[i]+minusI*(.5*dt)*k2[i];
      const auto k3=h_action(d,off,scratch);
      for(std::size_t i=0;i<state.size();i++)
        scratch[i]=state[i]+minusI*dt*k3[i];
      const auto k4=h_action(d,off,scratch);
      for(std::size_t i=0;i<state.size();i++)
        state[i]+=minusI*(dt/6.)*
                     (k1[i]+2.*k2[i]+2.*k3[i]+k4[i]);
    }
    return state;
  }
};

} // namespace mundo_x
