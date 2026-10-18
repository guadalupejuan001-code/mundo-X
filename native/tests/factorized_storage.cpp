#include "mundo_x/factorized_core.hpp"

#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

// Fault injection belongs only to this test executable. Every allocation in a
// chosen merge/split is rejected in turn; the simulator has no test-only API.
namespace allocation_probe {
bool enabled = false;
std::size_t calls = 0;
std::size_t failure = std::numeric_limits<std::size_t>::max();
std::size_t outstanding = 0;
void* allocate(std::size_t bytes) {
  if (enabled && calls++ == failure) throw std::bad_alloc();
  void* result = std::malloc(bytes ? bytes : 1);
  if (!result) throw std::bad_alloc();
  ++outstanding;
  return result;
}
void release(void* memory) noexcept {
  if (memory) { --outstanding; std::free(memory); }
}
struct Guard {
  explicit Guard(std::size_t rejected = std::numeric_limits<std::size_t>::max()) {
    calls = 0; failure = rejected; enabled = true;
  }
  ~Guard() { enabled = false; }
};
}
void* operator new(std::size_t bytes) { return allocation_probe::allocate(bytes); }
void* operator new[](std::size_t bytes) { return allocation_probe::allocate(bytes); }
void operator delete(void* memory) noexcept { allocation_probe::release(memory); }
void operator delete[](void* memory) noexcept { allocation_probe::release(memory); }
void operator delete(void* memory, std::size_t) noexcept { allocation_probe::release(memory); }
void operator delete[](void* memory, std::size_t) noexcept { allocation_probe::release(memory); }

namespace {
using Core = mundo_x::FactorizedCore;
std::size_t checks = 0;
std::size_t injected_failures = 0;
void require(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
bool equal_storage(const Core::StorageEstimate& a, const Core::StorageEstimate& b) {
  return a.singleton_capacity_bytes == b.singleton_capacity_bytes &&
      a.active_singleton_amplitude_bytes == b.active_singleton_amplitude_bytes &&
      a.retained_singleton_capacity_bytes == b.retained_singleton_capacity_bytes &&
      a.multiqubit_amplitude_capacity_bytes == b.multiqubit_amplitude_capacity_bytes &&
      a.estimated_metadata_bytes == b.estimated_metadata_bytes &&
      a.estimated_reserved_bytes == b.estimated_reserved_bytes;
}
struct Snapshot {
  std::vector<Core::Complex> dense;
  std::vector<std::vector<std::size_t>> partition;
  Core::StorageEstimate storage;
  std::size_t factors, amplitudes, largest;
};
Snapshot snapshot(const Core& core) {
  return {core.dense_state(), core.block_partition(), core.storage_estimate(),
          core.factor_count(), core.amplitude_count(), core.largest_factor_qubits()};
}
void unchanged(const Core& core, const Snapshot& before) {
  require(core.dense_state() == before.dense, "allocation failure changed coefficients");
  require(core.block_partition() == before.partition, "allocation failure changed partition");
  require(equal_storage(core.storage_estimate(), before.storage), "allocation failure changed storage accounting");
  require(core.factor_count() == before.factors, "allocation failure changed factor count");
  require(core.amplitude_count() == before.amplitudes, "allocation failure changed amplitude count");
  require(core.largest_factor_qubits() == before.largest, "allocation failure changed width histogram");
}

void constant_allocation_product() {
  std::size_t small_calls = 0;
  {
    allocation_probe::Guard probe;
    Core small(1);
    small_calls = allocation_probe::calls;
  }
  std::size_t large_calls = 0;
  {
    allocation_probe::Guard probe;
    Core large(100000);
    large_calls = allocation_probe::calls;
  }
  require(small_calls <= 8 && large_calls == small_calls,
          "singleton construction allocations must remain constant in N");
  Core product(100000);
  std::size_t gate_calls = 0;
  {
    allocation_probe::Guard probe;
    for (std::size_t q = 0; q < product.qubits(); ++q) {
      product.hadamard(q);
      if (q % 2) product.phase_t(q); else product.phase_s(q);
      if (q % 3) product.pauli_x(q); else product.pauli_z(q);
    }
    gate_calls = allocation_probe::calls;
  }
  require(gate_calls == 0, "arbitrary singleton local gates allocate no heap memory");
  const auto storage = product.storage_estimate();
  require(storage.singleton_capacity_bytes >= product.qubits() * 2 * sizeof(Core::Complex), "flat singleton capacity");
  require(storage.active_singleton_amplitude_bytes == product.logical_amplitude_bytes(), "all product coefficients active");
  require(storage.retained_singleton_capacity_bytes == storage.singleton_capacity_bytes - storage.active_singleton_amplitude_bytes,
          "retained singleton capacity is explicit");
  require(storage.multiqubit_amplitude_capacity_bytes == 0, "product has no dense heap factors");
  require(storage.estimated_reserved_bytes >= 56 * product.qubits(), "flat buffer accounting includes lookup and slots");
  require(storage.estimated_reserved_bytes < 64 * product.qubits() + 4096,
          "product storage estimate remains below 64 bytes per qubit plus constant metadata");
  require(product.factor_count() == product.qubits(), "local product operations keep singleton factors");
  for (const auto q : {0u, 1u, 99999u})
    require(std::abs(product.probability_one(q) - 0.5) < 1e-15, "heterogeneous product local query");
  std::cout << "storage_product constructor_allocations=" << large_calls
            << " qubits=" << product.qubits()
            << " logical_bytes=" << product.logical_amplitude_bytes()
            << " estimated_reserved_bytes=" << storage.estimated_reserved_bytes << '\n';
}

void retained_capacity_and_reuse() {
  Core core(8);
  const auto baseline = core.storage_estimate();
  core.hadamard(7);
  for (std::size_t iteration = 0; iteration < 200; ++iteration) {
    const std::size_t q = iteration % 7;
    core.cnot(7, q);
    const auto merged = core.storage_estimate();
    require(merged.singleton_capacity_bytes == baseline.singleton_capacity_bytes, "merging retains physical singleton capacity");
    require(merged.active_singleton_amplitude_bytes == 6 * 2 * sizeof(Core::Complex), "merged singleton coefficients inactive");
    require(merged.retained_singleton_capacity_bytes >= 2 * 2 * sizeof(Core::Complex), "inactive singleton payload reported");
    require(merged.multiqubit_amplitude_capacity_bytes == 4 * sizeof(Core::Complex), "Bell block dense storage accounted");
    require(core.logical_amplitude_bytes() == 16 * sizeof(Core::Complex), "logical payload differs from physical reserve");
    require(merged.estimated_reserved_bytes > baseline.estimated_reserved_bytes, "dense block adds capacity despite equal logical amplitude count");
    core.cnot(7, q);
    const auto dense_before = core.dense_state();
    // The old representative 7 lies in the right cut; q reuses its own slot.
    require(core.try_split({q}), "singleton split with old representative on right");
    require(core.dense_state() == dense_before, "reuse split preserves represented phase-sensitive state");
    require(equal_storage(core.storage_estimate(), baseline), "splitting returns to baseline owned capacities");
    require(core.factor_qubits(q) == std::vector<std::size_t>{q}, "detached slot lookup restored");
    require(core.factor_count() == 8 && core.largest_factor_qubits() == 1, "reused factor counters");
  }
  Core pairs(8);
  pairs.hadamard(7); pairs.cnot(7, 0);
  pairs.hadamard(6); pairs.cnot(6, 1);
  const auto two_pairs = pairs.storage_estimate();
  pairs.cnot(0, 6); pairs.cnot(0, 6);
  const auto dense_before = pairs.dense_state();
  require(pairs.try_split({1, 6}), "noncontiguous dense cut reuses inactive representative");
  require(pairs.dense_state() == dense_before, "dense split phase preservation");
  require(equal_storage(pairs.storage_estimate(), two_pairs), "dense split capacity accounting restored");
  require(pairs.same_factor(6, 1) && pairs.same_factor(7, 0) && !pairs.same_factor(0, 1), "dense cut lookup identity");
}

template <class Setup, class Operation>
void reject_every_allocation(Setup setup, Operation operation, const char* message) {
  bool reached_success = false;
  for (std::size_t failure = 0; failure < 128; ++failure) {
    Core core = setup();
    const auto before = snapshot(core);
    const auto outstanding_before = allocation_probe::outstanding;
    bool rejected = false, result = false;
    {
      allocation_probe::Guard probe(failure);
      try { result = operation(core); }
      catch (const std::bad_alloc&) { rejected = true; }
    }
    if (rejected) {
      ++injected_failures;
      require(allocation_probe::outstanding == outstanding_before, "allocation rejection leaked a temporary");
      unchanged(core, before);
    } else {
      require(result, message);
      reached_success = true;
      break;
    }
  }
  require(reached_success, "fault-injection loop did not reach success");
}

void allocation_transactions() {
  for (std::size_t failure = 0; failure < 12; ++failure) {
    const auto before = allocation_probe::outstanding;
    bool rejected = false;
    {
      allocation_probe::Guard probe(failure);
      try { Core core(8); }
      catch (const std::bad_alloc&) { rejected = true; }
    }
    require(allocation_probe::outstanding == before, "constructor failure leaked allocation");
    if (!rejected) break;
  }
  reject_every_allocation([] { Core c(8); c.hadamard(7); c.phase_s(7); return c; },
                         [](Core& c) { c.cnot(7, 0); return true; }, "singleton merge must eventually succeed");
  reject_every_allocation([] { Core c(8); c.hadamard(7); c.cnot(7, 2); c.hadamard(0); return c; },
                         [](Core& c) { c.cnot(2, 0); return true; }, "mixed merge must eventually succeed");
  reject_every_allocation([] { Core c(8); c.hadamard(7); c.cnot(7, 2); c.hadamard(0); c.cnot(0, 5); return c; },
                         [](Core& c) { c.controlled_z(2, 5); return true; }, "dense merge must eventually succeed");
  reject_every_allocation([] { Core c(8); c.hadamard(7); c.cnot(7, 0); c.cnot(7, 0); return c; },
                         [](Core& c) { return c.try_split({0}); }, "singleton cut must eventually succeed");
  reject_every_allocation([] { Core c(8); c.hadamard(7); c.cnot(7, 0); c.hadamard(6); c.cnot(6, 1); c.cnot(0, 6); c.cnot(0, 6); return c; },
                         [](Core& c) { return c.try_split({1, 6}); }, "dense cut must eventually succeed");
  reject_every_allocation([] { Core c(8); c.hadamard(7); c.cnot(7, 0); c.cnot(0, 6); c.cnot(0, 6); return c; },
                         [](Core& c) { return c.try_split({6}); }, "mixed cut must eventually succeed");
}
}

int main() {
  try {
    constant_allocation_product();
    retained_capacity_and_reuse();
    allocation_transactions();
    std::cout << "factorized_storage PASS checks=" << checks
              << " injected_allocation_failures=" << injected_failures << '\n';
    return 0;
  } catch (const std::exception& error) {
    allocation_probe::enabled = false;
    std::cerr << "factorized_storage FAIL: " << error.what() << '\n';
    return 1;
  }
}
