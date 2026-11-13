inline unsigned Cpu::try_shift_loop_batch(unsigned step_budget) {
  if (step_budget < 3 || !shift_loop_batch_candidate() || fault || halted || transaction_ ||
      interrupt_shadow_ || (eflags & (TF | 0x20000U)) || (cr0 & 0x80000000U) ||
      (cpl() != 0 && cpl() != 3) || step_trace_hook || irq_trace_hook)
    return 0;

  const auto remaining = reg16(ECX);
  if (!remaining) return 0;
  const auto iterations = std::min<unsigned>(remaining, step_budget / 3);
  std::array<std::uint8_t, 6> code;
  try {
    // Check the current bytes even after a matching scalar LOOP. The guest
    // can write to these instructions through another segment or A20 alias.
    const auto linear_code = segment_linear(CS, eip, code.size(), Access::Execute);
    if (code.size() - 1 > UINT32_MAX - linear_code || !bulk_ram_range(linear_code, code.size()))
      return 0;
    memory.read_block(bus_address(linear_code), code.data(), code.size());
  } catch (const std::exception&) {
    return 0;
  }
  if (code[0] != 0xd1 || code[2] != 0xd1 || code[4] != 0xe2 || code[5] != 0xfa ||
      code[1] < 0xc0 || code[3] < 0xc0)
    return 0;

  const auto shift = (code[1] >> 3) & 7U;
  const auto rotate = (code[3] >> 3) & 7U;
  const bool left = shift == 4 && rotate == 2;
  if (!left && (shift != 5 || rotate != 3)) return 0;
  const auto shifted = code[1] & 7U;
  const auto rotated = code[3] & 7U;
  if (shifted == rotated || shifted == ECX || rotated == ECX) return 0;

  const auto result = mundo_x::ether_next::shift_pair_16(reg16(shifted), reg16(rotated), left, iterations);
  set_reg16(shifted, result.shifted);
  set_reg16(rotated, result.rotated);
  szp(result.shifted, 2);
  set_flag(CF, result.carry);
  set_flag(OF, result.overflow);
  set_reg16(ECX, std::uint16_t(remaining - iterations));

  // The final instruction is the same LOOP as the scalar seed. Its
  // previously decoded bytes, decode cursor and start CS:IP are still valid.
  ip_written_ = remaining != iterations;
  if (!ip_written_) eip = cursor_ & code_mask();
  const unsigned steps = 3 * iterations;
  instructions += steps;
  return steps;
}
