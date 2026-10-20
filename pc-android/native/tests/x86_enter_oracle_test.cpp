// Test-only comparison of ENTER/LEAVE with the native x86-64 processor.
// It is never linked into Android and is not an alternate guest engine.
// Legacy mixed operand/stack sizes are covered by x86_cpu_test; this oracle
// checks 16/64-bit operands with the real host's 64-bit stack addressing.
#include <iostream>

#if defined(__x86_64__) && defined(__linux__) && !defined(__ANDROID__) && \
    (defined(__GNUC__) || defined(__clang__))

#include "../x86_cpu.hpp"
#include "../long_mode_guest.hpp"

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
using cosmic_pc::Cpu;
constexpr std::uint64_t kCodeAddress = 0xa000;
constexpr std::uint64_t kPhysicalStack = 0x100000;
constexpr std::size_t kStackBytes = 0x40000;
constexpr std::uint32_t kComparedFlags = Cpu::CF | Cpu::PF | Cpu::AF |
    Cpu::ZF | Cpu::SF | Cpu::OF | Cpu::DF;

struct NativeResult { std::uint64_t sp, bp, flags, initial_flags; };

// A separate assembly function owns its entire stack frame. R11 retains the
// real caller stack while ENTER/LEAVE run on an independent mmap allocation.
// The generated epilogue restores RSP/RBP before returning, captures flags
// without writing to the scratch stack, and clears DF for the System V ABI.
// This works with or without compiler frame pointers and never uses a C++
// local through RSP/RBP while either register holds a test value.
extern "C" void cosmic_enter_host(void*, std::uint64_t, const void*,
                                  NativeResult*, std::uint64_t);
asm(".text\n"
    ".global cosmic_enter_host\n"
    ".type cosmic_enter_host,@function\n"
    "cosmic_enter_host:\n"
    "endbr64\n"
    "pushq %rbp\n"
    "movq %rsp,%r11\n"
    "pushq %r8\n"
    "popfq\n"
    "pushfq\n"
    "popq %rax\n"
    "movq %rax,24(%rcx)\n"
    "movq %rdi,%rsp\n"
    "movq %rsi,%rbp\n"
    "jmp *%rdx\n"
    ".size cosmic_enter_host,.-cosmic_enter_host\n");

struct Mapping {
    std::uint8_t* data;
    std::size_t size;
    explicit Mapping(std::size_t bytes) : data(static_cast<std::uint8_t*>(
        mmap(nullptr, bytes, PROT_READ | PROT_WRITE,
             MAP_PRIVATE | MAP_ANONYMOUS, -1, 0))), size(bytes) {
        if (data == MAP_FAILED) throw std::runtime_error("oracle mmap failed");
    }
    ~Mapping() { munmap(data, size); }
    Mapping(const Mapping&) = delete;
    Mapping& operator=(const Mapping&) = delete;
};

struct Prefix {
    const char* name;
    std::vector<std::uint8_t> bytes;
};
const std::array<Prefix, 8> kPrefixes{{
    {"default", {}}, {"66", {0x66}}, {"67", {0x67}},
    {"66 67", {0x66, 0x67}}, {"66 REX.W", {0x66, 0x48}},
    {"REX.W 66", {0x48, 0x66}},
    {"66 67 REX.W", {0x66, 0x67, 0x48}},
    {"FS 66", {0x64, 0x66}},
}};

class HostCode {
    Mapping mapping_{4096};
    void put(std::size_t offset, const std::vector<std::uint8_t>& instruction) {
        // ENDBR64 allows the indirect branch on CET-enabled hosts too.
        const std::uint8_t start[] = {0xf3, 0x0f, 0x1e, 0xfa};
        const std::uint8_t finish[] = {
            0x48,0x89,0x21,             // MOV [RCX],RSP
            0x48,0x89,0x69,0x08,        // MOV [RCX+8],RBP
            0x4c,0x89,0xdc, 0x5d,       // MOV RSP,R11; POP RBP
            0x9c,0x58,                  // PUSHFQ; POP RAX (caller stack)
            0x48,0x89,0x41,0x10,        // MOV [RCX+16],RAX
            0xfc,0xc3};                 // CLD; RET
        auto* output = mapping_.data + offset;
        std::memcpy(output, start, sizeof start);
        std::memcpy(output + sizeof start, instruction.data(), instruction.size());
        std::memcpy(output + sizeof start + instruction.size(), finish, sizeof finish);
    }
public:
    std::vector<std::uint8_t> enter_bytes, leave_bytes;
    void build(const Prefix& prefix, std::uint16_t allocation, std::uint8_t nesting) {
        enter_bytes = prefix.bytes;
        enter_bytes.insert(enter_bytes.end(), {0xc8, std::uint8_t(allocation),
                            std::uint8_t(allocation >> 8), nesting});
        leave_bytes = prefix.bytes;
        leave_bytes.push_back(0xc9);
        if (mprotect(mapping_.data, mapping_.size, PROT_READ | PROT_WRITE))
            throw std::runtime_error("oracle code mapping cannot become writable");
        put(0, enter_bytes); put(128, leave_bytes);
        if (mprotect(mapping_.data, mapping_.size, PROT_READ | PROT_EXEC))
            throw std::runtime_error("oracle code mapping cannot become executable");
    }
    NativeResult run(std::uint64_t sp, std::uint64_t bp, std::uint32_t flags,
                     bool leave = false) const {
        NativeResult result{};
        cosmic_enter_host(reinterpret_cast<void*>(sp), bp,
                          mapping_.data + (leave ? 128 : 0), &result, flags);
        return result;
    }
};

void boot(Cpu& cpu) {
    const auto disk = cosmic_pc::long_mode_boot_disk();
    cpu.reset(); cpu.memory.write_block(0x7c00, disk.data(), disk.size());
    cpu.interrupt_hook = [](Cpu& guest, std::uint8_t vector) {
        if (vector != 0x15 || guest.reg16(Cpu::EAX) != 0x2401)
            guest.fail("unexpected BIOS service in ENTER oracle setup");
        guest.a20_enabled = true; guest.set_reg8(4, 0); guest.set_flag(Cpu::CF, false);
        return true;
    };
    for (unsigned i = 0; i < 10000 && !cpu.halted && !cpu.fault; ++i) cpu.step();
    if (cpu.fault || !cpu.halted || !cpu.is_long_mode())
        throw std::runtime_error("ENTER oracle did not boot its setup guest to long64");
}

// Map the independent guest backing at exactly the native virtual addresses,
// so the real frame-pointer values can be compared without normalizing them.
// The setup guest's low identity mapping and code remain untouched.
std::uint64_t map_page(Cpu& cpu, std::uint64_t virtual_address,
                       std::uint64_t physical_address, std::uint64_t& next_table) {
    std::uint64_t table = cpu.cr3;
    for (const unsigned shift : {39, 30, 21}) {
        const auto at = table + ((virtual_address >> shift) & 511) * 8;
        auto entry = cpu.read64(at);
        if (!(entry & 1)) {
            if (next_table >= 0x80000) throw std::runtime_error("oracle table pool exhausted");
            entry = next_table | 3;
            next_table += 4096;
            cpu.write64(at, entry);
        }
        if (entry & 0x80) throw std::runtime_error("native address overlaps guest large page");
        table = entry & 0x000ffffffffff000ULL;
    }
    const auto at = table + ((virtual_address >> 12) & 511) * 8;
    cpu.write64(at, physical_address | 3);
    return at;
}

struct Oracle {
    Cpu cpu;
    Mapping allocation{kStackBytes + 65536};
    HostCode code;
    std::uint8_t* stack;
    std::uint64_t base;
    std::vector<std::uint8_t> initial = std::vector<std::uint8_t>(kStackBytes);
    std::vector<std::uint8_t> actual = std::vector<std::uint8_t>(kStackBytes);
    std::array<std::uint64_t, kStackBytes / 4096> page_entries{};
    std::size_t comparisons = 0, fault_vectors = 0;

    Oracle() : stack(reinterpret_cast<std::uint8_t*>(
        (reinterpret_cast<std::uintptr_t>(allocation.data) + 65535) &
        ~std::uintptr_t{65535})), base(reinterpret_cast<std::uintptr_t>(stack)) {
        boot(cpu);
        std::uint64_t next_table = 0x10000, seed = 0x2f481a8cf05b789dULL;
        for (std::size_t i = 0; i < initial.size(); ++i) {
            seed ^= seed << 13; seed ^= seed >> 7; seed ^= seed << 17;
            initial[i] = std::uint8_t(seed);
        }
        for (std::size_t i = 0; i < page_entries.size(); ++i)
            page_entries[i] = map_page(cpu, base + i * 4096,
                                      kPhysicalStack + i * 4096, next_table);
    }

    void reset(std::uint64_t sp, std::uint64_t bp, std::uint32_t flags) {
        std::memcpy(stack, initial.data(), kStackBytes);
        cpu.memory.write_block(kPhysicalStack, initial.data(), initial.size());
        for (unsigned i = 0; i < cpu.regs.size(); ++i)
            cpu.regs[i] = 0xb706df2341800000ULL + i * 0x1020408ULL;
        cpu.regs[Cpu::ESP] = sp; cpu.regs[Cpu::EBP] = bp;
        cpu.eflags = flags; cpu.fault.reset(); cpu.halted = false;
    }

    void compare(const NativeResult& host, const std::vector<std::uint8_t>& instruction,
                 const std::string& label) {
        const auto before = cpu.regs;
        const auto flags = cpu.eflags;
        cpu.eip = kCodeAddress;
        cpu.memory.write_block(kCodeAddress, instruction.data(), instruction.size());
        if (cpu.step() != cosmic_pc::StepResult::Executed || cpu.fault)
            throw std::runtime_error(label + ": " +
                (cpu.fault ? cpu.fault->describe() : "unexpected stop"));
        if (cpu.regs[Cpu::ESP] != host.sp || cpu.regs[Cpu::EBP] != host.bp ||
            (cpu.eflags & kComparedFlags) != (host.flags & kComparedFlags) ||
            (host.flags & kComparedFlags) != (host.initial_flags & kComparedFlags) ||
            cpu.eflags != flags || cpu.eip != kCodeAddress + instruction.size()) {
            std::ostringstream reason;
            reason << label << std::hex << " SP=" << cpu.regs[Cpu::ESP]
                   << " hostSP=" << host.sp << " BP=" << cpu.regs[Cpu::EBP]
                   << " hostBP=" << host.bp << " flags=" << cpu.eflags
                   << " hostFlags=" << host.flags;
            throw std::runtime_error(reason.str());
        }
        for (unsigned i = 0; i < before.size(); ++i)
            if (i != Cpu::ESP && i != Cpu::EBP && cpu.regs[i] != before[i])
                throw std::runtime_error(label + ": unrelated register changed");
        cpu.memory.read_block(kPhysicalStack, actual.data(), actual.size());
        if (std::memcmp(actual.data(), stack, kStackBytes)) {
            std::size_t at = 0;
            while (at < kStackBytes && actual[at] == stack[at]) ++at;
            std::ostringstream reason;
            reason << label << std::hex << " stack mismatch at offset " << at
                   << ": guest=" << unsigned(actual[at]) << " host=" << unsigned(stack[at]);
            throw std::runtime_error(reason.str());
        }
        ++comparisons;
    }

    void pair(const Prefix& prefix, std::uint16_t allocation_size, std::uint8_t nesting,
              std::uint64_t sp_offset, std::uint64_t bp_offset) {
        code.build(prefix, allocation_size, nesting);
        const std::uint32_t flags = 2 | ((nesting & 1) ? kComparedFlags : 0);
        reset(base + sp_offset, base + bp_offset, flags);
        std::ostringstream description;
        description << "prefix=" << prefix.name << " alloc=" << allocation_size
                    << " level=" << unsigned(nesting) << std::hex
                    << " initialSP=" << sp_offset << " initialBP=" << bp_offset;
        const auto entered = code.run(cpu.regs[Cpu::ESP], cpu.regs[Cpu::EBP], flags);
        compare(entered, code.enter_bytes, "ENTER " + description.str());
        // A second independent host invocation starts from ENTER's real output.
        // Different upper BP/SP regions intentionally need not round-trip.
        const auto left = code.run(entered.sp, entered.bp, flags, true);
        compare(left, code.leave_bytes, "LEAVE " + description.str());
    }

    bool native_faults(std::uint64_t sp, std::uint64_t bp, int protection) {
        const pid_t child = fork();
        if (child < 0) throw std::runtime_error("oracle fork failed");
        if (child == 0) {
            const rlimit no_core{0, 0};
            if (setrlimit(RLIMIT_CORE, &no_core)) _exit(90);
            // A sanitizer's signal handler must not turn an intentional host
            // page fault into a different process exit status.
            if (std::signal(SIGSEGV, SIG_DFL) == SIG_ERR) _exit(91);
            if (mprotect(stack + 0x20000, 4096, protection)) _exit(92);
            (void)code.run(sp, bp, 2);
            _exit(0);
        }
        int status = 0;
        while (waitpid(child, &status, 0) < 0)
            if (errno != EINTR) throw std::runtime_error("oracle waitpid failed");
        if (WIFEXITED(status) && WEXITSTATUS(status) == 0) return false;
        if (WIFSIGNALED(status) && WTERMSIG(status) == SIGSEGV) return true;
        throw std::runtime_error("host guard probe failed outside the tested instruction");
    }

    void guards() {
        // The page immediately above the final SP distinguishes a one-byte
        // probe from a word/quadword probe. Only the host decides the result.
        for (const bool word : {false, true}) for (const bool read_only : {false, true})
            for (const int distance : {-9, -8, -7, -3, -2, -1, 0, 1, 7, 8}) {
                const auto sp = base + 0x24000;
                const auto target = base + 0x20000 + distance;
                const auto bytes = word ? 2 : 8;
                const auto allocation_size = std::uint16_t(sp - bytes - target);
                code.build(kPrefixes[word ? 1 : 0], allocation_size, 0);
                reset(sp, base + 0x28000, 2);
                const bool host_fault = native_faults(sp, base + 0x28000,
                                                     read_only ? PROT_READ : PROT_NONE);
                const auto pte = page_entries[0x20000 / 4096];
                const auto saved = cpu.read64(pte);
                cpu.write64(pte, read_only ? saved & ~std::uint64_t{2} : 0);
                cpu.eip = kCodeAddress;
                cpu.memory.write_block(kCodeAddress, code.enter_bytes.data(), code.enter_bytes.size());
                const auto result = cpu.step();
                cpu.write64(pte, saved);
                const bool guest_fault = result == cosmic_pc::StepResult::Faulted;
                if (host_fault != guest_fault || (guest_fault &&
                    (!cpu.fault || cpu.fault->code != cosmic_pc::FaultCode::PageFault ||
                     !(cpu.fault->error_code & 2) || cpu.eip != kCodeAddress ||
                     cpu.regs[Cpu::ESP] != sp || cpu.regs[Cpu::EBP] != base + 0x28000))) {
                    std::ostringstream reason;
                    reason << "ENTER final-SP probe word=" << word << " readOnly=" << read_only
                           << " distance=" << distance << " hostFault=" << host_fault
                           << " guestFault=" << guest_fault;
                    if (cpu.fault) reason << " " << cpu.fault->describe();
                    throw std::runtime_error(reason.str());
                }
                ++fault_vectors;
            }
    }
};
} // namespace

int main() {
    try {
        Oracle oracle;
        for (const auto& prefix : kPrefixes)
            for (unsigned level = 0; level < 256; ++level)
                oracle.pair(prefix, 0x123, std::uint8_t(level), 0x24000, 0x26000);
        const std::array<std::array<std::uint64_t, 2>, 6> layouts{{
            {{0x24000, 0x26000}}, {{0x20003, 0x20000}}, {{0x2ffff, 0x20001}},
            {{0x24000, 0x24000}}, {{0x24000, 0x24010}}, {{0x20000, 0x30000}},
        }};
        for (const auto& prefix : kPrefixes) for (const auto& layout : layouts)
            for (const unsigned level : {0, 1, 2, 3, 15, 30, 31, 32, 255})
                for (const unsigned size : {0, 1, 0x20, 0x123, 0x8000, 0xffff})
                    oracle.pair(prefix, std::uint16_t(size), std::uint8_t(level), layout[0], layout[1]);
        oracle.guards();
        std::cout << "x86_enter_oracle_test: " << oracle.comparisons
                  << " native ENTER/LEAVE comparisons and " << oracle.fault_vectors
                  << " native final-SP page-protection vectors passed\n";
    } catch (const std::exception& error) {
        std::cerr << "x86_enter_oracle_test: " << error.what() << '\n';
        return 1;
    }
}

#else
int main() {
    std::cout << "x86_enter_oracle_test: SKIP (requires Linux x86-64 GCC/Clang host)\n";
    return 77;
}
#endif
