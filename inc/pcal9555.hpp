/**
 * @file pcal9555.hpp
 * @brief PCAL9555 driver: the one header to include.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * `pcal9555::PCAL9555<Port>` drives one NXP PCA9555 or PCAL9555A 16-bit I2C GPIO expander through
 * a platform port you write (see `pcal9555_i2c_interface.hpp`). The driver has no platform code,
 * allocates nothing, throws nothing, takes no lock and is not thread-safe: give each object one
 * owner, or guard it with one mutex.
 */
#ifndef PCAL9555_HPP
#define PCAL9555_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pcal9555_detail.hpp"
#include "pcal9555_i2c_interface.hpp"
#include "pcal9555_registers.hpp"
#include "pcal9555_types.hpp"
#include "pcal9555_version.h"

namespace pcal9555 {

namespace detail {

/// `value` with the bits of `mask` set (`set`) or cleared.
[[nodiscard]] constexpr uint16_t Apply(uint16_t value, uint16_t mask, bool set) noexcept {
  const auto v = static_cast<uint32_t>(value);
  const auto m = static_cast<uint32_t>(mask);
  return static_cast<uint16_t>(set ? (v | m) : (v & ~m));
}

/// `codes` with every pin of `mask` at drive strength `level` (a value that is not an enumerator
/// is masked to two bits; the setters refuse it before they get here).
[[nodiscard]] constexpr uint32_t ApplyDrive(uint32_t codes, uint16_t mask,
                                            DriveStrength level) noexcept {
  const uint32_t code = static_cast<uint32_t>(ToUnderlying(level)) & reg::kDriveCodeMask;
  for (uint32_t pin = 0U; pin < kPinCount; ++pin) {
    if ((static_cast<uint32_t>(mask) & (uint32_t{1} << pin)) != 0U) {
      const uint32_t shift = reg::kDriveBitsPerPin * pin;
      codes = (codes & ~(reg::kDriveCodeMask << shift)) | (code << shift);
    }
  }
  return codes;
}

/// The 16-bit value of a register pair: port 0 (the even command byte) in bits 7..0.
[[nodiscard]] constexpr uint16_t PairValueOf(std::span<const uint8_t, 2> bytes) noexcept {
  return static_cast<uint16_t>(static_cast<uint32_t>(bytes[0]) |
                               (static_cast<uint32_t>(bytes[1]) << 8U));
}

/// `value` with byte `port` (0 or 1) replaced by `byte`.
[[nodiscard]] constexpr uint16_t WithByte(uint16_t value, uint32_t port, uint8_t byte) noexcept {
  const uint32_t shift = (port & 1U) * 8U;
  return static_cast<uint16_t>((static_cast<uint32_t>(value) & ~(0xFFU << shift)) |
                               (static_cast<uint32_t>(byte) << shift));
}

/// One register pair and the value it should hold.
struct PairValue {
  uint8_t reg;
  uint16_t value;
};

/// The pairs both parts have, in write order: output values before directions.
[[nodiscard]] constexpr std::array<PairValue, 3> StandardPairs(const Config& c) noexcept {
  return {{{.reg = reg::kOutputPort0, .value = c.output_high_mask},
           {.reg = reg::kPolarityPort0, .value = c.polarity_invert_mask},
           {.reg = reg::kConfigPort0, .value = c.input_mask}}};
}

/// The Agile I/O pairs, in write order: the pull selection before the pull enable.
[[nodiscard]] constexpr std::array<PairValue, 6> AgilePairs(const Config& c) noexcept {
  return {
      {{.reg = reg::kDriveStrength0,
        .value = static_cast<uint16_t>(c.drive_strength_codes & 0xFFFFU)},
       {.reg = reg::kDriveStrength2, .value = static_cast<uint16_t>(c.drive_strength_codes >> 16U)},
       {.reg = reg::kInputLatch0, .value = c.input_latch_mask},
       {.reg = reg::kPullSelect0, .value = c.pull_up_mask},
       {.reg = reg::kPullEnable0, .value = c.pull_enable_mask},
       {.reg = reg::kInterruptMask0, .value = c.interrupt_mask}}};
}

/// The output port configuration register for a configuration.
[[nodiscard]] constexpr uint8_t OutputPortConfig(const Config& config) noexcept {
  uint32_t value = 0U;
  if (config.port0_mode == OutputMode::OpenDrain) {
    value |= static_cast<uint32_t>(reg::kOutputConfigOden0);
  }
  if (config.port1_mode == OutputMode::OpenDrain) {
    value |= static_cast<uint32_t>(reg::kOutputConfigOden1);
  }
  return static_cast<uint8_t>(value);
}

/// `ResetSentinel::reg` when no register differs from its power-up value (the input port, which
/// the check never reads).
inline constexpr uint8_t kNoSentinel = 0x00U;
static_assert(kNoSentinel == reg::kInputPort0);

/// The register the reset check reads, what the driver wrote to it and its power-up value.
struct ResetSentinel {
  uint8_t reg;    ///< The command byte; `kNoSentinel` when there is none.
  uint16_t value; ///< What the driver's copy says the chip holds.
  uint16_t reset; ///< What a power-on reset loads (table 4).
  bool pair;      ///< `false` for 0x4F, the one register without a partner.
  bool only;      ///< No other register of the copy differs from its power-up value.
};

/// The bit of the driver's unknown-register set for a register it writes: 0x02 to 0x06 are bits 1
/// to 3, 0x40 to 0x4A bits 4 to 9, 0x4F bit 10.
[[nodiscard]] constexpr uint32_t UncertainIndex(uint8_t command) noexcept {
  const auto c = static_cast<uint32_t>(command);
  if (c == static_cast<uint32_t>(reg::kOutputPortConfig)) {
    return 10U;
  }
  if (c >= static_cast<uint32_t>(reg::kDriveStrength0)) {
    return (4U + ((c - static_cast<uint32_t>(reg::kDriveStrength0)) / 2U)) & 0x0FU;
  }
  return (c / 2U) & 0x0FU;
}

/// The bit of `UncertainIndex()`.
[[nodiscard]] constexpr uint32_t UncertainBit(uint8_t command) noexcept {
  return uint32_t{1} << UncertainIndex(command);
}

/// Every register the driver writes, as bits of `UncertainBit()`.
inline constexpr uint32_t kAllUncertain = 0x07FEU;

/// The stronger drive strength code of each pin of `a` and `b`.
[[nodiscard]] constexpr uint32_t StrongerDrive(uint32_t a, uint32_t b) noexcept {
  uint32_t codes = 0U;
  for (uint32_t pin = 0U; pin < kPinCount; ++pin) {
    const uint32_t shift = reg::kDriveBitsPerPin * pin;
    const uint32_t ca = (a >> shift) & reg::kDriveCodeMask;
    const uint32_t cb = (b >> shift) & reg::kDriveCodeMask;
    codes |= ((ca > cb) ? ca : cb) << shift;
  }
  return codes;
}

/**
 * What the copy holds after a write whose transfer failed: the chip may hold the old value, the
 * asked one or, for a pair, one byte of each, so each field takes the riskier of the two. A pin
 * that was or was asked to be an output is an output; outputs keep the written level; each pin
 * keeps the stronger drive; a port is push-pull if either was; the other fields take the written
 * value. The driver reads such a register back before it builds on it again.
 */
[[nodiscard]] constexpr Config Riskier(const Config& old, const Config& asked) noexcept {
  Config r = asked;
  r.input_mask = static_cast<uint16_t>(static_cast<uint32_t>(old.input_mask) &
                                       static_cast<uint32_t>(asked.input_mask));
  r.drive_strength_codes = StrongerDrive(old.drive_strength_codes, asked.drive_strength_codes);
  if (old.port0_mode == OutputMode::PushPull) {
    r.port0_mode = OutputMode::PushPull;
  }
  if (old.port1_mode == OutputMode::PushPull) {
    r.port1_mode = OutputMode::PushPull;
  }
  return r;
}

/// The value register `command` holds for configuration `c` (a pair, or 0x4F in bits 7..0).
[[nodiscard]] constexpr uint16_t RegisterValue(const Config& c, uint8_t command) noexcept {
  uint32_t v = 0U;
  if (command == reg::kOutputPort0) {
    v = c.output_high_mask;
  } else if (command == reg::kPolarityPort0) {
    v = c.polarity_invert_mask;
  } else if (command == reg::kConfigPort0) {
    v = c.input_mask;
  } else if (command == reg::kDriveStrength0) {
    v = c.drive_strength_codes & 0xFFFFU;
  } else if (command == reg::kDriveStrength2) {
    v = c.drive_strength_codes >> 16U;
  } else if (command == reg::kInputLatch0) {
    v = c.input_latch_mask;
  } else if (command == reg::kPullEnable0) {
    v = c.pull_enable_mask;
  } else if (command == reg::kPullSelect0) {
    v = c.pull_up_mask;
  } else if (command == reg::kInterruptMask0) {
    v = c.interrupt_mask;
  } else if (command == reg::kOutputPortConfig) {
    v = OutputPortConfig(c);
  } else {
    // Not a register the driver writes.
  }
  return static_cast<uint16_t>(v);
}

/// `c` with the field of register pair `command` set to `value`, as the chip reported it.
constexpr void Adopt(Config& c, uint8_t command, uint16_t value) noexcept {
  const auto v = static_cast<uint32_t>(value);
  if (command == reg::kOutputPort0) {
    c.output_high_mask = value;
  } else if (command == reg::kPolarityPort0) {
    c.polarity_invert_mask = value;
  } else if (command == reg::kConfigPort0) {
    c.input_mask = value;
  } else if (command == reg::kDriveStrength0) {
    c.drive_strength_codes = (c.drive_strength_codes & 0xFFFF'0000U) | v;
  } else if (command == reg::kDriveStrength2) {
    c.drive_strength_codes = (c.drive_strength_codes & 0x0000'FFFFU) | (v << 16U);
  } else if (command == reg::kInputLatch0) {
    c.input_latch_mask = value;
  } else if (command == reg::kPullEnable0) {
    c.pull_enable_mask = value;
  } else if (command == reg::kPullSelect0) {
    c.pull_up_mask = value;
  } else if (command == reg::kInterruptMask0) {
    c.interrupt_mask = value;
  } else if (command == reg::kOutputPortConfig) {
    c.port0_mode = ((v & static_cast<uint32_t>(reg::kOutputConfigOden0)) != 0U)
                       ? OutputMode::OpenDrain
                       : OutputMode::PushPull;
    c.port1_mode = ((v & static_cast<uint32_t>(reg::kOutputConfigOden1)) != 0U)
                       ? OutputMode::OpenDrain
                       : OutputMode::PushPull;
  } else {
    // Not a register the driver writes: nothing to adopt.
  }
}

/// One register the reset check may read.
[[nodiscard]] constexpr ResetSentinel Candidate(uint8_t command, uint16_t value, uint16_t reset,
                                                bool pair = true) noexcept {
  return {.reg = command, .value = value, .reset = reset, .pair = pair, .only = true};
}

/**
 * The register the reset check reads for a configuration: the first one, in the order below, whose
 * copy differs from its power-up value (table 4). The chip has no reset flag (section 6.4), so a
 * register that reads its power-up value while the copy says otherwise shows the reset. When every
 * register is at its power-up value there is none: a reset then leaves the chip as the driver
 * believes it to be. The registers `SetDirection()` writes back (0x4F, the output and configuration
 * pairs) come after the others, so a reset between its check and its writes is still found by the
 * next check; the output pair, written most often, comes last. A register whose last write failed
 * (`uncertain`, bits of `UncertainBit()`) is never the one read. `only` tells a setter about to
 * rewrite the register that its write repairs the chip whatever happened: the chip then holds
 * either the copy or the power-up values, which differ in that register only.
 */
[[nodiscard]] constexpr ResetSentinel FindResetSentinel(const Config& c, bool agile,
                                                        uint32_t uncertain = 0U) noexcept {
  const auto drive_low = static_cast<uint16_t>(c.drive_strength_codes & 0xFFFFU);
  const auto drive_high = static_cast<uint16_t>(c.drive_strength_codes >> 16U);
  const auto drive_reset = static_cast<uint16_t>(reg::kDriveStrengthReset & 0xFFFFU);
  const std::array<ResetSentinel, 10> candidates{{
      Candidate(reg::kInterruptMask0, c.interrupt_mask, reg::kInterruptMaskReset),
      Candidate(reg::kPullEnable0, c.pull_enable_mask, reg::kPullEnableReset),
      Candidate(reg::kPullSelect0, c.pull_up_mask, reg::kPullSelectReset),
      Candidate(reg::kInputLatch0, c.input_latch_mask, reg::kInputLatchReset),
      Candidate(reg::kDriveStrength0, drive_low, drive_reset),
      Candidate(reg::kDriveStrength2, drive_high, drive_reset),
      Candidate(reg::kPolarityPort0, c.polarity_invert_mask, reg::kPolarityReset),
      Candidate(reg::kOutputPortConfig, OutputPortConfig(c), reg::kOutputPortConfigReset, false),
      Candidate(reg::kConfigPort0, c.input_mask, reg::kConfigReset),
      Candidate(reg::kOutputPort0, c.output_high_mask, reg::kOutputReset),
  }};
  ResetSentinel found{.reg = kNoSentinel, .value = 0U, .reset = 0U, .pair = true, .only = true};
  for (const ResetSentinel& candidate : candidates) {
    if (!reg::IsImplemented(candidate.reg, agile) || (candidate.value == candidate.reset) ||
        ((uncertain & UncertainBit(candidate.reg)) != 0U)) {
      continue; // an unknown register proves nothing either way
    }
    if (found.reg != kNoSentinel) {
      found.only = false;
      break;
    }
    found = candidate;
  }
  if (uncertain != 0U) {
    found.only = false; // an unknown register may differ too
  }
  return found;
}

// The power-up configuration encodes to the power-up register values (table 4), and the helpers
// put each field where the register map says (KB-CPP20-012).
static_assert((StandardPairs(Config{})[0].value == reg::kOutputReset) &&
              (StandardPairs(Config{})[1].value == reg::kPolarityReset) &&
              (StandardPairs(Config{})[2].value == reg::kConfigReset));
static_assert((AgilePairs(Config{})[0].value == 0xFFFFU) &&
              (AgilePairs(Config{})[1].value == 0xFFFFU) &&
              (AgilePairs(Config{})[2].value == reg::kInputLatchReset) &&
              (AgilePairs(Config{})[3].value == reg::kPullSelectReset) &&
              (AgilePairs(Config{})[4].value == reg::kPullEnableReset) &&
              (AgilePairs(Config{})[5].value == reg::kInterruptMaskReset));
static_assert(OutputPortConfig(Config{}) == reg::kOutputPortConfigReset);
static_assert(DriveStrengthAll(DriveStrength::Full) == reg::kDriveStrengthReset);
static_assert(ApplyDrive(0_u32, PinMask(15_u8), DriveStrength::Full) == 0xC000'0000U);
static_assert(ApplyDrive(0xFFFF'FFFF_u32, PinMask(0_u8), DriveStrength::Quarter) == 0xFFFF'FFFCU);
static_assert(PairValueOf(std::array<uint8_t, 2>{0x34, 0x12}) == 0x1234U);
static_assert((UncertainBit(reg::kOutputPort0) == 0x0002U) &&
              (UncertainBit(reg::kConfigPort0) == 0x0008U) &&
              (UncertainBit(reg::kDriveStrength0) == 0x0010U) &&
              (UncertainBit(reg::kInterruptMask0) == 0x0200U) &&
              (UncertainBit(reg::kOutputPortConfig) == kAllUncertain - 0x03FEU));
static_assert(StrongerDrive(0x0000'00F0_u32, 0x0000'003C_u32) == 0x0000'00FCU);
static_assert((RegisterValue(Config{}, reg::kConfigPort0) == reg::kConfigReset) &&
              (RegisterValue(Config{}, reg::kDriveStrength2) == 0xFFFFU) &&
              (RegisterValue(Config{}, reg::kOutputPortConfig) == reg::kOutputPortConfigReset));
static_assert((WithByte(0x1234_u16, 1_u32, 0xAB_u8) == 0xAB34U) &&
              (WithByte(0x1234_u16, 0_u32, 0xAB_u8) == 0x12ABU));
static_assert((FindResetSentinel(Config{.output_high_mask = 0U, .input_mask = 0U}, false).reg ==
               reg::kConfigPort0) &&
              !FindResetSentinel(Config{.output_high_mask = 0U, .input_mask = 0U}, false).only);
static_assert((FindResetSentinel(Config{}, true).reg == kNoSentinel) &&
              (FindResetSentinel(
                   Config{.variant = Variant::Pcal9555a, .port1_mode = OutputMode::OpenDrain}, true)
                   .reg == reg::kOutputPortConfig));
static_assert((Apply(0x00F0_u16, 0x0F0F_u16, true) == 0x0FFFU) &&
              (Apply(0xFFFF_u16, 0x0F0F_u16, false) == 0xF0F0U));

} // namespace detail

/**
 * @defgroup pcal9555_driver PCAL9555 driver
 * @brief The driver class.
 * @{
 */

// clang-format off: the hook table below must keep one row per line.
/**
 * @brief Driver for one PCA9555 or PCAL9555A on a platform port.
 *
 * Lifecycle: construct with the configuration (no I/O), `EnsureInitialized()` (bus up, presence
 * check, every register written and read back), use, optionally `Deinitialize()` (every pin back
 * to an input). Every call that touches the chip returns `Error::NotInitialized` until
 * `EnsureInitialized()` has succeeded; no call starts the chip implicitly.
 *
 * Pins are numbered 0 to 15: 0 to 7 are P0_0 to P0_7, 8 to 15 are P1_0 to P1_7. Calls that change
 * pins take a mask (bit n = pin n, `PinMask()` builds one); a mask of 0 is `InvalidArgument`.
 * The driver keeps a copy of every register it wrote (`GetConfig()`): a pin call computes the new
 * register pair from that copy and writes it in one transfer, without reading it first. Nothing is
 * read back unless the call's last argument is `Verify::ReadBack` (`Verify::None` by default);
 * only then can a pin call return `WriteMismatch`.
 *
 * Every call listed below as returning `Bus` returns `NoDevice` instead when the port's optional
 * `GetLastFailure()` hook reports that nothing acknowledged the address (the chip was removed or
 * lost power). The chip NACKs no transfer the driver sends by design (handbook KB-CPP-130).
 *
 * Neither part has a reset pin or a reset flag, but a supply dip below the power-on reset voltage
 * reloads every register with its power-up value (section 6.4, table 4). The driver finds that by
 * reading one register whose copy differs from its power-up value (`detail::FindResetSentinel()`):
 * in `CheckForReset()`, `SetDirection()`, `ApplyConfig()`, and in a setter about to rewrite that
 * register (so a setter returns `DeviceReset` only when it would have rewritten the evidence;
 * the others send their one write as before). A call that finds it writes nothing, marks the
 * driver uninitialised and returns `DeviceReset`; `EnsureInitialized()` then writes the copy
 * again in the datasheet's order.
 *
 * A write whose transfer failed with `Bus` may still have reached the chip (a NACK or an ACK lost
 * after the bytes were clocked in). The copy then takes the riskier of the old and the asked value
 * (`detail::Riskier()`: an output rather than an input, the written level, the stronger drive,
 * push-pull) and the register is unknown: the next call that builds on it reads it into the copy
 * first (one more read), a successful write or a read-back call makes it known again, and the
 * reset check never reads it. `NoDevice`, an address NACK, proves nothing arrived: the copy stays.
 *
 * The Agile I/O calls (drive strength, pull resistors, input latch, interrupt mask and status,
 * output mode) need `Config::variant == Variant::Pcal9555a`; on a driver configured for the PCA9555
 * they return `Unsupported` without I/O.
 *
 * Optional port hooks, found at compile time:
 *
 * | Hook | Used for | Without it |
 * | --- | --- | --- |
 * | `void Log(LogLevel, const char* message) noexcept` | One line per lifecycle event or failure | Nothing is logged |
 * | `bool GetLastFailure(I2cFailure& failure) noexcept` | Telling an address NACK (`NoDevice`) from other bus failures | Every failed transfer is `Bus` |
 *
 * A hook declared with the wrong signature (for example `void GetLastFailure(...)`) is a compile
 * error. The chip needs no wait between transfers, so the port has no `DelayUs()` hook, and it has
 * no reset pin, so there is no `GpioSet()` hook.
 *
 * @tparam Port Your class derived from `I2cInterface<Port>`.
 */
// clang-format on
template <I2cPort Port>
class PCAL9555 {
  static_assert(!detail::DeclaresLog<Port> || detail::HasLog<Port>,
                "pcal9555: the optional Log hook must be `void Log(LogLevel, const char* message) "
                "noexcept`");
  static_assert(!detail::DeclaresGetLastFailure<Port> || detail::HasGetLastFailure<Port>,
                "pcal9555: the optional GetLastFailure hook must be `bool "
                "GetLastFailure(I2cFailure& failure) noexcept`");

public:
  /**
   * @brief Bind the driver to a port, a 7-bit I2C address and a configuration. No I/O, no checks:
   * `EnsureInitialized()` validates both.
   * @param port The port; it must outlive the driver.
   * @param address 7-bit I2C address, 0x20 to 0x27 (`kI2cAddress` plus the address pins,
   *        `AddressFromPins()`); `EnsureInitialized()` rejects any other with `InvalidArgument`.
   * @param config What `EnsureInitialized()` writes.
   */
  explicit PCAL9555(Port& port, uint8_t address = kI2cAddress, const Config& config = {}) noexcept
      : bus_(port, address), config_(config) {}

  /** @brief One driver object per chip. */
  PCAL9555(const PCAL9555&) = delete;
  /** @brief One driver object per chip. */
  PCAL9555& operator=(const PCAL9555&) = delete;

  /// @name Lifecycle
  /// @{

  /**
   * @brief Start the chip on the first call; `Ok` at once, with no I/O, once it has succeeded.
   *
   * Checks the stored configuration with `Validate()` and the address, brings the bus up, checks
   * that the part answers, writes every register of the configuration and reads each back. The
   * chip has no reset pin and no software reset, so instead of a reset every register the driver
   * owns is written, which also undoes whatever a previous run left behind. The presence check
   * reads the input ports, which also clears an interrupt left pending from before the start (an
   * address NACK is `NoDevice` with a port that has `GetLastFailure()`, otherwise `Bus`); on a
   * PCAL9555A it also reads the output port
   * configuration register, whose reserved bits 7..2 read 0. The identity check then writes
   * `reg::kIdentityPattern` to the polarity inversion pair and reads it back, which no floating,
   * stuck or deaf line can echo, before any configured value is written; the read-back of every
   * register after the writes confirms it: a register that does not hold what was written means
   * another part answers. After a failure the driver stays uninitialised and the next call starts
   * again from the beginning.
   *
   * @return `Ok`, `InvalidArgument` (the configuration or the address; no I/O), `Unsupported`
   *         (an Agile I/O field for a PCA9555; no I/O), `NoDevice`, `Bus` or `DeviceIdMismatch`.
   */
  Result<void> EnsureInitialized() noexcept;

  /** @return true after a successful `EnsureInitialized()` and before `Deinitialize()`. No I/O. */
  [[nodiscard]] bool IsInitialized() const noexcept {
    return initialized_;
  }

  /**
   * @brief Make every pin a high-impedance input (the power-up direction), the safe state, and
   * mark the driver uninitialised.
   *
   * All inputs is the power-up direction: nothing is driven, the PCAL9555A pull resistors stay as
   * configured. The driver is uninitialised afterwards even if the write failed. `GetConfig()`
   * keeps the configuration (with the pin changes made since), so the next `EnsureInitialized()`
   * writes it again. On an uninitialised driver it does nothing and returns `Ok`.
   *
   * @return `Ok` or `Bus`.
   */
  Result<void> Deinitialize() noexcept;

  /// @}
  /// @name Configuration
  /// @{

  /**
   * @brief Replace the configuration. When initialised, check for a reset (one read, see
   * `CheckForReset()`), then write every register and read each back; otherwise only store it for
   * the next `EnsureInitialized()` (no I/O).
   * @param config Checked with `Validate()` before any I/O. Once initialised its `variant` must
   *        stay the same (`Deinitialize()` first to change it).
   * @return `Ok`, `InvalidArgument` (including a changed `variant`), `Unsupported`, `Bus`,
   *         `DeviceReset` (nothing written, the configuration not stored, the driver
   *         uninitialised) or `WriteMismatch` (a register did not keep its value; the full
   *         configuration is always read back).
   */
  Result<void> ApplyConfig(const Config& config) noexcept;

  /**
   * @return The configuration stored or last applied, updated by every successful pin call: the
   * driver's copy of the chip's registers. No I/O.
   *
   * After a write that failed with `Bus` the register is unknown: the chip may hold the old value,
   * the written one or (a pair) one byte of each. The copy then shows the riskier of old and
   * written for the direction (an output), the drive strength (the stronger) and 0x4F
   * (push-pull), and the written value for every other field, the output levels included: not
   * necessarily what the pins do. The driver reads an unknown register from the chip before it
   * builds on it; to see the chip now, call `ReadOutputLatch()`, `ReadInputMask()` or
   * `ReadPolarityInversion()`, which also make the register known again.
   */
  [[nodiscard]] const Config& GetConfig() const& noexcept {
    return config_;
  }
  /** @brief Not on a temporary driver: the reference would dangle (handbook KB-CPP20-044). */
  void GetConfig() const&& = delete;

  /** @return The 7-bit I2C address passed to the constructor. No I/O. */
  [[nodiscard]] uint8_t GetAddress() const noexcept {
    return bus_.GetAddress();
  }

  /**
   * @brief Find out whether the chip went through a power-on reset since the start.
   *
   * Neither part has a reset flag (section 6.4). The driver reads one register whose copy differs
   * from its power-up value (table 4), preferring the Agile I/O pairs and the polarity pair, then
   * 0x4F, the configuration pair and the output pair (`detail::FindResetSentinel()`); if it reads
   * its power-up value, the chip reset. When every register of the copy is at its power-up value
   * nothing is sent: a reset then leaves the chip as the driver believes it to be. Call it where a
   * supply dip is possible, for example once per control period or before a critical output change:
   * one 2-byte read (one 1-byte read when 0x4F is the register), 113 µs at 400 kHz. A register
   * whose last write failed is never the one read; when no other is left, the check reads the
   * unknown ones instead (one read each), and one back at its power-up value is a reset. When the
   * failed write's old or new value was itself the power-up value, that is a false `DeviceReset`:
   * one time, recovered by `EnsureInitialized()`.
   *
   * A write to that register that the chip acknowledged but dropped reads the same way; the
   * recovery is the same. `SetDirection()`, `ApplyConfig()` and a setter about to rewrite the
   * checked register run the same check first.
   *
   * @return `Ok`, `NotInitialized`, `Bus` or `DeviceReset` (the driver is now uninitialised: call
   *         `EnsureInitialized()`, which writes every register again in the datasheet's order).
   */
  Result<void> CheckForReset() noexcept;

  /// @}
  /// @name Pins (PCA9555 and PCAL9555A)
  /// @{

  /**
   * @brief Set the direction of the pins in `mask` (configuration registers).
   *
   * To avoid a glitch, set the output level with `SetOutputs()` before making a pin an output.
   *
   * A pin that becomes an output drives its output latch through its port's output stage at once,
   * and a power-on reset sets the latch high and the stage push-pull (table 4). So the call first
   * checks for a reset (`CheckForReset()`, one read) and then writes, from the driver's copy, in
   * the datasheet's order (section 6.2.12): 0x4F (PCAL9555A), the output pair, the configuration
   * pair. When pins become outputs, even a reset that the check missed makes no pin an output at
   * the reset latch level or stage. The one exception is a partial release (below): a reset in the
   * microseconds between its check and its configuration write leaves the pins that stay outputs
   * at the power-up level until the 0x4F and output writes after it land. Cost: one read and two
   * write pairs on a PCA9555, one read, one 1-byte write and two write pairs on a PCAL9555A (no
   * read when the copy is all power-up values).
   *
   * Releasing pins (`Direction::Input`) is the safe direction. When every pin ends up an input, the
   * configuration pair is the only transfer, with no check: nothing can be driven. When other pins
   * stay outputs: the check, then the configuration pair first (the pins are released by the
   * first write), then 0x4F and the output pair for the remaining outputs.
   *
   * @param mask Pins to change; not 0.
   * @param direction The direction.
   * @param verify `Verify::ReadBack` to read the configuration pair back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Bus`, `DeviceReset` (nothing written) or
   *         `WriteMismatch`.
   */
  Result<void> SetDirection(uint16_t mask, Direction direction,
                            Verify verify = Verify::None) noexcept;

  /**
   * @brief Drive the pins in `mask` high or low (output port registers). Pins configured as inputs
   * keep the value for when they become outputs.
   * @param mask Pins to change; not 0.
   * @param high true for high.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Bus`, `DeviceReset` or `WriteMismatch`.
   */
  Result<void> SetOutputs(uint16_t mask, bool high, Verify verify = Verify::None) noexcept;

  /**
   * @brief Write both output port registers at once.
   * @param high_mask Bit n = 1 drives pin n high.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `Bus`, `DeviceReset` or `WriteMismatch`.
   */
  Result<void> WriteOutputs(uint16_t high_mask, Verify verify = Verify::None) noexcept;

  /**
   * @brief Invert the output value of the pins in `mask`.
   *
   * The new value is computed from `GetConfig()`. After `WriteMismatch` the copy already holds the
   * toggled value, so do not retry with `ToggleOutputs()` (it would toggle back): resend with
   * `WriteOutputs(GetConfig().output_high_mask)`.
   *
   * @param mask Pins to toggle; not 0.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Bus`, `DeviceReset` or `WriteMismatch`.
   */
  Result<void> ToggleOutputs(uint16_t mask, Verify verify = Verify::None) noexcept;

  /**
   * @brief Invert (or stop inverting) how the input pins in `mask` read (polarity inversion).
   * @param mask Pins to change; not 0.
   * @param invert true to invert.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `InvalidArgument`, `Bus`, `DeviceReset` or `WriteMismatch`.
   */
  Result<void> SetPolarityInversion(uint16_t mask, bool invert,
                                    Verify verify = Verify::None) noexcept;

  /**
   * @brief Read both input ports: the level of every pin, inputs and outputs, after polarity
   * inversion. Reading clears the interrupt of both ports.
   *
   * Reading an input port clears its interrupt and its interrupt status (sections 6.2.11, 6.5). So
   * on a PCAL9555A with an unmasked input (`input_mask & ~interrupt_mask` not 0) the call first
   * reads the interrupt status pair into `GetLatchedInterrupts()`: one more 2-byte read. If that
   * read fails, the inputs are not read and nothing is cleared.
   *
   * @return Bit n = pin n, or `NotInitialized` / `Bus`.
   */
  Result<uint16_t> ReadInputs() noexcept;

  /**
   * @brief Read the level of one pin. Reads its port's input register only, which clears that
   * port's interrupt. With an unmasked input on a PCAL9555A it first reads the interrupt status
   * pair into `GetLatchedInterrupts()`, as `ReadInputs()` does.
   * @param pin 0 to 15.
   * @return The level (true = high), or `NotInitialized`, `InvalidArgument` or `Bus`.
   */
  Result<bool> ReadPin(uint8_t pin) noexcept;

  /// @}
  /// @name Agile I/O (PCAL9555A only)
  /// @{

  /**
   * @brief Set the output drive strength of the pins in `mask`. Writes both drive strength pairs.
   * @param mask Pins to change; not 0.
   * @param level The drive strength.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `Unsupported`, `InvalidArgument`, `Bus`, `DeviceReset` or
   * `WriteMismatch`.
   */
  Result<void> SetDriveStrength(uint16_t mask, DriveStrength level,
                                Verify verify = Verify::None) noexcept;

  /**
   * @brief Connect a pull-up or pull-down to the pins in `mask`, or disconnect it. Writes the
   * selection pair, then the enable pair. A port in open-drain mode disconnects its resistors
   * whatever this says.
   * @param mask Pins to change; not 0.
   * @param pull The resistor.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `Unsupported`, `InvalidArgument`, `Bus`, `DeviceReset` or
   * `WriteMismatch`.
   */
  Result<void> SetPull(uint16_t mask, Pull pull, Verify verify = Verify::None) noexcept;

  /**
   * @brief Latch (or stop latching) input changes of the pins in `mask`: a latched pin keeps the
   * level that raised the interrupt until the input port is read.
   * @param mask Pins to change; not 0.
   * @param latch true to latch.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `Unsupported`, `InvalidArgument`, `Bus`, `DeviceReset` or
   * `WriteMismatch`.
   */
  Result<void> SetInputLatch(uint16_t mask, bool latch, Verify verify = Verify::None) noexcept;

  /**
   * @brief Let the input pins in `mask` raise INT on a change, or mask them (interrupt mask
   * registers; every pin is masked at power-up).
   * @param mask Pins to change; not 0.
   * @param enable true to unmask.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `Unsupported`, `InvalidArgument`, `Bus`, `DeviceReset` or
   * `WriteMismatch`.
   */
  Result<void> SetInterruptEnabled(uint16_t mask, bool enable,
                                   Verify verify = Verify::None) noexcept;

  /**
   * @brief Read which pins raised the interrupt (interrupt status registers). Masked pins read 0.
   * Reading the status does not clear the interrupt; reading the input ports does.
   * @return Bit n = pin n, or `NotInitialized`, `Unsupported` or `Bus`.
   */
  Result<uint16_t> ReadInterruptStatus() noexcept;

  /**
   * @brief Select push-pull or open-drain outputs for a whole port (output port configuration).
   * @param bank The port.
   * @param mode The output stage.
   * @param verify `Verify::ReadBack` to read the written register back (one more read); a value
   *        that is not an enumerator is `InvalidArgument`.
   * @return `Ok`, `NotInitialized`, `Unsupported`, `InvalidArgument` (not an enumerator), `Bus`,
   * `DeviceReset` or `WriteMismatch`.
   */
  Result<void> SetOutputMode(Bank bank, OutputMode mode, Verify verify = Verify::None) noexcept;
  /// @}
  /// @name Read-back: what the chip holds, as opposed to `GetConfig()`, what was asked for
  /// @{

  /**
   * @brief Read both output port registers: the level each pin is told to drive.
   *
   * For an output pin this is the truth `ReadInputs()` cannot give: on a loaded or shared line the
   * pin level may differ from the latch. Compare it with `GetConfig().output_high_mask` to find a
   * lost write.
   *
   * @return Bit n = 1 when pin n is set to drive high, or `NotInitialized` / `Bus`.
   */
  Result<uint16_t> ReadOutputLatch() noexcept;

  /**
   * @brief Read both configuration registers.
   * @return Bit n = 1 when pin n is an input, or `NotInitialized` / `Bus`.
   */
  Result<uint16_t> ReadInputMask() noexcept;

  /**
   * @brief Read both polarity inversion registers.
   * @return Bit n = 1 when input n reads inverted, or `NotInitialized` / `Bus`.
   */
  Result<uint16_t> ReadPolarityInversion() noexcept;

  /// @}
  /// @name Diagnostics: raw access, the last values read and the interrupt latch
  /// @{

  /**
   * @brief Read one register, once: one 1-byte write-read, no retry, no check of the value
   * (handbook KB-CPP-129). For bring-up and diagnostics.
   *
   * Side effects are the chip's own: reading an input port (0x00, 0x01) clears that port's
   * interrupt (section 6.5), without the status read `ReadInputs()` does first. The value also
   * updates `GetLastInputs()`, `GetLastOutput()` or the
   * interrupt latch, as the same register read by any other call would.
   *
   * @param command The command byte: 0x00 to 0x07, and on a driver configured for the PCAL9555A
   *        also 0x40 to 0x4D and 0x4F (`reg::IsImplemented()`). Any other is `InvalidArgument`
   *        with no I/O: the PCA9555 data sheet does not say how the part answers it.
   * @return The register, or `NotInitialized`, `InvalidArgument`, `NoDevice` or `Bus`.
   */
  Result<uint8_t> ReadRegister(uint8_t command) noexcept;

  /**
   * @return The input ports as the driver last read them (`ReadInputs()`, `ReadPin()`, a raw read
   * or the start-up presence check), bit n = pin n; 0 before the first read. No I/O.
   */
  [[nodiscard]] uint16_t GetLastInputs() const noexcept {
    return last_inputs_;
  }

  /**
   * @return The output port registers as the chip last reported them (`ReadOutputLatch()`, a
   * `Verify::ReadBack` of an output write, the start-up read-back or a raw read), bit n = 1 when
   * pin n is set to drive high. Compare it with `GetConfig().output_high_mask`, what was asked for,
   * to see a lost write without I/O. The power-up value 0xFFFF before the first read. No I/O.
   */
  [[nodiscard]] uint16_t GetLastOutput() const noexcept {
    return last_output_;
  }

  /**
   * @brief The pins any read of the interrupt status registers has reported since
   * `ClearLatchedInterrupts()`, ORed together (handbook KB-CPP-119). No I/O.
   *
   * Reading the input ports clears the chip's interrupt and its status (section 6.5), so a status
   * the driver has read is kept here until the caller clears it. PCAL9555A only; always 0 on a
   * driver configured for the PCA9555, which has no status register.
   *
   * @return Bit n = pin n raised the interrupt.
   */
  [[nodiscard]] uint16_t GetLatchedInterrupts() const noexcept {
    return latched_interrupts_;
  }

  /** @brief Empty the latch of `GetLatchedInterrupts()`. No I/O; the chip is not touched. */
  void ClearLatchedInterrupts() noexcept {
    latched_interrupts_ = 0U;
  }

  /// @}

private:
  Result<void> initialize() noexcept;
  Result<void> checkPresence() noexcept;
  Result<void> writeAll(const Config& c, uint8_t& failed) noexcept;
  void noteApplyFailure(const Config& asked, uint8_t failed, Error error) noexcept;
  Result<void> resolveAll() noexcept;
  Result<void> checkUnknown() noexcept;
  Result<void> writeStage() noexcept;
  void forget(uint8_t reg) noexcept;
  Result<void> verifyAll(const Config& c) noexcept;
  Result<void> checkCall(uint16_t mask, bool agile, Verify verify = Verify::None) const noexcept;
  Result<void> storeMask(uint8_t reg, uint16_t Config::* field, uint16_t value,
                         Verify verify) noexcept;
  Result<void> checkReset() noexcept;
  Result<void> guardWrite(uint8_t first, uint8_t second) noexcept;
  Result<void> noteInterrupts() noexcept;
  Result<void> prepare(uint8_t first, uint8_t second) noexcept;
  Result<void> resolve(uint8_t reg) noexcept;
  void markFailed(const Config& asked, uint8_t reg, Error error) noexcept;
  [[nodiscard]] bool isUncertain(uint8_t reg) const noexcept {
    return (static_cast<uint32_t>(uncertain_) & detail::UncertainBit(reg)) != 0U;
  }
  Result<void> writeDrive(uint32_t codes, Verify verify) noexcept;
  Result<void> storeDrive(uint8_t reg, uint32_t codes, uint16_t value, Verify verify) noexcept;
  Result<uint16_t> readPair(uint8_t reg) noexcept;
  Result<uint8_t> readByte(uint8_t reg) noexcept;
  Result<void> writeByte(uint8_t reg, uint8_t value) noexcept;
  void notePair(uint8_t reg, uint16_t value) noexcept;
  void noteByte(uint8_t reg, uint8_t value) noexcept;
  Result<void> writePair(uint8_t reg, uint16_t value) noexcept;
  Result<void> writePairChecked(uint8_t reg, uint16_t value, Verify verify) noexcept;
  Result<uint16_t> readStarted(uint8_t reg) noexcept;
  template <std::size_t N>
  Result<void> verifyPairs(const std::array<detail::PairValue, N>& pairs) noexcept;
  Result<void> fail(Error error, const char* message) noexcept;

  [[nodiscard]] bool isAgile() const noexcept {
    return config_.variant == Variant::Pcal9555a;
  }

  detail::RegisterBus<Port> bus_;
  Config config_{};
  uint16_t last_inputs_{0U};                ///< GetLastInputs().
  uint16_t last_output_{reg::kOutputReset}; ///< GetLastOutput().
  uint16_t latched_interrupts_{0U};         ///< GetLatchedInterrupts().
  uint16_t uncertain_{0U}; ///< Registers whose last write failed (bits of UncertainBit()).
  bool initialized_{false};
};

/** @} */

} // namespace pcal9555

// codeql::misra_deviation_next_line(MD-CPP-009)
#include "../src/pcal9555.ipp" // IWYU pragma: export

#endif // PCAL9555_HPP
