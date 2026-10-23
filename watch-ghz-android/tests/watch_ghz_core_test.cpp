#include "mundo_x/watch_ghz_core.hpp"
#include <cmath>
#include <complex>
#include <iostream>
#include <stdexcept>
#include <vector>

using Complex = std::complex<double>;
using State = std::vector<Complex>;
static int checks = 0;
static void check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}
static bool near(double a, double b) { return std::abs(a - b) < 1e-12; }

// Independent dense oracle, used only in tests (never linked into the app).
static State dense_ghz(unsigned n, unsigned phase) {
  State s(std::size_t{1} << n);
  s[0] = 1;
  for (std::size_t k = 0; k < s.size(); k += 2) {
    const auto a = s[k], b = s[k + 1];
    s[k] = (a + b) / std::sqrt(2.0);
    s[k + 1] = (a - b) / std::sqrt(2.0);
  }
  for (unsigned q = 1; q < n; ++q)
    for (std::size_t k = 0; k < s.size(); ++k)
      if ((k & 1U) && !(k & (std::size_t{1} << q)))
        std::swap(s[k], s[k | (std::size_t{1} << q)]);
  for (unsigned turn = 0; turn < phase; ++turn)
    for (std::size_t k = 1; k < s.size(); k += 2) s[k] *= Complex(0, 1);
  return s;
}

static double expectation(const State& s, unsigned n, char first) {
  Complex result = 0;
  const auto mask = (std::size_t{1} << n) - 1;
  for (std::size_t k = 0; k < s.size(); ++k) {
    const Complex factor = first == 'Y' ? Complex(0, (k & 1U) ? -1 : 1) : Complex(1, 0);
    result += std::conj(s[k ^ mask]) * factor * s[k];
  }
  check(std::abs(result.imag()) < 1e-12, "oracle expectation imaginary");
  return result.real();
}

int main() {
  mundo_x::WatchGhzCore engine;
  check(engine.report().active_qubits == 0, "initial state materialized");
  bool rejected = false;
  try { engine.measure(); } catch (const std::logic_error&) { rejected = true; }
  check(rejected, "empty measurement accepted");

  for (unsigned n = 3; n <= 7; ++n) {
    engine.prepare(n, 42);
    for (unsigned phase = 0; phase < 4; ++phase) {
      const auto r = engine.report();
      const auto oracle = dense_ghz(n, phase);
      check(near(r.x_all, expectation(oracle, n, 'X')), "X oracle mismatch");
      check(near(r.yx_all, expectation(oracle, n, 'Y')), "YX oracle mismatch");
      check(r.phase_quarters == phase && r.probability_one == .5, "phase / probability");
      check(r.zz_ends == 1, "GHZ Z correlation lost");
      engine.advance_phase();
    }
    check(engine.report().phase_quarters == 0 && engine.report().x_all == 1, "S cycle");
  }

  for (std::size_t n : {3U, 16U, 64U, 256U, 1000U}) {
    auto r = engine.prepare(n, 824);
    check(r.active_qubits == n && r.components == 1, "wrong active dimensions");
    check(r.tableau_bytes == n * (16 * ((n + 63) / 64) + 1), "payload accounting");
    check(r.x_all == 1 && r.yx_all == 0, "multiword GHZ X");
    r = engine.advance_phase();
    check(r.x_all == 0 && r.yx_all == 1, "multiword GHZ YX");
    r = engine.measure();
    check(r.collapsed && r.all_equal_verified, "collapse not verified at every address");
    check(r.x_all == 0 && r.yx_all == 0 && r.zz_ends == 1, "collapse coherence");
    check(r.probability_one == r.outcome, "projected probability");
    check(engine.measure().outcome == r.outcome, "remeasurement changed result");
    const auto before = engine.report().json();
    rejected = false;
    try { engine.advance_phase(); } catch (const std::logic_error&) { rejected = true; }
    check(rejected && before == engine.report().json(), "collapsed S rollback");
    r = engine.reset();
    check(r.active_qubits == 0 && r.tableau_bytes == 0 && r.components == 0, "reset payload retained");
  }

  engine.prepare(3, 9);
  const auto before = engine.report().json();
  for (auto n : {0U, 1U, 2U, 1001U}) {
    rejected = false;
    try { engine.prepare(n, 0); } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && before == engine.report().json(), "bad size changed state");
  }
  unsigned ones = 0;
  for (unsigned seed = 0; seed < 128; ++seed) {
    engine.prepare(3, seed);
    ones += static_cast<unsigned>(engine.measure().outcome);
  }
  check(ones > 32 && ones < 96, "seeded measurements implausibly biased");
  std::cout << "PASS: " << checks << " checks; dense oracle, phase, collapse, rollback, 1000 qubits, release\n";
  std::cout << "Seeded simulated samples: " << ones << " ones / 128 (not physical measurements)\n";
}
