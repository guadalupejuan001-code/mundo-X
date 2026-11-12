#include "../pc_machine.hpp"

#include <iostream>
#include <stdexcept>
#include <string>

namespace {
unsigned checks = 0;
void need(bool condition, const char* why) {
  ++checks;
  if (!condition) throw std::runtime_error(why);
}
std::string guest(cosmic_pc::PcMachine& machine, const std::string& xi) {
  auto& device = machine.quantum_device;
  device.clear();
  for (unsigned char c : xi)
    need(device.output(cosmic_pc::PcQuantumDevice::data_port, 1, c),
         "real guest port accepts the byte");
  need(device.output(cosmic_pc::PcQuantumDevice::control_port, 1, 2),
       "guest submission executed on the port");
  need(!(device.status() & cosmic_pc::PcQuantumDevice::request_error),
       "guest operation is valid through scheduler");
  std::string result;
  while (device.remaining()) {
    auto c = device.input(cosmic_pc::PcQuantumDevice::data_port, 1);
    need(bool(c), "guest response reads original port FIFO");
    result.push_back(char(*c));
  }
  return result;
}
}

int main() {
  try {
    cosmic_pc::PcMachine pc;
    const auto original = pc.quantum_snapshot();
    need(!pc.quantum_device.uses_scheduler(), "original PC path stays default");

    pc.quantum_autonomous_enable(true);
    need(pc.quantum_device.uses_scheduler(), "C01 now serves the actual guest port");
    pc.quantum_command("h 0\ncx 0 1\n");
    need(pc.quantum_json().find("\"scheduler_c01\":true") != std::string::npos,
         "the real UI JSON declares opt-in C01 enabled");
    need(pc.quantum_session->probability_one("0") == 0.5,
         "host XI was committed on original shared InfiniteSession");
    const auto xx = guest(pc, "expect X:0 X:1\n");
    need(xx.find("1") != std::string::npos, "guest sees the same entangled state");

    const auto before = pc.quantum_snapshot();
    pc.quantum_workers_benchmark("x 345\nprobz 345\n");
    need(pc.quantum_json().find("\"error\":\"\"") != std::string::npos,
         "isolated benchmark succeeded");
    need(pc.quantum_json().find("workers aislado") == std::string::npos,
         "human readable benchmark keeps concise output");
    need(pc.quantum_json().find("4 workers") != std::string::npos,
         "Android UI receives the C02 four-worker measurement");
    need(pc.quantum_snapshot() == before,
         "C02 copies never changed the live guest and graphics state");

    pc.quantum_workers_benchmark("x 456\nassert probz 456 0\n");
    need(pc.quantum_json().find("\"error\":\"\"") == std::string::npos,
         "native XI assertion failure is not silently accepted by C02");
    need(pc.quantum_snapshot() == before, "C02 XI errors never mutate the live owner");
    const auto next = guest(pc, "z 0\nexpect X:0 X:1\n");
    need(next.find("-1") != std::string::npos, "guest writes after benchmark still work");
    pc.quantum_autonomous_enable(false);
    need(!pc.quantum_device.uses_scheduler(), "C01 can be disabled without state reset");
    need(pc.quantum_json().find("\"scheduler_c01\":false") != std::string::npos,
         "new status is visible to the PC panel");
    pc.quantum_command("z 0\n");
    need(pc.quantum_json().find("\"error\":\"\"") != std::string::npos,
         "fallback direct execution continues after C01 is disabled");
    need(pc.quantum_snapshot() != original,
         "real PC quantum core still has the previously committed state");

    std::cout << "{\"suite\":\"pc_autonomous_integration\",\"checks\":"
              << checks << ",\"guest_shared_state\":true,"
              << "\"c01_optional\":true,\"c02_isolated\":true}\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL PC autonomy integration: " << error.what() << '\n';
    return 1;
  }
}
