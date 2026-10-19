#pragma once

#include "mundo_x/infinite_session.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace cosmic_pc {

// A bounded byte-stream device over the SAME session used by the PC frontend.
// A guest owns its request/response channel; host queries never drain this FIFO.
class PcQuantumDevice {
 public:
  using Session = mundo_x::InfiniteSession;
  static constexpr std::uint16_t data_port = 0x5850;
  static constexpr std::uint16_t control_port = 0x5851;
  static constexpr std::uint8_t clear_channel = 1;
  static constexpr std::uint8_t submit_request = 2;
  static constexpr std::uint8_t present = 0x80;
  static constexpr std::uint8_t response_ready = 1;
  static constexpr std::uint8_t request_error = 2;
  static constexpr std::uint8_t request_overflow = 4;
  static constexpr std::size_t max_request_bytes = 65536;

  explicit PcQuantumDevice(std::shared_ptr<Session> session)
      : session_(std::move(session)) {
    if (!session_) throw std::invalid_argument("Quantum device requires a session");
  }

  void clear() noexcept {
    used_ = offset_ = fallback_size_ = 0;
    response_.clear();
    overflow_ = error_ = false;
  }

  void power_reset() noexcept {
    clear();
    requests_ = errors_ = 0;
  }

  std::uint8_t status() const noexcept {
    return std::uint8_t(present | (remaining() ? response_ready : 0) |
                        (error_ ? request_error : 0) |
                        (overflow_ ? request_overflow : 0));
  }
  std::size_t request_bytes() const noexcept { return used_; }
  std::size_t remaining() const noexcept {
    const auto size = fallback_size_ ? fallback_size_ : response_.size();
    return offset_ < size ? size - offset_ : 0;
  }
  std::uint64_t requests() const noexcept { return requests_; }
  std::uint64_t errors() const noexcept { return errors_; }

  std::optional<std::uint32_t> input(std::uint16_t port, unsigned width) noexcept {
    if (port != data_port && port != control_port) return std::nullopt;
    // Unsupported widths are a device error, rather than a guest CPU fault.
    if (width != 1) { fail("Quantum ports require byte IN/OUT"); return 0; }
    if (port == control_port) return status();
    if (!remaining()) return 0;
    const unsigned char byte = fallback_size_ ? fallback_[offset_] : response_[offset_];
    ++offset_;
    return byte;
  }

  bool output(std::uint16_t port, unsigned width, std::uint32_t value) noexcept {
    if (port != data_port && port != control_port) return false;
    if (width != 1) { fail("Quantum ports require byte IN/OUT"); return true; }
    if (port == data_port) {
      if (used_ == request_.size()) {
        overflow_ = true;
        fail("Quantum request exceeds 65536 bytes; clear the channel");
      } else if (!overflow_) request_[used_++] = static_cast<char>(value & 255);
      return true;
    }
    if (value == clear_channel) clear();
    else if (value == submit_request) submit();
    else fail("Unknown quantum channel control");
    return true;
  }

 private:
  std::shared_ptr<Session> session_;
  std::array<char, max_request_bytes> request_{};
  std::size_t used_ = 0, offset_ = 0, fallback_size_ = 0;
  std::string response_;
  std::array<char, 512> fallback_{};
  bool overflow_ = false, error_ = false;
  std::uint64_t requests_ = 0, errors_ = 0;

  // This path deliberately allocates nothing, including under memory pressure.
  void fail(const char* message) noexcept {
    error_ = true;
    ++errors_;
    response_.clear(); offset_ = fallback_size_ = 0;
    const auto add = [this](char c) {
      if (fallback_size_ < fallback_.size()) fallback_[fallback_size_++] = c;
    };
    for (const char c : std::string_view("{\"ok\":false,\"error\":\"")) add(c);
    for (std::size_t i = 0; message && message[i] && fallback_size_ + 8 < fallback_.size(); ++i) {
      const unsigned char c = static_cast<unsigned char>(message[i]);
      if (c == '"' || c == '\\') { add('\\'); add(static_cast<char>(c)); }
      else add(c >= 32 && c < 127 ? static_cast<char>(c) : ' ');
    }
    add('"'); add('}'); add('\n');
  }

  void submit() noexcept {
    ++requests_;
    if (overflow_) { fail("Quantum request was truncated; clear the channel"); return; }
    try {
      // InfiniteSession completes validation/output allocation before committing.
      auto response = session_->execute(std::string(request_.data(), used_));
      response_ = std::move(response);
      used_ = offset_ = fallback_size_ = 0;
      error_ = false;
    } catch (const std::exception& e) {
      used_ = 0;
      fail(e.what());
    } catch (...) {
      used_ = 0;
      fail("Quantum command failed");
    }
  }
};

} // namespace cosmic_pc
