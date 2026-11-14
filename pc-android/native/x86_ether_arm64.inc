inline unsigned Cpu::try_ether_arm64_block(unsigned budget) {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (!ether_arm64_blocks_enabled() || budget < 2 || fault || halted || transaction_ ||
      interrupt_shadow_ || !a20_enabled || !page_translation_cache_enabled_ ||
      (cr0 & 0x80000001U) != 0x80000001U || !(cr4 & 0x20U) || !is_long_mode() ||
      (eflags & (TF | 0x20000U)) || (cpl() != 0 && cpl() != 3) ||
      step_trace_hook || irq_trace_hook || !canonical(eip))
    return 0;

  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto origin = eip;
  const auto width = std::min(ether_arm64::Block::kMaxBytes, page_bytes - unsigned(eip % page_bytes));
  if (width < 2 || eip > UINT64_MAX - width || !canonical(eip + width - 1)) return 0;
  auto& entry = ether_arm64_cache_->entry(eip);
  if (entry.rip != eip) entry.invalidate(eip);
  if (!entry.checked_bytes && entry.visits < 2) {
    ++entry.visits;
    return 0;
  }

  try {
    const auto physical = translate(eip, Access::Execute, nullptr, true);
    if (!bulk_ram_range(physical, width)) {
      if (entry.code) {
        ++ether_arm64_stats_.invalidated;
        entry.invalidate(eip);
      }
      return 0;
    }
    std::array<std::uint8_t, ether_arm64::Block::kMaxBytes> actual;
    const auto length = entry.checked_bytes ? entry.checked_bytes : width;
    memory.read_block(physical, actual.data(), length);
    const auto same_code = [&] {
      return std::memcmp(actual.data(), entry.bytes.data(), length) == 0;
    };
    if (entry.checked_bytes && (entry.physical != physical || entry.cr3 != cr3 ||
                               entry.cpl != cpl() || !same_code())) {
      if (entry.code) ++ether_arm64_stats_.invalidated;
      entry.invalidate(eip);
      if (length < width)
        memory.read_block(physical + length, actual.data() + length, width - length);
    }
    if (!entry.checked_bytes) {
      entry.block = ether_arm64::decode(actual.data(), width, eip);
      entry.checked_bytes = entry.block.count < 2 ? width : entry.block.size;
      entry.physical = physical;
      entry.cr3 = cr3;
      entry.cpl = cpl();
      std::copy_n(actual.begin(), entry.checked_bytes, entry.bytes.begin());
    }
    const auto& block = entry.block;
    if (block.count < 2 || block.count > budget) return 0;
    if (!entry.code) {
      if (!entry.code.build(block)) {
        ether_arm64_cache_->host_rejected = true;
        return 0;
      }
      ++ether_arm64_stats_.translated;
    } else {
      ++ether_arm64_stats_.hits;
    }

    ether_arm64::Frame frame{regs, 0, 0, 0, &Cpu::ether_arm64_read, &Cpu::ether_arm64_write, this};
    const auto outcome = entry.code.run(frame);
    if (!outcome) {
      ++ether_arm64_stats_.data_fallbacks;
      return 0; // Discard staged state. The interpreter takes the exact fault.
    }
    regs = frame.regs;
    if (block.compare_width) alu(7, frame.left, frame.right, block.compare_width);
    const auto& last = block.instructions[block.count - 1];
    start_cs_ = segments[CS];
    start_ip_ = origin + last.begin;
    cursor_ = origin + last.end;
    ip_written_ = outcome == 2;
    eip = ip_written_ ? block.target : cursor_;
    opcode_ = std::uint16_t(last.opcode);
    repeat_ = 0;
    decoded_.assign(entry.bytes.begin() + last.begin, entry.bytes.begin() + last.end);
    instructions += block.count;
    eflags |= 2;
    ++ether_arm64_stats_.blocks;
    ether_arm64_stats_.steps += block.count;
    return block.count;
  } catch (const std::exception&) {
    // Speculative walks never set A/D bits or access an I/O provider.
    // Preserve the original scalar path for the first architectural fault.
    if (entry.code) {
      ++ether_arm64_stats_.invalidated;
      entry.invalidate(eip);
    }
    return 0;
  }
#else
  (void)budget;
  return 0;
#endif
}

#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
inline std::optional<std::uint64_t> Cpu::ether_arm64_ram_address(
    std::uint64_t address, unsigned width, Access access) {
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  if (address % page_bytes > page_bytes - width || !canonical(address) ||
      address > UINT64_MAX - (width - 1) || !canonical(address + width - 1))
    return std::nullopt;
  const auto physical = translate(address, access, nullptr, true);
  if (!bulk_ram_range(physical, width)) return std::nullopt;
  return physical;
}

inline unsigned Cpu::ether_arm64_read(ether_arm64::Frame* frame, std::uint64_t address,
                                      unsigned width) noexcept {
  auto& cpu = *static_cast<Cpu*>(frame->cpu);
  try {
    const auto physical = cpu.ether_arm64_ram_address(address, width, Access::Read);
    if (!physical) return 0;
    frame->loaded = cpu.read_ram_span(*physical, width);
    return 1;
  } catch (const std::exception&) {
    return 0;
  }
}

inline unsigned Cpu::ether_arm64_write(ether_arm64::Frame* frame, std::uint64_t address,
                                       unsigned width, std::uint64_t value) noexcept {
  auto& cpu = *static_cast<Cpu*>(frame->cpu);
  try {
    const auto physical = cpu.ether_arm64_ram_address(address, width, Access::Write);
    if (!physical || cpu.memory.may_touch_page_tables(*physical, width)) return 0;
    std::array<std::uint8_t, 8> data;
    for (unsigned i = 0; i < width; ++i) data[i] = std::uint8_t(value >> (8 * i));
    cpu.memory.write_block(*physical, data.data(), width);
    return 1;
  } catch (const std::exception&) {
    return 0;
  }
}
#endif
