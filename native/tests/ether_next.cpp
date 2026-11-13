#include "mundo_x/experimental/ether_transform.hpp"

#include <algorithm>
#include <chrono>
#include <cstring>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#if defined(__x86_64__)
#include <emmintrin.h>
#endif

using namespace mundo_x::ether_next;
namespace {
struct Instruction { std::uint8_t opcode, dst, src, prefix = 0x66; };
using Program = std::vector<Instruction>;
void require(bool ok, const char* message) {
  if (!ok) throw std::runtime_error(message);
}
std::vector<std::uint8_t> encode(const Program& program) {
  std::vector<std::uint8_t> bytes;
  for (const auto& i : program) {
    bytes.push_back(i.prefix);
    const unsigned rex = ((i.dst >> 3) << 2) | (i.src >> 3);
    if (rex) bytes.push_back(0x40 | rex);
    bytes.insert(bytes.end(), {0x0f, i.opcode,
      static_cast<std::uint8_t>(0xc0 | ((i.dst & 7) << 3) | (i.src & 7))});
  }
  return bytes;
}
std::uint64_t read_lane(const Vector& v, unsigned offset, unsigned width) {
  std::uint64_t value = 0;
  for (unsigned b = 0; b < width; ++b) value |= std::uint64_t(v[offset + b]) << (b * 8);
  return value;
}
Vector scalar_op(std::uint8_t op, const Vector& a, const Vector& b) {
  if (op == 0x6f) return b;
  Vector out{};
  if (op == 0xef || op == 0xdb || op == 0xeb || op == 0xdf) {
    for (unsigned k = 0; k < 16; ++k) {
      if (op == 0xef) out[k] = a[k] ^ b[k];
      if (op == 0xdb) out[k] = a[k] & b[k];
      if (op == 0xeb) out[k] = a[k] | b[k];
      if (op == 0xdf) out[k] = static_cast<std::uint8_t>(~a[k]) & b[k];
    }
    return out;
  }
  unsigned width = 0;
  switch (op) {
    case 0xfc: case 0xf8: width = 1; break;
    case 0xfd: case 0xf9: width = 2; break;
    case 0xfe: case 0xfa: width = 4; break;
    case 0xd4: case 0xfb: width = 8; break;
    default: throw std::runtime_error("oracle opcode");
  }
  const bool subtract = op >= 0xf8 && op <= 0xfb;
  for (unsigned k = 0; k < 16; k += width) {
    const auto x = read_lane(a, k, width), y = read_lane(b, k, width);
    const std::uint64_t value = subtract ? x - y : x + y;
    for (unsigned n = 0; n < width; ++n) out[k + n] = value >> (n * 8);
  }
  return out;
}
Registers scalar_program(const Program& program, Registers values) {
  for (const auto& i : program) {
    if (i.opcode == 0x7f) values[i.src] = values[i.dst];
    else values[i.dst] = scalar_op(i.opcode, values[i.dst], values[i.src]);
  }
  return values;
}
#if defined(__x86_64__)
__attribute__((noinline)) Vector hardware_op(std::uint8_t op, const Vector& a, const Vector& b) {
  const auto x = _mm_loadu_si128(reinterpret_cast<const __m128i*>(a.data()));
  const auto y = _mm_loadu_si128(reinterpret_cast<const __m128i*>(b.data()));
  __m128i z;
  switch (op) {
    case 0x6f: z = y; break;
    case 0xef: z = _mm_xor_si128(x, y); break;
    case 0xdb: z = _mm_and_si128(x, y); break;
    case 0xeb: z = _mm_or_si128(x, y); break;
    case 0xdf: z = _mm_andnot_si128(x, y); break;
    case 0xfc: z = _mm_add_epi8(x, y); break;
    case 0xfd: z = _mm_add_epi16(x, y); break;
    case 0xfe: z = _mm_add_epi32(x, y); break;
    case 0xd4: z = _mm_add_epi64(x, y); break;
    case 0xf8: z = _mm_sub_epi8(x, y); break;
    case 0xf9: z = _mm_sub_epi16(x, y); break;
    case 0xfa: z = _mm_sub_epi32(x, y); break;
    case 0xfb: z = _mm_sub_epi64(x, y); break;
    default: throw std::runtime_error("hardware oracle opcode");
  }
  Vector result;
  _mm_storeu_si128(reinterpret_cast<__m128i*>(result.data()), z);
  return result;
}
Registers hardware_program(const Program& program, Registers values) {
  for (const auto& i : program) {
    if (i.opcode == 0x7f) values[i.src] = values[i.dst];
    else values[i.dst] = hardware_op(i.opcode, values[i.dst], values[i.src]);
  }
  return values;
}
#endif
std::uint8_t opcode(Op op) {
  switch (op) {
    case Op::Xor: return 0xef; case Op::And: return 0xdb;
    case Op::Or: return 0xeb; case Op::AndNot: return 0xdf;
    case Op::Add8: return 0xfc; case Op::Add16: return 0xfd;
    case Op::Add32: return 0xfe; case Op::Add64: return 0xd4;
    case Op::Sub8: return 0xf8; case Op::Sub16: return 0xf9;
    case Op::Sub32: return 0xfa; case Op::Sub64: return 0xfb;
    default: throw std::runtime_error("SSA oracle opcode");
  }
}
Transform unreduced_transform(const Program& program) {
  Transform t;
  for (unsigned k = 0; k < 16; ++k) {
    t.nodes.push_back({Op::Input, static_cast<std::uint16_t>(k), 0});
    t.outputs[k] = k;
  }
  t.nodes.push_back({Op::Zero, 0, 0});
  t.source_operations = program.size();
  for (const auto& i : program) {
    if (i.opcode == 0x6f) t.outputs[i.dst] = t.outputs[i.src];
    else if (i.opcode == 0x7f) t.outputs[i.src] = t.outputs[i.dst];
    else {
      Op op = Op::Input;
      for (unsigned k = static_cast<unsigned>(Op::Xor); k <= static_cast<unsigned>(Op::Sub64); ++k)
        if (opcode(static_cast<Op>(k)) == i.opcode) op = static_cast<Op>(k);
      require(op != Op::Input, "raw SSA opcode");
      const auto id = static_cast<std::uint16_t>(t.nodes.size());
      t.nodes.push_back({op, t.outputs[i.dst], t.outputs[i.src]});
      t.outputs[i.dst] = id;
      t.schedule.push_back(id);
    }
  }
  return t;
}
Registers scalar_transform(const Transform& t, const Registers& input) {
  std::vector<Vector> values(t.nodes.size());
  for (std::size_t k = 0; k < t.nodes.size(); ++k) {
    const auto& n = t.nodes[k];
    if (n.op == Op::Input) { require(n.a < 16, "input index"); values[k] = input[n.a]; }
    else if (n.op != Op::Zero) {
      require(n.a < k && n.b < k, "SSA topological order");
      values[k] = scalar_op(opcode(n.op), values[n.a], values[n.b]);
    }
  }
  Registers result;
  for (unsigned k = 0; k < 16; ++k) result[k] = values.at(t.outputs[k]);
  return result;
}
Registers pattern(unsigned seed) {
  Registers values;
  std::uint64_t random = 0x9e3779b97f4a7c15ULL ^ seed;
  for (auto& reg : values) for (auto& byte : reg) {
    random ^= random << 13; random ^= random >> 7; random ^= random << 17;
    byte = seed == 0 ? 0 : seed == 1 ? 255 : seed == 2 ? 128 :
           seed == 3 ? 127 : seed == 4 ? 1 : static_cast<std::uint8_t>(random);
  }
  return values;
}
Guard allowed() { Guard g; g.sse2_enabled = true; g.osfxsr = true; return g; }
std::uint64_t checksum(const Registers& values) {
  std::uint64_t sum = 0;
  for (const auto& r : values) for (const auto b : r) sum = sum * 131 + b;
  return sum;
}
double cpu_seconds(const rusage& r) {
  return r.ru_utime.tv_sec + r.ru_utime.tv_usec / 1e6 +
         r.ru_stime.tv_sec + r.ru_stime.tv_usec / 1e6;
}
#if defined(__aarch64__)
std::array<std::uint64_t, 2> fp_state() {
  std::array<std::uint64_t, 2> s{};
  asm volatile("mrs %0, fpcr\n\tmrs %1, fpsr" : "=r"(s[0]), "=r"(s[1]));
  return s;
}
#endif
}
int main(int argc, char** argv) {
  try {
    const Program representative{
      {0x6f,0,1},{0xfe,0,2},{0x6f,3,1},{0xfe,3,2},
      {0xef,4,4},{0xfe,0,4},{0xfa,0,2},{0xef,5,5},
      {0xeb,5,3},{0x6f,6,7},{0xd4,6,8},{0xfb,6,8},
      {0xef,9,10},{0xef,9,10},{0xdb,11,11},{0x6f,12,1},
      {0xfe,12,2},{0x6f,13,12},{0xfd,14,15},{0xf9,14,15}
    };
    std::vector<Program> corpus{representative};
    for (auto op : {0xef,0xdb,0xeb,0xdf,0xfc,0xfd,0xfe,0xd4,0xf8,0xf9,0xfa,0xfb}) {
      corpus.push_back({{static_cast<std::uint8_t>(op),0,1}});
      corpus.push_back({{static_cast<std::uint8_t>(op),15,8}});
      corpus.push_back({{static_cast<std::uint8_t>(op),9,9}});
    }
    corpus.push_back({{0x6f,15,8},{0x7f,15,14},{0x6f,0,14,0xf3}});
    // Sources must be captured before destination outputs overwrite their memory.
    corpus.push_back({{0x6f,0,1},{0x6f,1,2},{0x6f,2,15},{0x6f,15,8}});
    std::size_t cases = 0, native_cases = 0;
    for (const auto& program : corpus) {
      const auto transform = reduce_x86(encode(program));
      require(bool(transform), "supported block rejected");
      require(transform->source_operations == program.size(), "source operation counter");
      NativeCode native(*transform);
      for (unsigned seed = 0; seed < 24; ++seed) {
        const auto input = pattern(seed);
        const auto expected = scalar_program(program, input);
        require(scalar_transform(*transform, input) == expected, "reduced SSA differs from original");
#if defined(__x86_64__)
        require(hardware_program(program, input) == expected, "SSE2 hardware differs from scalar oracle");
#endif
        if (native.available()) {
          auto actual = input;
#if defined(__aarch64__)
          const auto fp_before = fp_state();
#endif
          require(native.execute(actual), "compiled block declined execution");
          require(actual == expected, "NEON output differs from original");
#if defined(__aarch64__)
          require(fp_state() == fp_before, "NEON changed FPCR/FPSR");
#endif
          ++native_cases;
        }
        ++cases;
      }
    }
    const auto bytes = encode(representative);
    const auto compile_start = std::chrono::steady_clock::now();
    const auto transform = reduce_x86(bytes);
    NativeCode native(*transform);
    const auto compile_end = std::chrono::steady_clock::now();
    require(transform->schedule.size() < representative.size() / 2, "work was not reduced");
    // Same emitter, same current source block, before versus after Ether reduction.
    // Copies are aliases in both; this measures work removed by Ether itself.
    const auto raw_transform = unreduced_transform(representative);
    NativeCode raw_native(raw_transform);
    require(raw_native.vector_instructions() > native.vector_instructions(),
            "native vector work was not reduced");
    require(raw_native.words().size() > native.words().size(), "total emitted work was not reduced");
    std::size_t raw_native_cases = 0;
    for (unsigned seed = 0; seed < 24; ++seed) {
      auto raw_values = pattern(seed);
      require(scalar_transform(raw_transform, raw_values) == scalar_program(representative,raw_values),
              "raw SSA differs from original");
      if (raw_native.available()) {
        const auto expected = scalar_program(representative,raw_values);
        require(raw_native.execute(raw_values) && raw_values == expected, "raw NEON differs from original");
        ++raw_native_cases;
      }
    }
    auto malformed = *transform;
    malformed.outputs[0] = static_cast<std::uint16_t>(malformed.nodes.size());
    NativeCode invalid_output(malformed);
    require(!invalid_output.available() && invalid_output.words().empty(), "invalid output accepted");
    malformed = *transform;
    malformed.schedule.push_back(malformed.schedule.back());
    NativeCode invalid_schedule(malformed);
    require(!invalid_schedule.available() && invalid_schedule.words().empty(), "invalid schedule accepted");
    const std::vector<std::vector<std::uint8_t>> rejected{
      {}, {0x66}, {0x66,0x0f,0xef}, {0x66,0x0f,0xef,0x00},
      {0x66,0x0f,0x58,0xc1}, {0xd8,0xc1}, {0xf0,0x66,0x0f,0xef,0xc1},
      {0x66,0x0f,0xef,0xc1,0x90}, {0xf3,0x0f,0xef,0xc1}
    };
    Cache cache(2);
    auto input = pattern(13);
    for (const auto& invalid : rejected) {
      require(!reduce_x86(invalid), "unsupported block partly accepted");
      auto unchanged = input;
      require(!cache.execute(invalid, unchanged, allowed()), "unsupported cache block accepted");
      require(unchanged == input, "fallback changed inputs");
    }
    for (unsigned k = 0; k < 8; ++k) {
      auto guard = allowed();
      switch (k) {
        case 0: guard.long_mode = false; break;
        case 1: guard.sse2_enabled = false; break;
        case 2: guard.osfxsr = false; break;
        case 3: guard.cr0_em = true; break;
        case 4: guard.cr0_ts = true; break;
        case 5: guard.single_step = true; break;
        case 6: guard.instruction_observation = true; break;
        default: guard.cr0_ts = guard.cr0_em = true;
      }
      auto unchanged = input;
      require(!cache.execute(bytes, unchanged, guard), "guard bypassed");
      require(unchanged == input, "guard failure changed inputs");
    }
    if (native.available()) {
      for (unsigned seed = 5; seed < 10; ++seed) {
        auto actual = pattern(seed);
        const auto expected = scalar_program(representative, actual);
        require(cache.execute(bytes, actual, allowed()) && actual == expected, "cache used stale inputs");
      }
      const auto misses = cache.stats().misses;
      const Program changed{{0xfe,0,1}};
      auto actual = input;
      require(cache.execute(encode(changed), actual, allowed()) && actual == scalar_program(changed,input),
              "changed bytes reused stale native code");
      cache.invalidate(); actual = input;
      require(cache.execute(bytes, actual, allowed()) && actual == scalar_program(representative,input),
              "cache invalidation broke execution");
      require(cache.stats().misses >= misses + 2 && cache.stats().hits >= 4, "cache counters");
    } else {
      auto unchanged = input;
      require(!native.execute(unchanged) && unchanged == input, "unavailable backend changed inputs");
      require(!cache.execute(bytes, unchanged, allowed()) && unchanged == input,
              "cache executed unavailable backend");
    }
    constexpr unsigned iterations = 20000;
    rusage before{}, after{};
    getrusage(RUSAGE_SELF, &before);
    const auto start = std::chrono::steady_clock::now();
    auto values = input;
    if (native.available()) for (unsigned k = 0; k < iterations; ++k) {
      values[1][0] = static_cast<std::uint8_t>(k);
      require(native.execute(values), "steady block failed");
    }
    const auto end = std::chrono::steady_clock::now();
    getrusage(RUSAGE_SELF, &after);
    if (argc > 1 && native.available()) {
      std::ofstream code(argv[1], std::ios::binary);
      for (const auto word : native.words()) for (unsigned k = 0; k < 4; ++k) code.put(word >> (k * 8));
      require(bool(code), "failed to save emitted code");
    }
    if (argc > 2 && raw_native.available()) {
      std::ofstream code(argv[2], std::ios::binary);
      for (const auto word : raw_native.words()) for (unsigned k = 0; k < 4; ++k) code.put(word >> (k * 8));
      require(bool(code), "failed to save raw emitted code");
    }
#if defined(__aarch64__)
    const char* architecture = "aarch64";
#elif defined(__x86_64__)
    const char* architecture = "x86_64";
#else
    const char* architecture = "other";
#endif
    std::cout << "{\"experiment\":\"ether-next\",\"architecture\":\"" << architecture
      << "\",\"equivalence_cases\":" << cases << ",\"native_cases\":" << native_cases
#if defined(__x86_64__)
      << ",\"hardware_sse2_oracle\":true"
#else
      << ",\"hardware_sse2_oracle\":false"
#endif
      << ",\"native_neon\":" << (native.available() ? "true" : "false")
      << ",\"source_operations\":" << transform->source_operations
      << ",\"reduced_operations\":" << transform->schedule.size()
      << ",\"raw_neon_arithmetic_per_call\":" << raw_native.vector_instructions()
      << ",\"raw_emitted_words\":" << raw_native.words().size()
      << ",\"raw_loads_per_call\":" << raw_native.loads()
      << ",\"raw_stores_per_call\":" << raw_native.stores()
      << ",\"raw_native_cases\":" << raw_native_cases
      << ",\"neon_arithmetic_per_call\":" << native.vector_instructions()
      << ",\"loads_per_call\":" << native.loads() << ",\"stores_per_call\":" << native.stores()
      << ",\"emitted_words\":" << native.words().size()
      << ",\"compile_ns\":" << std::chrono::duration_cast<std::chrono::nanoseconds>(compile_end - compile_start).count()
      << ",\"native_iterations\":" << (native.available() ? iterations : 0)
      << ",\"native_wall_ns\":" << (native.available() ? std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count() : 0)
      << ",\"native_cpu_seconds\":" << (native.available() ? cpu_seconds(after) - cpu_seconds(before) : 0)
      << ",\"max_rss_kib\":" << after.ru_maxrss
      << ",\"cache_hits\":" << cache.stats().hits << ",\"cache_misses\":" << cache.stats().misses
      << ",\"fallback_checks\":" << rejected.size() + 8
      << ",\"checksum\":" << checksum(values) << "}\n";
#if defined(__aarch64__)
    require(native.available() && native_cases == cases && raw_native_cases == 24,
            "ARM64 native execution required");
#endif
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "ether-next: " << error.what() << '\n';
    return 1;
  }
}
