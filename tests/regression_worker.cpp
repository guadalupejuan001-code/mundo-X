#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/exact_t_experiment.hpp"

namespace {
struct Gate { char op; std::size_t a, b; };
using Clock = std::chrono::steady_clock;

std::string json_string(const std::string& value) {
    std::string out = "\"";
    const char* hex = "0123456789abcdef";
    for (unsigned char ch : value) {
        if (ch == '\\' || ch == '"') { out += '\\'; out += ch; }
        else if (ch < 0x20) {
            out += "\\u00";
            out += hex[ch >> 4]; out += hex[ch & 15];
        } else out += ch;
    }
    return out + '"';
}

std::size_t number(const char* value) {
    std::string s(value);
    if (s.empty() || s[0] == '-') throw std::invalid_argument("expected unsigned integer");
    std::size_t end = 0;
    auto parsed = std::stoull(s, &end);
    if (end != s.size()) throw std::invalid_argument("invalid integer");
    return static_cast<std::size_t>(parsed);
}

template<class Core>
void apply_circuit(Core& core, const std::vector<Gate>& gates) {
    for (const Gate& gate : gates) {
        switch (gate.op) {
            case 'h': core.hadamard(gate.a); break;
            case 's': core.phase_s(gate.a); break;
            case 'c': core.cnot(gate.a, gate.b); break;
            case 'z': core.controlled_z(gate.a, gate.b); break;
            default: throw std::logic_error("unexpected gate in Clifford circuit");
        }
    }
}

void apply_circuit(mundo_x::ReducedExactTRepresentation& core, const std::vector<Gate>& gates) {
    for (const Gate& gate : gates) {
        switch (gate.op) {
            case 'h': core.hadamard(gate.a); break;
            case 's': core.phase_s(gate.a); break;
            case 'c': core.cnot(gate.a, gate.b); break;
            case 'z': core.controlled_z(gate.a, gate.b); break;
            case 't': core.phase_t(gate.a); break;
            default: throw std::logic_error("unknown gate");
        }
    }
}

void print_times(const std::vector<double>& values) {
    std::cout << '[';
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i) std::cout << ',';
        std::cout << values[i];
    }
    std::cout << ']';
}
}  // namespace

int main(int argc, char** argv) {
    try {
        if (argc != 7 && argc != 8)
            throw std::invalid_argument("usage: native_worker KIND N T K REPS MODE [DUMP_OUTPUT_PATH]");
        const std::string kind = argv[1], mode = argv[6];
        if ((mode == "dump") != (argc == 8))
            throw std::invalid_argument("dump requires an output path; other modes do not accept it");
        const std::size_t n = number(argv[2]), t = number(argv[3]);
        const std::size_t k = number(argv[4]), requested_reps = number(argv[5]);
        if (!n || !requested_reps) throw std::invalid_argument("N and REPS must be positive");
        if (mode != "timing" && mode != "validate" && mode != "inspect" && mode != "dump")
            throw std::invalid_argument("MODE must be timing, validate, inspect or dump");
        const bool is_reduced = kind == "t" || kind == "structured" || kind == "product";
        if (!is_reduced && kind != "ghz" && kind != "cluster")
            throw std::invalid_argument("unknown circuit kind");
        if (kind == "structured" && (!k || k > n))
            throw std::invalid_argument("structured requires 1 <= K <= N");
        // Dump is a separate validation route for the measured structured cases.
        // It never expands an unbounded register or changes timing-mode work.
        if (mode == "dump" && (kind != "structured" || n > 20)) {
            std::cout << "{\"status\":\"unsupported\",\"kind\":" << json_string(kind)
                      << ",\"mode\":\"dump\",\"n\":" << n
                      << ",\"reason\":\"binary dump supports structured circuits with at most 20 qubits; no circuit was evolved\"}\n";
            return 0;
        }
        if (is_reduced && n > 63)
            throw std::invalid_argument("current reduced engine supports at most 63 qubits");
        if (mode == "validate" && is_reduced && n > 12) {
            std::cout << "{\"status\":\"unsupported\",\"kind\":" << json_string(kind)
                      << ",\"mode\":\"validate\",\"n\":" << n
                      << ",\"reason\":\"dense validation is restricted to at most 12 qubits; no circuit was evolved\"}\n";
            return 0;
        }

        std::vector<Gate> gates;
        auto add = [&](char op, std::size_t a, std::size_t b = 0) { gates.push_back({op, a, b}); };
        auto add_t_circuit = [&](std::size_t active) {
            add('h', 0);
            for (std::size_t i = 1; i < active; ++i) {
                add('c', i - 1, i);
                if (i % 2 == 0) add('s', i);
            }
            for (std::size_t j = 0; j < t; ++j) {
                const std::size_t a = j % active, b = (a + 1) % active;
                add('t', a); add('h', a);
                if (active > 1) add('c', a, b);
                if (j % 2) add('s', b);
            }
        };
        std::size_t side = 0;
        if (kind == "ghz") {
            add('h', 0);
            for (std::size_t i = 1; i < n; ++i) add('c', i - 1, i);
        } else if (kind == "cluster") {
            side = static_cast<std::size_t>(std::llround(std::sqrt(static_cast<long double>(n))));
            if (side * side != n) throw std::invalid_argument("cluster requires square N");
            for (std::size_t i = 0; i < n; ++i) add('h', i);
            for (std::size_t r = 0; r < side; ++r) for (std::size_t c = 0; c < side; ++c) {
                const std::size_t v = r * side + c;
                if (c + 1 < side) add('z', v, v + 1);
                if (r + 1 < side) add('z', v, v + side);
            }
        } else if (kind == "t") {
            add_t_circuit(n);
        } else if (kind == "structured") {
            add_t_circuit(k);
            for (std::size_t q = k; q < n; ++q) add('h', q);
            for (std::size_t q = 0; q + 1 < n; ++q) add('c', q, q + 1);
            for (std::size_t q = 0; q + 2 < n; ++q) add('z', q, q + 2);
        } else {
            for (std::size_t q = 0; q < n; ++q) { add('h', q); add('t', q); }
        }

        const std::size_t repetitions = mode == "timing" ? requested_reps : 1;
        const std::size_t warmups = mode == "timing" ? 2 : 0;
        std::vector<double> total_times, evolution_times;
        total_times.reserve(repetitions); evolution_times.reserve(repetitions);
        std::unique_ptr<mundo_x::CompactCore> compact;
        std::unique_ptr<mundo_x::ReducedExactTRepresentation> reduced;
        for (std::size_t rep = 0; rep < warmups + repetitions; ++rep) {
            // Previous-state destruction is outside the allocation/evolution measurement.
            compact.reset(); reduced.reset();
            const auto start = Clock::now();
            if (is_reduced) reduced = std::make_unique<mundo_x::ReducedExactTRepresentation>(n);
            else compact = std::make_unique<mundo_x::CompactCore>(n);
            const auto evolve_start = Clock::now();
            if (is_reduced) apply_circuit(*reduced, gates);
            else apply_circuit(*compact, gates);
            const auto end = Clock::now();
            if (rep >= warmups) {
                total_times.push_back(std::chrono::duration<double, std::micro>(end - start).count());
                evolution_times.push_back(std::chrono::duration<double, std::micro>(end - evolve_start).count());
            }
        }

        std::size_t actual_t_count = 0;
        for (const auto& gate : gates) if (gate.op == 't') ++actual_t_count;
        double dump_norm = 0;
        if (mode == "dump") {
            static_assert(sizeof(double) == 8 && std::numeric_limits<double>::is_iec559,
                          "binary state dump requires IEEE 754 float64");
            // Serialize pairs explicitly, without depending on std::complex layout.
            // This reconstruction and file output are outside both recorded timers.
            const auto state = reduced->dense_from_reduced();
            std::ofstream output(argv[7], std::ios::binary | std::ios::trunc);
            if (!output) throw std::runtime_error("cannot open binary state output");
            for (const auto& amplitude : state) {
                const double pair[2] = {amplitude.real(), amplitude.imag()};
                output.write(reinterpret_cast<const char*>(pair), sizeof(pair));
                dump_norm += std::norm(amplitude);
            }
            output.close();
            if (!output) throw std::runtime_error("failed writing binary state output");
        }
        std::cout << std::setprecision(17)
                  << "{\"status\":\"ok\",\"kind\":" << json_string(kind)
                  << ",\"mode\":" << json_string(mode) << ",\"n\":" << n
                  << ",\"t\":" << t << ",\"k\":" << k
                  << ",\"t_count\":" << actual_t_count
                  << ",\"reps\":" << repetitions << ",\"warmups\":" << warmups
                  << ",\"gate_count\":" << gates.size() << ",\"total_us\":";
        print_times(total_times);
        std::cout << ",\"evolution_us\":";
        print_times(evolution_times);
        std::cout << ",\"logical_bytes\":"
                  << (is_reduced ? reduced->logical_payload_bytes() : compact->state_bytes());
        if (is_reduced) std::cout << ",\"components\":" << reduced->component_count();

        if (mode == "dump") {
            const unsigned short endian_probe = 1;
            const bool little_endian = *reinterpret_cast<const unsigned char*>(&endian_probe) == 1;
            std::cout << ",\"norm\":" << dump_norm
                      << ",\"state_file\":" << json_string(argv[7])
                      << ",\"state_format\":\"interleaved-real-imag-float64\""
                      << ",\"state_byte_order\":" << json_string(little_endian ? "little" : "big")
                      << ",\"state_amplitudes\":" << (std::size_t{1} << n);
        } else if (mode == "validate" && is_reduced) {
            const auto state = reduced->dense_from_reduced();
            double norm = 0;
            for (const auto& amplitude : state) norm += std::norm(amplitude);
            std::cout << ",\"norm\":" << norm << ",\"state\":[";
            for (std::size_t i = 0; i < state.size(); ++i) {
                if (i) std::cout << ',';
                std::cout << '[' << state[i].real() << ',' << state[i].imag() << ']';
            }
            std::cout << ']';
        } else if (mode == "validate") {
            const bool signature = kind == "ghz" ? compact->is_ghz_signature()
                                                   : compact->is_cluster_signature(side, side);
            std::cout << ",\"signature_pass\":" << (signature ? "true" : "false")
                      << ",\"generators\":[";
            for (std::size_t i = 0; i < n; ++i) {
                if (i) std::cout << ',';
                std::cout << json_string(compact->generator_string(i));
            }
            std::cout << ']';
        } else {
            std::cout << ",\"norm_not_checked\":true";
        }
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cout << "{\"status\":\"error\",\"error\":" << json_string(error.what()) << "}\n";
        return 2;
    }
}
