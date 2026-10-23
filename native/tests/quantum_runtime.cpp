#include "mundo_x/quantum_runtime.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
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
    mundo_x::QuantumRuntime runtime;

    rejects([&] { runtime.execute("qload8 0\n"); },
            "legacy qload8 remained enabled in QuantumRuntime");
    rejects([&] { runtime.execute("qstore8 0 1\n"); },
            "legacy qstore8 remained enabled in QuantumRuntime");

    const auto out = runtime.execute(
        "mem_store 1000000000000000000000000 7\n"
        "mem_read 1000000000000000000000000\n"
        "mem_bind 9 100\n"
        "x 100\n"
        "x 102\n"
        "mem_read 9\n"
        "mem_stats\n");
    need(out.find("\"address\":\"1000000000000000000000000\",\"deterministic\":true,\"value\":7") != std::string::npos,
         "XI did not expose the rule-defined byte");
    need(out.find("\"address\":\"9\",\"deterministic\":true,\"value\":5") != std::string::npos,
         "XI did not project the explicitly bound quantum byte");
    need(out.find("\"model\":\"inverse-ether-memory\"") != std::string::npos,
         "XI memory stats lost model identity");

    const auto core_saved = runtime.core_snapshot();
    const auto memory_saved = runtime.memory_snapshot();
    rejects([&] {
      runtime.execute(
          "mem_store 77 33\n"
          "x 900\n"
          "this_command_does_not_exist\n");
    }, "invalid XI program committed partially");
    need(runtime.core_snapshot() == core_saved, "failed XI program changed quantum state");
    need(runtime.memory_snapshot() == memory_saved, "failed XI program changed inverse memory");

    const auto quantum = runtime.execute(
        "mem_bind 77 200\n"
        "h 200\n"
        "mem_read 77\n");
    need(quantum.find("\"deterministic\":false") != std::string::npos,
         "XI read silently classicalized a quantum byte");
    need(quantum.find("\"handle\":\"qbyte:77@200\"") != std::string::npos,
         "XI read lost the quantum handle");

    const auto quantum_core = runtime.core_snapshot();
    const auto quantum_memory = runtime.memory_snapshot();
    rejects([&] { runtime.execute("mem_materialize 77 1\n"); },
            "XI materialization silently measured a quantum byte");
    need(runtime.core_snapshot() == quantum_core, "failed projection changed quantum state");
    need(runtime.memory_snapshot() == quantum_memory, "failed projection changed memory metadata");

    runtime.execute("mem_unbind 77\nmem_store 77 44\n");
    need(runtime.memory_snapshot() != quantum_memory, "memory mutation was not committed");
    runtime.restore(quantum_core, quantum_memory);
    need(runtime.core_snapshot() == quantum_core, "quantum restore changed core snapshot");
    need(runtime.memory_snapshot() == quantum_memory, "quantum restore changed memory snapshot");

    const auto material = runtime.execute(
        "mem_unbind 77\n"
        "mem_store 77 44\n"
        "mem_store 78 45\n"
        "mem_materialize 77 2\n");
    need(material.find("\"hex\":\"2c2d\"") != std::string::npos,
         "XI bounded projection changed bytes");

    // Snapshot from a memory owner with a wider address allowance must not
    // install an eight-qubit binding invalid for this core's tighter limit.
    mundo_x::InfiniteCore::Limits core_limits;
    core_limits.max_address_digits = 3;
    mundo_x::QuantumRuntime limited(mundo_x::InfiniteCore::Background::Zero,
                                    core_limits, mundo_x::InverseMemory::Limits{});
    const auto small_core = limited.core_snapshot();
    const auto small_memory = limited.memory_snapshot();
    rejects([&] {
      limited.restore(small_core, "MXIM1 0\nQ 77 999\n");
    }, "restore accepted a binding crossing the core address limit");
    need(limited.core_snapshot() == small_core &&
         limited.memory_snapshot() == small_memory,
         "rejected binding restore committed partial quantum or memory state");

    std::cout << "{\"suite\":\"quantum_runtime\",\"checks\":" << checks
              << ",\"xi_memory_commands\":true,"
              << "\"transactional\":true,"
              << "\"non_quantum_dependencies\":false,"
              << "\"failures\":0}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL QuantumRuntime: " << error.what() << '\n';
    return 1;
  }
}
