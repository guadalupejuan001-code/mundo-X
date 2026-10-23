#pragma once

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <stdexcept>
#include <vector>

namespace mundo_x {

// Two distinguishable quantum particles on a periodic 1-D lattice, evolved
// coherently by a symmetric split-operator FFT. H = -J(shift_1 + shift_2 + h.c.)
// + U sum_x |x,x><x,x|, with hbar=1. This is NOT a QCD/hadron collision model
// and does not invoke the ReducedExactTRepresentation circuit backend.
class MilScattering {
 public:
  using Complex = std::complex<double>;
  static constexpr double pi = 3.14159265358979323846264338327950288;
  static constexpr std::size_t max_sites = 1024;
  static constexpr std::size_t max_steps = 2048;
  static constexpr std::uint64_t default_work_budget = 300000000ULL;

  struct Input {
    std::size_t sites = 32;
    std::size_t steps = 80;
    double hopping = 1.0;
    double contact = 4.0;
    double dt = 0.1;
    std::uint64_t work_budget = default_work_budget;
  };
  struct Result {
    std::size_t sites = 0, steps = 0;
    std::uint64_t estimated_work = 0;
    std::size_t state_bytes = 0, scratch_bytes = 0;
    double norm = 0, max_norm_error = 0;
    double initial_contact = 0, peak_contact = 0, final_contact = 0;
    double exchanged_order = 0;
    std::vector<Complex> amplitudes;
  };

  static std::uint64_t estimate_work(std::size_t n, std::size_t steps) {
    if (n < 8 || n > max_sites || (n & (n - 1)) != 0 ||
        steps == 0 || steps > max_steps)
      throw std::invalid_argument("scatter requires power-of-two sites 8..1024 and steps 1..2048");
    std::size_t bits = 0;
    for (std::size_t k = n; k > 1; k >>= 1) ++bits;
    return static_cast<std::uint64_t>(n) * n * (2 * bits + 3) * steps;
  }

  static std::vector<Complex> initial_state(std::size_t n) {
    estimate_work(n, 1);
    std::vector<Complex> psi(n * n);
    const double displacement = std::min(n / 4.0, 10.0);
    const double left = n / 2.0 - displacement;
    const double right = n / 2.0 + displacement;
    const double sigma = std::max(1.4, std::min(4.0, n / 32.0));
    const double inv = 1.0 / (4 * sigma * sigma);
    const double momentum = pi / 3.0;
    double square_norm = 0.0;
    for (std::size_t x = 0; x < n; ++x) {
      const double dx = periodic_offset(static_cast<double>(x) - left, n);
      for (std::size_t y = 0; y < n; ++y) {
        const double dy = periodic_offset(static_cast<double>(y) - right, n);
        psi[x * n + y] = std::polar(std::exp(-(dx * dx + dy * dy) * inv),
                                    momentum * (dx - dy));
        square_norm += std::norm(psi[x * n + y]);
      }
    }
    for (auto& amplitude : psi) amplitude /= std::sqrt(square_norm);
    return psi;
  }

  // Signed dt enables an independent forward/backward unitarity check.
  static void propagate(std::vector<Complex>& psi, const Input& cfg, double signed_dt) {
    validate(cfg);
    if (psi.size() != cfg.sites * cfg.sites || !std::isfinite(signed_dt) ||
        std::abs(signed_dt) > 0.25)
      throw std::invalid_argument("invalid scatter amplitudes or time step");
    Workspace work(cfg.sites, cfg.hopping, cfg.contact, signed_dt);
    for (std::size_t step = 0; step < cfg.steps; ++step) work.step(psi);
  }

  static Result run(const Input& cfg) {
    const auto budget = validate(cfg);
    Result result;
    result.sites = cfg.sites;
    result.steps = cfg.steps;
    result.estimated_work = budget;
    result.amplitudes = initial_state(cfg.sites);
    result.state_bytes = result.amplitudes.size() * sizeof(Complex);
    result.scratch_bytes = cfg.sites * sizeof(Complex) * 2;
    result.initial_contact = contact_probability(result.amplitudes, cfg.sites);
    result.peak_contact = result.initial_contact;
    Workspace work(cfg.sites, cfg.hopping, cfg.contact, cfg.dt);
    for (std::size_t t = 0; t < cfg.steps; ++t) {
      work.step(result.amplitudes);
      const double observed_norm = norm(result.amplitudes);
      result.max_norm_error = std::max(result.max_norm_error, std::abs(observed_norm - 1.0));
      result.peak_contact = std::max(result.peak_contact,
                                    contact_probability(result.amplitudes, cfg.sites));
    }
    result.norm = norm(result.amplitudes);
    result.final_contact = contact_probability(result.amplitudes, cfg.sites);
    for (std::size_t x = 0; x < cfg.sites; ++x)
      for (std::size_t y = 0; y < x; ++y)
        result.exchanged_order += std::norm(result.amplitudes[x * cfg.sites + y]);
    return result;
  }

  static double norm(const std::vector<Complex>& psi) {
    double total = 0.0;
    for (const auto& a : psi) total += std::norm(a);
    return total;
  }
  static double contact_probability(const std::vector<Complex>& psi, std::size_t sites) {
    if (sites == 0 || psi.size() != sites * sites)
      throw std::invalid_argument("contact probability size mismatch");
    double total = 0.0;
    for (std::size_t i = 0; i < sites; ++i) total += std::norm(psi[i * sites + i]);
    return total;
  }
  // Reduced one-particle purity Tr(rho_1^2), with rho_1=Tr_2|psi><psi|.
  // Equals 1 for a pure product state, below 1 for an entangled two-body pure
  // state. Explicit O(N^3) observer is deliberately limited to <=128 sites,
  // and is NEVER invoked inside a propagator step.
  static double reduced_purity(const std::vector<Complex>& psi, std::size_t sites) {
    if (sites == 0 || sites > 128 || psi.size() != sites * sites)
      throw std::invalid_argument("reduced purity needs 1..128 sites and N*N amplitudes");
    double purity = 0.0;
    for (std::size_t x = 0; x < sites; ++x)
      for (std::size_t y = 0; y < sites; ++y) {
        Complex rho{};
        for (std::size_t k = 0; k < sites; ++k)
          rho += psi[x * sites + k] * std::conj(psi[y * sites + k]);
        purity += std::norm(rho);
      }
    return purity;
  }

  static double probability_l1(const Result& a, const Result& b) {
    if (a.sites != b.sites || a.amplitudes.size() != b.amplitudes.size())
      throw std::invalid_argument("incompatible scatter results");
    double total = 0.0;
    for (std::size_t i = 0; i < a.amplitudes.size(); ++i)
      total += std::abs(std::norm(a.amplitudes[i]) - std::norm(b.amplitudes[i]));
    return total;
  }

 private:
  static double periodic_offset(double delta, std::size_t n) {
    const double period = static_cast<double>(n);
    return delta - period * std::floor(delta / period + 0.5);
  }
  static std::uint64_t validate(const Input& cfg) {
    const auto estimated = estimate_work(cfg.sites, cfg.steps);
    if (cfg.work_budget == 0 || estimated > cfg.work_budget)
      throw std::length_error("scatter work budget exceeded; reduce sites or steps");
    if (!std::isfinite(cfg.hopping) || !(cfg.hopping > 0.0) || cfg.hopping > 100.0 ||
        !std::isfinite(cfg.contact) || std::abs(cfg.contact) > 100.0 ||
        !std::isfinite(cfg.dt) || !(cfg.dt > 0.0) || cfg.dt > 0.25)
      throw std::invalid_argument("scatter requires finite J>0, |U|<=100, 0<dt<=0.25");
    return estimated;
  }

  static void fft(Complex* values, std::size_t n, bool inverse) {
    for (std::size_t i = 1, j = 0; i < n; ++i) {
      std::size_t bit = n >> 1;
      for (; j & bit; bit >>= 1) j ^= bit;
      j ^= bit;
      if (i < j) std::swap(values[i], values[j]);
    }
    for (std::size_t len = 2; len <= n; len <<= 1) {
      const Complex root = std::polar(1.0, (inverse ? 2.0 : -2.0) * pi / len);
      for (std::size_t begin = 0; begin < n; begin += len) {
        Complex factor(1.0, 0.0);
        for (std::size_t j = 0; j < len / 2; ++j) {
          const Complex u = values[begin + j];
          const Complex v = values[begin + j + len / 2] * factor;
          values[begin + j] = u + v;
          values[begin + j + len / 2] = u - v;
          factor *= root;
        }
      }
    }
    if (inverse)
      for (std::size_t i = 0; i < n; ++i) values[i] /= static_cast<double>(n);
  }

  struct Workspace {
    std::size_t n;
    std::vector<Complex> column, kinetic;
    Complex half_contact;
    Workspace(std::size_t sites, double hopping, double contact, double dt)
      : n(sites), column(sites), kinetic(sites),
        half_contact(std::polar(1.0, -0.5 * dt * contact)) {
      for (std::size_t k = 0; k < n; ++k)
        kinetic[k] = std::polar(1.0, 2.0 * dt * hopping *
                                    std::cos(2 * pi * k / n));
    }
    void transform(std::vector<Complex>& amplitudes, bool inverse) {
      for (std::size_t row = 0; row < n; ++row)
        fft(amplitudes.data() + row * n, n, inverse);
      for (std::size_t col = 0; col < n; ++col) {
        for (std::size_t row = 0; row < n; ++row)
          column[row] = amplitudes[row * n + col];
        fft(column.data(), n, inverse);
        for (std::size_t row = 0; row < n; ++row)
          amplitudes[row * n + col] = column[row];
      }
    }
    void step(std::vector<Complex>& amplitudes) {
      for (std::size_t i = 0; i < n; ++i)
        amplitudes[i * n + i] *= half_contact;
      transform(amplitudes, false);
      for (std::size_t i = 0; i < n; ++i)
        for (std::size_t j = 0; j < n; ++j)
          amplitudes[i * n + j] *= kinetic[i] * kinetic[j];
      transform(amplitudes, true);
      for (std::size_t i = 0; i < n; ++i)
        amplitudes[i * n + i] *= half_contact;
    }
  };
};

}  // namespace mundo_x
