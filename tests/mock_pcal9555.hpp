/**
 * @file mock_pcal9555.hpp
 * @brief A protocol model of the PCA9555 / PCAL9555A for the host tests, plus the mock ports in
 * mock_pcal9555_bus.hpp.
 *
 * The model works at the level of I2C transactions, as the data sheets describe them: the command
 * byte sets the pointer, and each data byte moves the pointer to the other register of the pair
 * (section 7); a command byte the part does not implement is NACKed, or (fault model) aliased to
 * its low three bits. Behind the registers sit 16 pins: outputs drive their output port bit
 * (open-drain outputs only pull low), inputs follow an external level that a test sets. A change
 * on an input raises the interrupt (masked by the PCAL9555A interrupt mask), sets the interrupt
 * status bit, and is cleared by reading the input port of that pin, or by the pin returning to
 * the level of the last read unless it is latched (input latch register, section 6.2.7).
 *
 * The chip also changes state on its own: a supply dip below the power-on reset voltage reloads
 * every register with its power-up value in the middle of a session (section 6.4, table 4; the
 * parts have no reset pin), which `reset_after` schedules and `PowerOn()` does at once.
 *
 * The model is written from the data sheets on its own (handbook KB-TST-030): it does not include
 * the driver's `pcal9555_registers.hpp` or `pcal9555_types.hpp`, but carries its own register map
 * and power-up values below (namespace `chip`, each with its data sheet section), so a wrong
 * constant in the driver cannot hide behind the same wrong constant in the model.
 * tests/test_model.cpp checks the two maps against each other at compile time.
 */
#ifndef PCAL9555_MOCK_PCAL9555_HPP
#define PCAL9555_MOCK_PCAL9555_HPP

#include <array>
#include <cstddef>
#include <cstdint>

namespace pcal9555_test {

/// The chip as the data sheets describe it (PCAL9555A Rev. 2, PCA9555 Rev. 10), independent of
/// the driver's headers.
namespace chip {
// Command bytes (PCAL9555A section 6.2.1, table 4; PCA9555 section 6.2.1, table 4).
inline constexpr uint8_t kInputPort0 = 0x00;     // 0x01 port 1, read only
inline constexpr uint8_t kOutputPort0 = 0x02;    // 0x03 port 1
inline constexpr uint8_t kPolarityPort0 = 0x04;  // 0x05 port 1
inline constexpr uint8_t kConfigPort0 = 0x06;    // 0x07 port 1; 1 = input
inline constexpr uint8_t kLastStandard = 0x07;   // the PCA9555 ends here
inline constexpr uint8_t kDriveStrength0 = 0x40; // 0x41; 0x42, 0x43 for port 1 (6.2.6)
inline constexpr uint8_t kDriveStrength2 = 0x42;
inline constexpr uint8_t kInputLatch0 = 0x44;          // 6.2.7
inline constexpr uint8_t kPullEnable0 = 0x46;          // 6.2.8
inline constexpr uint8_t kPullSelect0 = 0x48;          // 6.2.9; 1 = pull-up
inline constexpr uint8_t kInterruptMask0 = 0x4A;       // 6.2.10; 1 = masked
inline constexpr uint8_t kInterruptStatus0 = 0x4C;     // 6.2.11; read only
inline constexpr uint8_t kOutputPortConfig = 0x4F;     // 6.2.12
inline constexpr uint8_t kOden0 = 0x01;                // 0x4F bit 0: port 0 open-drain
inline constexpr uint8_t kOden1 = 0x02;                // 0x4F bit 1: port 1 open-drain
inline constexpr uint8_t kOutputConfigReserved = 0xFC; // 0x4F bits 7..2 (table 27)
// Power-up values (PCAL9555A table 4).
inline constexpr uint16_t kOutputReset = 0xFFFF;
inline constexpr uint16_t kPolarityReset = 0x0000;
inline constexpr uint16_t kConfigReset = 0xFFFF;
inline constexpr uint16_t kDriveStrengthReset = 0xFFFF; // each of the two pairs
inline constexpr uint16_t kInputLatchReset = 0x0000;
inline constexpr uint16_t kPullEnableReset = 0xFFFF;
inline constexpr uint16_t kPullSelectReset = 0xFFFF;
inline constexpr uint16_t kInterruptMaskReset = 0xFFFF;
inline constexpr uint8_t kOutputPortConfigReset = 0x00;
// The bus (section 6.1, figure 4; section 13, table 33).
inline constexpr uint8_t kBaseAddress = 0x20; // 0100 A2 A1 A0
inline constexpr uint32_t kMaxClockHz = 400'000U;
} // namespace chip

class Pcal9555Model {
public:
  /// Which part the model is.
  enum class Part : uint8_t {
    Pca9555,  ///< Command bytes 0x00 to 0x07 only.
    Pcal9555a ///< Also the Agile I/O bank.
  };

  /// How the part answers a command byte it does not implement.
  enum class UnknownCommand : uint8_t {
    Nack,    ///< NACK the command byte (the assumption the driver is documented with).
    AliasLow ///< Use only the low three bits (a fault model: a PCA9555 that ignores bit 6).
  };

  // --- What the chip is --------------------------------------------------------------------------
  Part variant = Part::Pcal9555a;                        ///< The part.
  UnknownCommand unknown_command = UnknownCommand::Nack; ///< Answer to an unknown command.

  // --- Faults to inject --------------------------------------------------------------------------
  int stuck_register = -1;   ///< Writes to this command byte are dropped; -1 none.
  int drop_writes = 0;       ///< The next N data bytes are acknowledged but lost (a noisy bus).
  uint8_t reserved_bits = 0; ///< OR-ed into reads of 0x4F (a part that is not a PCAL9555A).
  bool deaf = false;         ///< A device that acknowledges everything and stores nothing.
  uint8_t deaf_value = 0xFF; ///< What a deaf device reads.
  /// A brown-out: after this many more I2C transactions the part goes through a power-on reset on
  /// its own (section 6.4), before the next transaction reaches it; -1 never.
  int reset_after = -1;
  int resets = 0; ///< Power-on resets `reset_after` has caused.

  Pcal9555Model() noexcept {
    PowerOn();
  }

  /// Power-on: every register at its default (table 4), no interrupt pending.
  void PowerOn() noexcept {
    regs_.fill(0);
    setPair(chip::kOutputPort0, chip::kOutputReset);
    setPair(chip::kPolarityPort0, chip::kPolarityReset);
    setPair(chip::kConfigPort0, chip::kConfigReset);
    setPair(chip::kDriveStrength0, chip::kDriveStrengthReset);
    setPair(chip::kDriveStrength2, chip::kDriveStrengthReset);
    setPair(chip::kInputLatch0, chip::kInputLatchReset);
    setPair(chip::kPullEnable0, chip::kPullEnableReset);
    setPair(chip::kPullSelect0, chip::kPullSelectReset);
    setPair(chip::kInterruptMask0, chip::kInterruptMaskReset);
    regs_[chip::kOutputPortConfig] = chip::kOutputPortConfigReset;
    pending_ = 0;
    reference_ = Levels();
    latched_ = 0;
    pointer_ = 0;
  }

  /// An I2C write: command byte, then data. Returns false for a NACK.
  bool Write(const uint8_t* data, std::size_t len) noexcept {
    tick();
    if (!setPointer(data[0])) {
      return false;
    }
    for (std::size_t i = 1; i < len; ++i) {
      store(pointer_, data[i]);
      pointer_ = next(pointer_);
    }
    return true;
  }

  /// An I2C write of the command byte, repeated START, then `len` bytes read.
  bool WriteRead(uint8_t command, uint8_t* rx, std::size_t len) noexcept {
    tick();
    if (!setPointer(command)) {
      return false;
    }
    for (std::size_t i = 0; i < len; ++i) {
      rx[i] = deaf ? deaf_value : load(pointer_);
      pointer_ = next(pointer_);
    }
    return true;
  }

  /// Set the level that external circuits drive on the pins (bit n = pin n).
  void SetExternal(uint16_t levels) noexcept {
    external_ = levels;
    update();
  }

  /// The level on every pin, as an oscilloscope would see it.
  [[nodiscard]] uint16_t Levels() const noexcept {
    return static_cast<uint16_t>(levels());
  }

  /// INT asserted (open-drain, active low on the pin).
  [[nodiscard]] bool InterruptAsserted() const noexcept {
    return (pending_ & ~effectiveMask()) != 0U;
  }

  /// A 16-bit register pair, for checks.
  [[nodiscard]] uint16_t Pair(uint8_t command) const noexcept {
    return pair(command);
  }
  /// One register, for checks.
  [[nodiscard]] uint8_t Register(uint8_t command) const noexcept {
    return regs_[command];
  }

  int writes = 0; ///< Data bytes written.

private:
  // One transaction reaches the chip: a scheduled power-on reset happens first.
  void tick() noexcept {
    if (reset_after < 0) {
      return;
    }
    if (reset_after == 0) {
      PowerOn();
      resets++;
    }
    reset_after--;
  }

  [[nodiscard]] bool isAgile() const noexcept {
    return variant == Part::Pcal9555a;
  }

  [[nodiscard]] static bool isAgileCommand(uint8_t command) noexcept {
    return (command >= chip::kDriveStrength0 && command <= chip::kInterruptStatus0 + 1) ||
           command == chip::kOutputPortConfig;
  }

  bool setPointer(uint8_t command) noexcept {
    if (deaf) {
      return true; // acknowledges any command byte
    }
    if (command <= chip::kLastStandard || (isAgile() && isAgileCommand(command))) {
      pointer_ = command;
      return true;
    }
    if (unknown_command == UnknownCommand::AliasLow) {
      pointer_ = static_cast<uint8_t>(static_cast<uint32_t>(command) & 0x07U);
      return true;
    }
    return false;
  }

  // The other register of the pair; 0x4F has no partner.
  [[nodiscard]] static uint8_t next(uint8_t pointer) noexcept {
    return pointer == chip::kOutputPortConfig
               ? pointer
               : static_cast<uint8_t>(static_cast<uint32_t>(pointer) ^ 1U);
  }

  void store(uint8_t command, uint8_t value) noexcept {
    writes++;
    if (deaf) {
      return;
    }
    if (drop_writes > 0) {
      drop_writes--;
      return;
    }
    if (command == static_cast<uint8_t>(stuck_register) || command <= chip::kInputPort0 + 1 ||
        command == chip::kInterruptStatus0 || command == chip::kInterruptStatus0 + 1) {
      return; // read-only, or the injected fault
    }
    if (command == chip::kOutputPortConfig) {
      value = static_cast<uint8_t>(static_cast<uint32_t>(value) &
                                   ~static_cast<uint32_t>(chip::kOutputConfigReserved));
    }
    regs_[command] = value;
    update();
  }

  uint8_t load(uint8_t command) noexcept {
    switch (command) {
    case chip::kInputPort0:
    case chip::kInputPort0 + 1:
      return readInput(command - chip::kInputPort0);
    case chip::kInterruptStatus0:
      return static_cast<uint8_t>(pending_ & ~effectiveMask() & 0xFFU);
    case chip::kInterruptStatus0 + 1:
      return static_cast<uint8_t>(((pending_ & ~effectiveMask()) >> 8U) & 0xFFU);
    case chip::kOutputPortConfig:
      return static_cast<uint8_t>(static_cast<uint32_t>(regs_[command]) |
                                  static_cast<uint32_t>(reserved_bits));
    default:
      return regs_[command];
    }
  }

  uint8_t readInput(int port) noexcept {
    const uint32_t port_mask = port == 0 ? 0x00FFU : 0xFF00U;
    const uint32_t level = levels();
    const uint32_t latch = isAgile() ? pair(chip::kInputLatch0) : 0U;
    const uint32_t use_latched = latch & pending_;
    uint32_t value = (level & ~use_latched) | (latched_ & use_latched);
    value ^= static_cast<uint32_t>(pair(chip::kPolarityPort0)) & pair(chip::kConfigPort0);
    // Reading the port clears its interrupt and starts a new comparison (section 6.5).
    pending_ &= ~port_mask;
    reference_ = (reference_ & ~port_mask) | (level & port_mask);
    return static_cast<uint8_t>(port == 0 ? value & 0xFFU : (value >> 8U) & 0xFFU);
  }

  // The level on every pin, bits 15..0.
  [[nodiscard]] uint32_t levels() const noexcept {
    const uint32_t inputs = pair(chip::kConfigPort0);
    const uint32_t out = pair(chip::kOutputPort0);
    const uint32_t oconf = regs_[chip::kOutputPortConfig];
    const uint32_t open_drain = ((oconf & chip::kOden0) != 0U ? 0x00FFU : 0U) |
                                ((oconf & chip::kOden1) != 0U ? 0xFF00U : 0U);
    // An open-drain output that is "high" lets the line float to the external level.
    const uint32_t driven = out & ~(open_drain & out);
    const uint32_t floating = inputs | (open_drain & out);
    return ((driven & ~floating) | (external_ & floating)) & 0xFFFFU;
  }

  void update() noexcept {
    const uint32_t inputs = pair(chip::kConfigPort0);
    const uint32_t level = levels();
    const uint32_t latch = isAgile() ? pair(chip::kInputLatch0) : 0U;
    const uint32_t changed = (level ^ reference_) & inputs;
    const uint32_t newly = changed & ~pending_;
    latched_ = (latched_ & ~newly) | (level & newly);
    // A non-latched input that returns to its old level clears its interrupt; a latched one stays.
    pending_ = ((pending_ & latch) | changed) & 0xFFFFU;
  }

  [[nodiscard]] uint32_t effectiveMask() const noexcept {
    return isAgile() ? pair(chip::kInterruptMask0) : 0U;
  }

  [[nodiscard]] uint16_t pair(uint8_t command) const noexcept {
    return static_cast<uint16_t>(static_cast<uint32_t>(regs_[command]) |
                                 (static_cast<uint32_t>(regs_[command + 1U]) << 8U));
  }
  void setPair(uint8_t command, uint16_t value) noexcept {
    regs_[command] = static_cast<uint8_t>(static_cast<uint32_t>(value) & 0xFFU);
    regs_[command + 1U] = static_cast<uint8_t>(static_cast<uint32_t>(value) >> 8U);
  }

  std::array<uint8_t, 0x50> regs_{};
  uint8_t pointer_ = 0;
  uint32_t external_ = 0xFFFFU; ///< Pulled up by default (the pull-ups are on at power-up).
  uint32_t pending_ = 0U;       ///< Inputs that changed since their port was last read.
  uint32_t reference_ = 0U;     ///< Pin levels at the last read of each port.
  uint32_t latched_ = 0U;       ///< Levels captured by the input latch.
};

} // namespace pcal9555_test

#include "mock_pcal9555_bus.hpp"

#endif // PCAL9555_MOCK_PCAL9555_HPP
