#include <cstddef>
#include <iostream>
#include <limits>
#include <random>
#include <stdexcept>
#include <string>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"

namespace {
int checks = 0;
int failures = 0;

void expect(bool condition, const char* name) {
  ++checks;
  std::cout << (condition ? "PASS " : "FAIL ") << name << '\n';
  failures += !condition;
}

template <typename Exception, typename Function>
void expect_exception(Function function, const char* name) {
  bool expected = false;
  try {
    function();
  } catch (const Exception&) {
    expected = true;
  } catch (const std::exception& error) {
    std::cout << "  unexpected exception: " << error.what() << '\n';
  }
  expect(expected, name);
}
}  // namespace

int main() {
  using mundo_x::CompactCore;
  using mundo_x::ReducedExactTRepresentation;
  const auto maximum = std::numeric_limits<std::size_t>::max();

  expect_exception<std::invalid_argument>([] { CompactCore state(0); },
                                          "compact rejects zero qubits");
  for (const std::size_t n : {63u, 64u, 65u}) {
    CompactCore state(n);
    state.hadamard(0);
    for (std::size_t q = 1; q < n; ++q) state.cnot(q - 1, q);
    expect(state.is_ghz_signature(),
           ("GHZ across packed-word boundary n=" + std::to_string(n)).c_str());
    const std::size_t words = n / 64 + (n % 64 != 0);
    expect(state.state_bytes() == 2 * n * words * sizeof(std::uint64_t) + n,
           ("tableau byte count n=" + std::to_string(n)).c_str());
    CompactCore measured(state);
    std::mt19937_64 rng(2026);
    const auto outcome = measured.measure_z(n - 1, rng);
    expect(outcome.random && outcome.probability == 0.5,
           "GHZ measurement at packed-word boundary is random");
    expect(measured.probability_one_z(0) == (outcome.outcome_one ? 1.0 : 0.0),
           "boundary measurement fixes distant GHZ qubit");
    expect(measured.probability_one_z(n - 1) == (outcome.outcome_one ? 1.0 : 0.0),
           "measured highest qubit is deterministic");
    expect_exception<std::out_of_range>([&] { state.hadamard(n); },
                                        "out-of-range gate rejected");
    expect(state.is_ghz_signature(), "rejected gate leaves state unchanged");
  }

  CompactCore one(1);
  one.hadamard(0);
  expect(one.is_cluster_signature(1, 1), "one-qubit cluster accepted");
  expect(!one.is_cluster_signature(maximum, maximum),
         "cluster signature rejects overflowing dimensions");

  // These powers make qubits * ceil(qubits / 64) wrap to zero on
  // both 32-bit and 64-bit size_t. Construction must reject the dimensions
  // before requesting the enormous phase vector from the allocator.
  const std::size_t overflowing_qubits =
      std::size_t{1} << ((std::numeric_limits<std::size_t>::digits + 7) / 2);
  expect_exception<std::length_error>(
      [&] { CompactCore state(overflowing_qubits); },
      "tableau multiplication overflow rejected before allocation");
  expect_exception<std::length_error>([&] { CompactCore state(maximum); },
                                     "SIZE_MAX tableau rejected");

  expect_exception<std::invalid_argument>(
      [] { ReducedExactTRepresentation state(0); }, "reduced rejects zero");
  expect_exception<std::invalid_argument>(
      [] { ReducedExactTRepresentation state(64); }, "reduced rejects 64");
  expect_exception<std::invalid_argument>(
      [&] { ReducedExactTRepresentation state(maximum); },
      "reduced rejects SIZE_MAX before constructing tableau");

  ReducedExactTRepresentation reduced(63);
  reduced.phase_t(62);
  expect(reduced.component_count() == 1,
         "T on highest supported computational-basis qubit");
  reduced.hadamard(62);
  reduced.phase_t(62);
  expect(reduced.component_count() == 2,
         "T branches on highest supported superposition qubit");
  expect_exception<std::out_of_range>([&] { reduced.phase_t(63); },
                                     "reduced rejects out-of-range gate");
  expect(reduced.component_count() == 2,
         "reduced rejected gate leaves components unchanged");
  expect_exception<std::runtime_error>([&] { reduced.dense_from_reduced(); },
                                       "dense validation guard at 63 qubits");

  std::cout << "checks=" << checks << " failures=" << failures << '\n';
  return failures == 0 ? 0 : 1;
}
