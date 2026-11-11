# Agile I/O

The PCAL9555A's additions to the PCA9555: output drive strength, pull resistors, input latch, interrupt mask and
status, and open-drain ports. Section numbers refer to the
[PCAL9555A data sheet](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf) (Rev. 2).

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Registers and command bytes: NXP PCAL9555A Rev. 2, 6.2 Registers; 6.2.1 Pointer register and command byte, [p. 5](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=5)
    - Output drive strength: NXP PCAL9555A Rev. 2, 6.2.6 Output drive strength register pairs; 8.2 Output drive strength control, [p. 9](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=9)
    - Input latch: NXP PCAL9555A Rev. 2, 6.2.7 Input latch register pair, [p. 9](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=9)
    - Pull-up and pull-down resistors: NXP PCAL9555A Rev. 2, 6.2.8 Pull-up/pull-down enable register pair; 6.2.9 Pull-up/pull-down selection register pair, [p. 10](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=10)
    - Interrupt mask: NXP PCAL9555A Rev. 2, 6.2.10 Interrupt mask register pair, [p. 11](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=11)
    - Interrupt status: NXP PCAL9555A Rev. 2, 6.2.11 Interrupt status register pair, [p. 12](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=12)
    - Open-drain ports and the 0x4F write order: NXP PCAL9555A Rev. 2, 6.2.12 Output port configuration register (4Fh), [p. 12](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=12)
    - Pull resistor value, sink current and logic levels: NXP PCAL9555A Rev. 2, 12 Static characteristics, [p. 26](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=26)
    - Registers, command bytes and power-up values: NXP PCA9555 Rev. 10, 6.2 Registers; 6.2.1 Command byte (Table 4), [p. 6](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=6)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Which part is fitted

The two parts are pin compatible and the PCAL9555A answers every PCA9555 register the same way, so a board may
carry either. The driver does not detect the part (neither data sheet says how a PCA9555 answers the Agile I/O
command bytes): `Config::variant` names it.

| `Config::variant` | Registers used | Agile I/O calls | Agile I/O `Config` fields |
| --- | --- | --- | --- |
| `Variant::Pca9555` (default) | 0x00 to 0x07 | `Unsupported`, nothing sent | Must keep their power-up value, else `Unsupported` |
| `Variant::Pcal9555a` | 0x00 to 0x07 and 0x40 to 0x4F | Work | Written and read back by `EnsureInitialized()` |

`Pca9555` is safe on both parts: a PCAL9555A driven that way keeps its Agile I/O registers at their power-up values
(pull-ups on, interrupts masked, full drive, push-pull). `Pcal9555a` on a PCA9555 fails the start-up before any
write ([Hardware](hardware.md#unimplemented-command-bytes)).

```cpp
--8<-- "tests/docs/api_tasks.cpp:agile"
```

## Drive strength

Section 6.2.6. Each output has four drive levels, two bits per pin in 0x40 to 0x43. A lower level slows the edges
and reduces ground bounce and EMI on lightly loaded lines; it also lowers the current the pin can sink.

| `DriveStrength` | Code | Of full drive |
| --- | --- | --- |
| `Quarter` | 00b | 0.25 |
| `Half` | 01b | 0.5 |
| `ThreeQuarters` | 10b | 0.75 |
| `Full` | 11b | 1 (power-up) |

`SetDriveStrength(mask, level)` writes both drive strength pairs. In `Config`, `drive_strength_codes` holds the
codes in the register layout, pin n at bits 2n+1..2n; `DriveStrengthAll(level)` builds a value with every pin at
one level.

```cpp
--8<-- "tests/docs/api_tasks.cpp:drive"
```

## Pull resistors

Sections 6.2.8 and 6.2.9. Each pin has a 100 kΩ (50 kΩ to 150 kΩ) resistor that can pull up, pull down or be
disconnected; at power-up every pin has its pull-up on.

| `Pull` | Enable bit (0x46, 0x47) | Selection bit (0x48, 0x49) |
| --- | --- | --- |
| `None` | 0 | unchanged |
| `Up` | 1 | 1 |
| `Down` | 1 | 0 |

`SetPull()` writes the selection before the enable, so that a pin switched from "off" to "down" never pulls up in
between. A port in open-drain mode disconnects its resistors whatever these registers say.

## Input latch

Section 6.2.7. Without the latch an input that changes and changes back before it is read leaves no trace: INT
releases and the input register shows the current level. With the latch on (`SetInputLatch(mask, true)`), the
input register keeps the level that raised the interrupt, and INT stays asserted, until the input port is read; the
next read shows the pin again. Use it for short pulses such as button presses or a sensor's data-ready strobe.

## Interrupt mask and status

Sections 6.2.10 and 6.2.11. At power-up every pin is masked: unlike the PCA9555, a PCAL9555A raises no interrupt
until `SetInterruptEnabled(mask, true)` clears mask bits. Unmasking a pin whose change is still pending asserts INT
at once. `ReadInterruptStatus()` returns the pins that raised the interrupt; masked pins read 0. Reading the status
does not clear the interrupt; reading the input port does.

## Output mode

Section 6.2.12. `SetOutputMode(Bank::Port0 or Bank::Port1, mode)` selects push-pull or open-drain for a whole port
(ODEN0, ODEN1 in 0x4F). An open-drain output only pulls low; a "high" output lets the line float to an external
pull-up, for wired-OR lines or a load on another supply. The data sheet recommends setting 0x4F before the
configuration registers make the pins outputs; `EnsureInitialized()` and `ApplyConfig()` write it in that order,
and a `SetOutputMode()` call should come before the `SetDirection()` call.

## Not verified on hardware

The behaviour above is the data sheet's, checked against the protocol model in the host tests. On a bench, with a
PCAL9555A at 0x20:

1. `SetDriveStrength(PinMask(0), DriveStrength::Quarter)` with P0_0 an output toggling into 50 pF: the edges are
   slower than at `Full` on an oscilloscope.
2. With P1_0 an input and nothing connected, `SetPull(PinMask(8), Pull::Down)` makes `ReadPin(8)` read low,
   `Pull::Up` high.
3. With P1_0 latched and unmasked, a 10 µs low pulse on it asserts INT, and the next `ReadInputs()` shows bit 8 low
   once, then high.
4. With port 1 open-drain, a 10 kΩ pull-up to 3.3 V on P1_0 and the output "high", the pin reads 3.3 V; "low" pulls
   it to ground.
