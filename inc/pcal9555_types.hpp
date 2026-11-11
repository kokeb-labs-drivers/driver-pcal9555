/**
 * @file pcal9555_types.hpp
 * @brief Public types of the PCAL9555 driver: errors, `Result<T>`, pins and configuration.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is plain data with no platform dependency, so handlers, tests and tools can
 * include it without the driver template. The configuration defaults are the power-up values of
 * the registers (PCAL9555A data sheet Rev. 2, table 4), so `Config{}` describes a chip that was
 * just powered up.
 */
#ifndef PCAL9555_TYPES_HPP
#define PCAL9555_TYPES_HPP

#include <cstdint>

#include "pcal9555_version.h"

/** @brief The PCAL9555 driver: everything it declares lives here; internals in `detail`. */
namespace pcal9555 {

/**
 * @defgroup pcal9555_types Driver types
 * @brief Errors, results, pins and configuration.
 * @{
 */

/**
 * @brief Why a call failed. Every call of the driver returns one of these in its `Result`.
 *
 * Values 0 to 9 are the shared base set; chip-specific values start at 16. The chip has no timed
 * operation, no CRC, no busy state and no fault flag, so `Timeout`, `Crc`, `Busy` and
 * `DeviceFault` are never returned. The type is `[[nodiscard]]`: a call whose `Error` is dropped
 * does not compile without a warning (handbook KB-CPP20-037).
 */
enum class [[nodiscard]] Error : uint8_t {
  // Base set: the same names and values in every driver of the org (standards/driver.md).
  Ok = 0,               ///< The call succeeded.
  NotInitialized = 1,   ///< `EnsureInitialized()` has not succeeded yet, or `Deinitialize()` ran
                        ///< since, or a call found the chip reset (`DeviceReset`).
  InvalidArgument = 2,  ///< An argument, the address or a `Config` field is out of range; nothing
                        ///< was sent.
  Bus = 3,              ///< A port hook returned `false`: a bus error, a NACK of a data byte, or a
                        ///< NACK the port cannot place. A port without the optional
                        ///< `GetLastFailure()` hook reports an absent chip as `Bus` too.
  Timeout = 4,          ///< Not returned by this driver (the chip has no timed operation).
  DeviceIdMismatch = 5, ///< At start-up, something answers at the address but is not the
                        ///< configured part: a register read back differently after
                        ///< `EnsureInitialized()` wrote it, or a reserved bit of the PCAL9555A
                        ///< output port configuration register read 1.
  Crc = 6,              ///< Not returned by this driver (I2C transfers carry no CRC).
  Busy = 7,             ///< Not returned by this driver (the chip has no busy state).
  DeviceFault = 8,      ///< Not returned by this driver (the chip reports no faults).
  Unsupported = 9,      ///< An Agile I/O call or `Config` field on a driver configured for the
                        ///< PCA9555 (`Variant::Pca9555`).
  // Chip-specific values start at 16; each one is returned by some call (tests/test_errors.cpp).
  WriteMismatch = 16, ///< After start-up, a register read back differently from what was just
                      ///< written: the chip acknowledged the write but did not keep it (a write
                      ///< dropped on a noisy bus). Pin calls only with `Verify::ReadBack`;
                      ///< `ApplyConfig()` always.
  NoDevice = 17,      ///< Nothing acknowledged the address byte: no chip at the address, or it
                      ///< is unpowered. Needs a port with the optional `GetLastFailure()` hook,
                      ///< which tells an address NACK from other failures; without it this is
                      ///< `Bus`.
  DeviceReset = 18,   ///< A running driver found the chip back at its power-up values: a supply
                      ///< dip below the power-on reset voltage reset every register (section
                      ///< 6.4, table 4). Nothing was written; the driver is uninitialised, so
                      ///< call `EnsureInitialized()`, which writes the copy again in order. A write
                      ///< to the checked register that the chip dropped reads the same way.
};

/**
 * @brief The name of an error, for logs.
 * @param error The error.
 * @return A static string such as `"Bus"`; `"Unknown"` for a value that is not an enumerator.
 */
constexpr const char* ToString(Error error) noexcept {
  switch (error) {
  case Error::Ok:
    return "Ok";
  case Error::NotInitialized:
    return "NotInitialized";
  case Error::InvalidArgument:
    return "InvalidArgument";
  case Error::Bus:
    return "Bus";
  case Error::Timeout:
    return "Timeout";
  case Error::DeviceIdMismatch:
    return "DeviceIdMismatch";
  case Error::Crc:
    return "Crc";
  case Error::Busy:
    return "Busy";
  case Error::DeviceFault:
    return "DeviceFault";
  case Error::Unsupported:
    return "Unsupported";
  case Error::WriteMismatch:
    return "WriteMismatch";
  case Error::NoDevice:
    return "NoDevice";
  case Error::DeviceReset:
    return "DeviceReset";
  default:
    break; // a value made by static_cast from an integer that is not an enumerator
  }
  return "Unknown";
}

/**
 * @brief The value of a call, or why there is none.
 *
 * Check `ok()` (or the `bool` conversion) before reading `value`; on failure `value` is `T{}`.
 *
 * @tparam T The value type. `Result<void>` carries only the error.
 */
template <typename T>
struct [[nodiscard]] Result {
  T value{};              ///< The value; `T{}` when the call failed.
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /**
   * @brief A successful result.
   * @param v The value.
   * @return `{v, Error::Ok}`.
   */
  static constexpr Result Success(T v) noexcept {
    return {v, Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{T{}, e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {T{}, e};
  }
};

/** @brief The result of a call that returns no value. */
template <>
struct [[nodiscard]] Result<void> {
  Error error{Error::Ok}; ///< `Error::Ok` on success.

  /** @return true when the call succeeded. */
  [[nodiscard]] constexpr bool ok() const noexcept {
    return error == Error::Ok;
  }
  /** @return true when the call succeeded. */
  constexpr explicit operator bool() const noexcept {
    return ok();
  }

  /** @return A successful result. */
  static constexpr Result Success() noexcept {
    return {Error::Ok};
  }
  /**
   * @brief A failed result.
   * @param e Why it failed; never `Error::Ok`.
   * @return `{e}`.
   */
  static constexpr Result Failure(Error e) noexcept {
    return {e};
  }
};

/** @brief Severity passed to the optional `Log()` hook. */
enum class LogLevel : uint8_t {
  Error = 0, ///< A call failed.
  Warning,   ///< Something unexpected that the driver recovered from.
  Info,      ///< Lifecycle events: initialised.
  Debug,     ///< Detail for bring-up.
};

/**
 * @brief Which part is on the bus. The driver does not detect it: the data sheets do not say how a
 * PCA9555 answers a command byte it does not implement, so probing the Agile I/O bank is not safe.
 */
enum class Variant : uint8_t {
  Pca9555 = 0,   ///< PCA9555 (or a PCAL9555A used as one): registers 0x00 to 0x07 only. Default.
  Pcal9555a = 1, ///< PCAL9555A: also the Agile I/O bank at 0x40 to 0x4F.
};

/** @brief Direction of a pin (configuration register: 1 = input). */
enum class Direction : uint8_t {
  Output = 0, ///< The pin drives the output port value.
  Input = 1,  ///< High-impedance input. Power-up value.
};

/** @brief Output drive strength of a pin, PCAL9555A only (data sheet section 6.2.6). */
enum class DriveStrength : uint8_t {
  Quarter = 0,       ///< 0.25 of the maximum drive.
  Half = 1,          ///< 0.5 of the maximum drive.
  ThreeQuarters = 2, ///< 0.75 of the maximum drive.
  Full = 3,          ///< The maximum drive. Power-up value.
};

/**
 * @brief Whether a pin call reads back what it wrote, chosen per call (the last parameter of every
 * pin and Agile I/O setter).
 *
 * The data sheet needs only the write, so that is the default. `ReadBack` costs one more I2C read
 * per register written (a 2-byte read after each pair, 1 byte after 0x4F), which more than doubles
 * the call's bus time. It catches a write the chip acknowledged but did not keep (a write dropped
 * on a noisy bus), which `ReadInputs()` cannot show for an output pin, and returns
 * `Error::WriteMismatch` for it. `EnsureInitialized()` and `ApplyConfig()` always read the whole
 * configuration back.
 */
enum class Verify : uint8_t {
  None = 0,     ///< Write only. Default.
  ReadBack = 1, ///< Write, then read the register back; a difference is `Error::WriteMismatch`.
};

/** @brief Pull resistor of a pin, PCAL9555A only (pull enable and selection registers). */
enum class Pull : uint8_t {
  None = 0, ///< No resistor (enable bit 0).
  Up = 1,   ///< 100 kOhm pull-up (enable 1, selection 1). Power-up value.
  Down = 2, ///< 100 kOhm pull-down (enable 1, selection 0).
};

/** @brief Output stage of a whole port, PCAL9555A only (output port configuration register). */
enum class OutputMode : uint8_t {
  PushPull = 0,  ///< Both transistors active. Power-up value.
  OpenDrain = 1, ///< Low-side transistor only; the pull resistors of the port are disconnected.
};

/** @brief One of the two 8-pin ports. */
enum class Bank : uint8_t {
  Port0 = 0, ///< P0_0 to P0_7, pins 0 to 7.
  Port1 = 1, ///< P1_0 to P1_7, pins 8 to 15.
};

/// Number of pins.
inline constexpr uint8_t kPinCount = 16;
/// Mask of every pin.
inline constexpr uint16_t kAllPins = 0xFFFF;
/// Lowest 7-bit I2C address (A2, A1, A0 all low): 0100 A2 A1 A0 (data sheet section 6.1).
inline constexpr uint8_t kMinI2cAddress = 0x20;
/// Highest 7-bit I2C address (A2, A1, A0 all high).
inline constexpr uint8_t kMaxI2cAddress = 0x27;
/// Default 7-bit I2C address, with A2, A1 and A0 low: the same value as `kI2cAddress`, the bus
/// requirement in `pcal9555_i2c_interface.hpp`. Kept so that existing code still compiles.
inline constexpr uint8_t kDefaultI2cAddress = kMinI2cAddress;

/**
 * @brief The mask of one pin.
 * @param pin 0 to 15; 0 to 7 are P0_0 to P0_7, 8 to 15 are P1_0 to P1_7.
 * @return `1 << pin`, or 0 for a pin out of range (which every mask call rejects).
 */
constexpr uint16_t PinMask(uint8_t pin) noexcept {
  return (pin < kPinCount) ? static_cast<uint16_t>(uint32_t{1} << static_cast<uint32_t>(pin))
                           : uint16_t{0};
}

/**
 * @brief The 7-bit address for the levels of the address pins.
 * @param a2 true when A2 is high.
 * @param a1 true when A1 is high.
 * @param a0 true when A0 is high.
 * @return 0x20 to 0x27.
 */
constexpr uint8_t AddressFromPins(bool a2, bool a1, bool a0) noexcept {
  return static_cast<uint8_t>(static_cast<uint32_t>(kMinI2cAddress) | (a2 ? 4U : 0U) |
                              (a1 ? 2U : 0U) | (a0 ? 1U : 0U));
}

/**
 * @brief Drive strength codes with every pin at one level, in the layout of `Config`.
 * @param level The level.
 * @return Two bits per pin, pin n at bits 2n+1..2n.
 */
constexpr uint32_t DriveStrengthAll(DriveStrength level) noexcept {
  // Two bits per code: a value that is not an enumerator is masked, never shifted into the next
  // pin.
  const uint32_t code = static_cast<uint32_t>(level) & 0x3U;
  uint32_t codes = 0U;
  for (uint32_t pin = 0U; pin < kPinCount; ++pin) {
    codes |= code << (2U * pin);
  }
  return codes;
}

/**
 * @brief Everything `EnsureInitialized()` and `ApplyConfig()` write to the chip.
 *
 * Bit n of each mask is pin n. The defaults are the power-up values of the registers, so `Config{}`
 * changes nothing on a freshly powered chip. The fields below `variant` marked "PCAL9555A" must
 * keep their default when `variant` is `Variant::Pca9555` (`Validate()` returns `Unsupported`
 * otherwise).
 */
struct Config {
  /// Driver side: which part is fitted. The Agile I/O calls need `Variant::Pcal9555a`.
  Variant variant{Variant::Pca9555};

  uint16_t output_high_mask{0xFFFF}; ///< Output ports: 1 = drive high. Power-up: all high.
  uint16_t polarity_invert_mask{0};  ///< Polarity inversion: 1 = input reads inverted.
  uint16_t input_mask{0xFFFF};       ///< Configuration: 1 = input, 0 = output. Power-up: inputs.

  /// PCAL9555A: drive strength codes (`DriveStrength`), two bits per pin, pin n at bits 2n+1..2n.
  uint32_t drive_strength_codes{0xFFFF'FFFFU};
  uint16_t input_latch_mask{0};                ///< PCAL9555A: 1 = input change latched until read.
  uint16_t pull_enable_mask{0xFFFF};           ///< PCAL9555A: 1 = pull resistor connected.
  uint16_t pull_up_mask{0xFFFF};               ///< PCAL9555A: 1 = pull-up, 0 = pull-down.
  uint16_t interrupt_mask{0xFFFF};             ///< PCAL9555A: 1 = the pin raises no interrupt.
  OutputMode port0_mode{OutputMode::PushPull}; ///< PCAL9555A: output stage of port 0 (ODEN0).
  OutputMode port1_mode{OutputMode::PushPull}; ///< PCAL9555A: output stage of port 1 (ODEN1).

  /**
   * @brief Field by field; every field is an integer or an enumeration, so equality is exact.
   * @param lhs The first configuration.
   * @param rhs The configuration to compare with.
   * @return true when every field is equal.
   */
  [[nodiscard]] friend constexpr bool operator==(const Config& lhs,
                                                 const Config& rhs) noexcept = default;
};

/**
 * @brief Check a configuration without touching the chip.
 *
 * @param config The configuration.
 * @return `Error::Ok`; `Error::InvalidArgument` when an enum holds a value that is not an
 *         enumerator; `Error::Unsupported` when `variant` is `Variant::Pca9555` and an Agile I/O
 *         field differs from its power-up value.
 */
constexpr Error Validate(const Config& config) noexcept {
  if ((static_cast<uint32_t>(config.variant) > static_cast<uint32_t>(Variant::Pcal9555a)) ||
      (static_cast<uint32_t>(config.port0_mode) > static_cast<uint32_t>(OutputMode::OpenDrain)) ||
      (static_cast<uint32_t>(config.port1_mode) > static_cast<uint32_t>(OutputMode::OpenDrain))) {
    return Error::InvalidArgument;
  }
  if (config.variant == Variant::Pca9555) {
    // Every field but the four both parts have must be at its power-up value. The comparison is
    // of the whole struct, so a field added later is covered without a change here.
    Config agile = config;
    const Config reset{};
    agile.output_high_mask = reset.output_high_mask;
    agile.polarity_invert_mask = reset.polarity_invert_mask;
    agile.input_mask = reset.input_mask;
    if (!(agile == reset)) {
      return Error::Unsupported;
    }
  }
  return Error::Ok;
}

/** @return The driver version, e.g. `"0.1.0"`, from the generated `pcal9555_version.h`. No I/O. */
constexpr const char* GetDriverVersion() noexcept {
  return PCAL9555_VERSION_STRING;
}
/** @return The major version. No I/O. */
constexpr uint32_t GetDriverVersionMajor() noexcept {
  return PCAL9555_VERSION_MAJOR;
}
/** @return The minor version. No I/O. */
constexpr uint32_t GetDriverVersionMinor() noexcept {
  return PCAL9555_VERSION_MINOR;
}
/** @return The patch version. No I/O. */
constexpr uint32_t GetDriverVersionPatch() noexcept {
  return PCAL9555_VERSION_PATCH;
}

/** @} */

} // namespace pcal9555

#endif // PCAL9555_TYPES_HPP
