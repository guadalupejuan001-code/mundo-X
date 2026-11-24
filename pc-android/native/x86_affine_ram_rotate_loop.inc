inline std::uint64_t Cpu::try_affine_ram_rotate_loop(std::uint64_t step_budget) {
  if (!affine_ram_rotate_loop_candidate() || !can_cache_translations() ||
      fault || halted || transaction_ || interrupt_shadow_ || (eflags & kScalarExecutionFlags) ||
      (cpl() != 0 && cpl() != 3) || step_trace_hook || irq_trace_hook) return 0;

  using Instruction = ether_arm64::Instruction;
  using Operand = ether_arm64::Operand;
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto length = unsigned(cursor_ - eip);
  try {
    const auto physical = counted_loop_ram_address(CS, eip, length, Access::Execute);
    if (!physical) return 0;
    const auto version = memory.code_page_version(*physical);
    const auto& cache = affine_ram_rotate_block_;
    if (!cache || !version || cache->version != version || cache->ip != eip ||
        cache->physical != *physical || cache->length != length) {
      std::array<std::uint8_t, ether_arm64::Block::kNarrowMaxBytes> code;
      memory.read_block(*physical, code.data(), length);
      affine_ram_rotate_block_ = AffineRamRotateBlock{eip, *physical, memory.watch_code_page(*physical),
          length, ether_arm64::decode(code.data(), length, eip)};
    }
  } catch (const std::exception&) {
    return 0;
  }
  const auto& block = affine_ram_rotate_block_->block;
  if (block.size != length || block.count < 3 || block.target != eip ||
      block.instructions[block.count - 1].opcode != 0x75 ||
      step_budget / block.count < 2) return 0;
  const auto& sub = block.instructions[block.count - 2];
  if (sub.kind != Instruction::Subtract || sub.width != 8 || sub.lhs.memory) return 0;
  const auto counter = sub.lhs.reg;

  // Derive the registers from the first mask read. Only the accumulator,
  // pointer and counter may change, and the final SUB defines the flags.
  const unsigned body = block.count - 2;
  unsigned accumulator = Operand::kNoIndex, pointer = Operand::kNoIndex;
  for (unsigned i = 0; i < body; ++i) {
    const auto& op = block.instructions[i];
    if (op.kind != Instruction::Xor || !op.rhs.memory) continue;
    accumulator = op.lhs.reg;
    pointer = op.rhs.reg;
    break;
  }
  if (accumulator >= Operand::kNoIndex || pointer >= Operand::kNoIndex ||
      accumulator == pointer || accumulator == counter || pointer == counter) return 0;
  const auto invariant = [&](const Operand& operand) -> std::optional<std::uint64_t> {
    if (operand.memory) return std::nullopt;
    if (operand.immediate) return operand.immediate_value;
    if (operand.reg >= Operand::kNoIndex || operand.reg == accumulator ||
        operand.reg == pointer || operand.reg == counter) return std::nullopt;
    return regs[operand.reg];
  };
  const auto decrement = invariant(sub.rhs);
  if (!decrement || !*decrement) return 0;

  struct Term { std::uint64_t offset; unsigned rotation; };
  const auto rotate = [](std::uint64_t value, unsigned left) {
    return left ? (value << left) | (value >> (64 - left)) : value;
  };
  std::array<Term, ether_arm64::Block::kNarrowMaxInstructions> masks;
  unsigned mask_count = 0, rotation = 0;
  std::uint64_t stride = 0, constant_mask = 0;
  for (unsigned i = 0; i < body; ++i) {
    const auto& op = block.instructions[i];
    if (op.kind == Instruction::Nop) continue;
    if (op.width != 8 || op.lhs.memory) return 0;
    if (op.kind == Instruction::Xor && op.lhs.reg == accumulator) {
      if (op.rhs.memory) {
        if (op.rhs.reg != pointer || op.rhs.index != Operand::kNoIndex) return 0;
        masks[mask_count++] = {stride + std::uint64_t(signed_value(op.rhs.displacement, 4)), rotation};
      } else {
        const auto constant = invariant(op.rhs);
        if (!constant) return 0;
        constant_mask ^= *constant;
      }
    } else if (op.kind == Instruction::Add && op.lhs.reg == pointer) {
      const auto increment = invariant(op.rhs);
      if (!increment) return 0;
      stride += *increment;
    } else if (Instruction::rotate(op.kind) && op.lhs.reg == accumulator) {
      const auto count = invariant(op.rhs);
      if (!count) return 0;
      const auto amount = unsigned(*count) & 63U;
      const auto left = (op.kind == Instruction::RotateRight ? 64 - amount : amount) & 63U;
      rotation = (rotation + left) & 63U;
      constant_mask = rotate(constant_mask, left);
    } else {
      return 0;
    }
  }
  if (!mask_count) return 0;
  for (unsigned term = 0; term < mask_count; ++term)
    masks[term].rotation = (rotation + 64 - masks[term].rotation) & 63U;

  // The loop writes no memory. Reuse an ordinary page mapping inside this
  // device-bounded slice. A failed guard leaves that round for scalar x86.
  auto linear_page = UINT64_MAX;
  std::uint64_t physical_page = 0;
  const auto read_mask = [&](std::uint64_t address) -> std::optional<std::uint64_t> {
    const auto offset = address % page_bytes;
    if (offset > page_bytes - 8) return std::nullopt;
    const auto page = address - offset;
    if (page != linear_page) {
      const auto physical = counted_loop_ram_address(DS, page, page_bytes, Access::Read);
      if (!physical) return std::nullopt;
      physical_page = *physical;
      linear_page = page;
    }
    return read_ram_span(physical_page + offset, 8);
  };

  auto address = regs[pointer], remaining = regs[counter];
  std::uint64_t rounds = 0, composed_mask = 0;
  const auto limit = step_budget / block.count;
  try {
    while (rounds < limit) {
      auto mask = constant_mask;
      unsigned i = 0;
      for (; i < mask_count; ++i) {
        const auto word = read_mask(address + masks[i].offset);
        if (!word) break;
        mask ^= rotate(*word, masks[i].rotation);
      }
      if (i != mask_count) break;
      // T(x) = R(x) XOR M, with M derived from the actual words read this round.
      composed_mask = rotate(composed_mask, rotation) ^ mask;
      address += stride;
      remaining -= *decrement;
      ++rounds;
      if (!remaining) break;
    }
  } catch (const std::exception&) {
    // No operation from the incomplete round has changed architectural state.
  }
  if (rounds < 2) return 0;
  regs[accumulator] = rotate(regs[accumulator], unsigned(rounds * rotation) & 63U) ^ composed_mask;
  regs[pointer] = address;
  regs[counter] = alu(5, remaining + *decrement, *decrement, 8);
  ip_written_ = remaining != 0;
  if (!ip_written_) eip = cursor_;
  const auto steps = rounds * block.count;
  instructions += steps;
  return steps;
}
