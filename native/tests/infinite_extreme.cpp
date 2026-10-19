#include "mundo_x/infinite_core.hpp"
#include "mundo_x/infinite_program.hpp"
#include "mundo_x/infinite_session.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
using mundo_x::InfiniteCore;
using mundo_x::InfiniteObservable;
using mundo_x::InfiniteSession;

void require(bool value, const char* message) {
  if (!value) throw std::runtime_error(message);
}

template <class F>
double median_ms(F&& body, int samples = 7) {
  std::vector<double> values;
  values.reserve(samples);
  volatile double sink = 0.0;
  for (int i = 0; i < samples; ++i) {
    const auto begin = Clock::now();
    sink += body();
    const auto end = Clock::now();
    values.push_back(std::chrono::duration<double, std::milli>(end - begin).count());
  }
  std::sort(values.begin(), values.end());
  if (!std::isfinite(sink)) throw std::runtime_error("non-finite timing sink");
  return values[values.size() / 2];
}

std::string address_1001(std::size_t index) {
  std::ostringstream tail;
  tail << std::setw(6) << std::setfill('0') << index;
  return std::string(995, '9') + tail.str();
}
}

int main() {
  try {
    InfiniteCore::Limits limits;
    limits.max_active_qubits = 4096;
    limits.max_component_qubits = 1024;
    limits.max_state_bytes = std::size_t{128} * 1024 * 1024;
    limits.max_address_digits = 4096;
    limits.max_query_terms = 4096;

    InfiniteCore active(InfiniteCore::Background::Zero, limits);
    std::vector<InfiniteCore::Gate> prepare;
    prepare.reserve(1024);
    prepare.push_back({'H', "0", ""});
    for (std::size_t q = 1; q < 1024; ++q)
      prepare.push_back({'C', "0", std::to_string(q)});
    active.apply(prepare);
    require(active.stats().active_qubits == 1024, "GHZ materialization width");

    std::vector<std::vector<InfiniteCore::Term>> terms(256);
    std::vector<InfiniteCore::QueryRef> refs;
    refs.reserve(terms.size());
    for (std::size_t i = 0; i < terms.size(); ++i) {
      const auto q = 1 + (i * 997) % 1023;
      terms[i] = {{"0", 'Z'}, {std::to_string(q), 'Z'}};
      refs.push_back({&terms[i], 1});
    }
    std::vector<double> scalar;
    scalar.reserve(terms.size());
    for (const auto& query : terms) scalar.push_back(active.expectation(query));
    const auto batched = active.expectations(refs);
    require(scalar == batched, "batched observer values differ from scalar queries");
    require(std::all_of(batched.begin(), batched.end(), [](double v) { return v == 1.0; }),
            "GHZ ZZ correlation lost");

    const double scalar_ms = median_ms([&] {
      double sum = 0.0;
      for (const auto& query : terms) sum += active.expectation(query);
      return sum;
    });
    const double batch_ms = median_ms([&] {
      const auto values = active.expectations(refs);
      return std::accumulate(values.begin(), values.end(), 0.0);
    });

    InfiniteSession remote(InfiniteCore::Background::BellPairs, limits);
    std::vector<InfiniteObservable> observables;
    observables.reserve(4096);
    for (std::size_t i = 0; i < 4096; ++i) {
      const auto a = address_1001(i * 2);
      const auto b = address_1001(i * 2 + 1);
      // Use independent remote sites only as single-site Z probes. Bell singles are zero.
      observables.push_back({{{a, 'Z'}}, 1});
      (void)b;
    }
    const auto remote_begin = Clock::now();
    const auto remote_values = remote.query_batch(observables);
    const auto remote_end = Clock::now();
    require(remote_values.values.size() == 4096, "remote query count");
    require(std::all_of(remote_values.values.begin(), remote_values.values.end(),
                        [](double v) { return v == 0.0; }), "remote Bell singles");
    require(remote_values.stats.active_qubits == 0 && remote_values.stats.tableau_bytes == 0,
            "remote read materialized infinite background");
    const double remote_ms =
        std::chrono::duration<double, std::milli>(remote_end - remote_begin).count();

    InfiniteSession churn(InfiniteCore::Background::Zero, limits);
    std::vector<InfiniteCore::Gate> gates;
    gates.reserve(65536);
    for (std::size_t i = 0; i < 65536; ++i) gates.push_back({'X', "7", ""});
    const auto churn_begin = Clock::now();
    churn.apply(gates);
    const auto churn_end = Clock::now();
    require(churn.probability_one("7") == 0.0, "even X stress result");
    require(churn.stats().active_qubits == 1, "stress retained unexpected sites");
    const auto before_bad = churn.snapshot();
    bool rejected = false;
    try { churn.apply({{'Q', "7", ""}}); }
    catch (const std::invalid_argument&) { rejected = true; }
    require(rejected && churn.snapshot() == before_bad, "failed batch changed state");
    require(churn.compact_background() == 1 && churn.stats().active_qubits == 0,
            "stress compaction failed");
    const double churn_ms =
        std::chrono::duration<double, std::milli>(churn_end - churn_begin).count();

    std::cout << std::setprecision(9)
              << "{\"schema\":1,\"probe\":\"infinite_extreme\",\"ok\":true,"
              << "\"active_component_qubits\":1024,\"observables\":256,"
              << "\"scalar_median_ms\":" << scalar_ms
              << ",\"batch_median_ms\":" << batch_ms
              << ",\"batch_speedup\":" << (scalar_ms / batch_ms)
              << ",\"remote_observables\":4096,\"remote_address_digits\":1001,"
              << "\"remote_active_after\":" << remote.stats().active_qubits
              << ",\"remote_ms\":" << remote_ms
              << ",\"stress_gates\":65536,\"stress_gate_ms\":" << churn_ms
              << ",\"rollback_exact\":true,\"compaction_exact\":true}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "{\"schema\":1,\"probe\":\"infinite_extreme\",\"ok\":false,\"error\":\""
              << error.what() << "\"}\n";
    return 1;
  }
}
