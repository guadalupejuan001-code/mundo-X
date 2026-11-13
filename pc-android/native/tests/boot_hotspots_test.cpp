#include "../boot_hotspots.hpp"
#include "../pc_machine.hpp"

#include <chrono>
#include <cstdint>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using cosmic_pc::BootHotspots;

void expect(bool condition, const char* message) {
  if (!condition) throw std::runtime_error(message);
}

std::string json(const BootHotspots& profile) {
  std::ostringstream out;
  profile.write_json(out);
  return out.str();
}

std::string without_elapsed(std::string report) {
  const auto start = report.find(",\"execution_us\":");
  if (start != std::string::npos) report.erase(start, report.find(',', start + 1) - start);
  return report;
}

void sampling_and_bulk() {
  BootHotspots single_steps, bulk;
  const std::vector<std::uint8_t> rep_move{0x66, 0xf3, 0xa5};
  constexpr std::uint64_t steps = 200'000;
  std::uint64_t last_boundary = 0, hits = 0, previous_gap = 0;
  bool saw_jitter = false;
  for (std::uint64_t i = 1; i <= steps; ++i) {
    const bool due = single_steps.wants_sample();
    const auto crossed = single_steps.advance(1);
    expect(crossed == due, "wants_sample must predict the next sample exactly");
    if (!due) continue;
    const auto gap = i - last_boundary;
    expect(gap >= 2048 && gap <= 8191, "sampling gap outside advertised range");
    saw_jitter |= previous_gap && previous_gap != gap;
    previous_gap = gap;
    last_boundary = i;
    ++hits;
    single_steps.record("protected16", 0x50, 0x45bf, rep_move, crossed);
  }
  expect(saw_jitter && hits > 20, "sampler should jitter to avoid a fixed-period alias");
  expect(bulk.wants_sample(steps), "bulk step must detect crossed threshold");
  const auto crossed = bulk.advance(steps);
  expect(crossed == hits, "bulk path should cross the same sample boundaries");
  bulk.record("protected16", 0x50, 0x45bf, rep_move, crossed);
  expect(json(single_steps) == json(bulk), "REP batch and step counts must produce identical profiles");
  bulk.add_elapsed(std::chrono::microseconds(34567));
  const auto report = json(bulk);
  expect(report.find("\"execution_us\":34567") != std::string::npos, "elapsed time not reported");
  expect(report.find("\"mode\":\"protected16\",\"cs\":80,\"ip\":17855,\"cs_hex\":\"0050\","
                     "\"ip_hex\":\"00000000000045bf\",\"prefixes\":\"66f3\",\"opcode\":\"a5\"") !=
             std::string::npos, "REP hotspot must report exact guest instruction provenance");
  bulk.reset();
  expect(json(bulk) == json(BootHotspots{}), "reset must clear the entire profile");
}

void bounded_hotspots() {
  BootHotspots profile;
  constexpr std::uint8_t nop = 0x90;
  const std::vector<std::uint8_t> vector_xor{0x66, 0x45, 0x0f, 0xef, 0xc9};
  auto advance_to_sample = [&] {
    std::uint64_t hits = 0;
    while (!hits) hits = profile.advance(1);
    return hits;
  };
  for (unsigned ip = 0; ip < 64; ++ip)
    profile.record("protected32", 0x20, ip, &nop, 1, advance_to_sample());
  for (unsigned n = 0; n < 70; ++n)
    profile.record("long64", 0x28, 0xffff800000000000ULL, vector_xor, advance_to_sample());
  profile.record("real16", 0, 0, nullptr, 0, advance_to_sample());
  const auto report = json(profile);
  expect(report.find("\"samples\":135,\"attributed_samples\":134") != std::string::npos,
         "samples without decoded bytes must remain unattributed");
  const auto leading = report.find("\"hotspots\":[{\"mode\":\"long64\"");
  expect(leading != std::string::npos, "most frequently observed instruction must rank first");
  expect(report.find("\"ip_hex\":\"ffff800000000000\",\"prefixes\":\"6645\","
                     "\"opcode\":\"0fef\",\"bytes\":\"66450fefc9\",\"estimated_samples\":72,"
                     "\"max_error_samples\":2", leading) != std::string::npos,
         "prefixes, multi-byte opcode and replacement error must remain distinguishable");
  std::size_t entries = 0;
  for (std::size_t i = 0; (i = report.find("\"mode\":", i)) != std::string::npos; ++i) ++entries;
  expect(entries == 8, "hotspot report must stay bounded to 8 entries");
}

void guest_boot() {
  cosmic_pc::PcMachine pc;
  std::array<std::uint8_t, 512> disk{};
  constexpr std::array<std::uint8_t, 17> program{
      0xfa, 0xfc,              // CLI, CLD.
      0xb8, 0x00, 0x20,        // MOV AX, 2000h.
      0x8e, 0xc0,              // MOV ES, AX.
      0x31, 0xff,              // XOR DI, DI.
      0xb9, 0xff, 0xff,        // MOV CX, ffffh.
      0xb0, 0xa5,              // MOV AL, a5h.
      0xf3, 0xaa,              // REP STOSB.
      0xf4,                    // HLT.
  };
  std::copy(program.begin(), program.end(), disk.begin());
  disk[510] = 0x55;
  disk[511] = 0xaa;
  pc.mount([&](std::uint64_t offset, std::uint8_t* dest, std::size_t len) {
    if (offset > disk.size() || len > disk.size() - offset) return false;
    std::copy_n(disk.data() + offset, len, dest);
    return true;
  }, disk.size(), "raw");
  expect(!pc.boot_hotspots(), "boot profile must be opt-in on core PcMachine");
  pc.enable_boot_hotspots();
  auto execute_guest = [&] {
    for (unsigned slice = 0; slice < 50 && !pc.cpu.halted && !pc.cpu.fault; ++slice) pc.run(4096);
    expect(pc.cpu.halted && !pc.cpu.fault, "boot guest failed to execute the repeated instruction");
    expect(pc.cpu.instructions == 65543, "guest instruction count differs from expected logical steps");
    expect(pc.cpu.memory.read_byte(0x20000) == 0xa5 &&
               pc.cpu.memory.read_byte(0x2fffe) == 0xa5 &&
               pc.cpu.memory.read_byte(0x2ffff) == 0,
           "guest did not fill the exact REP range");
    return json(*pc.boot_hotspots());
  };
  const auto report = execute_guest();
  expect(report.find("\"observed_steps\":65543") != std::string::npos,
         "PcMachine profile omitted completed logical steps");
  expect(report.find("\"mode\":\"real16\",\"cs\":0,\"ip\":31758,\"cs_hex\":\"0000\","
                     "\"ip_hex\":\"0000000000007c0e\",\"prefixes\":\"f3\",\"opcode\":\"aa\"") !=
             std::string::npos, "PcMachine profile mapped guest instructions to the wrong address");
  pc.reset();
  expect(json(*pc.boot_hotspots()) == json(BootHotspots{}), "guest reset must clear previous boot samples");
  pc.enable_boot_acceleration();
  expect(without_elapsed(execute_guest()) == without_elapsed(report),
         "accelerated guest should report the same logical hotspots as the interpreter");
  expect(pc.boot_acceleration().rep_elements > 60'000, "test did not exercise the REP batch path");
  pc.enable_boot_hotspots(false);
  expect(!pc.boot_hotspots(), "disabling profiling should restore the uninstrumented configuration");
}
}  // namespace

int main() {
  try {
    sampling_and_bulk();
    bounded_hotspots();
    guest_boot();
    std::cout << "BOOT_HOTSPOTS_PASS jittered=1 bulk_equivalent=1 bounded_top=8 guest_boot=1\n";
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "BOOT_HOTSPOTS_FAIL " << error.what() << '\n';
    return 1;
  }
}
