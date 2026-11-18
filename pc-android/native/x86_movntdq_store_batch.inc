inline unsigned Cpu::try_movntdq_store_batch(unsigned budget) {
  constexpr unsigned vector_bytes = 16, max_stores = 4, max_code_bytes = max_stores * 5;
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  if (budget < 2 || !movntdq_store_batch_candidate() || !can_cache_translations() ||
      !(cr0 & 0x80000000U) || fault || halted || transaction_ || interrupt_shadow_ ||
      (eflags & (TF | 0x20000U)) || (cpl() != 0 && cpl() != 3)) return 0;

  try {
    require_sse_state();
    const auto code_bytes = std::min(max_code_bytes, page_bytes - unsigned(eip % page_bytes));
    if (code_bytes < 8 || eip > UINT64_MAX - code_bytes) return 0;
    const auto code = counted_loop_ram_address(CS, eip, code_bytes, Access::Execute);
    if (!code) return 0;
    std::array<std::uint8_t, max_code_bytes> signature;
    memory.read_block(*code, signature.data(), code_bytes);
    const auto matches_prefix = [&](unsigned pos) {
      return code_bytes - pos >= 4 && signature[pos] == 0x66 &&
          signature[pos + 1] == 0x0f && signature[pos + 2] == 0xe7;
    };
    if (!matches_prefix(0)) return 0;
    const unsigned reg_and_base = signature[3] & 0x3fU;
    const unsigned base = reg_and_base & 7U;
    if (base == ESP) return 0; // SIB addressing remains scalar.
    const auto segment = base == EBP ? SS : DS;
    unsigned pos = 0, last = 0, stores = 0;
    std::int64_t first_displacement = 0;
    while (stores < std::min(budget, max_stores) && matches_prefix(pos)) {
      const unsigned operand = signature[pos + 3];
      const unsigned mode = operand >> 6;
      if ((operand & 0x3fU) != reg_and_base || mode > 1 ||
          (!mode && base == EBP)) break; // RIP-relative addressing remains scalar.
      const auto instruction_bytes = 4 + mode;
      if (instruction_bytes > code_bytes - pos) break;
      const auto displacement = mode ? signed_value(signature[pos + 4], 1) : 0;
      if (!stores) first_displacement = displacement;
      if (displacement != first_displacement + stores * vector_bytes) break;
      last = pos;
      pos += instruction_bytes;
      ++stores;
    }

    const auto linear = regs[base] + std::uint64_t(first_displacement);
    const auto bytes = stores * vector_bytes;
    if (stores < 2 || linear % vector_bytes) return 0;
    const auto target = counted_loop_ram_address(segment, linear, bytes, Access::Write);
    if (!target || memory.may_touch_page_tables(*target, bytes) ||
        (*target < *code + pos && *code < *target + bytes)) return 0;

    std::array<std::uint8_t, max_stores * vector_bytes> data;
    const auto& source = xmm[reg_and_base >> 3];
    for (unsigned i = 0; i < stores; ++i) {
      std::copy(source.begin(), source.end(), data.begin() + i * vector_bytes);
    }
    decoded_.reserve(pos - last);
    // The ordinary one-page write preflights backing before changing bytes.
    // Any fault or missing A/D bit falls back to the original instruction.
    const auto pins = memory.pin_writes();
    memory.write_block(*target, data.data(), bytes);
    start_ip_ = eip + last;
    start_cs_ = segments[CS];
    cursor_ = eip + pos;
    eip = cursor_;
    last_movntdq_end_ip_ = cursor_;
    ip_written_ = false;
    opcode_ = 0x0fe7;
    repeat_ = 0;
    decoded_.assign(signature.begin() + last, signature.begin() + pos);
    eflags |= 2;
    instructions += stores;
    return stores;
  } catch (const std::exception&) {
    return 0;
  }
}
