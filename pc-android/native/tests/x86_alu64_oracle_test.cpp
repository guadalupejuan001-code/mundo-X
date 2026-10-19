// Native-host oracle for the project's own 64-bit ALU and 16/32/64-bit double
// shifts. This file is test-only; it is excluded from Android builds and never
// acts as a guest execution engine.
#include <iostream>

#if defined(__x86_64__) && defined(__linux__) && !defined(__ANDROID__) && \
    (defined(__GNUC__) || defined(__clang__))

#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::Cpu;

constexpr std::uint64_t kCodeAddress = 0xa000;
constexpr std::uint32_t kDefinedFlags = Cpu::CF | Cpu::PF | Cpu::AF |
                                        Cpu::ZF | Cpu::SF | Cpu::OF;

enum class Operation { Add, Adc, Sub, Sbb, Cmp, Imul };
struct Encoding { Operation operation; std::uint8_t opcode; const char* name; };
constexpr std::array<Encoding, 6> kOperations{{
    {Operation::Add, 0x01, "ADD"},
    {Operation::Adc, 0x11, "ADC"},
    {Operation::Sub, 0x29, "SUB"},
    {Operation::Sbb, 0x19, "SBB"},
    {Operation::Cmp, 0x39, "CMP"},
    {Operation::Imul, 0xaf, "IMUL"},
}};

struct NativeResult { std::uint64_t value; std::uint64_t flags; };

NativeResult native_reference(Operation operation, std::uint64_t left,
                              std::uint64_t right, bool carry) {
    NativeResult result{left, 0};
    const std::uint64_t initial_carry = carry ? 1 : 0;
    // All operands are registers. Early-clobber outputs cannot alias either
    // input; cc/memory cover flags and the temporary stack access. BT supplies
    // CF inside the same asm block as the arithmetic operation. Capture flags
    // immediately, before any compiler-generated code can change them.
    //
    // Protect the System V red zone: PUSHFQ writes 136 bytes below the entry
    // stack pointer. RSP is restored before leaving asm and is not an operand.
    // Constraint/red-zone contract: https://gcc.gnu.org/onlinedocs/gcc/Extended-Asm.html
#define COSMIC_NATIVE_ALU64(mnemonic) \
    __asm__ __volatile__( \
        "subq $128, %%rsp\n\t" \
        "btq $0, %[carry]\n\t" \
        mnemonic " %[right], %[left]\n\t" \
        "pushfq\n\t" \
        "popq %[flags]\n\t" \
        "addq $128, %%rsp\n\t" \
        : [left] "+&r" (result.value), [flags] "=&r" (result.flags) \
        : [right] "r" (right), [carry] "r" (initial_carry) \
        : "cc", "memory")

    switch (operation) {
        case Operation::Add: COSMIC_NATIVE_ALU64("addq"); break;
        case Operation::Adc: COSMIC_NATIVE_ALU64("adcq"); break;
        case Operation::Sub: COSMIC_NATIVE_ALU64("subq"); break;
        case Operation::Sbb: COSMIC_NATIVE_ALU64("sbbq"); break;
        case Operation::Cmp: COSMIC_NATIVE_ALU64("cmpq"); break;
        case Operation::Imul: COSMIC_NATIVE_ALU64("imulq"); break;
    }
#undef COSMIC_NATIVE_ALU64
    return result;
}

NativeResult native_double_shift(bool left, unsigned bytes, std::uint64_t destination,
                                 std::uint64_t source, std::uint8_t count,
                                 std::uint32_t initial_flags) {
    NativeResult result{destination, 0};
    const std::uint64_t flags = initial_flags;
    // As above, the stack adjustment protects the red zone. Seed arithmetic
    // flags inside the same asm block, then capture them before compiler code.
    // CL and imm8 forms have identical defined shift semantics; this native CL
    // oracle checks both emulator encodings at each chosen raw count.
#define COSMIC_NATIVE_DOUBLE_SHIFT(mnemonic, size) \
    __asm__ __volatile__( \
        "subq $128, %%rsp\n\t" \
        "pushq %[initial]\n\t" \
        "popfq\n\t" \
        mnemonic " %%cl, %" size "[source], %" size "[destination]\n\t" \
        "pushfq\n\t" \
        "popq %[flags]\n\t" \
        "addq $128, %%rsp\n\t" \
        : [destination] "+&r" (result.value), [flags] "=&r" (result.flags) \
        : [source] "r" (source), "c" (count), [initial] "r" (flags) \
        : "cc", "memory")
    if (bytes == 2) {
        if (left) COSMIC_NATIVE_DOUBLE_SHIFT("shldw", "w");
        else COSMIC_NATIVE_DOUBLE_SHIFT("shrdw", "w");
    } else if (bytes == 4) {
        if (left) COSMIC_NATIVE_DOUBLE_SHIFT("shldl", "k");
        else COSMIC_NATIVE_DOUBLE_SHIFT("shrdl", "k");
    } else {
        if (left) COSMIC_NATIVE_DOUBLE_SHIFT("shldq", "");
        else COSMIC_NATIVE_DOUBLE_SHIFT("shrdq", "");
    }
#undef COSMIC_NATIVE_DOUBLE_SHIFT
    return result;
}

void enter_long_mode(Cpu& cpu) {
    const auto disk = cosmic_pc::long_mode_boot_disk();
    cpu.reset(); cpu.memory.write_block(0x7c00, disk.data(), disk.size());
    cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
        if (vector != 0x15 || guest.reg16(Cpu::EAX) != 0x2401)
            guest.fail("Unexpected BIOS call in the CPU oracle setup guest.");
        guest.a20_enabled = true; guest.set_reg8(4, 0); guest.set_flag(Cpu::CF, false);
        return true;
    };
    for (unsigned instruction = 0; instruction < 262144 && !cpu.halted && !cpu.fault; ++instruction)
        cpu.step();
    if (cpu.fault) {
        throw std::runtime_error("Long-mode setup guest faulted: " + cpu.fault->describe());
    }
    if (!cpu.halted || !cpu.is_long_mode()) {
        throw std::runtime_error("Setup guest did not reach HLT in long64 mode.");
    }
}

void compare(Cpu& cpu, const Encoding& encoding, std::uint64_t left,
             std::uint64_t right, bool carry) {
    const NativeResult expected = native_reference(encoding.operation, left, right, carry);
    // ALU: 48 op D8, r/m64 destination. IMUL: 48 0F AF C3, r64 destination.
    // Both encodings write RAX and preserve RBX.
    const bool imul = encoding.operation == Operation::Imul;
    const std::array<std::uint8_t, 4> code = imul
        ? std::array<std::uint8_t, 4>{{0x48, 0x0f, encoding.opcode, 0xc3}}
        : std::array<std::uint8_t, 4>{{0x48, encoding.opcode, 0xd8, 0}};
    const std::size_t code_size = imul ? 4 : 3;
    cpu.memory.write_block(kCodeAddress, code.data(), code_size);
    cpu.eip = kCodeAddress;
    cpu.halted = false;
    cpu.regs[Cpu::EAX] = left;
    cpu.regs[Cpu::EBX] = right;
    // Seed all other compared flags as set, so failures to clear a defined
    // flag are visible. Only carry is an input to ADC/SBB.
    cpu.eflags = 2 | (kDefinedFlags & ~Cpu::CF) | (carry ? Cpu::CF : 0);
    const auto step = cpu.step();
    if (step != cosmic_pc::StepResult::Executed || cpu.fault) {
        throw std::runtime_error(std::string(encoding.name) + " did not execute: " +
                                 (cpu.fault ? cpu.fault->describe() : "unexpected halt"));
    }
    if (cpu.eip != kCodeAddress + code_size || cpu.regs[Cpu::EBX] != right ||
        !cpu.is_long_mode()) {
        throw std::runtime_error(std::string(encoding.name) +
                                 " changed its source register, execution mode or instruction length.");
    }
    const std::uint64_t actual_value = cpu.regs[Cpu::EAX];
    // IMUL defines CF/OF; its remaining arithmetic flags are undefined.
    const std::uint32_t defined_flags = imul ? Cpu::CF | Cpu::OF : kDefinedFlags;
    const std::uint32_t actual_flags = cpu.eflags & defined_flags;
    const std::uint32_t expected_flags = static_cast<std::uint32_t>(expected.flags) & defined_flags;
    if (actual_value != expected.value || actual_flags != expected_flags) {
        std::ostringstream message;
        message << encoding.name << "64 carry=" << carry << std::hex << std::setfill('0')
                << " left=0x" << std::setw(16) << left
                << " right=0x" << std::setw(16) << right
                << " result=0x" << std::setw(16) << actual_value
                << " host=0x" << std::setw(16) << expected.value
                << " flags=0x" << std::setw(3) << actual_flags
                << " host_flags=0x" << std::setw(3) << expected_flags;
        throw std::runtime_error(message.str());
    }
}

void compare_double_shift(Cpu& cpu, bool left, unsigned bytes,
                          std::uint64_t destination, std::uint64_t source,
                          std::uint8_t count, bool immediate, bool initial) {
    const unsigned effective = count & (bytes == 8 ? 63 : 31);
    if (effective > bytes * 8) throw std::logic_error("undefined word shift is not a native oracle case");
    const std::uint32_t initial_status = 2 | (initial ? kDefinedFlags : 0);
    const auto expected = native_double_shift(left, bytes, destination, source, count, initial_status);
    std::vector<std::uint8_t> code;
    if (bytes != 4) code.push_back(bytes == 2 ? 0x66 : 0x48);
    code.push_back(0x0f);
    code.push_back(std::uint8_t((left ? 0xa4 : 0xac) + (immediate ? 0 : 1)));
    code.push_back(0xd8); // destination AX/EAX/RAX, source BX/EBX/RBX
    if (immediate) code.push_back(count);
    cpu.memory.write_block(kCodeAddress, code.data(), code.size());
    cpu.eip = kCodeAddress; cpu.halted = false;
    cpu.regs[Cpu::EAX] = destination; cpu.regs[Cpu::EBX] = source;
    const std::uint64_t count_register = 0xabcdef1234567800ULL | count;
    cpu.regs[Cpu::ECX] = count_register;
    cpu.eflags = initial_status | Cpu::IF | Cpu::DF;
    if (cpu.step() != cosmic_pc::StepResult::Executed || cpu.fault)
        throw std::runtime_error(cpu.fault ? cpu.fault->describe() : "double shift did not execute");
    // Intel SDM SHLD/SHRD: AF is undefined for nonzero counts; OF is defined
    // only for count1. At count0 every arithmetic flag remains unchanged.
    const std::uint32_t defined = effective == 0 ? kDefinedFlags
        : Cpu::CF | Cpu::PF | Cpu::ZF | Cpu::SF | (effective == 1 ? Cpu::OF : 0);
    if (cpu.regs[Cpu::EAX] != expected.value || (cpu.eflags & defined) != (expected.flags & defined)) {
        std::ostringstream message;
        message << (left ? "SHLD" : "SHRD") << bytes * 8 << (immediate ? " imm8=" : " CL=")
                << unsigned(count) << " initial_flags=" << initial << std::hex
                << " destination=0x" << destination << " source=0x" << source
                << " result=0x" << cpu.regs[Cpu::EAX] << " host=0x" << expected.value
                << " flags=0x" << (cpu.eflags & defined) << " host_flags=0x" << (expected.flags & defined);
        throw std::runtime_error(message.str());
    }
    if (cpu.eip != kCodeAddress + code.size() || cpu.regs[Cpu::EBX] != source ||
        cpu.regs[Cpu::ECX] != count_register || !cpu.is_long_mode() ||
        !cpu.flag(Cpu::IF) || !cpu.flag(Cpu::DF)) {
        throw std::runtime_error("double shift changed source/count/mode/control flags or instruction length");
    }
}

} // namespace

int main() {
    try {
        Cpu cpu(16 * 1024 * 1024);
        enter_long_mode(cpu);
        constexpr std::array<std::uint64_t, 20> boundaries{{
            0, 1, 2, 7, 0x0f, 0x10, 0x7f, 0x80, 0xff, 0x100,
            0x7fffffffULL, 0x80000000ULL, 0xffffffffULL, 0x100000000ULL,
            0x7ffffffffffffffeULL, 0x7fffffffffffffffULL,
            0x8000000000000000ULL, 0x8000000000000001ULL,
            0xfffffffffffffffeULL, 0xffffffffffffffffULL,
        }};
        std::uint64_t vectors = 0;
        auto pair = [&](std::uint64_t left, std::uint64_t right) {
            for (const auto& encoding : kOperations) {
                for (const bool carry : {false, true}) {
                    compare(cpu, encoding, left, right, carry);
                    ++vectors;
                }
            }
        };
        for (const auto left : boundaries) {
            for (const auto right : boundaries) pair(left, right);
        }
        std::uint64_t state = 0x6436346f7261636cULL;
        auto next = [&] {
            state = state * 6364136223846793005ULL + 1442695040888963407ULL;
            return state;
        };
        for (unsigned i = 0; i < 256; ++i) {
            const auto left = next();
            const auto right = next();
            pair(left, right);
        }
        std::cout << "x86-64 ALU native oracle: " << vectors
                  << " instruction vectors passed (ADD/ADC/SUB/SBB/CMP; "
                     "CF/PF/AF/ZF/SF/OF; IMUL CF/OF).\n";
        constexpr std::array<std::uint64_t, 8> shift_boundaries{{
            0, 1, UINT64_MAX, 0x8000000000000000ULL, 0x8000800080008000ULL,
            0x7fff7fff7fff7fffULL, 0x123456789abcdef0ULL, 0xfedcba9876543211ULL,
        }};
        constexpr std::array<std::uint8_t, 14> counts{{0, 1, 4, 15, 16, 17, 31, 32, 33, 63, 64, 65, 128, 255}};
        std::uint64_t shift_vectors = 0;
        auto shift_pair = [&](std::uint64_t destination, std::uint64_t source) {
            for (const unsigned bytes : {2U, 4U, 8U}) for (const auto count : counts) {
                if ((count & (bytes == 8 ? 63U : 31U)) > bytes * 8U) continue;
                for (const bool left : {false, true}) for (const bool immediate : {false, true}) {
                    for (const bool initial : {false, true}) {
                        compare_double_shift(cpu, left, bytes, destination, source, count, immediate, initial);
                        ++shift_vectors;
                    }
                }
            }
        };
        for (const auto destination : shift_boundaries) for (const auto source : shift_boundaries)
            shift_pair(destination, source);
        for (unsigned i = 0; i < 64; ++i) {
            const auto destination = next(); const auto source = next();
            shift_pair(destination, source);
        }
        std::cout << "SHLD/SHRD native oracle: " << shift_vectors
                  << " instruction vectors passed (16/32/64-bit, imm8/CL, defined flags only).\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}

#else

int main() {
    std::cout << "SKIP: native ALU oracle requires an x86_64 Linux host with GCC/Clang; "
                 "it is not an Android test.\n";
    return 77;
}

#endif
