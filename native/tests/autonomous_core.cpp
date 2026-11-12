#include "mundo_x/autonomous_core.hpp"

#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Scheduler = mundo_x::AutonomousCore;
using Session = mundo_x::InfiniteSession;
std::size_t assertions = 0;

void check(bool condition, const char* description) {
  ++assertions;
  if (!condition) throw std::runtime_error(description);
}
template<class F>
void expect_failure(F fn, const char* description) {
  bool threw = false;
  try { fn(); } catch (const std::exception&) { threw = true; }
  check(threw, description);
}
}

int main() {
  try {
    auto original = std::make_shared<Session>();
    check(original->stats().revision == 0, "original state starts pristine");

    {
      Scheduler manager(original);
      const auto bell = manager.submit("h 0\ncx 0 1\nexpect X:0 X:1\n",
                                       Scheduler::Priority::Interactive).get();
      check(bell.ok, "accepted XI task actually executed");
      check(bell.output.find("1") != std::string::npos, "real XI observable returned");
      check(bell.epoch == original->epoch(), "same owner epoch");

      const auto before = original->snapshot();
      const auto failed = manager.submit("assert probz 0 1\n").get();
      check(!failed.ok && !failed.error.empty(), "failed task reported without faking success");
      check(original->snapshot() == before, "failed XI task rolled back entire state and RNG");
      check(manager.telemetry().failed == 1, "scheduler failure accounting");

      const auto query = manager.submit("probz 1000000000\n",
                                       Scheduler::Priority::Background).get();
      check(query.ok, "background task completes");
      check(original->snapshot() == before, "read-only query does not mutate state");

      std::vector<std::thread> submitters;
      std::atomic<std::size_t> successful{0};
      for (unsigned thread = 0; thread < 4; ++thread) {
        submitters.emplace_back([&] {
          for (unsigned index = 0; index < 8; ++index) {
            try {
              const auto result = manager.submit("expect Z:999999999\n",
                                                  Scheduler::Priority::Normal).get();
              if (result.ok) ++successful;
            } catch (const std::exception&) {
              // A saturated bounded queue rejects admission rather than corrupting state.
            }
          }
        });
      }
      for (auto& thread : submitters) thread.join();
      check(successful > 0, "concurrent submitters have finished real XI calls");
      check(original->snapshot() == before, "parallel admission never split the state owner");
      auto telemetry = manager.telemetry();
      check(telemetry.accepted >= 3 + successful, "accepted tasks accounted for");
      check(telemetry.completed == telemetry.accepted, "no accepted job silently lost");
      check(telemetry.failed == 1, "failed task isolated");

      expect_failure([&] {
        manager.submit(std::string(1024 * 1024 + 1, 'x'));
      }, "unbounded request is rejected at admission");

      manager.shutdown();
      check(manager.telemetry().stopped, "shutdown changes scheduler state");
      expect_failure([&] { manager.submit("stats\n"); }, "new work rejected after drain");
    }

    check(original->stats().revision != 0, "original owner retained its committed state");
    check(original->probability_one("0") == 0.5, "state survives manager destruction");

    {
      Scheduler::Limits limits;
      limits.max_queued = 2;
      limits.max_program_bytes = 4;
      limits.max_queued_bytes = 8;
      Scheduler bounded(original, limits);
      expect_failure([&] { bounded.submit("probz 0\n"); },
                     "small explicit transport budget enforced");
      const auto valid = bounded.submit("stats").get();
      check(valid.ok, "bounded scheduler continues serving valid requests");
    }
    std::cout << "{\"status\":\"pass\",\"checks\":" << assertions
              << ",\"execution\":\"original-xi\",\"state_owner\":\"shared-InfiniteSession\"}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "autonomous C01 FAILED: " << error.what() << '\n';
    return 1;
  }
}
