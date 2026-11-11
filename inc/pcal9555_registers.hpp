/**
 * @file pcal9555_registers.hpp
 * @brief PCA9555 / PCAL9555A register map: command bytes, fields and power-up defaults.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Section and table numbers refer to the NXP PCAL9555A data sheet, Rev. 2 (section 6.2, table 4)
 * and the NXP PCA9555 data sheet, Rev. 10 (section 6.2).
 *
 * Registers are 8 bits wide and come in pairs: port 0 at the even command byte, port 1 at the odd
 * one. After the command byte the chip toggles between the two registers of a pair on each data
 * byte (section 7); it does not step on to the next pair. The driver therefore moves at most two
 * bytes per transfer, port 0 first, and treats a pair as one 16-bit value with port 0 in bits
 * 7..0 (pins 0 to 7 are P0_0 to P0_7, pins 8 to 15 are P1_0 to P1_7).
 *
 * The PCA9555 has the registers 0x00 to 0x07 only. The PCAL9555A adds the Agile I/O bank at 0x40
 * to 0x4F (0x4E is not used).
 */
#ifndef PCAL9555_REGISTERS_HPP
#define PCAL9555_REGISTERS_HPP

#include <cstdint>

/** @brief The PCA9555 / PCAL9555A register map. */
namespace pcal9555::reg {

/**
 * @defgroup pcal9555_registers Register map
 * @brief Command bytes, fields and power-up defaults.
 * @{
 */

// Both parts: four register pairs.
inline constexpr uint8_t kInputPort0 = 0x00;    ///< Input port 0 (R): pin levels; 0x01 is port 1.
inline constexpr uint8_t kOutputPort0 = 0x02;   ///< Output port 0 (R/W); 0x03 is port 1.
inline constexpr uint8_t kPolarityPort0 = 0x04; ///< Polarity inversion 0 (R/W); 0x05 is port 1.
inline constexpr uint8_t kConfigPort0 = 0x06;   ///< Configuration 0 (R/W): 1 = input; 0x07 port 1.

// PCAL9555A only: the Agile I/O bank.
inline constexpr uint8_t kDriveStrength0 = 0x40; ///< Drive strength 0 (R/W): P0_0-3; 0x41 P0_4-7.
inline constexpr uint8_t kDriveStrength2 = 0x42; ///< Drive strength 2 (R/W): P1_0-3; 0x43 P1_4-7.
inline constexpr uint8_t kInputLatch0 = 0x44;    ///< Input latch 0 (R/W): 1 = latched; 0x45 port 1.
inline constexpr uint8_t kPullEnable0 = 0x46;    ///< Pull-up/pull-down enable 0 (R/W); 0x47 port 1.
inline constexpr uint8_t kPullSelect0 = 0x48; ///< Pull-up/pull-down selection 0 (R/W): 1 = pull-up.
inline constexpr uint8_t kInterruptMask0 = 0x4A;   ///< Interrupt mask 0 (R/W): 1 = masked.
inline constexpr uint8_t kInterruptStatus0 = 0x4C; ///< Interrupt status 0 (R); 0x4D is port 1.
inline constexpr uint8_t kOutputPortConfig = 0x4F; ///< Output port configuration (R/W).

inline constexpr uint8_t kOutputConfigOden0 = 1U << 0; ///< 0x4F ODEN0: port 0 open-drain.
inline constexpr uint8_t kOutputConfigOden1 = 1U << 1; ///< 0x4F ODEN1: port 1 open-drain.
/// Written to the polarity inversion pair at start-up and read back before the configuration is
/// written: alternating bits, so that neither a line stuck at 0 or 1 nor a device that acknowledges
/// everything and returns a fixed byte can echo it. Polarity only changes how inputs read.
inline constexpr uint16_t kIdentityPattern = 0x55AA;
/// 0x4F bits 7..2 are reserved and read 0 (PCAL9555A table 27).
inline constexpr uint8_t kOutputConfigReservedMask = 0xFC;

/// Bits per pin in the drive strength registers: pin n of a port uses bits 2(n%4)+1..2(n%4).
inline constexpr uint32_t kDriveBitsPerPin = 2U;
/// Mask of one pin's drive strength code.
inline constexpr uint32_t kDriveCodeMask = 0x3;

// Power-up defaults (PCAL9555A table 4; the PCA9555 has the same first three).
inline constexpr uint16_t kOutputReset = 0xFFFF;   ///< Output ports: every pin high.
inline constexpr uint16_t kPolarityReset = 0x0000; ///< Polarity inversion: none.
inline constexpr uint16_t kConfigReset = 0xFFFF;   ///< Configuration: every pin an input.
/// Drive strength: full on every pin (0x40 to 0x43 all 0xFF).
inline constexpr uint32_t kDriveStrengthReset = 0xFFFF'FFFFU;
inline constexpr uint16_t kInputLatchReset = 0x0000;    ///< Input latch: off.
inline constexpr uint16_t kPullEnableReset = 0xFFFF;    ///< Pull resistors: enabled.
inline constexpr uint16_t kPullSelectReset = 0xFFFF;    ///< Pull resistors: pull-up.
inline constexpr uint16_t kInterruptMaskReset = 0xFFFF; ///< Interrupts: every pin masked.
inline constexpr uint8_t kOutputPortConfigReset = 0x00; ///< Both ports push-pull.

/**
 * @brief Whether a part implements a command byte (table 4): 0x00 to 0x07 on both parts, and on
 * the PCAL9555A also 0x40 to 0x4D and 0x4F. The PCA9555 data sheet does not define its answer to
 * any other command byte, so the driver never sends one.
 * @param command The command byte.
 * @param agile `true` for the PCAL9555A.
 * @return `true` when the part has the register.
 */
[[nodiscard]] constexpr bool IsImplemented(uint8_t command, bool agile) noexcept {
  const auto c = static_cast<uint32_t>(command);
  if (c <= (static_cast<uint32_t>(kConfigPort0) + 1U)) {
    return true;
  }
  return agile && (((c >= static_cast<uint32_t>(kDriveStrength0)) &&
                    (c <= (static_cast<uint32_t>(kInterruptStatus0) + 1U))) ||
                   (c == static_cast<uint32_t>(kOutputPortConfig)));
}

static_assert(IsImplemented(uint8_t{0x07}, false) && !IsImplemented(kDriveStrength0, false));
static_assert(IsImplemented(uint8_t{0x4D}, true) && !IsImplemented(uint8_t{0x4E}, true) &&
              IsImplemented(kOutputPortConfig, true) && !IsImplemented(uint8_t{0x08}, true));

/** @} */

} // namespace pcal9555::reg

#endif // PCAL9555_REGISTERS_HPP
