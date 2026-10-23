#include "mundo_x/mil_scattering.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using MIL = mundo_x::MilScattering;
void require(bool ok, const char* label) {
  if (!ok) throw std::runtime_error(label);
}
template <typename Fn>
void rejects(Fn fn, const char* label) {
  bool rejected = false;
  try { fn(); } catch (const std::exception&) { rejected = true; }
  require(rejected, label);
}
}  // namespace

int main() {
  try {
    MIL::Input input;
    input.sites = 32;
    input.steps = 80;
    input.hopping = 1.0;
    input.contact = 5.0;
    const auto interacting = MIL::run(input);
    require(std::abs(interacting.norm - 1.0) < 2e-11, "contact-interacting norm");
    require(interacting.max_norm_error < 2e-11, "intermediate norm");
    require(interacting.peak_contact > 3.0 * interacting.initial_contact,
            "two packets did not meet");
    require(interacting.estimated_work == MIL::estimate_work(32, 80),
            "deterministic work accounting");
    require(interacting.state_bytes == 32 * 32 * sizeof(MIL::Complex),
            "amplitude memory accounting");

    const auto initial = MIL::initial_state(input.sites);
    auto reversed = interacting.amplitudes;
    MIL::propagate(reversed, input, -input.dt);
    double max_reverse_error = 0.0;
    for (std::size_t i = 0; i < initial.size(); ++i)
      max_reverse_error = std::max(max_reverse_error, std::abs(initial[i] - reversed[i]));
    require(max_reverse_error < 2e-11, "time reversal failed");

    input.contact = 0.0;
    const auto free = MIL::run(input);
    require(std::abs(free.norm - 1.0) < 2e-11, "free propagator norm");
    const double difference = MIL::probability_l1(interacting, free);
    require(difference > 0.005, "contact coupling must affect collision statistics");
    const double interacting_purity = MIL::reduced_purity(interacting.amplitudes, 32);
    const double free_purity = MIL::reduced_purity(free.amplitudes, 32);
    require(interacting_purity < 0.95,
            "contact collision should entangle initially independent particles");
    require(std::abs(free_purity - 1.0) < 1e-10,
            "noninteracting product should remain separable");

    const auto repeat = MIL::run(input);
    require(MIL::probability_l1(free, repeat) == 0.0, "deterministic re-execution");

    input.sites = 128;
    input.steps = 50;
    input.contact = 8.0;
    const auto large = MIL::run(input);
    require(std::abs(large.norm - 1.0) < 2e-11, "128-site two-particle norm");
    require(large.state_bytes == 128 * 128 * sizeof(MIL::Complex),
            "128-site state memory");

    // Prevent accidental quadratic state blow-ups and malformed inputs.
    input.sites = 1000;
    rejects([&] { MIL::run(input); }, "non-power-of-two lattice accepted");
    input.sites = 2048;
    rejects([&] { MIL::run(input); }, "out-of-budget lattice accepted");
    input.sites = 1024;
    input.steps = 2048;
    rejects([&] { MIL::run(input); }, "unbounded FFT work accepted");
    input.sites = 16;
    input.steps = 10;
    input.contact = std::nan("");
    rejects([&] { MIL::run(input); }, "non-finite interaction accepted");
    input.contact = 4.0;
    input.hopping = 0.0;
    rejects([&] { MIL::run(input); }, "zero kinetic coupling accepted");
    input.hopping = 1.0;
    input.work_budget = 1;
    rejects([&] { MIL::run(input); }, "operation budget ignored");

    std::cout << "MIL_SCATTER_PASS"
              << " small_norm=" << interacting.norm
              << " large_norm=" << large.norm
              << " initial_contact=" << interacting.initial_contact
              << " peak_contact=" << interacting.peak_contact
              << " interacting_vs_free_l1=" << difference
              << " interacting_purity=" << interacting_purity
              << " free_purity=" << free_purity
              << " reverse_error=" << max_reverse_error
              << " state_bytes_128=" << large.state_bytes << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "MIL_SCATTER_FAIL: " << e.what() << "\n";
    return 1;
  }
}
