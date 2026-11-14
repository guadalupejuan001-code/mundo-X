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
}

int main() {
    try {
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
            constexpr auto physical = PcMachine::kMaxPhysicalBytes;
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
