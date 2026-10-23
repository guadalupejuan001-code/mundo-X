#include "mundo_x/worldlink_field_memory.hpp"
#include <cassert>
#include <iostream>
#include <limits>

int main(){
  mundo_x::WorldLinkFieldMemory memory;
  bool rejected=false;try{memory.load();}catch(const std::logic_error&){rejected=true;}
  assert(rejected);
  const std::array<double,6> a{21,-8,42,4,-3,2};memory.store(a);assert(memory.load()==a);
  assert(memory.active_qubits()==0);assert(memory.overrides()<=48);
  auto invalid=a;invalid[2]=std::numeric_limits<double>::quiet_NaN();rejected=false;
  try{memory.store(invalid);}catch(const std::invalid_argument&){rejected=true;}
  assert(rejected);assert(memory.load()==a);
  const std::array<double,6> b{0,0,0,0,0,0};memory.store(b);assert(memory.load()==b);assert(memory.overrides()==0);
  std::cout<<"PASS canonical Ether field rule: exact bytes, rollback, zero elision, zero active qubits\n";
}
