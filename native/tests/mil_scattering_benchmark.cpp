#include "mundo_x/mil_scattering.hpp"
#include <chrono>
#include <iomanip>
#include <iostream>

int main() {
  using MIL = mundo_x::MilScattering;
  std::cout << "sites,steps,elapsed_ms,state_bytes,norm,peak_contact\n";
  for (const std::size_t sites : {32U, 64U, 128U, 256U, 512U}) {
    MIL::Input cfg;
    cfg.sites = sites;
    cfg.steps = 30;
    cfg.hopping = 1.0;
    cfg.contact = 5.0;
    const auto start = std::chrono::steady_clock::now();
    const auto result = MIL::run(cfg);
    const auto elapsed = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - start).count();
    std::cout << sites << ',' << cfg.steps << ','
              << std::setprecision(12) << elapsed << ','
              << result.state_bytes << ','
              << std::setprecision(17) << result.norm << ','
              << result.peak_contact << '\n';
  }
}
