#pragma once

#include <cstdint>
#include <optional>

// A bounded, single-channel subset of the PC/AT Intel 8042 PS/2 controller.
// Purpose: make real BIOS boot-loader A20 and keyboard-controller discovery
// protocols observable, without falsely implementing keyboard interrupts or
// every 8042 command. All operations finish synchronously (IBF is never busy).
//
// Port 0x64: read status, write command.
// Port 0x60: read response byte, write command payload.
// The authoritative A20 state is owned by Cpu. The controller is not allowed
// to create a second A20 state that would diverge from fast-gate port 0x92.
namespace cosmic_pc {
class Pc8042 {
 public:
  enum class Pending : std::uint8_t { None=0, ConfigByte=1, OutputPort=2 };
  struct Metrics {
    std::uint64_t command_writes=0;
    std::uint64_t data_writes=0;
    std::uint64_t status_reads=0;
    std::uint64_t data_reads=0;
    std::uint8_t last_command=0;
  };

  void reset() noexcept {
    config_=0x04;  // System POST flag. No PS/2 IRQ is actually generated.
    pending_=Pending::None;
    data_ready_=false;
    response_=0xff;
    command_last_=false;
    output_port_latched_=0x01;
    metrics_={};
  }

  std::optional<std::uint32_t> input(std::uint16_t port,
                                     unsigned width, bool a20) noexcept {
    if(width!=1)return std::nullopt;
    if(port==0x64) {
      ++metrics_.status_reads;
      // Bit0 output buffer full, bit1 input buffer busy (always 0 in
      // this synchronous model), bit2 POST, bit3 command/data marker.
      return std::uint32_t((data_ready_?1u:0u) |
                           (config_ & 4u) | (command_last_?8u:0u));
    }
    if(port==0x60) {
      ++metrics_.data_reads;
      // Empty read is an explicit open-bus approximation (0xff), not a
      // keyboard event. No scancode or IRQ source exists in this model.
      const std::uint8_t value=data_ready_?response_:0xff;
      data_ready_=false;
      return std::uint32_t(value);
    }
    (void)a20;
    return std::nullopt;
  }

  bool output(std::uint16_t port,unsigned width,std::uint32_t value,
              bool& a20) noexcept {
    if(width!=1 || (port!=0x64 && port!=0x60))return false;
    const auto v=std::uint8_t(value);
    if(port==0x64) {
      // A command cannot replace the expected data byte of an earlier D1/60.
      if(pending_!=Pending::None)return false;
      const bool reply=v==0x20 || v==0xd0 || v==0xaa || v==0xab;
      if(reply && data_ready_)return false; // no silent buffer overwrite
      switch(v) {
        case 0x20: offer(config_);break;              // Read controller config
        case 0x60: pending_=Pending::ConfigByte;break; // Next data -> config
        case 0xad: config_ |= 0x10;break;             // Disable keyboard clock
        case 0xae: config_ &= std::uint8_t(~0x10);break; // Enable keyboard
        case 0xa7: config_ |= 0x20;break;             // Disable second port
        case 0xaa: config_ |= 0x04;offer(0x55);break; // Self-test passes
        case 0xab: offer(0x00);break;                // First port test passes
        case 0xd0: offer(output_port(a20));break;    // Read output port / A20
        case 0xd1: pending_=Pending::OutputPort;break;// Next data -> A20
        // PC/AT "pulse output line" family 0xF0..0xFF, bits 0..3
        // are active-LOW masks. 0xFF means *none* of the four lines
        // are pulsed; unlike 0xFE it must not trigger CPU reset.
        // No response is queued, A20/config/output-port stay unchanged.
        // Other 0xF0..0xFE values remain unsupported pending real
        // output-line/reset modeling (not silently treated as no-ops).
        case 0xff: break;
        default:return false; // Explicitly unsupported, not a fake ACK
      }
      metrics_.last_command=v;
      ++metrics_.command_writes;
      command_last_=true;
      return true;
    }
    // Data port. The first two supported writes are required by actual
    // BIOS A20 sequences. Device keyboard commands (ED, FF, F4 etc.) are
    // deliberately NOT acknowledged: IRQ/scancode emulation is not present.
    if(pending_==Pending::ConfigByte) {
      config_=v;
      pending_=Pending::None;
    }else if(pending_==Pending::OutputPort) {
      // Low output-port bit asserts hardware reset. We must not silently
      // ignore it or treat it as a successful Windows boot.
      if(!(v&1))return false;
      a20=(v&2)!=0;
      output_port_latched_=v;
      pending_=Pending::None;
    }else return false;
    command_last_=false;
    ++metrics_.data_writes;
    return true;
  }

  std::uint8_t output_port(bool a20) const noexcept {
    return std::uint8_t((output_port_latched_ & std::uint8_t(~2u)) |
                        (a20 ? 2u : 0u) | 1u);
  }
  std::uint8_t config_byte() const noexcept {return config_;}
  bool keyboard_disabled() const noexcept {return (config_ & 0x10)!=0;}
  bool has_reply() const noexcept {return data_ready_;}
  Pending pending() const noexcept {return pending_;}
  Metrics metrics() const noexcept {return metrics_;}

 private:
  void offer(std::uint8_t v) noexcept {
    response_=v;
    data_ready_=true;
  }
  std::uint8_t config_=0x04, response_=0xff;
  std::uint8_t output_port_latched_=0x01;
  Pending pending_=Pending::None;
  bool data_ready_=false,command_last_=false;
  Metrics metrics_{};
};
} // namespace cosmic_pc
