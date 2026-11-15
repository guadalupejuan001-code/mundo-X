inline bool Cpu::ether_arm64_block_candidate() const noexcept {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (!ether_arm64_blocks_enabled() || !a20_enabled) return false;
  if (is_long_mode())
    return page_translation_cache_enabled_ && (cr0 & 0x80000001U) == 0x80000001U && (cr4 & 0x20U);
  // Every branch target and fetch must be valid within the current CS.
  // DS and SS can have distinct bases, limits and permissions and are
  // checked separately by each translated memory access.
  const auto& code = cache_[CS];
  return (cr0 & 0x80000001U) == 1 && !(efer & 0x400U) && code.valid &&
         code.code && code.big && !code.expand_down && !code.base && code.limit == UINT32_MAX;
#else
  return false;
#endif
}

inline unsigned Cpu::try_ether_arm64_block(unsigned budget, std::unique_lock<std::mutex>* bus) {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (!ether_arm64_block_candidate() || budget < 2 || fault || halted || transaction_ ||
      interrupt_shadow_ ||
      (eflags & (TF | 0x20000U)) || (cpl() != 0 && cpl() != 3) ||
      step_trace_hook || irq_trace_hook)
    return 0;

  // Return to the machine at its next observation boundary. Short chains
  // amortize dispatch while leaving the specialized scanner first in line.
  const auto limit = std::min(budget, 64U);
  unsigned completed = 0;
  while (limit - completed >= 2) {
    const auto retired = retire_ether_arm64_block(limit - completed, bus);
    if (!retired) break;
    completed += retired;
    if (bus || scan_loop_batch_candidate()) break;
  }
  return completed;
#else
  (void)budget;
  (void)bus;
  return 0;
#endif
}

#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
inline unsigned Cpu::retire_ether_arm64_block(unsigned budget, std::unique_lock<std::mutex>* bus) {
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto origin = eip;
  const bool long_mode = is_long_mode();
  const auto width = std::min(ether_arm64::Block::kMaxBytes, page_bytes - unsigned(eip % page_bytes));
  if (width < 2 || (long_mode
          ? eip > UINT64_MAX - width || !canonical(eip) || !canonical(eip + width - 1)
          : eip > UINT32_MAX)) return 0;
  const auto lookup = ether_arm64_cache_->lookup(eip);
  auto& entry = lookup.entry;
  if (!entry.checked_bytes && entry.visits < 2) {
    ++entry.visits;
    return 0;
  }
  // An unpaged decode miss only depends on the identity-mapped code bytes.
  // Leave cold or unsupported instructions on the scalar path immediately.
  if (!long_mode && entry.checked_bytes && !entry.block.long_mode && entry.block.count < 2 &&
      entry.code_version && entry.code_version == memory.code_page_version(eip)) {
    ++ether_arm64_stats_.code_version_hits;
    return 0;
  }

  auto* active = &entry;
  try {
    const auto physical = translate(eip, Access::Execute, nullptr, true);
    if (!bulk_ram_range(physical, width)) {
      if (entry.code) {
        ++ether_arm64_stats_.invalidated;
        entry.invalidate(eip);
      }
      return 0;
    }
    const auto privilege = cpl();
    const bool same_mapping = entry.checked_bytes && entry.physical == physical &&
                              entry.cr3 == cr3 && entry.cpl == privilege &&
                              entry.block.long_mode == long_mode;
    const auto version = memory.code_page_version(physical);
    if (same_mapping && version && version == entry.code_version) {
      ++ether_arm64_stats_.code_version_hits;
    } else {
      ++ether_arm64_stats_.code_byte_checks;
      std::array<std::uint8_t, ether_arm64::Block::kMaxBytes> actual;
      const auto length = same_mapping ? entry.checked_bytes : width;
      memory.read_block(physical, actual.data(), length);
      ether_arm64_stats_.code_bytes_read += length;
      if (!same_mapping || std::memcmp(actual.data(), entry.bytes.data(), length) != 0) {
        if (entry.code) ++ether_arm64_stats_.invalidated;
        entry.invalidate(eip);
        if (length < width) {
          memory.read_block(physical + length, actual.data() + length, width - length);
          ether_arm64_stats_.code_bytes_read += width - length;
        }
        entry.block = ether_arm64::decode(actual.data(), width, eip, long_mode);
        entry.checked_bytes = entry.block.count < 2 ? width : entry.block.size;
        entry.physical = physical;
        entry.cr3 = cr3;
        entry.cpl = privilege;
        std::copy_n(actual.begin(), entry.checked_bytes, entry.bytes.begin());
      }
      entry.code_version = memory.watch_code_page(physical);
    }
    if (entry.block.count < 2 || entry.block.count > budget) return 0;
    if (!entry.code) {
      const auto start = std::chrono::steady_clock::now();
      const bool built = entry.code.build(entry.block);
      ether_arm64_stats_.native_compile_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
      if (!built) {
        ether_arm64_cache_->host_rejected = true;
        if (topology_.logical_processors() == 1) memory.enable_code_page_versions(false);
        return 0;
      }
      ++ether_arm64_stats_.translated;
      if (lookup.pending)
        active = &ether_arm64_cache_->admit(entry, ether_arm64_stats_.compiled_evictions);
    } else {
      ++ether_arm64_stats_.hits;
    }

    const auto& block = active->block;
    const auto& last = block.instructions[block.count - 1];
    decoded_.reserve(last.end - last.begin);
    ether_arm64::Frame frame{regs, 0, 0, 0, &Cpu::ether_arm64_read, &Cpu::ether_arm64_write, this};
    // The code bytes and translations were validated under the bus lock.
    // Register-only native code touches only this CPU's private frame. The
    // motherboard defers asynchronous INIT/SIPI until this CPU yields.
    const bool private_frame = bus && std::none_of(block.instructions.begin(),
        block.instructions.begin() + block.count, [](const auto& instruction) {
          return instruction.lhs.memory || instruction.rhs.memory;
        });
    if (private_frame) bus->unlock();
    const auto outcome = active->code.run(frame);
    if (private_frame) bus->lock();
    if (!outcome) {
      ++ether_arm64_stats_.data_fallbacks;
      return 0; // Discard staged state. The interpreter takes the exact fault.
    }
    regs = frame.regs;
    switch (block.flag_op) {
      case ether_arm64::Instruction::Add:
        alu(0, frame.left, frame.right, block.flag_width);
        break;
      case ether_arm64::Instruction::Subtract:
        alu(5, frame.left, frame.right, block.flag_width);
        break;
      case ether_arm64::Instruction::Compare:
        alu(7, frame.left, frame.right, block.flag_width);
        break;
      case ether_arm64::Instruction::Test:
      case ether_arm64::Instruction::And:
        alu(4, frame.left, frame.right, block.flag_width);
        break;
      case ether_arm64::Instruction::Or:
        alu(1, frame.left, frame.right, block.flag_width);
        break;
      case ether_arm64::Instruction::Xor:
        alu(6, frame.left, frame.right, block.flag_width);
        break;
      case ether_arm64::Instruction::ShiftLeft:
      case ether_arm64::Instruction::ShiftRight:
      case ether_arm64::Instruction::ShiftArithmetic:
        shift_register_result(block.flag_op == ether_arm64::Instruction::ShiftLeft ? 4
            : block.flag_op == ether_arm64::Instruction::ShiftRight ? 5 : 7,
            block.flag_width, unsigned(frame.right), frame.left);
        break;
      case ether_arm64::Instruction::Increment:
      case ether_arm64::Instruction::Decrement: {
        const auto carry = flag(CF);
        alu(block.flag_op == ether_arm64::Instruction::Increment ? 0 : 5,
            frame.left, 1, block.flag_width);
        set_flag(CF, carry);
        break;
      }
      default: break;
    }
    start_cs_ = segments[CS];
    start_ip_ = origin + last.begin;
    cursor_ = origin + last.end;
    // Byte arithmetic, parity and inherited flags use the x86 condition.
    ip_written_ = last.kind == ether_arm64::Instruction::Conditional &&
                  !block.uses_arm_condition()
        ? condition(last.opcode & 15) : outcome == 2;
    eip = ip_written_ ? block.target : long_mode ? cursor_ : std::uint32_t(cursor_);
    opcode_ = std::uint16_t(last.opcode);
    repeat_ = 0;
    decoded_.assign(active->bytes.begin() + last.begin, active->bytes.begin() + last.end);
    instructions += block.count;
    eflags |= 2;
    ++ether_arm64_stats_.blocks;
    ether_arm64_stats_.steps += block.count;
    if (!long_mode) ether_arm64_stats_.protected32_steps += block.count;
    return block.count;
  } catch (const std::exception&) {
    // Speculative walks never set A/D bits or access an I/O provider.
    // Preserve the original scalar path for the first architectural fault.
    if (active->code) {
      ++ether_arm64_stats_.invalidated;
      active->invalidate(origin);
    }
    return 0;
  }
}

inline std::optional<std::uint64_t> Cpu::ether_arm64_ram_address(
    std::uint64_t address, unsigned width, bool stack, Access access) {
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  address = segment_linear(stack ? SS : DS, address, width, access);
  if (address % page_bytes > page_bytes - width)
    return std::nullopt;
  const auto physical = translate(address, access, nullptr, true);
  if (!bulk_ram_range(physical, width)) return std::nullopt;
  return physical;
}

inline unsigned Cpu::ether_arm64_read(ether_arm64::Frame* frame, std::uint64_t address,
                                      unsigned spec) noexcept {
  auto& cpu = *static_cast<Cpu*>(frame->cpu);
  try {
    const auto width = spec & (ether_arm64::kStackAccess - 1);
    const auto physical = cpu.ether_arm64_ram_address(
        address, width, spec & ether_arm64::kStackAccess, Access::Read);
    if (!physical) return 0;
    frame->loaded = cpu.read_ram_span(*physical, width);
    return 1;
  } catch (const std::exception&) {
    return 0;
  }
}

inline unsigned Cpu::ether_arm64_write(ether_arm64::Frame* frame, std::uint64_t address,
                                       unsigned spec, std::uint64_t value) noexcept {
  auto& cpu = *static_cast<Cpu*>(frame->cpu);
  try {
    const auto width = spec & (ether_arm64::kStackAccess - 1);
    const auto physical = cpu.ether_arm64_ram_address(
        address, width, spec & ether_arm64::kStackAccess, Access::Write);
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
