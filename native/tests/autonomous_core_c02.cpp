#include "mundo_x/autonomous_core_c02.hpp"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using C02 = mundo_x::AutonomousCoreC02;
using Session = mundo_x::InfiniteSession;
namespace detail = mundo_x::autonomous_c02_detail;
std::size_t checks = 0;

void check(bool condition, const char* message) {
  ++checks;
  if (!condition) throw std::runtime_error(message);
}
template<class F> void rejects(F action, const char* message) {
  bool failed = false;
  try { action(); } catch (const std::exception&) { failed = true; }
  check(failed, message);
}
C02::MemoryProbe sufficient_memory() {
  return [] { return std::uint64_t{4} * 1024 * 1024 * 1024; };
}

void admission_tests() {
  C02::Options o;
  check(o.max_queued == 1024, "1024 pending jobs by default");
  check(o.max_program_bytes == 16u * 1024u * 1024u, "16MiB input budget");
  check(o.max_queue_bytes == 128u * 1024u * 1024u, "128MiB aggregate input budget");
  const auto max = o.max_queue_bytes;
  check(detail::can_admit(1023, 1024, max - 10, 10, max),
        "exact aggregate boundary accepted");
  check(!detail::can_admit(1024, 1024, 0, 1, max),
        "1025th pending job rejected");
  check(!detail::can_admit(1023, 1024, max - 10, 11, max),
        "overflow of total bytes rejected");
  check(!detail::can_admit(1, 1024, 900, 101, 1000),
        "dynamic cap rejects excess even below static maximum");
  check(detail::memory_budget(128, 400, 100, 50) == 128,
        "dynamic headroom never exceeds fixed safety ceiling");
  check(detail::memory_budget(128, 200, 100, 50) == 50,
        "dynamic headroom responds to low available memory");
  check(detail::memory_budget(128, 50, 100, 50) == 0,
        "below host reserve no new memory budget admitted");
  check(detail::memory_budget(128, 0, 100, 50) == 128,
        "unsupported host probe retains configured fixed cap");

  std::atomic<std::uint64_t> available{200};
  C02::Options small;
  small.workers = 1;
  small.max_queued = 2;
  small.max_program_bytes = 1000;
  small.max_queue_bytes = 1000;
  small.host_reserve_bytes = 100;
  small.admission_share_percent = 50;
  C02 manager(small, [&] { return available.load(); });
  rejects([&] { manager.submit(std::string(60, ' ')); },
          "memory pressure rejects admission before allocating XI state");
  available.store(1000);
  check(manager.submit("stats\n").get().ok, "memory recovered: job admitted");
  check(manager.telemetry().last_effective_cap == 450,
        "dynamic effective cap is auditable");
  check(manager.telemetry().rejected == 1, "pressure rejection counted");
  manager.shutdown();
  rejects([&] { manager.submit("stats\n"); },
          "admission denied after shutdown");

  C02::Options normal;
  normal.workers = 1;
  C02 large(normal, sufficient_memory());
  rejects([&] { large.submit(std::string(16u * 1024u * 1024u + 1, ' ')); },
          "17MiB request rejected with 16MiB limit");
}

void aging_tests() {
  const std::array<std::uint64_t, 3> young{{0, 0, 0}};
  std::size_t cursor = 0;
  std::array<std::size_t, 3> counts{{0, 0, 0}};
  for (int i = 0; i < 7; ++i) {
    const auto selected = detail::pick_priority(
        {{true, true, true}}, young, cursor, 2000000, 5000000);
    ++counts[selected];
  }
  check(counts == std::array<std::size_t, 3>{{4, 2, 1}},
        "weighted 4:2:1 preserved under mixed load");
  cursor = 0;
  check(detail::pick_priority({{false, true, true}}, young, cursor,
                              2000000, 5000000) == 1,
        "unused interactive slots transferred without busy-loop");
  cursor = 0;
  check(detail::pick_priority({{false, false, true}}, young, cursor,
                              2000000, 5000000) == 2,
        "background receives unused interactive and normal slots");
  cursor = 4;
  check(detail::pick_priority({{true, true, true}}, {{0, 0, 3000000}}, cursor,
                              2000000, 5000000) == 2,
        "aged background promoted into a normal slot");
  cursor = 0;
  check(detail::pick_priority({{true, true, true}}, {{0, 0, 6000000}}, cursor,
                              2000000, 5000000) == 2,
        "5-second background task outranks fresh interactive tasks");
  cursor = 0;
  check(detail::pick_priority({{true, true, true}}, {{0, 6000000, 3000000}}, cursor,
                              2000000, 5000000) == 1,
        "expired normal task promoted to interactive");
  cursor = 0;
  check(detail::pick_priority({{true, true, true}}, {{0, 6100000, 6200000}}, cursor,
                              2000000, 5000000) == 2,
        "oldest promoted task wins without moving its FIFO position");
}

void native_state_tests() {
  C02::Options config;
  config.workers = 4;
  C02 manager(config, sufficient_memory());
  Session reference;
  const std::vector<std::string> programs{
      "h 0\ncx 0 1\n", "s 0\n", "z 1\n"};
  for (const auto& program : programs) {
    const auto result = manager.submit(program).get();
    check(result.ok && result.committed && result.state_changed,
          "XI write committed through the real original runtime");
    reference.execute(program);
  }
  const auto main_state = manager.snapshot();
  check(main_state == reference.snapshot(),
        "serialized authoritative state byte-equivalent to direct XI");
  check(manager.metadata().epoch >= programs.size(), "authority epoch advances");

  const auto failed = manager.submit("x 123\nassert probz 123 0\n").get();
  check(!failed.ok && !failed.error.empty(), "native XI assertion failure visible");
  check(manager.snapshot() == main_state,
        "failed native XI program never commits a partial state");
  std::vector<std::future<C02::Outcome>> jobs;
  for (unsigned i = 0; i < 4; ++i) {
    jobs.push_back(manager.submit("x " + std::to_string(200 + i) + "\n",
        C02::Priority::Normal, C02::Mode::Isolated, true));
  }
  for (unsigned i = 0; i < jobs.size(); ++i) {
    const auto result = jobs[i].get();
    check(result.ok && result.isolated && !result.committed &&
              result.state_changed && !result.private_snapshot.empty(),
          "private worker state produced but not committed");
    Session local;
    local.restore(result.private_snapshot);
    check(local.probability_one(std::to_string(200 + i)) == 1.0,
          "private worker preserves its own XI transformation");
    check(local.probability_one(std::to_string(200 + ((i + 1) % 4))) == 0.0,
          "private workers do not share transient quantum changes");
  }
  check(manager.snapshot() == main_state, "all isolated runs leave central state intact");
  const auto read = manager.submit("expect Z:0 Z:1\n").get();
  check(read.ok && !read.committed && !read.state_changed,
        "read-only transactional query does not bump epoch");
  check(manager.snapshot() == main_state, "read-only transaction does not mutate RNG");
  const auto t = manager.telemetry();
  check(t.peak_active <= config.workers && t.active == 0 && t.queued == 0,
        "workers complete without leaking reservations");
  check(t.reserved_program_bytes == 0 && t.completed == t.accepted,
        "all accepted jobs accounted for on real executor");
}

void forced_conflict_tests(std::size_t retries, bool expect_both) {
  C02::Options options;
  options.workers = 2;
  options.conflict_retries = retries;
  std::mutex gate;
  std::condition_variable ready;
  int arrivals = 0;
  C02 manager(options, sufficient_memory(),
      [&](std::uint64_t, std::size_t attempt) {
        if (attempt != 0) return;
        std::unique_lock<std::mutex> lock(gate);
        ++arrivals;
        if (arrivals == 2) ready.notify_all();
        else if (!ready.wait_for(lock, std::chrono::seconds(10),
                                [&] { return arrivals >= 2; }))
          throw std::runtime_error("deterministic conflict barrier timed out");
      });
  auto a = manager.submit("x 100\n");
  auto b = manager.submit("x 101\n");
  const auto ra = a.get();
  const auto rb = b.get();
  check(arrivals == 2, "both private workers reached the same original epoch");
  check(manager.telemetry().conflicts >= 1, "epoch mismatch detected");
  if (expect_both) {
    check(ra.ok && rb.ok, "conflicted job retries and eventually commits");
    check((ra.conflicts + rb.conflicts) >= 1, "optimistic retry was exercised");
  } else {
    check(ra.ok != rb.ok, "strict zero-retry policy rejects one conflicting write");
    check(ra.conflict_exhausted || rb.conflict_exhausted,
          "exhausted conflict returns explicit failure without overwriting state");
  }
  Session state;
  state.restore(manager.snapshot());
  check(state.probability_one("100") == (ra.ok ? 1.0 : 0.0),
        "first writer's resulting bit agrees with durable authority");
  check(state.probability_one("101") == (rb.ok ? 1.0 : 0.0),
        "second writer's resulting bit agrees with durable authority");
  check(manager.telemetry().completed == 2, "all conflicting requests resolved");
}

}  // namespace

int main() {
  try {
    admission_tests();
    aging_tests();
    native_state_tests();
    forced_conflict_tests(4, true);
    forced_conflict_tests(0, false);
    std::cout << "{\"status\":\"PASS\",\"checks\":" << checks
              << ",\"engine\":\"original-XI\",\"authority\":\"InfiniteSession\","
                 "\"workers_tested\":4,\"conflict_exercised\":true}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "InfiniteCore C02 FAILED: " << e.what() << '\n';
    return 1;
  }
}
