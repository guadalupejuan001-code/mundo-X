#include "mundo_x/infinite_core.hpp"
#include "mundo_x/quantum_runtime.hpp"

#include <iostream>
#include <stdexcept>
#include <vector>

namespace {
using Core = mundo_x::InfiniteCore;
using Gate = Core::Gate;
unsigned checks = 0;
void check(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

void equivalent(const Core& left, const Core& right) {
  // Exhaust the four-site Pauli basis, including signed Y correlations.
  for (unsigned word = 0; word < 256; ++word) {
    std::vector<Core::Term> terms;
    for (unsigned site = 0; site < 4; ++site) {
      const char axis = "IXYZ"[(word >> (site * 2)) & 3U];
      if (axis != 'I') terms.push_back({std::to_string(site), axis});
    }
    check(left.expectation(terms) == right.expectation(terms),
          "reduced finite gates changed a Pauli expectation");
  }
}
}

int main() {
  try {
    Core::Limits no_materialization;
    no_materialization.max_active_qubits = 0;
    Core reduced(Core::Background::Zero, no_materialization);
    reduced.apply({{'H', "00", {}}, {'X', "1", {}},
                   {'H', "0", {}}, {'X', "01", {}}});
    auto work = reduced.gate_work_stats();
    check(work.input_gates == 4 && work.cancelled_gates == 4 &&
          work.executed_gates == 0, "independent inverse gates were executed");
    check(reduced.stats().active_qubits == 0 && reduced.stats().revision == 1,
          "elimination changed the nonempty-batch revision contract");
    const auto saved = reduced.snapshot();
    bool rejected = false;
    try {
      reduced.apply({{'H', "0", {}}, {'H', "0", {}}, {'T', "0", {}}});
    } catch (const std::invalid_argument&) { rejected = true; }
    check(rejected && reduced.snapshot() == saved,
          "elimination bypassed invalid-tail validation or changed state");

    const std::vector<Gate> gates{
      {'H', "0", {}}, {'H', "1", {}}, {'H', "0", {}}, {'H', "1", {}},
      {'C', "0", "1"}, {'Y', "3", {}}, {'C', "0", "1"}, {'Y', "3", {}},
      // Overlap blocks cancellation; S is not an involution; CX is directed.
      {'H', "0", {}}, {'S', "0", {}}, {'H', "0", {}},
      {'S', "1", {}}, {'S', "1", {}},
      {'C', "0", "1"}, {'C', "1", "0"},
      {'D', "0", "2"}, {'X', "2", {}}, {'D', "0", "2"}};
    for (const auto background : {Core::Background::Zero, Core::Background::BellPairs}) {
      for (const bool framed : {false, true}) {
        Core batch(background), sequential(background);
        if (framed) {
          batch.h_all(); batch.cz_neighbors();
          sequential.h_all(); sequential.cz_neighbors();
        }
        batch.apply(gates);
        for (const auto& gate : gates) sequential.apply({gate});
        equivalent(batch, sequential);
        check(batch.gate_work_stats().cancelled_gates == 8,
              "dependency cancellation crossed a touched operand");
      }
    }

    Core cached, sequential;
    cached.h_all(); sequential.h_all();
    const auto frame = cached.snapshot();
    cached.apply({{'S', "0", {}}, {'S', "0", {}}, {'S', "0", {}}});
    for (unsigned i = 0; i < 3; ++i) sequential.apply({{'S', "0", {}}});
    work = cached.gate_work_stats();
    check(work.pullback_builds == 1 && work.pullback_hits == 2,
          "the same frame image was rebuilt within a batch");
    equivalent(cached, sequential);
    cached.restore(frame);
    cached.cz_neighbors();
    sequential.restore(frame);
    sequential.cz_neighbors();
    cached.apply({{'S', "0", {}}});
    sequential.apply({{'S', "0", {}}});
    check(cached.gate_work_stats().pullback_builds == 1 &&
          cached.gate_work_stats().pullback_hits == 0,
          "a new batch reused an image from a restored/different frame");
    equivalent(cached, sequential);

    // XI and Inverse Memory keep using the same core and quantum byte binding.
    mundo_x::QuantumRuntime runtime;
    runtime.execute("begin\nh 100\nh 101\nh 100\nh 101\nend\n"
                    "mem_bind 7 100\nmem_store 7 165\n");
    const auto byte = runtime.memory().read("7", runtime.core());
    check(byte.deterministic && byte.value == 165,
          "XI reduced batch changed the inverse-memory quantum byte");
    check(runtime.core().stats().active_qubits == 4,
          "cancelled XI gates left materialized intermediate sites");

    mundo_x::InverseMemory::Limits memory_limits;
    memory_limits.max_address_digits = 2;
    mundo_x::InverseMemory background(173, memory_limits);
    Core zero;
    const auto projection = background.materialize("90", 10, zero);
    check(projection == std::vector<std::uint8_t>(10, 173),
          "direct rule projection changed classical bytes");
    check(background.stats().overrides == 0 && background.stats().revision == 0,
          "direct projection materialized persistent memory");
    rejected = false;
    try { (void)background.materialize("99", 2, zero); }
    catch (const std::length_error&) { rejected = true; }
    check(rejected, "direct projection skipped final-address overflow");
    rejected = false;
    try { (void)background.materialize("bad", 0, zero); }
    catch (const std::exception&) { rejected = true; }
    check(rejected, "empty projection skipped address validation");
    check(background.materialize("99", 0, zero).empty(),
          "empty direct projection changed the zero-count contract");
    background.store("91", 11, zero);
    check(background.materialize("90", 3, zero) ==
          std::vector<std::uint8_t>({173, 11, 173}),
          "an override incorrectly used the direct-background projection");
    std::cout << "PASS ether_next_quantum " << checks << " checks\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << error.what() << '\n';
    return 1;
  }
}
