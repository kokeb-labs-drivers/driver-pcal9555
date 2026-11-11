/**
 * @file test_diagnostics.cpp
 * @brief An absent chip (`NoDevice`), the bus line modes, the optional-hook concepts, raw register
 * access, the last values read and the interrupt latch.
 */
#include <cstdint>
#include <utility>

#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"
#include "test_harness.hpp"

using pcal9555::Error;
using pcal9555::I2cFailure;
using pcal9555_test::BarePort;
using pcal9555_test::FullPort;
using pcal9555_test::Line;
using pcal9555_test::Pcal9555Model;
namespace reg = pcal9555::reg;
namespace detail = pcal9555::detail;

namespace {

pcal9555::Config Pcal() {
  pcal9555::Config c{};
  c.variant = pcal9555::Variant::Pcal9555a;
  return c;
}

/// A port whose GetLastFailure() says it cannot tell: the driver must fall back to Bus.
class UnsurePort : public pcal9555_test::MockPortBase<UnsurePort> {
public:
  bool GetLastFailure(I2cFailure& failure) noexcept {
    failure = I2cFailure::AddressNack; // must be ignored: the hook returned false
    return false;
  }
};

/// A hook with the wrong return type, for the Declares/Has pair.
class VoidFailurePort : public pcal9555_test::MockPortBase<VoidFailurePort> {
public:
  void GetLastFailure(I2cFailure& failure) noexcept {
    failure = I2cFailure::Other;
  }
};

// The optional-hook concepts (KB-CPP20-007): Declares tests the name, Has the exact signature.
static_assert(detail::HasGetLastFailure<FullPort> && detail::HasLog<FullPort>);
static_assert(!detail::DeclaresGetLastFailure<BarePort> && !detail::DeclaresLog<BarePort>);
static_assert(detail::DeclaresGetLastFailure<VoidFailurePort> &&
              !detail::HasGetLastFailure<VoidFailurePort>);
static_assert(detail::HasGetLastFailure<UnsurePort>);
// GetConfig() on a temporary driver is deleted: the reference would dangle (KB-CPP20-044).
template <typename D>
concept HasGetConfig = requires { std::declval<D>().GetConfig(); };
static_assert(!HasGetConfig<const pcal9555::PCAL9555<BarePort>&&>);
static_assert(HasGetConfig<const pcal9555::PCAL9555<BarePort>&>);

} // namespace

// --- NoDevice
// --------------------------------------------------------------------------------------

TEST_CASE("Nothing at the address is NoDevice with GetLastFailure, Bus without it") {
  {
    FullPort port;
    port.state.address = 0x21;
    pcal9555::PCAL9555<FullPort> io(port, 0x20);
    CHECK_EQ(io.EnsureInitialized().error, Error::NoDevice);
    CHECK(!io.IsInitialized());
    CHECK_EQ(port.state.transfers, 1); // the presence read, nothing written
    CHECK_EQ(port.failure_calls, 1);
    CHECK(port.error_lines > 0);
  }
  {
    BarePort port;
    port.state.address = 0x21;
    pcal9555::PCAL9555<BarePort> io(port, 0x20);
    CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  }
  {
    UnsurePort port;
    port.state.address = 0x21;
    pcal9555::PCAL9555<UnsurePort> io(port, 0x20);
    CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  }
}

TEST_CASE("A chip that disappears after the start is NoDevice on every read and write") {
  FullPort port;
  pcal9555::PCAL9555<FullPort> io(port, pcal9555::kI2cAddress, Pcal());
  REQUIRE(io.EnsureInitialized().ok());
  REQUIRE(io.ReadOutputLatch().ok());
  const uint16_t inputs = io.GetLastInputs();
  const uint16_t output = io.GetLastOutput();
  const uint16_t latched = io.GetLatchedInterrupts();
  port.state.address = 0x27; // the chip is gone
  CHECK_EQ(io.ReadInputs().error, Error::NoDevice);
  CHECK_EQ(io.ReadInputs().value, 0U);
  CHECK_EQ(io.ReadPin(3).error, Error::NoDevice);
  CHECK_EQ(io.ReadRegister(reg::kInputPort0).error, Error::NoDevice);
  CHECK_EQ(io.ReadInterruptStatus().error, Error::NoDevice);
  CHECK_EQ(io.ReadOutputLatch().error, Error::NoDevice);
  // A failed read changes none of the last values: nothing stale or invented is kept.
  CHECK_EQ(io.GetLastInputs(), inputs);
  CHECK_EQ(io.GetLastOutput(), output);
  CHECK_EQ(io.GetLatchedInterrupts(), latched);
  CHECK_EQ(io.SetOutputs(1, false).error, Error::NoDevice);
  CHECK_EQ(io.GetConfig().output_high_mask, 0xFFFFU); // the copy follows the chip only (KB-CPP-120)
  CHECK_EQ(io.SetOutputMode(pcal9555::Bank::Port1, pcal9555::OutputMode::OpenDrain).error,
           Error::NoDevice);
  CHECK(io.GetConfig().port1_mode == pcal9555::OutputMode::PushPull);
  CHECK_EQ(io.ApplyConfig(Pcal()).error, Error::NoDevice);
  CHECK_EQ(io.Deinitialize().error, Error::NoDevice);
  CHECK(!io.IsInitialized());
}

TEST_CASE("A command byte the part NACKs is Bus, not NoDevice") {
  // A PCA9555 configured as a PCAL9555A: the address is acknowledged, the Agile command byte is
  // not.
  FullPort port;
  port.state.chip.variant = Pcal9555Model::Part::Pca9555;
  port.state.chip.PowerOn();
  pcal9555::PCAL9555<FullPort> io(port, pcal9555::kI2cAddress, Pcal());
  CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  CHECK(port.state.last_failure == I2cFailure::DataNack);
  CHECK_EQ(port.state.chip.writes, 0);
}

// --- Line modes (KB-CPP-116, KB-CPP-117)
// ------------------------------------------------------------

TEST_CASE("A floating, stuck or deaf bus never passes the start") {
  {
    FullPort port;
    port.state.line = Line::FloatHigh;
    pcal9555::PCAL9555<FullPort> io(port, pcal9555::kI2cAddress, Pcal());
    CHECK_EQ(io.EnsureInitialized().error, Error::NoDevice);
  }
  {
    BarePort port;
    port.state.line = Line::FloatHigh;
    pcal9555::PCAL9555<BarePort> io(port);
    CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  }
  {
    FullPort port;
    port.state.line = Line::StuckLow;
    pcal9555::PCAL9555<FullPort> io(port);
    CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
  }
  // A deaf device fails whatever the Config, even one whose registers match what it reads.
  pcal9555::Config all_ones{};
  all_ones.polarity_invert_mask = 0xFFFF;
  pcal9555::Config all_zeros{};
  all_zeros.output_high_mask = 0;
  all_zeros.input_mask = 0;
  for (const uint8_t fixed : {uint8_t{0xFF}, uint8_t{0x00}}) {
    for (const pcal9555::Config& c : {pcal9555::Config{}, Pcal(), all_ones, all_zeros}) {
      FullPort port; // acknowledges everything, stores nothing, reads `fixed`
      port.state.chip.deaf = true;
      port.state.chip.deaf_value = fixed;
      pcal9555::PCAL9555<FullPort> io(port, pcal9555::kI2cAddress, c);
      const Error e = io.EnsureInitialized().error;
      CHECK(e == Error::DeviceIdMismatch);
      CHECK(!io.IsInitialized());
    }
  }
}

TEST_CASE(
    "After the start a stuck or floating line is an error on every read path, never a value") {
  for (const Line line : {Line::FloatHigh, Line::StuckLow}) {
    FullPort port;
    pcal9555::PCAL9555<FullPort> io(port, pcal9555::kI2cAddress, Pcal());
    REQUIRE(io.EnsureInitialized().ok());
    port.state.line = line;
    const Error expected = line == Line::FloatHigh ? Error::NoDevice : Error::Bus;
    CHECK_EQ(io.ReadInputs().error, expected);
    CHECK_EQ(io.ReadPin(0).error, expected);
    CHECK_EQ(io.ReadInterruptStatus().error, expected);
    CHECK_EQ(io.ReadOutputLatch().error, expected);
    CHECK_EQ(io.ReadInputMask().error, expected);
    CHECK_EQ(io.ReadPolarityInversion().error, expected);
    CHECK_EQ(io.ReadRegister(reg::kOutputPortConfig).error, expected);
  }
}

// --- Raw access and the last values read (KB-CPP-129)
// ------------------------------------------------

TEST_CASE("ReadRegister reads one register once, after the start, for the configured part") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port); // configured as a PCA9555
  CHECK_EQ(io.ReadRegister(reg::kOutputPort0).error, Error::NotInitialized);
  REQUIRE(io.EnsureInitialized().ok());
  int before = port.state.transfers;
  const auto r = io.ReadRegister(reg::kConfigPort0 + 1U);
  CHECK(r.ok());
  CHECK_EQ(r.value, 0xFFU);
  CHECK_EQ(port.state.transfers - before, 1);
  CHECK_EQ(port.state.max_len, 2U);
  before = port.state.transfers;
  for (const uint8_t command : {uint8_t{0x08}, uint8_t{0x40}, uint8_t{0x4C}, uint8_t{0x4F}}) {
    CHECK_EQ(io.ReadRegister(command).error, Error::InvalidArgument); // not on a PCA9555
  }
  CHECK_EQ(port.state.transfers, before);

  BarePort port2;
  pcal9555::PCAL9555<BarePort> agile(port2, pcal9555::kI2cAddress, Pcal());
  REQUIRE(agile.EnsureInitialized().ok());
  CHECK_EQ(agile.ReadRegister(reg::kOutputPortConfig).value, 0x00U);
  CHECK_EQ(agile.ReadRegister(reg::kDriveStrength2 + 1U).value, 0xFFU);
  before = port2.state.transfers;
  CHECK_EQ(agile.ReadRegister(uint8_t{0x4E}).error, Error::InvalidArgument);
  CHECK_EQ(agile.ReadRegister(uint8_t{0x50}).error, Error::InvalidArgument);
  CHECK_EQ(port2.state.transfers, before);
  port2.state.fail_transfer = port2.state.transfers;
  CHECK_EQ(agile.ReadRegister(reg::kInputPort0).error, Error::Bus); // no retry
  CHECK_EQ(port2.state.transfers, before + 1);
}

TEST_CASE("Reading an input port with ReadRegister clears that port's interrupt") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.interrupt_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.SetExternal(0x7FFE); // pin 0 and pin 15 change
  CHECK(port.state.chip.InterruptAsserted());
  CHECK_EQ(io.ReadRegister(reg::kInputPort0).value, 0xFEU);
  CHECK(port.state.chip.InterruptAsserted()); // port 1 still pending
  CHECK_EQ(io.ReadRegister(reg::kInputPort0 + 1U).value, 0x7FU);
  CHECK(!port.state.chip.InterruptAsserted());
  CHECK_EQ(io.GetLastInputs(), 0x7FFEU);
}

TEST_CASE("GetLastInputs and GetLastOutput keep what the chip last reported, with no I/O") {
  BarePort port;
  pcal9555::PCAL9555<BarePort> io(port);
  CHECK_EQ(io.GetLastInputs(), 0U);
  CHECK_EQ(io.GetLastOutput(), 0xFFFFU);
  port.state.chip.SetExternal(0xA5A5);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(io.GetLastInputs(), 0xA5A5U); // the presence read
  port.state.chip.SetExternal(0x0FF0);
  CHECK_EQ(io.ReadPin(4).value, true);
  CHECK_EQ(io.GetLastInputs(), 0xA5F0U); // only port 0 was read
  CHECK_EQ(io.ReadInputs().value, 0x0FF0U);
  CHECK_EQ(io.GetLastInputs(), 0x0FF0U);

  // A lost write shows as a difference between the copy and the last output read.
  port.state.chip.drop_writes = 2;
  CHECK(io.SetOutputs(0x0003, false).ok()); // no read-back by default
  const int before = port.state.transfers;
  CHECK_EQ(io.GetLastOutput(), 0xFFFFU); // the start-up read-back
  CHECK_EQ(port.state.transfers, before);
  CHECK_EQ(io.ReadOutputLatch().value, 0xFFFFU);
  CHECK(io.GetLastOutput() != io.GetConfig().output_high_mask);
  CHECK(io.WriteOutputs(0x1234, pcal9555::Verify::ReadBack).ok());
  CHECK_EQ(io.GetLastOutput(), 0x1234U);
  CHECK_EQ(io.ReadRegister(reg::kOutputPort0 + 1U).value, 0x12U);
  CHECK_EQ(io.GetLastOutput(), 0x1234U);
}

// --- Interrupt latch (KB-CPP-119)
// ------------------------------------------------------------------

TEST_CASE("Every interrupt status read is ORed into the latch until ClearLatchedInterrupts") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.interrupt_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  CHECK_EQ(io.GetLatchedInterrupts(), 0U);
  port.state.chip.SetExternal(0xFFFE);
  CHECK_EQ(io.ReadInterruptStatus().value, 0x0001U);
  CHECK_EQ(io.ReadInputs().value, 0xFFFEU); // clears the chip's status
  CHECK_EQ(io.ReadInterruptStatus().value, 0U);
  CHECK_EQ(io.GetLatchedInterrupts(), 0x0001U); // still kept
  port.state.chip.SetExternal(0x7FFE);
  CHECK_EQ(io.ReadRegister(reg::kInterruptStatus0 + 1U).value, 0x80U);
  CHECK_EQ(io.GetLatchedInterrupts(), 0x8001U);
  const int before = port.state.transfers;
  io.ClearLatchedInterrupts();
  CHECK_EQ(io.GetLatchedInterrupts(), 0U);
  CHECK_EQ(port.state.transfers, before);
  CHECK(port.state.chip.InterruptAsserted()); // the chip is not touched
}

TEST_CASE("The start-up identity pattern goes to the polarity pair and the Config replaces it") {
  BarePort port;
  port.state.chip.stuck_register = reg::kPolarityPort0 + 1U; // keeps 0x00
  pcal9555::PCAL9555<BarePort> io(port);
  CHECK_EQ(io.EnsureInitialized().error, Error::DeviceIdMismatch);
  CHECK_EQ(port.state.chip.Pair(reg::kConfigPort0), 0xFFFFU); // nothing configured
  BarePort good;
  pcal9555::Config c{};
  c.polarity_invert_mask = 0x0F0F;
  pcal9555::PCAL9555<BarePort> io2(good, pcal9555::kI2cAddress, c);
  REQUIRE(io2.EnsureInitialized().ok());
  CHECK_EQ(good.state.chip.Pair(reg::kPolarityPort0), 0x0F0FU);
}

TEST_CASE("Deinitialize clears the diagnostics") {
  BarePort port;
  pcal9555::Config c = Pcal();
  c.interrupt_mask = 0x0000;
  pcal9555::PCAL9555<BarePort> io(port, pcal9555::kI2cAddress, c);
  REQUIRE(io.EnsureInitialized().ok());
  port.state.chip.SetExternal(0xFFFE);
  REQUIRE(io.ReadInterruptStatus().ok());
  REQUIRE(io.WriteOutputs(0x00FF, pcal9555::Verify::ReadBack).ok());
  CHECK(io.GetLatchedInterrupts() != 0U);
  REQUIRE(io.Deinitialize().ok());
  CHECK_EQ(io.GetLatchedInterrupts(), 0U);
  CHECK_EQ(io.GetLastInputs(), 0U);
  CHECK_EQ(io.GetLastOutput(), 0xFFFFU);
}

TEST_CASE("Error names include NoDevice") {
  CHECK(pcal9555::ToString(Error::NoDevice)[0] == 'N');
  CHECK(pcal9555::ToString(static_cast<Error>(17))[2] == 'D');
}
