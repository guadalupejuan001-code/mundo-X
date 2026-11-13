#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <random>
#include <stdexcept>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::StepResult;
using Value = Cpu::Extended80;

constexpr unsigned integer = 0x8000, bias = 0x3fff;
constexpr std::uint64_t int_bit = 1ULL << 63;
constexpr unsigned arithmetic_mask = 0x82bf; // B, C1, ES and the six exception bits.
constexpr unsigned remainder_mask = 0xc7bf;
constexpr unsigned operations[]{0, 1, 4, 5, 6, 7, 8, 9, 10, 11};

unsigned status_mask(unsigned op) { return op >= 10 ? remainder_mask : arithmetic_mask; }

Value bits(std::uint64_t significand, unsigned sign_and_exponent) {
  Value result{};
  for (unsigned i = 0; i < 8; ++i) result[i] = std::uint8_t(significand >> (8 * i));
  result[8] = std::uint8_t(sign_and_exponent);
  result[9] = std::uint8_t(sign_and_exponent >> 8);
  return result;
}

struct State {
  Value number;
  unsigned status;

  bool operator==(const State& rhs) const { return number == rhs.number && status == rhs.status; }
};

struct Sample {
  unsigned op, control;
  Value lhs, rhs;
};

State guest(Cpu& cpu, const Sample& input) {
  cpu.reset(0, 0x1000);
  cpu.cr0 |= 0x20;
  cpu.fpu_control = std::uint16_t(input.control);
  cpu.fpu_tags = 0xfff0;
  cpu.x87[0] = input.lhs;
  cpu.x87[1] = input.rhs;
  constexpr unsigned special[]{0xfad9,0xfdd9,0xf8d9,0xf5d9}; // FSQRT, FSCALE, FPREM, FPREM1.
  if (input.op >= 8) cpu.write16(0x1000, std::uint16_t(special[input.op - 8]));
  else cpu.write16(0x1000, std::uint16_t((0xc1U + (input.op << 3)) << 8) | 0xd8U);
  if (cpu.step() != StepResult::Executed)
    throw std::runtime_error(cpu.fault ? cpu.fault->reason : "x87 instruction stopped");
  if ((cpu.fpu_status & 0x3800) != 0) throw std::runtime_error("unexpected x87 TOP change");
  return {cpu.x87[0], cpu.fpu_status & status_mask(input.op)};
}

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
State physical(const Sample& input) {
  alignas(16) std::array<std::uint8_t, 512> saved{}, observed{};
  const auto cw = std::uint16_t(input.control);
  const auto left = input.lhs, right = input.rhs;
#define X87_OP(code) asm volatile( \
      "fxsave %[saved]\n\tfninit\n\tfldt %[right]\n\tfldt %[left]\n\tfldcw %[cw]\n\t" \
      code "\n\tfxsave %[observed]\n\tfnclex\n\tfxrstor %[saved]" \
      : [saved] "=m"(saved), [observed] "=m"(observed) \
      : [right] "m"(right), [left] "m"(left), [cw] "m"(cw) \
      : "st", "st(1)", "st(2)", "st(3)", "st(4)", "st(5)", "st(6)", "st(7)", "memory")
  switch (input.op) {
    case 0: X87_OP(".byte 0xd8,0xc1"); break;
    case 1: X87_OP(".byte 0xd8,0xc9"); break;
    case 4: X87_OP(".byte 0xd8,0xe1"); break;
    case 5: X87_OP(".byte 0xd8,0xe9"); break;
    case 6: X87_OP(".byte 0xd8,0xf1"); break;
    case 7: X87_OP(".byte 0xd8,0xf9"); break;
    case 8: X87_OP(".byte 0xd9,0xfa"); break;
    case 9: X87_OP(".byte 0xd9,0xfd"); break;
    case 10: X87_OP(".byte 0xd9,0xf8"); break;
    case 11: X87_OP(".byte 0xd9,0xf5"); break;
    default: throw std::logic_error("unrecognized oracle operation");
  }
#undef X87_OP
  State state{};
  std::memcpy(state.number.data(), observed.data() + 32, state.number.size());
  state.status = (unsigned(observed[2]) | (unsigned(observed[3]) << 8)) & status_mask(input.op);
  return state;
}

void hardware_matrix(Cpu& cpu) {
  constexpr unsigned count = 21'000;
  std::mt19937_64 rng(3620);
  for (unsigned i = 0; i < count; ++i) {
    const auto exp = [](int value) { return unsigned(value<1 ? 1 : value>0x7ffe ? 0x7ffe : value); };
    unsigned exp_a = exp(1 + int(rng() % 0x7ffe)), exp_b = exp(1 + int(rng() % 0x7ffe));
    auto sig_a = rng() | int_bit, sig_b = rng() | int_bit;
    switch (i % 7) {
      case 1: exp_b = exp(int(exp_a) + int(rng() % 140) - 70); break;
      case 2: exp_a = exp(int(rng() % 500) - 250); exp_b = exp(bias + int(rng() % 500) - 250); break;
      case 3: exp_a = exp(0x7ffe + int(rng() % 500) - 250); exp_b = exp(bias + int(rng() % 500) - 250); break;
      case 4: exp_a = 0; sig_a &= int_bit - 1; break;
      case 5: exp_a = 0; sig_a = rng(); break;
      case 6: exp_a = 0x7fff; sig_a = std::array<std::uint64_t, 4>{int_bit,int_bit|1,int_bit|(int_bit>>1),0}[rng()%4]; break;
    }
    if (i % 19 == 2) { exp_b = 0; sig_b = 0; }
    if (i % 19 == 4) { exp_b = 0x7fff; sig_b = int_bit; }
    const auto operation = operations[rng() % std::size(operations)];
    if (operation == 9 && i % 2) exp_b = bias + unsigned(rng() % 20);
    const auto pc = std::array<unsigned, 3>{0,2,3}[rng() % 3];
    const auto rc = unsigned(rng() % 4);
    const auto exception = unsigned(rng() % 12);
    const auto control = (0x7fU | (pc << 8) | (rc << 10)) & ~(exception < 6 ? 1U << exception : 0U);
    const Sample sample{operation, control,
                        bits(sig_a, exp_a | ((rng() & 1) ? integer : 0)),
                        bits(sig_b, exp_b | ((rng() & 1) ? integer : 0))};
    if (!(guest(cpu, sample) == physical(sample)))
      throw std::runtime_error("x86 hardware differs at arithmetic matrix case " + std::to_string(i));
  }
  std::cout << "X87_ARITHMETIC_NATIVE_X86_PASS samples=" << count << '\n';
}
#endif

void golden_matrix(Cpu& cpu) {
  const auto zero = bits(0,0), one = bits(int_bit,bias), two = bits(int_bit,bias+1);
  const auto three = bits(0xc000000000000000ULL,bias+1), minus_one = bits(int_bit,integer|bias);
  const auto double_round = bits(int_bit+1,bias-24);
  const auto min_normal = bits(int_bit,1), min_subnormal = bits(1,0);
  const auto max_normal = bits(~std::uint64_t{0},0x7ffe);
  const auto snan = bits(int_bit | 0xfff,0x7fff), qnan = bits(0xc000000000000011ULL,0x7fff);
  const auto infinity = bits(int_bit,0x7fff), indefinite = bits(0xc000000000000000ULL,0xffff);
  const auto seven = bits(0xe000000000000000ULL,bias+2);
  struct Golden { Sample input; State output; };
  const Golden cases[]{
    {{0,0x007f,one,double_round},{bits(0x8000010000000000ULL,bias),0x0220}},
    {{0,0x027f,one,double_round},{bits(0x8000008000000000ULL,bias),0x0020}},
    {{0,0x037f,one,double_round},{bits(0x8000008000000000ULL,bias),0x0020}},
    {{0,0x0b7f,one,double_round},{bits(0x8000008000000001ULL,bias),0x0220}},
    {{0,0x0f7f,one,double_round},{bits(0x8000008000000000ULL,bias),0x0020}},
    {{0,0x077f,one,minus_one},{bits(0,integer),0}},
    {{0,0x037f,min_subnormal,min_subnormal},{bits(2,0),0x0002}},
    {{0,0x037d,min_subnormal,min_subnormal},{min_subnormal,0x8082}},
    {{0,0x037f,bits(int_bit,0),zero},{min_normal,0x0002}},
    {{0,0x037f,qnan,snan},{qnan,0x0001}},
    {{0,0x037e,qnan,snan},{qnan,0x8081}},
    {{0,0x037f,qnan,bits(1,2)},{indefinite,0x0001}},
    {{1,0x037f,max_normal,two},{infinity,0x0228}},
    {{1,0x0377,max_normal,two},{bits(~std::uint64_t{0},0x1fff),0x8088}},
    {{1,0x0477,max_normal,two},{bits(0xffffff0000000000ULL,0x1fff),0x80a8}},
    {{1,0x037f,min_normal,bits(~std::uint64_t{0},bias-1)},{min_normal,0x0230}},
    {{1,0x027f,min_normal,bits(~std::uint64_t{0},bias-1)},{min_normal,0x0220}},
    {{4,0x037f,two,one},{one,0}},
    {{5,0x037f,two,one},{minus_one,0}},
    {{6,0x037f,one,zero},{infinity,0x0004}},
    {{6,0x037b,one,zero},{one,0x8084}},
    {{6,0x037f,min_normal,two},{bits(0x4000000000000000ULL,0),0}},
    {{6,0x037f,min_normal,three},{bits(0x2aaaaaaaaaaaaaabULL,0),0x0230}},
    {{6,0x036f,min_normal,two},{bits(int_bit,0x6000),0x8090}},
    {{6,0x036f,min_normal,three},{bits(0xaaaaaaaaaaaaaaabULL,0x5fff),0x82b0}},
    {{7,0x037f,one,two},{two,0}},
    {{8,0x037f,two,zero},{bits(0xb504f333f9de6484ULL,bias),0x0020}},
    {{8,0x027f,two,zero},{bits(0xb504f333f9de6800ULL,bias),0x0220}},
    {{8,0x035f,two,zero},{bits(0xb504f333f9de6484ULL,bias),0x80a0}},
    {{8,0x037f,minus_one,zero},{indefinite,0x0001}},
    {{8,0x037f,min_subnormal,zero},{bits(0xb504f333f9de6484ULL,0x1fe0),0x0022}},
    {{9,0x007f,bits(int_bit+3,bias),one},{bits(int_bit+3,bias+1),0}},
    {{9,0x037f,one,bits(int_bit,(bias+16)|integer)},{zero,0x0030}},
    {{9,0x037f,min_normal,bits(int_bit,(bias+1)|integer)},{bits(0x2000000000000000ULL,0),0}},
    {{9,0x0377,one,bits(int_bit,bias+14)},{bits(int_bit,0x1fff),0x8088}},
    {{9,0x037f,zero,infinity},{indefinite,0x0001}},
    {{10,0x037f,seven,two},{one,0x4200}},
    {{11,0x037f,seven,two},{minus_one,0x0100}},
    {{10,0x037f,max_normal,two},{bits(int_bit,0x7fbf),0x0400}},
    {{11,0x036f,bits(3,0),bits(2,0)},{bits(int_bit,0xdfc2),0xc092}},
    {{10,0x037f,one,zero},{indefinite,0x0001}},
  };
  for (unsigned i = 0; i < std::size(cases); ++i) {
    if (!(guest(cpu, cases[i].input) == cases[i].output))
      throw std::runtime_error("incorrect 80-bit result/FSW in golden case " + std::to_string(i));
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    if (!(physical(cases[i].input) == cases[i].output))
      throw std::runtime_error("incorrect physical x86 reference in golden case " + std::to_string(i));
#endif
  }
  std::cout << "X87_ARITHMETIC_GOLDEN_PASS samples=" << std::size(cases) << '\n';
}

void memory_exceptions(Cpu& cpu) {
  const auto run = [&](unsigned group, const Value& input, std::uint32_t source, unsigned control) {
    cpu.reset(0, 0x1000);
    cpu.cr0 |= 0x20;
    cpu.fpu_control = std::uint16_t(control);
    cpu.fpu_tags = 0xfffc;
    cpu.x87[0] = input;
    cpu.write16(0x1000, std::uint16_t(((0x06U + (group << 3)) << 8) | 0xd8U));
    cpu.write16(0x1002, 0x3000);
    cpu.write32(0x3000, source);
    if (cpu.step() != StepResult::Executed)
      throw std::runtime_error(cpu.fault ? cpu.fault->reason : "x87 memory instruction stopped");
    return State{cpu.x87[0], cpu.fpu_status & arithmetic_mask};
  };
  const auto qnan = bits(0xc000000000000011ULL,0x7fff), unsupported = bits(1,2);
  if (!(run(0,qnan,1,0x037f) == State{qnan,0}))
    throw std::runtime_error("qNaN suppresses IEEE memory denormal exception");
  if (!(run(0,qnan,0x7fbff00aU,0x037f) == State{qnan,0x0001}))
    throw std::runtime_error("qNaN takes precedence over signaling IEEE memory NaN");
  if (!(run(0,unsupported,1,0x037d) == State{bits(0xc000000000000000ULL,0xffff),0x0001}))
    throw std::runtime_error("invalid dominates unmasked IEEE memory denormal exception");
  if (!(run(7,{},1,0x037f) == State{bits(int_bit,0x7fff),0x0004}))
    throw std::runtime_error("divide-by-zero takes precedence over IEEE memory denormal exception");
  std::cout << "X87_MEMORY_EXCEPTION_PRIORITY_PASS samples=4\n";
}
} // namespace

int main() {
  try {
    Cpu cpu(2 * 1024 * 1024);
    golden_matrix(cpu);
    memory_exceptions(cpu);
#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
    hardware_matrix(cpu);
#endif
  } catch (const std::exception& error) {
    std::cerr << "X87_ARITHMETIC_ORACLE_FAIL " << error.what() << '\n';
    return 1;
  }
}
