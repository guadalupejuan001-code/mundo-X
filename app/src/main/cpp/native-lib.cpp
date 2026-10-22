#include <jni.h>

#include <chrono>
#include <charconv>
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
#include "mundo_x/ether_renderer.hpp"

using mundo_x::CompactCore;
using mundo_x::DenseCore;
using mundo_x::EtherRenderer;

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
    std::size_t line_number = 0;

    while (std::getline(lines, line)) {
      ++line_number;
      std::istringstream in(line);
      std::string cmd;
      in >> cmd;
      if (cmd.empty()) continue;
      out << "> " << line << "\n";

      auto read_index = [&]() {
        std::string token;
        std::size_t value = 0;
        if (in >> token) {
          const auto parsed = std::from_chars(token.data(), token.data() + token.size(), value);
          if (parsed.ec == std::errc{} && parsed.ptr == token.data() + token.size())
            return value;
        }
        throw std::invalid_argument("line " + std::to_string(line_number) +
                                    ": expected unsigned qubit index for " + cmd);
      };
      auto require_end = [&]() {
        std::string extra;
        if (in >> extra)
          throw std::invalid_argument("line " + std::to_string(line_number) +
                                      ": unexpected argument '" + extra + "'");
      };

      if (cmd == "h" || cmd == "x" || cmd == "z" || cmd == "s") {
        const auto a = read_index();
        require_end();
        if (cmd == "h") q.hadamard(a);
        else if (cmd == "x") q.pauli_x(a);
        else if (cmd == "z") q.pauli_z(a);
        else q.phase_s(a);
      } else if (cmd == "cx") {
        const auto a = read_index();
        const auto b = read_index();
        require_end();
        q.cnot(a, b);
      } else if (cmd == "show") {
        require_end();
        for (std::size_t r = 0; r < n; ++r)
          out << q.generator_string(r) << "\n";
      } else if (cmd == "bytes") {
        require_end();
        out << "state_bytes=" << q.state_bytes() << "\n";
      } else if (cmd == "reset") {
        require_end();
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
double er_unit(std::uint64_t& state) {
  state = state * 6364136223846793005ULL + 1442695040888963407ULL;
  return static_cast<double>((state >> 11) & ((1ULL << 53) - 1)) /
         static_cast<double>(1ULL << 53);
}

void er_scene(EtherRenderer& renderer, int scene) {
  if (scene == 0) {
    renderer.set_camera({0,0,12},{0,0,-4},47);
    renderer.add_body({0,{0,0,0},1.6,{{.18,.58,1.0}}});
    renderer.add_body({1,{3.5,.8,-3.},1.0,{{1.0,.42,.10}}});
    renderer.add_body({2,{-4.2,-1.1,-9.},2.0,{{.52,.82,.28}}});
    return;
  }
  if (scene == 1) {
    renderer.set_camera({0,1.5,18},{0,0,-18},52);
    for (std::uint32_t id=0; id<23; ++id) {
      const double a=static_cast<double>(id)*2.399963229728653;
      const double ring=1.3+static_cast<double>(id)*.62;
      const double z=-static_cast<double>(id)*2.2;
      const double r=.28+static_cast<double>((id*7)%11)*.055;
      const double cr=.22+.72*static_cast<double>((id*17)%97)/96.;
      const double cg=.18+.70*static_cast<double>((id*31)%89)/88.;
      const double cb=.20+.72*static_cast<double>((id*47)%83)/82.;
      renderer.add_body({id,{std::cos(a)*ring,std::sin(a)*ring*.36,z},r,{{cr,cg,cb}}});
    }
    return;
  }
  if (scene == 2) {
    renderer.set_camera({0,0,16},{0,0,-28},58);
    std::uint64_t rng=0x455448455252454eULL;
    for (std::uint32_t id=0; id<64; ++id) {
      const double x=(er_unit(rng)-.5)*34.;
      const double y=(er_unit(rng)-.5)*18.;
      const double z=-2.-er_unit(rng)*62.;
      const double r=.18+er_unit(rng)*1.55;
      renderer.add_body({id,{x,y,z},r,{{.15+.85*er_unit(rng),.15+.85*er_unit(rng),.15+.85*er_unit(rng)}}});
    }
    return;
  }
  renderer.set_camera({0,0,14},{0,0,-20},48);
  // Occlusion stack: many legal candidates share screen space, so front-to-back
  // distance pruning can prove that deeper bodies cannot beat the nearest hit.
  for (std::uint32_t id=0; id<64; ++id) {
    const double z=-static_cast<double>(id)*1.15;
    const double r=2.9+.01*static_cast<double>(id%5);
    renderer.add_body({id,{0,0,z},r,{{.15+.012*id,.42,.85-.008*id}}});
  }
}

std::size_t er_dimension(jint value, std::size_t minimum, std::size_t maximum) {
  if (value < static_cast<jint>(minimum)) return minimum;
  if (value > static_cast<jint>(maximum)) return maximum;
  return static_cast<std::size_t>(value);
}
}  // namespace

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_etherRenderBenchmark(JNIEnv* env, jclass,
                                                     jint width_in, jint height_in,
                                                     jint scene_in, jint tile_in,
                                                     jboolean train, jboolean exact) {
  try {
    const auto width=er_dimension(width_in,64,1920);
    const auto height=er_dimension(height_in,36,1080);
    const auto tile=er_dimension(tile_in,8,256);
    const int scene=std::max(0,std::min(3,static_cast<int>(scene_in)));
    EtherRenderer renderer(width,height,tile);
    er_scene(renderer,scene);

    std::size_t chosen_tile=tile;
    double train_ms=0.;
    if(train) {
      const auto begin=std::chrono::steady_clock::now();
      const auto trained=renderer.train_tiles({8,16,32,64,128,256});
      const auto end=std::chrono::steady_clock::now();
      chosen_tile=trained.chosen_tile;
      train_ms=std::chrono::duration<double,std::milli>(end-begin).count();
    }

    const auto begin=std::chrono::steady_clock::now();
    const auto stats=renderer.render(exact);
    const auto end=std::chrono::steady_clock::now();
    const double render_ms=std::chrono::duration<double,std::milli>(end-begin).count();

    std::ostringstream out;
    out << std::fixed << std::setprecision(4)
        << "{"
        << "\"engine\":\"Ether Render X\","
        << "\"scene\":" << scene << ","
        << "\"width\":" << width << ","
        << "\"height\":" << height << ","
        << "\"tile\":" << stats.tile << ","
        << "\"chosen_tile\":" << chosen_tile << ","
        << "\"bodies\":" << stats.bodies << ","
        << "\"regions\":" << stats.tiles_x*stats.tiles_y << ","
        << "\"candidate_tests\":" << stats.candidate_tests << ","
        << "\"brute_tests\":" << stats.brute_tests << ","
        << "\"test_ratio\":" << stats.test_ratio() << ","
        << "\"distance_pruned\":" << stats.distance_pruned_candidates << ","
        << "\"max_candidates\":" << stats.max_candidates << ","
        << "\"exact_mismatches\":" << stats.exact_mismatches << ","
        << "\"frame_hash\":" << stats.frame_hash << ","
        << "\"render_ms\":" << render_ms << ","
        << "\"train_ms\":" << train_ms
        << "}";
    return as_jstring(env,out.str());
  } catch (const std::exception& e) {
    return as_jstring(env,std::string("{\"error\":\"")+e.what()+"\"}");
  }
}

extern "C" JNIEXPORT jintArray JNICALL
Java_com_mundox_lab_NativeCore_etherRenderFrame(JNIEnv* env, jclass,
                                                 jint width_in, jint height_in,
                                                 jint scene_in, jint tile_in) {
  try {
    const auto width=er_dimension(width_in,64,1280);
    const auto height=er_dimension(height_in,36,720);
    const auto tile=er_dimension(tile_in,8,256);
    const int scene=std::max(0,std::min(3,static_cast<int>(scene_in)));
    EtherRenderer renderer(width,height,tile);
    er_scene(renderer,scene);
    renderer.render(false);
    const auto& frame=renderer.frame();
    std::vector<jint> pixels(width*height);
    for(std::size_t y=0;y<height;++y)for(std::size_t x=0;x<width;++x)
      pixels[(height-1-y)*width+x]=static_cast<jint>(frame[y*width+x].rgba);
    jintArray result=env->NewIntArray(static_cast<jsize>(pixels.size()));
    if(result) env->SetIntArrayRegion(result,0,static_cast<jsize>(pixels.size()),pixels.data());
    return result;
  } catch (...) {
    return env->NewIntArray(0);
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
Java_com_mundox_lab_NativeCore_magicWitness(JNIEnv* env, jclass) {
  try {
    const auto result = mundo_x::run_magic_state_witness();
    return as_jstring(env, result.report);
  } catch (const std::exception& e) {
    return as_jstring(env, std::string("ERROR: ") + e.what());
  }
}

extern "C" JNIEXPORT jstring JNICALL
Java_com_mundox_lab_NativeCore_tTraceVariant(JNIEnv* env, jclass,
                                             jint qubits_in,
                                             jint t_gates_in) {
  try {
    std::size_t qubits = static_cast<std::size_t>(qubits_in);
    std::size_t t_gates = static_cast<std::size_t>(t_gates_in);
    if (qubits < 4) qubits = 4;
    if (qubits > 10) qubits = 10;
    if (t_gates > 24) t_gates = 24;
    const auto result =
        mundo_x::run_reduced_growth_trace_variant(qubits, t_gates);
    return as_jstring(env, result.report);
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
