/**
 * @file host_pcal9555_port.hpp
 * @brief The host port: the driver's bus hooks over a simulated PCAL9555A, so the example runs on
 * any PC.
 *
 * `pcal9555_test::MockPortBase` (tests/mock_pcal9555_bus.hpp) supplies the required bus hooks over
 * the protocol model the host tests use. This class adds what a real port may add: `Log()` to the
 * console, and `GetLastFailure()`, so that an absent chip is `NoDevice`. A port for a real bus on a
 * PC (Linux i2c-dev, a USB bridge) implements the same hooks: see docs/porting.md.
 */
#ifndef PCAL9555_HOST_PCAL9555_PORT_HPP
#define PCAL9555_HOST_PCAL9555_PORT_HPP

#include <cstdint>
#include <cstdio>

#include "mock_pcal9555.hpp"

/// The host port: simulated chip, real console.
class HostPcal9555Port : public pcal9555_test::MockPortBase<HostPcal9555Port> {
public:
  /// The bus settings a real port would use; checked against the driver's bus requirements
  /// (handbook KB-CPP-123) like any port's.
  static constexpr uint32_t kClockHz = 400'000;
  static_assert(kClockHz <= pcal9555::kMaxClockHz, "SCL above the PCAL9555's maximum");
  static_assert(pcal9555::kRepeatedStart, "WriteRead() is one combined transaction");

  /// Why the last transfer failed: the simulated controller knows whether the address was NACKed.
  bool GetLastFailure(pcal9555::I2cFailure& failure) noexcept {
    failure = state.last_failure;
    return true;
  }

  /// Driver messages (they carry their own `pcal9555:` prefix) go to the console, one line each.
  void Log(pcal9555::LogLevel level, const char* message) noexcept {
    (void)level;
    (void)std::puts(message); // plain text: never pass it as a format string
  }
};

#endif // PCAL9555_HOST_PCAL9555_PORT_HPP
