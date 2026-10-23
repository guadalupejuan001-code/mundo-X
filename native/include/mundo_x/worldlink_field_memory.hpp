#pragma once
#include "mundo_x/quantum_runtime.hpp"
#include <array>
#include <cmath>
#include <cstring>
#include <sstream>
#include <stdexcept>

namespace mundo_x {
// Classical fitted physical rule stored in canonical Inverse Ether Memory.
// No Bell state, physical qubit, or implicit measurement is constructed.
class WorldLinkFieldMemory {
 public:
  void store(const std::array<double,6>& parameters) {
    static_assert(sizeof(double)==8,"IEEE binary64 required");
    for(double x:parameters)if(!std::isfinite(x))throw std::invalid_argument("Nonfinite field rule");
    std::array<unsigned char,48> bytes{};std::memcpy(bytes.data(),parameters.data(),bytes.size());
    std::ostringstream xi;
    for(std::size_t i=0;i<bytes.size();++i)xi<<"mem_store "<<(2000000+i)<<" "<<unsigned(bytes[i])<<"\n";
    runtime_.execute(xi.str()); // Atomic publication of the whole rule; reuses COW pages.
    ready_=true;
  }
  std::array<double,6> load() const {
    if(!ready_)throw std::logic_error("No fitted field rule in Ether");
    std::array<unsigned char,48> bytes{};
    for(std::size_t i=0;i<bytes.size();++i){const auto r=runtime_.memory().read(std::to_string(2000000+i),runtime_.core());
      if(!r.deterministic)throw std::logic_error("Physical rule cannot be a quantum handle");
      bytes[i]=r.value;}
    std::array<double,6> out{};std::memcpy(out.data(),bytes.data(),bytes.size());return out;
  }
  std::size_t active_qubits() const {return runtime_.core().stats().active_qubits;}
  std::size_t overrides() const {return runtime_.memory().stats().overrides;}
 private:
  QuantumRuntime runtime_;
  bool ready_=false;
};
}
