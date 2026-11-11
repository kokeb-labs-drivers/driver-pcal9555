/**
 * @file test_pcal9555.cpp
 * @brief Driver behaviour over the protocol model: lifecycle, presence and identity checks,
 * configuration, pins, Agile I/O, port hooks.
 */
#include <cstring>

#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"
#include "test_harness.hpp"

using pcal9555::Bank;
using pcal9555::Direction;
using pcal9555::DriveStrength;
using pcal9555::Error;
using pcal9555::OutputMode;
using pcal9555::Pull;
using pcal9555::Variant;
using pcal9555_test::BarePort;
using pcal9555_test::FullPort;
using pcal9555_test::Pcal9555Model;
namespace reg = pcal9555::reg;

namespace {

pcal9555::Config Pcal() {
  pcal9555::Config c{};
  c.variant = Variant::Pcal9555a;
  return c;
}

constexpr auto kReadBack = pcal9555::Verify::ReadBack;

/// A port whose chip is a PCA9555.
template <typename Port>
void MakePca9555(Port& port) {
  port.state.chip.variant = Pcal9555Model::Part::Pca9555;
  port.state.chip.PowerOn();
}

} // namespace

// --- Lifecycle -----------------------------------------------------------------------------------

TEST_CASE("The constructor does no I/O") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, 0x23);
  CHECK(!io.IsInitialized());
  CHECK_EQ(io.GetAddress(), 0x23);
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(port.state.bus_up_calls, 0);
}

TEST_CASE("Every I/O call returns NotInitialized before EnsureInitialized") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  CHECK_EQ(io.SetDirection(1, Direction::Output).error, Error::NotInitialized);
  CHECK_EQ(io.SetOutputs(1, true).error, Error::NotInitialized);
  CHECK_EQ(io.WriteOutputs(0).error, Error::NotInitialized);
  CHECK_EQ(io.ToggleOutputs(1).error, Error::NotInitialized);
  CHECK_EQ(io.SetPolarityInversion(1, true).error, Error::NotInitialized);
  CHECK_EQ(io.ReadInputs().error, Error::NotInitialized);
  CHECK_EQ(io.ReadPin(0).error, Error::NotInitialized);
  CHECK_EQ(io.SetDriveStrength(1, DriveStrength::Half).error, Error::NotInitialized);
  CHECK_EQ(io.SetPull(1, Pull::Down).error, Error::NotInitialized);
  CHECK_EQ(io.SetInputLatch(1, true).error, Error::NotInitialized);
  CHECK_EQ(io.SetInterruptEnabled(1, true).error, Error::NotInitialized);
  CHECK_EQ(io.ReadInterruptStatus().error, Error::NotInitialized);
  CHECK_EQ(io.SetOutputMode(Bank::Port0, OutputMode::OpenDrain).error, Error::NotInitialized);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("EnsureInitialized on a PCA9555 writes and reads back the standard registers only") {
  BarePort port;
  MakePca9555(port);
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK(io.IsInitialized());
  CHECK_EQ(port.state.bus_up_calls, 1);
  // presence, the identity pattern written and read, three writes, three read-backs
  CHECK_EQ(port.state.transfers, 1 + 2 + 3 + 3);
  CHECK(port.state.max_len <= 2); // never more than one register pair
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0xFFFFU);
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xFFFFU);
}

TEST_CASE("EnsureInitialized on a PCAL9555A also writes and reads back the Agile I/O bank") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, 2 + 2 + 7 + 3 + 3 + 7); // presence, pattern, writes, read-backs
  CHECK(port.state.max_len <= 2);
}

TEST_CASE("Config{} is the power-up state, so the start leaves a fresh chip unchanged") {
  BarePort port;
  const Pcal9555Model fresh;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  for (const uint8_t r : {reg::kOutputPort0, reg::kPolarityPort0, reg::kConfigPort0,
                          reg::kDriveStrength0, reg::kDriveStrength2, reg::kInputLatch0,
                          reg::kPullEnable0, reg::kPullSelect0, reg::kInterruptMask0}) {
    CHECK_EQ(port.state.chip.Pair(r), fresh.Pair(r));
  }
  CHECK_EQ(port.state.chip.Register(reg::kOutputPortConfig), reg::kOutputPortConfigReset);
  CHECK_EQ(pcal9555::Config{}.drive_strength_codes, reg::kDriveStrengthReset);
  CHECK_EQ(pcal9555::DriveStrengthAll(DriveStrength::Full), reg::kDriveStrengthReset);
}

TEST_CASE("EnsureInitialized writes a whole configuration") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.output_high_mask = 0x00F0;
  c.input_mask = 0xFF00;
  c.polarity_invert_mask = 0x0100;
  c.drive_strength_codes = pcal9555::DriveStrengthAll(DriveStrength::Half);
  c.input_latch_mask = 0x0300;
  c.pull_enable_mask = 0xF000;
  c.pull_up_mask = 0x3000;
  c.interrupt_mask = 0x00FF;
  c.port1_mode = OutputMode::OpenDrain;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x00F0U);
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xFF00U);
  CHECK_EQ(port.state.chip.Pair(reg::kPolarityPort0), 0x0100U);
  CHECK_EQ(port.state.chip.Pair(reg::kDriveStrength0), 0x5555U);
  CHECK_EQ(port.state.chip.Pair(reg::kDriveStrength2), 0x5555U);
  CHECK_EQ(port.state.chip.Pair(reg::kInputLatch0), 0x0300U);
  CHECK_EQ(port.state.chip.Pair(reg::kPullEnable0), 0xF000U);
  CHECK_EQ(port.state.chip.Pair(reg::kPullSelect0), 0x3000U);
  CHECK_EQ(port.state.chip.Pair(reg::kInterruptMask0), 0x00FFU);
  CHECK_EQ(port.state.chip.Register(reg::kOutputPortConfig), reg::kOutputConfigOden1);
  CHECK_EQ(io.GetConfig().input_mask, 0xFF00U);
}

TEST_CASE("A second EnsureInitialized does no I/O") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  const int before = port.state.transfers;
  const int bus_up = port.state.bus_up_calls;
  CHECK(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers, before);
  CHECK_EQ(port.state.bus_up_calls, bus_up);
}

TEST_CASE("After a failed start the next EnsureInitialized starts again from scratch") {
  BarePort port;
  port.state.bus_up_ok = false;
  pcal9555::PCAL9555<BarePort> io(port);
  CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  port.state.bus_up_ok = true;
  port.state.fail_transfer = port.state.transfers + 4; // a read-back fails
  CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  CHECK(!io.IsInitialized());
  const int before = port.state.transfers;
  CHECK(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.transfers - before, 1 + 2 + 3 + 3); // the whole start-up again
  CHECK(io.IsInitialized());
}

TEST_CASE("An invalid stored configuration is InvalidArgument without I/O, every time") {
  pcal9555::Config c{};
  c.variant = static_cast<Variant>(5); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  CHECK_EQ(io.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(io.EnsureInitialized().error, Error::InvalidArgument);
  CHECK_EQ(port.state.bus_up_calls, 0);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("Deinitialize then EnsureInitialized restarts with the configuration and pin changes") {
  BarePort port;
  pcal9555::Config c{};
  c.output_high_mask = 0x0F0F;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.SetDirection(0x0001, Direction::Output).ok());
  REQUIRE(io.Deinitialize().ok());
  port.state.chip.PowerOn(); // the board lost power meanwhile
  const int bus_up = port.state.bus_up_calls;
  CHECK(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.bus_up_calls, bus_up + 1);
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x0F0FU);
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xFFFEU);
}

TEST_CASE("ApplyConfig before the start only stores the configuration") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  pcal9555::Config c = Pcal();
  c.output_high_mask = 0x00FF;
  CHECK(io.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers, 0);
  CHECK_EQ(port.state.bus_up_calls, 0);
  CHECK(!io.IsInitialized());
  CHECK_EQ(io.GetConfig().variant, Variant::Pcal9555a);
  pcal9555::Config bad{};
  bad.pull_up_mask = 0;
  CHECK_EQ(io.ApplyConfig(bad).error, Error::Unsupported); // still validated
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x00FFU);
}

TEST_CASE("Deinitialize makes every pin an input and keeps the configuration") {
  BarePort port;
  pcal9555::Config c{};
  c.input_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  CHECK(io.Deinitialize().ok()); // uninitialised: nothing to do
  CHECK_EQ(port.state.transfers, 0);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK(io.Deinitialize().ok());
  CHECK(!io.IsInitialized());
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xFFFFU);
  CHECK_EQ(io.GetConfig().input_mask, 0x0000U);
  CHECK_EQ(io.ReadInputs().error, Error::NotInitialized);
}

TEST_CASE("A failed Deinitialize still leaves the driver uninitialised") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.Deinitialize().error, Error::Bus);
  CHECK(!io.IsInitialized());
}

// --- Presence and identity -----------------------------------------------------------------------

TEST_CASE("An address outside 0x20 to 0x27 is rejected before any I/O") {
  for (const uint8_t address : {uint8_t{0x1F}, uint8_t{0x28}, uint8_t{0x48}}) {
    BarePort port;
    port.state.address = address;
    pcal9555::PCAL9555<BarePort> io(port, address);
    CHECK_EQ(io.EnsureInitialized().error, Error::InvalidArgument);
    CHECK_EQ(port.state.bus_up_calls, 0);
    CHECK_EQ(port.state.transfers, 0);
  }
}

TEST_CASE("AddressFromPins covers 0x20 to 0x27") {
  CHECK_EQ(pcal9555::AddressFromPins(false, false, false), 0x20);
  CHECK_EQ(pcal9555::AddressFromPins(false, false, true), 0x21);
  CHECK_EQ(pcal9555::AddressFromPins(true, false, true), 0x25);
  CHECK_EQ(pcal9555::AddressFromPins(true, true, true), 0x27);
  BarePort port;
  port.state.address = 0x25;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::AddressFromPins(true, false, true));
  CHECK(io.EnsureInitialized().ok());
}

TEST_CASE("No chip at the address is Bus, and the driver stays uninitialised") {
  BarePort port;
  port.state.address = 0x21;
  pcal9555::PCAL9555<BarePort> io(port, 0x20);
  CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  CHECK(!io.IsInitialized());
  CHECK_EQ(port.state.transfers, 1); // stopped after the presence read
}

TEST_CASE("A port that cannot bring the bus up is Bus") {
  BarePort port;
  port.state.bus_up_ok = false;
  pcal9555::PCAL9555<BarePort> io(port);
  CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("A PCA9555 configured as a PCAL9555A fails the start before any write") {
  { // The documented behaviour: the Agile I/O command byte is NACKed.
    BarePort port;
    MakePca9555(port);
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
    CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
    CHECK_EQ(port.state.chip.writes, 0);
  }
  { // Fault model: a part that aliases 0x4F onto configuration port 1 (0xFF after power-up).
    BarePort port;
    MakePca9555(port);
    port.state.chip.unknown_command = Pcal9555Model::UnknownCommand::AliasLow;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
    CHECK_EQ(io.EnsureInitialized().error, Error::DeviceIdMismatch);
    CHECK_EQ(port.state.chip.writes, 0);
  }
}

TEST_CASE("Reserved bits of 0x4F reading 1 is DeviceIdMismatch") {
  BarePort port;
  port.state.chip.reserved_bits = 0x80;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  CHECK_EQ(io.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK(!io.IsInitialized());
}

TEST_CASE("A register that does not hold what was written is DeviceIdMismatch") {
  for (const int stuck :
       {int{reg::kOutputPort0}, int{reg::kConfigPort0 + 1}, int{reg::kPullSelect0},
        int{reg::kInterruptMask0 + 1}, int{reg::kOutputPortConfig}}) {
    FullPort port;
    port.state.chip.stuck_register = stuck;
    pcal9555::Config c = Pcal();
    c.output_high_mask = 0x1234;
    c.input_mask = 0x00FF;
    c.pull_up_mask = 0x0F0F;
    c.interrupt_mask = 0x0FFF;
    c.port0_mode = OutputMode::OpenDrain;
    pcal9555::PCAL9555<FullPort> io(port, pcal9555::kI2cAddress, c);
    CHECK_EQ(io.EnsureInitialized().error, Error::DeviceIdMismatch);
    CHECK(!io.IsInitialized());
    CHECK(port.error_lines > 0);
  }
}

TEST_CASE("A PCAL9555A driven as a PCA9555 works with the shared registers") {
  BarePort port; // the model is a PCAL9555A
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK(io.SetOutputs(0x0001, false).ok());
  CHECK_EQ(io.SetPull(0x0001, Pull::Down).error, Error::Unsupported);
}

// --- Validate and ApplyConfig --------------------------------------------------------------------

TEST_CASE("Validate rejects values that are not enumerators") {
  pcal9555::Config c = Pcal();
  CHECK_EQ(pcal9555::Validate(c), Error::Ok);
  c.variant = static_cast<Variant>(2); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(pcal9555::Validate(c), Error::InvalidArgument);
  c = Pcal();
  c.port0_mode = static_cast<OutputMode>(2); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(pcal9555::Validate(c), Error::InvalidArgument);
  c = Pcal();
  c.port1_mode = static_cast<OutputMode>(7); // NOLINT(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(pcal9555::Validate(c), Error::InvalidArgument);
}

TEST_CASE("Validate refuses Agile I/O fields for a PCA9555") {
  CHECK_EQ(pcal9555::Validate({}), Error::Ok);
  pcal9555::Config c{};
  c.drive_strength_codes = 0;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  c = {};
  c.input_latch_mask = 1;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  c = {};
  c.pull_enable_mask = 0;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  c = {};
  c.pull_up_mask = 0;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  c = {};
  c.interrupt_mask = 0;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  c = {};
  c.port0_mode = OutputMode::OpenDrain;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  c = {};
  c.port1_mode = OutputMode::OpenDrain;
  CHECK_EQ(pcal9555::Validate(c), Error::Unsupported);
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  CHECK_EQ(io.EnsureInitialized().error, Error::Unsupported);
  CHECK_EQ(port.state.transfers, 0);
}

TEST_CASE("ApplyConfig on a running driver writes every register and reads each back") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  pcal9555::Config c = Pcal();
  c.output_high_mask = 0x8001;
  c.interrupt_mask = 0x7FFF;
  const int before = port.state.transfers;
  REQUIRE(io.ApplyConfig(c).ok());
  CHECK_EQ(port.state.transfers - before, 10 + 10);
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x8001U);
  CHECK_EQ(port.state.chip.Pair(reg::kInterruptMask0), 0x7FFFU);
  CHECK_EQ(io.GetConfig().output_high_mask, 0x8001U);
}

TEST_CASE("ApplyConfig rejects a changed variant and invalid values before any I/O") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  const int before = port.state.transfers;
  CHECK_EQ(io.ApplyConfig(Pcal()).error, Error::InvalidArgument);
  pcal9555::Config c{};
  c.pull_up_mask = 0;
  CHECK_EQ(io.ApplyConfig(c).error, Error::Unsupported);
  CHECK_EQ(port.state.transfers, before);
}

TEST_CASE("ApplyConfig reports a register that does not take the value") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.stuck_register = reg::kPolarityPort0;
  pcal9555::Config c{};
  c.polarity_invert_mask = 0x0001;
  CHECK_EQ(io.ApplyConfig(c).error,
           Error::WriteMismatch); // started: a lost write, not another part
}

TEST_CASE("A failed ApplyConfig leaves the riskier configuration, read before it is built on") {
  BarePort port;
  pcal9555::Config start{};
  start.output_high_mask = 0x0F0F;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, start);
  REQUIRE(io.EnsureInitialized().ok());
  pcal9555::Config c{};
  c.output_high_mask = 0;
  port.state.fail_transfer =
      port.state.transfers + 1; // the reset check read, then the output write
  CHECK_EQ(io.ApplyConfig(c).error, Error::Bus);
  CHECK_EQ(io.GetConfig().output_high_mask, 0x0000U); // the written level: it may have arrived
  const int before = port.state.transfers;
  REQUIRE(io.SetOutputs(1, false).ok()); // reads the output pair (0x0F0F) first
  CHECK_EQ(port.state.transfers - before, 2);
  CHECK_EQ(io.GetConfig().output_high_mask, 0x0F0EU);
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x0F0EU);
}

// --- Pins ----------------------------------------------------------------------------------------

TEST_CASE("SetDirection and SetOutputs change only the pins in the mask") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  const int before = port.state.transfers;
  REQUIRE(io.SetOutputs(pcal9555::PinMask(3) | pcal9555::PinMask(12), false).ok());
  REQUIRE(io.SetDirection(pcal9555::PinMask(3) | pcal9555::PinMask(12), Direction::Output).ok());
  // SetOutputs: one write. SetDirection: the reset check, then the output and configuration pairs.
  CHECK_EQ(port.state.transfers - before, 1 + 3);
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0xEFF7U);
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xEFF7U);
  REQUIRE(io.SetDirection(pcal9555::PinMask(12), Direction::Input).ok());
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xFFF7U);
  CHECK_EQ(io.GetConfig().input_mask, 0xFFF7U);
  REQUIRE(io.SetOutputs(pcal9555::PinMask(3), true).ok());
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0xEFFFU);
}

TEST_CASE("WriteOutputs and ToggleOutputs") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.WriteOutputs(0x0000).ok());
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x0000U);
  REQUIRE(io.ToggleOutputs(0x8001).ok());
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0x8001U);
  REQUIRE(io.ToggleOutputs(0x0001).ok());
  CHECK_EQ(io.GetConfig().output_high_mask, 0x8000U);
}

TEST_CASE("A mask of 0 or a pin out of range is InvalidArgument without I/O") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  const int before = port.state.transfers;
  CHECK_EQ(io.SetDirection(0, Direction::Output).error, Error::InvalidArgument);
  CHECK_EQ(io.SetOutputs(0, true).error, Error::InvalidArgument);
  CHECK_EQ(io.ToggleOutputs(0).error, Error::InvalidArgument);
  CHECK_EQ(io.SetPolarityInversion(0, true).error, Error::InvalidArgument);
  CHECK_EQ(io.ReadPin(16).error, Error::InvalidArgument);
  CHECK_EQ(io.SetOutputs(pcal9555::PinMask(16), true).error, Error::InvalidArgument);
  CHECK_EQ(io.SetDriveStrength(0, DriveStrength::Half).error, Error::InvalidArgument);
  CHECK_EQ(io.SetPull(0, Pull::Up).error, Error::InvalidArgument);
  CHECK_EQ(io.SetInputLatch(0, true).error, Error::InvalidArgument);
  CHECK_EQ(io.SetInterruptEnabled(0, true).error, Error::InvalidArgument);
  // NOLINTBEGIN(clang-analyzer-optin.core.EnumCastOutOfRange): out-of-range values on purpose
  CHECK_EQ(io.SetDriveStrength(1, static_cast<DriveStrength>(4)).error, Error::InvalidArgument);
  CHECK_EQ(io.SetPull(1, static_cast<Pull>(3)).error, Error::InvalidArgument);
  CHECK_EQ(io.SetOutputMode(static_cast<Bank>(2), OutputMode::OpenDrain).error,
           Error::InvalidArgument);
  CHECK_EQ(io.SetOutputMode(Bank::Port0, static_cast<OutputMode>(2)).error, Error::InvalidArgument);
  // NOLINTEND(clang-analyzer-optin.core.EnumCastOutOfRange)
  CHECK_EQ(port.state.transfers, before);
}

TEST_CASE("ReadInputs and ReadPin return the pin levels, outputs included") {
  BarePort port;
  pcal9555::Config c{};
  c.input_mask = 0xFF00;       // port 0 outputs, port 1 inputs
  c.output_high_mask = 0x00A5; // port 0 drives 0xA5
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.SetExternal(0x3C00);
  const auto in = io.ReadInputs();
  REQUIRE(in.ok());
  CHECK_EQ(in.value, 0x3CA5U);
  CHECK_EQ(io.ReadPin(0).value, true);
  CHECK_EQ(io.ReadPin(1).value, false);
  CHECK_EQ(io.ReadPin(10).value, true);
  CHECK_EQ(io.ReadPin(15).value, false);
}

TEST_CASE("Polarity inversion inverts how input pins read") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.SetExternal(0x0000);
  REQUIRE(io.SetPolarityInversion(0x0003, true).ok());
  CHECK_EQ(io.ReadInputs().value, 0x0003U);
  REQUIRE(io.SetPolarityInversion(0x0001, false).ok());
  CHECK_EQ(io.ReadInputs().value, 0x0002U);
}

// --- Agile I/O -----------------------------------------------------------------------------------

TEST_CASE("Every Agile I/O call is Unsupported without I/O on a PCA9555") {
  BarePort port;
  MakePca9555(port);
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  const int before = port.state.transfers;
  CHECK_EQ(io.SetDriveStrength(1, DriveStrength::Half).error, Error::Unsupported);
  CHECK_EQ(io.SetPull(1, Pull::Down).error, Error::Unsupported);
  CHECK_EQ(io.SetInputLatch(1, true).error, Error::Unsupported);
  CHECK_EQ(io.SetInterruptEnabled(1, true).error, Error::Unsupported);
  CHECK_EQ(io.ReadInterruptStatus().error, Error::Unsupported);
  CHECK_EQ(io.SetOutputMode(Bank::Port1, OutputMode::OpenDrain).error, Error::Unsupported);
  CHECK_EQ(port.state.transfers, before);
}

TEST_CASE("SetDriveStrength writes two bits per pin across both pairs") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.SetDriveStrength(pcal9555::PinMask(0) | pcal9555::PinMask(7), DriveStrength::Quarter)
              .ok());
  REQUIRE(io.SetDriveStrength(pcal9555::PinMask(9), DriveStrength::ThreeQuarters).ok());
  CHECK_EQ(port.state.chip.Register(reg::kDriveStrength0), 0xFCU);     // P0_0 = 00
  CHECK_EQ(port.state.chip.Register(reg::kDriveStrength0 + 1), 0x3FU); // P0_7 = 00
  CHECK_EQ(port.state.chip.Register(reg::kDriveStrength2), 0xFBU);     // P1_1 = 10
  CHECK_EQ(port.state.chip.Register(reg::kDriveStrength2 + 1), 0xFFU);
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFB'3FFCU);
}

TEST_CASE("SetPull selects before it enables, and None only disconnects") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.SetPull(0x000F, Pull::Down).ok());
  CHECK_EQ(port.state.chip.Pair(reg::kPullSelect0), 0xFFF0U);
  CHECK_EQ(port.state.chip.Pair(reg::kPullEnable0), 0xFFFFU);
  const int before = port.state.transfers;
  REQUIRE(io.SetPull(0x0003, Pull::None).ok());
  CHECK_EQ(port.state.transfers - before, 1); // the enable register only
  CHECK_EQ(port.state.chip.Pair(reg::kPullEnable0), 0xFFFCU);
  CHECK_EQ(port.state.chip.Pair(reg::kPullSelect0), 0xFFF0U);
  REQUIRE(io.SetPull(0x0001, Pull::Up).ok());
  CHECK_EQ(port.state.chip.Pair(reg::kPullSelect0), 0xFFF1U);
  CHECK_EQ(port.state.chip.Pair(reg::kPullEnable0), 0xFFFDU);
}

TEST_CASE("Interrupts: masked at power-up, raised by an unmasked input, cleared by reading") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  auto& chip = port.state.chip;
  chip.SetExternal(0xFFFE);
  CHECK(!chip.InterruptAsserted());             // masked
  CHECK_EQ(io.ReadInterruptStatus().value, 0U); // masked pins read 0
  REQUIRE(io.SetInterruptEnabled(0x0001, true).ok());
  CHECK(chip.InterruptAsserted()); // unmasking a pending change asserts INT
  CHECK_EQ(io.ReadInterruptStatus().value, 0x0001U);
  CHECK(chip.InterruptAsserted());     // reading the status does not clear it
  CHECK_EQ(io.ReadPin(8).value, true); // port 1 only: port 0 still pending
  CHECK(chip.InterruptAsserted());
  CHECK_EQ(io.ReadInputs().value, 0xFFFEU);
  CHECK(!chip.InterruptAsserted());
  REQUIRE(io.SetInterruptEnabled(0x0001, false).ok());
  CHECK_EQ(io.GetConfig().interrupt_mask, 0xFFFFU);
}

TEST_CASE("A latched input keeps the level that raised the interrupt until it is read") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.SetInterruptEnabled(0x0003, true).ok());
  REQUIRE(io.SetInputLatch(0x0001, true).ok());
  auto& chip = port.state.chip;
  chip.SetExternal(0xFFFC);                          // pins 0 and 1 go low...
  chip.SetExternal(0xFFFF);                          // ...and back high
  CHECK(chip.InterruptAsserted());                   // the latched pin keeps INT asserted
  CHECK_EQ(io.ReadInterruptStatus().value, 0x0001U); // the unlatched pin cleared itself
  CHECK_EQ(io.ReadInputs().value, 0xFFFEU);          // pin 0 reads the latched low
  CHECK(!chip.InterruptAsserted());
  CHECK_EQ(io.ReadInputs().value, 0xFFFFU); // the next read shows the pin again
  REQUIRE(io.SetInputLatch(0x0001, false).ok());
  CHECK_EQ(io.GetConfig().input_latch_mask, 0U);
}

TEST_CASE("Open-drain outputs only pull low and let a high float to the external level") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.SetOutputMode(Bank::Port1, OutputMode::OpenDrain).ok());
  CHECK_EQ(port.state.chip.Register(reg::kOutputPortConfig), reg::kOutputConfigOden1);
  REQUIRE(io.SetOutputs(0x0300, false).ok());
  REQUIRE(io.SetOutputs(0x0100, true).ok());
  REQUIRE(io.SetDirection(0x0300, Direction::Output).ok());
  port.state.chip.SetExternal(0xFEFF); // something else pulls P1_0 low
  const auto in = io.ReadInputs();
  CHECK_EQ(in.value & 0x0300U, 0x0000U); // P1_0 "high" floats low; P1_1 drives low
  REQUIRE(io.SetOutputMode(Bank::Port0, OutputMode::OpenDrain).ok());
  REQUIRE(io.SetOutputMode(Bank::Port1, OutputMode::PushPull).ok());
  CHECK_EQ(port.state.chip.Register(reg::kOutputPortConfig), reg::kOutputConfigOden0);
  CHECK_EQ(io.GetConfig().port1_mode, OutputMode::PushPull);
}

TEST_CASE("A failed second drive strength write keeps the copy in step with the chip") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1; // write 0x40, then write 0x42
  CHECK_EQ(io.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Quarter).error, Error::Bus);
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFF'0000U);
  CHECK_EQ(port.state.chip.Pair(reg::kDriveStrength0), 0x0000U);
  CHECK_EQ(port.state.chip.Pair(reg::kDriveStrength2), 0xFFFFU);
}

// --- Read-back and verified writes ---------------------------------------------------------------

TEST_CASE("ReadOutputLatch, ReadInputMask and ReadPolarityInversion read what the chip holds") {
  BarePort port;
  pcal9555::Config c{};
  c.output_high_mask = 0x5D15;
  c.input_mask = 0xA2E2;
  c.polarity_invert_mask = 0x0003;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  CHECK_EQ(io.ReadOutputLatch().error, Error::NotInitialized);
  CHECK_EQ(io.ReadInputMask().error, Error::NotInitialized);
  CHECK_EQ(io.ReadPolarityInversion().error, Error::NotInitialized);
  CHECK_EQ(port.state.transfers, 0);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(io.ReadOutputLatch().value, 0x5D15);
  CHECK_EQ(io.ReadInputMask().value, 0xA2E2);
  CHECK_EQ(io.ReadPolarityInversion().value, 0x0003);
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.ReadOutputLatch().error, Error::Bus);
}

TEST_CASE("A write the chip acknowledges but loses is reported as WriteMismatch") {
  // An acknowledge carries no checksum, and the data sheet allows for data corruption (PCAL9555A
  // section 8.3); the output port reads back what it holds (6.2.3). Verify::ReadBack catches a
  // write that was acknowledged and dropped.
  BarePort port;
  pcal9555::Config c{};
  c.input_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.drop_writes = 2; // both bytes of the output port write
  CHECK_EQ(io.SetOutputs(0x0100, false, kReadBack).error, Error::WriteMismatch);
  CHECK_EQ(io.GetConfig().output_high_mask, 0xFEFF);                  // what was asked for
  CHECK_EQ(io.ReadOutputLatch().value, 0xFFFF);                       // what the chip kept
  CHECK_EQ(io.SetOutputs(0x0100, false, kReadBack).error, Error::Ok); // a retry resends it
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0xFEFF);
}

TEST_CASE("With Verify::ReadBack, every setter verifies its write") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  const auto lost = [&](auto call) {
    port.state.chip.drop_writes = 1;
    const Error e = call();
    port.state.chip.drop_writes = 0;
    return e;
  };
  port.state.chip.drop_writes = 1 + 2 + 1; // 0x4F and the output pair arrive; the config byte not
  CHECK_EQ(io.SetDirection(1, Direction::Output, kReadBack).error, Error::WriteMismatch);
  port.state.chip.drop_writes = 0;
  CHECK_EQ(lost([&] { return io.WriteOutputs(0x1234, kReadBack).error; }), Error::WriteMismatch);
  CHECK_EQ(lost([&] { return io.ToggleOutputs(1, kReadBack).error; }), Error::WriteMismatch);
  CHECK_EQ(lost([&] { return io.SetPolarityInversion(1, true, kReadBack).error; }),
           Error::WriteMismatch);
  CHECK_EQ(lost([&] { return io.SetPull(1, Pull::Down, kReadBack).error; }), Error::WriteMismatch);
  CHECK_EQ(lost([&] { return io.SetInputLatch(1, true, kReadBack).error; }), Error::WriteMismatch);
  CHECK_EQ(lost([&] { return io.SetInterruptEnabled(1, true, kReadBack).error; }),
           Error::WriteMismatch);
  CHECK_EQ(lost([&] { return io.SetDriveStrength(1, DriveStrength::Half, kReadBack).error; }),
           Error::WriteMismatch);
  CHECK_EQ(
      lost([&] { return io.SetOutputMode(Bank::Port1, OutputMode::OpenDrain, kReadBack).error; }),
      Error::WriteMismatch);
  // So many lost writes left registers at their power-up values: the reset check of ApplyConfig()
  // would rightly read that as a reset. Start again, which writes the copy.
  REQUIRE(io.Deinitialize().ok());
  REQUIRE(io.EnsureInitialized().ok());
  pcal9555::Config next = Pcal(); // ApplyConfig() always reads back
  next.output_high_mask = 0x00FF;
  port.state.chip.stuck_register = reg::kOutputPort0 + 1; // output port 1 keeps 0xFF
  CHECK_EQ(io.ApplyConfig(next).error, Error::WriteMismatch);
}

TEST_CASE(
    "After WriteMismatch the copy holds what was asked, drive strength and toggles included") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.drop_writes = 2;
  CHECK_EQ(io.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Quarter, kReadBack).error,
           Error::WriteMismatch);
  // The first pair was lost, so the second was never sent: pins 8..15 keep their codes.
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFF'0000U);
  port.state.chip.stuck_register = reg::kDriveStrength2; // the second pair is lost
  CHECK_EQ(io.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Half, kReadBack).error,
           Error::WriteMismatch);
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0x5555'5555U);
  port.state.chip.stuck_register = -1;
  port.state.chip.drop_writes = 2;
  CHECK_EQ(io.ToggleOutputs(0x0001, kReadBack).error, Error::WriteMismatch);
  CHECK_EQ(io.GetConfig().output_high_mask, 0xFFFE); // the toggled value
  CHECK_EQ(io.WriteOutputs(io.GetConfig().output_high_mask, kReadBack).error,
           Error::Ok); // the documented retry
  CHECK_EQ(port.state.chip.Pair(reg::kOutputPort0), 0xFFFE);
}

TEST_CASE("By default no pin write is read back: each call sends only its writes") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  const auto writes_only = [&](auto call, int writes) {
    const int transfers = port.state.transfers;
    const int reads = port.state.reads;
    const Error e = call();
    CHECK_EQ(port.state.reads, reads);
    CHECK_EQ(port.state.transfers - transfers, writes);
    return e;
  };
  // SetDirection writes 0x4F, the output pair and the configuration pair (section 6.2.12); the
  // copy is all power-up values, so its reset check sends nothing.
  CHECK_EQ(writes_only([&] { return io.SetDirection(1, Direction::Output).error; }, 3), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.SetOutputs(1, false).error; }, 1), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.WriteOutputs(0x1234).error; }, 1), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.ToggleOutputs(1).error; }, 1), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.SetPolarityInversion(1, true).error; }, 1), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.SetPull(1, Pull::Down).error; }, 2), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.SetInputLatch(1, true).error; }, 1), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.SetInterruptEnabled(1, true).error; }, 1), Error::Ok);
  CHECK_EQ(writes_only([&] { return io.SetDriveStrength(1, DriveStrength::Half).error; }, 2),
           Error::Ok);
  CHECK_EQ(
      writes_only([&] { return io.SetOutputMode(Bank::Port1, OutputMode::OpenDrain).error; }, 1),
      Error::Ok);
  port.state.chip.drop_writes = 2;
  CHECK_EQ(writes_only([&] { return io.WriteOutputs(0x0000).error; }, 1),
           Error::Ok);                           // a lost write is not seen without the read-back
  CHECK_EQ(io.ReadOutputLatch().value, 0x1235U); // the chip kept the old levels
}

TEST_CASE("A Verify value that is not an enumerator is InvalidArgument, with no I/O") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): out of range on purpose
  const auto bad = static_cast<pcal9555::Verify>(2);
  const int before = port.state.transfers;
  CHECK_EQ(io.SetOutputs(1, false, bad).error, Error::InvalidArgument);
  CHECK_EQ(io.SetPull(1, Pull::Down, bad).error, Error::InvalidArgument);
  CHECK_EQ(io.SetOutputMode(Bank::Port0, OutputMode::OpenDrain, bad).error, Error::InvalidArgument);
  CHECK_EQ(port.state.transfers, before);
  CHECK_EQ(io.GetConfig().output_high_mask, 0xFFFFU);
}

// --- Port hooks ----------------------------------------------------------------------------------

TEST_CASE("Log() is called for lifecycle events and failures") {
  FullPort port;
  pcal9555::PCAL9555<FullPort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK(port.log_lines > 0);
  CHECK_EQ(port.error_lines, 0);
  REQUIRE(io.Deinitialize().ok());
  port.state.bus_up_ok = false;
  CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  CHECK(port.error_lines > 0);
}

TEST_CASE("Error names") {
  CHECK(std::strcmp(pcal9555::ToString(Error::Bus), "Bus") == 0);
  // NOLINTNEXTLINE(clang-analyzer-optin.core.EnumCastOutOfRange): not an enumerator on purpose
  CHECK(std::strcmp(pcal9555::ToString(static_cast<Error>(200)), "Unknown") == 0);
}
