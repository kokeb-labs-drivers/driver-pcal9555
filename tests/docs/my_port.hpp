/**
 * @file my_port.hpp
 * @brief The I2C port shown on the Quickstart and Porting pages, compiled by the host build.
 *
 * Every platform call goes through the `platform_*` functions declared below; a real port replaces
 * them with its SDK (ESP-IDF, STM32 HAL, Zephyr, Linux i2c-dev).
 */
#ifndef PCAL9555_MY_PORT_HPP
#define PCAL9555_MY_PORT_HPP

#include <cstddef>
#include <cstdint>

#include "pcal9555_i2c_interface.hpp"

// Your SDK's calls. Declared only: the docs build checks that the port compiles.
bool platform_i2c_init() noexcept; // 100 or 400 kHz
bool platform_i2c_write(uint8_t addr7, const uint8_t* data,
                        std::size_t len) noexcept; // START .. STOP
bool platform_i2c_write_read(uint8_t addr7, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                             std::size_t rx_len) noexcept; // repeated START

// --8<-- [start:port]
class MyPort : public pcal9555::I2cInterface<MyPort> {
public:
  // Bring the bus up once; later calls return true at once.
  bool EnsureInitialized() noexcept {
    if (!ready_) {
      ready_ = platform_i2c_init();
    }
    return ready_;
  }

  // One write transaction: START, address + W, the bytes (command byte first), STOP.
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return platform_i2c_write(addr, data, len);
  }

  // Write then read with a repeated START in between.
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return platform_i2c_write_read(addr, tx, tx_len, rx, rx_len);
  }

private:
  bool ready_ = false;
};
// --8<-- [end:port]

#endif // PCAL9555_MY_PORT_HPP
