// Host CLI for native InfiniteCore C02, independent from the original XI CLI.
#include "mundo_x/autonomous_core_c02.hpp"

#include <cstddef>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {
using Pool = mundo_x::AutonomousCoreC02;

std::string quoted(const std::string& s) {
  constexpr char hexdigits[] = "0123456789abcdef";
  std::string out("\"");
  for (unsigned char ch : s) {
    switch (ch) {
      case '"': out += "\\\""; break;
      case '\\': out += "\\\\"; break;
      case '\n': out += "\\n"; break;
      case '\r': out += "\\r"; break;
      case '\t': out += "\\t"; break;
      default:
        if (ch < 32) {
          out += "\\u00";
          out += hexdigits[ch >> 4];
          out += hexdigits[ch & 15];
        } else out += char(ch);
    }
  }
  return out + '"';
}
std::string read(const std::string& filename) {
  std::ifstream source(filename, std::ios::binary);
  if (!source) throw std::runtime_error("Cannot open XI input: " + filename);
  std::string program;
  char block[8192];
  while (source) {
    source.read(block, sizeof(block));
    const auto count = static_cast<std::size_t>(source.gcount());
    if (count > mundo_x::infinite_program_detail::max_program_bytes - program.size())
      throw std::length_error("XI input exceeds 16MiB: " + filename);
    program.append(block, count);
  }
  if (source.bad()) throw std::runtime_error("Cannot read XI file: " + filename);
  return program;
}
}  // namespace

int main(int argc, char** argv) {
  try {
    Pool::Options options;
    auto mode = Pool::Mode::Isolated;
    auto background = mundo_x::InfiniteSession::Background::Zero;
    std::vector<std::string> paths;
    for (int i = 1; i < argc; ++i) {
      const std::string flag = argv[i];
      if ((flag == "--workers" || flag == "--mode" || flag == "--background") &&
          i + 1 >= argc) throw std::invalid_argument("option requires a value");
      if (flag == "--workers") {
        const auto raw = std::string(argv[++i]);
        const auto count = mundo_x::infinite_program_detail::decimal<std::size_t>(
            raw, "--workers");
        if (!count) throw std::invalid_argument("workers must be positive");
        options.workers = count;
      } else if (flag == "--mode") {
        const std::string text = argv[++i];
        if (text == "isolated") mode = Pool::Mode::Isolated;
        else if (text == "transactional") mode = Pool::Mode::Transactional;
        else throw std::invalid_argument("mode must be isolated or transactional");
      } else if (flag == "--background") {
        background = mundo_x::infinite_program_detail::parse_background(argv[++i]);
      } else if (flag == "--help") {
        std::cout << "Usage: mundo_x_autonomous [--workers N] [--mode isolated|transactional]"
                     " [--background zero|bell] job1.xi [job2.xi ...]\n"
                     "One private XI execution per submitted job; transactional mode uses"
                     " C02 epoch compare-and-commit. Original CLI is unaffected.\n";
        return 0;
      } else if (!flag.empty() && flag[0] == '-') {
        throw std::invalid_argument("unknown flag: " + flag);
      } else paths.push_back(flag);
    }
    if (paths.empty()) throw std::invalid_argument("at least one XI file is required");
    Pool pool(options, {}, {}, background);
    std::vector<std::future<Pool::Outcome>> futures;
    futures.reserve(paths.size());
    for (const auto& path : paths)
      futures.push_back(pool.submit(read(path), Pool::Priority::Normal, mode));
    bool all_ok = true;
    for (std::size_t i = 0; i < futures.size(); ++i) {
      const auto answer = futures[i].get();
      all_ok &= answer.ok;
      std::cout << "{\"job\":" << (i + 1)
                << ",\"path\":" << quoted(paths[i])
                << ",\"ok\":" << (answer.ok ? "true" : "false")
                << ",\"isolated\":" << (answer.isolated ? "true" : "false")
                << ",\"committed\":" << (answer.committed ? "true" : "false")
                << ",\"conflicts\":" << answer.conflicts
                << ",\"source_epoch\":" << answer.source_epoch
                << ",\"epoch\":" << answer.epoch
                << ",\"output\":" << quoted(answer.output)
                << ",\"error\":" << quoted(answer.error) << "}\n";
    }
    const auto tally = pool.telemetry();
    std::cout << "{\"summary\":true,\"accepted\":" << tally.accepted
              << ",\"completed\":" << tally.completed
              << ",\"failed\":" << tally.failed
              << ",\"committed\":" << tally.committed
              << ",\"isolated\":" << tally.isolated
              << ",\"conflicts\":" << tally.conflicts
              << ",\"revision\":" << pool.metadata().stats.revision << "}\n";
    return all_ok ? 0 : 1;
  } catch (const std::exception& e) {
    std::cerr << "InfiniteCore autonomous host: " << e.what() << '\n';
    return 1;
  }
}
