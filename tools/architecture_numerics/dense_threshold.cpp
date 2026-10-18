#include <cmath>
#include <iomanip>
#include <iostream>
#include "mundo_x/dense_core.hpp"

int main() {
  mundo_x::DenseCore state(1);
  const double tolerance=3e-5;
  double max_before=0;
  std::cout << std::setprecision(17);
  for(std::size_t step=1;step<=100000;++step) {
    if(step%2) state.hadamard(0);else state.phase_t(0);
    double norm=0;
    for(auto z:state.state()) norm+=double(z.real())*z.real()+double(z.imag())*z.imag();
    const double error=std::abs(norm-1);
    if(error>tolerance) {
      std::cout << "{\"first_gate\":" << step << ",\"squared_norm\":" << norm
                << ",\"norm_error\":" << error << ",\"max_prior_norm_error\":" << max_before
                << ",\"tolerance\":" << tolerance << ",\"state\":[";
      for(std::size_t i=0;i<state.state().size();++i) {
        if(i) std::cout << ',';
        auto z=state.state()[i];std::cout << '[' << z.real() << ',' << z.imag() << ']';
      }
      std::cout << "]}\n";return 0;
    }
    max_before=std::max(max_before,error);
  }
  return 1;
}
