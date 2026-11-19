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
  if (budget < 2 || !ether_arm64_dispatch_ready()) return 0;

  // Keep short chains for different RIPs. A guarded backedge can use more
  // of the caller's budget without looking up a second block.
  const auto limit = std::min(budget, 64U);
  ether_arm64::ResidentReadPages resident;
  unsigned completed = 0;
  while (completed + 2 <= limit) {
    const auto retired = retire_ether_arm64_block(limit - completed, budget - completed, bus, resident);
    if (!retired) break;
    completed += retired;
    if (bus || scan_loop_batch_candidate() || wide_multiply_loop_batch_candidate() ||
        rotate_xor_loop_batch_candidate() || affine_ram_rotate_loop_candidate()) break;
  }
  return completed;
#else
  (void)budget;
  (void)bus;
  return 0;
#endif
}

inline unsigned Cpu::ether_arm64_loop_length(EtherLoopAccess access) const noexcept {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (const auto* entry = cached_ether_arm64_work(access, true)) return entry->block.count;
#else
  (void)access;
#endif
  return 0;
}

inline void Cpu::EtherLoopWork::run(Clock::time_point deadline) noexcept {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (!owner_ || outcome_ == 1) return;
  frame_.guest_regs = frame_.regs.data();
  const bool timed = deadline != Clock::time_point::max();
  const unsigned deadline_interval = reads_ram() ? 64U : 4096U;
  while (rounds_ < round_limit_) {
    if (timed && Clock::now() >= deadline) return;
    auto rounds = round_limit_ - rounds_;
    if (const auto outcome = entry_->run_state_vector(frame_, rounds)) {
      outcome_ = outcome;
      rounds_ += rounds - frame_.loop_remaining;
      return;
    }
    // This deadline check only observes this worker's frame and host clock.
    if (timed) rounds = std::min(rounds, deadline_interval);
    frame_.loop_remaining = rounds;
    outcome_ = entry_->code.run(frame_);
    if (!outcome_) {
      rounds_ = 0;
      return;
    }
    rounds_ += entry_->block.repeats ? rounds - frame_.loop_remaining : 1;
    if (outcome_ == 1) return;
  }
#else
  (void)deadline;
#endif
}

inline Cpu::EtherLoopWork::InstructionView Cpu::EtherLoopWork::instruction_at(unsigned step) const noexcept {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (entry_ && rounds_) {
    const auto& op = entry_->block.instructions[step % entry_->block.count];
    return {entry_->rip + op.begin, entry_->bytes.data() + op.begin, op.end - op.begin};
  }
#else
  (void)step;
#endif
  return {};
}

inline bool Cpu::prepare_ether_arm64_loop(unsigned budget, EtherLoopWork& work, EtherLoopAccess access) {
  return prepare_ether_arm64_work<false>(budget, work, access);
}

inline bool Cpu::prepare_parallel_ether_arm64_register_block(unsigned budget, EtherLoopWork& work) {
  return prepare_ether_arm64_work<true>(budget, work, EtherLoopAccess::Registers);
}

template <bool ParallelRead>
inline bool Cpu::prepare_ether_arm64_work(unsigned budget, EtherLoopWork& work, EtherLoopAccess access) {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  work.owner_ = nullptr;
  work.entry_ = nullptr;
  work.rounds_ = work.outcome_ = 0;
  const auto* entry = cached_ether_arm64_work(access, !ParallelRead);
  if (!entry || budget < entry->block.count || entry->cr3 != cr3 || entry->cpl != cpl()) return false;
  try {
    bool ordinary_page = false;
    const auto physical = translate_instruction<ParallelRead>(eip, ordinary_page, true);
    const auto version = memory.code_page_version(physical);
    const auto epoch = memory.page_table_epoch();
    if (physical != entry->physical || !version || version != entry->code_version || !epoch ||
        (!ordinary_page && !bulk_ram_range(physical, entry->block.size))) return false;
    const auto& last = entry->block.instructions[entry->block.count - 1];
    decoded_.reserve(last.end - last.begin);
    work.frame_.regs = regs;
    work.frame_.cpu = this;
    work.frame_.read = &Cpu::ether_arm64_read<true>;
    work.frame_.reuse_resident_reads = !entry->block.register_only;
    work.frame_.resident = {};
    work.frame_.resident_read_hits = work.frame_.ram_read_callbacks = 0;
    work.frame_.state_vector_rounds = 0;
    work.round_limit_ = entry->block.repeats ? budget / entry->block.count : 1;
    work.code_version_ = version;
    work.page_epoch_ = epoch;
    work.entry_ = entry;
    work.owner_ = this;
    return true;
  } catch (const std::exception&) {
    return false;
  }
#else
  (void)budget;
  (void)work;
  (void)access;
  return false;
#endif
}

inline unsigned Cpu::retire_ether_arm64_loop(EtherLoopWork& work) {
#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
  if (work.owner_ != this) return 0;
  work.owner_ = nullptr;
  ether_arm64_stats_.ram_read_callbacks += work.frame_.ram_read_callbacks;
  if (!work.rounds_ || !work.outcome_) {
    if (work.frame_.ram_read_callbacks) ++ether_arm64_stats_.data_fallbacks;
    return 0;
  }
  const auto* entry = cached_ether_arm64_work(EtherLoopAccess::ReadOnlyRam, false);
  if (!entry || entry != work.entry_ || cr3 != entry->cr3 || cpl() != entry->cpl ||
      memory.page_table_epoch() != work.page_epoch_ ||
      memory.code_page_version(entry->physical) != work.code_version_) {
    ++ether_arm64_stats_.invalidated;
    return 0;
  }
  ++ether_arm64_stats_.hits;
  ++ether_arm64_stats_.code_version_hits;
  return commit_ether_arm64_result(*entry, work.frame_, work.rounds_, work.outcome_, false, true);
#else
  (void)work;
  return 0;
#endif
}

inline unsigned Cpu::try_ether_arm64_register_loop(unsigned budget, std::unique_lock<std::mutex>& bus) {
  if (!bus.owns_lock()) return 0;
  EtherRegisterWork work;
  const auto limit = std::min(budget, ether_arm64_register_loop_length() * 1024U);
  if (!prepare_ether_arm64_register_loop(limit, work)) return 0;
  bus.unlock();
  work.run();
  bus.lock();
  return retire_ether_arm64_register_loop(work);
}

#if defined(MUNDO_X_ETHER_ARM64_BLOCKS)
inline const ether_arm64::Cache::Entry* Cpu::cached_ether_arm64_work(EtherLoopAccess access,
                                                                  bool loop_only) const noexcept {
  if (!is_long_mode() || !ether_arm64_dispatch_ready()) return nullptr;
  const auto* set = ether_arm64_cache_->find_set(eip);
  if (!set) return nullptr;
  for (const auto& entry : set->entries)
    if (entry.occupied && entry.rip == eip && entry.code &&
        (!loop_only || entry.block.repeats) && (entry.block.register_only ||
           (access == EtherLoopAccess::ReadOnlyRam && !entry.block.writes))) return &entry;
  return nullptr;
}

inline bool Cpu::ether_arm64_dispatch_ready() const noexcept {
  return ether_arm64_block_candidate() && !fault && !halted && !transaction_ && !interrupt_shadow_ &&
         !(eflags & (TF | 0x20000U)) && (cpl() == 0 || cpl() == 3) &&
         !step_trace_hook && !irq_trace_hook;
}

inline unsigned Cpu::retire_ether_arm64_block(unsigned budget, unsigned loop_budget,
                                             std::unique_lock<std::mutex>* bus,
                                             ether_arm64::ResidentReadPages& resident) {
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto origin = eip;
  const bool long_mode = is_long_mode();
  const auto width = std::min(ether_arm64::Block::kMaxBytes, page_bytes - unsigned(eip % page_bytes));
  if (width < 2 || (long_mode
          ? eip > UINT64_MAX - width || !canonical(eip) || !canonical(eip + width - 1)
          : eip > UINT32_MAX)) return 0;
  const auto found = ether_arm64_cache_->lookup(eip);
  if (!found) return 0;
  const auto& lookup = *found;
  auto& entry = lookup.entry;
  if (!entry.checked_bytes && entry.visits < 2) {
    ++entry.visits;
    return 0;
  }
  // An unpaged decode miss only depends on the identity-mapped code bytes.
  // Leave cold or unsupported instructions on the scalar path immediately.
  if (!long_mode && entry.checked_bytes && !entry.block.long_mode && !entry.block.can_translate() &&
      entry.code_version && entry.code_version == memory.code_page_version(eip)) {
    ++ether_arm64_stats_.code_version_hits;
    return 0;
  }

  auto* active = &entry;
  try {
    bool ordinary_page = false;
    const auto physical = long_mode ? translate_instruction(eip, ordinary_page, true) : eip;
    if (!ordinary_page && !bulk_ram_range(physical, width)) {
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
        entry.state_vector = ether_arm64::RegisterStateVector::build(entry.block);
        entry.checked_bytes = entry.block.can_translate() ? entry.block.size : width;
        entry.physical = physical;
        entry.cr3 = cr3;
        entry.cpl = privilege;
        std::copy_n(actual.begin(), entry.checked_bytes, entry.bytes.begin());
      }
      entry.code_version = memory.watch_code_page(physical);
    }
    if (!entry.block.can_translate() || entry.block.count > budget) return 0;
    if (!entry.code) {
      auto* slot = lookup.pending ? ether_arm64_cache_->admission_slot(lookup) : &entry;
      if (!slot) return 0;
      if (lookup.pending && slot->code) {
        ++ether_arm64_stats_.compiled_evictions;
        entry.code = std::move(slot->code);
      }
      const auto start = std::chrono::steady_clock::now();
      const bool built = entry.code.build(entry.block);
      ether_arm64_stats_.native_compile_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(
          std::chrono::steady_clock::now() - start).count();
      if (!built) {
        ether_arm64_cache_->host_rejected = true;
        ether_arm64_cache_->clear();
        if (topology_.logical_processors() == 1) memory.enable_code_page_versions(false);
        return 0;
      }
      ++ether_arm64_stats_.translated;
      if (lookup.pending) {
        ether_arm64_cache_->admit(entry, *slot);
        active = slot;
      }
    } else {
      ++ether_arm64_stats_.hits;
    }

    const auto& block = active->block;
    const auto& last = block.instructions[block.count - 1];
    // Only read-only blocks can repeat during a secondary CPU's turn.
    const auto round_limit = block.repeats && (!bus || (!block.writes && !flag(IF)))
        ? loop_budget / block.count : 1U;
    auto repetitions = std::min(256U, round_limit);
    decoded_.reserve(last.end - last.begin);
    ether_arm64::PendingRamWrites pending;
    const bool staged = block.stages_ram();
    if (staged) {
      pending.code_first = physical;
      pending.code_end = physical + block.size;
      pending.count = 0;
    }
    // A register-only block on the serialized CPU cannot fail a memory
    // guard. It can update the guest register file directly. Blocks that
    // may fall back or release the bus keep a private working copy.
    const bool direct_regs = block.register_only && !bus;
    ether_arm64::Frame frame;
    if (!direct_regs) frame.regs = regs;
    frame.read = &Cpu::ether_arm64_read<false>;
    frame.write = &Cpu::ether_arm64_write;
    frame.cpu = this;
    frame.pending = staged ? &pending : nullptr;
    frame.loop_remaining = repetitions;
    frame.guest_regs = direct_regs ? regs.data() : frame.regs.data();
    frame.reuse_resident_reads = long_mode && !block.writes && !block.register_only;
    if (frame.reuse_resident_reads) frame.resident = resident;
    if (block.writes || !long_mode || bus) resident = {};
    // Register-only code touches only the private frame. Validate its
    // instruction bytes and mapping again before publishing the result.
    const auto code_version = active->code_version;
    const auto mapping_epoch = bus && block.register_only && long_mode ? memory.page_table_epoch() : 0;
    const bool private_frame = bus && block.register_only && code_version &&
                               (!long_mode || mapping_epoch);
    if (private_frame) bus->unlock();
    auto outcome = active->run_state_vector(frame, round_limit);
    if (outcome) repetitions = round_limit;
    else outcome = active->code.run(frame);
    if (private_frame) bus->lock();
    ether_arm64_stats_.ram_read_callbacks += frame.ram_read_callbacks;
    if (private_frame && (!a20_enabled || memory.code_page_version(physical) != code_version ||
        (long_mode && memory.page_table_epoch() != mapping_epoch))) {
      ++ether_arm64_stats_.invalidated;
      active->invalidate(origin);
      return 0;
    }
    if (!outcome) {
      ++ether_arm64_stats_.data_fallbacks;
      return 0; // Discard staged state. The interpreter takes the exact fault.
    }
    const auto rounds = block.repeats ? repetitions - frame.loop_remaining : 1U;
    const auto retired = rounds * block.count;
    if (private_frame) ether_arm64_stats_.off_bus_register_steps += retired;
    if (staged) {
      // A later speculative page walk may have discovered a new page table.
      for (unsigned i = 0; i < pending.count; ++i) {
        const auto& store = pending.stores[i];
        if (memory.may_touch_page_tables(store.physical, store.width)) {
          ++ether_arm64_stats_.data_fallbacks;
          return 0;
        }
      }
      try {
        if (pending.count == 1) {
          const auto& store = pending.stores[0];
          ether_arm64_commit_word(store.physical, store.width, store.value);
        } else {
          // Pin and reserve all destinations before changing guest bytes.
          const auto pins = memory.pin_writes();
          for (unsigned i = 0; i < pending.count; ++i)
            memory.prepare_write(pending.stores[i].physical, pending.stores[i].width);
          for (unsigned i = 0; i < pending.count; ++i) {
            const auto& store = pending.stores[i];
            ether_arm64_commit_word(store.physical, store.width, store.value);
          }
        }
      } catch (const std::exception&) {
        ++ether_arm64_stats_.data_fallbacks;
        return 0;
      }
      ++ether_arm64_stats_.staged_ram_blocks;
      ether_arm64_stats_.staged_ram_steps += retired;
      ether_arm64_stats_.coalesced_ram_writes += block.writes * rounds - pending.count;
    }
    const auto completed = commit_ether_arm64_result(*active, frame, rounds, outcome, direct_regs, private_frame);
    if (frame.reuse_resident_reads && !bus) resident = frame.resident;
    return completed;
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

inline unsigned Cpu::commit_ether_arm64_result(const ether_arm64::Cache::Entry& entry,
                                             const ether_arm64::Frame& frame, unsigned rounds,
                                             unsigned outcome, bool direct_regs, bool private_frame) {
  const auto& block = entry.block;
  const auto& last = block.instructions[block.count - 1];
  const auto origin = entry.rip;
  const auto long_mode = block.long_mode;
  const auto retired = rounds * block.count;
  if (!direct_regs) regs = frame.regs;
  switch (block.flag_op) {
    case ether_arm64::Instruction::SignedMultiply:
      multiply_signed(last.lhs.reg, frame.left, frame.right, block.flag_width);
      break;
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
  if (block.auxiliary_index != ether_arm64::Block::kNoFlagSource &&
      block.auxiliary_index != block.flag_index) {
    const auto kind = block.instructions[block.auxiliary_index].kind;
    const auto left = frame.auxiliary_left;
    const auto right = kind == ether_arm64::Instruction::Increment ||
        kind == ether_arm64::Instruction::Decrement ? 1 : frame.auxiliary_right;
    const auto result = kind == ether_arm64::Instruction::Add ||
        kind == ether_arm64::Instruction::Increment ? left + right : left - right;
    set_flag(AF, ((left ^ right ^ result) & 0x10U) != 0);
  }
  if (block.rotate_carry_index != ether_arm64::Block::kNoFlagSource) {
    const auto& op = block.instructions[block.rotate_carry_index];
    if (op.rhs.immediate || (frame.rotate_count & (op.width * 8 - 1))) {
      const auto carry = op.kind == ether_arm64::Instruction::RotateLeft ? 1 : sign_bit(op.width);
      set_flag(CF, (frame.rotate_carry_result & carry) != 0);
    }
  }
  if (block.rotate_overflow_index != ether_arm64::Block::kNoFlagSource) {
    const auto& op = block.instructions[block.rotate_overflow_index];
    if (op.rhs.immediate || (frame.rotate_count & (op.width * 8 - 1)) == 1) {
      const auto value = block.rotate_overflow_index == block.rotate_carry_index
          ? frame.rotate_carry_result : frame.rotate_overflow_result;
      const auto sign = sign_bit(op.width);
      const auto low = op.kind == ether_arm64::Instruction::RotateLeft ? 1 : sign >> 1;
      set_flag(OF, bool(value & sign) != bool(value & low));
    }
  }
  if (last.kind == ether_arm64::Instruction::ConditionalMove) {
    if (condition(last.opcode & 15)) put_reg(last.lhs.reg, last.width, frame.conditional_source);
    else if (last.width == 4 && (efer & 0x400U))
      put_reg(last.lhs.reg, last.width, get_reg(last.lhs.reg, last.width));
  }
  // Byte arithmetic, parity and inherited flags use the x86 condition.
  const bool taken = last.kind == ether_arm64::Instruction::Conditional && !block.uses_arm_condition()
      ? condition(last.opcode & 15) : outcome == 2;
  retire_ether_decoded_block(block, entry.bytes.data(), origin, taken, retired);
  ++ether_arm64_stats_.blocks;
  ether_arm64_stats_.steps += retired;
  if (frame.state_vector_rounds) {
    ++ether_arm64_stats_.state_vector_dispatches;
    const auto projected = std::uint64_t(frame.state_vector_rounds) * block.count;
    ether_arm64_stats_.state_vector_steps += projected;
    if (!long_mode) ether_arm64_stats_.protected32_state_vector_steps += projected;
  }
  if (block.rotates) ether_arm64_stats_.rotate_steps += rounds * block.rotates;
  if (block.variable_rotates) ether_arm64_stats_.variable_rotate_steps += rounds * block.variable_rotates;
  if (block.last_wide_rotate_offset != ether_arm64::Block::kMaxBytes)
    last_immediate_rotate_ip_ = origin + block.last_wide_rotate_offset;
  if (block.last_variable_rotate_offset != ether_arm64::Block::kMaxBytes)
    last_variable_rotate_ip_ = origin + block.last_variable_rotate_offset;
  if (!long_mode) ether_arm64_stats_.protected32_steps += retired;
  ether_arm64_stats_.resident_read_hits += frame.resident_read_hits;
  if (rounds > 1) {
    ++ether_arm64_stats_.repeated_loop_dispatches;
    ether_arm64_stats_.repeated_loop_steps += retired;
    if (private_frame) ether_arm64_stats_.private_loop_steps += retired;
  }
  return retired;
}

template <bool ParallelRead>
inline std::optional<std::uint64_t> Cpu::ether_arm64_ram_address(
    std::uint64_t address, unsigned width, bool stack, Access access) {
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  address = segment_linear(stack ? SS : DS, address, width, access);
  if (address % page_bytes > page_bytes - width)
    return std::nullopt;
  const auto physical = translate<false, ParallelRead>(address, access, nullptr, true);
  if (!bulk_ram_range(physical, width)) return std::nullopt;
  return physical;
}

template <bool ParallelRead>
inline unsigned Cpu::ether_arm64_read(ether_arm64::Frame* frame, std::uint64_t address,
                                      unsigned spec) noexcept {
  auto& cpu = *static_cast<Cpu*>(frame->cpu);
  ++frame->ram_read_callbacks;
  try {
    const auto width = spec & (ether_arm64::kStackAccess - 1);
    const auto physical = cpu.ether_arm64_ram_address<ParallelRead>(
        address, width, spec & ether_arm64::kStackAccess, Access::Read);
    if (!physical) return 0;
    if (frame->reuse_resident_reads) {
      constexpr auto page_bytes = MemoryFabric::kPageSize;
      static_assert(page_bytes == ether_arm64::kResidentReadPageBytes);
      const auto offset = static_cast<unsigned>(address % page_bytes);
      const auto start = *physical - offset;
      if (cpu.bulk_ram_range(start, page_bytes)) {
        if (const auto* bytes = cpu.memory.resident_ram_span(start, page_bytes, !ParallelRead)) {
          frame->resident.previous = frame->resident.recent;
          frame->resident.recent = {bytes, address - offset};
          frame->loaded = little_endian_word(bytes + offset, width);
          return 1;
        }
      }
    }
    if constexpr (ParallelRead) frame->loaded = cpu.read_ram_span<true>(*physical, width);
    else frame->loaded = cpu.ether_arm64_load_ram(*frame, *physical, width);
    return 1;
  } catch (const std::exception&) {
    return 0;
  }
}

inline std::uint64_t Cpu::ether_arm64_load_ram(const ether_arm64::Frame& frame, std::uint64_t physical,
                                               unsigned width) {
  if (!frame.pending) return read_ram_span(physical, width);
  return frame.pending->load(physical, width, [&] { return read_ram_span(physical, width); });
}

inline void Cpu::ether_arm64_commit_word(std::uint64_t physical, unsigned width, std::uint64_t value) {
  std::array<std::uint8_t, 8> data;
  for (unsigned i = 0; i < width; ++i) data[i] = std::uint8_t(value >> (8 * i));
  memory.write_block(physical, data.data(), width);
}

inline unsigned Cpu::ether_arm64_write(ether_arm64::Frame* frame, std::uint64_t address,
                                       unsigned spec, std::uint64_t value) noexcept {
  auto& cpu = *static_cast<Cpu*>(frame->cpu);
  try {
    const auto width = spec & (ether_arm64::kStackAccess - 1);
    const auto physical = cpu.ether_arm64_ram_address(
        address, width, spec & ether_arm64::kStackAccess, Access::Write);
    if (!physical || cpu.memory.may_touch_page_tables(*physical, width)) return 0;
    if (spec & ether_arm64::kSubtractAccess) {
      // This x64 RAM form has no separate read permission from its validated write.
      frame->left = cpu.ether_arm64_load_ram(*frame, *physical, width);
      frame->right = value;
      value = frame->left - value;
    }
    if (frame->pending) return frame->pending->stage(*physical, width, value);
    cpu.ether_arm64_commit_word(*physical, width, value);
    return 1;
  } catch (const std::exception&) {
    return 0;
  }
}
#endif
