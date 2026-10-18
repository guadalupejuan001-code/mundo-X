#include <cstdint>
#include <cstring>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>

#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif

#include "mundo_x/visual_scene.hpp"

namespace {
void uint32(std::uint32_t value) {
  const char bytes[] = {static_cast<char>(value >> 24), static_cast<char>(value >> 16),
                        static_cast<char>(value >> 8), static_cast<char>(value)};
  std::cout.write(bytes, sizeof(bytes));
}
void uint64(std::uint64_t value) {
  uint32(static_cast<std::uint32_t>(value >> 32));
  uint32(static_cast<std::uint32_t>(value));
}
void blob(const std::string& text) {
  uint32(static_cast<std::uint32_t>(text.size()));
  std::cout.write(text.data(), static_cast<std::streamsize>(text.size()));
}
}  // namespace

int main(int argc, char** argv) {
  try {
    if (argc != 3) throw std::invalid_argument("scene and operations required");
#ifdef _WIN32
    if (_setmode(_fileno(stdout), _O_BINARY) == -1)
      throw std::runtime_error("cannot select binary standard output");
#endif
    std::ifstream input(argv[1], std::ios::binary);
    if (!input) throw std::runtime_error("cannot open probe scene");
    const std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    auto scene = mundo_x::VisualScene::import_text(text);
    const auto initial = scene.export_text();
    std::ifstream commands(argv[2]);
    if (!commands) throw std::runtime_error("cannot open probe operations");
    std::ostringstream operations;
    std::string line;
    while (std::getline(commands, line)) {
      if (line.empty()) continue;
      std::istringstream command(line);
      std::string name, extra;
      command >> name;
      std::uint32_t id = 0, state = 0;
      if (name == "edit") {
        if (!(command >> id >> state) || (command >> extra))
          throw std::runtime_error("invalid probe edit");
      } else if (name != "clear" || (command >> extra)) {
        throw std::runtime_error("invalid probe operation");
      }
      const auto previous = scene.version();
      try {
        if (name == "edit") scene.edit(id, state);
        else scene.clear_edits();
        operations << (scene.version() == previous ? "unchanged\n" : "changed\n");
      } catch (const std::logic_error&) {
        operations << "rejected\n";
      }
    }
    std::cout << "MXBRIDGE1\n";
    blob(initial);
    blob(operations.str());
    blob(scene.export_text());
    uint32(scene.count());
    uint32(scene.side());
    uint64(scene.logical_rule_bytes());
    for (std::uint32_t id = 0; id < scene.count(); ++id) {
      for (const float value : scene.transform(id)) {
        std::uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        uint32(bits);
      }
    }
    std::cout.flush();
    if (!std::cout) throw std::runtime_error("cannot write probe stream");
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "scene bridge probe: " << error.what() << '\n';
    return 1;
  }
}
