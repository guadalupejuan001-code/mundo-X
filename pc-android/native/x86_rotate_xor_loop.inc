inline std::uint64_t Cpu::try_rotate_xor_loop_batch(std::uint64_t step_budget) {
  constexpr unsigned steps_per_round = 4;
  if (!can_cache_translations() || step_budget < 2 * steps_per_round) return 0;
  const auto bytes = unsigned(cursor_ - eip);
  std::array<std::uint8_t, 17> code;
  try {
    const auto address = counted_loop_ram_address(CS, eip, bytes, Access::Execute);
    if (!address) return 0;
    memory.read_block(*address, code.data(), bytes);
  } catch (const std::exception&) {
    return 0;
  }
  const auto body = bytes - 2;
  if (code[body] != 0x75 || code[body + 1] != decoded_[1]) return 0;

  // Match a rotation and XOR by constants on one 64-bit register, in either order.
  unsigned pos = 0, reg = 16, rotation = 0;
  std::uint64_t xor_mask = 0;
  bool has_xor = false, right = false, rotate_last = false;
  for (unsigned operation = 0; operation < 2; ++operation) {
    if (body - pos < 3 || (code[pos] & 0xfeU) != 0x48U) return 0;
    const auto rex = code[pos++], opcode = code[pos++], modrm = code[pos++];
    if ((modrm & 0xc0U) != 0xc0U) return 0;
    const auto target = (modrm & 7U) + ((rex & 1U) ? 8 : 0);
    if (operation && target != reg) return 0;
    reg = target;
    const auto group = (modrm >> 3) & 7U;
    if (!rotation && group < 2 && (opcode == 0xd1 || (opcode == 0xc1 && pos < body))) {
      const auto count = opcode == 0xd1 ? 1U : unsigned(code[pos++]) & 63U;
      if (!count) return 0;
      right = group != 0;
      rotation = right ? 64 - count : count;
      rotate_last = operation != 0;
    } else if (!has_xor && group == 6 && (opcode == 0x81 || opcode == 0x83)) {
      const unsigned literal_bytes = opcode == 0x81 ? 4 : 1;
      if (body - pos < literal_bytes) return 0;
      xor_mask = std::uint64_t(signed_value(little_endian_word(code.data() + pos, literal_bytes), literal_bytes));
      pos += literal_bytes;
      has_xor = true;
    } else {
      return 0;
    }
  }
  if (!rotation || !has_xor || body - pos < 3 || (code[pos] & 0xfeU) != 0x48U) return 0;
  const auto rex = code[pos], opcode = code[pos + 1], modrm = code[pos + 2];
  const auto counter = (modrm & 7U) + ((rex & 1U) ? 8 : 0);
  const bool dec = body - pos == 3 && opcode == 0xff && (modrm & 0xf8U) == 0xc8U;
  const bool sub = body - pos == 4 && opcode == 0x83 && (modrm & 0xf8U) == 0xe8U && code[pos + 3] == 1;
  if ((!dec && !sub) || reg == counter) return 0;

  const auto remaining = regs[counter], rounds = std::min(remaining, step_budget / steps_per_round);
  if (rounds < 2) return 0;
  const auto rotate = [](std::uint64_t value, unsigned bits) {
    return bits ? (value << bits) | (value >> (64 - bits)) : value;
  };
  if (rotate_last) xor_mask = rotate(xor_mask, rotation);
  auto value = regs[reg];
  // Squaring the affine map R(x) XOR mask composes both the rotation
  // and the parity of its mask. The intermediate states are unobservable.
  for (auto power = rounds; power; power >>= 1) {
    if (power & 1) value = rotate(value, rotation) ^ xor_mask;
    xor_mask ^= rotate(xor_mask, rotation);
    rotation = (2 * rotation) & 63U;
  }
  regs[reg] = value;
  regs[counter] = alu(5, remaining - rounds + 1, 1, 8);
  if (dec) set_flag(CF, rotate_last && ((value >> (right ? 63 : 0)) & 1U));
  ip_written_ = remaining != rounds;
  if (!ip_written_) eip = cursor_;
  const auto steps = rounds * steps_per_round;
  instructions += steps;
  return steps;
}
