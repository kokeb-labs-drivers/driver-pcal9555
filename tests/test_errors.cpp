/**
 * @file test_errors.cpp
 * @brief The `Error` enum follows the standard, and its values are returned by real calls.
 *
 * - The base set (0 to 9) has the names and values every driver of the org shares.
 * - Every chip-specific value (16 and up) MUST be returned by some call: the enumerators are found
 *   through `ToString()` (-Wswitch keeps it complete), so a new value fails this test until a
 *   scenario below produces it.
 * - Base values MAY go unreturned: the chip has no timed operation, no CRC, no busy state and no
 *   fault flag, so `Timeout`, `Crc`, `Busy` and `DeviceFault` are never returned (the API guide
 *   lists them).
 */
#include <array>
#include <cstdio>
#include <cstring>

#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"
#include "test_harness.hpp"

using pcal9555::Error;
using pcal9555_test::BarePort;
using pcal9555_test::FullPort;

static_assert(static_cast<int>(Error::WriteMismatch) == 16 &&
                  static_cast<int>(Error::NoDevice) == 17 &&
                  static_cast<int>(Error::DeviceReset) == 18,
              "chip-specific values start at 16 and never change");
static_assert(static_cast<int>(Error::Ok) == 0 && static_cast<int>(Error::NotInitialized) == 1 &&
                  static_cast<int>(Error::InvalidArgument) == 2 &&
                  static_cast<int>(Error::Bus) == 3 && static_cast<int>(Error::Timeout) == 4 &&
                  static_cast<int>(Error::DeviceIdMismatch) == 5 &&
                  static_cast<int>(Error::Crc) == 6 && static_cast<int>(Error::Busy) == 7 &&
                  static_cast<int>(Error::DeviceFault) == 8 &&
                  static_cast<int>(Error::Unsupported) == 9,
              "the base Error set has fixed values (standards/driver.md)");

namespace {

constexpr bool kRequireEveryBaseValue = false;
constexpr int kFirstChipSpecific = 16;

// Which Error values some scenario returned; written by Record() while the scenarios run.
std::array<bool, 256> g_seen{}; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables)

void Record(Error e) {
  g_seen[static_cast<uint8_t>(e)] = true;
}

void RunScenarios() {
  { // Ok, NotInitialized, InvalidArgument, Unsupported
    BarePort port;
    pcal9555::PCAL9555<BarePort> io(port);
    Record(io.ReadInputs().error);
    Record(io.EnsureInitialized().error);
    Record(io.SetOutputs(0, true).error);
    Record(io.SetPull(1, pcal9555::Pull::Down).error);
  }
  { // Bus
    BarePort port;
    port.state.bus_up_ok = false;
    pcal9555::PCAL9555<BarePort> io(port);
    Record(io.EnsureInitialized().error);
  }
  { // DeviceIdMismatch
    BarePort port;
    port.state.chip.stuck_register = pcal9555::reg::kOutputPort0;
    pcal9555::Config c{};
    c.output_high_mask = 0;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    Record(io.EnsureInitialized().error);
  }
  { // NoDevice: nothing acknowledges the address, and the port says so
    FullPort port;
    port.state.address = 0x27;
    pcal9555::PCAL9555<FullPort> io(port);
    Record(io.EnsureInitialized().error);
  }
  { // WriteMismatch (a pin call only with Verify::ReadBack)
    BarePort port;
    pcal9555::PCAL9555<BarePort> io(port);
    REQUIRE(io.EnsureInitialized().ok());
    port.state.chip.drop_writes = 1;
    Record(io.SetOutputs(1, false, pcal9555::Verify::ReadBack).error);
  }
  { // DeviceReset: the chip went through a power-on reset since the start (section 6.4)
    BarePort port;
    pcal9555::Config c{};
    c.input_mask = 0x00FF;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    port.state.chip.PowerOn();
    Record(io.CheckForReset().error);
  }
}

} // namespace

TEST_CASE("Every chip-specific Error value is returned by some call") {
  RunScenarios();
  int enumerators = 0;
  for (int v = 0; v < 256; ++v) {
    const auto e = static_cast<Error>(v);
    if (std::strcmp(pcal9555::ToString(e), "Unknown") == 0) {
      continue;
    }
    ++enumerators;
    const bool required = v >= kFirstChipSpecific || kRequireEveryBaseValue;
    const bool seen = g_seen[static_cast<std::size_t>(v)];
    if (!seen) {
      std::printf("    %s: Error::%s is never returned\n", required ? "FAIL" : "note",
                  pcal9555::ToString(e));
    }
    CHECK(seen || !required);
    CHECK(v < 10 || v >= kFirstChipSpecific); // 10 to 15 are reserved for the base set
  }
  CHECK(enumerators >= 2);
}
