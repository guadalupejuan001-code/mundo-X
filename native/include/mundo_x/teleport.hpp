#pragma once

#include <cstddef>
#include <sstream>
#include <string>

#include "mundo_x/compact_core.hpp"

namespace mundo_x {

struct TeleportSummary {
  int passed = 0;
  int total = 0;
  std::size_t state_bytes = 0;
  bool all_passed = false;
  std::string report;
};

inline void prepare_teleport_source(CompactCore& q, int state_id) {
  switch (state_id) {
    case 0:  // |0>  stabilized by +Z
      break;
    case 1:  // |1>  stabilized by -Z
      q.pauli_x(0);
      break;
    case 2:  // |+>  stabilized by +X
      q.hadamard(0);
      break;
    case 3:  // |->  stabilized by -X
      q.hadamard(0);
      q.pauli_z(0);
      break;
    case 4:  // |+i> stabilized by +Y
      q.hadamard(0);
      q.phase_s(0);
      break;
    case 5:  // |-i> stabilized by -Y
      q.hadamard(0);
      q.phase_s(0);
      q.pauli_z(0);
      break;
    default:
      throw std::invalid_argument("invalid teleport source state");
  }
}

inline TeleportSummary run_compact_teleportation() {
  static const char* names[6] = {
      "|0>", "|1>", "|+>", "|->", "|+i>", "|-i>"};
  static const char expected_pauli[6] = {
      'Z', 'Z', 'X', 'X', 'Y', 'Y'};
  static const bool expected_negative[6] = {
      false, true, false, true, false, true};

  TeleportSummary summary;
  std::ostringstream out;
  out << "COMPACT QUANTUM TELEPORTATION\n"
      << "source=q0  entangled_pair=q1-q2  destination=q2\n"
      << "protocol=CNOT(0,1), H(0), measure q0/q1, "
         "X(q2)^m1, Z(q2)^m0\n\n";

  for (int state = 0; state < 6; ++state) {
    for (int m0 = 0; m0 <= 1; ++m0) {
      for (int m1 = 0; m1 <= 1; ++m1) {
        CompactCore q(3);
        prepare_teleport_source(q, state);

        // Bell resource shared between q1 and q2.
        q.hadamard(1);
        q.cnot(1, 2);

        // Bell measurement circuit between source q0 and q1.
        q.cnot(0, 1);
        q.hadamard(0);

        const bool random0 = q.measure_z_forced(0, m0 != 0);
        const bool random1 = q.measure_z_forced(1, m1 != 0);

        // Classical feed-forward corrections at the destination.
        if (m1) q.pauli_x(2);
        if (m0) q.pauli_z(2);

        bool negative = false;
        const char p = q.single_qubit_stabilizer(2, &negative);
        const bool ok =
            random0 && random1 &&
            p == expected_pauli[state] &&
            negative == expected_negative[state];

        ++summary.total;
        if (ok) ++summary.passed;
        summary.state_bytes = q.state_bytes();

        out << names[state]
            << " branch=" << m0 << m1
            << " destination="
            << (negative ? "-" : "+") << p
            << " " << (ok ? "PASS" : "FAIL") << "\n";
      }
    }
  }

  summary.all_passed = summary.passed == summary.total;
  out << "\npassed=" << summary.passed << "/" << summary.total << "\n"
      << "compact_state_bytes=" << summary.state_bytes << "\n"
      << "result=" << (summary.all_passed ? "PASS" : "FAIL") << "\n"
      << "meaning=the quantum STATE is teleported; no physical particle or "
         "matter is transported\n";

  summary.report = out.str();
  return summary;
}

}  // namespace mundo_x
