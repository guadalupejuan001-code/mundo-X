inline std::uint64_t Cpu::try_wide_multiply_loop_batch(std::uint64_t step_budget) {
  // Multiply by R10 and propagate the carry through successive 64-bit words.
  static constexpr std::array<std::uint8_t, kWideMultiplyLoopBytes> multiply_add{
      0x4b,0x8b,0x04,0x08, 0x49,0xf7,0xe2, 0x49,0x03,0xc3, 0x48,0x83,0xd2,0,
      0x4a,0x01,0x04,0x09, 0x48,0x83,0xd2,0, 0x49,0x83,0xc1,8, 0x4c,0x8b,0xda, 0x75,0xe1};
  static constexpr std::array<std::uint8_t, kWideMultiplyLoopBytes> multiply_subtract{
      0x4b,0x8b,0x04,0xc8, 0x4c,0x8b,0xda, 0x49,0xf7,0xe2, 0x4a,0x29,0x04,0xc9,
      0x48,0x83,0xd2,0, 0x4e,0x29,0x1c,0xc9, 0x48,0x83,0xd2,0, 0x49,0xff,0xc1, 0x75,0xe1};
  constexpr unsigned word_bytes = 8, steps_per_round = 9, page_bytes = MemoryFabric::kPageSize;
  if (step_budget < 2 * steps_per_round || !can_cache_translations() || !(cr0 & 0x80000000U))
    return 0;

  try {
    const auto code = counted_loop_ram_address(CS, eip, kWideMultiplyLoopBytes, Access::Execute);
    if (!code) return 0;
    std::array<std::uint8_t, kWideMultiplyLoopBytes> current;
    memory.read_block(*code, current.data(), current.size());
    const bool subtract = current == multiply_subtract;
    if (!subtract && current != multiply_add) return 0;

    const unsigned increment = subtract ? 1 : word_bytes;
    const auto index = regs[R9];
    const auto remaining = std::uint64_t{0} - index;
    if (remaining % increment) return 0;
    const auto offset = subtract ? index * word_bytes : index;
    const auto source = regs[R8] + offset, target = regs[ECX] + offset;
    const auto rounds = std::min({remaining / increment, step_budget / steps_per_round,
        (page_bytes - source % page_bytes) / word_bytes,
        (page_bytes - target % page_bytes) / word_bytes});
    if (rounds < 2) return 0;
    const auto bytes = unsigned(rounds) * word_bytes;
    const auto from = counted_loop_ram_address(DS, source, bytes, Access::Read);
    const auto to = counted_loop_ram_address(DS, target, bytes, Access::Write);
    if (!from || !to || (*to > *from && *to - *from < bytes) ||
        (*to < *code + kWideMultiplyLoopBytes && *code < *to + bytes) ||
        memory.may_touch_page_tables(*to, bytes)) return 0;

    // The fitted spans are at most one page each. A forward store must not
    // overlap a later source word when the complete source is read in advance.
    std::array<std::uint8_t, page_bytes> input, output;
    memory.read_block(*from, input.data(), bytes);
    memory.read_block(*to, output.data(), bytes);
    const auto store_word = [](std::uint8_t* destination, std::uint64_t value) {
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
      std::memcpy(destination, &value, word_bytes);
#else
      for (unsigned byte = 0; byte < word_bytes; ++byte) destination[byte] = std::uint8_t(value >> (8 * byte));
#endif
    };
    const auto multiplier = regs[R10];
    auto carry = subtract ? regs[EDX] : regs[R11];
    std::uint64_t low = 0, previous_carry = 0;
    bool carry_flag = false;
    if (subtract) {
      for (unsigned pos = 0; pos < bytes; pos += word_bytes) {
        const auto product = wide_integer::multiply(little_endian_word(input.data() + pos, word_bytes), multiplier);
        const auto original = little_endian_word(output.data() + pos, word_bytes);
        const auto difference = original - product.low;
        const auto high = product.high + (original < product.low);
        previous_carry = carry;
        carry = high + (difference < previous_carry);
        carry_flag = carry < high;
        low = product.low;
        store_word(output.data() + pos, difference - previous_carry);
      }
    } else {
      for (unsigned pos = 0; pos < bytes; pos += word_bytes) {
        const auto product = wide_integer::multiply(little_endian_word(input.data() + pos, word_bytes), multiplier);
        const auto original = little_endian_word(output.data() + pos, word_bytes);
        low = product.low + carry;
        const auto sum = original + low;
        carry = product.high + (low < product.low) + (sum < low);
        store_word(output.data() + pos, sum);
      }
    }

    const auto pins = memory.pin_writes();
    memory.prepare_write(*to, bytes);
    memory.write_block(*to, output.data(), bytes);
    regs[EAX] = low;
    regs[EDX] = carry;
    regs[R11] = subtract ? previous_carry : carry;
    regs[R9] = alu(0, index + (rounds - 1) * increment, increment, 8);
    if (subtract) set_flag(CF, carry_flag); // INC preserves the last ADC carry.
    ip_written_ = regs[R9] != 0;
    if (!ip_written_) eip = cursor_;
    const auto steps = rounds * steps_per_round;
    instructions += steps;
    return steps;
  } catch (const std::exception&) {
    return 0; // Let the interpreter report any fault at the original instruction.
  }
}
