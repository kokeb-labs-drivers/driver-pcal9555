# Troubleshooting

Symptoms, causes and fixes, cheapest check first.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Address pins and the 7-bit address: NXP PCAL9555A Rev. 2, 6.1 Device address, [p. 5](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=5)
    - Input latch: NXP PCAL9555A Rev. 2, 6.2.7 Input latch register pair, [p. 9](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=9)
    - Pull-up and pull-down resistors: NXP PCAL9555A Rev. 2, 6.2.8 Pull-up/pull-down enable register pair; 6.2.9 Pull-up/pull-down selection register pair, [p. 10](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=10)
    - Interrupt mask: NXP PCAL9555A Rev. 2, 6.2.10 Interrupt mask register pair, [p. 11](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=11)
    - Open-drain ports and the 0x4F write order: NXP PCAL9555A Rev. 2, 6.2.12 Output port configuration register (4Fh), [p. 12](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=12)
    - Power-on reset: NXP PCAL9555A Rev. 2, 6.4 Power-on reset; 8.3 Power-on reset requirements, [p. 13](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=13)
    - Interrupt output (INT): NXP PCAL9555A Rev. 2, 6.5 Interrupt output, [p. 14](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=14)
    - Power-on reset: NXP PCA9555 Rev. 10, 6.3 Power-on reset, [p. 8](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=8)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## EnsureInitialized returns NoDevice

Nothing acknowledged the address: the port's `GetLastFailure()` saw the address byte NACKed. Check the supply of
the expander, then A2, A1 and A0 against the address you passed (`AddressFromPins(a2, a1, a0)`). A port without
`GetLastFailure()` reports the same fault as `Bus`.

## EnsureInitialized returns Bus

The port reported a failure: its `EnsureInitialized()` returned `false`, or a transfer did. Without the optional
`GetLastFailure()` hook the driver cannot tell a NACK from a bus error, so the first transfer failing usually
means that nothing answers at the address; with it, `Bus` means the bus itself failed (a stuck line, lost
arbitration, a timeout) or the part refused a command byte.

1. Check that your port's `EnsureInitialized()` returns `true` on its own (log it).
2. Check A2, A1 and A0 against the address you passed: `AddressFromPins(a2, a1, a0)`. The pins must be tied to
   VDD or VSS, never left open.
3. Check the pull-ups on SDA and SCL (to the expander's VDD) and the supply.
4. Read input port 0 at the address with your SDK (one write of 0x00, a repeated START, a one-byte read): an
   address that answers here and not in the driver points at the port's address handling (7-bit, not shifted).
   Avoid zero-length "scan" transfers; the driver never sends one (handbook KB-CPP-130).
5. With `Variant::Pcal9555a`: a failure at the second transfer (the 0x4F read) means the part NACKs the Agile I/O
   bank, so it is a PCA9555. Use `Variant::Pca9555`.

## EnsureInitialized returns DeviceIdMismatch

Something answers at the address, but a register did not read back what was written, or a reserved bit of 0x4F
read 1 on a driver configured for the PCAL9555A.

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Only with `Variant::Pcal9555a` | A PCA9555 that does not NACK 0x4F | Use `Variant::Pca9555`, or fit a PCAL9555A |
| With either variant | Another part at the address (a different I/O expander, an EEPROM page, a sensor) | Check the part number and the address pins |
| Now and then | Corrupted bytes on the bus | Lower the clock; check the pull-ups and the wiring length |

## EnsureInitialized returns Unsupported

An Agile I/O field of `Config` differs from its power-up value while `variant` is `Pca9555`. Set
`variant = Variant::Pcal9555a` if a PCAL9555A is fitted, or leave the field at its default.

## EnsureInitialized returns InvalidArgument

The address is outside 0x20 to 0x27 (an 8-bit address such as 0x40 is the 7-bit 0x20 shifted), or a `Config`
field holds a value that is not an enumerator. Nothing was sent.

## A call returns DeviceReset

The driver found a register back at its power-up value while its copy says otherwise: the expander went through a
power-on reset (a supply dip below the power-on reset voltage, section 6.4), or the chip dropped a write to that
register. Nothing was written, and the driver is uninitialised: call `EnsureInitialized()`, which writes every
register again with the outputs before the directions.

- Now and then: check the expander's supply (decoupling, a load that pulls it down when outputs switch, the ramp in
  section 8.3), then the bus.
- After every output change: the outputs draw more current than the supply gives; check the total current limits
  ([Hardware](hardware.md#power)).

## An output does not change

- A call returned `WriteMismatch`: the chip acknowledged the write but did not keep it. Retry (after
  `ToggleOutputs()`, with `WriteOutputs(GetConfig().output_high_mask)`); if it keeps
  happening, check the pull-ups, the bus length and noise near the lines, and compare `ReadOutputLatch()` with
  `GetConfig().output_high_mask`.
- The pin is still an input: `SetDirection(mask, Direction::Output)`.
- The port is open-drain (PCAL9555A, `SetOutputMode()`): a "high" output floats and needs an external pull-up.
- A call returned `Bus`: the write may or may not have arrived. `GetConfig()` shows the riskier value (a pin that
  may be an output counts as one, outputs at the written level); the next call reads the register first, and
  `ReadOutputLatch()` / `ReadInputMask()` show what the chip holds.
- The chip lost power since the start: it is back at its power-up state, every pin an input. `CheckForReset()`
  returns `DeviceReset`; `EnsureInitialized()` then writes the driver's copy again.

## An input reads the wrong level

- Polarity inversion is on for the pin (`GetConfig().polarity_invert_mask`).
- On a PCAL9555A the input is latched (`input_latch_mask`): the read shows the level that raised the interrupt, and
  the next read shows the pin.
- A floating input: on the PCAL9555A check `pull_enable_mask` and `pull_up_mask`.

## INT stays low or never falls

| Symptom | Likely cause | Fix |
| --- | --- | --- |
| Never falls on a PCAL9555A | Every pin is masked at power-up | `SetInterruptEnabled(mask, true)` |
| `GetLatchedInterrupts()` is empty though INT fell | The input port was read with `ReadRegister()`, which clears the status without latching it | Use `ReadInputs()` or `ReadPin()`, which latch the status first while an input is unmasked |
| Stays low | The input port of the pin that raised it was not read | `ReadInputs()` (reads both ports); `ReadPin()` releases only its own port |
| Falls when a pin becomes an input | The data sheet's false interrupt: the pin level differs from the input register | Read the inputs once after changing directions |
| Never falls on either part | No pull-up on INT, or the pin is an output | INT is open-drain; only inputs raise it |

## Bench checks

Hardware-dependent behaviour is **not verified on hardware** yet. With a PCAL9555A (or PCA9555) at 0x20 on an MCU
port, run the ESP32 example or your own code:

1. `EnsureInitialized()` returns `Ok`; a logic analyser shows a read of 0x00 (and of 0x4F for a PCAL9555A), the
   register writes and their read-backs, each pair in one transfer.
2. An LED on P0_0 follows `ToggleOutputs(PinMask(0))`.
3. `ReadPin(8)` follows a button on P1_0; INT falls on a press and rises after `ReadInputs()`.
4. A PCA9555 driven with `Variant::Pcal9555a` fails with `Bus` (or `DeviceIdMismatch`) and the analyser shows no
   write: this checks the data sheets' silence about unimplemented command bytes.
5. With the address pins at another level, `EnsureInitialized()` with the default address returns `Bus`, or
   `NoDevice` with a port that has `GetLastFailure()` (the host example port does; the ESP32 one cannot).
6. `GetLatchedInterrupts()` keeps a pin after `ReadInputs()` has released INT, until `ClearLatchedInterrupts()`.
7. Power-on reset: configure P0_0 as an open-drain output driven low into a load (an LED to VDD through a resistor),
   then pull the expander's VDD below the power-on reset voltage for longer than the section 8.3 minimum while the
   MCU keeps running. `ReadInputMask()` reads 0xFFFF; `CheckForReset()` returns `DeviceReset`, and
   `SetDirection(PinMask(8), Direction::Output)` returns `DeviceReset` with the analyser showing one read and no
   write, and the LED stays off. `EnsureInitialized()` brings P0_0 back low and open-drain. This checks that a real
   dip reloads table 4 as the model does.
