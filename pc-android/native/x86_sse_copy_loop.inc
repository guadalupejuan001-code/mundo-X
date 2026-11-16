inline std::uint64_t Cpu::try_sse_copy_loop_batch(std::uint64_t step_budget, unsigned length) {
  // Copies four vectors per iteration. XMM0 carries the last vector across
  // iterations. The streaming form uses MOVNTPS and an advisory prefetch.
  static constexpr std::array<std::uint8_t, kSseCopyLoopBytes> cached_copy{
      0x0f,0x10,0x0c,0x11, 0x0f,0x10,0x54,0x11,0x10,
      0x0f,0x10,0x5c,0x11,0x20, 0x0f,0x10,0x64,0x11,0x30,
      0x0f,0x29,0x41,0xf0, 0x48,0x83,0xc1,0x40, 0x49,0xff,0xc9,
      0x0f,0x29,0x49,0xc0, 0x0f,0x29,0x51,0xd0, 0x0f,0x29,0x59,0xe0,
      0x0f,0x28,0xc4, 0x75,0xd1};
  static constexpr std::array<std::uint8_t, kStreamingSseCopyLoopBytes> streaming_copy{
      0x0f,0x10,0x0c,0x11, 0x0f,0x10,0x54,0x11,0x10,
      0x0f,0x10,0x5c,0x11,0x20, 0x0f,0x10,0x64,0x11,0x30,
      0x0f,0x2b,0x41,0xf0, 0x48,0x83,0xc1,0x40, 0x0f,0x18,0x44,0x11,0x40, 0x49,0xff,0xc9,
      0x0f,0x2b,0x49,0xc0, 0x0f,0x2b,0x51,0xd0, 0x0f,0x2b,0x59,0xe0,
      0x0f,0x28,0xc4, 0x75,0xcc};
  const bool streaming = length == streaming_copy.size();
  const unsigned steps_per_round = streaming ? 13 : 12;
  constexpr unsigned vector_bytes = 16, round_bytes = 4 * vector_bytes;
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto cursor = regs[ECX], remaining = regs[R9];
  if (!can_cache_translations() || !(cr0 & 0x80000000U) || cursor < vector_bytes || cursor % vector_bytes)
    return 0;
  const auto source = cursor + regs[EDX], destination = cursor - vector_bytes;
  const auto rounds = std::min({remaining, step_budget / steps_per_round,
      (page_bytes - source % page_bytes) / round_bytes,
      (page_bytes - destination % page_bytes) / round_bytes});
  const auto bytes = unsigned(rounds) * round_bytes;
  if (rounds < 2 || cursor > UINT64_MAX - bytes) return 0;

  try {
    require_sse_state();
    const auto code = counted_loop_ram_address(CS, eip, length, Access::Execute);
    if (!code) return 0;
    std::array<std::uint8_t, streaming_copy.size()> signature;
    memory.read_block(*code, signature.data(), length);
    const auto* expected = streaming ? streaming_copy.data() : cached_copy.data();
    if (std::memcmp(signature.data(), expected, length) != 0) return 0;

    const auto from = counted_loop_ram_address(DS, source, bytes, Access::Read);
    const auto to = counted_loop_ram_address(DS, destination, bytes, Access::Write);
    if (!from || !to || (*to < *from + bytes && *from < *to + bytes) ||
        (*to < *code + length && *code < *to + bytes) || memory.may_touch_page_tables(*to, bytes)) return 0;

    // The final source vector remains in XMM0 and is stored by the guest's next iteration or tail.
    std::array<std::uint8_t, page_bytes + vector_bytes> data;
    std::copy(xmm[0].begin(), xmm[0].end(), data.begin());
    memory.read_block(*from, data.data() + vector_bytes, bytes);
    const auto pins = memory.pin_writes();
    memory.prepare_write(*to, bytes);
    memory.write_block(*to, data.data(), bytes);
    const auto* last = data.data() + vector_bytes + bytes - round_bytes;
    for (unsigned i = 0; i < 4; ++i) std::copy_n(last + i * vector_bytes, vector_bytes, xmm[i + 1].begin());
    xmm[0] = xmm[4];
    regs[ECX] = cursor + bytes;
    regs[R9] = alu(5, remaining - rounds + 1, 1, 8);
    set_flag(CF, false); // The final ADD RCX,64 cannot carry; DEC R9 preserves CF.
    ip_written_ = remaining != rounds;
    if (!ip_written_) eip = cursor_;
    const auto steps = rounds * steps_per_round;
    instructions += steps;
    return steps;
  } catch (const std::exception&) {
    return 0; // The interpreter reports the original fault at its instruction.
  }
}
