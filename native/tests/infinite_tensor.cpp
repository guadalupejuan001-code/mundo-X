#include <algorithm>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"

namespace {
using Core = mundo_x::CompactCore;
using Dense = mundo_x::DenseCore64;
using Complex = std::complex<double>;
constexpr double tolerance = 1e-10;
std::size_t checks = 0, dense_comparisons = 0, tensor_cases = 0;

void require(bool condition, const std::string& message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
void close(double actual, double expected, const std::string& message) {
  ++dense_comparisons;
  require(std::isfinite(actual) && std::abs(actual - expected) < tolerance, message);
}
void gate(Core& core, Dense& dense, char op, std::size_t a, std::size_t b = 0) {
  if (op == 'H') { core.hadamard(a); dense.hadamard(a); }
  else if (op == 'S') { core.phase_s(a); dense.phase_s(a); }
  else if (op == 'X') { core.pauli_x(a); dense.pauli_x(a); }
  else if (op == 'Z') { core.pauli_z(a); dense.pauli_z(a); }
  else if (op == 'C') { core.cnot(a, b); dense.cnot(a, b); }
  else if (op == 'D') { core.controlled_z(a, b); dense.controlled_z(a, b); }
  else throw std::logic_error("invalid tensor fixture gate");
}
std::vector<std::string> rows(const Core& core) {
  std::vector<std::string> result;
  for (std::size_t r = 0; r < core.qubits(); ++r) result.push_back(core.generator_string(r));
  return result;
}
double expectation(const std::vector<Complex>& state, const std::string& pauli) {
  Complex value{};
  double norm = 0;
  for (std::size_t i = 0; i < state.size(); ++i) {
    auto target = i;
    Complex phase{1., 0.};
    norm += std::norm(state[i]);
    for (std::size_t q = 0; q < pauli.size(); ++q) {
      const bool one = ((i >> q) & 1U) != 0;
      if (pauli[q] == 'X' || pauli[q] == 'Y') target ^= std::size_t{1} << q;
      if (pauli[q] == 'Y') phase *= one ? Complex{0, -1} : Complex{0, 1};
      if (pauli[q] == 'Z' && one) phase = -phase;
    }
    value += std::conj(state[target]) * phase * state[i];
  }
  require(norm > 0 && std::abs(value.imag()) < tolerance, "invalid dense tensor oracle expectation");
  return value.real() / norm;
}

// A full-rank set of n commuting signed Paulis stabilizing the independently
// prepared state identifies its pure state. Checking rank prevents a tensor
// implementation that accidentally duplicates valid generators from passing.
void full_rank(const std::vector<std::string>& generators, std::size_t n) {
  const auto words = (2 * n + 63) / 64;
  std::vector<std::vector<std::uint64_t>> matrix(n, std::vector<std::uint64_t>(words));
  for (std::size_t r = 0; r < n; ++r) {
    require(generators[r].size() == n + 1, "tensor generator has wrong length");
    for (std::size_t q = 0; q < n; ++q) {
      const char axis = generators[r][q + 1];
      require(axis == 'I' || axis == 'X' || axis == 'Y' || axis == 'Z', "invalid tensor Pauli letter");
      if (axis == 'X' || axis == 'Y') matrix[r][q / 64] |= std::uint64_t{1} << (q % 64);
      if (axis == 'Z' || axis == 'Y') matrix[r][(q + n) / 64] |= std::uint64_t{1} << ((q + n) % 64);
    }
  }
  std::size_t rank = 0;
  for (std::size_t column = 0; column < 2 * n && rank < n; ++column) {
    auto pivot = rank;
    const auto bit = std::uint64_t{1} << (column % 64);
    while (pivot < n && !(matrix[pivot][column / 64] & bit)) ++pivot;
    if (pivot == n) continue;
    std::swap(matrix[rank], matrix[pivot]);
    for (std::size_t r = rank + 1; r < n; ++r)
      if (matrix[r][column / 64] & bit)
        for (std::size_t w = 0; w < words; ++w) matrix[r][w] ^= matrix[rank][w];
    ++rank;
  }
  require(rank == n, "tensor product lost independent generators");
}
void check_small_product(const Core& core, const Dense& left, const Dense& right) {
  const auto n = left.qubits() + right.qubits();
  require(core.qubits() == n, "tensor product qubit count");
  // Direct complex multiplication of the two input vectors is independent of
  // packed offsets, row layout and tableau sign conventions.
  std::vector<Complex> state(std::size_t{1} << n);
  for (std::size_t b = 0; b < right.amplitudes(); ++b)
    for (std::size_t a = 0; a < left.amplitudes(); ++a)
      state[a | (b << left.qubits())] = left.state()[a] * right.state()[b];
  const auto generators = rows(core);
  for (const auto& row : generators) {
    require(row[0] == '+' || row[0] == '-', "invalid tensor generator sign");
    close((row[0] == '-' ? -1. : 1.) * expectation(state, row.substr(1)), 1.,
          "tensor generator fails independent dense product");
  }
  full_rank(generators, n);
  for (std::size_t q = 0; q < n; ++q) {
    std::string z(n, 'I'); z[q] = 'Z';
    close(core.probability_one_z(q), (1. - expectation(state, z)) / 2.,
          "tensor product local probability mismatch");
  }
}

void small_dense_products() {
  std::mt19937_64 rng(0x54454e534f525f58ULL);
  static constexpr char ops[] = {'H', 'S', 'X', 'Z', 'C', 'D'};
  for (std::size_t n = 1; n <= 3; ++n)
    for (std::size_t m = 1; m <= 3; ++m)
      for (std::size_t trial = 0; trial < 12; ++trial) {
        Core left(n), right(m); Dense a(n), b(m);
        for (std::size_t step = 0; step < 16; ++step) {
          for (const bool use_left : {true, false}) {
            auto& compact = use_left ? left : right;
            auto& dense = use_left ? a : b;
            const auto count = compact.qubits();
            const auto first = rng() % count;
            auto second = count > 1 ? rng() % (count - 1) : 0;
            if (count > 1 && second >= first) ++second;
            gate(compact, dense, ops[rng() % (count > 1 ? 6 : 4)], first, second);
          }
        }
        const auto left_before = rows(left), right_before = rows(right);
        auto product = Core::tensor_product(left, right); ++tensor_cases;
        check_small_product(product, a, b);
        require(rows(left) == left_before && rows(right) == right_before,
                "tensor construction mutated an input core");
        product.pauli_x(0);
        require(rows(left) == left_before && rows(right) == right_before,
                "tensor mutation aliases an input core");
      }
}

struct Factor {
  std::size_t start;
  Dense state;
};
struct WideReference {
  explicit WideReference(std::size_t n) : core(n) {}
  Core core;
  std::vector<Factor> factors;
};
WideReference prepare_wide(std::size_t n, std::size_t salt) {
  WideReference result(n);
  for (std::size_t q = 0; q < n;) {
    const auto count = std::min<std::size_t>(2, n - q);
    Dense reference(count);
    const auto run = [&](char op, std::size_t local, std::size_t other = 0) {
      if (op == 'H') { result.core.hadamard(q + local); reference.hadamard(local); }
      else if (op == 'S') { result.core.phase_s(q + local); reference.phase_s(local); }
      else if (op == 'X') { result.core.pauli_x(q + local); reference.pauli_x(local); }
      else if (op == 'Z') { result.core.pauli_z(q + local); reference.pauli_z(local); }
      else if (op == 'C') { result.core.cnot(q + local, q + other); reference.cnot(local, other); }
    };
    run('H', 0);
    if (count == 2) run('C', 0, 1);
    // Local complex phases and negative generators expose sign errors that
    // all-zero inputs or unsigned Bell fixtures would miss.
    if ((q + salt) % 3 != 0) run('S', 0);
    if ((q + salt) % 5 == 0) run('Z', count - 1);
    if ((q + salt) % 7 == 0) run('X', 0);
    result.factors.push_back(Factor{q, std::move(reference)});
    q += count;
  }
  return result;
}
double factorized_expectation(const std::string& pauli, const WideReference& reference,
                              std::size_t offset) {
  double value = 1.;
  for (const auto& factor : reference.factors)
    value *= expectation(factor.state.state(), pauli.substr(offset + factor.start, factor.state.qubits()));
  return value;
}
void word_boundary_products() {
  for (const auto& size : std::vector<std::pair<std::size_t, std::size_t>>{
      {1, 63}, {63, 1}, {63, 2}, {64, 1}, {65, 63}, {1, 127}, {127, 1}}) {
    const auto left = prepare_wide(size.first, 3), right = prepare_wide(size.second, 11);
    const auto left_before = rows(left.core), right_before = rows(right.core);
    const auto product = Core::tensor_product(left.core, right.core); ++tensor_cases;
    const auto n = size.first + size.second;
    require(product.qubits() == n, "wide tensor qubit count");
    const auto generators = rows(product);
    for (const auto& row : generators) {
      const auto pauli = row.substr(1);
      const double expected = factorized_expectation(pauli, left, 0) *
                              factorized_expectation(pauli, right, size.first);
      close((row[0] == '-' ? -1. : 1.) * expected, 1.,
            "tensor word-boundary generator/sign mismatch");
    }
    full_rank(generators, n);
    require(rows(left.core) == left_before && rows(right.core) == right_before,
            "wide tensor construction changed an input");
    for (const auto q : {std::size_t{0}, size.first - 1, size.first, n - 1}) {
      std::string z(n, 'I'); z[q] = 'Z';
      const double expected = factorized_expectation(z, left, 0) *
                              factorized_expectation(z, right, size.first);
      close(product.probability_one_z(q), (1. - expected) / 2.,
            "word-boundary tensor Born probability mismatch");
    }
  }
}
}  // namespace

int main() {
  bool ok = true;
  try { small_dense_products(); word_boundary_products(); }
  catch (const std::exception& error) { std::cerr << "infinite_tensor: " << error.what() << '\n'; ok = false; }
  std::cout << "{\"ok\":" << (ok ? "true" : "false")
            << ",\"checks\":" << checks << ",\"dense_comparisons\":" << dense_comparisons
            << ",\"tensor_cases\":" << tensor_cases << "}\n";
  return ok ? 0 : 1;
}
