/**
 * @file fuzz_pcal9555.cpp
 * @brief Fuzz target: the driver over an I2C bus whose behaviour the fuzzer controls.
 *
 * What the driver reads comes from the chip over I2C, so read data is the untrusted input. The
 * fuzzer's bytes choose a configuration and a part (PCA9555 or PCAL9555A, which may differ from the
 * one configured), then a sequence of driver calls; on every transaction they also choose what the
 * bus does: pass it to the chip model, fail it (an address NACK, or another bus error), replace the
 * bytes read, acknowledge a write and lose it (a write dropped on a noisy bus), or store a write
 * and report it failed (a NACK or a lost ACK after the bytes arrived).
 *
 * After every call the target checks the driver's promises (docs/contract.md), and aborts, naming
 * the line, when one breaks:
 *
 * - a failed call returns an `Error` the enum defines, and `T{}`;
 * - `NoDevice` only when the port reported the last failed transfer as an address NACK;
 * - before `EnsureInitialized()` succeeds, no call makes a transaction: reads and pin calls return
 *   `NotInitialized`, `ApplyConfig()` only stores the configuration;
 * - `InvalidArgument` and `Unsupported` mean nothing was sent;
 * - with `Verify::ReadBack`, a pin call that returns `Ok` while the bus passed every byte unchanged
 *   left the chip holding what `GetConfig()` says: success is never claimed for a lost write;
 * - `SetDirection()` that returns `Ok` on an honest bus, with an output left, kept the output pair
 *   (and on a PCAL9555A 0x4F) as the copy says, so no pin becomes an output at a level or stage
 * nobody asked for, even when the chip went through a power-on reset (which the fuzzer also
 * chooses);
 * - `DeviceReset` leaves the driver uninitialised; `CheckForReset()` sends at most one read, plus
 *   one per register whose last write failed (ten registers in all).
 *
 * Built two ways (tests/fuzz/CMakeLists.txt): with libFuzzer (Clang,
 * `-DPCAL9555_BUILD_FUZZERS=ON`), and as a plain executable that replays the corpus in
 * tests/fuzz/corpus/ as a CTest.
 */
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>

#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"

namespace {

using pcal9555::Error;
using pcal9555_test::Pcal9555Model;
namespace reg = pcal9555::reg;

/// The fuzzer's bytes, read front to back; zeros once they run out.
class Input {
public:
  Input(const uint8_t* data, std::size_t size) noexcept : data_(data), size_(size) {}

  [[nodiscard]] bool Empty() const noexcept {
    return pos_ >= size_;
  }
  uint8_t Byte() noexcept {
    return pos_ < size_ ? data_[pos_++] : uint8_t{0};
  }
  uint16_t Word() noexcept {
    const auto high = Byte();
    return static_cast<uint16_t>((static_cast<uint32_t>(high) << 8U) | Byte());
  }

private:
  const uint8_t* data_;
  std::size_t size_;
  std::size_t pos_ = 0;
};

/// A broken promise: name the line and abort, so libFuzzer saves the input as a crash.
void Check(bool promise, int line = __builtin_LINE()) noexcept {
  if (!promise) {
    (void)std::fprintf(stderr, "fuzz_pcal9555: promise broken at line %d\n", line);
    std::abort();
  }
}

/// The I2C port: the chip model behind a bus the fuzzer controls.
class FuzzPort : public pcal9555::I2cInterface<FuzzPort> {
public:
  explicit FuzzPort(Input& in) noexcept : in_(in) {}

  [[nodiscard]] bool EnsureInitialized() const noexcept {
    return bus_up_ok_;
  }

  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    Check(data != nullptr && len >= 1 && len <= 3 && addr >= 0x20 && addr <= 0x27);
    transfers_++;
    const uint8_t action = in_.Byte();
    if (failed(action)) {
      return false; // nothing stored
    }
    if (action == 0xFEU) {
      tampered_ = true; // acknowledged, lost
      return true;
    }
    if (action == 0xFAU) {
      (void)chip_.Write(data, len); // arrived, but the master saw a NACK
      last_failure_ = pcal9555::I2cFailure::DataNack;
      knows_ = true;
      return false;
    }
    if (!chip_.Write(data, len)) {
      last_failure_ = pcal9555::I2cFailure::DataNack;
      return false;
    }
    return true;
  }

  /// The fuzzer also decides whether the port can tell why a transfer failed.
  bool GetLastFailure(pcal9555::I2cFailure& failure) noexcept {
    failure = last_failure_;
    return knows_;
  }
  /// True when the driver may return NoDevice: the port reported an address NACK last.
  [[nodiscard]] bool LastWasAddressNack() const noexcept {
    return knows_ && last_failure_ == pcal9555::I2cFailure::AddressNack;
  }

  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    Check(tx != nullptr && rx != nullptr && tx_len == 1 && rx_len >= 1 && rx_len <= 2 &&
          addr >= 0x20 && addr <= 0x27);
    transfers_++;
    const uint8_t action = in_.Byte();
    if (failed(action)) {
      return false;
    }
    if (!chip_.WriteRead(tx[0], rx, rx_len)) {
      last_failure_ = pcal9555::I2cFailure::DataNack;
      return false;
    }
    if (action == 0xFDU) {
      tampered_ = true;
      for (std::size_t i = 0; i < rx_len; ++i) {
        rx[i] = in_.Byte(); // another device answered, or noise
      }
    }
    return true;
  }

  Pcal9555Model& Chip() noexcept {
    return chip_;
  }
  [[nodiscard]] int Transfers() const noexcept {
    return transfers_;
  }
  void SetBusUp(bool ok) noexcept {
    bus_up_ok_ = ok;
  }
  /// True when the bus lost a write or replaced read data since the last call of this.
  bool TakeTampered() noexcept {
    const bool t = tampered_;
    tampered_ = false;
    return t;
  }

private:
  // 0xFF: a bus error; 0xFC: an address NACK; 0xFB: an address NACK the port cannot place.
  bool failed(uint8_t action) noexcept {
    if (action != 0xFFU && action != 0xFCU && action != 0xFBU) {
      return false;
    }
    last_failure_ =
        action == 0xFFU ? pcal9555::I2cFailure::Other : pcal9555::I2cFailure::AddressNack;
    knows_ = action != 0xFBU;
    return true;
  }

  Input& in_;
  Pcal9555Model chip_;
  int transfers_ = 0;
  bool bus_up_ok_ = true;
  bool tampered_ = false;
  pcal9555::I2cFailure last_failure_ = pcal9555::I2cFailure::Other;
  bool knows_ = true;
};

using Driver = pcal9555::PCAL9555<FuzzPort>;

FuzzPort* g_port = nullptr; // NOLINT(cppcoreguidelines-avoid-non-const-global-variables): the run

bool KnownError(Error e) noexcept {
  switch (e) {
  case Error::Ok:
  case Error::NotInitialized:
  case Error::InvalidArgument:
  case Error::Bus:
  case Error::Timeout:
  case Error::DeviceIdMismatch:
  case Error::Crc:
  case Error::Busy:
  case Error::DeviceFault:
  case Error::Unsupported:
  case Error::WriteMismatch:
  case Error::NoDevice:
  case Error::DeviceReset:
    return true;
  default:
    break; // not an enumerator
  }
  return false;
}

/// A configuration from the fuzzer's bytes; sometimes invalid on purpose.
pcal9555::Config FuzzConfig(Input& in) noexcept {
  pcal9555::Config c{};
  const uint32_t flags = in.Byte();
  c.variant = static_cast<pcal9555::Variant>(flags % 3U); // 2 is not an enumerator
  c.port0_mode = static_cast<pcal9555::OutputMode>((flags >> 3U) % 3U);
  c.port1_mode = static_cast<pcal9555::OutputMode>((flags >> 5U) & 0x01U);
  if ((flags & 0x80U) != 0U) {
    c.output_high_mask = in.Word();
    c.polarity_invert_mask = in.Word();
    c.input_mask = in.Word();
    c.pull_enable_mask = in.Word();
    c.pull_up_mask = in.Word();
    c.interrupt_mask = in.Word();
    c.input_latch_mask = in.Word();
    c.drive_strength_codes = static_cast<uint32_t>(in.Word()) << 16U | in.Word();
  }
  return c;
}

template <typename R>
void CheckCommon(const Driver& d, const R& r, bool was_up, int transfers) noexcept {
  Check(KnownError(r.error));
  if (r.error == Error::NoDevice) {
    Check(g_port->LastWasAddressNack() && transfers > 0);
  }
  if (!was_up) {
    Check(r.error == Error::NotInitialized && transfers == 0 && !d.IsInitialized());
  }
  if (r.error == Error::InvalidArgument || r.error == Error::Unsupported) {
    Check(transfers == 0);
  }
  if (r.error == Error::DeviceReset) {
    Check(!d.IsInitialized());
  }
}

template <typename T>
void CheckValue(const pcal9555::Result<T>& r) noexcept {
  if (!r) {
    Check(r.value == T{});
  }
}

/// A pin call that succeeded on an honest bus left the chip as the driver says.
void CheckHolds(pcal9555::Verify v, FuzzPort& port, const pcal9555::Result<void>& r, bool tampered,
                uint8_t reg_pair, uint16_t expected) noexcept {
  if (r.ok() && !tampered && v == pcal9555::Verify::ReadBack) {
    Check(port.Chip().Pair(reg_pair) == expected);
  }
}

/// A read-back call reads the chip's register, not the driver's copy.
void CheckReadBack(const Driver& d, FuzzPort& port, const pcal9555::Result<uint16_t>& r,
                   bool was_up, int transfers, uint8_t pair) noexcept {
  CheckCommon(d, r, was_up, transfers);
  CheckValue(r);
  if (r.ok() && !port.TakeTampered()) {
    Check(r.value == port.Chip().Pair(pair));
  }
}

void RunOne(Driver& d, FuzzPort& port, Input& in) noexcept {
  const uint8_t op_byte = in.Byte();
  const uint8_t op = op_byte % 22U;
  // Per call: None, ReadBack, or 2, which is not an enumerator (InvalidArgument).
  const auto v = static_cast<pcal9555::Verify>((op_byte / 22U) % 3U);
  const bool was_up = d.IsInitialized();
  const int before = port.Transfers();
  (void)port.TakeTampered();
  const auto sent = [&] { return port.Transfers() - before; };
  const uint16_t mask = in.Word();

  switch (op) {
  case 0: {
    const auto r = d.EnsureInitialized();
    Check(KnownError(r.error) && r.ok() == d.IsInitialized());
    Check(r.error != Error::NoDevice || port.LastWasAddressNack());
    if (was_up) {
      Check(r.ok() && sent() == 0);
    }
    break;
  }
  case 1: {
    const auto r = d.Deinitialize(); // started: one write, every pin back to an input
    Check(KnownError(r.error) && !d.IsInitialized() && sent() == (was_up ? 1 : 0));
    Check(r.error != Error::NoDevice || port.LastWasAddressNack());
    break;
  }
  case 2: {
    const auto r = d.ApplyConfig(FuzzConfig(in));
    Check(KnownError(r.error));
    Check(r.error != Error::NoDevice || port.LastWasAddressNack());
    if (!was_up || r.error == Error::InvalidArgument || r.error == Error::Unsupported) {
      Check(sent() == 0);
    }
    break;
  }
  case 3: {
    const auto r = d.SetOutputs(mask, (in.Byte() & 1U) != 0U, v);
    CheckCommon(d, r, was_up, sent());
    CheckHolds(v, port, r, port.TakeTampered(), reg::kOutputPort0, d.GetConfig().output_high_mask);
    break;
  }
  case 4: {
    const auto r = d.WriteOutputs(mask, v);
    CheckCommon(d, r, was_up, sent());
    CheckHolds(v, port, r, port.TakeTampered(), reg::kOutputPort0, mask);
    break;
  }
  case 5: {
    const auto r = d.ToggleOutputs(mask, v);
    CheckCommon(d, r, was_up, sent());
    CheckHolds(v, port, r, port.TakeTampered(), reg::kOutputPort0, d.GetConfig().output_high_mask);
    break;
  }
  case 6: {
    const auto r = d.SetDirection(mask, static_cast<pcal9555::Direction>(in.Byte() & 1U), v);
    CheckCommon(d, r, was_up, sent());
    const bool tampered = port.TakeTampered();
    CheckHolds(v, port, r, tampered, reg::kConfigPort0, d.GetConfig().input_mask);
    // Releasing the last output writes the configuration pair only: nothing else then matters.
    if (r.ok() && !tampered && (d.GetConfig().input_mask != reg::kConfigReset)) {
      Check(port.Chip().Pair(reg::kOutputPort0) == d.GetConfig().output_high_mask);
      if (d.GetConfig().variant == pcal9555::Variant::Pcal9555a) {
        Check(port.Chip().Register(reg::kOutputPortConfig) ==
              pcal9555::detail::OutputPortConfig(d.GetConfig()));
      }
    }
    break;
  }
  case 7: {
    const auto r = d.SetPolarityInversion(mask, (in.Byte() & 1U) != 0U, v);
    CheckCommon(d, r, was_up, sent());
    CheckHolds(v, port, r, port.TakeTampered(), reg::kPolarityPort0,
               d.GetConfig().polarity_invert_mask);
    break;
  }
  case 8: {
    const auto r = d.ReadInputs();
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    break;
  }
  case 9: {
    const auto r = d.ReadPin(static_cast<uint8_t>(in.Byte() % 18U)); // 16, 17 are not pins
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    break;
  }
  case 10: {
    const auto r =
        d.SetDriveStrength(mask, static_cast<pcal9555::DriveStrength>(in.Byte() % 5U), v);
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 11: {
    const auto r = d.SetPull(mask, static_cast<pcal9555::Pull>(in.Byte() % 4U), v);
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 12: {
    const auto r = d.SetInputLatch(mask, (in.Byte() & 1U) != 0U, v);
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 13: {
    const auto r = d.SetInterruptEnabled(mask, (in.Byte() & 1U) != 0U, v);
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 14: {
    const auto r = d.ReadInterruptStatus();
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    if (r.ok()) { // every status read is in the latch
      Check((static_cast<uint32_t>(d.GetLatchedInterrupts()) & r.value) == r.value);
    }
    break;
  }
  case 15: {
    const uint8_t b = in.Byte();
    const auto r = d.SetOutputMode(static_cast<pcal9555::Bank>(b % 3U),
                                   static_cast<pcal9555::OutputMode>((b >> 2U) % 3U), v);
    CheckCommon(d, r, was_up, sent());
    break;
  }
  case 16:
    CheckReadBack(d, port, d.ReadOutputLatch(), was_up, sent(), reg::kOutputPort0);
    break;
  case 17:
    CheckReadBack(d, port, d.ReadInputMask(), was_up, sent(), reg::kConfigPort0);
    break;
  case 18:
    CheckReadBack(d, port, d.ReadPolarityInversion(), was_up, sent(), reg::kPolarityPort0);
    break;
  case 19: {
    // Raw access: one transfer for an implemented command byte, none otherwise.
    const uint8_t command = in.Byte();
    const auto r = d.ReadRegister(command);
    CheckCommon(d, r, was_up, sent());
    CheckValue(r);
    if (was_up) {
      const bool agile = d.GetConfig().variant == pcal9555::Variant::Pcal9555a;
      Check(sent() == (reg::IsImplemented(command, agile) ? 1 : 0));
    }
    break;
  }
  case 20: {
    // The diagnostics accessors do no I/O; the clear empties the latch.
    (void)d.GetLastInputs();
    (void)d.GetLastOutput();
    d.ClearLatchedInterrupts();
    Check(d.GetLatchedInterrupts() == 0U && sent() == 0);
    const auto r = d.CheckForReset();
    CheckCommon(d, r, was_up, sent());
    Check(sent() <= 11);
    break;
  }
  default: {
    port.Chip().SetExternal(mask);
    const uint8_t b = in.Byte();
    port.SetBusUp((b & 0x07U) != 0U);
    if ((b & 0x80U) != 0U) {
      port.Chip().PowerOn(); // a supply dip below the power-on reset voltage (section 6.4)
    }
    break;
  }
  }
}

} // namespace

extern "C" int LLVMFuzzerTestOneInput(const uint8_t* data, std::size_t size) {
  Input in(data, size);
  FuzzPort port(in);
  g_port = &port;
  if ((in.Byte() & 1U) != 0U) {
    port.Chip().variant =
        Pcal9555Model::Part::Pca9555; // the part fitted may not be the one configured
    port.Chip().PowerOn();
  }
  Driver driver(port, static_cast<uint8_t>(0x20U + (in.Byte() % 9U)), FuzzConfig(in)); // 0x28: bad
  for (int calls = 0; calls < 64 && !in.Empty(); ++calls) {
    RunOne(driver, port, in);
  }
  g_port = nullptr;
  return 0;
}
