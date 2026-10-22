#include <chrono>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <string>
#include "mundo_x/adaptive_phase_core.hpp"

struct Gate { char op; std::size_t a, b = 0; double theta = 0.; };
using Clock = std::chrono::steady_clock;

template<class Core> void apply(Core& state, const Gate& g) {
  switch (g.op) {
    case 'H': state.hadamard(g.a); break;
    case 'X': state.pauli_x(g.a); break;
    case 'Z': state.pauli_z(g.a); break;
    case 'S': state.phase_s(g.a); break;
    case 'T': state.phase_t(g.a); break;
    case 'P': state.phase(g.a, g.theta); break;
    case 'C': state.cnot(g.a, g.b); break;
    case 'D': state.controlled_z(g.a, g.b); break;
    case 'Q': state.controlled_phase(g.a, g.b, g.theta); break;
    default: throw std::invalid_argument("unknown gate");
  }
}

template<class Core> std::vector<double> observables(const Core& state) {
  std::vector<double> out;
  out.reserve(3*state.qubits());
  for (std::size_t q = 0; q < state.qubits(); ++q)
    for (char axis : {'X', 'Y', 'Z'}) out.push_back(state.pauli_expectation(axis, q));
  return out;
}

void array(const std::vector<double>& values) {
  std::cout << '[';
  for (std::size_t i = 0; i < values.size(); ++i) {
    if (i) std::cout << ',';
    std::cout << values[i];
  }
  std::cout << ']';
}

int main(int argc, char** argv) {
  try {
    if (argc < 3 || argc > 5)
      throw std::invalid_argument("usage: mundo_x_phase PROGRAM adaptive|adaptive-replay|reduced [REPS] [dump]");
    const std::string backend = argv[2];
    const int reps = argc >= 4 ? std::stoi(argv[3]) : 1;
    const bool dump = argc == 5 && std::string(argv[4]) == "dump";
    if (reps < 1 || (backend != "adaptive" && backend != "adaptive-replay" && backend != "reduced"))
      throw std::invalid_argument("invalid backend or repetitions");
    std::ifstream input(argv[1]);
    std::size_t n;
    if (!(input >> n) || n == 0 ||
        n > mundo_x::ReducedExactTRepresentation::kMaxQubits)
      throw std::invalid_argument("invalid qubit count");
    std::string line;
    std::getline(input, line);
    std::vector<Gate> gates;
    while (std::getline(input, line)) {
      if (line.empty() || line[0] == '#') continue;
      std::istringstream in(line);
      Gate g;
      if (!(in >> g.op >> g.a)) throw std::invalid_argument("invalid gate line");
      if ((g.op == 'C' || g.op == 'D' || g.op == 'Q') && !(in >> g.b))
        throw std::invalid_argument("missing target");
      if ((g.op == 'P' || g.op == 'Q') && !(in >> g.theta))
        throw std::invalid_argument("missing phase angle");
      std::string extra;
      if (in >> extra) throw std::invalid_argument("extra gate arguments");
      gates.push_back(g);
    }
    if (!input.eof()) throw std::runtime_error("cannot read program");
    std::unique_ptr<mundo_x::AdaptivePhaseCore> adaptive;
    std::unique_ptr<mundo_x::ReducedExactTRepresentation> reduced;
    std::vector<double> elapsed, evolution, obs;
    std::size_t peak_components = 1;
    for (int rep = (reps > 1 ? -3 : 0); rep < reps; ++rep) {
      adaptive.reset(); reduced.reset();
      const auto start = Clock::now();
      if (backend != "reduced") {
        using Core = mundo_x::AdaptivePhaseCore;
        adaptive = std::make_unique<Core>(n, 1e-14, 65536,
            backend == "adaptive-replay" ? Core::Transition::Replay : Core::Transition::Transfer);
        for (const auto& g : gates) apply(*adaptive, g);
      } else {
        reduced = std::make_unique<mundo_x::ReducedExactTRepresentation>(n);
        for (const auto& g : gates) {
          apply(*reduced, g);
          peak_components = std::max(peak_components, reduced->component_count());
          if (reduced->component_count() > 65536)
            throw std::length_error("reduced component budget exceeded");
        }
      }
      const auto evolved = Clock::now();
      obs = adaptive ? observables(*adaptive) : observables(*reduced);
      const auto read = Clock::now();
      if (rep >= 0) {
        evolution.push_back(std::chrono::duration<double,std::micro>(evolved-start).count());
        elapsed.push_back(std::chrono::duration<double,std::micro>(read-start).count());
      }
    }
    std::cout << std::setprecision(17) << "{\"status\":\"ok\",\"backend\":\"" << backend
              << "\",\"qubits\":" << n << ",\"gates\":" << gates.size()
              << ",\"product\":" << (adaptive && adaptive->is_product() ? "true" : "false")
              << ",\"product_residual_sum\":" << (adaptive ? adaptive->product_residual_sum() : 0.)
              << ",\"history_gates\":" << (adaptive ? adaptive->history_gate_count() : 0)
              << ",\"replayed_gates\":" << (adaptive ? adaptive->replayed_gate_count() : 0)
              << ",\"transferred_qubits\":" << (adaptive ? adaptive->transferred_qubit_count() : 0)
              << ",\"components\":" << (adaptive ? adaptive->component_count() : reduced->component_count())
              << ",\"peak_components\":" << (adaptive ? adaptive->peak_component_count() : peak_components)
              << ",\"logical_payload_bytes\":" << (adaptive ? adaptive->logical_payload_bytes() : reduced->logical_payload_bytes())
              << ",\"norm_squared\":" << (adaptive ? adaptive->norm_squared() : reduced->coefficient_norm_squared())
              << ",\"init_evolution_us\":";
    array(evolution); std::cout << ",\"init_evolution_readout_us\":"; array(elapsed);
    std::cout << ",\"pauli_xyz\":"; array(obs);
    if (dump) {
      const auto state = adaptive ? adaptive->dense_state() : reduced->dense_from_reduced();
      std::cout << ",\"state\":[";
      for (std::size_t i = 0; i < state.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << '[' << state[i].real() << ',' << state[i].imag() << ']';
      }
      std::cout << ']';
    }
    std::cout << "}\n";
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "phase simulation failed: " << e.what() << '\n';
    return 2;
  }
}
