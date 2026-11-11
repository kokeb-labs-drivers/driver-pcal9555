# Datasheet map

The driver follows the NXP PCAL9555A and PCA9555 data sheets. This page says which revisions, where to get them,
and where each part of the driver is described in them, so you can check any behaviour against the source. Section
names and page numbers are the data sheets' own; the printed page numbers equal the PDF pages.

The PCAL9555A is a superset of the PCA9555 (same pins, address and registers 0x00 to 0x07), and the driver's
behaviour is defined by it. Each topic cites the PCAL9555A data sheet; a second row cites the PCA9555 data sheet
where the driver also runs on that part and its data sheet has its own section.

<!-- generated: datasheet-map -->
- **Datasheet: PCAL9555A Low-voltage 16-bit I2C-bus GPIO with Agile I/O, interrupt and weak pull-up**, NXP PCAL9555A,
  revision Rev. 2 (2014-12-19). Get it from the [vendor page](https://www.nxp.com/products/PCAL9555A) or as a
  [PDF](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf).
  SHA-256 of the file used: `76a60b066b420e86acdd5d01bcd43ef44ca24f2f1eb0e669987f769c315be888`.
- **Datasheet: PCA9555 16-bit I2C-bus and SMBus I/O port with interrupt**, NXP PCA9555, revision Rev. 10 (2017-11-08).
  Get it from the [vendor page](https://www.nxp.com/products/PCA9555) or as a
  [PDF](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf).
  SHA-256 of the file used: `9f4f9c0ce4c461d71e0a46858394f830c74daba72bb6616f368af78a39f44b1f`.

| Topic | Datasheet section | Page | In the driver | Explained on |
| --- | --- | --- | --- | --- |
| Pins and package | PCAL9555A: 5 Pinning information | [p. 4](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=4) | - | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Address pins and the 7-bit address | PCAL9555A: 6.1 Device address | [p. 5](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=5) | `AddressFromPins()`, `kI2cAddress`, `Error::InvalidArgument` | [Hardware](hardware.md), [Porting](porting.md), [Troubleshooting](troubleshooting.md) |
| Registers and command bytes | PCAL9555A: 6.2 Registers; 6.2.1 Pointer register and command byte | [p. 5](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=5) | `pcal9555::reg` (`inc/pcal9555_registers.hpp`), `Config::variant` | [Hardware](hardware.md), [Agile I/O](agile-io.md) |
| Power-up value of every register | PCAL9555A: Table 4 Command byte (power-up default) | [p. 6](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=6) | `Config{}`, `Validate()`, the `reg::k...Reset` constants | [Configuration](configuration.md), [Contract](contract.md) |
| Input, output, polarity inversion and configuration registers | PCAL9555A: 6.2.2 to 6.2.5 Input port, Output port, Polarity inversion and Configuration register pairs | [p. 7](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=7) | `ReadInputs()`, `ReadPin()`, `SetOutputs()`, `WriteOutputs()`, `ToggleOutputs()`, `SetPolarityInversion()`, `SetDirection()` | [API guide](api-guide.md), [Hardware](hardware.md) |
| Output drive strength | PCAL9555A: 6.2.6 Output drive strength register pairs; 8.2 Output drive strength control | [p. 9](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=9) | `SetDriveStrength()`, `DriveStrength`, `DriveStrengthAll()`, `Config::drive_strength_codes` | [Agile I/O](agile-io.md) |
| Input latch | PCAL9555A: 6.2.7 Input latch register pair | [p. 9](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=9) | `SetInputLatch()`, `Config::input_latch_mask` | [Agile I/O](agile-io.md), [Troubleshooting](troubleshooting.md) |
| Pull-up and pull-down resistors | PCAL9555A: 6.2.8 Pull-up/pull-down enable register pair; 6.2.9 Pull-up/pull-down selection register pair | [p. 10](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=10) | `SetPull()`, `Pull`, `Config::pull_enable_mask`, `pull_up_mask` | [Agile I/O](agile-io.md), [Troubleshooting](troubleshooting.md) |
| Interrupt mask | PCAL9555A: 6.2.10 Interrupt mask register pair | [p. 11](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=11) | `SetInterruptEnabled()`, `Config::interrupt_mask` | [Agile I/O](agile-io.md), [Troubleshooting](troubleshooting.md) |
| Interrupt status | PCAL9555A: 6.2.11 Interrupt status register pair | [p. 12](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=12) | `ReadInterruptStatus()`, `ReadInputs()` and `ReadPin()` (status read first while an input is unmasked) | [Agile I/O](agile-io.md), [API guide](api-guide.md), [Hardware](hardware.md) |
| Open-drain ports and the 0x4F write order | PCAL9555A: 6.2.12 Output port configuration register (4Fh) | [p. 12](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=12) | `SetOutputMode()`, `OutputMode`, `Bank`, `Config::port0_mode`, `port1_mode`, the reserved-bits check | [Agile I/O](agile-io.md), [Configuration](configuration.md), [Troubleshooting](troubleshooting.md) |
| Power-on reset | PCAL9555A: 6.4 Power-on reset; 8.3 Power-on reset requirements | [p. 13](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=13) | `CheckForReset()`, `Error::DeviceReset`, `detail::FindResetSentinel()`, the check in `SetDirection()`, `ApplyConfig()` and the setters, `EnsureInitialized()` (writes every register again) | [Hardware](hardware.md), [API guide](api-guide.md), [Status and limitations](limitations.md), [Troubleshooting](troubleshooting.md) |
| Interrupt output (INT) | PCAL9555A: 6.5 Interrupt output | [p. 14](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=14) | `ReadInputs()`, `ReadPin()`, `GetLatchedInterrupts()` | [Hardware](hardware.md), [API guide](api-guide.md), [Troubleshooting](troubleshooting.md) |
| Register pairs, pointer toggling and burst transfers | PCAL9555A: 7 Bus transactions; 7.1 Writing to the port registers; 7.2 Reading the port registers | [p. 14](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=14) | `Write()` and `WriteRead()` of the port, `kRepeatedStart`, every read and write (one pair per transfer), `ReadRegister()` | [Hardware](hardware.md), [Porting](porting.md), [Architecture](architecture.md) |
| Application circuit | PCAL9555A: 8 Application design-in information | [p. 20](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=20) | - | [Hardware](hardware.md), [Quickstart](quickstart.md) |
| Supply range and limiting values | PCAL9555A: 9 Limiting values; 10 Recommended operating conditions | [p. 25](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=25) | - | [Hardware](hardware.md) |
| Pull resistor value, sink current and logic levels | PCAL9555A: 12 Static characteristics | [p. 26](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=26) | - | [Hardware](hardware.md), [Agile I/O](agile-io.md) |
| I2C timing and INT timing | PCAL9555A: 13 Dynamic characteristics | [p. 32](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=32) | `kMaxClockHz` (f_SCL, table 33); no waits, so the port has no `DelayUs()` | [Hardware](hardware.md), [Porting](porting.md) |
| Pins and package | PCA9555: 5 Pinning information | [p. 3](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=3) | - | [Hardware](hardware.md) |
| Address pins and the 7-bit address | PCA9555: 6.1 Device address | [p. 6](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=6) | `AddressFromPins()`, `kI2cAddress` | [Hardware](hardware.md) |
| Registers, command bytes and power-up values | PCA9555: 6.2 Registers; 6.2.1 Command byte (Table 4) | [p. 6](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=6) | `Variant::Pca9555` (registers 0x00 to 0x07 only), `Config{}` | [Hardware](hardware.md), [Agile I/O](agile-io.md), [Configuration](configuration.md) |
| Power-on reset | PCA9555: 6.3 Power-on reset | [p. 8](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=8) | `CheckForReset()`, `Error::DeviceReset`, the check in `SetDirection()`, `EnsureInitialized()` | [Hardware](hardware.md), [API guide](api-guide.md), [Status and limitations](limitations.md), [Troubleshooting](troubleshooting.md) |
| Fixed weak pull-up on inputs | PCA9555: 6.4 I/O port | [p. 8](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=8) | - (`SetPull()` returns `Unsupported`) | [Hardware](hardware.md) |
| Register pairs, pointer toggling and burst transfers | PCA9555: 6.5 Bus transactions; 6.5.1 Writing to the port registers; 6.5.2 Reading the port registers | [p. 9](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=9) | `Write()` and `WriteRead()` of the port, every read and write (one pair per transfer) | [Hardware](hardware.md) |
| Interrupt output (INT), no mask or status | PCA9555: 6.5.3 Interrupt output | [p. 14](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=14) | `ReadInputs()`, `ReadPin()` | [Hardware](hardware.md), [API guide](api-guide.md) |
| Application circuit | PCA9555: 8 Application design-in information | [p. 16](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=16) | - | [Hardware](hardware.md) |
| Supply range, sink current and logic levels | PCA9555: 10 Static characteristics | [p. 18](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=18) | - | [Hardware](hardware.md) |
| I2C timing and INT timing | PCA9555: 11 Dynamic characteristics | [p. 20](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=20) | - | [Hardware](hardware.md) |
<!-- /generated -->

## When the data sheets and the driver differ

The driver is written against the revisions above. If you have a newer revision, compare its "Revision history"
section with the topics above; a change to a register, a power-up value or a bus transaction is a bug report for
this repository. Where the driver goes beyond the data sheets (how a PCA9555 answers a PCAL9555A-only command byte,
which neither data sheet defines), [Hardware](hardware.md#unimplemented-command-bytes) and the
[Contract](contract.md) say so.

The repository never contains the PDFs themselves: vendors own their documents. Download them from NXP, and compare
each file's SHA-256 with the one above to confirm you have the same file.
