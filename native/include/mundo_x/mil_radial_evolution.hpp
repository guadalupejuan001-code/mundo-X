#pragma once

#include "mundo_x/exact_t_experiment.hpp"

#include <algorithm>
#include <array>
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
 * Limited REAL quantum evolution of an explicitly supplied radial Hamiltonian:
 *
 *     H_jj = diagonal[j],   H_j,j+1 = H_j+1,j = hopping
 *     |psi(tau)> = exp(-i tau H)|initial_grid_site>.
 *
 * tau = physical time / hbar in MeV^(-1). For H in MeV,
 * t_seconds = tau * 6.582119569e-22 s.
 *
 * The quantum path executes actual MIL ReducedExactTRepresentation gates:
 * a Pauli decomposition, basis changes, CNOT parity ladders, continuously
 * parametrized phase gates, and second-order Suzuki/Strang product formula.
 *
 * A SMALL independent dense RK4 oracle integrates the ORIGINAL sparse H,
 * NOT the Pauli circuit. It is validation only and is never used to compute
 * the MIL state. The 4/8-site runs are deliberately bounded: this does not
 * simulate the 256-site research Hamiltonian, physical nuclear many-body
 * dynamics, fusion rates, or 1000 active radial qubits.
 */
class MilRadialEvolution {
 public:
  using Complex = std::complex<double>;
  static constexpr double hbar_mev_s = 6.582119569e-22;
  static constexpr std::size_t max_components = 1024;

  struct Term {
    std::array<char,3> axes{{'I','I','I'}};
    double weight_mev = 0.;
  };

  struct Result {
    std::size_t qubits=0,sites=0,initial_site=0,trotter_steps=0;
    std::size_t pauli_terms=0,mil_gates=0,peak_components=0;
    double tau_inverse_mev=0.,time_seconds=0.,norm=0.;
    double reference_norm=0., fidelity=0.,probability_l1=0.;
    double initial_probability=0.;
    std::vector<double> probabilities;
    std::vector<double> reference_probabilities;
  };

  static std::vector<Term> decompose(const std::vector<double>& diagonal,
                                    double hopping_mev) {
    const auto qubits=checked_qubits(diagonal,hopping_mev);
    const std::size_t dim=diagonal.size();
    std::vector<Term> terms;
    for(std::size_t index=0;index<(std::size_t{1}<<(2*qubits));++index){
      auto mask=index;
      Term term;
      for(std::size_t q=0;q<qubits;q++){
        term.axes[q]="IXYZ"[mask&3U];
        mask >>= 2;
      }
      Complex trace{};
      for(std::size_t ket=0;ket<dim;ket++){
        const auto [bra,amp]=act_pauli(term,qubits,ket);
        const double hij=bra==ket ? diagonal[ket] :
            (bra+1==ket || ket+1==bra ? hopping_mev : 0.);
        trace += amp*hij;
      }
      trace/=static_cast<double>(dim);
      if(std::abs(trace.imag())>1e-9)
        throw std::runtime_error("non-Hermitian Hamiltonian Pauli coefficient");
      if(std::abs(trace.real())>1e-12){
        term.weight_mev=trace.real();
        terms.push_back(term);
      }
    }
    // Verify the EXACT operator, including the discarded identity shift.
    // No hidden Pauli truncation is allowed beyond negligible FP zeros.
    for(std::size_t ket=0;ket<dim;ket++){
      for(std::size_t bra=0;bra<dim;bra++){
        Complex reconstructed{};
        for(const auto& t:terms){
          const auto [target,amplitude]=act_pauli(t,qubits,ket);
          if(target==bra)reconstructed+=amplitude*t.weight_mev;
        }
        double expected=bra==ket?diagonal[ket]:
            (bra+1==ket||ket+1==bra?hopping_mev:0.);
        if(std::abs(reconstructed-Complex{expected,0.})>
           1e-9*std::max(1.,std::abs(expected)))
          throw std::runtime_error("Pauli decomposition failed matrix reconstruction");
      }
    }
    return terms;
  }

  static Result execute(const std::vector<double>& diagonal,
                        double hopping_mev,
                        std::size_t initial_site,double tau_inverse_mev,
                        std::size_t trotter_steps) {
    const auto qubits=checked_qubits(diagonal,hopping_mev);
    const auto sites=diagonal.size();
    if(initial_site>=sites)throw std::out_of_range("initial radial grid site");
    if(!std::isfinite(tau_inverse_mev) || tau_inverse_mev<0. ||
       tau_inverse_mev>.12)
      throw std::invalid_argument("MIL evolution tau must be 0..0.12 MeV^-1");
    if(trotter_steps<1||trotter_steps>24)
      throw std::invalid_argument("MIL evolution requires 1..24 Strang steps");

    const auto terms=decompose(diagonal,hopping_mev);
    ReducedExactTRepresentation quantum(qubits,max_components);
    std::size_t gates=0, peak=1;
    auto observe=[&] {
      peak=std::max(peak,quantum.component_count());
      if(peak>max_components)
        throw std::length_error("MIL component budget exceeded");
      if(++gates>15000)
        throw std::length_error("MIL evolution gate budget exceeded");
    };
    for(std::size_t q=0;q<qubits;q++){
      if((initial_site>>q)&1U){ quantum.pauli_x(q);observe(); }
    }

    // Drop the identity from the gate circuit: it changes only the global
    // phase and cannot affect populations or phase-invariant fidelity.
    std::vector<Term> active;
    for(const auto& t:terms){
      bool identity=true;
      for(std::size_t q=0;q<qubits;q++)if(t.axes[q]!='I')identity=false;
      if(!identity)active.push_back(t);
    }
    const double half_tau=tau_inverse_mev/(2.*trotter_steps);
    for(std::size_t step=0;step<trotter_steps;step++){
      for(const auto& term:active)
        rotate_pauli(quantum,term,qubits,half_tau,observe);
      for(auto it=active.rbegin();it!=active.rend();++it)
        rotate_pauli(quantum,*it,qubits,half_tau,observe);
    }

    // The only dense export is a bounded 4/8-amplitude READ for validation.
    // MIL Reduced executes the quantum gates above independently of RK4.
    const auto amplitudes=quantum.dense_from_reduced();
    const auto reference=reference_rk4(diagonal,hopping_mev,initial_site,tau_inverse_mev);
    Result result;
    result.qubits=qubits;result.sites=sites;
    result.initial_site=initial_site;result.trotter_steps=trotter_steps;
    result.pauli_terms=terms.size();result.mil_gates=gates;
    result.peak_components=peak;
    result.tau_inverse_mev=tau_inverse_mev;
    result.time_seconds=tau_inverse_mev*hbar_mev_s;
    result.probabilities.resize(sites);
    result.reference_probabilities.resize(sites);
    Complex overlap{};
    for(std::size_t k=0;k<sites;k++){
      auto p=std::norm(amplitudes.at(k));
      auto r=std::norm(reference.at(k));
      result.probabilities[k]=p;
      result.reference_probabilities[k]=r;
      result.norm+=p;result.reference_norm+=r;
      result.probability_l1+=std::abs(p-r);
      overlap+=std::conj(reference[k])*amplitudes[k];
    }
    if(!(result.norm>0.)||!(result.reference_norm>0.) ||
       !std::isfinite(result.norm)||!std::isfinite(result.reference_norm))
      throw std::runtime_error("MIL or reference evolution has invalid norm");
    result.fidelity=std::norm(overlap)/(result.norm*result.reference_norm);
    result.initial_probability=result.probabilities.at(initial_site);
    if(!std::isfinite(result.fidelity)||result.fidelity<0.||
       result.fidelity>1.+1e-8)
      throw std::runtime_error("invalid MIL/reference fidelity");
    return result;
  }

 private:
  static std::size_t checked_qubits(const std::vector<double>& diagonal,double hopping) {
    if(diagonal.size()!=4&&diagonal.size()!=8)
      throw std::invalid_argument("MIL radial demonstration only supports 4 or 8 sites");
    if(!std::isfinite(hopping)||std::abs(hopping)>500.)
      throw std::invalid_argument("non-finite or oversized radial hopping");
    for(double value:diagonal)
      if(!std::isfinite(value)||std::abs(value)>500.)
        throw std::invalid_argument("non-finite or oversized radial diagonal");
    return diagonal.size()==4?2U:3U;
  }

  static std::pair<std::size_t,Complex>
  act_pauli(const Term& term,std::size_t qubits,std::size_t ket) {
    std::size_t bra=ket;
    Complex phase{1.,0.};
    const Complex I{0.,1.};
    for(std::size_t q=0;q<qubits;q++){
      const bool bit=((ket>>q)&1U)!=0;
      switch(term.axes[q]){
        case 'I':break;
        case 'X':bra^=(std::size_t{1}<<q);break;
        case 'Y':bra^=(std::size_t{1}<<q);phase*=bit?-I:I;break;
        case 'Z':if(bit)phase=-phase;break;
        default:throw std::logic_error("unrecognized Pauli axis");
      }
    }
    return {bra,phase};
  }

  template<class Counter>
  static void rotate_pauli(ReducedExactTRepresentation& quantum,const Term& term,
                           std::size_t qubits,double tau,Counter& count) {
    std::array<std::size_t,3> active{};
    std::size_t countActive=0;
    for(std::size_t q=0;q<qubits;q++){
      const char p=term.axes[q];
      if(p=='I')continue;
      active[countActive++]=q;
      if(p=='X') {quantum.hadamard(q);count();}
      else if(p=='Y'){
        // R=H S† rotates Y into Z; S†=S³.
        for(unsigned i=0;i<3;i++){quantum.phase_s(q);count();}
        quantum.hadamard(q);count();
      }
    }
    if(countActive==0)return;
    const std::size_t target=active[countActive-1];
    for(std::size_t i=0;i+1<countActive;i++){
      quantum.cnot(active[i],target);count();
    }
    // phase(theta) = exp(+i theta/2) exp(-i theta Z/2).
    // Hence theta = 2 * c * tau realizes exp(-i c tau P),
    // apart from a physically irrelevant global phase.
    quantum.phase(target,2.*term.weight_mev*tau);count();
    for(std::size_t i=countActive-1;i>0;i--){
      quantum.cnot(active[i-1],target);count();
    }
    for(std::size_t q=qubits;q-->0;){
      const char p=term.axes[q];
      if(p=='X'){quantum.hadamard(q);count();}
      else if(p=='Y'){
        quantum.hadamard(q);count();
        quantum.phase_s(q);count();
      }
    }
  }

  static std::vector<Complex> h_action(const std::vector<double>& diagonal,
                                       double off,
                                       const std::vector<Complex>& v) {
    std::vector<Complex> y(v.size());
    for(std::size_t i=0;i<v.size();i++){
      y[i]=diagonal[i]*v[i];
      if(i>0)y[i]+=off*v[i-1];
      if(i+1<v.size())y[i]+=off*v[i+1];
    }
    return y;
  }

  /** RK4 on the sparse ORIGINAL H, independent of MIL Pauli gates. */
  static std::vector<Complex> reference_rk4(const std::vector<double>& diagonal,
                                             double off,std::size_t initial,
                                             double tau) {
    std::vector<Complex> v(diagonal.size(),Complex{0.,0.});
    v[initial]={1.,0.};
    if(tau==0.)return v;
    double scale=2.*std::abs(off);
    for(double d:diagonal)scale=std::max(scale,std::abs(d)+2.*std::abs(off));
    const std::size_t steps=std::clamp<std::size_t>(
        static_cast<std::size_t>(std::ceil(scale*tau/.003)),160,12000);
    const double dt=tau/steps;
    const Complex minus_i{0.,-1.};
    for(std::size_t step=0;step<steps;step++){
      const auto k1=h_action(diagonal,off,v);
      std::vector<Complex> work(v.size());
      for(std::size_t i=0;i<v.size();i++)work[i]=v[i]+(minus_i*dt*.5)*k1[i];
      const auto k2=h_action(diagonal,off,work);
      for(std::size_t i=0;i<v.size();i++)work[i]=v[i]+(minus_i*dt*.5)*k2[i];
      const auto k3=h_action(diagonal,off,work);
      for(std::size_t i=0;i<v.size();i++)work[i]=v[i]+(minus_i*dt)*k3[i];
      const auto k4=h_action(diagonal,off,work);
      for(std::size_t i=0;i<v.size();i++){
        v[i]+=minus_i*(dt/6.)*(k1[i]+2.*k2[i]+2.*k3[i]+k4[i]);
      }
    }
    return v;
  }
};

} // namespace mundo_x
