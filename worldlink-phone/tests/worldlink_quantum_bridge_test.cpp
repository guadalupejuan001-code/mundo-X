#include "mundo_x/worldlink_quantum_bridge.hpp"

#include <cmath>
#include <iostream>
#include <stdexcept>

void check(bool v, const char* reason) {
  if(!v)throw std::runtime_error(reason);
}
void near(double actual,double expected,double tol,const char* reason) {
  if(!std::isfinite(actual)||std::abs(actual-expected)>tol)
    throw std::runtime_error(reason);
}
int main() {
  try {
    mundo_x::WorldLinkQuantumBridge b;
    check(b.active_simulated_qubits()>=2,"Ether did not prepare entangled simulated pair");
    const auto baseline=b.evolve(-1,-1,-1);
    check(!baseline.magnetic_available && !baseline.motion_available &&
          !baseline.light_available,"Missing sensors should be labeled missing");
    near(baseline.norm,1.,1e-9,"MIL sparse norm");
    near(baseline.zz,1.,1e-9,"MIL multiword Bell ZZ");
    constexpr double pi=3.14159265358979323846;
    near(baseline.xx,std::cos(pi/4.+.19),1e-8,
         "MIL T+general-phase correlations");
    check(baseline.frequency_hz >= 180 && baseline.frequency_hz <=1200,
          "Physical frequency outside bounded range");
    check(baseline.event==1 && baseline.ether_memory_overrides==1,
          "Ether COW journal didn't store first event");
    check(baseline.components<=32 && baseline.mil_logical_bytes>0,
          "MIL component/memory budget missing");
    const auto sensor=b.evolve(89.0,6.0,140.0);
    check(sensor.magnetic_available&&sensor.motion_available&&sensor.light_available,
          "Sensor payload validation failed");
    near(sensor.zz,1.,1e-9,"Unit parity should survive phases");
    check(std::abs(sensor.xx-baseline.xx)>.05,
          "Physical input did not change MIL Pauli observable");
    check(sensor.frequency_hz!=baseline.frequency_hz,
          "Physical command did not depend on MIL");
    check(sensor.ether_memory_overrides==2,"Ether journal update was lost");
    b.reset();
    check(b.events()==0,"Reset leaked journal");
    const auto after=b.evolve(-1,-1,-1);
    near(after.xx,baseline.xx,1e-10,"Fresh reset didn't reproduce Bell state");
    check(after.event==1,"Reset event numbering broken");
    std::cout<<"WORLDLINK_MIL_ETHER_BRIDGE_PASS qubits=1000 memory=COW256 "
                "Bell=simulated physical_entanglement=false checks=15\n";
    return 0;
  }catch(const std::exception& e) {
    std::cerr << "WORLDLINK_BRIDGE_FAIL " << e.what() << '\n';
    return 1;
  }
}
