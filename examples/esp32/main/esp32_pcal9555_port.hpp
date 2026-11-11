/**
 * @file esp32_pcal9555_port.hpp
 * @brief ESP-IDF port for the PCAL9555 driver: i2c_master for the bus and ESP_LOG for the log
 * hook.
 *
 * Start from this file for any ESP-IDF target. The pins are in Esp32Pcal9555Pins; the defaults
 * match the wiring table in examples/esp32/README.md (ESP32-C6 DevKitC-1).
 */
#ifndef PCAL9555_ESP32_PCAL9555_PORT_HPP
#define PCAL9555_ESP32_PCAL9555_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "driver/i2c_master.h"
#include "esp_log.h"

#include "pcal9555.hpp"

/// The port's default SCL clock and the example's address, checked against the driver's bus
/// requirements (handbook KB-CPP-123).
inline constexpr uint32_t kEsp32Pcal9555ClockHz = 400'000;
/// A2, A1 and A0 low, as wired in examples/esp32/README.md.
inline constexpr uint8_t kEsp32Pcal9555Address = pcal9555::AddressFromPins(false, false, false);
static_assert(kEsp32Pcal9555ClockHz <= pcal9555::kMaxClockHz, "SCL above the PCAL9555's maximum");
static_assert(kEsp32Pcal9555Address >= pcal9555::kMinI2cAddress &&
                  kEsp32Pcal9555Address <= pcal9555::kMaxI2cAddress,
              "the PCAL9555 answers at 0x20 to 0x27 only");
static_assert(pcal9555::kRepeatedStart, "this port reads with i2c_master_transmit_receive");

/// Pins and bus settings of the ESP-IDF port.
struct Esp32Pcal9555Pins {
  i2c_port_num_t port = I2C_NUM_0;
  int sda = 6;
  int scl = 7;
  uint32_t clock_hz = kEsp32Pcal9555ClockHz; ///< Up to `pcal9555::kMaxClockHz`.
  int timeout_ms = 10;                       ///< Per transaction.
};

/// The PCAL9555 I2C port on ESP-IDF, with the optional Log hook. It has no GetLastFailure() hook:
/// i2c_master reports a NACK without saying whether the address or a data byte was refused, so
/// an absent chip is `Error::Bus` with this port (docs/porting.md).
class Esp32Pcal9555Port : public pcal9555::I2cInterface<Esp32Pcal9555Port> {
public:
  explicit Esp32Pcal9555Port(const Esp32Pcal9555Pins& pins = {}) noexcept : pins_(pins) {}

  bool EnsureInitialized() noexcept {
    if (bus_ != nullptr) {
      return true;
    }
    i2c_master_bus_config_t bus{};
    bus.i2c_port = pins_.port;
    bus.sda_io_num = static_cast<gpio_num_t>(pins_.sda);
    bus.scl_io_num = static_cast<gpio_num_t>(pins_.scl);
    bus.clk_source = I2C_CLK_SRC_DEFAULT;
    bus.glitch_ignore_cnt = 7;
    bus.flags.enable_internal_pullup = true; // weak: fit 4.7 kOhm pull-ups on the board
    if (i2c_new_master_bus(&bus, &bus_) != ESP_OK) {
      bus_ = nullptr;
      return false;
    }
    return true;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return dev != nullptr && i2c_master_transmit(dev, data, len, pins_.timeout_ms) == ESP_OK;
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    i2c_master_dev_handle_t dev = device(addr);
    return dev != nullptr &&
           i2c_master_transmit_receive(dev, tx, tx_len, rx, rx_len, pins_.timeout_ms) == ESP_OK;
  }

  void Log(pcal9555::LogLevel level, const char* message) noexcept {
    const esp_log_level_t esp_level = level == pcal9555::LogLevel::Error     ? ESP_LOG_ERROR
                                      : level == pcal9555::LogLevel::Warning ? ESP_LOG_WARN
                                      : level == pcal9555::LogLevel::Info    ? ESP_LOG_INFO
                                                                             : ESP_LOG_DEBUG;
    esp_log_write(esp_level, kTag, "%s\n", message); // plain text, never a format
  }

private:
  // The device handle for one 7-bit address, created on first use (one chip per port in this
  // example).
  i2c_master_dev_handle_t device(uint8_t addr) noexcept {
    if (device_ != nullptr && device_addr_ == addr) {
      return device_;
    }
    if (device_ != nullptr) {
      i2c_master_bus_rm_device(device_);
      device_ = nullptr;
    }
    i2c_device_config_t cfg{};
    cfg.dev_addr_length = I2C_ADDR_BIT_LEN_7;
    cfg.device_address = addr;
    cfg.scl_speed_hz = pins_.clock_hz;
    if (i2c_master_bus_add_device(bus_, &cfg, &device_) != ESP_OK) {
      device_ = nullptr;
      return nullptr;
    }
    device_addr_ = addr;
    return device_;
  }

  static constexpr const char* kTag = "pcal9555";
  Esp32Pcal9555Pins pins_;
  i2c_master_bus_handle_t bus_ = nullptr;
  i2c_master_dev_handle_t device_ = nullptr;
  uint8_t device_addr_ = 0;
};

#endif // PCAL9555_ESP32_PCAL9555_PORT_HPP
