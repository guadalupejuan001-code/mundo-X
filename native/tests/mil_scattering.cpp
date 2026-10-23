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

using C = MIL::Complex;
// Independent RK4 integration of the ORIGINAL two-particle sparse Hamiltonian.
// No FFT, no Suzuki splitting and no call to the MIL propagator in this oracle.
std::vector<C> sparse_derivative(const std::vector<C>& psi, std::size_t n,
                                  double hopping, double contact) {
  std::vector<C> output(n * n);
  for (std::size_t x = 0; x < n; ++x)
    for (std::size_t y = 0; y < n; ++y) {
      const auto at = [&](std::size_t a, std::size_t b) { return psi[a * n + b]; };
      const C kinetic = -hopping *
          (at((x + 1) % n, y) + at((x + n - 1) % n, y) +
           at(x, (y + 1) % n) + at(x, (y + n - 1) % n));
      const C interaction = x == y ? contact * at(x, y) : C(0.0, 0.0);
      output[x * n + y] = C(0.0, -1.0) * (kinetic + interaction);
    }
  return output;
}
std::vector<C> add_scaled(const std::vector<C>& psi,
                          const std::vector<C>& k, double multiplier) {
  auto result = psi;
  for (std::size_t i = 0; i < result.size(); ++i) result[i] += multiplier * k[i];
  return result;
}
std::vector<C> independent_rk4(std::vector<C> state, std::size_t n,
                                double hopping, double contact,
                                double dt, std::size_t steps) {
  for (std::size_t t = 0; t < steps; ++t) {
    const auto k1 = sparse_derivative(state, n, hopping, contact);
    const auto k2 = sparse_derivative(add_scaled(state, k1, dt / 2.0), n, hopping, contact);
    const auto k3 = sparse_derivative(add_scaled(state, k2, dt / 2.0), n, hopping, contact);
    const auto k4 = sparse_derivative(add_scaled(state, k3, dt), n, hopping, contact);
    for (std::size_t i = 0; i < state.size(); ++i)
      state[i] += dt * (k1[i] + 2.0 * k2[i] + 2.0 * k3[i] + k4[i]) / 6.0;
  }
  return state;
}
double amplitude_l2(const std::vector<C>& a, const std::vector<C>& b) {
  require(a.size() == b.size(), "oracle dimensions");
  double squared = 0.0;
  for (std::size_t i = 0; i < a.size(); ++i) squared += std::norm(a[i] - b[i]);
  return std::sqrt(squared);
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
    require(free.factorized_free && !interacting.factorized_free,
            "use factorization only for known free product");
    auto general_free = MIL::initial_state(input.sites);
    MIL::propagate(general_free, input, input.dt);
    double free_equivalence = 0.0;
    for (std::size_t i = 0; i < general_free.size(); ++i)
      free_equivalence = std::max(free_equivalence,
          std::abs(general_free[i] - free.amplitudes[i]));
    require(free_equivalence < 1e-10,
            "factorized free state disagrees with full two-body FFT");
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

    // Test physical evolution, not just self-consistency of the FFT.
    // Solve the original H independently with 4000 RK4 substeps to time 0.4.
    const auto reference = independent_rk4(MIL::initial_state(8),
                                            8, 1.0, 4.0, 0.0001, 4000);
    MIL::Input oracle_cfg;
    oracle_cfg.sites = 8;
    oracle_cfg.contact = 4.0;
    oracle_cfg.hopping = 1.0;
    oracle_cfg.steps = 4;
    oracle_cfg.dt = 0.1;
    auto coarse = MIL::initial_state(8);
    MIL::propagate(coarse, oracle_cfg, oracle_cfg.dt);
    oracle_cfg.steps = 40;
    oracle_cfg.dt = 0.01;
    auto refined = MIL::initial_state(8);
    MIL::propagate(refined, oracle_cfg, oracle_cfg.dt);
    const double coarse_l2 = amplitude_l2(coarse, reference);
    const double refined_l2 = amplitude_l2(refined, reference);
    require(refined_l2 < 0.0002, "split-operator disagrees with independent RK4");
    require(refined_l2 < coarse_l2 / 20.0, "time-step refinement did not converge");

    input.sites = 128;
    input.steps = 50;
    input.contact = 8.0;
    const auto large = MIL::run(input);
    require(std::abs(large.norm - 1.0) < 2e-11, "128-site two-particle norm");
    require(large.state_bytes == 128 * 128 * sizeof(MIL::Complex),
            "128-site state memory");

    // O(S*N*log N) free evolution: 1024 sites is practical under the same
    // budget even though 100 steps of interacting N*N FFT are not.
    input.sites = 1024;
    input.steps = 100;
    input.contact = 0.0;
    const auto large_free = MIL::run(input);
    require(large_free.factorized_free && large_free.state_bytes == 16 * 1024 * 1024,
            "1024-site free run and full final state");
    require(std::abs(large_free.norm - 1.0) < 2e-10,
            "large free state norm");
    require(large_free.estimated_work ==
                MIL::estimate_work(1024, 100, true) &&
            large_free.estimated_work < MIL::default_work_budget,
            "factorized work budget must match actual path");

    // Arbitrary pre-entangled input must remain in the full 2D propagation
    // path even when the interaction U=0.
    input.sites = 8;
    input.steps = 16;
    std::vector<MIL::Complex> entangled(8 * 8);
    const double bell = 1.0 / std::sqrt(2.0);
    entangled[0] = {bell, 0.0};
    entangled[9] = {bell, 0.0};
    MIL::propagate(entangled, input, input.dt);
    require(std::abs(MIL::reduced_purity(entangled, 8) - 0.5) < 1e-10,
            "free propagation must preserve preexisting entanglement");

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
              << " rk4_l2_coarse=" << coarse_l2
              << " rk4_l2_refined=" << refined_l2
              << " interacting_purity=" << interacting_purity
              << " free_equivalence=" << free_equivalence
              << " free_purity=" << free_purity
              << " reverse_error=" << max_reverse_error
              << " state_bytes_128=" << large.state_bytes << "\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "MIL_SCATTER_FAIL: " << e.what() << "\n";
    return 1;
  }
}
