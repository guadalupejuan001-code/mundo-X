#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <ostream>
#include <string_view>
#include <vector>

namespace cosmic_pc {

// Bounded, jittered sampling of completed guest steps, optionally weighted
// by elapsed nanoseconds measured by the caller for the sampled scalar step.
class BootHotspots {
 public:
  explicit BootHotspots(bool weighted_by_duration = false, bool by_code_region = false)
      : entries_(by_code_region ? 128 : 32),
        weighted_by_duration_(weighted_by_duration), by_code_region_(by_code_region) {}

  bool wants_sample(std::uint64_t steps = 1) const noexcept {
    return steps >= until_sample_;
  }
  unsigned steps_before_sample() const noexcept {
    return static_cast<unsigned>(until_sample_ - 1);
  }

  // The caller records the decoded instruction for every crossed sample.
  // Aggregation is valid when a bulk execution finishes a single REP opcode.
  std::uint64_t advance(std::uint64_t steps) noexcept {
    observed_steps_ += steps;
    std::uint64_t hits = 0;
    while (steps >= until_sample_) {
      steps -= until_sample_;
      ++hits;
      until_sample_ = next_interval();
    }
    until_sample_ -= steps;
    samples_ += hits;
    return hits;
  }

  // Widen the key to a 64-byte region so a loop's branches and memory
  // operands contribute together. CS and CPU mode keep contexts separate.
  void observe_region(std::string_view mode, std::uint16_t cs, std::uint64_t ip,
                      std::uint64_t ns) noexcept {
    ++observed_steps_;
    ++samples_;
    record({parse_mode(mode), cs, ip & ~(kRegionBytes - 1), {}, 0}, 1, ns);
  }

  void record(std::string_view mode, std::uint16_t cs, std::uint64_t ip,
              const std::vector<std::uint8_t>& bytes, std::uint64_t hits,
              std::uint64_t nanoseconds = 0) noexcept {
    record(mode, cs, ip, bytes.data(), bytes.size(), hits, nanoseconds);
  }

  void record(std::string_view mode, std::uint16_t cs, std::uint64_t ip,
              const std::uint8_t* bytes, std::size_t size, std::uint64_t hits,
              std::uint64_t nanoseconds = 0) noexcept {
    if (!hits || !size || size > kMaxInstructionBytes) return;
    record({parse_mode(mode), cs, ip, to_bytes(bytes, size), size}, hits, nanoseconds);
  }

  void add_elapsed(std::chrono::steady_clock::duration elapsed) noexcept {
    elapsed_ += elapsed;
  }

  void reset() noexcept {
    random_ = kSeed;
    until_sample_ = next_interval();
    observed_steps_ = samples_ = attributed_samples_ = sampled_ns_ = 0;
    elapsed_ = {};
    std::fill(entries_.begin(), entries_.end(), Entry{});
  }

  void write_json(std::ostream& out) const {
    std::vector<const Entry*> ranked;
    ranked.reserve(entries_.size());
    for (const auto& entry : entries_)
      if (entry.count) ranked.push_back(&entry);
    std::stable_sort(ranked.begin(), ranked.end(), [](const Entry* a, const Entry* b) {
      return a->count > b->count;
    });
    const auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(elapsed_).count();
    out << "{\"observed_steps\":" << observed_steps_ << ",\"samples\":" << samples_
        << ",\"attributed_samples\":" << attributed_samples_;
    if (by_code_region_) {
      out << ",\"region_bytes\":" << kRegionBytes << ",\"tracked_regions\":" << entries_.size();
      // Every unlisted key is bounded by the larger of the first omitted
      // estimate and the minimum counter once the table becomes full.
      const auto omitted = ranked.size() > kReportLimit ? ranked[kReportLimit]->count :
          ranked.size() == entries_.size() ? ranked.back()->count : 0;
      out << ",\"unlisted_upper_bound_ns\":" << omitted;
    } else {
      out << ",\"min_interval_steps\":" << kMinInterval << ",\"max_interval_steps\":" << kMaxInterval;
    }
    if (weighted_by_duration_) out << ",\"sampled_step_ns\":" << sampled_ns_;
    else out << ",\"execution_us\":" << elapsed;
    out << ",\"hotspots\":[";
    for (std::size_t i = 0; i < std::min(kReportLimit, ranked.size()); ++i) {
      const auto& entry = *ranked[i];
      const auto& key = entry.key;
      if (i) out << ',';
      out << "{\"mode\":\"" << kModes[key.mode] << "\",\"cs\":" << key.cs
          << ",\"ip\":" << key.ip << ",\"cs_hex\":\"";
      hex_number(out, key.cs, 4);
      out << "\",\"ip_hex\":\"";
      hex_number(out, key.ip, 16);
      if (!by_code_region_) {
        const auto prefix_size = prefix_length(key);
        const auto opcode_size = opcode_length(key, prefix_size);
        out << "\",\"prefixes\":\"";
        hex_bytes(out, key.bytes.data(), prefix_size);
        out << "\",\"opcode\":\"";
        hex_bytes(out, key.bytes.data() + prefix_size, opcode_size);
        out << "\",\"bytes\":\"";
        hex_bytes(out, key.bytes.data(), key.size);
      }
      if (weighted_by_duration_) {
        out << "\",\"estimated_sample_ns\":" << entry.count << ",\"max_error_ns\":" << entry.error;
        if (by_code_region_) out << ",\"lower_bound_ns\":" << entry.count - entry.error;
        out << '}';
      } else {
        out << "\",\"estimated_samples\":" << entry.count << ",\"max_error_samples\":" << entry.error << '}';
      }
    }
    out << "]}";
  }

 private:
  static constexpr std::size_t kMaxInstructionBytes = 15;
  static constexpr std::size_t kReportLimit = 8;
  static constexpr std::uint64_t kRegionBytes = 64;
  static constexpr std::uint64_t kMinInterval = 2048;
  static constexpr std::uint64_t kMaxInterval = 8191;
  static constexpr std::uint32_t kSeed = 0x6f3d09a7U;
  static constexpr std::array<std::string_view, 8> kModes{
      "unknown", "real16", "real32", "protected16", "protected32", "compat16", "compat32", "long64"};
  using Bytes = std::array<std::uint8_t, kMaxInstructionBytes>;

  struct Key {
    std::size_t mode = 0;
    std::uint16_t cs = 0;
    std::uint64_t ip = 0;
    Bytes bytes{};
    std::size_t size = 0;

    bool operator==(const Key& other) const noexcept {
      return cs == other.cs && ip == other.ip && mode == other.mode &&
             size == other.size && bytes == other.bytes;
    }
  };

  struct Entry {
    Key key;
    std::uint64_t count = 0;
    std::uint64_t error = 0;
  };

  void record(const Key& key, std::uint64_t hits, std::uint64_t nanoseconds) noexcept {
    attributed_samples_ += hits;
    const auto weight = by_code_region_ ? nanoseconds :
        weighted_by_duration_ ? std::max<std::uint64_t>(1, nanoseconds) : hits;
    if (!weight) return;
    if (weighted_by_duration_) sampled_ns_ += weight;
    Entry* minimum = &entries_.front();
    for (auto& entry : entries_) {
      if (entry.count && entry.key == key) {
        entry.count += weight;
        return;
      }
      if (entry.count < minimum->count) minimum = &entry;
    }
    // Space-Saving heavy hitters. A replaced slot carries its previous count
    // as the maximum possible overestimate for its new instruction.
    *minimum = {key, minimum->count + weight, minimum->count};
  }

  static std::size_t parse_mode(std::string_view mode) noexcept {
    const auto found = std::find(kModes.begin(), kModes.end(), mode);
    return found == kModes.end() ? 0 : static_cast<std::size_t>(found - kModes.begin());
  }

  static Bytes to_bytes(const std::uint8_t* source, std::size_t size) noexcept {
    Bytes bytes{};
    std::copy_n(source, size, bytes.begin());
    return bytes;
  }

  static std::size_t prefix_length(const Key& key) noexcept {
    std::size_t length = 0;
    for (; length < key.size; ++length) {
      const auto byte = key.bytes[length];
      switch (byte) {
        case 0xf0: case 0xf2: case 0xf3:
        case 0x26: case 0x2e: case 0x36: case 0x3e: case 0x64: case 0x65:
        case 0x66: case 0x67:
          continue;
        default:
          if (kModes[key.mode] == "long64" && byte >= 0x40 && byte <= 0x4f) continue;
          return length;
      }
    }
    return length;
  }

  static std::size_t opcode_length(const Key& key, std::size_t prefix) noexcept {
    const auto remaining = key.size - prefix;
    if (!remaining) return 0;
    if (key.bytes[prefix] != 0x0f || remaining == 1) return 1;
    const auto map = key.bytes[prefix + 1];
    return (map == 0x38 || map == 0x3a) && remaining >= 3 ? 3 : 2;
  }

  static void hex_bytes(std::ostream& out, const std::uint8_t* bytes, std::size_t size) {
    constexpr char hex[] = "0123456789abcdef";
    for (std::size_t i = 0; i < size; ++i) {
      out.put(hex[bytes[i] >> 4]);
      out.put(hex[bytes[i] & 0xf]);
    }
  }

  static void hex_number(std::ostream& out, std::uint64_t value, unsigned digits) {
    constexpr char hex[] = "0123456789abcdef";
    while (digits) out.put(hex[(value >> (4 * --digits)) & 0xf]);
  }

  std::uint64_t next_interval() noexcept {
    random_ ^= random_ << 13;
    random_ ^= random_ >> 17;
    random_ ^= random_ << 5;
    return kMinInterval + random_ % (kMaxInterval - kMinInterval + 1);
  }

  std::vector<Entry> entries_;
  std::uint32_t random_ = kSeed;
  std::uint64_t until_sample_ = next_interval();
  std::uint64_t observed_steps_ = 0, samples_ = 0, attributed_samples_ = 0;
  std::uint64_t sampled_ns_ = 0;
  bool weighted_by_duration_, by_code_region_;
  std::chrono::steady_clock::duration elapsed_{};
};

}  // namespace cosmic_pc
