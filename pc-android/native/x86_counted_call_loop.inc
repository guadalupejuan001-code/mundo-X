inline std::optional<std::uint64_t> Cpu::counted_loop_ram_address(
    unsigned segment, std::uint64_t offset, unsigned bytes, Access access) {
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto linear = segment_linear(segment, offset, bytes, access);
  if (bytes > page_bytes - (linear % page_bytes)) return std::nullopt;
  const auto physical = translate(linear, access, nullptr, true);
  if (!bulk_ram_range(physical, bytes)) return std::nullopt;
  return physical;
}

inline std::optional<Cpu::CountedCallLoop> Cpu::decode_counted_call_loop(
    const std::uint8_t* code, unsigned length) {
  using Instruction = ether_arm64::Instruction;
  using Operand = ether_arm64::Operand;

  // The first operation selects a loop with a 64-bit immediate ADD or SUB.
  if ((code[0] & 0xf8U) != 0x48U ||
      (code[1] != 0x81 && code[1] != 0x83 && code[1] != 0x05 && code[1] != 0x2d))
    return std::nullopt;
  const auto body = ether_arm64::decode(code, length, eip);
  const auto call_offset = body.size;
  if (body.count < 2 || length - call_offset < 10 || code[call_offset] != 0xe8) return std::nullopt;
  const auto& accumulator = body.instructions[0];
  if ((accumulator.kind != Instruction::Add && accumulator.kind != Instruction::Subtract) ||
      accumulator.width != 8 || accumulator.lhs.memory || !accumulator.rhs.immediate)
    return std::nullopt;

  const unsigned return_offset = call_offset + 5;
  const auto return_ip = eip + return_offset;
  const auto displacement = little_endian_word(code + call_offset + 1, 4);
  const auto target = return_ip + std::uint64_t(signed_value(displacement, 4));
  const auto tail = ether_arm64::decode(code + return_offset, length - return_offset, return_ip);
  const auto& counter = tail.instructions[0];
  if (tail.count != 2 || tail.size != length - return_offset || tail.target != eip ||
      tail.instructions[1].kind != Instruction::Conditional || tail.instructions[1].opcode != opcode_ ||
      counter.kind != Instruction::Subtract || counter.lhs.memory ||
      !counter.rhs.immediate || counter.rhs.immediate_value != 1 ||
      (counter.width != 4 && counter.width != 8))
    return std::nullopt;

  // Verify a leaf routine that saves a register, multiplies a register by a
  // constant, restores the saved register and returns.
  CountedCallLoop plan{};
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto callee_bytes = std::min<unsigned>(plan.callee.size(), page_bytes - (target % page_bytes));
  const auto callee_address = counted_loop_ram_address(CS, target, callee_bytes, Access::Execute);
  if (!callee_address) return std::nullopt;
  memory.read_block(*callee_address, plan.callee.data(), callee_bytes);
  const auto& callee = plan.callee;
  unsigned pos = 0;
  const auto stack_register = [&](unsigned first_opcode) -> std::optional<unsigned> {
    const unsigned high = pos < callee_bytes && callee[pos] == 0x41 ? 8 : 0;
    if (high) ++pos;
    if (pos == callee_bytes || (callee[pos] & 0xf8U) != first_opcode) return std::nullopt;
    return high + (callee[pos++] & 7U);
  };
  const auto saved = stack_register(0x50);
  if (!saved || callee_bytes - pos < 4 || (callee[pos] & 0xf8U) != 0x48U) return std::nullopt;
  const auto rex = callee[pos++];
  const auto multiply = callee[pos++];
  const auto modrm = callee[pos++];
  if ((modrm & 0xc0U) != 0xc0U || (multiply != 0x69 && multiply != 0x6b)) return std::nullopt;
  const unsigned product = ((modrm >> 3) & 7U) + ((rex & 4U) ? 8 : 0);
  const unsigned source = (modrm & 7U) + ((rex & 1U) ? 8 : 0);
  const unsigned immediate_bytes = multiply == 0x69 ? 4 : 1;
  if (callee_bytes - pos < immediate_bytes) return std::nullopt;
  const auto literal = little_endian_word(callee.data() + pos, immediate_bytes);
  const auto multiplier = std::uint64_t(signed_value(literal, immediate_bytes));
  pos += immediate_bytes;
  const auto restored = stack_register(0x58);
  if (restored != saved || pos == callee_bytes || callee[pos++] != 0xc3 ||
      *saved == ESP || source == ESP || source == product || product == *saved)
    return std::nullopt;

  const unsigned acc = accumulator.lhs.reg, count = counter.lhs.reg;
  if (acc == count || acc == product || count == product ||
      acc == ESP || count == ESP || product == ESP)
    return std::nullopt;
  const unsigned changed = (1U << acc) | (1U << count) | (1U << product);
  const auto invariant = [changed](unsigned reg) {
    return reg >= Operand::kNoIndex || (changed & (1U << reg)) == 0;
  };
  for (unsigned i = 1; i < body.count; ++i) {
    const auto& op = body.instructions[i];
    const bool subtract = op.kind == Instruction::Subtract && op.width == 8;
    if (!op.lhs.memory || (!subtract && op.kind != Instruction::Move) || op.rhs.memory ||
        !invariant(op.lhs.reg) || !invariant(op.lhs.index) ||
        (!op.rhs.immediate && !invariant(op.rhs.reg)) || (subtract && op.rhs.immediate) ||
        plan.store_count == plan.stores.size())
      return std::nullopt;
    plan.stores[plan.store_count++] = op;
  }
  std::copy_n(code, length, plan.code.begin());
  plan.entry = eip;
  plan.target = target;
  plan.delta = accumulator.kind == Instruction::Add
      ? accumulator.rhs.immediate_value : 0U - accumulator.rhs.immediate_value;
  plan.multiplier = multiplier;
  plan.code_bytes = length;
  plan.callee_bytes = pos;
  plan.call_offset = call_offset;
  plan.accumulator = acc;
  plan.counter = count;
  plan.counter_width = counter.width;
  plan.saved = *saved;
  plan.source = source;
  plan.product = product;
  return plan;
}

inline std::uint64_t Cpu::try_counted_call_loop_batch(std::uint64_t step_budget) {
  using Instruction = ether_arm64::Instruction;
  using Operand = ether_arm64::Operand;
  if (step_budget < 18 || !can_cache_translations() || !(cr0 & 0x80000000U) || regs[ESP] < 16)
    return 0;

  const auto overlaps = [](std::uint64_t a, unsigned a_bytes, std::uint64_t b, unsigned b_bytes) {
    return a < b + b_bytes && b < a + a_bytes;
  };
  try {
    const auto length = unsigned(cursor_ - eip);
    const auto code_address = counted_loop_ram_address(CS, eip, length, Access::Execute);
    if (!code_address) return 0;
    std::array<std::uint8_t, ether_arm64::Block::kMaxBytes> code;
    memory.read_block(*code_address, code.data(), length);
    auto& cached = counted_call_loop_;
    std::optional<std::uint64_t> callee_address;
    if (cached && cached->entry == eip && cached->code_bytes == length &&
        std::memcmp(code.data(), cached->code.data(), length) == 0) {
      callee_address = counted_loop_ram_address(CS, cached->target, cached->callee_bytes, Access::Execute);
      if (!callee_address) return 0;
      std::array<std::uint8_t, 12> callee;
      memory.read_block(*callee_address, callee.data(), cached->callee_bytes);
      if (std::memcmp(callee.data(), cached->callee.data(), cached->callee_bytes) != 0)
        callee_address.reset();
    }
    if (!callee_address) {
      cached = decode_counted_call_loop(code.data(), length);
      if (!cached) return 0;
      callee_address = counted_loop_ram_address(CS, cached->target, cached->callee_bytes, Access::Execute);
      if (!callee_address) return 0;
    }
    const auto& plan = *cached;
    const auto return_ip = eip + plan.call_offset + 5;
    const unsigned steps_per_round = plan.store_count + 8; // ADD/SUB, CALL, leaf body, SUB and JNZ.
    const auto remaining = get_reg(plan.counter, plan.counter_width);
    const auto rounds = std::min(remaining, step_budget / steps_per_round);
    if (rounds < 2) return 0;
    const auto stack = counted_loop_ram_address(SS, regs[ESP] - 16, 16, Access::Write);
    const auto touches_code = [&](std::uint64_t physical, unsigned bytes) {
      return overlaps(physical, bytes, *code_address, length) ||
          overlaps(physical, bytes, *callee_address, plan.callee_bytes);
    };
    if (!stack || touches_code(*stack, 16)) return 0;

    struct Store { std::uint64_t linear, physical, value; unsigned width; bool subtract; };
    std::array<Store, 4> stores{};
    unsigned store_count = 0;
    for (unsigned i = 0; i < plan.store_count; ++i) {
      const auto& op = plan.stores[i];
      const bool subtract = op.kind == Instruction::Subtract;
      auto offset = op.lhs.immediate_value;
      if (op.lhs.reg != Operand::kRipBase) {
        offset = std::uint64_t(signed_value(op.lhs.displacement, 4));
        if (op.lhs.reg != Operand::kNoIndex) offset += regs[op.lhs.reg];
        if (op.lhs.index != Operand::kNoIndex) offset += regs[op.lhs.index] << op.lhs.scale_shift;
      }
      const auto value = op.rhs.immediate ? op.rhs.immediate_value : regs[op.rhs.reg];
      if (store_count && subtract && stores[store_count - 1].subtract &&
          stores[store_count - 1].linear == offset) {
        stores[store_count - 1].value += value;
        continue;
      }
      const auto physical = counted_loop_ram_address(DS, offset, op.width, Access::Write);
      if (!physical || touches_code(*physical, op.width) || overlaps(*physical, op.width, *stack, 16))
        return 0;
      bool combined = false;
      for (unsigned j = 0; j < store_count; ++j) {
        auto& store = stores[j];
        if (!overlaps(*physical, op.width, store.physical, store.width)) continue;
        if (!subtract || !store.subtract || *physical != store.physical) return 0;
        store.value += value;
        combined = true;
        break;
      }
      if (!combined) stores[store_count++] = {offset, *physical, value, op.width, subtract};
    }

    // Check the page-table dependencies found by the speculative walks.
    // Pin all destinations before committing the final guest state.
    if (memory.may_touch_page_tables(*stack, 16)) return 0;
    const auto pins = memory.pin_writes();
    memory.prepare_write(*stack, 16);
    for (unsigned i = 0; i < store_count; ++i) {
      auto& store = stores[i];
      if (memory.may_touch_page_tables(store.physical, store.width)) return 0;
      memory.prepare_write(store.physical, store.width);
      if (store.subtract) store.value = read_ram_span(store.physical, store.width) - rounds * store.value;
    }

    const auto input_at_call = [&](unsigned reg, std::uint64_t iteration) {
      if (reg == plan.accumulator) return regs[reg] + (iteration + 1) * plan.delta;
      if (reg == plan.counter && iteration) return remaining - iteration;
      return regs[reg];
    };
    const auto product_at = [&](std::uint64_t iteration) {
      return input_at_call(plan.source, iteration) * plan.multiplier;
    };
    const auto last = rounds - 1;
    const auto saved = input_at_call(plan.saved, last);
    std::array<std::uint8_t, 16> frame;
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
    std::memcpy(frame.data(), &saved, 8);
    std::memcpy(frame.data() + 8, &return_ip, 8);
#else
    for (unsigned i = 0; i < 8; ++i) {
      frame[i] = std::uint8_t(saved >> (8 * i));
      frame[8 + i] = std::uint8_t(return_ip >> (8 * i));
    }
#endif
    for (unsigned i = 0; i < store_count; ++i) {
      const auto& store = stores[i];
      write_ram_word(store.physical, store.width, store.value);
    }
    memory.write_block(*stack, frame.data(), frame.size());

    const auto call_ip = eip + plan.call_offset;
    if ((call_ip >> 63) && cpl() == 0) {
      const auto reg_at_call = [&](unsigned reg, std::uint64_t iteration) {
        return reg == plan.product && iteration ? product_at(iteration - 1) : input_at_call(reg, iteration);
      };
      const auto recorded = std::min<std::uint64_t>(rounds, recent_near_calls_.size());
      const auto skipped = rounds - recorded;
      near_calls_written_ = (near_calls_written_ + skipped) % recent_near_calls_.size();
      for (auto i = skipped; i < rounds; ++i)
        record_near_call({call_ip, plan.target, return_ip, regs[ESP] - 8,
                         {reg_at_call(ECX, i), reg_at_call(EDX, i), reg_at_call(R8, i),
                          reg_at_call(R9, i)}});
    }
    const auto result = product_at(last);
    regs[plan.accumulator] = input_at_call(plan.accumulator, last);
    regs[plan.product] = result;
    put_reg(plan.counter, plan.counter_width, alu(5, remaining - rounds + 1, 1, plan.counter_width));
    ip_written_ = remaining != rounds;
    if (!ip_written_) eip = cursor_;
    const auto steps = rounds * steps_per_round;
    instructions += steps;
    return steps;
  } catch (const std::exception&) {
    // Let the interpreter report the original fault at its instruction.
    return 0;
  }
}
