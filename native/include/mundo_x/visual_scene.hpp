#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace mundo_x {

// A finite visual scene: a generation rule plus sparse exceptions, compatible
// with Android's ObjectSceneModel and MUNDOX_OBJECTS_V1. Transform generation
// does not retain a population-sized array. This value has no renderer, physics
// or thread owner; callers synchronize simultaneous mutation/read access.
class VisualScene {
 public:
  static constexpr std::uint32_t max_count = 1048576;
  static constexpr std::size_t max_edits = 4096;
  static constexpr std::size_t max_export_chars = 65536;
  static constexpr std::uint64_t max_version = 9223372036854775807ULL;

  VisualScene(std::uint32_t count, std::int32_t seed)
      : count_(count), seed_(seed) {
    if (count < 1 || count > max_count)
      throw std::invalid_argument("Object count must be 1..1048576");
    // Integer ceil(sqrt(count)): independent of floating rounding mode.
    while (side_ * side_ < count_) ++side_;
  }

  std::uint32_t count() const noexcept { return count_; }
  std::int32_t seed() const noexcept { return seed_; }
  std::uint32_t side() const noexcept { return side_; }
  std::uint64_t version() const noexcept { return version_; }
  std::size_t edit_count() const noexcept { return edits_.size(); }

  // Logical format payload only: excludes std::map nodes, this C++ object,
  // temporary export text, mesh/driver memory and framebuffer storage.
  std::uint64_t logical_rule_bytes() const noexcept {
    return 32ULL + 8ULL * edits_.size();
  }

  std::uint32_t edit_state(std::uint32_t id) const {
    check_id(id);
    const auto found = edits_.find(id);
    return found == edits_.end() ? 0 : found->second;
  }

  // State 0 restores the rule; 1..6 raise an object; 7 hides it. Failed edits
  // leave both the sparse representation and version unchanged.
  bool edit(std::uint32_t id, std::uint32_t state) {
    check_id(id);
    if (state > 7) throw std::invalid_argument("Edit state must be 0..7");
    const auto found = edits_.find(id);
    const auto previous = found == edits_.end() ? 0 : found->second;
    if (previous == state) return false;
    if (state != 0 && found == edits_.end() && edits_.size() >= max_edits)
      throw std::logic_error("Sparse edit limit reached: 4096");
    check_version();
    if (state == 0) edits_.erase(found);
    else if (found == edits_.end()) edits_.emplace(id, state);
    else found->second = state;
    ++version_;
    return true;
  }

  void clear_edits() {
    if (edits_.empty()) return;
    check_version();
    edits_.clear();
    ++version_;
  }

  // x, z, height, tone. Separate float stores preserve Java's binary32
  // multiplication/addition boundaries, including builds that contract FMA.
  std::array<float, 4> transform(std::uint32_t id) const {
    static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559 &&
                      std::numeric_limits<float>::digits == 24,
                  "VisualScene requires IEEE 754 binary32 floats");
    const auto state = edit_state(id);
    const auto mixed = hash(id, seed_);
    const float center = rounded(static_cast<float>(side_ - 1) * 0.5f);
    const float column = rounded(static_cast<float>(id % side_) - center);
    const float row = rounded(static_cast<float>(id / side_) - center);
    const float base_height = rounded(0.7f + static_cast<float>(mixed & 255U) / 128.0f);
    const float raise = rounded(static_cast<float>(state) * 0.35f);
    return {{rounded(column * 2.2f), rounded(row * 2.2f),
             state == 7 ? 0.0f : rounded(base_height + raise),
             rounded(static_cast<float>((mixed >> 8U) & 255U) / 255.0f)}};
  }

  std::string export_text() const {
    // to_string on integers is independent of the global C++ stream locale.
    std::string text = "MUNDOX_OBJECTS_V1\ncount=" + std::to_string(count_) +
        "\nseed=" + std::to_string(seed_) + "\nversion=" + std::to_string(version_) +
        "\nedits=" + std::to_string(edits_.size()) + '\n';
    for (const auto& entry : edits_)
      text += std::to_string(entry.first) + ':' + std::to_string(entry.second) + '\n';
    return text;
  }

  // Strict bounded parsing into a new value: no existing scene is modified if
  // parsing, validation or allocation fails. Like Java, one final LF is optional,
  // decimal leading zeroes and -0 are accepted; '+', whitespace and CR are not.
  static VisualScene import_text(const std::string& text) {
    if (text.size() > max_export_chars)
      throw std::invalid_argument("Invalid scene text size");
    std::vector<std::string_view> lines;
    std::size_t begin = 0;
    while (true) {
      const auto end = text.find('\n', begin);
      if (end == std::string::npos) {
        lines.emplace_back(text.data() + begin, text.size() - begin);
        break;
      }
      lines.emplace_back(text.data() + begin, end - begin);
      begin = end + 1;
    }
    if (!lines.empty() && lines.back().empty()) lines.pop_back();
    if (lines.size() < 5 || lines[0] != "MUNDOX_OBJECTS_V1")
      throw std::invalid_argument("Unsupported scene format");
    const auto count = parse_int32(field(lines[1], "count="));
    const auto seed = parse_int32(field(lines[2], "seed="));
    const auto version = parse_decimal(field(lines[3], "version="), max_version,
                                       max_version + 1);
    const auto size = parse_int32(field(lines[4], "edits="));
    if (version < 0 || size < 0 || static_cast<std::size_t>(size) > max_edits ||
        lines.size() != 5 + static_cast<std::size_t>(size))
      throw std::invalid_argument("Invalid scene metadata");
    VisualScene result(static_cast<std::uint32_t>(count), seed);
    std::int32_t previous = -1;
    for (std::int32_t i = 0; i < size; ++i) {
      const auto line = lines[5 + static_cast<std::size_t>(i)];
      const auto colon = line.find(':');
      if (colon == std::string_view::npos || colon == 0 || colon + 1 == line.size() ||
          line.find(':', colon + 1) != std::string_view::npos)
        throw std::invalid_argument("Invalid scene edit");
      const auto id = parse_int32(line.substr(0, colon));
      const auto state = parse_int32(line.substr(colon + 1));
      result.check_id(static_cast<std::uint32_t>(id));
      if (id <= previous || state < 1 || state > 7)
        throw std::invalid_argument("Edits must be sorted, unique and in states 1..7");
      result.edits_.emplace(static_cast<std::uint32_t>(id),
                            static_cast<std::uint32_t>(state));
      previous = id;
    }
    result.version_ = static_cast<std::uint64_t>(version);
    return result;
  }

 private:
  static float rounded(float value) {
    volatile float binary32 = value;
    return binary32;
  }

  static std::uint32_t hash(std::uint32_t id, std::int32_t seed) noexcept {
    auto value = id ^ static_cast<std::uint32_t>(seed) ^ UINT32_C(0x9e3779b9);
    value ^= value >> 16U;
    value *= UINT32_C(0x7feb352d);
    value ^= value >> 15U;
    value *= UINT32_C(0x846ca68b);
    value ^= value >> 16U;
    return value;
  }

  void check_id(std::uint32_t id) const {
    if (id >= count_) throw std::invalid_argument("Object ID outside scene");
  }

  void check_version() const {
    if (version_ == max_version) throw std::logic_error("Scene version exhausted");
  }

  static std::string_view field(std::string_view line, std::string_view prefix) {
    if (line.substr(0, prefix.size()) != prefix)
      throw std::invalid_argument("Missing scene metadata field");
    return line.substr(prefix.size());
  }

  static std::int32_t parse_int32(std::string_view number) {
    return static_cast<std::int32_t>(parse_decimal(number, 2147483647ULL, 2147483648ULL));
  }

  // Accumulate a checked unsigned magnitude to handle INT64_MIN without signed
  // overflow, and require exactly the ASCII decimal grammar used by Java.
  static std::int64_t parse_decimal(std::string_view number,
                                    std::uint64_t positive_limit,
                                    std::uint64_t negative_limit) {
    const bool negative = !number.empty() && number.front() == '-';
    const std::size_t begin = negative ? 1 : 0;
    if (begin == number.size()) throw std::invalid_argument("Missing integer");
    const auto limit = negative ? negative_limit : positive_limit;
    std::uint64_t magnitude = 0;
    for (std::size_t i = begin; i < number.size(); ++i) {
      const char value = number[i];
      if (value < '0' || value > '9') throw std::invalid_argument("Invalid integer");
      const auto digit = static_cast<std::uint64_t>(value - '0');
      if (magnitude > (limit - digit) / 10)
        throw std::invalid_argument("Integer outside range");
      magnitude = magnitude * 10 + digit;
    }
    if (!negative) return static_cast<std::int64_t>(magnitude);
    if (magnitude == max_version + 1) return std::numeric_limits<std::int64_t>::min();
    return -static_cast<std::int64_t>(magnitude);
  }

  std::uint32_t count_;
  std::int32_t seed_;
  std::uint32_t side_ = 1;
  std::uint64_t version_ = 0;
  std::map<std::uint32_t, std::uint32_t> edits_;
};

}  // namespace mundo_x
