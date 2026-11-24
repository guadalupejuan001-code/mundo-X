inline std::uint64_t Cpu::try_sse_triplet_batch(std::uint64_t step_budget) {
  // Store three independent vectors, advance RCX, then compare it to R8.
  static constexpr std::array<std::uint8_t, kStreamingSseTripletBytes> body{
      0x66,0x0f,0xe7,0x11, 0x66,0x0f,0xe7,0x49,0x10, 0x66,0x0f,0xe7,0x41,0x20,
      0x48,0x83,0xc1,0x30, 0x49,0x3b,0xc8, 0x75,0xe9};
  constexpr unsigned vector_bytes = 16, round_bytes = 3 * vector_bytes, steps_per_round = 6;
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto destination = regs[ECX], end = regs[R8];
  if (!can_cache_translations() || destination % vector_bytes || end <= destination ||
      (end - destination) % round_bytes) return 0;
  const auto rounds = std::min({(end - destination) / round_bytes, step_budget / steps_per_round,
      std::uint64_t((page_bytes - destination % page_bytes) / round_bytes)});
  if (rounds < 2) return 0;
  const auto bytes = unsigned(rounds) * round_bytes;
  try {
    require_sse_state();
    const auto code = counted_loop_ram_address(CS, eip, body.size(), Access::Execute);
    if (!code) return 0;
    std::array<std::uint8_t, body.size()> guest;
    memory.read_block(*code, guest.data(), guest.size());
    if (guest != body) return 0;
    const auto physical = counted_loop_ram_address(DS, destination, bytes, Access::Write);
    if (!physical || (*physical < *code + body.size() && *code < *physical + bytes) ||
        memory.may_touch_page_tables(*physical, bytes)) return 0;

    std::array<std::uint8_t, page_bytes> data;
    for (unsigned i = 0; i < 3; ++i)
      std::copy(xmm[2 - i].begin(), xmm[2 - i].end(), data.begin() + i * vector_bytes);
    for (unsigned copied = round_bytes; copied < bytes; copied *= 2)
      std::copy_n(data.begin(), std::min(copied, bytes - copied), data.begin() + copied);
    const auto pins = memory.pin_writes();
    memory.prepare_write(*physical, bytes);
    memory.write_block(*physical, data.data(), bytes);
    regs[ECX] = destination + bytes;
    alu(7, regs[ECX], end, 8); // The final CMP sets every arithmetic flag.
    ip_written_ = regs[ECX] != end;
    if (!ip_written_) eip = cursor_;
    const auto steps = rounds * steps_per_round;
    instructions += steps;
    streaming_sse_triplet_steps_ += steps;
    return steps;
  } catch (const std::exception&) {
    return 0; // Scalar execution exposes the original fault.
  }
}

inline std::uint64_t Cpu::try_sse_memory_loop_batch(std::uint64_t step_budget, unsigned length) {
  // Eight aligned stores repeat XMM0 without changing any vector register.
  static constexpr std::array<std::uint8_t, kSseFillLoopBytes> cached_fill{
      0x0f,0x29,0x01, 0x0f,0x29,0x41,0x10, 0x48,0x81,0xc1,0x80,0x00,0x00,0x00,
      0x0f,0x29,0x41,0xa0, 0x0f,0x29,0x41,0xb0, 0x49,0xff,0xc9,
      0x0f,0x29,0x41,0xc0, 0x0f,0x29,0x41,0xd0, 0x0f,0x29,0x41,0xe0,
      0x0f,0x29,0x41,0xf0, 0x75,0xd5};
  static constexpr std::array<std::uint8_t, kStreamingSseFillLoopBytes> streaming_fill{
      0x66,0x0f,0xe7,0x01, 0x66,0x0f,0xe7,0x41,0x10, 0x66,0x0f,0xe7,0x41,0x20, 0x66,0x0f,0xe7,0x41,0x30,
      0x48,0x03,0xc8, 0x66,0x0f,0xe7,0x41,0xc0, 0x66,0x0f,0xe7,0x41,0xd0, 0x48,0xff,0xca,
      0x66,0x0f,0xe7,0x41,0xe0, 0x66,0x0f,0xe7,0x41,0xf0, 0x75,0xd1};
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
  // The cached copy has the same length as the streaming fill. Use the
  // preceding store observation to select the full signature to check.
  const bool filling_stream = length == streaming_fill.size() && streaming_sse_fill_loop_batch_candidate();
  const bool fill = filling_stream || length == cached_fill.size();
  const bool copying_stream = length == streaming_copy.size();
  const unsigned steps_per_round = fill ? 11 : copying_stream ? 13 : 12;
  constexpr unsigned vector_bytes = 16;
  const unsigned round_bytes = (fill ? 8 : 4) * vector_bytes;
  constexpr unsigned page_bytes = MemoryFabric::kPageSize;
  const auto counter = filling_stream ? EDX : R9;
  const auto cursor = regs[ECX], remaining = regs[counter];
  if (!can_cache_translations() || !(cr0 & 0x80000000U) || (!fill && cursor < vector_bytes) || cursor % vector_bytes)
    return 0;
  // ADD RCX,RAX must have the same stride as the eight destination vectors.
  if (filling_stream && regs[EAX] != round_bytes) return 0;
  const auto destination = fill ? cursor : cursor - vector_bytes;
  const auto source = cursor + regs[EDX];
  auto rounds = std::min({remaining, step_budget / steps_per_round,
      (page_bytes - destination % page_bytes) / round_bytes});
  if (!fill) rounds = std::min(rounds, (page_bytes - source % page_bytes) / round_bytes);
  const auto bytes = unsigned(rounds) * round_bytes;
  if (rounds < 2 || cursor > UINT64_MAX - bytes) return 0;

  try {
    require_sse_state();
    const auto code = counted_loop_ram_address(CS, eip, length, Access::Execute);
    if (!code) return 0;
    std::array<std::uint8_t, streaming_copy.size()> signature;
    memory.read_block(*code, signature.data(), length);
    const auto* expected = filling_stream ? streaming_fill.data() : fill ? cached_fill.data()
                         : copying_stream ? streaming_copy.data() : cached_copy.data();
    if (std::memcmp(signature.data(), expected, length) != 0) return 0;

    const auto from = fill ? std::optional<std::uint64_t>{}
                           : counted_loop_ram_address(DS, source, bytes, Access::Read);
    if (!fill && !from) return 0;
    const auto to = counted_loop_ram_address(DS, destination, bytes, Access::Write);
    if (!to || (from && *to < *from + bytes && *from < *to + bytes) ||
        (*to < *code + length && *code < *to + bytes) || memory.may_touch_page_tables(*to, bytes)) return 0;

    std::array<std::uint8_t, page_bytes + vector_bytes> data;
    std::copy(xmm[0].begin(), xmm[0].end(), data.begin());
    if (fill) {
      for (unsigned copied = vector_bytes; copied < bytes; copied *= 2)
        std::copy_n(data.begin(), std::min(copied, bytes - copied), data.begin() + copied);
    } else {
      memory.read_block(*from, data.data() + vector_bytes, bytes);
    }
    const auto pins = memory.pin_writes();
    memory.prepare_write(*to, bytes);
    memory.write_block(*to, data.data(), bytes);
    if (!fill) {
      // The guest's next copy round or tail stores the final source vector.
      const auto* last = data.data() + vector_bytes + bytes - round_bytes;
      for (unsigned i = 0; i < 4; ++i) std::copy_n(last + i * vector_bytes, vector_bytes, xmm[i + 1].begin());
      xmm[0] = xmm[4];
    }
    regs[ECX] = cursor + bytes;
    regs[counter] = alu(5, remaining - rounds + 1, 1, 8);
    set_flag(CF, false); // The final ADD cannot carry, and the countdown's DEC preserves CF.
    ip_written_ = remaining != rounds;
    if (!ip_written_) eip = cursor_;
    const auto steps = rounds * steps_per_round;
    instructions += steps;
    return steps;
  } catch (const std::exception&) {
    return 0; // The interpreter reports the original fault at its instruction.
  }
}
