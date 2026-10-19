// Native-host oracle for the project's own 64-bit ALU. This file is test-only;
// it is excluded from Android builds and never acts as a guest execution engine.
#include <iostream>

#if defined(__x86_64__) && defined(__linux__) && !defined(__ANDROID__) && \
    (defined(__GNUC__) || defined(__clang__))

#include "../pc_machine.hpp"

#include <array>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::PcMachine;

constexpr std::uint64_t kCodeAddress = 0xa000;
constexpr std::uint32_t kDefinedFlags = Cpu::CF | Cpu::PF | Cpu::AF |
                                        Cpu::ZF | Cpu::SF | Cpu::OF;

enum class Operation { Add, Adc, Sub, Sbb, Cmp };
struct Encoding { Operation operation; std::uint8_t opcode; const char* name; };
constexpr std::array<Encoding, 5> kOperations{{
    {Operation::Add, 0x01, "ADD"},
    {Operation::Adc, 0x11, "ADC"},
    {Operation::Sub, 0x29, "SUB"},
    {Operation::Sbb, 0x19, "SBB"},
    {Operation::Cmp, 0x39, "CMP"},
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
    }
#undef COSMIC_NATIVE_ALU64
    return result;
}

void enter_long_mode(PcMachine& pc) {
    pc.boot_long_mode_demo();
    for (unsigned slice = 0; slice < 256 && !pc.cpu.halted && !pc.cpu.fault &&
                              !pc.waiting_key; ++slice) {
        pc.run(1024);
    }
    if (pc.cpu.fault) {
        throw std::runtime_error("Long-mode setup guest faulted: " + pc.cpu.fault->describe());
    }
    if (!pc.cpu.halted || !pc.cpu.is_long_mode() || pc.waiting_key) {
        throw std::runtime_error("Setup guest did not reach HLT in long64 mode.");
    }
}

void compare(PcMachine& pc, const Encoding& encoding, std::uint64_t left,
             std::uint64_t right, bool carry) {
    const NativeResult expected = native_reference(encoding.operation, left, right, carry);
    // 48 op D8: REX.W, operation r/m64,r64, ModRM RAX destination/RBX source.
    const std::array<std::uint8_t, 3> code{{0x48, encoding.opcode, 0xd8}};
    pc.cpu.memory.write_block(kCodeAddress, code.data(), code.size());
    pc.cpu.eip = kCodeAddress;
    pc.cpu.halted = false;
    pc.cpu.regs[Cpu::EAX] = left;
    pc.cpu.regs[Cpu::EBX] = right;
    // Seed all other compared flags as set, so failures to clear a defined
    // flag are visible. Only carry is an input to ADC/SBB.
    pc.cpu.eflags = 2 | (kDefinedFlags & ~Cpu::CF) | (carry ? Cpu::CF : 0);
    const auto step = pc.cpu.step();
    if (step != cosmic_pc::StepResult::Executed || pc.cpu.fault) {
        throw std::runtime_error(std::string(encoding.name) + " did not execute: " +
                                 (pc.cpu.fault ? pc.cpu.fault->describe() : "unexpected halt"));
    }
    if (pc.cpu.eip != kCodeAddress + code.size() || pc.cpu.regs[Cpu::EBX] != right ||
        !pc.cpu.is_long_mode()) {
        throw std::runtime_error(std::string(encoding.name) +
                                 " changed its source register, execution mode or instruction length.");
    }
    const std::uint64_t actual_value = pc.cpu.regs[Cpu::EAX];
    const std::uint32_t actual_flags = pc.cpu.eflags & kDefinedFlags;
    const std::uint32_t expected_flags = static_cast<std::uint32_t>(expected.flags) & kDefinedFlags;
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

} // namespace

int main() {
    try {
        PcMachine pc(16 * 1024 * 1024);
        enter_long_mode(pc);
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
                    compare(pc, encoding, left, right, carry);
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
                     "CF/PF/AF/ZF/SF/OF).\n";
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
