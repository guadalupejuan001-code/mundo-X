#include <jni.h>

#include <chrono>
#include <cmath>
#include <cstddef>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <mutex>

#include "mundo_x/compact_core.hpp"
#include "mundo_x/dense_core.hpp"
#include "mundo_x/teleport.hpp"
#include "mundo_x/exact_t_experiment.hpp"

using mundo_x::CompactCore;
using mundo_x::DenseCore;

static jstring as_jstring(JNIEnv* env, const std::string& s) {
  return env->NewStringUTF(s.c_str());
}

static double mib(std::size_t bytes) {
  return static_cast<double>(bytes) / (1024.0 * 1024.0);
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_hello(JNIEnv* env, jclass) {
  try {
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

    std::ostringstream out;
    out << "HELLO MUNDO X\n"
        << "Dense:   " << (dense_ok ? "PASS" : "FAIL") << "\n"
        << "Compact: " << (compact_ok ? "PASS" : "FAIL") << "\n"
        << "Result:  |1>\n"
        << "Dense state: " << dense.state_bytes() << " bytes\n"
        << "Compact state: " << compact.state_bytes() << " bytes\n";
    return as_jstring(env, out.str());
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_bench(JNIEnv* env, jclass, jint qubits,
                                     jboolean include_dense) {
  try {
    std::size_t n = static_cast<std::size_t>(qubits);
    if (n < 2) n = 2;
    if (n > 10000) n = 10000;

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);

    const auto c0 = std::chrono::steady_clock::now();
    CompactCore compact(n);
    compact.hadamard(0);
    for (std::size_t i = 1; i < n; ++i) compact.cnot(i - 1, i);
    const auto c1 = std::chrono::steady_clock::now();
    const double cms =
        std::chrono::duration<double, std::milli>(c1 - c0).count();

    out << "COMPACT X\n"
        << "qubits=" << n << "\n"
        << "state_bytes=" << compact.state_bytes() << "\n"
        << "state_mib=" << mib(compact.state_bytes()) << "\n"
        << "GHZ=" << (compact.is_ghz_signature() ? "PASS" : "FAIL") << "\n"
        << "time_ms=" << cms << "\n";

    if (include_dense) {
      if (n > 22) {
        out << "\nDENSE skipped: Android safety limit is 22 qubits.\n";
      } else {
        const auto d0 = std::chrono::steady_clock::now();
        DenseCore dense(n);
        dense.hadamard(0);
        for (std::size_t i = 1; i < n; ++i) dense.cnot(i - 1, i);
        const auto d1 = std::chrono::steady_clock::now();

        const double dms =
            std::chrono::duration<double, std::milli>(d1 - d0).count();
        const std::size_t last = (std::size_t{1} << n) - 1;
        const bool ok =
            std::abs(dense.probability(0) - 0.5f) < 1e-5f &&
            std::abs(dense.probability(last) - 0.5f) < 1e-5f;

        out << "\nDENSE CORE\n"
            << "state_bytes=" << dense.state_bytes() << "\n"
            << "state_mib=" << mib(dense.state_bytes()) << "\n"
            << "GHZ=" << (ok ? "PASS" : "FAIL") << "\n"
            << "time_ms=" << dms << "\n";

        if (compact.state_bytes() > 0) {
          out << "state_size_ratio_dense_over_compact="
              << static_cast<double>(dense.state_bytes()) /
                     static_cast<double>(compact.state_bytes())
              << "x\n";
        }
      }
    }

    return as_jstring(env, out.str());
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}


extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_cluster(JNIEnv* env, jclass, jint rows_in,
                                       jint cols_in, jboolean include_dense) {
  try {
    std::size_t rows = static_cast<std::size_t>(rows_in);
    std::size_t cols = static_cast<std::size_t>(cols_in);

    if (rows < 1) rows = 1;
    if (cols < 1) cols = 1;
    if (rows > 64) rows = 64;
    if (cols > 64) cols = 64;

    const std::size_t n = rows * cols;

    std::ostringstream out;
    out << std::fixed << std::setprecision(3);

    const auto c0 = std::chrono::steady_clock::now();

    CompactCore compact(n);
    for (std::size_t v = 0; v < n; ++v) compact.hadamard(v);

    for (std::size_t r = 0; r < rows; ++r) {
      for (std::size_t col = 0; col < cols; ++col) {
        const std::size_t v = r * cols + col;
        if (col + 1 < cols) compact.controlled_z(v, v + 1);
        if (r + 1 < rows) compact.controlled_z(v, v + cols);
      }
    }

    const auto c1 = std::chrono::steady_clock::now();
    const double cms =
        std::chrono::duration<double, std::milli>(c1 - c0).count();

    out << "2D CLUSTER STATE\n"
        << "rows=" << rows << "\n"
        << "cols=" << cols << "\n"
        << "qubits=" << n << "\n"
        << "state_bytes=" << compact.state_bytes() << "\n"
        << "state_mib=" << mib(compact.state_bytes()) << "\n"
        << "signature="
        << (compact.is_cluster_signature(rows, cols) ? "PASS" : "FAIL")
        << "\n"
        << "time_ms=" << cms << "\n";

    if (include_dense) {
      if (n > 20) {
        out << "\nDENSE skipped: safety limit is 20 qubits for cluster validation.\n";
      } else {
        const auto d0 = std::chrono::steady_clock::now();

        DenseCore dense(n);
        for (std::size_t v = 0; v < n; ++v) dense.hadamard(v);

        for (std::size_t r = 0; r < rows; ++r) {
          for (std::size_t col = 0; col < cols; ++col) {
            const std::size_t v = r * cols + col;
            if (col + 1 < cols) dense.controlled_z(v, v + 1);
            if (r + 1 < rows) dense.controlled_z(v, v + cols);
          }
        }

        const bool ok = dense.cluster_is_valid(rows, cols);

        const auto d1 = std::chrono::steady_clock::now();
        const double dms =
            std::chrono::duration<double, std::milli>(d1 - d0).count();

        out << "\nDENSE VALIDATION\n"
            << "state_bytes=" << dense.state_bytes() << "\n"
            << "state_mib=" << mib(dense.state_bytes()) << "\n"
            << "signature=" << (ok ? "PASS" : "FAIL") << "\n"
            << "time_ms=" << dms << "\n";
      }
    }

    return as_jstring(env, out.str());
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_practice(JNIEnv* env, jclass, jint qubits,
                                        jstring script) {
  const char* raw = env->GetStringUTFChars(script, nullptr);
  std::string source = raw ? raw : "";
  if (raw) env->ReleaseStringUTFChars(script, raw);

  try {
    std::size_t n = static_cast<std::size_t>(qubits);
    if (n < 1) n = 1;
    if (n > 64) n = 64;
    CompactCore q(n);

    std::istringstream lines(source);
    std::ostringstream out;
    std::string line;

    while (std::getline(lines, line)) {
      std::istringstream in(line);
      std::string cmd;
      in >> cmd;
      if (cmd.empty()) continue;
      out << "> " << line << "\n";

      if (cmd == "h") {
        std::size_t a; in >> a; q.hadamard(a);
      } else if (cmd == "x") {
        std::size_t a; in >> a; q.pauli_x(a);
      } else if (cmd == "z") {
        std::size_t a; in >> a; q.pauli_z(a);
      } else if (cmd == "s") {
        std::size_t a; in >> a; q.phase_s(a);
      } else if (cmd == "cx") {
        std::size_t a, b; in >> a >> b; q.cnot(a, b);
      } else if (cmd == "show") {
        for (std::size_t r = 0; r < n; ++r)
          out << q.generator_string(r) << "\n";
      } else if (cmd == "bytes") {
        out << "state_bytes=" << q.state_bytes() << "\n";
      } else if (cmd == "reset") {
        q = CompactCore(n);
        out << "reset |0...0>\n";
      } else {
        out << "unknown command\n";
      }
    }

    return as_jstring(env, out.str());
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}


namespace {
std::mutex g_visual_mutex;
CompactCore g_visual_state(32);
std::uint64_t g_visual_step = 0;

void reset_visual_state() {
  g_visual_state = CompactCore(32);
  // Begin from a structured entangled Clifford state rather than |0...0>.
  g_visual_state.hadamard(0);
  for (std::size_t i = 1; i < 32; ++i) g_visual_state.cnot(i - 1, i);
  g_visual_step = 0;
}
}  // namespace

extern "C" JNIEXPORT jfloatArray JNICALL
Java_com_mundox_lab_NativeCore_quantumVisualStep(JNIEnv* env, jclass,
                                                 jint touch_qubit,
                                                 jboolean reset) {
  std::lock_guard<std::mutex> lock(g_visual_mutex);

  if (reset) reset_visual_state();

  const std::size_t n = g_visual_state.qubits();
  const std::size_t a = static_cast<std::size_t>(g_visual_step % n);
  const std::size_t b = static_cast<std::size_t>((g_visual_step * 5ULL + 3ULL) % n);
  const std::size_t cidx = static_cast<std::size_t>((g_visual_step * 7ULL + 11ULL) % n);

  g_visual_state.hadamard(a);
  g_visual_state.phase_s(b);
  if (a != cidx) g_visual_state.cnot(a, cidx);
  if ((g_visual_step & 3ULL) == 0ULL) g_visual_state.pauli_z(b);
  if ((g_visual_step & 7ULL) == 3ULL) g_visual_state.pauli_x(cidx);

  if (touch_qubit >= 0) {
    const std::size_t tq = static_cast<std::size_t>(touch_qubit) % n;
    const std::size_t neighbor = (tq + 1) % n;
    g_visual_state.hadamard(tq);
    g_visual_state.cnot(tq, neighbor);
    if ((touch_qubit & 1) != 0) g_visual_state.phase_s(neighbor);
  }

  ++g_visual_step;

  const auto m = g_visual_state.visual_metrics();
  jfloat values[10];
  for (std::size_t i = 0; i < 8; ++i) values[i] = m[i];
  values[8] = static_cast<jfloat>(g_visual_state.state_bytes());
  values[9] = static_cast<jfloat>(g_visual_step);

  jfloatArray out = env->NewFloatArray(10);
  if (out != nullptr) env->SetFloatArrayRegion(out, 0, 10, values);
  return out;
}


extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_tExperiment(JNIEnv* env, jclass,
                                           jint qubits_in, jint t_gates_in) {
  try {
    std::size_t qubits = static_cast<std::size_t>(qubits_in);
    std::size_t t_gates = static_cast<std::size_t>(t_gates_in);
    if (qubits < 1) qubits = 1;
    if (qubits > 16) qubits = 16;
    if (t_gates > 12) t_gates = 12;

    const auto start = std::chrono::steady_clock::now();
    const auto result = mundo_x::run_exact_t_experiment(qubits, t_gates);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::ostringstream out;
    out << result.report
        << std::fixed << std::setprecision(3)
        << "wall_time_ms=" << ms << "\n";
    return as_jstring(env, out.str());
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_tReducedExperiment(JNIEnv* env, jclass,
                                                  jint qubits_in,
                                                  jint t_gates_in) {
  try {
    std::size_t qubits = static_cast<std::size_t>(qubits_in);
    std::size_t t_gates = static_cast<std::size_t>(t_gates_in);
    if (qubits < 1) qubits = 1;
    if (qubits > 12) qubits = 12;
    if (t_gates > 128) t_gates = 128;

    const auto start = std::chrono::steady_clock::now();
    const auto result =
        mundo_x::run_reduced_exact_t_experiment(qubits, t_gates);
    const auto end = std::chrono::steady_clock::now();
    const double ms =
        std::chrono::duration<double, std::milli>(end - start).count();

    std::ostringstream out;
    out << result.report
        << std::fixed << std::setprecision(3)
        << "wall_time_ms=" << ms << "\n";
    return as_jstring(env, out.str());
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_teleport(JNIEnv* env, jclass) {
  try {
    const auto result = mundo_x::run_compact_teleportation();
    return as_jstring(env, result.report);
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}
