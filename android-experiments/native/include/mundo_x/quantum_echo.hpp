#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <limits>
#include <locale>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "mundo_x/exact_t_experiment.hpp"

namespace mundo_x {
namespace quantum_echo {

constexpr int circuit_version = 1;
constexpr double tolerance = 1e-8;

struct Config {
    std::size_t rows = 2;
    std::size_t cols = 3;
    std::size_t depth = 3;
    std::uint64_t seed = 7;
    std::size_t perturb = 0;
    std::size_t probe = 1;
    std::size_t rounds = 1;
    std::size_t max_components = 4096;
    bool compare_dense = true;
};

struct Metrics {
    bool available = false;
    double z = 0;
    double norm = 0;
    double elapsed_us = 0;
    std::size_t peak_components = 0;
    std::size_t final_components = 0;
    std::size_t logical_payload_bytes = 0;
    std::size_t operations = 0;
};

struct Result {
    Config config;
    std::string status = "ERROR";
    std::string validation = "UNVERIFIED";
    std::string message;
    Metrics echo;
    Metrics control;
    Metrics dense_echo;
    Metrics dense_control;
    std::size_t gates_u = 0;
    std::size_t t_gates_u = 0;
    bool reference_performed = false;
    bool reference_ok = false;
    bool max_error_available = false;
    double max_error = 0;
};

namespace detail {
using Clock = std::chrono::steady_clock;
using Cancel = std::function<bool()>;
using Complex = std::complex<double>;

enum class Op { H, S, SDagger, T, TDagger, X, Z, CX, CZ };
struct Gate {
    Op op;
    std::size_t a;
    std::size_t b = 0;
    bool operator==(const Gate& other) const {
        return op == other.op && a == other.a && b == other.b;
    }
};
using Circuit = std::vector<Gate>;

inline void check_cancel(const Cancel& cancel) {
    if (cancel && cancel()) throw std::runtime_error("quantum echo cancelled");
}

inline void validate_config(const Config& config) {
    if (!config.rows || !config.cols || config.rows > 63 || config.cols > 63 ||
        config.rows * config.cols > 63)
        throw std::invalid_argument("La cuadrícula debe tener entre 1 y 63 qubits.");
    if (config.depth > 30)
        throw std::invalid_argument("La profundidad debe estar entre 0 y 30.");
    if (config.rounds != 1 && config.rounds != 2)
        throw std::invalid_argument("El número de rondas debe ser 1 o 2.");
    const auto n = config.rows * config.cols;
    if (config.perturb >= n || config.probe >= n)
        throw std::invalid_argument("Perturbación y sonda deben estar dentro de la cuadrícula.");
    if (!config.max_components)
        throw std::invalid_argument("El presupuesto de componentes debe ser positivo.");
}

// Fixed integer PRNG and gate ordering make circuit_version=1 portable across
// platforms. The seed changes local mixing and the direction of nearest-neighbor CX.
class SplitMix64 {
 public:
    explicit SplitMix64(std::uint64_t seed) : value_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (value_ += UINT64_C(0x9e3779b97f4a7c15));
        z = (z ^ (z >> 30)) * UINT64_C(0xbf58476d1ce4e5b9);
        z = (z ^ (z >> 27)) * UINT64_C(0x94d049bb133111eb);
        return z ^ (z >> 31);
    }
 private:
    std::uint64_t value_;
};

inline Circuit make_circuit(const Config& config) {
    validate_config(config);
    Circuit gates;
    if (config.depth == 0) return gates;  // Identity calibration.
    const auto n = config.rows * config.cols;
    SplitMix64 rng(config.seed);
    std::vector<std::pair<std::size_t, std::size_t>> edges;
    for (std::size_t r = 0; r < config.rows; ++r) {
        for (std::size_t c = 0; c < config.cols; ++c) {
            const auto q = r * config.cols + c;
            if (c + 1 < config.cols) edges.emplace_back(q, q + 1);
            if (r + 1 < config.rows) edges.emplace_back(q, q + config.cols);
        }
    }
    for (std::size_t q = 0; q < n; ++q) gates.push_back({Op::H, q});
    for (std::size_t layer = 0; layer < config.depth; ++layer) {
        for (std::size_t q = 0; q < n; ++q) {
            const auto choice = rng.next();
            if (layer && (choice & 1)) gates.push_back({Op::H, q});
            if ((choice & 2) || q == layer % n) gates.push_back({Op::S, q});
            gates.push_back({Op::T, q});
            if (choice & 4) gates.push_back({Op::H, q});
        }
        for (std::size_t i = 0; i < edges.size(); ++i) {
            const auto& edge = edges[(layer & 1) ? edges.size() - 1 - i : i];
            if ((i + layer) & 1) {
                gates.push_back({Op::CZ, edge.first, edge.second});
            } else if (rng.next() & 1) {
                gates.push_back({Op::CX, edge.second, edge.first});
            } else {
                gates.push_back({Op::CX, edge.first, edge.second});
            }
        }
        for (std::size_t q = 0; q < n; ++q)
            if (rng.next() & 1) gates.push_back({Op::H, q});
    }
    return gates;
}

inline Gate inverse(Gate gate) {
    switch (gate.op) {
        case Op::S: gate.op = Op::SDagger; break;
        case Op::SDagger: gate.op = Op::S; break;
        case Op::T: gate.op = Op::TDagger; break;
        case Op::TDagger: gate.op = Op::T; break;
        default: break;  // H, X, Z, CX, CZ are self-inverse.
    }
    return gate;
}

// Chronological protocol: U, optional X, U†; optional Z_probe; U, optional X, U†.
// The control uses the identical protocol with only X omitted.
template<class Visit>
inline void protocol(const Config& config, const Circuit& gates, bool control,
                     Visit visit, const Cancel& cancel) {
    for (std::size_t round = 0; round < config.rounds; ++round) {
        for (const auto& gate : gates) { check_cancel(cancel); visit(gate); }
        if (!control) { check_cancel(cancel); visit(Gate{Op::X, config.perturb}); }
        for (auto gate = gates.rbegin(); gate != gates.rend(); ++gate) {
            check_cancel(cancel);
            visit(inverse(*gate));
        }
        if (round + 1 < config.rounds) {
            check_cancel(cancel);
            visit(Gate{Op::Z, config.probe});
        }
    }
}

inline Metrics reduced(const Config& config, const Circuit& gates, bool control,
                       const Cancel& cancel) {
    const auto start = Clock::now();
    ReducedExactTRepresentation state(config.rows * config.cols, config.max_components, cancel);
    Metrics out;
    out.peak_components = state.component_count();
    auto observe_size = [&] { out.peak_components = std::max(out.peak_components, state.component_count()); };
    auto visit = [&](const Gate& gate) {
        switch (gate.op) {
            case Op::H: state.hadamard(gate.a); break;
            case Op::S: state.phase_s(gate.a); break;
            case Op::SDagger:
                for (int i = 0; i < 3; ++i) { state.phase_s(gate.a); observe_size(); }
                break;
            case Op::T: state.phase_t(gate.a); break;
            case Op::TDagger: state.phase_t_dagger(gate.a); break;
            case Op::X: state.pauli_x(gate.a); break;
            case Op::Z: state.pauli_z(gate.a); break;
            case Op::CX: state.cnot(gate.a, gate.b); break;
            case Op::CZ: state.controlled_z(gate.a, gate.b); break;
        }
        observe_size();
        ++out.operations;
    };
    protocol(config, gates, control, visit, cancel);
    check_cancel(cancel);
    out.norm = state.sparse_norm();
    const double raw_z = state.z_expectation(config.probe);
    if (!std::isfinite(out.norm) || out.norm <= 0 || !std::isfinite(raw_z))
        throw std::runtime_error("La lectura produjo una norma o expectativa inválida.");
    out.z = raw_z / out.norm;
    out.final_components = state.component_count();
    out.logical_payload_bytes = state.logical_payload_bytes();
    check_cancel(cancel);
    out.elapsed_us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    out.available = true;
    return out;
}

// Independent double-precision reference. No DenseCore and no stabilizer algebra
// are used here. Gate matrices act directly on a complete complex<double> vector.
class DenseReference {
 public:
    DenseReference(std::size_t n, Cancel cancel) : n_(n), cancel_(std::move(cancel)) {
        if (!n || n > 16) throw std::invalid_argument("dense reference requires 1..16 qubits");
        check_cancel(cancel_);
        state_.assign(std::size_t{1} << n, Complex{});
        state_[0] = Complex{1, 0};
    }

    void apply(const Gate& gate) {
        if (gate.a >= n_ || ((gate.op == Op::CX || gate.op == Op::CZ) &&
                            (gate.b >= n_ || gate.a == gate.b)))
            throw std::invalid_argument("invalid dense gate target");
        check_cancel(cancel_);
        const std::size_t bit = std::size_t{1} << gate.a;
        if (gate.op == Op::CX || gate.op == Op::CZ) {
            const std::size_t target = std::size_t{1} << gate.b;
            for (std::size_t i = 0; i < state_.size(); ++i) {
                poll(i);
                if (!(i & bit)) continue;
                if (gate.op == Op::CX) {
                    if (!(i & target)) std::swap(state_[i], state_[i | target]);
                } else if (i & target) state_[i] = -state_[i];
            }
            return;
        }
        const double s = std::sqrt(0.5);
        if (gate.op == Op::H || gate.op == Op::X) {
            for (std::size_t i = 0; i < state_.size(); ++i) {
                poll(i);
                if (i & bit) continue;
                if (gate.op == Op::X) std::swap(state_[i], state_[i | bit]);
                else {
                    const Complex a = state_[i], b = state_[i | bit];
                    state_[i] = (a + b) * s;
                    state_[i | bit] = (a - b) * s;
                }
            }
            return;
        }
        Complex phase;
        switch (gate.op) {
            case Op::S: phase = {0, 1}; break;
            case Op::SDagger: phase = {0, -1}; break;
            case Op::T: phase = {s, s}; break;
            case Op::TDagger: phase = {s, -s}; break;
            case Op::Z: phase = {-1, 0}; break;
            default: throw std::logic_error("unknown dense gate");
        }
        for (std::size_t i = 0; i < state_.size(); ++i) {
            poll(i);
            if (i & bit) state_[i] *= phase;
        }
    }

    std::pair<double, double> norm_and_z(std::size_t probe) const {
        if (probe >= n_) throw std::invalid_argument("invalid dense probe");
        const std::size_t mask = std::size_t{1} << probe;
        double norm = 0, z = 0, norm_correction = 0, z_correction = 0;
        for (std::size_t i = 0; i < state_.size(); ++i) {
            poll(i);
            const double probability = std::norm(state_[i]);
            add_compensated(probability, norm, norm_correction);
            add_compensated((i & mask) ? -probability : probability, z, z_correction);
        }
        if (!std::isfinite(norm) || norm <= 0 || !std::isfinite(z))
            throw std::runtime_error("invalid dense readout");
        return {norm, z / norm};
    }

 private:
    static void add_compensated(double value, double& sum, double& correction) {
        const double adjusted = value - correction;
        const double next = sum + adjusted;
        correction = (next - sum) - adjusted;
        sum = next;
    }
    void poll(std::size_t position) const {
        if ((position & 1023U) == 0) check_cancel(cancel_);
    }
    std::size_t n_;
    Cancel cancel_;
    std::vector<Complex> state_;
};

inline Metrics dense(const Config& config, const Circuit& gates, bool control,
                     const Cancel& cancel) {
    const auto start = Clock::now();
    DenseReference state(config.rows * config.cols, cancel);
    Metrics out;
    protocol(config, gates, control, [&](const Gate& gate) {
        state.apply(gate);
        ++out.operations;
    }, cancel);
    const auto readout = state.norm_and_z(config.probe);
    out.norm = readout.first;
    out.z = readout.second;
    out.logical_payload_bytes = (std::size_t{1} << (config.rows * config.cols)) * sizeof(Complex);
    check_cancel(cancel);
    out.elapsed_us = std::chrono::duration<double, std::micro>(Clock::now() - start).count();
    out.available = true;
    return out;
}

inline bool valid_readout(const Metrics& result) {
    return result.available && std::isfinite(result.z) && std::isfinite(result.norm) &&
           std::abs(result.norm - 1) <= tolerance && std::abs(result.z) <= 1 + tolerance;
}

inline Result evaluate(const Config& config, const Circuit& gates, const Cancel& cancel) {
    Result result;
    result.config = config;
    result.gates_u = gates.size();
    for (const auto& gate : gates) if (gate.op == Op::T) ++result.t_gates_u;
    result.echo = reduced(config, gates, false, cancel);
    result.control = reduced(config, gates, true, cancel);
    if (!valid_readout(result.echo) || !valid_readout(result.control) ||
        std::abs(result.control.z - 1) > tolerance) {
        result.validation = "FAILED";
        result.message = "La norma o el control sin perturbación no cumplen la tolerancia.";
        return result;
    }
    if (config.compare_dense && config.rows * config.cols <= 16) {
        result.dense_echo = dense(config, gates, false, cancel);
        result.dense_control = dense(config, gates, true, cancel);
        result.reference_performed = true;
        result.max_error = std::max({std::abs(result.echo.z - result.dense_echo.z),
                std::abs(result.echo.norm - result.dense_echo.norm),
                std::abs(result.control.z - result.dense_control.z),
                std::abs(result.control.norm - result.dense_control.norm)});
        result.max_error_available = std::isfinite(result.max_error);
        result.reference_ok = valid_readout(result.dense_echo) && valid_readout(result.dense_control) &&
                std::abs(result.dense_control.z - 1) <= tolerance &&
                result.max_error_available && result.max_error <= tolerance;
        if (!result.reference_ok) {
            result.validation = "FAILED";
            result.message = "El eco no coincide con la referencia densa dentro de la tolerancia.";
            return result;
        }
        result.validation = "VERIFIED";
        result.message = "Eco y control coinciden con una referencia densa independiente en doble precisión.";
    } else {
        result.validation = "UNVERIFIED";
        result.message = config.compare_dense
                ? "Eco y control completados. Referencia densa omitida: más de 16 qubits."
                : "Eco y control completados. Comparación densa desactivada.";
    }
    check_cancel(cancel);
    result.status = "PASS";
    return result;
}

inline std::string quote(const std::string& value) {
    const char* hex = "0123456789abcdef";
    std::string out = "\"";
    for (unsigned char ch : value) {
        if (ch == '"' || ch == '\\') { out += '\\'; out += ch; }
        else if (ch < 32) { out += "\\u00"; out += hex[ch >> 4]; out += hex[ch & 15]; }
        else out += ch;
    }
    return out + '"';
}

inline void scalar(std::ostream& out, double value, bool available) {
    if (available && std::isfinite(value)) out << value;
    else out << "null";
}

inline void metrics_json(std::ostream& out, const Metrics& m, bool publish) {
    const bool available = publish && m.available;
    out << "{\"available\":" << (available ? "true" : "false") << ",\"z\":";
    scalar(out, m.z, available);
    out << ",\"norm\":"; scalar(out, m.norm, available);
    out << ",\"elapsed_us\":"; scalar(out, m.elapsed_us, available);
    out << ",\"peak_components\":";
    if (available) out << m.peak_components; else out << "null";
    out << ",\"final_components\":";
    if (available) out << m.final_components; else out << "null";
    out << ",\"logical_payload_bytes\":";
    if (available) out << m.logical_payload_bytes; else out << "null";
    out << ",\"operations\":";
    if (available) out << m.operations; else out << "null";
    out << '}';
}
}  // namespace detail

inline Result run_echo(const Config& config, std::function<bool()> cancel = {}) {
    Result result;
    result.config = config;
    bool cancelled = false;
    auto checked_cancel = [&]() {
        if (cancel && cancel()) cancelled = true;
        return cancelled;
    };
    try {
        detail::validate_config(config);
        detail::check_cancel(checked_cancel);
        const auto gates = detail::make_circuit(config);  // Excluded from both engine and dense timings.
        return detail::evaluate(config, gates, checked_cancel);
    } catch (const std::bad_alloc&) {
        result.status = "LIMIT";
        result.message = "Memoria insuficiente. No se publica una lectura parcial.";
    } catch (const std::exception& error) {
        if (cancelled) {
            result.status = "CANCELLED";
            result.message = "Cancelado. No se publica una lectura parcial.";
        } else if (std::string(error.what()).find("reduced component limit reached") != std::string::npos) {
            result.status = "LIMIT";
            result.message = "Se alcanzó el presupuesto de componentes. No se publica una lectura parcial.";
        } else {
            result.status = "ERROR";
            result.message = error.what();
        }
    } catch (...) {
        result.status = "ERROR";
        result.message = "Error nativo inesperado. No se publica una lectura parcial.";
    }
    return result;
}

inline std::string to_json(const Result& result) {
    const Config& c = result.config;
    const bool publish = result.status == "PASS";
    const bool available = publish && result.echo.available;
    const bool reference = publish && result.reference_performed;
    std::ostringstream out;
    out.imbue(std::locale::classic());
    out << std::setprecision(17) << "{\"status\":" << detail::quote(result.status)
        << ",\"validation\":" << detail::quote(result.validation)
        << ",\"message\":" << detail::quote(result.message)
        << ",\"circuit_version\":" << circuit_version << ",\"tolerance\":" << tolerance
        << ",\"config\":{\"rows\":" << c.rows << ",\"cols\":" << c.cols
        << ",\"depth\":" << c.depth << ",\"seed\":" << detail::quote(std::to_string(c.seed))
        << ",\"perturb\":" << c.perturb << ",\"probe\":" << c.probe
        << ",\"rounds\":" << c.rounds << ",\"max_components\":" << c.max_components
        << ",\"compare_dense\":" << (c.compare_dense ? "true" : "false") << '}'
        << ",\"topology\":\"2d-nearest-neighbor-row-major\",\"gates_u\":" << result.gates_u
        << ",\"t_gates_u\":" << result.t_gates_u
        << ",\"reference_performed\":" << (result.reference_performed ? "true" : "false")
        << ",\"reference_ok\":" << (result.reference_ok ? "true" : "false")
        << ",\"echo\":";
    detail::metrics_json(out, result.echo, publish);
    out << ",\"control\":"; detail::metrics_json(out, result.control, publish);
    out << ",\"dense\":{\"performed\":" << (result.reference_performed ? "true" : "false")
        << ",\"echo\":";
    detail::metrics_json(out, result.dense_echo, reference);
    out << ",\"control\":"; detail::metrics_json(out, result.dense_control, reference);
    out << ",\"max_abs_error\":"; detail::scalar(out, result.max_error, result.max_error_available);
    out << "},\"signal_z\":"; detail::scalar(out, result.echo.z, available);
    out << ",\"norm\":"; detail::scalar(out, result.echo.norm, available);
    out << ",\"elapsed_us\":"; detail::scalar(out, result.echo.elapsed_us, available);
    out << ",\"control_z\":"; detail::scalar(out, result.control.z, publish && result.control.available);
    out << ",\"control_norm\":"; detail::scalar(out, result.control.norm, publish && result.control.available);
    out << ",\"control_elapsed_us\":";
    detail::scalar(out, result.control.elapsed_us, publish && result.control.available);
    out << ",\"dense_us\":"; detail::scalar(out, result.dense_echo.elapsed_us, reference);
    out << ",\"dense_control_us\":"; detail::scalar(out, result.dense_control.elapsed_us, reference);
    out << ",\"max_error\":"; detail::scalar(out, result.max_error, result.max_error_available);
    out << ",\"logical_bytes\":";
    if (available) out << result.echo.logical_payload_bytes; else out << "null";
    out << ",\"peak_components\":";
    if (available) out << result.echo.peak_components; else out << "null";
    out << ",\"final_components\":";
    if (available) out << result.echo.final_components; else out << "null";
    out << ",\"timing_scope\":\"initialization+evolution+readout; circuit generation excluded\"}";
    return out.str();
}

inline std::string self_test_json(std::function<bool()> cancel = {}) {
    constexpr std::size_t total_tests = 13;
    bool cancelled = false;
    auto checked_cancel = [&] {
        if (cancel && cancel()) cancelled = true;
        return cancelled;
    };
    std::vector<std::pair<std::string, bool>> tests;
    auto record = [&](const std::string& name, const std::function<bool()>& test) {
        try {
            if (checked_cancel()) return;
            const bool passed = test();
            if (!checked_cancel()) tests.emplace_back(name, passed);
        } catch (...) {
            if (!cancelled) tests.emplace_back(name, false);
        }
    };
    for (std::size_t rounds : {std::size_t{1}, std::size_t{2}}) {
        record("H T analytical echo, rounds=" + std::to_string(rounds), [&, rounds] {
            Config c; c.rows = c.cols = 1; c.probe = c.perturb = 0; c.rounds = rounds;
            const detail::Circuit gates{{detail::Op::H, 0}, {detail::Op::T, 0}};
            const auto r = detail::evaluate(c, gates, checked_cancel);
            const double expected = rounds == 1 ? 0.0 : -1.0;
            return r.status == "PASS" && r.reference_ok &&
                   std::abs(r.echo.z - expected) < tolerance && std::abs(r.control.z - 1) < tolerance;
        });
    }
    for (std::uint64_t seed : {UINT64_C(0), UINT64_C(7), UINT64_C(42)}) {
        for (std::size_t rounds : {std::size_t{1}, std::size_t{2}}) {
            record("grid oracle seed=" + std::to_string(seed) + " rounds=" + std::to_string(rounds), [&, seed, rounds] {
                Config c; c.rows = c.cols = 2; c.depth = 2; c.seed = seed; c.rounds = rounds;
                const auto r = run_echo(c, checked_cancel);
                return r.status == "PASS" && r.validation == "VERIFIED" && r.reference_ok;
            });
        }
    }
    record("identity calibration", [&] {
        Config c; c.depth = 0; c.probe = c.perturb;
        const auto r = run_echo(c, checked_cancel);
        return r.status == "PASS" && std::abs(r.echo.z + 1) < tolerance && r.control.z == 1;
    });
    record("component budget cannot pass", [&] {
        Config c; c.max_components = 1;
        const auto r = run_echo(c, checked_cancel);
        return r.status == "LIMIT" && !r.echo.available && !r.reference_ok;
    });
    record("cancellation cannot pass", [] {
        const auto r = run_echo(Config{}, [] { return true; });
        return r.status == "CANCELLED" && !r.echo.available && !r.reference_ok;
    });
    record("invalid configuration", [&] {
        Config c; c.rows = 0;
        return run_echo(c, checked_cancel).status == "ERROR";
    });
    record("explicitly unverified", [&] {
        Config c; c.rows = c.cols = 2; c.compare_dense = false;
        const auto r = run_echo(c, checked_cancel);
        return r.status == "PASS" && r.validation == "UNVERIFIED" && !r.reference_performed;
    });
    std::size_t passed = 0;
    for (const auto& test : tests) if (test.second) ++passed;
    std::ostringstream out;
    out << "{\"status\":\"" << (cancelled ? "CANCELLED" : passed == total_tests ? "PASS" : "ERROR")
        << "\",\"passed\":" << passed << ",\"total\":" << total_tests
        << ",\"completed\":" << tests.size()
        << ",\"message\":" << detail::quote(cancelled ? "Autopruebas canceladas."
                : "Pruebas analíticas, referencia densa, control y límites.") << ",\"tests\":[";
    for (std::size_t i = 0; i < tests.size(); ++i) {
        if (i) out << ',';
        out << "{\"name\":" << detail::quote(tests[i].first)
            << ",\"passed\":" << (tests[i].second ? "true" : "false") << '}';
    }
    out << "]}";
    return out.str();
}

}  // namespace quantum_echo
}  // namespace mundo_x
