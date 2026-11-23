inline unsigned Cpu::try_xmm_store_batch(unsigned budget, XmmStoreBatch kind) {
  constexpr unsigned vector_bytes = 16, max_stores = 8, max_instruction_bytes = 9;
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const bool streaming = kind == XmmStoreBatch::Streaming;
  if (budget < 2 || !(streaming ? movntdq_store_batch_candidate() :
      kind == XmmStoreBatch::Aligned && aligned_xmm_store_batch_candidate()) || !can_cache_translations() ||
      !(cr0 & 0x80000000U) || fault || halted || transaction_ || interrupt_shadow_ ||
      (eflags & (TF | 0x20000U)) || (cpl() != 0 && cpl() != 3)) return 0;

  try {
    require_sse_state();
    const auto limit = std::min(budget, max_stores);
    const auto code_bytes = std::min(limit * max_instruction_bytes, page_bytes - unsigned(eip % page_bytes));
    if (code_bytes < (streaming ? 8U : 7U) || eip > UINT64_MAX - code_bytes) return 0;
    const auto code = counted_loop_ram_address(CS, eip, code_bytes, Access::Execute);
    if (!code) return 0;
    std::array<std::uint8_t, max_stores * max_instruction_bytes> signature;
    memory.read_block(*code, signature.data(), code_bytes);
    std::array<unsigned, max_stores> sources;
    unsigned pos = 0, last = 0, stores = 0, base = 0, memory_loop_end = 0;
    std::int64_t first_displacement = 0;
    while (stores < limit && code_bytes - pos >= (streaming ? 4U : 3U)) {
      if (streaming && signature[pos] != 0x66) break;
      auto next = pos + unsigned(streaming);
      const unsigned rex = (signature[next] & 0xf0) == 0x40 ? signature[next++] : 0;
      if (code_bytes - next < 3 || signature[next] != 0x0f ||
          signature[next + 1] != (streaming ? 0xe7 : 0x29)) break;
      const unsigned operand = signature[next + 2];
      next += 3;
      const unsigned mode = operand >> 6;
      const unsigned rm = operand & 7U;
      if (mode == 3 || rm == ESP || (!mode && rm == EBP)) break;
      const unsigned address_reg = rm | ((rex & 1U) << 3);
      std::int64_t displacement = 0;
      if (mode == 1) {
        if (next == code_bytes) break;
        displacement = signed_value(signature[next++], 1);
      } else if (mode == 2) {
        if (code_bytes - next < 4) break;
        displacement = signed_value(little_endian_word(signature.data() + next, 4), 4);
        next += 4;
      }
      if (!stores) {
        base = address_reg;
        first_displacement = displacement;
      }
      if (address_reg != base || displacement != first_displacement + stores * vector_bytes) break;
      sources[stores++] = ((operand >> 3) & 7U) | ((rex & 4U) << 1);
      if (!streaming && next - pos == 4 && !rex && operand == 0x41 && signature[pos + 3] == 0xf0)
        memory_loop_end = next;
      last = pos;
      pos = next;
    }

    const auto linear = regs[base] + std::uint64_t(first_displacement);
    const auto bytes = stores * vector_bytes;
    if (stores < 2 || linear % vector_bytes) return 0;
    const auto target = counted_loop_ram_address((base & 7U) == EBP ? SS : DS, linear, bytes, Access::Write);
    if (!target || memory.may_touch_page_tables(*target, bytes) ||
        (!streaming && !memory.resident_ram_span(*target, bytes, false)) ||
        (*target < *code + pos && *code < *target + bytes)) return 0;

    std::array<std::uint8_t, max_stores * vector_bytes> data;
    for (unsigned i = 0; i < stores; ++i) {
      const auto& source = xmm[sources[i]];
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
    if (streaming) last_movntdq_end_ip_ = cursor_;
    else if (memory_loop_end) last_xmm_memory_loop_end_ip_ = eip + memory_loop_end;
    eip = cursor_;
    ip_written_ = false;
    opcode_ = streaming ? 0x0fe7 : 0x0f29;
    repeat_ = 0;
    decoded_.assign(signature.begin() + last, signature.begin() + pos);
    eflags |= 2;
    instructions += stores;
    return stores;
  } catch (const std::exception&) {
    return 0;
  }
}
