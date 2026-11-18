#include "../pc_machine.hpp"
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
using cosmic_pc::PcMachine;
constexpr std::size_t MiB = 1024 * 1024;
unsigned checks = 0;
void need(bool ok, const std::string& reason) {
    ++checks;
    if (!ok) throw std::runtime_error(reason);
}
void drain(PcMachine& pc) {
    for (unsigned i = 0; i < 1000 && !pc.cpu.halted && !pc.cpu.fault && !pc.waiting_key; ++i)
        pc.run(1000);
}
void boot(PcMachine& pc, std::initializer_list<std::uint8_t> code) {
    auto bytes = std::make_shared<std::vector<std::uint8_t>>(512);
    std::copy(code.begin(), code.end(), bytes->begin());
    (*bytes)[510] = 0x55; (*bytes)[511] = 0xaa;
    pc.mount([bytes](std::uint64_t off, std::uint8_t* dst, std::size_t n) {
        if (off > bytes->size() || n > bytes->size() - off) return false;
        std::copy_n(bytes->data() + off, n, dst); return true;
    }, bytes->size(), "raw");
}

void verify_expanded_memory_map() {
    constexpr auto installed = 64ULL << 30;
    constexpr auto top = PcMachine::high_ram_address_space(installed, true);
    PcMachine pc(top, 32 * MiB);
    pc.reserve_pci_mmio(true);
    pc.boot_demo();
    auto& cpu = pc.cpu;
    constexpr std::uint8_t code[]{0xcd, 0x15, 0xf4};
    cpu.memory.write_block(0x1000, code, sizeof(code));
    std::uint64_t usable = 0, end = 0;
    bool pci_reserved = false, apic_reserved = false, upper_ram = false;
    unsigned token = 0, entries = 0;
    do {
        cpu.reset(0, 0x1000);
        cpu.regs[cosmic_pc::Cpu::EAX] = 0xe820;
        cpu.regs[cosmic_pc::Cpu::EBX] = token;
        cpu.regs[cosmic_pc::Cpu::ECX] = 24;
        cpu.regs[cosmic_pc::Cpu::EDX] = 0x534d4150;
        cpu.regs[cosmic_pc::Cpu::EDI] = 0x9000;
        pc.run(2);
        need(!cpu.fault && cpu.halted && !cpu.flag(cosmic_pc::Cpu::CF),
             "Expanded guest must receive every E820 entry");
        const auto base = cpu.read64(0x9000), length = cpu.read64(0x9008);
        const auto type = cpu.read32(0x9010);
        need(base == end && length && length <= top - base, "Expanded E820 map must be contiguous");
        end = base + length;
        if (type == 1) usable += length;
        if (base == PcMachine::kPciMmioBase)
            pci_reserved = type == 2 && length == PcMachine::kPciMmioBytes;
        if (base == PcMachine::kIoApicAddress)
            apic_reserved = type == 2 && end == PcMachine::kLegacyPhysicalLimit;
        if (base == PcMachine::kLegacyPhysicalLimit) upper_ram = type == 1 && end == top;
        token = cpu.regs[cosmic_pc::Cpu::EBX];
        need(++entries <= 9, "Expanded E820 map must terminate");
    } while (token);
    need(pci_reserved && apic_reserved && upper_ram && usable == installed &&
         pc.usable_ram_bytes() == installed && cpu.memory.allocated_bytes() < MiB,
         "E820 must expose exactly 64 GiB and keep the device holes reserved");
}

void verify_adaptive_ram_compaction() {
    constexpr unsigned base_interval = 32768;
    constexpr auto page_size = cosmic_pc::MemoryFabric::kPageSize;
    constexpr std::uint64_t first_page = 0x20000;
    constexpr unsigned pages = 4;
    PcMachine pc(2 * MiB);
    boot(pc, {0xeb, 0xfe}); // A real guest retires a jump back to itself.
    auto& cpu = pc.cpu;
    cpu.memory.reset_zero();
    cpu.write16(0x7c00, 0xfeeb);
    for (unsigned page = 0; page < pages; ++page)
        cpu.write8(first_page + page * page_size, std::uint8_t(page + 1));
    cpu.memory.fill(0x30000, 0x73, page_size);
    pc.enable_boot_acceleration();
    const auto run_steps = [&](std::uint64_t amount) {
        const auto target = cpu.instructions + amount;
        while (cpu.instructions < target) {
            const auto before = cpu.instructions;
            const auto grant = unsigned(std::min<std::uint64_t>(target - before, PcMachine::kDefaultRunSteps));
            pc.run(grant);
            need(!cpu.fault && cpu.instructions == before + grant, "RAM sweep changed guest retirement");
        }
    };

    const auto& stats = pc.ram_compaction_stats();
    run_steps(base_interval);
    need(stats.passes == 1 && stats.pages_compacted == 1 && stats.interval_steps == 2 * base_interval,
         "A low-yield RAM sweep must increase the interval");
    run_steps(2 * base_interval - 1);
    need(stats.passes == 1, "RAM sweep did not honor the increased interval");
    run_steps(1);
    need(stats.passes == 2 && stats.interval_steps == 4 * base_interval,
         "An unproductive RAM sweep must extend the interval again");
    run_steps(4 * base_interval);
    run_steps(8 * base_interval);
    need(stats.passes == 4 && stats.pages_compacted == 1 && stats.interval_steps == 8 * base_interval,
         "Several unproductive sweeps must stop increasing the interval at the bound");

    using Adjustment = cosmic_pc::SparseRam::BudgetAdjustment;
    const auto resident_budget = cpu.memory.budget_bytes();
    const auto reclaim_target = cpu.memory.allocated_bytes() - page_size + 8;
    need(cpu.memory.adjust_payload_budget(reclaim_target, 1) == Adjustment::Applied &&
             cpu.memory.allocated_bytes() <= reclaim_target && stats.passes == 4 &&
             stats.interval_steps == 8 * base_interval,
         "Pressure reclaim must remain available during periodic compaction backoff");
    need(cpu.memory.adjust_payload_budget(resident_budget, 0) == Adjustment::Applied,
         "Fixture could not restore the resident memory budget");

    for (unsigned page = 0; page < pages; ++page)
        cpu.memory.fill(first_page + page * page_size, std::uint8_t(page + 2), page_size);
    run_steps(8 * base_interval);
    need(stats.passes == 5 && stats.pages_compacted == 1 + pages && stats.interval_steps == base_interval,
         "A productive sweep must reconsider rewritten pages and restore the usual interval");
    for (unsigned page = 0; page < pages; ++page) {
        std::array<std::uint8_t, page_size> bytes{};
        cpu.memory.read_block(first_page + page * page_size, bytes.data(), bytes.size());
        need(std::all_of(bytes.begin(), bytes.end(), [&](auto byte) { return byte == page + 2; }),
             "Adaptive compaction changed a rewritten page's bytes");
    }
    pc.reset();
    need(stats.passes == 0 && stats.pages_compacted == 0 && stats.interval_steps == base_interval,
         "Guest reset must restore the default compaction interval and clear its counters");
    std::cout << "PRIME_RAM_ADAPTIVE_COMPACTION_PASS max_interval=" << 8 * base_interval
              << " pages_reconsidered=" << pages << " pressure_independent=1 reset=1\n";
}
}

int main() {
    try {
        verify_expanded_memory_map();
        verify_adaptive_ram_compaction();
        PcMachine pc(std::size_t(2048) * MiB, std::size_t(1024) * MiB);
        need(pc.cpu.memory.size() == std::uint64_t(2048) * MiB, "Logical RAM must be 2 GiB");
        need(pc.cpu.memory.budget_bytes() == std::uint64_t(1024) * MiB, "Resident guest budget must be 1 GiB");
        need(pc.cpu.memory.allocated_bytes() < MiB, "Creating a machine must not eagerly allocate guest RAM");
        need(pc.cpu.memory.metadata_bytes() < 16 * MiB, "RAM metadata must stay bounded");
        pc.boot_demo(); drain(pc);
        need(!pc.cpu.fault && pc.waiting_key, pc.cpu.fault ? pc.cpu.fault->describe() : "2 GiB real-mode guest failed");
        need(pc.screen().find("ALU + RAM + lectura de disco: OK") != std::string::npos, "Guest boot verification failed");
        need(pc.cpu.read16(0x9000) == 0x1336 && pc.cpu.read16(0x8000) == 0x5843, "Guest RAM and disk results changed");
        const auto payload = pc.cpu.memory.allocated_bytes();
        need(payload < MiB, "The boot test should only occupy touched pages");
        pc.cpu.write32(0x7ffffffc, 0x89abcdef);
        need(pc.cpu.read32(0x7ffffffc) == 0x89abcdef, "Top of the 2 GiB physical range must not truncate");
        need(pc.cpu.read32(0x7fffe000) == 0, "Untouched high RAM must read as zero");
        pc.reset(); drain(pc);
        need(pc.cpu.read32(0x7ffffffc) == 0, "Reset must clear high-address guest data");
        need(pc.cpu.memory.allocated_bytes() == payload, "Reset must release old backing pages");
        need(pc.json().find("\"ram_bytes\":2147483648") != std::string::npos, "JSON must retain unsigned 2 GiB size");
        need(pc.json().find("\"windows_ready\":false") != std::string::npos, "RAM configuration must not claim Windows support");

        // Ask E820 for the extended region using guest instructions, not a direct host callback.
        boot(pc, {0x66,0xb8,0x20,0xe8,0,0, 0x66,0xba,0x50,0x41,0x4d,0x53,
                  0x66,0xb9,0x18,0,0,0, 0x66,0xbb,2,0,0,0, 0xbf,0,0x90, 0xcd,0x15,0xf4});
        drain(pc);
        need(!pc.cpu.fault && pc.cpu.halted, "Guest E820 query must finish");
        need(pc.cpu.read32(0x9000) == 0x100000 && pc.cpu.read32(0x9004) == 0, "E820 extended region start");
        need(pc.cpu.read32(0x9008) == 0x3ff00000 && pc.cpu.read32(0x900c) == 0,
             "E820 usable low RAM must end before the framebuffer aperture");
        need(pc.cpu.regs[3] == 3 && (pc.cpu.eflags & 1) == 0, "E820 must continue through the reserved video range");
        for (const std::uint8_t index : {std::uint8_t{3}, std::uint8_t{4}}) {
            boot(pc, {0x66,0xb8,0x20,0xe8,0,0, 0x66,0xba,0x50,0x41,0x4d,0x53,
                      0x66,0xb9,0x18,0,0,0, 0x66,0xbb,index,0,0,0, 0xbf,0,0x90, 0xcd,0x15,0xf4});
            drain(pc);
            need(!pc.cpu.fault && pc.cpu.halted && !(pc.cpu.eflags & 1), "Guest must enumerate the remaining physical map");
            need(pc.cpu.read32(0x9000) == (index == 3 ? 0x40000000 : 0x48000000) &&
                 pc.cpu.read32(0x9004) == 0, "E820 framebuffer/post-framebuffer start");
            need(pc.cpu.read32(0x9008) == (index == 3 ? 0x08000000 : 0x38000000) &&
                 pc.cpu.read32(0x900c) == 0, "E820 must retain the exact 2-GiB physical boundary");
            need(pc.cpu.read32(0x9010) == (index == 3 ? 2u : 1u),
                 "E820 must reserve VRAM while leaving higher physical RAM usable");
            if (index == 3) {
                const auto first = pc.cpu.read64(0x9000);
                const auto end = first + pc.cpu.read64(0x9008);
                need(pc.cpu.memory.has_window() && first <= pc.cpu.memory.window_base() &&
                     pc.cpu.memory.window_base() + pc.cpu.memory.window_bytes() <= end,
                     "E820 must reserve the complete Ether window");
                pc.cpu.write8(PcMachine::kEtherMemoryPhysicalBase, 0xa6);
                need(pc.cpu.read8(PcMachine::kEtherMemoryPhysicalBase) == 0xa6,
                     "The explicit Ether device must stay accessible inside the reservation");
            }
            need(pc.cpu.regs[3] == (index == 3 ? 4u : 0u), "E820 enumeration completion");
        }

        if (PcMachine::max_ram_mib() >= 4096) {
            constexpr auto physical = PcMachine::kLegacyPhysicalLimit;
            PcMachine high(static_cast<std::size_t>(physical), 768 * MiB);
            const auto e820 = [&](std::uint8_t index) {
                boot(high, {0x66,0xb8,0x20,0xe8,0,0, 0x66,0xba,0x50,0x41,0x4d,0x53,
                            0x66,0xb9,0x18,0,0,0, 0x66,0xbb,index,0,0,0, 0xbf,0,0x90, 0xcd,0x15,0xf4});
                drain(high);
                need(!high.cpu.fault && high.cpu.halted && !high.cpu.flag(cosmic_pc::Cpu::CF), "4 GiB E820 guest query");
            };
            e820(4);
            need(high.cpu.read64(0x9000) == 0x48000000 &&
                 high.cpu.read64(0x9008) == PcMachine::kIoApicAddress - 0x48000000 &&
                 high.cpu.read32(0x9010) == 1 && high.cpu.regs[3] == 5,
                 "Usable upper RAM must end at the high MMIO area");
            e820(5);
            need(high.cpu.read64(0x9000) == PcMachine::kIoApicAddress &&
                 high.cpu.read64(0x9008) == physical - PcMachine::kIoApicAddress &&
                 high.cpu.read32(0x9010) == 2 && high.cpu.regs[3] == 0,
                 "E820 must reserve IOAPIC and LAPIC before completing");
            need(high.usable_ram_bytes() == PcMachine::kIoApicAddress - cosmic_pc::PcVideo::kApertureBytes - 0x60000,
                 "Usable RAM count must exclude the high MMIO, VGA and BIOS ranges");
            high.cpu.write32(PcMachine::kIoApicAddress - 4, 0x12345678);
            need(high.cpu.read32(PcMachine::kIoApicAddress - 4) == 0x12345678 &&
                 high.cpu.memory.allocated_bytes() < MiB, "High guest RAM must have sparse backing");
            boot(high, {0xb8,0x01,0xe8,0xcd,0x15,0xf4});
            drain(high);
            need(!high.cpu.fault && high.cpu.reg16(0) == 15360 && high.cpu.reg16(3) == (1024-16)*16 &&
                 high.cpu.reg16(1) == high.cpu.reg16(0) && high.cpu.reg16(2) == high.cpu.reg16(3),
                 "E801 must describe only contiguous extended RAM before video memory");
        }

        PcMachine below_aperture(static_cast<std::size_t>(PcMachine::kEtherMemoryPhysicalBase + 4096 - 1));
        need(!below_aperture.cpu.memory.has_window() &&
             below_aperture.usable_ram_bytes() == below_aperture.cpu.memory.size() - 0x60000,
             "A truncated aperture cannot overlap ordinary RAM described by E820");

        // A dword store straddles two new pages with only one page of budget left.
        // It must stop explicitly and leave all guest-visible state unchanged.
        PcMachine limited(2 * MiB, 4 * 4096);
        boot(limited, {0x66,0xc7,0x06,0xff,0x9f,0x78,0x56,0x34,0x12,0xf4});
        const auto regs = limited.cpu.regs;
        const auto flags = limited.cpu.eflags;
        limited.run(1);
        need(bool(limited.cpu.fault), "An exhausted RAM budget must stop execution");
        need(limited.cpu.fault->code == cosmic_pc::FaultCode::MemoryBudget, "The fault must identify the memory budget");
        need(limited.cpu.eip == 0x7c00 && limited.cpu.regs == regs && limited.cpu.eflags == flags,
             "Budget fault must preserve registers, flags and instruction address");
        need(limited.cpu.read32(0x9fff) == 0, "Cross-page budget fault must not leak a partial write");
        need(limited.cpu.memory.allocated_bytes() <= limited.cpu.memory.budget_bytes(), "Budget must never be exceeded");
        need(limited.status() == "fault" && !limited.cpu.halted, "Budget exhaustion is a fault, not a successful HLT");

        std::cout << "PC memory: " << checks << " checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
