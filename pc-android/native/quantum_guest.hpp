#pragma once

#include "quantum_device.hpp"
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <string>
#include <vector>

namespace cosmic_pc {

// A real boot guest. Stage one reads stage two through INT 13h; stage two
// sends a program through OUT, reads device status with IN, and branches on
// the computed assertions. The x64 variant executes its own mode transition.
inline std::vector<std::uint8_t> quantum_boot_disk(bool long_mode = false,
                                                 bool valid_program = true,
                                                 bool apply_z = false) {
  constexpr std::uint32_t origin = 0x8000;
  std::vector<std::uint8_t> code;
  const auto emit = [&](std::initializer_list<std::uint8_t> bytes) {
    code.insert(code.end(), bytes.begin(), bytes.end());
  };
  const auto number = [&](std::uint64_t value, unsigned bytes) {
    for (unsigned i = 0; i < bytes; ++i) code.push_back(std::uint8_t(value >> (i * 8)));
  };
  const auto patch = [&](std::size_t offset, std::uint64_t value, unsigned bytes) {
    if (offset > code.size() || bytes > code.size() - offset)
      throw std::logic_error("quantum guest relocation overflow");
    for (unsigned i = 0; i < bytes; ++i) code[offset + i] = std::uint8_t(value >> (i * 8));
  };
  const auto rel8 = [&](std::size_t offset, std::size_t target) {
    const auto distance = std::int64_t(target) - std::int64_t(offset + 1);
    if (distance < -128 || distance > 127) throw std::logic_error("quantum guest branch range");
    patch(offset, std::uint8_t(distance), 1);
  };

  emit({0xfa, 0xfc, 0x31, 0xc0, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0});
  emit({0xbc, 0x00, 0x70});
  std::size_t gdtr_pointer = 0;
  if (long_mode) {
    emit({0x66, 0x0f, 0x01, 0x16}); gdtr_pointer = code.size(); number(0, 2);
    emit({0x0f, 0x20, 0xc0, 0x0c, 0x01, 0x0f, 0x22, 0xc0, 0x66, 0xea});
    const auto protected_pointer = code.size(); number(0, 4); number(8, 2);
    patch(protected_pointer, origin + code.size(), 4);
    emit({0x66, 0xb8, 0x10, 0x00, 0x8e, 0xd8, 0x8e, 0xc0, 0x8e, 0xd0, 0xbc});
    number(0x7000, 4);
    emit({0x31, 0xc0, 0xbf}); number(0x1000, 4);
    emit({0xb9}); number(3 * 4096 / 4, 4); emit({0xf3, 0xab});
    const auto store = [&](std::uint32_t address, std::uint32_t value) {
      emit({0xc7, 0x05}); number(address, 4); number(value, 4);
    };
    store(0x1000, 0x2003); store(0x2000, 0x3003); store(0x3000, 0x83);
    emit({0xb8}); number(0x20, 4); emit({0x0f, 0x22, 0xe0, 0xb8});
    number(0x1000, 4); emit({0x0f, 0x22, 0xd8, 0xb9}); number(0xc0000080U, 4);
    emit({0x0f, 0x32, 0x0d}); number(0x100, 4); emit({0x0f, 0x30});
    emit({0x0f, 0x20, 0xc0, 0x0d}); number(0x80010000U, 4);
    emit({0x0f, 0x22, 0xc0, 0xea});
    const auto long_pointer = code.size(); number(0, 4); number(0x18, 2);
    patch(long_pointer, origin + code.size(), 4);
  }

  // Select the byte protocol, clear previous transport data, send exact ASCII.
  if (long_mode) emit({0x66});
  emit({0xba}); number(PcQuantumDevice::control_port, 2);
  emit({0xb0, PcQuantumDevice::clear_channel, 0xee});
  if (long_mode) emit({0x66});
  emit({0xba}); number(PcQuantumDevice::data_port, 2);
  emit({0xbe}); const auto program_pointer = code.size(); number(0, long_mode ? 4 : 2);
  const auto send = code.size();
  emit({0xac, 0x84, 0xc0, 0x74}); const auto sent_branch = code.size(); number(0, 1);
  emit({0xee, 0xeb}); const auto send_back = code.size(); number(0, 1);
  const auto sent = code.size();
  if (long_mode) emit({0x66});
  emit({0xba}); number(PcQuantumDevice::control_port, 2);
  emit({0xb0, PcQuantumDevice::submit_request, 0xee, 0xec});
  // Copy status into BL, verify presence, and branch on the real error bit.
  emit({0x88, 0xc3, 0x24, PcQuantumDevice::present, 0x3c, PcQuantumDevice::present, 0x75});
  const auto absent_branch = code.size(); number(0, 1);
  emit({0x88, 0xd8, 0x24, PcQuantumDevice::request_error, 0x75});
  const auto error_branch = code.size(); number(0, 1);
  emit({0xbe}); const auto success_pointer = code.size(); number(0, long_mode ? 4 : 2);
  emit({0xeb}); const auto success_branch = code.size(); number(0, 1);
  const auto failed = code.size();
  emit({0xbe}); const auto failure_pointer = code.size(); number(0, long_mode ? 4 : 2);
  const auto print_setup = code.size();
  if (long_mode) {
    emit({0xbf}); number(0xb8000, 4);
  }
  const auto print_loop = code.size();
  emit({0xac, 0x84, 0xc0, 0x74}); const auto halt_branch = code.size(); number(0, 1);
  if (long_mode) emit({0xb4, 0x0a, 0x66, 0xab});
  else emit({0xb4, 0x0e, 0xbb, 0x07, 0x00, 0xcd, 0x10});
  emit({0xeb}); const auto print_back = code.size(); number(0, 1);
  const auto halt = code.size(); emit({0xfa, 0xf4});
  rel8(sent_branch, sent); rel8(send_back, send);
  rel8(absent_branch, failed); rel8(error_branch, failed);
  rel8(success_branch, print_setup); rel8(halt_branch, halt); rel8(print_back, print_loop);

  if (long_mode) {
    while (code.size() % 8) emit({0});
    const auto gdt = code.size();
    number(0, 8); number(0x00cf9a000000ffffULL, 8);
    number(0x00cf92000000ffffULL, 8); number(0x00af9a000000ffffULL, 8);
    const auto gdtr = code.size(); number(31, 2); number(origin + gdt, 4);
    patch(gdtr_pointer, origin + gdtr, 2);
  }
  const auto text = [&](std::size_t pointer, const std::string& value) {
    patch(pointer, origin + code.size(), long_mode ? 4 : 2);
    code.insert(code.end(), value.begin(), value.end()); code.push_back(0);
  };
  const std::string assertions = std::string("h 0\ncx 0 1000000000\n") +
      (apply_z ? "z 0\nassert expect -1 X:0 X:1000000000\nassert expect 1 Y:0 Y:1000000000\n"
               : "assert expect 1 X:0 X:1000000000\nassert expect -1 Y:0 Y:1000000000\n") +
      "assert probz 0 0.5\n";
  text(program_pointer, assertions + (valid_program ? "assert expect 1" : "assert expect -1") +
       " Z:0 Z:1000000000\nstats\n");
  text(success_pointer, apply_z ? "InfiniteCore guest Z: OK" : (long_mode ? "InfiniteCore x64: OK" : "InfiniteCore x86: OK"));
  text(failure_pointer, long_mode ? "InfiniteCore x64: ERROR" : "InfiniteCore x86: ERROR");

  const std::size_t sectors = (code.size() + 511) / 512;
  if (sectors == 0 || sectors > 16) throw std::logic_error("quantum guest stage two too large");
  std::vector<std::uint8_t> disk((sectors + 1) * 512, 0);
  // DS/ES/SS=0; read stage two into 0000:8000, then jump there.
  const std::uint8_t loader[] = {
      0xfa,0x31,0xc0,0x8e,0xd8,0x8e,0xc0,0x8e,0xd0,0xbc,0x00,0x7c,
      0xb8,std::uint8_t(sectors),0x02,0xbb,0x00,0x80,0xb9,0x02,0x00,
      0xba,0x80,0x00,0xcd,0x13,0x72,0x05,0xea,0x00,0x80,0x00,0x00,
      0xb8,'F',0x0e,0xcd,0x10,0xfa,0xf4};
  std::copy(std::begin(loader), std::end(loader), disk.begin());
  disk[510] = 0x55; disk[511] = 0xaa;
  std::copy(code.begin(), code.end(), disk.begin() + 512);
  return disk;
}

} // namespace cosmic_pc
