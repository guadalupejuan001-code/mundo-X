#include "mundo_x/factorized_core.hpp"

#include <chrono>
#include <cmath>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <string>
#include <sstream>
#include <vector>

// Same source is compiled against both headers by examine_factorized_structure.py.
// These circuits have analytic product-state answers; this is not a claim about
// generic entangled circuits or the performance of other simulators.
int main(int argc, char** argv) {
  if (argc != 3) return 2;
  const std::string mode = argv[1];
  if (mode != "fanout" && mode != "gauge") return 2;
  const auto n = static_cast<std::size_t>(std::stoull(argv[2]));
  if (n < 2) return 2;
  using Core = mundo_x::FactorizedCore;
  Core core(n, mode == "gauge" ? 2 : 20);
  for (std::size_t q = 0; q < n; ++q) core.hadamard(q);
  if (mode == "fanout") core.phase_t(0); else core.phase_s(0);
  std::size_t completed = 0, failed_partner = 0;
  std::size_t peak_payload = core.logical_amplitude_bytes();
  double maximum_probability_error = 0.0;
  std::string error;
  const auto start = std::chrono::steady_clock::now();
  try {
    for (std::size_t q = 1; q < n; ++q) {
      failed_partner = q;
      if (mode == "fanout") {
        core.cnot(0, q);
      } else {
        core.controlled_z(0, q);
        peak_payload = std::max(peak_payload, core.logical_amplitude_bytes());
        core.controlled_z(0, q);
        if (!core.try_factorize(0)) throw std::runtime_error("strict split rejected");
        maximum_probability_error = std::max(maximum_probability_error,
            std::abs(core.probability_one(0) - 0.5));
      }
      peak_payload = std::max(peak_payload, core.logical_amplitude_bytes());
      ++completed;
    }
    failed_partner = 0;
  } catch (const std::exception& exception) { error = exception.what(); }
  const double evolution_ms = std::chrono::duration<double, std::milli>(
      std::chrono::steady_clock::now() - start).count();
  double log_error = 0.0, phase_error = 0.0;
  if (error.empty()) {
    for (std::size_t q = 0; q < n; ++q)
      maximum_probability_error = std::max(maximum_probability_error,
          std::abs(core.probability_one(q) - 0.5));
    std::vector<bool> bits(n, false);
    bits[0] = true;
    const auto value = core.log_amplitude(bits);
    log_error = std::abs(value.log_magnitude + n * std::log(2.0) / 2);
    phase_error = std::abs(value.phase - std::acos(-1.0) / (mode == "fanout" ? 4 : 2));
    if (value.zero || log_error > 1e-9 || phase_error > 1e-12 ||
        maximum_probability_error > 1e-12) error = "analytic validation failed";
  }
  // /proc resets at exec; getrusage can include an inherited pre-exec peak.
  std::size_t peak_rss_bytes = 0;
  std::ifstream status("/proc/self/status");
  for (std::string line; std::getline(status, line);) {
    if (line.compare(0, 6, "VmHWM:") == 0) {
      std::istringstream field(line.substr(6));
      field >> peak_rss_bytes;
      peak_rss_bytes *= 1024;
      break;
    }
  }
  std::cout << std::setprecision(17)
            << "{\"mode\":\"" << mode << "\",\"qubits\":" << n
            << ",\"completed_partners\":" << completed
            << ",\"failed_partner\":" << failed_partner
            << ",\"success\":" << (error.empty() ? "true" : "false")
            << ",\"error\":\"" << error << '"'
            << ",\"factor_count\":" << core.factor_count()
            << ",\"largest_factor_qubits\":" << core.largest_factor_qubits()
            << ",\"logical_amplitude_bytes\":" << core.logical_amplitude_bytes()
            << ",\"peak_logical_amplitude_bytes\":" << peak_payload
            << ",\"estimated_owned_bytes\":" << core.storage_estimate().estimated_reserved_bytes
            << ",\"peak_rss_bytes\":" << peak_rss_bytes
            << ",\"evolution_ms\":" << evolution_ms
            << ",\"max_probability_error\":" << maximum_probability_error
            << ",\"log_amplitude_error\":";
  if (error.empty()) std::cout << log_error; else std::cout << "null";
  std::cout << ",\"phase_error\":";
  if (error.empty()) std::cout << phase_error; else std::cout << "null";
  std::cout << "}\n";
  return error.empty() ? 0 : 1;
}
