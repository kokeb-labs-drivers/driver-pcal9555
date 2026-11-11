/**
 * @file test_reset.cpp
 * @brief A power-on reset in the middle of a session (section 6.4, table 4) and the interrupt
 * status that a read of the inputs clears (sections 6.2.11, 6.5).
 *
 * The parts have no reset pin and no reset flag: a supply dip below the power-on reset voltage
 * reloads every register with its power-up value (outputs high, every pin an input, 0x4F
 * push-pull). The model does that on its own (`reset_after`) or at once (`PowerOn()`).
 */
#include <cstdio>

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
using pcal9555_test::Pcal9555Model;
namespace chip = pcal9555_test::chip;
namespace reg = pcal9555::reg;

namespace {

constexpr auto kReadBack = pcal9555::Verify::ReadBack;

/// Pins 0..7 outputs driven low through an open-drain port 0 (loads off), pins 8..15 inputs.
pcal9555::Config LoadsOff() {
  pcal9555::Config c{};
  c.variant = Variant::Pcal9555a;
  c.input_mask = 0xFF00;
  c.output_high_mask = 0xFF00;
  c.port0_mode = OutputMode::OpenDrain;
  return c;
}

/// Pins 0..7 outputs driven low, push-pull, on a PCA9555.
pcal9555::Config PcaLoadsOff() {
  pcal9555::Config c{};
  c.input_mask = 0xFF00;
  c.output_high_mask = 0xFF00;
  return c;
}

template <typename Port>
void MakePca9555(Port& port) {
  port.state.chip.variant = Pcal9555Model::Part::Pca9555;
  port.state.chip.PowerOn();
}

} // namespace

// --- The model -----------------------------------------------------------------------------------

TEST_CASE("The model resets on its own after the scheduled number of transactions") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, LoadsOff());
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFF00U);
  port.state.chip.reset_after = 1; // one more transaction, then the supply dips
  REQUIRE(io.ReadOutputLatch().ok());
  CHECK_EQ(port.state.chip.resets, 0);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFF00U);
  (void)io.ReadOutputLatch();
  CHECK_EQ(port.state.chip.resets, 1);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), chip::kConfigReset);
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), chip::kOutputReset);
  CHECK_EQ(port.state.chip.Register(chip::kOutputPortConfig), chip::kOutputPortConfigReset);
  CHECK_EQ(port.state.chip.reset_after, -1); // once
}

// --- P1: no setter turns pins back on after a reset ----------------------------------------------

TEST_CASE("After a power-on reset SetDirection returns DeviceReset and turns no output on") {
  BarePort port;
  port.state.chip.SetExternal(0x0000); // the loads pull the lines low
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, LoadsOff());
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Levels(), 0x0000U);
  port.state.chip.PowerOn(); // VDD dips below VPOR
  const int before = port.state.transfers;
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(8), Direction::Output).error, Error::DeviceReset);
  CHECK_EQ(port.state.transfers - before, 1); // the sentinel read; nothing written
  CHECK(!io.IsInitialized());
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), chip::kConfigReset); // every pin an input
  CHECK_EQ(port.state.chip.Levels(), 0x0000U);
  // Every call now returns NotInitialized until EnsureInitialized() writes the copy again, in
  // order.
  CHECK_EQ(io.SetOutputs(1, true).error, Error::NotInitialized);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFF00U);
  CHECK_EQ(port.state.chip.Register(chip::kOutputPortConfig), chip::kOden0);
  CHECK_EQ(port.state.chip.Levels(), 0x0000U);
}

TEST_CASE("SetDirection with Verify::ReadBack also reports the reset instead of passing") {
  BarePort port;
  port.state.chip.SetExternal(0x0000);
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, LoadsOff());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.PowerOn();
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(8), Direction::Output, kReadBack).error,
           Error::DeviceReset);
  CHECK_EQ(port.state.chip.Levels(), 0x0000U);
}

TEST_CASE("A reset between the check and the writes still turns no pin on at the reset level") {
  // The check read sees the chip intact; the supply dips right after it. SetDirection then writes
  // 0x4F, the output pair and the configuration pair from the copy, in that order (section
  // 6.2.12), so the new outputs start low and open-drain as configured, never high and push-pull.
  BarePort port;
  port.state.chip.SetExternal(0x0000);
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, LoadsOff());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.reset_after = 1; // after the check read
  const int before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(8), Direction::Output).ok());
  CHECK_EQ(port.state.chip.resets, 1);
  CHECK_EQ(port.state.transfers - before, 1 + 3); // check, 0x4F, output pair, configuration pair
  CHECK_EQ(port.state.chip.Register(chip::kOutputPortConfig), chip::kOden0);
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), 0xFF00U);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFE00U);
  CHECK_EQ(port.state.chip.Levels(), 0x0100U); // pins 0..7 low; pin 8 drives its high latch
  // The Agile I/O bank that SetDirection does not write stays at reset, and it is where the check
  // looks first, so the next check finds the reset.
  pcal9555::Config c = LoadsOff();
  c.input_mask = 0xFE00;
  c.interrupt_mask = 0x7FFF;
  BarePort port2;
  pcal9555::PCAL9555<BarePort> io2(port2, pcal9555::kI2cAddress, c);
  REQUIRE(io2.EnsureInitialized().ok());
  port2.state.chip.reset_after = 1;
  REQUIRE(io2.SetDirection(pcal9555::PinMask(9), Direction::Output).ok());
  CHECK_EQ(port2.state.chip.resets, 1);
  CHECK_EQ(io2.CheckForReset().error, Error::DeviceReset);
  CHECK(!io2.IsInitialized());
}

TEST_CASE("On a PCA9555 SetDirection writes the output pair before the configuration pair") {
  BarePort port;
  MakePca9555(port);
  port.state.chip.SetExternal(0x0000);
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, PcaLoadsOff());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.reset_after = 1;
  const int before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(8), Direction::Output).ok());
  CHECK_EQ(port.state.transfers - before, 1 + 2); // check, output pair, configuration pair
  CHECK_EQ(port.state.chip.Levels(), 0x0100U);
  // And a reset before the call is found.
  port.state.chip.PowerOn();
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(9), Direction::Output).error, Error::DeviceReset);
  CHECK_EQ(port.state.chip.Levels() & 0x00FFU, 0x0000U);
}

TEST_CASE("SetDirection sends 0x4F, the output pair and the configuration pair, then reads back") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, LoadsOff());
  REQUIRE(io.EnsureInitialized().ok());
  int before = port.state.transfers;
  int reads = port.state.reads;
  REQUIRE(io.SetDirection(pcal9555::PinMask(8), Direction::Output).ok());
  CHECK_EQ(port.state.transfers - before, 4);
  CHECK_EQ(port.state.reads - reads, 1);
  before = port.state.transfers;
  reads = port.state.reads;
  REQUIRE(io.SetDirection(pcal9555::PinMask(9), Direction::Output, kReadBack).ok());
  CHECK_EQ(port.state.transfers - before, 5);
  CHECK_EQ(port.state.reads - reads, 2);
  CHECK_EQ(io.GetConfig().input_mask, 0xFC00U);
  // A failed 0x4F or output write stops the call there; it resent the copy's own value, so the
  // copy stays known. A failed configuration write may have arrived: pin 10 counts as an output.
  for (int k = 1; k <= 2; ++k) {
    port.state.fail_transfer = port.state.transfers + k;
    CHECK_EQ(io.SetDirection(pcal9555::PinMask(10), Direction::Output).error, Error::Bus);
    CHECK_EQ(io.GetConfig().input_mask, 0xFC00U);
  }
  port.state.fail_transfer = port.state.transfers + 3;
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(10), Direction::Output).error, Error::Bus);
  CHECK_EQ(io.GetConfig().input_mask, 0xF800U);
  port.state.fail_transfer = port.state.transfers; // the check read
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(10), Direction::Output).error, Error::Bus);
  CHECK(io.IsInitialized());
}

TEST_CASE("CheckForReset reads one register and finds a reset in every part of the copy") {
  struct Case {
    const char* what;
    void (*set)(pcal9555::Config&);
  };
  const Case cases[] = {
      {"outputs", [](pcal9555::Config& c) { c.input_mask = 0x00FF; }},
      {"interrupt mask", [](pcal9555::Config& c) { c.interrupt_mask = 0xFFFE; }},
      {"pull enable", [](pcal9555::Config& c) { c.pull_enable_mask = 0x0000; }},
      {"pull select", [](pcal9555::Config& c) { c.pull_up_mask = 0x00FF; }},
      {"input latch", [](pcal9555::Config& c) { c.input_latch_mask = 0x0001; }},
      {"drive 0x40", [](pcal9555::Config& c) { c.drive_strength_codes = 0xFFFF'FFFEU; }},
      {"drive 0x42", [](pcal9555::Config& c) { c.drive_strength_codes = 0x7FFF'FFFFU; }},
      {"0x4F", [](pcal9555::Config& c) { c.port1_mode = OutputMode::OpenDrain; }},
      {"polarity", [](pcal9555::Config& c) { c.polarity_invert_mask = 0x8000; }},
      {"output latch", [](pcal9555::Config& c) { c.output_high_mask = 0x7FFF; }},
  };
  for (const Case& k : cases) {
    BarePort port;
    pcal9555::Config c{};
    c.variant = Variant::Pcal9555a;
    k.set(c);
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    int before = port.state.transfers;
    CHECK_EQ(io.CheckForReset().error, Error::Ok);
    CHECK_EQ(port.state.transfers - before, 1);
    port.state.chip.PowerOn();
    before = port.state.transfers;
    const Error e = io.CheckForReset().error;
    if (e != Error::DeviceReset) {
      std::printf("    case %s\n", k.what);
    }
    CHECK_EQ(e, Error::DeviceReset);
    CHECK_EQ(port.state.transfers - before, 1);
    CHECK(!io.IsInitialized());
  }
}

TEST_CASE("CheckForReset sends nothing when the copy holds the power-up values") {
  // The chip then holds after a reset what it held before: nothing to find, nothing to repair.
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  CHECK_EQ(io.CheckForReset().error, Error::NotInitialized);
  REQUIRE(io.EnsureInitialized().ok());
  const int before = port.state.transfers;
  CHECK_EQ(io.CheckForReset().error, Error::Ok);
  CHECK_EQ(port.state.transfers, before);
  // A failed read is the read's error, and the driver stays started.
  REQUIRE(io.SetOutputs(1, false).ok());
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.CheckForReset().error, Error::Bus);
  CHECK(io.IsInitialized());
}

TEST_CASE("On a PCA9555 the check reads the standard registers only") {
  BarePort port;
  MakePca9555(port);
  pcal9555::Config c{};
  c.polarity_invert_mask = 0x0100; // all inputs: polarity is what a reset would lose
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.CheckForReset().ok());
  port.state.chip.PowerOn();
  CHECK_EQ(io.CheckForReset().error, Error::DeviceReset);
}

TEST_CASE("A setter that writes the sentinel register checks it first") {
  // All inputs: the sentinel is the interrupt mask, which SetInterruptEnabled() rewrites, so it
  // checks before writing; a setter of another register does not.
  BarePort port;
  pcal9555::Config c{};
  c.variant = Variant::Pcal9555a;
  c.interrupt_mask = 0xFFFE;
  c.polarity_invert_mask = 0x0100;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  int before = port.state.transfers;
  REQUIRE(io.SetPolarityInversion(1, true).ok());
  CHECK_EQ(port.state.transfers - before, 1);
  before = port.state.transfers;
  REQUIRE(io.SetInterruptEnabled(2, true).ok());
  CHECK_EQ(port.state.transfers - before, 2);
  port.state.chip.PowerOn();
  CHECK_EQ(io.SetPolarityInversion(4, true).error, Error::Ok); // not the sentinel
  CHECK_EQ(io.SetInterruptEnabled(4, true).error, Error::DeviceReset);
  CHECK_EQ(port.state.chip.Pair(chip::kInterruptMask0), chip::kInterruptMaskReset);
}

TEST_CASE("A setter of the only register off its power-up value repairs either case unread") {
  // The chip holds the copy or the power-up values, which differ in that register only: the
  // write leaves the chip as the copy says, after a reset or a lost write alike, with no read.
  struct Case {
    const char* what;
    void (*set)(pcal9555::Config&);
    Error (*call)(pcal9555::PCAL9555<BarePort>&);
    uint8_t reg;
  };
  const Case cases[] = {
      {"SetInterruptEnabled", [](pcal9555::Config& c) { c.interrupt_mask = 0xFFFE; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetInterruptEnabled(2, true).error; },
       chip::kInterruptMask0},
      {"SetOutputs", [](pcal9555::Config& c) { c.output_high_mask = 0x7FFF; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetOutputs(1, false).error; },
       chip::kOutputPort0},
      {"WriteOutputs", [](pcal9555::Config& c) { c.output_high_mask = 0x7FFF; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.WriteOutputs(0x7FFE).error; },
       chip::kOutputPort0},
      {"ToggleOutputs", [](pcal9555::Config& c) { c.output_high_mask = 0x7FFF; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.ToggleOutputs(1).error; },
       chip::kOutputPort0},
  };
  for (const Case& k : cases) {
    BarePort port;
    pcal9555::Config c{};
    c.variant = Variant::Pcal9555a;
    k.set(c);
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    port.state.chip.PowerOn();
    const int before = port.state.transfers;
    CHECK_EQ(k.call(io), Error::Ok);
    CHECK_EQ(port.state.transfers - before, 1);
    CHECK(io.IsInitialized());
    const bool holds = port.state.chip.Pair(k.reg) == (k.reg == chip::kOutputPort0
                                                           ? io.GetConfig().output_high_mask
                                                           : io.GetConfig().interrupt_mask);
    if (!holds) {
      std::printf("    case %s\n", k.what);
    }
    CHECK(holds);
    CHECK_EQ(io.CheckForReset().error, Error::Ok);
  }
}

TEST_CASE("Every setter of a sentinel register reports the reset before writing") {
  struct Case {
    const char* what;
    void (*set)(pcal9555::Config&);
    Error (*call)(pcal9555::PCAL9555<BarePort>&);
  };
  const Case cases[] = {
      {"SetPull select",
       [](pcal9555::Config& c) {
         c.pull_up_mask = 0x00FF;
         c.output_high_mask = 0x7FFF; // a second register off its power-up value
       },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPull(1, Pull::Down).error; }},
      {"SetPull enable",
       [](pcal9555::Config& c) {
         c.pull_enable_mask = 0x00FF;
         c.output_high_mask = 0x7FFF; // a second register off its power-up value
       },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPull(1, Pull::None).error; }},
      {"SetInputLatch",
       [](pcal9555::Config& c) {
         c.input_latch_mask = 0x0100;
         c.output_high_mask = 0x7FFF; // a second register off its power-up value
       },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetInputLatch(1, true).error; }},
      {"SetDriveStrength",
       [](pcal9555::Config& c) {
         c.drive_strength_codes = 0x7FFF'FFFFU;
         c.output_high_mask = 0x7FFF; // a second register off its power-up value
       },
       [](pcal9555::PCAL9555<BarePort>& io) {
         return io.SetDriveStrength(1, DriveStrength::Half).error;
       }},
      {"SetOutputMode",
       [](pcal9555::Config& c) {
         c.port1_mode = OutputMode::OpenDrain;
         c.output_high_mask = 0x7FFF; // a second register off its power-up value
       },
       [](pcal9555::PCAL9555<BarePort>& io) {
         return io.SetOutputMode(Bank::Port0, OutputMode::OpenDrain).error;
       }},
      {"SetPolarityInversion",
       [](pcal9555::Config& c) {
         c.polarity_invert_mask = 0x0100;
         c.output_high_mask = 0x7FFF; // a second register off its power-up value
       },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPolarityInversion(1, true).error; }},
      {"ApplyConfig", [](pcal9555::Config& c) { c.input_mask = 0x00FF; },
       [](pcal9555::PCAL9555<BarePort>& io) {
         pcal9555::Config next = io.GetConfig();
         next.output_high_mask = 0;
         return io.ApplyConfig(next).error;
       }},
  };
  for (const Case& k : cases) {
    BarePort port;
    pcal9555::Config c{};
    c.variant = Variant::Pcal9555a;
    k.set(c);
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    const pcal9555::Config kept = io.GetConfig();
    port.state.chip.PowerOn();
    const int writes = port.state.chip.writes;
    const Error e = k.call(io);
    if (e != Error::DeviceReset) {
      std::printf("    case %s\n", k.what);
    }
    CHECK_EQ(e, Error::DeviceReset);
    CHECK_EQ(port.state.chip.writes, writes); // nothing written
    CHECK(!io.IsInitialized());
    CHECK_EQ(io.GetConfig().output_high_mask, kept.output_high_mask);
    CHECK_EQ(io.GetConfig().input_mask, kept.input_mask);
  }
}

TEST_CASE("A lost write to the sentinel register reads as a reset; the recovery is the same") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, [] {
    pcal9555::Config c{};
    c.variant = Variant::Pcal9555a;
    return c;
  }());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.drop_writes = 2;
  REQUIRE(io.SetInterruptEnabled(1, true).ok()); // acknowledged, not kept
  CHECK_EQ(io.CheckForReset().error, Error::DeviceReset);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Pair(chip::kInterruptMask0), 0xFFFEU);
}

TEST_CASE("The auditor's sequence: a reset, then SetDirection on an unrelated pin") {
  // Pins 0..7 low, open-drain, on loads; the supply dips; SetDirection(pin 8). Before the fix the
  // call wrote only the configuration pair, so pins 0..7 became push-pull outputs at the reset
  // latch, high, and Verify::ReadBack passed.
  for (const bool verify : {false, true}) {
    BarePort port;
    port.state.chip.SetExternal(0x0000);
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, LoadsOff());
    REQUIRE(io.EnsureInitialized().ok());
    port.state.chip.PowerOn();
    const auto r = verify ? io.SetDirection(1U << 8U, Direction::Output, kReadBack)
                          : io.SetDirection(1U << 8U, Direction::Output);
    CHECK_EQ(r.error, Error::DeviceReset);
    CHECK_EQ(port.state.chip.Levels() & 0x00FFU, 0x0000U); // pins 0..7 not driven high
    CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0) & 0x00FFU, 0x00FFU);
  }
}

// --- P2: the input read does not lose the interrupt status ---------------------------------------

TEST_CASE("With interrupts unmasked, an input read first latches the interrupt status") {
  BarePort port;
  pcal9555::Config c{};
  c.variant = Variant::Pcal9555a;
  c.interrupt_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.SetExternal(static_cast<uint16_t>(0xFFFFU & ~(1U << 4U))); // pin 4 falls
  REQUIRE(port.state.chip.InterruptAsserted());
  int before = port.state.transfers;
  const auto in = io.ReadPin(0);
  REQUIRE(in.ok());
  CHECK_EQ(port.state.transfers - before, 2); // the status pair, then the input port
  CHECK(!port.state.chip.InterruptAsserted());
  CHECK_EQ(io.GetLatchedInterrupts(), 1U << 4U);
  CHECK_EQ(io.ReadInterruptStatus().value, 0U); // the chip's status is gone, the latch kept it
  CHECK_EQ(io.GetLatchedInterrupts(), 1U << 4U);
  // ReadInputs too.
  io.ClearLatchedInterrupts();
  port.state.chip.SetExternal(0xFFFF); // pin 4 rises
  before = port.state.transfers;
  const auto all = io.ReadInputs();
  REQUIRE(all.ok());
  CHECK_EQ(all.value, 0xFFFFU);
  CHECK_EQ(port.state.transfers - before, 2);
  CHECK_EQ(io.GetLatchedInterrupts(), 1U << 4U);
  // A failed status read is the call's error, and the inputs are not read (nothing is cleared).
  port.state.chip.SetExternal(static_cast<uint16_t>(0xFFFFU & ~(1U << 5U)));
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.ReadInputs().error, Error::Bus);
  CHECK(port.state.chip.InterruptAsserted());
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.ReadPin(5).error, Error::Bus);
  CHECK(port.state.chip.InterruptAsserted());
}

TEST_CASE("With every input masked, or on a PCA9555, an input read is one transfer") {
  {
    BarePort port;
    pcal9555::Config c{};
    c.variant = Variant::Pcal9555a;
    c.interrupt_mask = 0x00FF; // pins 8..15 unmasked, but outputs: no input can interrupt
    c.input_mask = 0x00FF;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    const int before = port.state.transfers;
    REQUIRE(io.ReadInputs().ok());
    REQUIRE(io.ReadPin(3).ok());
    CHECK_EQ(port.state.transfers - before, 2);
  }
  {
    BarePort port;
    MakePca9555(port);
    pcal9555::PCAL9555<BarePort> io(port);
    REQUIRE(io.EnsureInitialized().ok());
    const int before = port.state.transfers;
    REQUIRE(io.ReadInputs().ok());
    CHECK_EQ(port.state.transfers - before, 1);
  }
}
