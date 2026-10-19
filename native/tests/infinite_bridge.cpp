#include "mundo_x/infinite_bridge.hpp"
#include "mundo_x/infinite_session.hpp"

#include <atomic>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <new>

namespace {
std::atomic<long> allocations_until_failure{-1};
}

#if defined(__GNUC__) || defined(__clang__)
#define MXI_NOINLINE __attribute__((noinline))
#elif defined(_MSC_VER)
#define MXI_NOINLINE __declspec(noinline)
#else
#define MXI_NOINLINE
#endif

MXI_NOINLINE void* operator new(std::size_t bytes) {
  const auto remaining = allocations_until_failure.load();
  if (remaining == 0) throw std::bad_alloc();
  if (remaining > 0) --allocations_until_failure;
  if (void* memory = std::malloc(bytes ? bytes : 1)) return memory;
  throw std::bad_alloc();
}
MXI_NOINLINE void* operator new[](std::size_t bytes) { return ::operator new(bytes); }
MXI_NOINLINE void operator delete(void* memory) noexcept { std::free(memory); }
MXI_NOINLINE void operator delete[](void* memory) noexcept { std::free(memory); }
MXI_NOINLINE void operator delete(void* memory, std::size_t) noexcept { std::free(memory); }
MXI_NOINLINE void operator delete[](void* memory, std::size_t) noexcept { std::free(memory); }

namespace {
using Session = mundo_x::InfiniteSession;
std::size_t checks = 0, allocation_failures = 0;
void check(bool value, const char* message) {
  ++checks; if (!value) throw std::runtime_error(message);
}

struct Owner {
  void* value;
  ~Owner() { mxi_destroy(value); }
};

std::string snapshot(void* handle) {
  char* output = nullptr;
  check(mxi_snapshot(handle, &output) == 0 && output, "C snapshot failed");
  std::string result(output); mxi_free_string(output); return result;
}

void shared_ownership() {
  auto shared = std::make_shared<Session>(Session::Background::BellPairs);
  Owner render{mundo_x::share_infinite_session(shared)};
  Owner host{mundo_x::share_infinite_session(shared)};
  check(mundo_x::infinite_session_from_handle(render.value) == shared,
        "C handle did not retain the same session");
  const char* observables[] = {"X:0 X:1", "Y:0 Y:1", "Z:0 Z:1"};
  double values[3] = {};
  std::uint64_t revision = 9, epoch = 9;
  check(mxi_query(render.value, observables, 3, values, &revision, &epoch) == 0,
        "C sparse query failed");
  check(values[0] == 1 && values[1] == -1 && values[2] == 1 && !revision && !epoch,
        "C implicit Bell values/metadata differ");
  char* response = nullptr;
  check(mxi_execute(host.value, "z 0\nexpect X:0 X:1\n", &response) == 0,
        "shared host execute failed");
  check(std::string(response) == "{\"type\":\"expectation\",\"value\":-1}\n",
        "shared host response differs");
  mxi_free_string(response);
  check(mxi_query(render.value, observables, 3, values, &revision, &epoch) == 0 &&
        values[0] == -1 && values[1] == 1 && values[2] == 1 && revision == 1 && epoch == 1,
        "renderer handle observed a different session from host");
  mx_infinite_stats stats{};
  check(mxi_get_stats(render.value, &stats) == 0 && stats.active_qubits == 2 &&
        stats.components == 1 && stats.largest_component == 2 && stats.tableau_bytes == 34 &&
        stats.address_bytes == 2 && stats.background == 1 && stats.revision == revision &&
        stats.epoch == epoch, "C statistics ABI layout differs");
  const auto saved = snapshot(host.value);
  check(mxi_restore(render.value, saved.c_str()) == 0, "shared snapshot restore failed");
  check(mxi_get_stats(host.value, &stats) == 0 && stats.revision == 1 && stats.epoch == 2,
        "same-revision restore did not invalidate shared observer");
  mxi_destroy(host.value); host.value = nullptr;
  shared.reset();
  check(mxi_query(render.value, observables, 3, values, &revision, &epoch) == 0 && values[0] == -1,
        "destroying another owner invalidated a live renderer");
}

void failures() {
  Owner owner{mxi_create(0)};
  check(owner.value != nullptr, "C create failed");
  char* response = nullptr;
  check(mxi_execute(owner.value, "seed 178\nh 0\n", &response) == 0, "preparation failed");
  mxi_free_string(response);
  const auto before = snapshot(owner.value);
  mx_infinite_stats initial{};
  check(mxi_get_stats(owner.value, &initial) == 0, "initial stats failed");
  char sentinel[] = "unchanged";
  response = sentinel;
  check(mxi_execute(owner.value, "measure 0\nx 1\nassert probz 999 1\n", &response) == -1 &&
        response == sentinel && std::strstr(mxi_last_error(), "assertion failed"),
        "C rejected program published output or lost its error");
  check(snapshot(owner.value) == before, "C failed program changed state or RNG");
  const char* bad_queries[] = {"X:0", "X:1 Z:01"};
  double values[2] = {111, 222}; std::uint64_t revision = 333, epoch = 444;
  check(mxi_query(owner.value, bad_queries, 2, values, &revision, &epoch) == -1 &&
        values[0] == 111 && values[1] == 222 && revision == 333 && epoch == 444,
        "C failed query partially wrote output");
  check(mxi_restore(owner.value, "MXI1:00") == -1 && snapshot(owner.value) == before,
        "C malformed import changed state");
  check(mxi_reset(owner.value, 2) == -1 && snapshot(owner.value) == before,
        "C invalid reset changed state");
  check(mxi_execute(owner.value, nullptr, &response) == -1 && response == sentinel,
        "C null program was accepted");
  check(mxi_execute(owner.value, "x 0\n", nullptr) == -1 && snapshot(owner.value) == before,
        "C null output committed a gate");
  check(mxi_query(owner.value, nullptr, 0, nullptr, &revision, &epoch) == 0 &&
        revision == initial.revision && epoch == initial.epoch, "empty atomic query failed");
  check(mxi_create(2) == nullptr && std::strlen(mxi_last_error()) != 0, "invalid C background accepted");
  check(mxi_get_stats(nullptr, &initial) == -1, "C null handle accepted");
  mxi_destroy(nullptr); mxi_free_string(nullptr);
}

void response_allocation_rollback() {
  auto shared = std::make_shared<Session>();
  shared->execute("seed 413\nh 0\n");
  Owner owner{mundo_x::share_infinite_session(shared)};
  const auto before = shared->snapshot(); const auto epoch = shared->epoch();
  bool completed = false;
  char sentinel[] = "sentinel";
  for (long budget = 0; budget < 512; ++budget) {
    char* output = sentinel;
    allocations_until_failure = budget;
    const auto status = mxi_execute(owner.value, "measure 0\nexpect Z:0\nstats\n", &output);
    allocations_until_failure = -1;
    if (status == 0) {
      check(output != sentinel, "successful C transport did not publish response");
      mxi_free_string(output); completed = true; break;
    }
    ++allocation_failures;
    check(output == sentinel, "allocation failure published a partial C response");
    check(shared->snapshot() == before && shared->epoch() == epoch,
          "allocation failure committed state/RNG/epoch before response allocation");
  }
  check(completed && allocation_failures > 0, "allocation probe did not reach both failure and success");
  Session expected;
  expected.restore(before);
  expected.execute("measure 0\nexpect Z:0\nstats\n");
  check(shared->snapshot() == expected.snapshot(), "failed C requests consumed the RNG sequence");
}
}  // namespace

int main() {
  try {
    static_assert(sizeof(mx_infinite_stats) == 8 * sizeof(std::uint64_t), "C statistics ABI changed");
    shared_ownership(); failures(); response_allocation_rollback();
    std::cout << "{\"suite\":\"infinite_bridge\",\"ok\":true,\"checks\":" << checks
              << ",\"allocation_failures\":" << allocation_failures
              << ",\"shared_state_and_owner_lifetime\":true}\n";
    return 0;
  } catch (const std::exception& error) {
    allocations_until_failure = -1;
    std::cerr << "infinite_bridge: " << error.what() << '\n'; return 1;
  }
}
