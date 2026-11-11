/**
 * @file pcal9555.ipp
 * @brief PCAL9555 driver implementation, included by `pcal9555.hpp`.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * Section numbers refer to the NXP PCAL9555A data sheet, Rev. 2 (19 December 2014).
 */
#ifndef PCAL9555_IPP
#define PCAL9555_IPP

namespace pcal9555 {

// -------------------------------------------------------------------------------------------------
// Lifecycle
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCAL9555<Port>::EnsureInitialized() noexcept {
  if (initialized_) {
    return Result<void>::Success();
  }
  return initialize();
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::initialize() noexcept {
  if (const Error e = Validate(config_); e != Error::Ok) {
    return fail(e, "pcal9555: invalid Config");
  }
  if ((bus_.GetAddress() < kMinI2cAddress) || (bus_.GetAddress() > kMaxI2cAddress)) {
    return fail(Error::InvalidArgument, "pcal9555: address outside 0x20 to 0x27");
  }
  if (!bus_.GetPort().EnsureInitialized()) {
    return fail(Error::Bus, "pcal9555: the port could not bring the bus up");
  }
  if (auto r = checkPresence(); !r) {
    return r;
  }
  uint8_t failed = 0U;
  if (auto r = writeAll(config_, failed); !r) {
    return fail(r.error, "pcal9555: writing the registers failed");
  }
  if (auto r = verifyAll(config_); !r) {
    return r;
  }
  initialized_ = true;
  uncertain_ = 0U; // every register written and read back
  detail::Log(bus_.GetPort(), LogLevel::Info, "pcal9555: initialised");
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::Deinitialize() noexcept {
  if (!initialized_) {
    return Result<void>::Success();
  }
  initialized_ = false;
  // The diagnostics start again with the next start-up, which may be for another part.
  last_inputs_ = 0U;
  last_output_ = reg::kOutputReset;
  latched_interrupts_ = 0U;
  return writePair(reg::kConfigPort0, reg::kConfigReset);
}

// The chip has no ID register (standards/driver.md: check the closest equivalent). Reading the
// input ports proves that something ACKs the address; their value is the pin levels, so it says
// nothing about the part. The read also clears an interrupt left pending from before the start
// (section 6.5), so INT reflects new changes only. A PCAL9555A must also answer at 0x4F with the
// reserved bits 7..2 at 0 (table 27). Then a test pattern goes to the polarity inversion pair,
// which changes nothing but how inputs read, and must read back: an active answer that a floating,
// stuck or deaf bus cannot give, whatever the Config holds (KB-CPP-117). writeAll() then writes
// the configured polarity, and verifyAll() reads every register back.
template <I2cPort Port>
Result<void> PCAL9555<Port>::checkPresence() noexcept {
  if (auto r = readPair(reg::kInputPort0); !r) {
    return fail(r.error, (r.error == Error::NoDevice) ? "pcal9555: nothing acknowledges the address"
                                                      : "pcal9555: no chip answers at the address");
  }
  if (isAgile()) {
    const auto value = readByte(reg::kOutputPortConfig);
    if (!value) {
      return fail(value.error, "pcal9555: the Agile I/O bank does not answer");
    }
    if ((static_cast<uint32_t>(value.value) &
         static_cast<uint32_t>(reg::kOutputConfigReservedMask)) != 0U) {
      return fail(Error::DeviceIdMismatch, "pcal9555: not a PCAL9555A (reserved bits set)");
    }
  }
  if (auto r = writePair(reg::kPolarityPort0, reg::kIdentityPattern); !r) {
    return fail(r.error, "pcal9555: the identity pattern could not be written");
  }
  const auto back = readPair(reg::kPolarityPort0);
  if (!back) {
    return fail(back.error, "pcal9555: the identity pattern could not be read back");
  }
  if (back.value != reg::kIdentityPattern) {
    return fail(Error::DeviceIdMismatch, "pcal9555: the identity pattern read back differently");
  }
  return Result<void>::Success();
}

// Order: the Agile I/O bank first, so that the output stage (0x4F) and the pull resistors are in
// place before a pin turns into an output (section 6.2.12 recommends 0x4F before 0x06/0x07); then
// the output values before the directions, so that a new output starts at its configured level.
template <I2cPort Port>
Result<void> PCAL9555<Port>::writeAll(const Config& c, uint8_t& failed) noexcept {
  if (isAgile()) {
    for (const auto& p : detail::AgilePairs(c)) {
      failed = p.reg;
      if (auto r = writePair(p.reg, p.value); !r) {
        return r;
      }
    }
    failed = reg::kOutputPortConfig;
    if (auto r = writeByte(reg::kOutputPortConfig, detail::OutputPortConfig(c)); !r) {
      return r;
    }
  }
  for (const auto& p : detail::StandardPairs(c)) {
    failed = p.reg;
    if (auto r = writePair(p.reg, p.value); !r) {
      return r;
    }
  }
  return Result<void>::Success();
}

// A failed ApplyConfig(): the writes before the failed one were acknowledged (the copy takes their
// new values), the failed one may have arrived (unknown, the riskier value), the rest were not
// sent (the copy keeps their old values). Same order as writeAll().
template <I2cPort Port>
void PCAL9555<Port>::noteApplyFailure(const Config& asked, uint8_t failed, Error error) noexcept {
  Config next = config_;
  std::array<uint8_t, 10> order{};
  std::size_t n = 0U;
  if (isAgile()) {
    for (const auto& p : detail::AgilePairs(asked)) {
      order[n++] = p.reg;
    }
    order[n++] = reg::kOutputPortConfig;
  }
  for (const auto& p : detail::StandardPairs(asked)) {
    order[n++] = p.reg;
  }
  std::size_t i = 0U;
  while ((i < n) && (order[i] != failed)) {
    detail::Adopt(next, order[i], detail::RegisterValue(asked, order[i]));
    ++i;
  }
  config_ = next;
  if (i < n) { // writeAll() names the register it failed on
    Config sent = next;
    detail::Adopt(sent, failed, detail::RegisterValue(asked, failed));
    markFailed(sent, failed, error);
  }
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::verifyAll(const Config& c) noexcept {
  Result<void> r = verifyPairs(detail::StandardPairs(c));
  if (r && isAgile()) {
    r = verifyPairs(detail::AgilePairs(c));
    if (r) {
      const auto value = readByte(reg::kOutputPortConfig);
      if (!value) {
        r = Result<void>::Failure(value.error);
      } else if (value.value != detail::OutputPortConfig(c)) {
        r = Result<void>::Failure(Error::DeviceIdMismatch);
      } else {
        // The output port configuration holds what was written: r stays a success.
      }
    }
  }
  if (!r) {
    return fail(r.error, (r.error == Error::DeviceIdMismatch)
                             ? "pcal9555: a register read back differently"
                             : "pcal9555: reading the registers back failed");
  }
  return r;
}

template <I2cPort Port>
template <std::size_t N>
Result<void> PCAL9555<Port>::verifyPairs(const std::array<detail::PairValue, N>& pairs) noexcept {
  for (const auto& p : pairs) {
    const auto r = readPair(p.reg);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    if (r.value != p.value) {
      return Result<void>::Failure(Error::DeviceIdMismatch);
    }
  }
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Configuration
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCAL9555<Port>::ApplyConfig(const Config& config) noexcept {
  if (const Error e = Validate(config); e != Error::Ok) {
    return Result<void>::Failure(e);
  }
  if (!initialized_) {
    config_ = config; // written by the next EnsureInitialized()
    return Result<void>::Success();
  }
  if (config.variant != config_.variant) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  // A reset is reported, not silently repaired: the application learns that the chip lost its
  // state (section 6.4) and starts it again.
  if (auto r = checkReset(); !r) {
    return r;
  }
  if (auto r = resolveAll(); !r) { // every register known before it is written again
    return r;
  }
  uint8_t failed = 0U;
  if (auto r = writeAll(config, failed); !r) {
    noteApplyFailure(config, failed, r.error);
    return r;
  }
  config_ = config;
  uncertain_ = 0U; // every write was acknowledged
  const auto r = verifyAll(config);
  // Started, the part was already identified: a register that does not keep its value is a lost
  // write, not another device.
  return (r.error == Error::DeviceIdMismatch) ? Result<void>::Failure(Error::WriteMismatch) : r;
}

// -------------------------------------------------------------------------------------------------
// Pins
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetDirection(uint16_t mask, Direction direction,
                                          Verify verify) noexcept {
  if (auto r = checkCall(mask, false, verify); !r) {
    return r;
  }
  const bool release = direction == Direction::Input;
  // Every pin an input afterwards: nothing is driven whatever the chip went through, so the
  // configuration pair alone is written, first and with nothing before it that could fail.
  if (release && (detail::Apply(config_.input_mask, mask, true) == reg::kConfigReset)) {
    return storeMask(reg::kConfigPort0, &Config::input_mask, reg::kConfigReset, verify);
  }
  if (auto r = checkReset(); !r) {
    return r;
  }
  // Nothing is built from a register whose last write failed: read it into the copy first.
  for (const uint8_t r : {reg::kOutputPortConfig, reg::kOutputPort0, reg::kConfigPort0}) {
    if (auto resolved = resolve(r); !resolved) {
      return resolved;
    }
  }
  const uint16_t inputs = detail::Apply(config_.input_mask, mask, release);
  if (release) {
    // Releasing is the safe direction: the configuration pair goes first, so a failure after it
    // leaves the pins released. 0x4F and the output pair follow for the pins that stay outputs.
    if (auto r = storeMask(reg::kConfigPort0, &Config::input_mask, inputs, verify); !r) {
      return r;
    }
    return writeStage();
  }
  // A pin that becomes an output drives its latch through its port's stage at once, and a reset
  // after the check loads a high latch and push-pull stages (table 4). So 0x4F goes first (section
  // 6.2.12), then the output pair, then the configuration pair, all from the copy: no pin becomes
  // an output at the reset latch level or stage.
  if (auto r = writeStage(); !r) {
    return r;
  }
  return storeMask(reg::kConfigPort0, &Config::input_mask, inputs, verify);
}

// The output stage and levels from the copy: 0x4F (PCAL9555A), then the output pair.
template <I2cPort Port>
Result<void> PCAL9555<Port>::writeStage() noexcept {
  if (isAgile()) {
    if (auto r = writeByte(reg::kOutputPortConfig, detail::OutputPortConfig(config_)); !r) {
      return r;
    }
  }
  return writePair(reg::kOutputPort0, config_.output_high_mask);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetOutputs(uint16_t mask, bool high, Verify verify) noexcept {
  if (auto r = checkCall(mask, false, verify); !r) {
    return r;
  }
  if (auto r = prepare(reg::kOutputPort0, reg::kOutputPort0); !r) {
    return r;
  }
  return storeMask(reg::kOutputPort0, &Config::output_high_mask,
                   detail::Apply(config_.output_high_mask, mask, high), verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::WriteOutputs(uint16_t high_mask, Verify verify) noexcept {
  if (auto r = checkCall(kAllPins, false, verify); !r) {
    return r;
  }
  // Absolute: nothing is built from the copy, so an unknown output pair needs no read first.
  if (auto r = guardWrite(reg::kOutputPort0, reg::kOutputPort0); !r) {
    return r;
  }
  return storeMask(reg::kOutputPort0, &Config::output_high_mask, high_mask, verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::ToggleOutputs(uint16_t mask, Verify verify) noexcept {
  if (auto r = checkCall(mask, false, verify); !r) {
    return r;
  }
  if (auto r = prepare(reg::kOutputPort0, reg::kOutputPort0); !r) {
    return r;
  }
  return storeMask(reg::kOutputPort0, &Config::output_high_mask,
                   static_cast<uint16_t>(static_cast<uint32_t>(config_.output_high_mask) ^
                                         static_cast<uint32_t>(mask)),
                   verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetPolarityInversion(uint16_t mask, bool invert,
                                                  Verify verify) noexcept {
  if (auto r = checkCall(mask, false, verify); !r) {
    return r;
  }
  if (auto r = prepare(reg::kPolarityPort0, reg::kPolarityPort0); !r) {
    return r;
  }
  return storeMask(reg::kPolarityPort0, &Config::polarity_invert_mask,
                   detail::Apply(config_.polarity_invert_mask, mask, invert), verify);
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::ReadInputs() noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  if (auto r = noteInterrupts(); !r) {
    return Result<uint16_t>::Failure(r.error);
  }
  return readPair(reg::kInputPort0);
}

template <I2cPort Port>
Result<bool> PCAL9555<Port>::ReadPin(uint8_t pin) noexcept {
  if (!initialized_) {
    return Result<bool>::Failure(Error::NotInitialized);
  }
  if (pin >= kPinCount) {
    return Result<bool>::Failure(Error::InvalidArgument);
  }
  if (auto r = noteInterrupts(); !r) {
    return Result<bool>::Failure(r.error);
  }
  const auto bit = static_cast<uint32_t>(pin);
  const auto value =
      readByte(static_cast<uint8_t>(static_cast<uint32_t>(reg::kInputPort0) + (bit / 8U)));
  if (!value) {
    return Result<bool>::Failure(value.error);
  }
  return Result<bool>::Success(((static_cast<uint32_t>(value.value) >> (bit % 8U)) & 1U) != 0U);
}

// -------------------------------------------------------------------------------------------------
// Agile I/O
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetDriveStrength(uint16_t mask, DriveStrength level,
                                              Verify verify) noexcept {
  if (auto r = checkCall(mask, true, verify); !r) {
    return r;
  }
  if (!detail::IsEnumerator(level, DriveStrength::Full)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (auto r = prepare(reg::kDriveStrength0, reg::kDriveStrength2); !r) {
    return r;
  }
  return writeDrive(detail::ApplyDrive(config_.drive_strength_codes, mask, level), verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetPull(uint16_t mask, Pull pull, Verify verify) noexcept {
  if (auto r = checkCall(mask, true, verify); !r) {
    return r;
  }
  if (!detail::IsEnumerator(pull, Pull::Down)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (auto r = prepare(reg::kPullSelect0, reg::kPullEnable0); !r) {
    return r;
  }
  if (pull != Pull::None) {
    if (auto r = storeMask(reg::kPullSelect0, &Config::pull_up_mask,
                           detail::Apply(config_.pull_up_mask, mask, pull == Pull::Up), verify);
        !r) {
      return r;
    }
  }
  return storeMask(reg::kPullEnable0, &Config::pull_enable_mask,
                   detail::Apply(config_.pull_enable_mask, mask, pull != Pull::None), verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetInputLatch(uint16_t mask, bool latch, Verify verify) noexcept {
  if (auto r = checkCall(mask, true, verify); !r) {
    return r;
  }
  if (auto r = prepare(reg::kInputLatch0, reg::kInputLatch0); !r) {
    return r;
  }
  return storeMask(reg::kInputLatch0, &Config::input_latch_mask,
                   detail::Apply(config_.input_latch_mask, mask, latch), verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetInterruptEnabled(uint16_t mask, bool enable,
                                                 Verify verify) noexcept {
  if (auto r = checkCall(mask, true, verify); !r) {
    return r;
  }
  // A mask bit of 1 masks the pin.
  if (auto r = prepare(reg::kInterruptMask0, reg::kInterruptMask0); !r) {
    return r;
  }
  return storeMask(reg::kInterruptMask0, &Config::interrupt_mask,
                   detail::Apply(config_.interrupt_mask, mask, !enable), verify);
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::ReadInterruptStatus() noexcept {
  if (auto r = checkCall(kAllPins, true); !r) {
    return Result<uint16_t>::Failure(r.error);
  }
  return readPair(reg::kInterruptStatus0);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::SetOutputMode(Bank bank, OutputMode mode, Verify verify) noexcept {
  if (auto r = checkCall(kAllPins, true, verify); !r) {
    return r;
  }
  if (!detail::IsEnumerator(bank, Bank::Port1) ||
      !detail::IsEnumerator(mode, OutputMode::OpenDrain)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  if (auto r = prepare(reg::kOutputPortConfig, reg::kOutputPortConfig); !r) {
    return r;
  }
  Config next = config_;
  ((bank == Bank::Port0) ? next.port0_mode : next.port1_mode) = mode;
  const uint8_t value = detail::OutputPortConfig(next);
  Result<void> r = writeByte(reg::kOutputPortConfig, value);
  if (r && (verify == Verify::ReadBack)) {
    const auto back = readByte(reg::kOutputPortConfig);
    if (!back) {
      r = Result<void>::Failure(back.error);
    } else if (back.value != value) {
      r = Result<void>::Failure(Error::WriteMismatch);
    } else {
      // The register holds what was written: r stays a success.
    }
  }
  if (!r && (r.error != Error::WriteMismatch)) {
    markFailed(next, reg::kOutputPortConfig, r.error); // the byte may have arrived
    return r;
  }
  config_ = next;
  forget(reg::kOutputPortConfig);
  return r;
}

// -------------------------------------------------------------------------------------------------
// Register access
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCAL9555<Port>::checkCall(uint16_t mask, bool agile, Verify verify) const noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  if (agile && !isAgile()) {
    return Result<void>::Failure(Error::Unsupported);
  }
  if ((mask == 0U) || !detail::IsEnumerator(verify, Verify::ReadBack)) {
    return Result<void>::Failure(Error::InvalidArgument);
  }
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::storeMask(uint8_t reg, uint16_t Config::* field, uint16_t value,
                                       Verify verify) noexcept {
  if (auto r = writePairChecked(reg, value, verify); !r) {
    if (r.error == Error::WriteMismatch) {
      config_.*field = value; // what was asked for: the next write of this register resends it
    } else {
      Config asked = config_;
      asked.*field = value;
      markFailed(asked, reg, r.error); // the transfer failed, but its bytes may have arrived
    }
    return r;
  }
  config_.*field = value;
  forget(reg);
  return Result<void>::Success();
}

// -------------------------------------------------------------------------------------------------
// Reset detection (section 6.4, table 4) and the interrupt status (sections 6.2.11, 6.5)
// -------------------------------------------------------------------------------------------------

template <I2cPort Port>
Result<void> PCAL9555<Port>::CheckForReset() noexcept {
  if (!initialized_) {
    return Result<void>::Failure(Error::NotInitialized);
  }
  return checkReset();
}

// The chip has no reset flag: a register whose copy differs from its power-up value and that
// reads its power-up value shows the reset. Nothing is written; the next EnsureInitialized()
// writes the copy again in the datasheet's order.
template <I2cPort Port>
Result<void> PCAL9555<Port>::checkReset() noexcept {
  const detail::ResetSentinel sentinel =
      detail::FindResetSentinel(config_, isAgile(), static_cast<uint32_t>(uncertain_));
  if (sentinel.reg == detail::kNoSentinel) {
    return checkUnknown(); // the copy is the power-up state, or every changed register is unknown
  }
  uint16_t value = 0U;
  if (sentinel.pair) {
    const auto r = readPair(sentinel.reg);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    value = r.value;
  } else {
    const auto r = readByte(sentinel.reg);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    value = r.value;
  }
  if (value != sentinel.reset) {
    return Result<void>::Success();
  }
  initialized_ = false;
  return fail(Error::DeviceReset, "pcal9555: the chip is back at its power-up values (a reset)");
}

// A setter about to rewrite the register the check reads checks first, so that its write does not
// erase the evidence of a reset. When no other register differs from its power-up value, the chip
// holds the copy or the power-up values, which differ in that register only: the write repairs
// both, so no read is needed (and a retry after a lost write stays a retry).
template <I2cPort Port>
Result<void> PCAL9555<Port>::guardWrite(uint8_t first, uint8_t second) noexcept {
  const detail::ResetSentinel sentinel =
      detail::FindResetSentinel(config_, isAgile(), static_cast<uint32_t>(uncertain_));
  const bool rewrites = (sentinel.reg == first) || (sentinel.reg == second);
  return (rewrites && !sentinel.only) ? checkReset() : Result<void>::Success();
}

// A write whose transfer failed may still have reached the chip (a NACK or a lost ACK after the
// bytes): the copy takes the riskier of the old and the asked value and the register is unknown.
// An address NACK (NoDevice) is the one failure that proves no byte arrived: the copy stays.
template <I2cPort Port>
void PCAL9555<Port>::markFailed(const Config& asked, uint8_t reg, Error error) noexcept {
  if (error == Error::NoDevice) {
    return;
  }
  config_ = detail::Riskier(config_, asked);
  uncertain_ = static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) | detail::UncertainBit(reg));
}

template <I2cPort Port>
void PCAL9555<Port>::forget(uint8_t reg) noexcept {
  uncertain_ =
      static_cast<uint16_t>(static_cast<uint32_t>(uncertain_) & ~detail::UncertainBit(reg));
}

// An unknown register is read into the copy before anything is built from it. Read back at its
// power-up value (table 4) it is taken as a reset (section 6.4): nothing is written, and
// EnsureInitialized() writes the copy again. That is also the reading when the failed write's old
// or new value was the power-up value: a one-time false DeviceReset, the price of a rule that
// needs no record of what the write may have left. Any other value is adopted.
template <I2cPort Port>
Result<void> PCAL9555<Port>::resolve(uint8_t reg) noexcept {
  if (!isUncertain(reg)) {
    return Result<void>::Success();
  }
  uint16_t value = 0U;
  if (reg == reg::kOutputPortConfig) {
    const auto r = readByte(reg);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    value = r.value;
  } else {
    const auto r = readPair(reg);
    if (!r) {
      return Result<void>::Failure(r.error);
    }
    value = r.value;
  }
  if (value == detail::RegisterValue(Config{}, reg)) {
    initialized_ = false;
    return fail(Error::DeviceReset, "pcal9555: an unknown register is back at its power-up value");
  }
  detail::Adopt(config_, reg, value);
  forget(reg);
  return Result<void>::Success();
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::resolveAll() noexcept {
  for (const uint8_t r :
       {reg::kDriveStrength0, reg::kDriveStrength2, reg::kInputLatch0, reg::kPullSelect0,
        reg::kPullEnable0, reg::kInterruptMask0, reg::kOutputPortConfig, reg::kOutputPort0,
        reg::kPolarityPort0, reg::kConfigPort0}) {
    if (auto resolved = resolve(r); !resolved) {
      return resolved;
    }
  }
  return Result<void>::Success();
}

// No sentinel: every register is at its power-up value in the copy or unknown. The unknown ones are
// read, and one back at its power-up value is a reset.
template <I2cPort Port>
Result<void> PCAL9555<Port>::checkUnknown() noexcept {
  if (uncertain_ == 0U) {
    return Result<void>::Success();
  }
  return resolveAll();
}

// Before a setter builds on the copy: the reset check if it would rewrite the evidence, then a
// read of each register it builds on whose last write failed.
template <I2cPort Port>
Result<void> PCAL9555<Port>::prepare(uint8_t first, uint8_t second) noexcept {
  if (auto r = guardWrite(first, second); !r) {
    return r;
  }
  if (auto r = resolve(first); !r) {
    return r;
  }
  return resolve(second);
}

// Reading an input port clears its interrupt and its status bits (sections 6.2.11, 6.5), so with
// an input that can interrupt, the status goes into the latch first (handbook KB-CPP-119).
template <I2cPort Port>
Result<void> PCAL9555<Port>::noteInterrupts() noexcept {
  const uint32_t armed =
      static_cast<uint32_t>(config_.input_mask) & ~static_cast<uint32_t>(config_.interrupt_mask);
  if (!isAgile() || ((armed & 0xFFFFU) == 0U)) {
    return Result<void>::Success();
  }
  const auto r = readPair(reg::kInterruptStatus0);
  return r ? Result<void>::Success() : Result<void>::Failure(r.error);
}

// Drive strength: pins 0..7 in 0x40/0x41 (bits 15..0 of the codes), pins 8..15 in 0x42/0x43.
template <I2cPort Port>
Result<void> PCAL9555<Port>::writeDrive(uint32_t codes, Verify verify) noexcept {
  // What was asked for, for the pair that was sent; pins 8..15 were not sent and keep theirs.
  const uint32_t low = (config_.drive_strength_codes & 0xFFFF'0000U) | (codes & 0x0000'FFFFU);
  if (auto r =
          storeDrive(reg::kDriveStrength0, low, static_cast<uint16_t>(codes & 0xFFFFU), verify);
      !r) {
    return r;
  }
  return storeDrive(reg::kDriveStrength2, codes, static_cast<uint16_t>(codes >> 16U), verify);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::storeDrive(uint8_t reg, uint32_t codes, uint16_t value,
                                        Verify verify) noexcept {
  if (auto r = writePairChecked(reg, value, verify); !r) {
    if (r.error == Error::WriteMismatch) {
      config_.drive_strength_codes = codes;
    } else {
      Config asked = config_;
      asked.drive_strength_codes = codes;
      markFailed(asked, reg, r.error);
    }
    return r;
  }
  config_.drive_strength_codes = codes;
  forget(reg);
  return Result<void>::Success();
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::readPair(uint8_t reg) noexcept {
  std::array<uint8_t, 2> bytes{};
  if (const Error e = bus_.Read(reg, std::span<uint8_t, 2>{bytes}); e != Error::Ok) {
    return Result<uint16_t>::Failure(e);
  }
  const uint16_t value = detail::PairValueOf(bytes);
  notePair(reg, value);
  return Result<uint16_t>::Success(value);
}

template <I2cPort Port>
Result<uint8_t> PCAL9555<Port>::readByte(uint8_t reg) noexcept {
  std::array<uint8_t, 1> byte{};
  if (const Error e = bus_.Read(reg, std::span<uint8_t, 1>{byte}); e != Error::Ok) {
    return Result<uint8_t>::Failure(e);
  }
  noteByte(reg, byte[0]);
  return Result<uint8_t>::Success(byte[0]);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::writeByte(uint8_t reg, uint8_t value) noexcept {
  const std::array<uint8_t, 1> byte{value};
  const Error e = bus_.Write(reg, std::span<const uint8_t, 1>{byte});
  return (e == Error::Ok) ? Result<void>::Success() : Result<void>::Failure(e);
}

// What a read saw, kept for the diagnostics accessors: two compares per read, no I/O. The
// interrupt status is ORed into the latch, never overwritten (KB-CPP-119).
template <I2cPort Port>
void PCAL9555<Port>::notePair(uint8_t reg, uint16_t value) noexcept {
  if (reg == reg::kInputPort0) {
    last_inputs_ = value;
  } else if (reg == reg::kOutputPort0) {
    last_output_ = value;
  } else if (reg == reg::kInterruptStatus0) {
    latched_interrupts_ = static_cast<uint16_t>(static_cast<uint32_t>(latched_interrupts_) |
                                                static_cast<uint32_t>(value));
  } else {
    // Any other pair has no diagnostics accessor: nothing to keep.
  }
}

template <I2cPort Port>
void PCAL9555<Port>::noteByte(uint8_t reg, uint8_t value) noexcept {
  const auto r = static_cast<uint32_t>(reg);
  const uint32_t port = r & 1U;
  const uint32_t pair = r & ~1U;
  if (pair == static_cast<uint32_t>(reg::kInputPort0)) {
    last_inputs_ = detail::WithByte(last_inputs_, port, value);
  } else if (pair == static_cast<uint32_t>(reg::kOutputPort0)) {
    last_output_ = detail::WithByte(last_output_, port, value);
  } else if (pair == static_cast<uint32_t>(reg::kInterruptStatus0)) {
    latched_interrupts_ = static_cast<uint16_t>(static_cast<uint32_t>(latched_interrupts_) |
                                                (static_cast<uint32_t>(value) << (port * 8U)));
  } else {
    // Any other register has no diagnostics accessor: nothing to keep.
  }
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::writePairChecked(uint8_t reg, uint16_t value, Verify verify) noexcept {
  if (auto r = writePair(reg, value); !r || (verify == Verify::None)) {
    return r;
  }
  const auto back = readPair(reg);
  if (!back) {
    return Result<void>::Failure(back.error);
  }
  return (back.value == value) ? Result<void>::Success()
                               : Result<void>::Failure(Error::WriteMismatch);
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::readStarted(uint8_t reg) noexcept {
  if (!initialized_) {
    return Result<uint16_t>::Failure(Error::NotInitialized);
  }
  const auto r = readPair(reg);
  if (r && isUncertain(reg) && (r.value != detail::RegisterValue(Config{}, reg))) {
    // A read of the chip makes an unknown register known again; one at its power-up value stays
    // unknown, for the next reset check.
    detail::Adopt(config_, reg, r.value);
    forget(reg);
  }
  return r;
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::ReadOutputLatch() noexcept {
  return readStarted(reg::kOutputPort0);
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::ReadInputMask() noexcept {
  return readStarted(reg::kConfigPort0);
}

template <I2cPort Port>
Result<uint16_t> PCAL9555<Port>::ReadPolarityInversion() noexcept {
  return readStarted(reg::kPolarityPort0);
}

template <I2cPort Port>
Result<uint8_t> PCAL9555<Port>::ReadRegister(uint8_t command) noexcept {
  if (!initialized_) {
    return Result<uint8_t>::Failure(Error::NotInitialized);
  }
  if (!reg::IsImplemented(command, isAgile())) {
    return Result<uint8_t>::Failure(Error::InvalidArgument);
  }
  return readByte(command);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::writePair(uint8_t reg, uint16_t value) noexcept {
  const auto v = static_cast<uint32_t>(value);
  const std::array<uint8_t, 2> bytes{static_cast<uint8_t>(v & 0xFFU),
                                     static_cast<uint8_t>(v >> 8U)};
  const Error e = bus_.Write(reg, std::span<const uint8_t, 2>{bytes});
  return (e == Error::Ok) ? Result<void>::Success() : Result<void>::Failure(e);
}

template <I2cPort Port>
Result<void> PCAL9555<Port>::fail(Error error, const char* message) noexcept {
  detail::Log(bus_.GetPort(), LogLevel::Error, message);
  return Result<void>::Failure(error);
}

} // namespace pcal9555

#endif // PCAL9555_IPP
