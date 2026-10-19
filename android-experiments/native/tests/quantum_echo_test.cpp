#include <cmath>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "mundo_x/quantum_echo.hpp"

namespace qe = mundo_x::quantum_echo;

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--json") {
            std::cout << qe::to_json(qe::run_echo(qe::Config{})) << '\n';
            std::cout << qe::self_test_json() << '\n';
            return 0;
        }
        std::size_t checks = 0;
        auto require = [&](bool condition, const std::string& message) {
            ++checks;
            if (!condition) throw std::runtime_error(message);
        };
        const auto self = qe::self_test_json();
        require(self.find("\"status\":\"PASS\"") != std::string::npos, "self-tests failed: " + self);
        for (const auto& grid : std::vector<std::pair<std::size_t, std::size_t>>{
                {1, 1}, {1, 3}, {2, 2}, {2, 3}, {3, 2}, {2, 4}}) {
            for (std::uint64_t seed : {UINT64_C(0), UINT64_C(1), UINT64_C(7), UINT64_C(42)}) {
                for (std::size_t depth : {std::size_t{0}, std::size_t{1}, std::size_t{3}}) {
                    for (std::size_t rounds : {std::size_t{1}, std::size_t{2}}) {
                        qe::Config c;
                        c.rows = grid.first; c.cols = grid.second; c.seed = seed;
                        c.depth = depth; c.rounds = rounds;
                        c.perturb = (seed + depth) % (c.rows * c.cols);
                        c.probe = (seed + rounds) % (c.rows * c.cols);
                        const auto gates = qe::detail::make_circuit(c);
                        require(gates == qe::detail::make_circuit(c), "circuit is not reproducible");
                        std::size_t t_count = 0;
                        for (const auto& gate : gates) {
                            if (gate.op == qe::detail::Op::T) ++t_count;
                            require(qe::detail::inverse(qe::detail::inverse(gate)) == gate,
                                    "inverse is not involutive");
                            if (gate.op == qe::detail::Op::CX || gate.op == qe::detail::Op::CZ) {
                                const auto ar = gate.a / c.cols, ac = gate.a % c.cols;
                                const auto br = gate.b / c.cols, bc = gate.b % c.cols;
                                const auto distance = (ar > br ? ar - br : br - ar) + (ac > bc ? ac - bc : bc - ac);
                                require(distance == 1, "grid gate is not nearest-neighbor");
                            }
                        }
                        require(t_count == c.rows * c.cols * c.depth, "wrong T count");
                        const auto r = qe::run_echo(c);
                        require(r.status == "PASS", qe::to_json(r));
                        require(r.validation == "VERIFIED" && r.reference_ok, "missing dense validation");
                        require(r.max_error <= qe::tolerance, "dense mismatch");
                        require(std::abs(r.control.z - 1) <= qe::tolerance, "control not identity");
                        require(std::abs(r.echo.norm - 1) <= qe::tolerance, "norm drift");
                        require(r.echo.operations == rounds * (2 * gates.size() + 1) + rounds - 1,
                                "wrong chronological protocol length");
                        require(r.control.operations == rounds * 2 * gates.size() + rounds - 1,
                                "wrong control length");
                        require(r.echo.peak_components >= r.echo.final_components, "incorrect component peak");
                    }
                }
            }
        }
        // Every single-qubit gate and both two-qubit types are covered in one
        // deliberately ordered circuit, beyond the seeded generator families.
        qe::Config ordered; ordered.rows = 1; ordered.cols = 2; ordered.rounds = 2;
        const qe::detail::Circuit gates{{qe::detail::Op::H, 0}, {qe::detail::Op::S, 1},
                {qe::detail::Op::T, 0}, {qe::detail::Op::CX, 0, 1}, {qe::detail::Op::H, 1},
                {qe::detail::Op::T, 1}, {qe::detail::Op::CZ, 0, 1}, {qe::detail::Op::S, 0},
                {qe::detail::Op::X, 1}, {qe::detail::Op::Z, 0}};
        const auto ordered_result = qe::detail::evaluate(ordered, gates, {});
        require(ordered_result.status == "PASS" && ordered_result.reference_ok, "ordered oracle mismatch");
        // Depth-zero large registers verify the compact-only path without a dense allocation.
        qe::Config large; large.rows = 7; large.cols = 9; large.depth = 0;
        large.probe = large.perturb = 62;
        const auto unverified = qe::run_echo(large);
        require(unverified.status == "PASS" && unverified.validation == "UNVERIFIED" &&
                !unverified.reference_performed && unverified.echo.z == -1,
                "large-register skip must remain explicitly unverified");
        for (std::size_t budget : {std::size_t{1}, std::size_t{2}, std::size_t{4}}) {
            qe::Config c; c.max_components = budget;
            const auto r = qe::run_echo(c);
            require(r.status == "LIMIT" && !r.echo.available && !r.reference_ok, "budget partially passed");
            require(qe::to_json(r).find("\"signal_z\":null") != std::string::npos, "partial signal published");
        }
        for (std::size_t cancel_after : {std::size_t{0}, std::size_t{10}, std::size_t{100}}) {
            std::size_t calls = 0;
            const auto r = qe::run_echo(qe::Config{}, [&] { return calls++ >= cancel_after; });
            require(r.status == "CANCELLED" && !r.echo.available && !r.reference_ok, "cancel partially passed");
        }
        qe::Config invalid;
        invalid.rows = 8; invalid.cols = 8;
        require(qe::run_echo(invalid).status == "ERROR", "oversized grid accepted");
        invalid = qe::Config{}; invalid.depth = 31;
        require(qe::run_echo(invalid).status == "ERROR", "oversized depth accepted");
        invalid = qe::Config{}; invalid.rounds = 0;
        require(qe::run_echo(invalid).status == "ERROR", "invalid round count accepted");
        invalid = qe::Config{}; invalid.probe = 6;
        require(qe::run_echo(invalid).status == "ERROR", "invalid probe accepted");
        invalid = qe::Config{}; invalid.max_components = 0;
        require(qe::run_echo(invalid).status == "ERROR", "zero budget accepted");
        const auto thrown = qe::run_echo(qe::Config{}, []() -> bool { throw std::runtime_error("callback error"); });
        require(thrown.status == "ERROR", "exception escaped cancel callback");
        require(qe::self_test_json([] { return true; }).find("\"status\":\"CANCELLED\"") != std::string::npos,
                "self-test lifecycle cancellation failed");
        const std::string json = qe::to_json(qe::run_echo(qe::Config{}));
        require(json.find("\"validation\":\"VERIFIED\"") != std::string::npos, "missing verified JSON");
        require(json.find("\"circuit_version\":1") != std::string::npos, "missing circuit version");
        std::cout << "PASS quantum_echo " << checks << " checks\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL quantum_echo: " << error.what() << '\n';
        return 1;
    }
}
