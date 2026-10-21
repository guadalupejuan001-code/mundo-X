#include <chrono>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/xasm.hpp"
#include "mundo_x/teleport.hpp"
#include "mundo_x/exact_t_experiment.hpp"
#include "mundo_x/mxlang.hpp"
#include "mundo_x/rule_physics.hpp"

using mundo_x::CompactCore;
using mundo_x::DenseCore;

static std::size_t parse_count(const std::string& text) {
  std::size_t value = 0;
  const auto result =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (text.empty() || result.ec != std::errc{} ||
      result.ptr != text.data() + text.size())
    throw std::invalid_argument("expected a non-negative decimal integer: " + text);
  return value;
}

static std::size_t read_qubit(std::istringstream& input) {
  std::string token;
  if (!(input >> token)) throw std::invalid_argument("missing qubit index");
  return parse_count(token);
}

// A concrete native consumer for the recovered finite 3D physics model. This
// diagnostic advances every body and emits bounded output; it does not claim
// that these positions have become Ether qubits or Solar orbital ephemerides.
static int physics3d(std::size_t count,std::size_t steps) {
  using Physics=mundo_x::RulePhysics;
  Physics world;
  if(count==0||count>world.rules().max_bodies||steps==0||steps>4096)
    throw std::invalid_argument("physics3d expects 1..65536 bodies and 1..4096 steps");
  for(std::size_t i=0;i<count;++i){
    Physics::Body body;
    body.position={3.*double(i%64),10.+3.*double(i/64%64),3.*double(i/4096)};
    body.radius=.5;body.restitution=1;
    if(i==0){body.position.x=-.49;body.velocity.x=1;}
    if(i==1){body.position.x=.49;body.velocity.x=-1;}
    world.add(body);
  }
  std::size_t contacts=0;
  for(std::size_t i=0;i<steps;++i){world.step(1./120.);contacts+=world.step_stats().contacts;}
  double checksum=0;
  for(const auto& body:world.bodies())checksum+=body.position.x+body.position.y+body.position.z+body.velocity.y;
  std::cout<<std::setprecision(17)<<"{\"engine\":\"rule-physics-3d\",\"body_state\":\"explicit_finite\",\"bodies\":"<<count
           <<",\"steps\":"<<steps<<",\"dt_seconds\":"<<1./120.
           <<",\"revision\":"<<world.revision()<<",\"contacts\":"<<contacts
           <<",\"body_payload_bytes\":"<<world.body_payload_bytes()
           <<",\"scratch_capacity_bytes\":"<<world.step_stats().scratch_bytes
           <<",\"checksum\":"<<checksum<<",\"first_positions\":[";
  for(std::size_t i=0;i<std::min<std::size_t>(8,count);++i){
    const auto& p=world.bodies()[i].position;
    if(i)std::cout<<',';
    std::cout<<'['<<p.x<<','<<p.y<<','<<p.z<<']';
  }
  std::cout<<"]}\n";return 0;
}

static double mib(std::size_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

static int hello() {
  DenseCore dense(1);
  dense.hadamard(0);
  dense.pauli_z(0);
  dense.hadamard(0);

  CompactCore compact(1);
  compact.hadamard(0);
  compact.pauli_z(0);
  compact.hadamard(0);

  const bool dense_ok = dense.probability(1) > 0.999999f;
  const bool compact_ok = compact.hello_is_one();

  std::cout << "HELLO_MUNDO_X dense=" << (dense_ok ? "PASS" : "FAIL")
            << " compact=" << (compact_ok ? "PASS" : "FAIL") << "\n";

  return (dense_ok && compact_ok) ? 0 : 1;
}

static int bench(std::size_t n, bool run_dense) {
  std::cout << std::fixed << std::setprecision(3);

  {
    const auto start = std::chrono::steady_clock::now();

    CompactCore q(n);
    q.hadamard(0);
    for (std::size_t i = 1; i < n; ++i) q.cnot(i - 1, i);

    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "COMPACT qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " ghz=" << (q.is_ghz_signature() ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  if (run_dense) {
    if (n >= 63) {
      std::cout << "DENSE skipped=dimension_overflow\n";
      return 0;
    }

    const auto start = std::chrono::steady_clock::now();

    DenseCore q(n);
    q.hadamard(0);
    for (std::size_t i = 1; i < n; ++i) q.cnot(i - 1, i);

    const auto end = std::chrono::steady_clock::now();

    const std::size_t last = (std::size_t{1} << n) - 1;
    const bool ok =
        std::abs(q.probability(0) - 0.5f) < 1e-5f &&
        std::abs(q.probability(last) - 0.5f) < 1e-5f;

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "DENSE qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " ghz=" << (ok ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  return 0;
}


static int cluster(std::size_t rows, std::size_t cols, bool run_dense) {
  if (rows == 0 || cols == 0) return 2;
  if (rows > std::numeric_limits<std::size_t>::max() / cols)
    throw std::invalid_argument("cluster dimensions overflow qubit count");
  const std::size_t n = rows * cols;

  std::cout << std::fixed << std::setprecision(3);

  {
    const auto start = std::chrono::steady_clock::now();

    CompactCore q(n);
    for (std::size_t v = 0; v < n; ++v) q.hadamard(v);

    for (std::size_t r = 0; r < rows; ++r) {
      for (std::size_t col = 0; col < cols; ++col) {
        const std::size_t v = r * cols + col;
        if (col + 1 < cols) q.controlled_z(v, v + 1);
        if (r + 1 < rows) q.controlled_z(v, v + cols);
      }
    }

    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "CLUSTER_COMPACT rows=" << rows
              << " cols=" << cols
              << " qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " signature="
              << (q.is_cluster_signature(rows, cols) ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  if (run_dense) {
    if (n > 20) {
      std::cout << "CLUSTER_DENSE skipped=safety_limit_20_qubits\n";
      return 0;
    }

    const auto start = std::chrono::steady_clock::now();

    DenseCore q(n);
    for (std::size_t v = 0; v < n; ++v) q.hadamard(v);

    for (std::size_t r = 0; r < rows; ++r) {
      for (std::size_t col = 0; col < cols; ++col) {
        const std::size_t v = r * cols + col;
        if (col + 1 < cols) q.controlled_z(v, v + 1);
        if (r + 1 < rows) q.controlled_z(v, v + cols);
      }
    }

    const bool ok = q.cluster_is_valid(rows, cols);

    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << "CLUSTER_DENSE rows=" << rows
              << " cols=" << cols
              << " qubits=" << n
              << " state_bytes=" << q.state_bytes()
              << " state_mib=" << mib(q.state_bytes())
              << " signature=" << (ok ? "PASS" : "FAIL")
              << " ms=" << ms << "\n";
  }

  return 0;
}


static int t_experiment(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result = mundo_x::run_exact_t_experiment(qubits, t_gates);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << result.report;
    std::cout << std::fixed << std::setprecision(3)
              << "wall_time_ms=" << ms << "\n";
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "T EXPERIMENT ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int t_reduced_experiment(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result =
        mundo_x::run_reduced_exact_t_experiment(qubits, t_gates);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::cout << result.report;
    std::cout << std::fixed << std::setprecision(3)
              << "wall_time_ms=" << ms << "\n";
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "REDUCED T EXPERIMENT ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int t_growth_trace(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto result =
        mundo_x::run_reduced_growth_trace(qubits, t_gates);
    std::cout << result.report;
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "T TRACE ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int magic_witness() {
  try {
    const auto result = mundo_x::run_magic_state_witness();
    std::cout << result.report;
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "MAGIC WITNESS ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int t_growth_trace_variant(std::size_t qubits, std::size_t t_gates) {
  try {
    const auto result =
        mundo_x::run_reduced_growth_trace_variant(qubits, t_gates);
    std::cout << result.report;
    return result.all_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "T TRACE VARIANT ERROR: " << e.what() << "\n";
    return 1;
  }
}

static int teleport() {
  const auto result = mundo_x::run_compact_teleportation();
  std::cout << result.report;
  return result.all_passed ? 0 : 1;
}

static void print_program_result(const mundo_x::XasmRunResult& result,
                                const std::string& kind,
                                const std::string& path, double ms) {
  std::cout << kind << " program=" << path << "\n";
  std::cout << "qubits=" << result.qubits
            << " instructions=" << result.instructions;
  if (result.backend == "reduced") {
    std::cout << " backend=reduced"
              << " components=" << result.components
              << " t_gates=" << result.t_gates
              << " logical_payload_bytes=" << result.logical_payload_bytes;
  } else {
    std::cout << " state_bytes=" << result.state_bytes;
  }
  std::cout << " ms=" << std::fixed << std::setprecision(3) << ms << "\n";
  std::cout << result.output;
  if (result.scene_objects) {
    std::cout << "visual_instructions=" << result.visual_instructions
              << " scene_objects=" << result.scene_objects
              << " scene_logical_rule_bytes=" << result.scene_rule_bytes << "\n";
  }
}

static int run_program(const std::string& path, bool scene_stdout = false) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto result = mundo_x::run_xasm_file(path);
    const auto end = std::chrono::steady_clock::now();

    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    if (scene_stdout) {
      if (!result.assertions_passed) throw std::runtime_error("scene export requires passing assertions");
      if (result.scene_text.empty()) throw std::runtime_error("program did not create a scene");
#ifdef _WIN32
      // The portable scene format uses LF. CRT text mode would silently emit
      // CRLF bytes, which are outside ObjectSceneModel's canonical format.
      if (_setmode(_fileno(stdout), _O_BINARY) == -1)
        throw std::runtime_error("cannot configure scene stdout for binary output");
#endif
      std::cout << result.scene_text;
      std::cout.flush();
      if (!std::cout) throw std::runtime_error("failed to write scene to stdout");
      return 0;
    }

    print_program_result(result, "XASM", path, ms);

    return result.assertions_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "XASM ERROR: " << e.what() << "\n";
    return 1;
  }
}

static std::string mx_diagnostic(const std::string& path,
                                 const std::string& message) {
  if (message.compare(0, 5, "line ") == 0)
    return path + ":" + message.substr(5);
  return path + ": " + message;
}

static int compile_mx_program(const std::string& path) {
  try {
    const auto program = mundo_x::compile_mx_file(path);
    // Keep stdout suitable for redirection to a plain XASM file.
    std::cout << program.xasm;
    if (!std::cout) throw std::runtime_error("cannot write compiled XASM");
    return 0;
  } catch (const std::exception& e) {
    std::cerr << "MX ERROR " << mx_diagnostic(path, e.what()) << "\n";
    return 1;
  }
}

static int run_mx_program(const std::string& path) {
  try {
    const auto start = std::chrono::steady_clock::now();
    const auto program = mundo_x::compile_mx_file(path);
    std::istringstream input(program.xasm);
    mundo_x::XasmRunResult result;
    try {
      result = mundo_x::run_xasm(input);
    } catch (const std::exception& e) {
      const std::string message = e.what();
      if (message.compare(0, 5, "line ") == 0) {
        std::istringstream location(message.substr(5));
        std::size_t generated_line = 0;
        char colon = '\0';
        if ((location >> generated_line >> colon) && colon == ':' &&
            generated_line > 0 && generated_line <= program.source_map.size()) {
          const auto& origin = program.source_map[generated_line - 1];
          std::string detail;
          std::getline(location, detail);
          throw std::runtime_error("line " + std::to_string(origin.line) +
                                   ":" + std::to_string(origin.column) +
                                   ":" + detail);
        }
      }
      throw;
    }
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();
    print_program_result(result, "MX", path, ms);
    return result.assertions_passed ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "MX ERROR " << mx_diagnostic(path, e.what()) << "\n";
    return 1;
  }
}

static int practice(std::size_t n) {
  CompactCore q(n);
  std::mt19937_64 random(0);

  std::cout << "MUNDO X PRACTICE\n";
  std::cout << "commands: h q | x q | z q | s q | cx c t | probz q | measure q | seed n | show | bytes | reset | quit\n";

  std::string line;

  while (true) {
    std::cout << "mx> " << std::flush;
    if (!std::getline(std::cin, line)) break;

    std::istringstream in(line);
    std::string op;
    in >> op;

    try {
      if (op.empty()) continue;
      if (op == "quit" || op == "exit") break;

      if (op == "h") {
        const std::size_t a = read_qubit(in);
        q.hadamard(a);
      } else if (op == "x") {
        const std::size_t a = read_qubit(in);
        q.pauli_x(a);
      } else if (op == "z") {
        const std::size_t a = read_qubit(in);
        q.pauli_z(a);
      } else if (op == "s") {
        const std::size_t a = read_qubit(in);
        q.phase_s(a);
      } else if (op == "cx") {
        const std::size_t c = read_qubit(in);
        const std::size_t t = read_qubit(in);
        q.cnot(c, t);
      } else if (op == "probz") {
        const auto a = read_qubit(in);
        mundo_x::xasm_detail::require_end(in, 1);
        const auto p1 = q.probability_one_z(a);
        std::cout << "PROB_Z q=" << a << " p0=" << (1.0 - p1) << " p1=" << p1 << "\n";
      } else if (op == "measure") {
        const auto a = read_qubit(in);
        mundo_x::xasm_detail::require_end(in, 1);
        const auto measured = q.measure_z(a, random);
        std::cout << "MEASURE_Z q=" << a << " outcome=" << measured.outcome_one
                  << " probability=" << measured.probability << " random=" << measured.random << "\n";
      } else if (op == "seed") {
        const auto seed = mundo_x::xasm_detail::read_unsigned<std::uint64_t>(in, 1, "seed <uint64>");
        mundo_x::xasm_detail::require_end(in, 1);
        random.seed(seed);
      } else if (op == "show") {
        for (std::size_t r = 0; r < q.qubits(); ++r) {
          std::cout << q.generator_string(r) << "\n";
        }
      } else if (op == "bytes") {
        std::cout << "state_bytes=" << q.state_bytes() << "\n";
      } else if (op == "reset") {
        q = CompactCore(n);
        std::cout << "reset\n";
      } else {
        std::cout << "unknown command\n";
      }
    } catch (const std::exception& e) {
      std::cout << "error: " << e.what() << "\n";
    }
  }

  return 0;
}


static int run_cli(int argc, char** argv) {
  if (argc < 2) {
    std::cerr
        << "usage: mundo_x hello | teleport | magic-witness | "
           "t-experiment <qubits> <t_gates> | "
           "t-reduced-experiment <qubits> <t_gates> | "
           "t-trace <qubits> <t_gates> | "
           "t-trace-variant <qubits> <t_gates> | "
           "bench <qubits> [--dense] | "
           "cluster <rows> <cols> [--dense] | practice <qubits> | "
           "run <program.x> [--scene-stdout] | compile <program.mx> | run-mx <program.mx> | "
           "physics3d [bodies] [steps]\n";
    return 2;
  }

  const std::string cmd = argv[1];

  if(cmd=="physics3d") {
    if(argc>4)return 2;
    return physics3d(argc>2?parse_count(argv[2]):256,argc>3?parse_count(argv[3]):120);
  }

  if (cmd == "compile" || cmd == "run-mx") {
    if (argc != 3) return 2;
    return cmd == "compile" ? compile_mx_program(argv[2]) : run_mx_program(argv[2]);
  }

  if (cmd == "hello") return hello();
  if (cmd == "teleport") return teleport();
  if (cmd == "magic-witness") return magic_witness();


  if (cmd == "t-experiment") {
    if (argc < 4) return 2;
    const std::size_t n = parse_count(argv[2]);
    const std::size_t t_gates = parse_count(argv[3]);
    return t_experiment(n, t_gates);
  }

  if (cmd == "t-reduced-experiment") {
    if (argc < 4) return 2;
    const std::size_t n = parse_count(argv[2]);
    const std::size_t t_gates = parse_count(argv[3]);
    return t_reduced_experiment(n, t_gates);
  }

  if (cmd == "t-trace") {
    if (argc < 4) return 2;
    const std::size_t n = parse_count(argv[2]);
    const std::size_t t_gates = parse_count(argv[3]);
    return t_growth_trace(n, t_gates);
  }

  if (cmd == "t-trace-variant") {
    if (argc < 4) return 2;
    const std::size_t n = parse_count(argv[2]);
    const std::size_t t_gates = parse_count(argv[3]);
    return t_growth_trace_variant(n, t_gates);
  }

  if (cmd == "cluster") {
    if (argc < 4) return 2;
    const std::size_t rows = parse_count(argv[2]);
    const std::size_t cols = parse_count(argv[3]);

    bool dense = false;
    for (int i = 4; i < argc; ++i) {
      if (std::string(argv[i]) == "--dense") dense = true;
    }

    return cluster(rows, cols, dense);
  }

  if (cmd == "run") {
    if (argc != 3 && argc != 4) throw std::invalid_argument("run <program.x> [--scene-stdout]");
    const bool scene_stdout = argc == 4;
    if (scene_stdout && std::string(argv[3]) != "--scene-stdout")
      throw std::invalid_argument("unknown run option: " + std::string(argv[3]));
    return run_program(argv[2], scene_stdout);
  }

  if (cmd == "practice") {
    if (argc < 3) return 2;
    const std::size_t n = parse_count(argv[2]);
    return practice(n);
  }

  if (cmd == "bench") {
    if (argc < 3) return 2;

    const std::size_t n = parse_count(argv[2]);

    bool dense = false;
    for (int i = 3; i < argc; ++i) {
      if (std::string(argv[i]) == "--dense") dense = true;
    }

    return bench(n, dense);
  }

  return 2;
}

int main(int argc, char** argv) {
  try {
    return run_cli(argc, argv);
  } catch (const std::invalid_argument& error) {
    std::cerr << "INPUT ERROR: " << error.what() << '\n';
    return 2;
  } catch (const std::exception& error) {
    std::cerr << "ERROR: " << error.what() << '\n';
    return 1;
  }
}
