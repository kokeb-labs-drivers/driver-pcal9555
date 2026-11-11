/**
 * @file api_tasks.cpp
 * @brief The API guide's and the Agile I/O page's examples, compiled by the host build (never run).
 */
#include <cstdio>

#include "my_port.hpp"
#include "pcal9555.hpp"

using Expander = pcal9555::PCAL9555<MyPort>;

// --8<-- [start:lifecycle]
constexpr pcal9555::Config Agile() {
  pcal9555::Config c{};
  c.variant = pcal9555::Variant::Pcal9555a;
  return c;
}

pcal9555::Error BringUp(Expander& io) {
  // Expander io(port, 0x20, Agile()); stores the configuration, no I/O.
  const auto r = io.EnsureInitialized(); // first call starts the chip; then Ok, no I/O
  if (!r) {
    std::printf("PCAL9555A start: %s\n", pcal9555::ToString(r.error)); // the next call retries
  }
  return r.error;
}
// --8<-- [end:lifecycle]

// --8<-- [start:outputs]
pcal9555::Error DriveRelays(Expander& io) {
  constexpr uint16_t kRelays = 0x000F;              // P0_0..P0_3
  if (auto r = io.SetOutputs(kRelays, false); !r) { // level first: off
    return r.error;
  }
  if (auto r = io.SetDirection(kRelays, pcal9555::Direction::Output); !r) {
    return r.error;
  }
  return io.ToggleOutputs(pcal9555::PinMask(2)).error; // relay 2 on, one write
}
// --8<-- [end:outputs]

// --8<-- [start:readback]
// Enable a motor driver, retrying a write the chip acknowledged but lost.
pcal9555::Error EnableDriver(Expander& io) {
  constexpr uint16_t kEnable = pcal9555::PinMask(9); // P1_1
  pcal9555::Result<void> r{};
  for (int attempt = 0; attempt < 3; ++attempt) {
    r = io.SetOutputs(kEnable, true, pcal9555::Verify::ReadBack); // write, then read back
    if (r.error != pcal9555::Error::WriteMismatch) {
      break;
    }
  }
  if (!r) {
    return r.error;
  }
  const auto latch = io.ReadOutputLatch(); // what the chip drives, not the driver's copy
  return latch && (latch.value & kEnable) != 0 ? pcal9555::Error::Ok
                                               : pcal9555::Error::WriteMismatch;
}
// --8<-- [end:readback]

// --8<-- [start:inputs]
pcal9555::Result<uint16_t> ReadSwitches(Expander& io) {
  // Active-low switches read as 1 when closed; only input pins are inverted.
  if (auto r = io.SetPolarityInversion(0xFF00, true); !r) {
    return pcal9555::Result<uint16_t>::Failure(r.error);
  }
  const auto in = io.ReadInputs(); // both ports in one transfer; clears INT
  if (!in) {
    return in;
  }
  return pcal9555::Result<uint16_t>::Success(static_cast<uint16_t>(in.value >> 8U));
}
// --8<-- [end:inputs]

// --8<-- [start:agile]
pcal9555::Error SetUpButtons(Expander& io) {
  constexpr uint16_t kButtons = 0xF000; // P1_4..P1_7, to ground
  if (auto r = io.SetPull(kButtons, pcal9555::Pull::Up); !r) {
    return r.error; // Unsupported on a driver configured for the PCA9555
  }
  if (auto r = io.SetInputLatch(kButtons, true); !r) { // a short press is not lost
    return r.error;
  }
  return io.SetInterruptEnabled(kButtons, true).error; // INT falls on a press
}
// --8<-- [end:agile]

// --8<-- [start:interrupt]
// Call from a task when INT went low (never from the ISR: every call blocks on the bus).
pcal9555::Result<uint16_t> ServiceInterrupt(Expander& io, uint16_t& levels) {
  const auto source = io.ReadInterruptStatus(); // which pins; does not clear INT
  if (!source) {
    return source;
  }
  const auto in = io.ReadInputs(); // latched levels; clears INT
  if (!in) {
    return pcal9555::Result<uint16_t>::Failure(in.error);
  }
  levels = in.value;
  return source;
}
// --8<-- [end:interrupt]

// --8<-- [start:drive]
pcal9555::Error QuietLeds(Expander& io) {
  // Less drive, slower edges, less ground bounce: LEDs on port 0 at a quarter of full drive.
  if (auto r = io.SetDriveStrength(0x00FF, pcal9555::DriveStrength::Quarter); !r) {
    return r.error;
  }
  return io.SetOutputMode(pcal9555::Bank::Port1, pcal9555::OutputMode::OpenDrain).error;
}
// --8<-- [end:drive]

// --8<-- [start:errors]
const char* Describe(pcal9555::Error e) {
  switch (e) {
  case pcal9555::Error::NoDevice:
    return "nothing acknowledges the address: check the address pins and the supply";
  case pcal9555::Error::Bus:
    return "a transfer failed: check the pull-ups, the bus length and the supply";
  case pcal9555::Error::DeviceIdMismatch:
    return "another part answers at this address, or the variant is wrong";
  case pcal9555::Error::DeviceReset:
    return "the expander reset (supply dip): call EnsureInitialized() again";
  case pcal9555::Error::Unsupported:
    return "an Agile I/O feature on a PCA9555: set Config::variant if a PCAL9555A is fitted";
  default:
    return pcal9555::ToString(e);
  }
}
// --8<-- [end:errors]

// --8<-- [start:reset]
// Once per control period: a supply dip resets the expander without a word (section 6.4).
pcal9555::Result<void> KeepStarted(Expander& io) {
  if (const auto r = io.CheckForReset(); r.error != pcal9555::Error::DeviceReset) {
    return r; // Ok (one read, more only after a failed write), Bus or NoDevice
  }
  return io.EnsureInitialized(); // writes every register again, outputs before directions
}
// --8<-- [end:reset]

// --8<-- [start:diagnostics]
// Bring-up: compare what was asked for, what the chip last reported and what it holds now.
void Diagnose(Expander& io) {
  const uint16_t asked = io.GetConfig().output_high_mask; // the driver's copy, no I/O
  const uint16_t seen = io.GetLastOutput();               // the last output read, no I/O
  const auto oconf = io.ReadRegister(pcal9555::reg::kOutputPortConfig); // one read, no retry
  const uint16_t changed = io.GetLatchedInterrupts(); // every status read since the clear
  io.ClearLatchedInterrupts();
  std::printf("outputs asked 0x%04X, seen 0x%04X; 0x4F %s 0x%02X; changed 0x%04X\n",
              static_cast<unsigned>(asked), static_cast<unsigned>(seen),
              pcal9555::ToString(oconf.error), static_cast<unsigned>(oconf.value),
              static_cast<unsigned>(changed));
}
// --8<-- [end:diagnostics]

// --8<-- [start:deinit]
void Release(Expander& io) {
  if (auto r = io.Deinitialize(); !r) { // every pin back to an input
    std::printf("PCAL9555 release failed: %s\n", pcal9555::ToString(r.error));
  }
  // io.EnsureInitialized() later writes the configuration again, pin changes included.
}
// --8<-- [end:deinit]
