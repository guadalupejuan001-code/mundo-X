#include "mundo_x/inverse_memory.hpp"

#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using mundo_x::InfiniteCore;
using mundo_x::InverseMemory;
std::size_t checks = 0;

void need(bool value, const char* message) {
  ++checks;
  if (!value) throw std::runtime_error(message);
}

template<class F>
void rejects(F action, const char* message) {
  bool failed = false;
  try { action(); } catch (const std::exception&) { failed = true; }
  need(failed, message);
}
}

int main() {
  try {
    InfiniteCore core;
    InverseMemory memory;
    const std::string far = std::string(999, '9') + "7";

    auto value = memory.read(far, core);
    need(value.deterministic && value.value == 0, "rule background did not answer huge address");
    need(core.stats().active_qubits == 0, "classical rule read materialized qubits");

    memory.store(far, 173, core);
    value = memory.read(far, core);
    need(value.deterministic && value.value == 173, "finite override changed value");
    need(memory.stats().overrides == 1, "finite override was not recorded");
    need(core.stats().active_qubits == 0, "classical override used quantum storage");

    memory.store(far, 0, core);
    need(memory.stats().overrides == 0, "return to background retained classical state");

    memory.bind_quantum_byte("42", "1000", core);
    core.apply({{'X',"1000",{}},{'X',"1002",{}},{'X',"1007",{}}});
    value = memory.read("42", core);
    need(value.deterministic && value.value == 0x85, "explicit quantum byte projection changed bits");

    memory.store("42", 0x5a, core);
    value = memory.read("42", core);
    need(value.deterministic && value.value == 0x5a, "bound classical store failed");
    need(memory.stats().quantum_bindings == 1, "quantum binding disappeared");

    const auto core_before_superposition = core.snapshot();
    core.apply({{'H',"1000",{}}});
    value = memory.read("42", core);
    need(!value.deterministic && value.handle == "qbyte:42@1000",
         "superposed byte was exposed as classical");
    const auto core_quantum = core.snapshot();
    rejects([&] { memory.store("42", 3, core); },
            "classical store silently collapsed a quantum byte");
    need(core.snapshot() == core_quantum, "rejected quantum store changed the core");
    rejects([&] { (void)memory.materialize("42", 1, core); },
            "materialization silently collapsed a quantum byte");

    memory.unbind_quantum_byte("42");
    value = memory.read("42", core);
    need(value.deterministic && value.value == 0,
         "unbind did not reveal the rule-defined classical byte");

    memory.store("100", 1, core);
    memory.store("101", 2, core);
    memory.store("102", 3, core);
    const auto bytes = memory.materialize("100", 3, core);
    need(bytes == std::vector<std::uint8_t>({1,2,3}),
         "bounded materialization changed classical bytes");

    const auto saved = memory.snapshot();
    const auto restored = InverseMemory::from_snapshot(saved, memory.limits());
    need(restored.snapshot() == saved, "Inverse Memory snapshot did not round-trip");

    memory.reset(255);
    need(memory.stats().background_byte == 255 && memory.stats().overrides == 0 &&
         memory.stats().quantum_bindings == 0, "global rule reset retained finite state");
    need(memory.read("999999999999999999999", core).value == 255,
         "global rule reset did not cover the logical domain");

    InverseMemory::Limits tiny;
    tiny.max_overrides = 1;
    tiny.max_quantum_bindings = 1;
    tiny.max_materialize_bytes = 2;
    InverseMemory bounded(0, tiny);
    bounded.store("1", 1, core);
    rejects([&] { bounded.store("2", 2, core); }, "override budget ignored");
    bounded.bind_quantum_byte("3", "3000", core);
    rejects([&] { bounded.bind_quantum_byte("4", "4000", core); }, "binding budget ignored");
    rejects([&] { (void)bounded.materialize("0", 3, core); }, "projection budget ignored");

    need(core_before_superposition != core_quantum, "H did not change the tested quantum state");

    // Copy-on-write: even a large store must be cheap to copy for a read-only
    // transaction, and writes to any copy must not affect the other owners.
    InverseMemory populated;
    for (unsigned i = 0; i < 1024; ++i)
      populated.store(std::to_string(i), static_cast<std::uint8_t>(1 + i % 254), core);
    populated.bind_quantum_byte("5000", "6000", core);
    const auto original = populated.snapshot();
    std::vector<InverseMemory> readers(64, populated);
    for (auto& reader : readers) {
      need(reader.read("42", core).deterministic && reader.read("42", core).value == 43,
           "read-only copy lost a classical override");
      need(reader.read("5000", core).deterministic, "read-only copy lost quantum binding");
    }
    readers[0].store("42", 200, core);
    readers[1].bind_quantum_byte("5000", "7000", core);
    readers[2].unbind_quantum_byte("5000");
    readers[3].reset(9);
    need(populated.snapshot() == original, "one writer changed the shared parent");
    need(readers[4].snapshot() == original, "one writer changed another shared reader");
    need(readers[0].read("42", core).value == 200 &&
         populated.read("42", core).value == 43, "classical map did not detach");
    need(readers[1].stats().quantum_bindings == 1 &&
         readers[1].snapshot() != original, "rebind did not detach quantum map");
    need(readers[2].stats().quantum_bindings == 0, "unbind did not detach quantum map");
    need(readers[3].read("42", core).value == 9 &&
         readers[3].stats().overrides == 0, "reset did not abandon shared maps");
    const auto no_op_revision = readers[0].stats().revision;
    readers[0].store("42", 200, core);
    need(readers[0].stats().revision == no_op_revision, "no-op store changed revision");

    // Shards must not change the byte-for-byte sorted MXIM1 wire format.
    InverseMemory ordered;
    for (const unsigned id : {200U, 10U, 1U, 100U, 2U})
      ordered.store(std::to_string(id), static_cast<std::uint8_t>(id), core);
    need(ordered.snapshot() ==
         "MXIM1 0\nO 1 1\nO 2 2\nO 10 10\nO 100 100\nO 200 200\n",
         "sharded snapshot lost numeric address ordering");
    need(InverseMemory::from_snapshot(ordered.snapshot()).snapshot() == ordered.snapshot(),
         "sharded snapshot failed exact restoration");

    InverseMemory::Limits short_address;
    short_address.max_address_digits = 3;
    rejects([&] {
      (void)InverseMemory::from_snapshot("MXIM1 0\nQ 7 999\n", short_address);
    }, "snapshot accepted a quantum byte crossing the address limit");

    std::cout << "{\"suite\":\"inverse_memory\",\"checks\":" << checks
              << ",\"rule_domain\":\"decimal-unbounded\","
              << "\"implicit_byte_array\":false,"
              << "\"hidden_measurement\":false,"
              << "\"failures\":0}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL Inverse Memory: " << error.what() << '\n';
    return 1;
  }
}
