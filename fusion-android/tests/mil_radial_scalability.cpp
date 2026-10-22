#include "mil_radial_scaling.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Scal=mundo_x::MilRadialScaling;
constexpr double hbarc=197.3269804;
constexpr double mass=1125.06;

void require(bool ok,const std::string& message) {
  if(!ok)throw std::runtime_error(message);
}
std::pair<std::vector<double>,double> physical_radial(std::size_t sites) {
  constexpr double L=96.;
  constexpr double rContact=3.2426047442427373;
  const double dr=L/(sites+1);
  const double hopping=-(hbarc*hbarc)/(2.*mass*dr*dr);
  std::vector<double> d(sites);
  for(std::size_t i=0;i<sites;i++) {
    const double r=(i+1)*dr;
    const double cv=r>=rContact?1.4399764/r:
                 1.4399764*(3.-std::pow(r/rContact,2))/(2.*rContact);
    const double depth=-35./(1.+std::exp((r-rContact)/.65));
    d[i]=-2.*hopping+cv+depth;
  }
  return {d,hopping};
}
std::pair<std::vector<double>,double> synthetic(std::size_t N) {
  std::vector<double> d(N);
  for(std::size_t i=0;i<N;i++)
    d[i]=std::sin((i+1)*.39)+std::cos((i+2)*.79)+i*.023;
  return {d,-.72};
}
void all_profiles() {
  for(std::size_t bits=2;bits<=8;bits++) {
    const auto n=std::size_t{1}<<bits;
    auto sample=synthetic(n);
    Scal::Profile profile;
    const auto terms=Scal::decompose(sample.first,sample.second,&profile);
    require(profile.sites==n&&profile.qubits==bits,"qubit/grid mapping");
    require(profile.total_terms==2*n-1,"symbolic term count must be 2N-1");
    require(profile.diagonal_terms==n && profile.hopping_terms==n-1,
            "Z + binary-carry term decomposition");
    require(profile.verification_entries==n*n,"full H matrix audit");
    require(profile.reconstruction_max_error<1e-9,
            "Pauli reconstruction not exact");
    std::cout<<std::setprecision(9)
             <<"MIL_RADIAL_PROFILE_PASS"
             <<" qubits="<<bits<<" sites="<<n
             <<" diagonal_terms="<<profile.diagonal_terms
             <<" hopping_terms="<<profile.hopping_terms
             <<" total_terms="<<profile.total_terms
             <<" gate_estimate_per_step="<<profile.gate_estimate_per_strang_step
             <<" max_entry_error="<<profile.reconstruction_max_error<<"\n";
  }
}
void one_run(std::size_t qubits) {
  const std::size_t N=std::size_t{1}<<qubits;
  const auto [diagonal,off]=physical_radial(N);
  const std::size_t initial=std::min(N-1,std::max<std::size_t>(1,
      static_cast<std::size_t>(std::lround(3.24/(96./(N+1))))));
  const double tau=std::min(.08,.06/std::abs(off));
  const std::size_t steps=qubits<=4?4:qubits<=5?2:1;
  auto begin=std::chrono::steady_clock::now();
  const auto result=Scal::evolve(diagonal,off,initial,tau,steps);
  const double wall=std::chrono::duration<double,std::milli>(
      std::chrono::steady_clock::now()-begin).count();
  require(std::abs(result.norm-1.)<1e-8,"MIL normalized state");
  require(std::abs(result.reference_norm-1.)<1e-8,"independent RK4 normalized");
  require(result.fidelity>.9999,"MIL vs RK4 fidelity below target");
  require(result.probability_l1<.01,"MIL vs RK4 probability L1 too large");
  require(result.gates>0&&result.profile.total_terms>0,"MIL did not run actual gates");
  require(result.peak_components<=Scal::kMaxComponents,"MIL component budget");
  std::cout<<std::setprecision(11)
           <<"MIL_RADIAL_SCALED_RUN_PASS"
           <<" qubits="<<result.profile.qubits
           <<" sites="<<result.profile.sites
           <<" pauli_terms="<<result.profile.total_terms
           <<" steps="<<result.steps
           <<" mil_gates="<<result.gates
           <<" max_components="<<result.peak_components
           <<" time_ms="<<result.time_ms
           <<" wall_ms="<<wall
           <<" fidelity="<<result.fidelity
           <<" l1="<<result.probability_l1
           <<" norm="<<result.norm
           <<" reference_norm="<<result.reference_norm
           <<" tau_inverse_mev="<<tau
           <<"\n";
}
void rejects(){
  auto bad=synthetic(16);
  bool failed=false;
  try{Scal::evolve(std::vector<double>(12,0.),-.1,3,.01,1);}
  catch(const std::exception&){failed=true;}
  require(failed,"reject non power of two");
  failed=false;
  try{Scal::evolve(bad.first,bad.second,3,.01,13);}
  catch(const std::exception&){failed=true;}
  require(failed,"reject steps>12");
  failed=false;
  bad.first[4]=std::numeric_limits<double>::quiet_NaN();
  try{Scal::decompose(bad.first,bad.second);}
  catch(const std::exception&){failed=true;}
  require(failed,"reject NaN");
}
}

int main(int argc,char** argv) {
  try{
    if(argc==1 || (argc==2 && std::string(argv[1])=="profile")) {
      all_profiles();
      rejects();
      std::cout<<"MIL_RADIAL_SYMBOLIC_DECOMPOSITION_PASS max_sites=256 max_qubits=8\n";
      return 0;
    }
    if(argc==3&&std::string(argv[1])=="run") {
      const int n=std::stoi(argv[2]);
      if(n<2||n>8)throw std::invalid_argument("run requires qubits=2..8");
      one_run(static_cast<std::size_t>(n));
      return 0;
    }
    std::cerr<<"Usage: mil_radial_scalability [profile | run 2..8]\n";
    return 2;
  }catch(const std::exception& e){
    std::cerr<<"MIL_RADIAL_SCALING_FAIL: "<<e.what()<<"\n";
    return 1;
  }
}
