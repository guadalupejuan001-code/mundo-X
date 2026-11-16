// Guest-visible platform registers for the Intel-compatible Windows profile.
// Memory type registers retain the guest values. MemoryFabric is coherent
// and has no modeled host-cache state for these attributes to invalidate.
inline int Cpu::fixed_mtrr_index(std::uint32_t number) noexcept {
  if (number == 0x250) return 0;
  if (number >= 0x258 && number <= 0x259) return 1 + int(number - 0x258);
  if (number >= 0x268 && number <= 0x26f) return 3 + int(number - 0x268);
  return -1;
}

inline std::optional<std::uint64_t> Cpu::read_platform_msr(std::uint32_t number) const noexcept {
  if (number >= 0x200 && number <= 0x20f)
    return platform_msrs_.mtrr_variable[number - 0x200];
  if (const auto index = fixed_mtrr_index(number); index >= 0)
    return platform_msrs_.mtrr_fixed[index];
  switch (number) {
    // Platform 1 identifies the emulated package independently of its ISA
    // signature. Physical firmware updates do not describe this processor.
    case 0x17: return 1ULL << 50;
    case 0xfe: return 0x508; // Eight variable ranges, fixed ranges and WC.
    case 0x277: return platform_msrs_.pat;
    case 0x2ff: return platform_msrs_.mtrr_default_type;
    case 0x179: return 1; // One MCA bank. No global control or CMCI extensions.
    case 0x1a0: return platform_msrs_.misc_enable | 0x1800U; // No BTS or PEBS.
    case 0x400: return platform_msrs_.machine_check_control;
    // No hardware errors or firmware microcode updates are pending.
    case 0x8b: case 0x17a: case 0x401: case 0x402: case 0x403: return 0;
    default: return std::nullopt;
  }
}

inline bool Cpu::write_platform_msr(std::uint32_t number, std::uint64_t value) {
  const auto valid_type = [](unsigned type) {
    return type == 0 || type == 1 || type == 4 || type == 5 || type == 6;
  };
  const auto require = [&](bool valid) {
    if (!valid) fail("reserved platform MSR bits or memory type", FaultCode::ProtectionFault);
  };
  if (number >= 0x200 && number <= 0x20f) {
    const auto valid_bits = physical_page_mask_ | (number & 1 ? 0x800U : 0xffU);
    require(!(value & ~valid_bits) && (number & 1 || valid_type(value & 0xffU)));
    platform_msrs_.mtrr_variable[number - 0x200] = value;
    return true;
  }
  if (const auto index = fixed_mtrr_index(number); index >= 0) {
    for (unsigned shift = 0; shift < 64; shift += 8) require(valid_type((value >> shift) & 0xffU));
    platform_msrs_.mtrr_fixed[index] = value;
    return true;
  }
  switch (number) {
    case 0x1a0: {
      constexpr auto writable = kMiscXdDisable | 1;
      require(!(value & ~(writable | 0x1800U)));
      if ((platform_msrs_.misc_enable ^ value) & kMiscXdDisable) {
        if (value & kMiscXdDisable) efer &= ~0x800ULL;
        memory.invalidate_page_tables();
      }
      platform_msrs_.misc_enable = value & writable;
      return true;
    }
    case 0x277:
      for (unsigned shift = 0; shift < 64; shift += 8) {
        const auto type = (value >> shift) & 0xffU;
        require(valid_type(type) || type == 7);
      }
      platform_msrs_.pat = value;
      return true;
    case 0x2ff:
      require(!(value & ~0xcffULL) && valid_type(value & 0xffU));
      platform_msrs_.mtrr_default_type = value;
      return true;
    case 0x400:
      platform_msrs_.machine_check_control = value;
      return true;
    case 0x8b: case 0x17a: case 0x401: case 0x402: case 0x403:
      require(!value);
      return true;
    case 0x17: case 0xfe: case 0x179:
      require(false);
      return true;
    default:
      return false;
  }
}

inline unsigned Cpu::io_breakpoint_mask(std::uint16_t port, unsigned bytes) const noexcept {
  if (!(cr4 & 8U) || !(dr7_ & 0xffU)) return 0;
  unsigned hits = 0;
  for (unsigned i = 0; i < debug_address_.size(); ++i) {
    if (!(dr7_ & (3U << (2 * i)))) continue;
    const auto control = (dr7_ >> (16 + 4 * i)) & 15U;
    if ((control & 3U) != 2 || (control >> 2) == 2 || debug_address_[i] > UINT16_MAX) continue;
    const auto length = 1U + (control >> 2);
    const auto watch = unsigned(debug_address_[i]) & ~(length - 1);
    if (watch < unsigned(port) + bytes && unsigned(port) < watch + length) hits |= 1U << i;
  }
  return hits;
}

template <bool Inspect>
inline std::uint64_t Cpu::translate_legacy(std::uint64_t address, Access access, bool speculative,
                                           unsigned* mapping_bytes_left) {
  // Legacy two-level paging with 4-KiB or 4-MiB pages. The existing iTLB and
  // batch translation fast paths are guarded for four-level IA-32e.
  if (speculative) return UINT64_MAX;
  address = std::uint32_t(address);
  if constexpr (!Inspect) ++page_walks;
  const auto directory = (cr3 & 0xfffff000ULL) + (address >> 22) * 4;
  if constexpr (Inspect)
    if (!bulk_ram_range(directory, 4)) return UINT64_MAX;
  const auto pde = read32(directory);
  if (!(pde & 1)) page_fault(address, access, false, false, "legacy page directory entry is not present");
  const bool large = pde & 0x80U;
  if (large && (!(cr4 & 0x10U) || (pde & 0x003fe000U)))
    page_fault(address, access, true, true, "reserved legacy large-page bits");
  auto pte = pde;
  auto leaf_address = directory;
  if (!large) {
    leaf_address = (pde & 0xfffff000U) + ((address >> 12) & 0x3ffU) * 4;
    if constexpr (Inspect)
      if (!bulk_ram_range(leaf_address, 4)) return UINT64_MAX;
    pte = read32(leaf_address);
    if (!(pte & 1)) page_fault(address, access, false, false, "legacy page table entry is not present");
  }
  const bool user = cpl() == 3 && !supervisor_internal_;
  if (user && !(pde & pte & 4U))
    page_fault(address, access, true, false, "legacy page denies user access");
  if (access == Access::Write && !(pde & pte & 2U) && (user || (cr0 & 0x10000U)))
    page_fault(address, access, true, false, "legacy page is read-only");
  if constexpr (!Inspect) {
    if (!large && !(pde & 0x20U)) write32(directory, pde | 0x20U);
    const auto accessed = pte | 0x20U | (access == Access::Write ? 0x40U : 0U);
    if (accessed != pte) write32(leaf_address, accessed);
  }
  const auto offset_mask = large ? 0x3fffffU : 0xfffU;
  if (mapping_bytes_left) *mapping_bytes_left = offset_mask + 1U - unsigned(address & offset_mask);
  return (pte & (0xfffff000U & ~offset_mask)) | (address & offset_mask);
}
