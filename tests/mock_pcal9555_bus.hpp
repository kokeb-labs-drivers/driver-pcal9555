/**
 * @file mock_pcal9555_bus.hpp
 * @brief I2C mock ports over the protocol model. The transaction decoding here is the only
 * bus-specific part of the host tests.
 *
 * | Port | Hooks |
 * | --- | --- |
 * | `BarePort` | the required ones only: every failed transfer is `Bus` to the driver |
 * | `FullPort` | also `Log()` and `GetLastFailure()`, which tells an address NACK from other
 * failures |
 */
#ifndef PCAL9555_MOCK_PCAL9555_BUS_HPP
#define PCAL9555_MOCK_PCAL9555_BUS_HPP

#include <cstddef>
#include <cstdint>

#include "mock_pcal9555.hpp"
#include "pcal9555_i2c_interface.hpp"
#include "pcal9555_types.hpp"

namespace pcal9555_test {

/// What the bus lines do, whatever the chip would answer (handbook KB-CPP-116).
enum class Line : uint8_t {
  Ok,        ///< The chip answers.
  FloatHigh, ///< SDA pulled up and nothing drives it: no address is acknowledged.
  StuckLow,  ///< SDA held low: no START can be made (a bus error).
};

/// Shared state of the mock ports: the chip, fault injection and counters.
struct MockBusState {
  Pcal9555Model chip;
  uint8_t address = chip::kBaseAddress; ///< The address the chip answers.
  bool bus_up_ok = true;                ///< EnsureInitialized() result.
  Line line = Line::Ok;                 ///< The bus lines.
  int fail_transfer = -1; ///< Fail the transaction with this index (0-based); -1 never.
  /// The write with this index reaches the chip, which stores every byte, but the port reports a
  /// failure (an ACK lost after the last byte, or noise on it); -1 never.
  int stored_then_fail = -1;
  int transfers = 0;       ///< Transactions so far.
  int reads = 0;           ///< Read transactions (write-read) so far.
  int bus_up_calls = 0;    ///< EnsureInitialized() calls so far.
  std::size_t max_len = 0; ///< Longest data phase seen, in bytes.
  /// Why the last failed transaction failed, as a port that knows the controller would say.
  pcal9555::I2cFailure last_failure = pcal9555::I2cFailure::Other;

  /// START, addr + W, [command, data...], STOP.
  bool DoWrite(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    const int index = transfers++;
    if (!addressed(index, addr) || len == 0 || data == nullptr) {
      return false;
    }
    max_len = len - 1 > max_len ? len - 1 : max_len;
    const bool acked = dataAcked(chip.Write(data, len));
    if (index == stored_then_fail) {
      last_failure = pcal9555::I2cFailure::DataNack;
      return false; // the bytes are in the chip; the master saw a NACK
    }
    return acked;
  }

  /// START, addr + W, [command], repeated START, addr + R, rx_len bytes, STOP.
  bool DoWriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                   std::size_t rx_len) noexcept {
    const int index = transfers++;
    reads++;
    if (!addressed(index, addr) || tx_len != 1 || rx == nullptr || rx_len == 0) {
      return false;
    }
    max_len = rx_len > max_len ? rx_len : max_len;
    return dataAcked(chip.WriteRead(tx[0], rx, rx_len));
  }

private:
  // The address phase: false (with the reason) when the transaction ends there.
  bool addressed(int index, uint8_t addr) noexcept {
    if (index == fail_transfer || line == Line::StuckLow) {
      last_failure = pcal9555::I2cFailure::Other; // a bus error
      return false;
    }
    if (line == Line::FloatHigh || addr != address) {
      last_failure = pcal9555::I2cFailure::AddressNack;
      return false;
    }
    return true;
  }
  // The chip NACKs a command byte it does not implement.
  bool dataAcked(bool acked) noexcept {
    if (!acked) {
      last_failure = pcal9555::I2cFailure::DataNack;
    }
    return acked;
  }
};

/// The required I2C hooks over a MockBusState. Derive `class P : public MockPortBase<P>`.
template <typename Self>
class MockPortBase : public pcal9555::I2cInterface<Self> {
public:
  MockBusState state;

  bool EnsureInitialized() noexcept {
    state.bus_up_calls++;
    return state.bus_up_ok;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    return state.DoWrite(addr, data, len);
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    return state.DoWriteRead(addr, tx, tx_len, rx, rx_len);
  }
};

/// Only the required hooks.
class BarePort : public MockPortBase<BarePort> {};

/// Every optional hook, with counters.
class FullPort : public MockPortBase<FullPort> {
public:
  int log_lines = 0;     ///< Log() calls.
  int error_lines = 0;   ///< Log() calls at LogLevel::Error.
  int failure_calls = 0; ///< GetLastFailure() calls.

  void Log(pcal9555::LogLevel level, const char* message) noexcept {
    (void)message;
    log_lines++;
    if (level == pcal9555::LogLevel::Error) {
      error_lines++;
    }
  }

  bool GetLastFailure(pcal9555::I2cFailure& failure) noexcept {
    failure_calls++;
    failure = state.last_failure;
    return true;
  }
};

} // namespace pcal9555_test

#endif // PCAL9555_MOCK_PCAL9555_BUS_HPP
