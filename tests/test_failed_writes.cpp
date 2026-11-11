/**
 * @file test_failed_writes.cpp
 * @brief A write whose transfer failed may still have reached the chip.
 *
 * I2C reports a failure after the bytes have been clocked in as easily as before: a NACK of the
 * last data byte, an ACK lost to noise, a controller timeout after STOP. The model's
 * `stored_then_fail` stores the bytes and reports the failure. The driver then holds the riskier
 * of the old and the asked value (an output rather than an input, the written level, the stronger
 * drive, push-pull) and marks the register unknown; the next call that builds on it reads it
 * first, so no setter computes from a stale copy. An address NACK (`NoDevice`) proves nothing
 * arrived, and the copy stays.
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
using pcal9555_test::FullPort;
namespace chip = pcal9555_test::chip;

namespace {

constexpr auto kReadBack = pcal9555::Verify::ReadBack;

pcal9555::Config Pcal() {
  pcal9555::Config c{};
  c.variant = Variant::Pcal9555a;
  return c;
}

} // namespace

// --- Configuration pair: an output rather than an input -----------------------------------------

TEST_CASE("A failed direction write that arrived: the pin counts as an output, read before reuse") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers + 1; // output pair, then configuration pair
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Output).error, Error::Bus);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFEU); // it arrived
  CHECK_EQ(io.GetConfig().input_mask, 0xFFFEU);                // and the copy says so
  const int before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(1), Direction::Output).ok());
  // The reset check skips the unknown pair (and finds nothing else to read); the driver reads the
  // pair into the copy, then writes the output and configuration pairs.
  CHECK_EQ(port.state.transfers - before, 1 + 2);
  CHECK_EQ(io.GetConfig().input_mask, 0xFFFCU);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFCU);
}

TEST_CASE("A failed request to make an output an input keeps it an output in the copy") {
  for (const bool arrived : {false, true}) {
    BarePort port;
    pcal9555::Config c{};
    c.input_mask = 0xFFFC; // pins 0, 1 outputs
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    // Pin 1 stays an output: the check read, then the configuration pair first.
    (arrived ? port.state.stored_then_fail : port.state.fail_transfer) = port.state.transfers + 1;
    CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Input).error, Error::Bus);
    CHECK_EQ(io.GetConfig().input_mask, 0xFFFCU); // the riskier value: still an output
    CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), arrived ? 0xFFFDU : 0xFFFCU);
    // A later call builds on what the chip holds, not on either guess.
    REQUIRE(io.SetDirection(pcal9555::PinMask(2), Direction::Output).ok());
    const uint16_t expected = arrived ? 0xFFF9U : 0xFFF8U;
    CHECK_EQ(io.GetConfig().input_mask, expected);
    CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), expected);
  }
}

TEST_CASE("A failed write whose value is the power-up value reads as a reset (accepted)") {
  // The rule is simple: an unknown register back at its power-up value is a reset. When the
  // failed write itself asked for the power-up value (here: every pin an input) and arrived, the
  // read cannot tell it from a reset. The cost is one false DeviceReset: nothing is written,
  // EnsureInitialized() writes the copy again, and nothing stays stuck.
  BarePort port;
  pcal9555::Config c{};
  c.input_mask = 0xFFFE;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers; // the only write of a full release
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Input).error, Error::Bus);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFFU); // the release arrived
  const int writes = port.state.chip.writes;
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(1), Direction::Output).error, Error::DeviceReset);
  CHECK_EQ(port.state.chip.writes, writes); // nothing written
  REQUIRE(io.EnsureInitialized().ok());     // the copy (pin 0 an output, the riskier value)
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFEU);
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Input).ok()); // and the retry works
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFFU);
}

// --- Output pair: the written level -------------------------------------------------------------

TEST_CASE("A failed output write: the copy holds the written level and Toggle reads first") {
  for (const bool arrived : {false, true}) {
    BarePort port;
    pcal9555::Config c{};
    c.output_high_mask = 0x7FFF; // off its power-up value, so neither outcome reads as a reset
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    (arrived ? port.state.stored_then_fail : port.state.fail_transfer) = port.state.transfers;
    CHECK_EQ(io.SetOutputs(pcal9555::PinMask(0), false).error, Error::Bus);
    CHECK_EQ(io.GetConfig().output_high_mask, 0x7FFEU);
    const uint16_t held = arrived ? 0x7FFEU : 0x7FFFU;
    CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), held);
    // ToggleOutputs is relative: built on a stale copy it would invert the wrong level.
    const int before = port.state.transfers;
    REQUIRE(io.ToggleOutputs(pcal9555::PinMask(0)).ok());
    CHECK_EQ(port.state.transfers - before, 2); // read the pair, then write it
    const auto toggled = static_cast<uint16_t>(held ^ 0x0001U);
    CHECK_EQ(io.GetConfig().output_high_mask, toggled);
    CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), toggled);
    // Known again: the next call is one write.
    const int after = port.state.transfers;
    REQUIRE(io.SetOutputs(pcal9555::PinMask(1), false).ok());
    CHECK_EQ(port.state.transfers - after, 1);
  }
}

TEST_CASE("An absolute output write after a failure needs no read") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers;
  CHECK_EQ(io.WriteOutputs(0x00FF).error, Error::Bus);
  const int before = port.state.transfers;
  REQUIRE(io.WriteOutputs(0x0F0F).ok());
  CHECK_EQ(port.state.transfers - before, 1);
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), 0x0F0FU);
  // And a successful write made it known: SetOutputs is one write again.
  const int after = port.state.transfers;
  REQUIRE(io.SetOutputs(1, true).ok());
  CHECK_EQ(port.state.transfers - after, 1);
}

TEST_CASE("A failed read-back after an acknowledged write leaves the written value, unknown") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1; // the write, then the read-back
  CHECK_EQ(io.SetOutputs(pcal9555::PinMask(3), false, kReadBack).error, Error::Bus);
  CHECK_EQ(io.GetConfig().output_high_mask, 0xFFF7U);
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), 0xFFF7U);
  // A read-back call makes it known again; the next relative call then needs no read.
  CHECK_EQ(io.ReadOutputLatch().value, 0xFFF7U);
  const int before = port.state.transfers;
  REQUIRE(io.ToggleOutputs(pcal9555::PinMask(3)).ok());
  CHECK_EQ(port.state.transfers - before, 1);
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), 0xFFFFU);
}

// --- Drive strength: the stronger code ----------------------------------------------------------

TEST_CASE("A failed drive strength write: the copy holds the stronger code, read before reuse") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers; // the 0x40 pair arrives
  CHECK_EQ(io.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Quarter).error, Error::Bus);
  CHECK_EQ(port.state.chip.Pair(chip::kDriveStrength0), 0x0000U);
  CHECK_EQ(port.state.chip.Pair(chip::kDriveStrength2), 0xFFFFU); // not sent
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFF'FFFFU);    // full: the stronger of both
  const int before = port.state.transfers;
  REQUIRE(io.SetDriveStrength(pcal9555::PinMask(0), DriveStrength::Half).ok());
  CHECK_EQ(port.state.transfers - before, 1 + 2); // read 0x40, then write both pairs
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFF'0001U);
  CHECK_EQ(port.state.chip.Pair(chip::kDriveStrength0), 0x0001U);
  // The second pair failing (not arrived) leaves pins 8..15 at the stronger code too.
  port.state.fail_transfer = port.state.transfers + 1;
  CHECK_EQ(io.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Quarter).error, Error::Bus);
  CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFF'0000U);
}

// --- 0x4F: push-pull rather than open-drain -----------------------------------------------------

TEST_CASE("A failed output mode write: the copy says push-pull, read before reuse") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers;
  CHECK_EQ(io.SetOutputMode(Bank::Port0, OutputMode::OpenDrain).error, Error::Bus);
  CHECK_EQ(port.state.chip.Register(chip::kOutputPortConfig), chip::kOden0);
  CHECK(io.GetConfig().port0_mode == OutputMode::PushPull);
  const int before = port.state.transfers;
  REQUIRE(io.SetOutputMode(Bank::Port1, OutputMode::OpenDrain).ok());
  CHECK_EQ(port.state.transfers - before, 2); // read 0x4F, write it
  CHECK(io.GetConfig().port0_mode == OutputMode::OpenDrain);
  CHECK_EQ(port.state.chip.Register(chip::kOutputPortConfig), chip::kOden0 | chip::kOden1);
  // With Verify::ReadBack, a failed read-back of an arrived byte is unknown too.
  port.state.fail_transfer = port.state.transfers + 1;
  CHECK_EQ(io.SetOutputMode(Bank::Port1, OutputMode::PushPull, kReadBack).error, Error::Bus);
  CHECK(io.GetConfig().port1_mode == OutputMode::PushPull);
  // SetDirection writes 0x4F from the copy: it reads the unknown register first.
  const int dir = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Output).ok());
  CHECK_EQ(port.state.transfers - dir, 1 + 3); // read 0x4F (the reset check has none), 3 writes
  CHECK_EQ(port.state.chip.Register(chip::kOutputPortConfig), chip::kOden0);
}

// --- The other pairs: the written value ---------------------------------------------------------

TEST_CASE("Every other pair takes the written value after a failed write, read before reuse") {
  struct Case {
    const char* what;
    Error (*fail)(pcal9555::PCAL9555<BarePort>&);
    Error (*next)(pcal9555::PCAL9555<BarePort>&);
    uint16_t (*copy)(const pcal9555::Config&);
    void (*start)(pcal9555::Config&);
    uint16_t after;
    uint8_t reg;
  };
  const Case cases[] = {
      {"polarity",
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPolarityInversion(1, true).error; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPolarityInversion(2, true).error; },
       [](const pcal9555::Config& c) { return c.polarity_invert_mask; },
       [](pcal9555::Config& c) { c.polarity_invert_mask = 0x8000; }, 0x8002, chip::kPolarityPort0},
      {"input latch",
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetInputLatch(1, true).error; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetInputLatch(2, true).error; },
       [](const pcal9555::Config& c) { return c.input_latch_mask; },
       [](pcal9555::Config& c) { c.input_latch_mask = 0x8000; }, 0x8002, chip::kInputLatch0},
      {"pull enable",
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPull(1, Pull::None).error; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetPull(2, Pull::None).error; },
       [](const pcal9555::Config& c) { return c.pull_enable_mask; },
       [](pcal9555::Config& c) { c.pull_enable_mask = 0x7FFF; }, 0x7FFD, chip::kPullEnable0},
      {"interrupt mask",
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetInterruptEnabled(1, true).error; },
       [](pcal9555::PCAL9555<BarePort>& io) { return io.SetInterruptEnabled(2, true).error; },
       [](const pcal9555::Config& c) { return c.interrupt_mask; },
       [](pcal9555::Config& c) { c.interrupt_mask = 0x7FFF; }, 0x7FFD, chip::kInterruptMask0},
  };
  for (const Case& k : cases) {
    BarePort port;
    pcal9555::Config c = Pcal();
    k.start(c); // off its power-up value, so the read is not taken as a reset
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    port.state.fail_transfer = port.state.transfers; // nothing arrives this time
    CHECK_EQ(k.fail(io), Error::Bus);
    const int before = port.state.transfers;
    CHECK_EQ(k.next(io), Error::Ok);
    // The read of the unknown pair, then the write: built on what the chip held (pin 0 unchanged).
    const bool ok = (port.state.transfers - before == 2) && (k.copy(io.GetConfig()) == k.after) &&
                    (port.state.chip.Pair(k.reg) == k.after);
    if (!ok) {
      std::printf("    case %s\n", k.what);
    }
    CHECK(ok);
  }
  // Pull selection: SetPull(Down) writes the selection pair first.
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers;
  CHECK_EQ(io.SetPull(1, Pull::Down).error, Error::Bus);
  CHECK_EQ(io.GetConfig().pull_up_mask, 0xFFFEU);
  const int before = port.state.transfers;
  REQUIRE(io.SetPull(2, Pull::Down).ok());
  CHECK_EQ(port.state.transfers - before, 1 + 2);
  CHECK_EQ(port.state.chip.Pair(chip::kPullSelect0), 0xFFFCU);
}

// --- What an unknown register does to the other checks ------------------------------------------

TEST_CASE("The reset check never reads an unknown register") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.interrupt_mask = 0xFFFE; // the sentinel
  c.input_mask = 0x00FF;     // the next candidate
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers + 1; // the check read passes, the write fails
  CHECK_EQ(io.SetInterruptEnabled(0x0002, true).error, Error::Bus);
  port.state.chip.PowerOn();
  CHECK_EQ(io.CheckForReset().error, Error::DeviceReset); // found through the configuration pair
}

TEST_CASE("A failed read of an unknown register stops the call before any write") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers;
  CHECK_EQ(io.SetOutputs(1, false).error, Error::Bus);
  const int writes = port.state.chip.writes;
  port.state.fail_transfer = port.state.transfers; // the read of the output pair
  CHECK_EQ(io.ToggleOutputs(1).error, Error::Bus);
  CHECK_EQ(port.state.chip.writes, writes);
  REQUIRE(io.ToggleOutputs(1).ok()); // still unknown: it reads again, then writes
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), 0xFFFFU);
}

TEST_CASE("An address NACK sent nothing: the copy stays known") {
  FullPort port;
  pcal9555::PCAL9555<FullPort> io(port);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.address = 0x27; // the chip stops answering
  CHECK_EQ(io.SetOutputs(1, false).error, Error::NoDevice);
  CHECK_EQ(io.GetConfig().output_high_mask, 0xFFFFU);
  port.state.address = pcal9555::kI2cAddress;
  const int before = port.state.transfers;
  REQUIRE(io.ToggleOutputs(1).ok());
  CHECK_EQ(port.state.transfers - before, 1);
}

TEST_CASE("ApplyConfig and EnsureInitialized make every register known again") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.output_high_mask = 0x0FF0;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.SetOutputs(1, false).error, Error::Bus);
  REQUIRE(io.ApplyConfig(io.GetConfig()).ok());
  int before = port.state.transfers;
  REQUIRE(io.ToggleOutputs(1).ok());
  CHECK_EQ(port.state.transfers - before, 1);
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.SetOutputs(2, false).error, Error::Bus);
  REQUIRE(io.Deinitialize().ok());
  REQUIRE(io.EnsureInitialized().ok());
  before = port.state.transfers;
  REQUIRE(io.ToggleOutputs(2).ok());
  CHECK_EQ(port.state.transfers - before, 1);
}

TEST_CASE("Each read before reuse that fails is the call's error, and nothing is written") {
  // SetDirection: the unknown configuration pair cannot be read.
  {
    BarePort port;
    pcal9555::PCAL9555<BarePort> io(port);
    REQUIRE(io.EnsureInitialized().ok());
    port.state.stored_then_fail = port.state.transfers + 1;
    CHECK_EQ(io.SetDirection(1, Direction::Output).error, Error::Bus);
    const int writes = port.state.chip.writes;
    port.state.fail_transfer = port.state.transfers;
    CHECK_EQ(io.SetDirection(2, Direction::Output).error, Error::Bus);
    CHECK_EQ(port.state.chip.writes, writes);
  }
  // SetOutputMode: the unknown 0x4F cannot be read.
  {
    BarePort port;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
    REQUIRE(io.EnsureInitialized().ok());
    port.state.stored_then_fail = port.state.transfers;
    CHECK_EQ(io.SetOutputMode(Bank::Port0, OutputMode::OpenDrain).error, Error::Bus);
    const int writes = port.state.chip.writes;
    port.state.fail_transfer = port.state.transfers;
    CHECK_EQ(io.SetOutputMode(Bank::Port1, OutputMode::OpenDrain).error, Error::Bus);
    CHECK_EQ(port.state.chip.writes, writes);
  }
  // The second drive strength pair, unknown, is read into pins 8..15 of the copy.
  {
    BarePort port;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, Pcal());
    REQUIRE(io.EnsureInitialized().ok());
    port.state.stored_then_fail = port.state.transfers + 1;
    CHECK_EQ(io.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Quarter).error, Error::Bus);
    CHECK_EQ(io.GetConfig().drive_strength_codes, 0xFFFF'0000U); // pins 8..15: the stronger
    REQUIRE(io.SetDriveStrength(pcal9555::PinMask(15), DriveStrength::Full).ok());
    CHECK_EQ(io.GetConfig().drive_strength_codes, 0xC000'0000U); // read back 0x0000, pin 15 full
    CHECK_EQ(port.state.chip.Pair(chip::kDriveStrength2), 0xC000U);
  }
}

TEST_CASE("The reset check before a setter can fail too: the setter sends nothing") {
  // Two registers off their power-up values; the polarity pair is the one the check reads, so
  // SetPolarityInversion checks before it rewrites it.
  BarePort port;
  pcal9555::Config c{};
  c.polarity_invert_mask = 0x0100;
  c.output_high_mask = 0x7FFF;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  const int writes = port.state.chip.writes;
  port.state.fail_transfer = port.state.transfers; // the check read
  CHECK_EQ(io.SetPolarityInversion(0x0200, true).error, Error::Bus);
  CHECK_EQ(port.state.chip.writes, writes);
  CHECK(io.IsInitialized());
}

TEST_CASE("SetOutputs and WriteOutputs check for a reset when the output pair is the evidence") {
  for (const bool absolute : {false, true}) {
    BarePort port;
    pcal9555::Config c{};
    c.output_high_mask = 0x7FFF;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    // A second register off its power-up value that no output call writes: an unknown one.
    port.state.fail_transfer = port.state.transfers;
    CHECK_EQ(io.SetPolarityInversion(1, true).error, Error::Bus);
    port.state.chip.PowerOn();
    const Error e = absolute ? io.WriteOutputs(0x00FF).error : io.SetOutputs(1, false).error;
    CHECK_EQ(e, Error::DeviceReset);
  }
}

// --- A reset while registers are unknown --------------------------------------------------------

TEST_CASE("A failed ApplyConfig, then a power-on reset: the reset is found (review Q1)") {
  // Pins 0..7 open-drain outputs driven low; ApplyConfig fails part-way; the supply dips. Before
  // the fix every register was unknown, the check read nothing, and SetDirection(8) adopted the
  // reset values: pin 8 push-pull high, pins 0..7 released.
  BarePort port;
  pcal9555::Config c = Pcal();
  c.input_mask = 0xFF00;
  c.output_high_mask = 0x0000;
  c.port0_mode = OutputMode::OpenDrain;
  c.port1_mode = OutputMode::OpenDrain;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  pcal9555::Config c2 = c;
  c2.polarity_invert_mask = 0x0001;
  port.state.fail_transfer = port.state.transfers + 3; // the check read, then the third write
  CHECK_EQ(io.ApplyConfig(c2).error, Error::Bus);
  // Only the write that failed is unknown: the ones before it were acknowledged, the rest unsent.
  CHECK_EQ(io.GetConfig().input_mask, 0xFF00U);
  CHECK_EQ(io.GetConfig().polarity_invert_mask, 0x0000U); // never sent
  port.state.fail_transfer = -1;
  port.state.chip.PowerOn();
  CHECK_EQ(io.CheckForReset().error, Error::DeviceReset);
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(8), Direction::Output).error, Error::NotInitialized);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), chip::kConfigReset); // nothing turned on
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFF00U);
  CHECK_EQ(port.state.chip.Pair(chip::kOutputPort0), 0x0000U);
}

TEST_CASE("Every changed register unknown, then a reset: the read of it finds the reset") {
  // Only the direction differs from power-up, and its write failed: no sentinel is left, so the
  // check reads the unknown configuration pair, and 0xFFFF, its power-up value, is a reset.
  for (const bool arrived : {false, true}) {
    BarePort port;
    pcal9555::Config c{};
    c.input_mask = 0xFF00;
    pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
    REQUIRE(io.EnsureInitialized().ok());
    // The check read, the output pair, then the configuration pair.
    (arrived ? port.state.stored_then_fail : port.state.fail_transfer) = port.state.transfers + 2;
    CHECK_EQ(io.SetDirection(pcal9555::PinMask(8), Direction::Output).error, Error::Bus);
    // Without a reset the read is not the power-up value, and the copy follows the chip.
    if (!arrived) {
      const int before = port.state.transfers;
      CHECK_EQ(io.CheckForReset().error, Error::Ok);
      CHECK_EQ(port.state.transfers - before, 1);
      CHECK_EQ(io.GetConfig().input_mask, 0xFF00U);
      continue;
    }
    port.state.chip.PowerOn();
    CHECK_EQ(io.CheckForReset().error, Error::DeviceReset);
    CHECK(!io.IsInitialized());
  }
}

TEST_CASE("Two failed writes, then a reset: the unknown register at power-up is a reset") {
  // However many writes failed, an unknown register read back at its power-up value is a reset.
  BarePort port;
  pcal9555::Config c = Pcal();
  c.output_high_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  // The output pair is the only register off power-up: no check before either write.
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.WriteOutputs(0x00FF).error, Error::Bus);
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.WriteOutputs(0x0F0F).error, Error::Bus);
  port.state.chip.PowerOn();
  CHECK_EQ(io.CheckForReset().error, Error::DeviceReset);
}

TEST_CASE("A read-back call does not adopt a power-up value into an unknown register") {
  BarePort port;
  pcal9555::Config c{};
  c.input_mask = 0x00FF; // the sentinel
  c.output_high_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers; // the output write (its check is skipped)
  CHECK_EQ(io.SetOutputs(1, true).error, Error::Bus);
  port.state.chip.PowerOn();
  CHECK_EQ(io.ReadOutputLatch().value, 0xFFFFU);      // what the chip holds
  CHECK_EQ(io.GetConfig().output_high_mask, 0x0001U); // not adopted: still unknown
  CHECK_EQ(io.CheckForReset().error, Error::DeviceReset);
}

// --- Releasing pins (review Q2) -----------------------------------------------------------------

TEST_CASE("Releasing the last output is one write, with nothing before it that can fail") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.input_mask = 0xFFFE;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  int before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Input).ok());
  CHECK_EQ(port.state.transfers - before, 1);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFFU);
  // After a reset too: every pin an input is safe whatever the chip went through.
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Output).ok());
  port.state.chip.PowerOn();
  before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Input).ok());
  CHECK_EQ(port.state.transfers - before, 1);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFFU);
  // A failure is the call's error; the retry is again one write.
  REQUIRE(io.ApplyConfig(c).ok());
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Input).error, Error::Bus);
  before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Input).ok());
  CHECK_EQ(port.state.transfers - before, 1);
}

TEST_CASE("Releasing a pin while others stay outputs writes the configuration pair first") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.input_mask = 0xFFFC; // pins 0, 1 outputs
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  // The check read, the configuration pair, then 0x4F, which fails: pin 0 is released anyway.
  port.state.fail_transfer = port.state.transfers + 2;
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Input).error, Error::Bus);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFDU);
  CHECK_EQ(io.GetConfig().input_mask, 0xFFFDU);
  // A clean release: the check, the configuration pair, 0x4F, the output pair.
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Output).ok());
  const int before = port.state.transfers;
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Input).ok());
  CHECK_EQ(port.state.transfers - before, 4);
  // A failing check read: nothing written (pin 1 stays an output, so the check is needed).
  REQUIRE(io.SetDirection(pcal9555::PinMask(0), Direction::Output).ok());
  const int writes = port.state.chip.writes;
  port.state.fail_transfer = port.state.transfers;
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Input).error, Error::Bus);
  CHECK_EQ(port.state.chip.writes, writes);
}

TEST_CASE("A failed read of an unknown register stops SetDirection and ApplyConfig") {
  // The sentinel (polarity) is known and reads fine; the unknown output pair cannot be read.
  BarePort port;
  pcal9555::Config c{};
  c.polarity_invert_mask = 0x0100;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.fail_transfer = port.state.transfers; // the output write (polarity is the sentinel)
  CHECK_EQ(io.SetOutputs(1, false).error, Error::Bus);
  const int writes = port.state.chip.writes;
  port.state.fail_transfer = port.state.transfers + 1; // the check read passes, the resolve fails
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Output).error, Error::Bus);
  port.state.fail_transfer = port.state.transfers + 1;
  CHECK_EQ(io.ApplyConfig(io.GetConfig()).error, Error::Bus);
  CHECK_EQ(port.state.chip.writes, writes);
  CHECK(io.IsInitialized());
}

TEST_CASE("Releasing a pin while others stay outputs: a failed configuration write is unknown") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.input_mask = 0xFFFC;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.stored_then_fail = port.state.transfers + 1; // the check read, then the config pair
  CHECK_EQ(io.SetDirection(pcal9555::PinMask(0), Direction::Input).error, Error::Bus);
  CHECK_EQ(port.state.chip.Pair(chip::kConfigPort0), 0xFFFDU); // it arrived
  CHECK_EQ(io.GetConfig().input_mask, 0xFFFCU);                // the copy: still an output
  CHECK_EQ(io.ReadInputMask().value, 0xFFFDU);                 // a read makes it known
  CHECK_EQ(io.GetConfig().input_mask, 0xFFFDU);
}
