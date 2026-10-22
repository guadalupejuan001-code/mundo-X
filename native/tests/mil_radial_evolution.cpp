#include "mundo_x/mil_radial_evolution.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using MIL = mundo_x::MilRadialEvolution;

void require(bool condition,const std::string& message) {
  if(!condition) throw std::runtime_error(message);
}
void near(double x,double y,double tolerance,const std::string& label) {
  if(!std::isfinite(x)||std::abs(x-y)>tolerance)
    throw std::runtime_error(label + " actual=" + std::to_string(x) +
                             " expected=" + std::to_string(y));
}
double sum(const std::vector<double>& a) {
  double n=0.;for(double x:a)n+=x;return n;
}
template<class Callback>
void must_fail(Callback action,const char* name){
  bool rejected=false;
  try{action();}catch(const std::exception&){rejected=true;}
  require(rejected,name);
}
}

int main() {
 try {
   const std::vector<double> h4{-.4,.2,.7,1.1};
   const auto terms=MIL::decompose(h4,-.8);
   require(terms.size()>=5,"nontrivial Y/Y exchange term missing");
   const auto zero=MIL::execute(h4,-.8,1,0.,1);
   near(zero.probabilities[1],1.,1e-13,"zero-time localized state");
   near(zero.probabilities[0],0.,1e-13,"zero-time no hopping");
   near(zero.norm,1.,1e-13,"zero-time MIL norm");

   // Original H, not a single artificial phase gate: locality spreads
   // an initial radial basis state across neighbouring grid sites.
   const auto coarse=MIL::execute(h4,-.8,1,.09,1);
   const auto refined=MIL::execute(h4,-.8,1,.09,12);
   near(refined.norm,1.,3e-10,"MIL remains unitary");
   near(refined.reference_norm,1.,1e-10,"RK4 remains normalized");
   require(refined.probabilities[0]>1e-4,"MIL did not generate radial transport");
   require(refined.probabilities[2]>1e-4,"MIL neighbour transport missing");
   require(refined.fidelity>0.99999,"MIL 4-site versus dense independent RK4");
   require(refined.probability_l1<0.001,"MIL radial occupancy wrong");
   require(refined.fidelity>=coarse.fidelity-1e-7,
           "Strang refinement did not improve fidelity");
   require(refined.mil_gates>120 && refined.peak_components>=1,
           "No real MIL Reduced phase/CNOT gates were executed");
   near(refined.time_seconds,.09*MIL::hbar_mev_s,1e-35,
        "physical time/MeV units");

   const std::vector<double> h8{1.2,-.4,2.6,-1.,.3,1.4,-.8,.2};
   const auto wide=MIL::execute(h8,-2.,3,.06,12);
   require(wide.qubits==3 && wide.sites==8,"three-qubit Hilbert grid");
   near(wide.norm,1.,5e-10,"MIL 8-site norm");
   require(wide.fidelity>0.999,"MIL 8-site fidelity versus independent RK4");
   require(wide.probability_l1<.005,"MIL 8-site probability distance");
   require(wide.probabilities[2]>1e-4 && wide.probabilities[4]>1e-4,
           "MIL 8-site radial spreading");
   require(wide.peak_components <= MIL::max_components,
           "MIL component limit exceeded");
   require(wide.pauli_terms>7,"8-site H Pauli decomposition too small");

   const auto diagonal_only=MIL::execute({-.1,.2,.8,1.0},0.,2,.12,6);
   near(diagonal_only.initial_probability,1.,1e-10,
        "Diagonal Hamiltonian must not change site population");
   near(diagonal_only.fidelity,1.,1e-9,
        "No-hopping reference fidelity");

   must_fail([&]{MIL::execute(std::vector<double>(256,0.),-.5,1,.02,2);},
             "No silent 256-site evolution on a 3-qubit backend");
   must_fail([&]{MIL::execute(h4,-.8,1,.5,1);},"Reject huge time");
   must_fail([&]{MIL::execute(h4,-.8,4,.02,1);},"Reject out of range site");
   must_fail([&]{MIL::execute(h4,-.8,1,.02,25);},"Reject too many gates");
   must_fail([&]{MIL::execute({-.1,.2,std::nan(""),1.},-.8,1,.02,2);},
             "Reject NaN Hamiltonian");
   std::cout << "MIL_RADIAL_EVOLUTION_PASS"
             << " qubits="<<wide.qubits<<" sites="<<wide.sites
             << " time_s="<<wide.time_seconds
             << " pauli_terms="<<wide.pauli_terms
             << " mil_gates="<<wide.mil_gates
             << " components="<<wide.peak_components
             << " fidelity="<<wide.fidelity
             << " l1="<<wide.probability_l1
             << " norm="<<wide.norm
             << " dense_oracle_independent=RK4\n";
   return 0;
 }catch(const std::exception& error){
   std::cerr<<"MIL_RADIAL_EVOLUTION_FAIL: "<<error.what()<<"\n";
   return 1;
 }
}
