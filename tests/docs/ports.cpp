/**
 * @file ports.cpp
 * @brief The port with every optional hook, as shown on the Porting page. Compiled, never run.
 */
#include <cstddef>
#include <cstdint>

#include "pcal9555.hpp"

bool platform_i2c_init() noexcept;
bool platform_i2c_write(uint8_t addr7, const uint8_t* data, std::size_t len) noexcept;
bool platform_i2c_write_read(uint8_t addr7, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                             std::size_t rx_len) noexcept;
void platform_log(const char* line) noexcept;
/// What the controller saw in the last failed transaction: 0 none known, 1 address NACK, 2 data
/// NACK.
int platform_i2c_last_nack() noexcept;

// The port's bus settings, checked against what the chip needs (handbook KB-CPP-123).
inline constexpr uint32_t kMyClockHz = 400'000;
static_assert(kMyClockHz <= pcal9555::kMaxClockHz, "SCL above the PCAL9555's maximum");
static_assert(pcal9555::kRepeatedStart, "WriteRead() must use a repeated START");

// --8<-- [start:full-port]
class FullPort : public pcal9555::I2cInterface<FullPort> {
public:
  bool EnsureInitialized() noexcept {
    return platform_i2c_init();
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

  // Optional: one line per lifecycle event or failure. The driver never formats; the port does.
  void Log(pcal9555::LogLevel level, const char* message) noexcept {
    if (level > pcal9555::LogLevel::Warning) {
      return;
    }
    platform_log(message); // plain text: never use it as a format string
  }

  // Optional: why the last Write() or WriteRead() failed. With it an absent chip is
  // Error::NoDevice; without it, or when this returns false, every failure is Error::Bus.
  bool GetLastFailure(pcal9555::I2cFailure& failure) noexcept {
    switch (platform_i2c_last_nack()) {
    case 1:
      failure = pcal9555::I2cFailure::AddressNack;
      return true;
    case 2:
      failure = pcal9555::I2cFailure::DataNack;
      return true;
    default:
      return false; // the controller cannot tell
    }
  }
};
// --8<-- [end:full-port]

// --8<-- [start:two-chips]
// Two expanders on one bus share the port; each driver object has its own address.
bool BringUpBoth(FullPort& bus) {
  pcal9555::PCAL9555<FullPort> left(bus, pcal9555::AddressFromPins(false, false, false)); // 0x20
  pcal9555::PCAL9555<FullPort> right(bus, pcal9555::AddressFromPins(false, false, true)); // 0x21
  return left.EnsureInitialized().ok() && right.EnsureInitialized().ok();
}
// --8<-- [end:two-chips]
