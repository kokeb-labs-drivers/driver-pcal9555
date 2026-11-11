# Status and limitations

What this driver has and has not been shown to do, so you can decide whether it is ready for your use.

## Status

| | |
| --- | --- |
| **Version** | 0.1.0, not released |
| **On hardware** | **This driver is not verified yet**: see [Before you depend on it](#before-you-depend-on-it) |
| **Tested against** | A protocol model of both parts ([Testing and CI](testing.md)), a fuzzer and the quality gates |
| **Assurance level** | `high` (Kokeb Labs handbook): not qualified for use in a safety function |

## Design choices the data sheets call for

Each of these follows from the PCAL9555A data sheet (Rev. 2); the [Datasheet map](datasheet.md) links the sections.

- **An acknowledged write can still be lost or altered.** An I2C acknowledge confirms that a byte was received; it
  carries no checksum, and the data sheet itself allows for a "glitch or data corruption" (section 8.3). The output
  port registers read back the value written, not the pin (section 6.2.3), so a read-back shows what the chip
  holds. The driver reads a write back when the call asks for it (`Verify::ReadBack`, the last argument of every
  setter; off by default) and then returns `WriteMismatch`; the model can drop writes, and the tests and the fuzzer
  cover it.
- **Reading the inputs does not identify the part.** The input ports hold the pin levels (section 6.2.2), so any
  value is plausible, and a device that acknowledges every byte answers the read. At start-up the driver writes a
  test pattern to the polarity inversion pair, which changes only how inputs read (section 6.2.4), and reads it
  back, then writes every register and reads each back.
- **Outputs are written before directions**, so no pin glitches: a pin that becomes an output drives its output
  latch at once (section 6.2.5), and the latch is high after power-up (table 4).
- **A power-on reset is found in the registers.** Neither part has a reset pin or a reset flag; a supply dip
  below the power-on reset voltage reloads every register (section 6.4, table 4): outputs high, push-pull, every pin
  an input. The driver reads one register whose copy differs from its power-up value (`CheckForReset()`, and first
  in `SetDirection()`, `ApplyConfig()` and a setter about to rewrite that register) and returns `DeviceReset`,
  uninitialised, instead of writing its copy over a reset chip. A `SetDirection()` that makes pins outputs writes
  0x4F, the output pair and the configuration pair, in that order (section 6.2.12), so even a reset between its
  check and its writes never turns a pin into an output at the reset level. Releasing pins is the safe direction
  and goes the other way: when every pin ends up an input the configuration pair is the only transfer, with no
  check; when outputs remain, the check, then the configuration pair first, then 0x4F and the output pair for the
  pins that stay outputs. A reset in the microseconds between that check and the configuration write leaves those
  remaining outputs at the power-up level until the two writes after it land. A dropped write to the checked
  register reads as a reset, with the same recovery. A reset is found at the next check, not when it happens: call
  `CheckForReset()` as often as a dip matters.
- **A failed write may have arrived.** An I2C failure can come after the bytes were clocked in (a NACK of the
  last byte, a lost ACK). After a write that failed with `Bus` the copy takes the riskier of the old and the asked
  value (an output rather than an input, the written level, the stronger drive, push-pull) and the register is
  unknown until a read or a successful write; the next call that builds on it reads it first. An unknown register
  read back at its power-up value is taken as a reset (`DeviceReset`), and any other value is adopted; the reset
  check never reads an unknown register as its sentinel, and with no other register to read it reads the unknown
  ones. The rule needs no record of what the failed write may have left, at a price: when that write's old or new
  value was the power-up value, the read gives one false `DeviceReset`. Nothing is written, `EnsureInitialized()`
  writes the copy again, and nothing stays stuck. `GetConfig()` shows the riskier value for the direction, the
  drive strength and 0x4F, but the written level for the outputs, which is not necessarily what the pins do:
  `ReadOutputLatch()` reads it.
- **An input read latches the interrupt status first.** Reading an input port clears its interrupt status
  (sections 6.2.11, 6.5), so with an unmasked input on a PCAL9555A the driver reads the status pair before the
  inputs, into `GetLatchedInterrupts()`: one more read per input read. The two reads are separate transfers: an
  edge between them sets a status bit that the input read then clears (section 6.5), so that bit never reaches
  `GetLatchedInterrupts()`. The edge still shows in the level the input read returns (with the input latch, the
  latched level), so compare the returned value with the last one (`GetLastInputs()`) when no edge may be missed.
- **The driver never waits.** The chip has no busy state and needs no delay between transfers (section 7), so a
  call's duration is the port's transfers; the port's timeout bounds every call
  ([API guide](api-guide.md#use-it-from-a-handler)).

## What the model cannot show

The model is written from the data sheets by the same reading as the driver. It cannot show:

- how a PCA9555 answers the PCAL9555A-only command 0x4F. The driver assumes a NACK (or a value with reserved bits
  set), so that a PCAL9555A configuration on a PCA9555 fails at start-up; no PCA9555 has been tried;
- electrical behaviour: drive strength, the 100 kΩ pulls, open-drain levels, the INT pin's timing;
- what another master does to the registers between two calls. A supply dip is modelled as the data sheet
  describes it, every register back at its power-up value (section 6.4); how a real supply ramp behaves near the
  power-on reset voltage, and whether a partial dip can leave some registers changed, is **not verified on
  hardware** (bench step 7 in [Troubleshooting](troubleshooting.md#bench-checks)).

## Before you depend on it

1. Run it behind your handler on your board through the procedure your firmware already uses, and compare
   `ReadOutputLatch()` with what you set.
2. Report the board, the driver commit and the result in an issue; a verified board is listed below.

## Hardware verification record (this driver)

| Board | Driver commit | Procedure | Result |
| --- | --- | --- | --- |
| (none yet) | | | |
