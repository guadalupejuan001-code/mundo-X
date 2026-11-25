#include "../long_mode_guest.hpp"
#include "../x86_cpu.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>

namespace {
using cosmic_pc::Cpu;
using cosmic_pc::CpuTopology;
using cosmic_pc::StepResult;

struct CpuidRegisters {
    std::uint64_t eax, ebx, ecx, edx;

    bool all_zero() const { return (eax | ebx | ecx | edx) == 0; }
};

struct CacheGeometry {
    unsigned type, level, ways, sets;
    std::uint64_t bytes;
};

constexpr std::array<CacheGeometry, 4> expected_caches{{
    {1, 1, 8, 64, 32 * 1024},
    {2, 1, 8, 64, 32 * 1024},
    {3, 2, 8, 512, 256 * 1024},
    {3, 3, 16, 8192, 8 * 1024 * 1024},
}};

void check(bool ok, const std::string& reason) {
    if (!ok) throw std::runtime_error(reason);
}

void boot_long_mode(Cpu& cpu) {
    const auto boot_disk = cosmic_pc::long_mode_boot_disk();
    cpu.reset();
    cpu.memory.write_block(0x7c00, boot_disk.data(), boot_disk.size());
    cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
        check(vector == 0x15 && guest.reg16(Cpu::EAX) == 0x2401, "unexpected BIOS call");
        guest.a20_enabled = true;
        guest.set_reg8(4, 0);
        guest.set_flag(Cpu::CF, false);
        return true;
    };
    for (unsigned step = 0; step < 10000 && !cpu.halted; ++step) {
        check(cpu.step() != StepResult::Faulted,
              cpu.fault ? cpu.fault->describe() : "guest fault entering long mode");
    }
    check(cpu.halted && cpu.is_long_mode(), "fixture did not enter long mode");
    cpu.halted = false;
}

CpuidRegisters cpuid(Cpu& cpu, std::uint32_t leaf, std::uint32_t subleaf = 0) {
    constexpr std::uint64_t ip = 0xa000;
    constexpr std::array<std::uint8_t, 2> opcode{{0x0f, 0xa2}};
    constexpr std::uint32_t flags = 2 | Cpu::CF | Cpu::OF;
    constexpr std::uint64_t high_bits = 0xffffffff00000000ULL;
    cpu.memory.write_block(ip, opcode.data(), opcode.size());
    cpu.eip = ip;
    cpu.eflags = flags;
    cpu.regs[Cpu::EAX] = high_bits | leaf;
    cpu.regs[Cpu::EBX] = high_bits;
    cpu.regs[Cpu::ECX] = high_bits | subleaf;
    cpu.regs[Cpu::EDX] = high_bits;
    check(cpu.step() == StepResult::Executed && cpu.eip == ip + opcode.size(),
          "guest CPUID did not execute");
    check(cpu.eflags == flags, "guest CPUID changed flags");
    const CpuidRegisters result{cpu.regs[Cpu::EAX], cpu.regs[Cpu::EBX],
                                cpu.regs[Cpu::ECX], cpu.regs[Cpu::EDX]};
    check(((result.eax | result.ebx | result.ecx | result.edx) & high_bits) == 0,
          "guest CPUID did not zero-extend its outputs");
    return result;
}

void check_caches(Cpu& cpu, CpuTopology topology, unsigned apic_id) {
    const auto maximum_leaf = cpuid(cpu, 0).eax;
    check(maximum_leaf >= 4, "CPUID.0 does not advertise deterministic caches");
    const auto features = cpuid(cpu, 1);
    const auto clflush_line_bytes = ((features.ebx >> 8) & 0xff) * 8;
    const auto package_logical = (features.ebx >> 16) & 0xff;
    check((features.edx & (1U << 19)) && clflush_line_bytes == 64,
          "CPUID.1 CLFLUSH line size differs from the virtual line size");
    check(package_logical == topology.logical_processors() &&
          (features.ebx >> 24) == apic_id &&
          bool(features.edx & (1U << 28)) == (package_logical > 1),
          "CPUID.1 logical count, APIC ID or HTT bit is inconsistent");

    unsigned threads_per_core = 1;
    if (package_logical > 1) {
        check(maximum_leaf >= 0x0b, "CPUID.0 does not advertise SMT topology");
        const auto smt = cpuid(cpu, 0x0b, 0);
        const auto package = cpuid(cpu, 0x0b, 1);
        const auto topology_end = cpuid(cpu, 0x0b, 2);
        check(smt.ebx == topology.threads_per_core && smt.eax == 1 &&
              smt.ecx == 0x100 && smt.edx == apic_id,
              "CPUID.0B SMT count, shift, level or APIC ID is inconsistent");
        check(package.ebx == package_logical && package.eax == 4 &&
              package.ecx == 0x201 && package.edx == apic_id,
              "CPUID.0B package count, shift, level or APIC ID is inconsistent");
        check(topology_end.eax == 0 && topology_end.ebx == 0 &&
              topology_end.ecx == 2 && topology_end.edx == apic_id,
              "CPUID.0B does not terminate after the package");
        threads_per_core = static_cast<unsigned>(smt.ebx);
    } else {
        check(maximum_leaf < 0x0b && cpuid(cpu, 0x0b).all_zero(),
              "single-thread CPU unexpectedly advertises extended topology");
    }

    for (std::uint32_t index = 0; index < expected_caches.size(); ++index) {
        const auto expected = expected_caches[index];
        const auto cache = cpuid(cpu, 4, index);
        const auto sharing = ((cache.eax >> 14) & 0x0fff) + 1;
        const auto cores = (cache.eax >> 26) + 1;
        const auto line = (cache.ebx & 0x0fff) + 1;
        const auto partitions = ((cache.ebx >> 12) & 0x03ff) + 1;
        const auto ways = (cache.ebx >> 22) + 1;
        const auto sets = cache.ecx + 1;
        const auto label = "CPUID.4 subleaf " + std::to_string(index);
        check((cache.eax & 0x1f) == expected.type &&
              ((cache.eax >> 5) & 7) == expected.level, label + " has wrong type or level");
        check(line == clflush_line_bytes && partitions == 1 && ways == expected.ways &&
              sets == expected.sets && line * partitions * ways * sets == expected.bytes,
              label + " has wrong cache geometry");
        check(cores == topology.cores && cores == package_logical / threads_per_core &&
              sharing == (expected.level == 3 ? package_logical : threads_per_core),
              label + " has wrong package core or sharing count");
    }
    const auto end = static_cast<std::uint32_t>(expected_caches.size());
    check(cpuid(cpu, 4, end).all_zero() && cpuid(cpu, 4, end + 1).all_zero() &&
          cpuid(cpu, 4, UINT32_MAX).all_zero(), "CPUID.4 does not terminate with all-zero outputs");
}

void check_package(CpuTopology topology) {
    for (unsigned apic_id = 0; apic_id < topology.logical_processors(); ++apic_id) {
        Cpu cpu(2U << 20);
        cpu.configure_topology(topology, apic_id);
        cpu.use_windows_x64_profile();
        try {
            boot_long_mode(cpu);
            check_caches(cpu, topology, apic_id);
        } catch (const std::exception& ex) {
            throw std::runtime_error("package threads=" + std::to_string(topology.logical_processors()) +
                                     " APIC ID=" + std::to_string(apic_id) + ": " + ex.what());
        }
    }
}
}

int main() {
    try {
        check_package({1, 1});
        check_package({8, 2});
        std::cout << "X86_CPUID_CACHE_PASS guest=long64 package_threads=1,16 apic_ids=0-15 "
                     "subleaves=4 line_bytes=64 terminator=zero\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "X86_CPUID_CACHE_FAIL " << ex.what() << '\n';
        return 1;
    }
}
