# Figures

<!-- Written by kokeb-docs figures from docs/diagrams/figures/*.py. Do not edit by hand. -->

Every figure on this site is drawn by a Python module from facts extracted from the code, so it is rebuilt,
not redrawn, when the code changes. CI runs `kokeb-docs figures --check` and fails when a
committed figure no longer matches the code. Each figure below lists the files it is drawn from.

## Inside the PCAL9555A

<!-- figure: chip-blocks -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-blocks.dark.svg">
  <img src="chip-blocks.svg"
    alt="Block diagram of the PCAL9555A: its function blocks, how they connect and which package pins reach
    each block.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/pcal9555_i2c_interface.hpp`](../../inc/pcal9555_i2c_interface.hpp)

## PCAL9555A: Typical application

<!-- figure: chip-circuit-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-circuit-typical.dark.svg">
  <img src="chip-circuit-typical.svg"
    alt="Schematic of the PCAL9555A typical application: the chip with U2 MCU, R1 resistor 10 kΩ, R2 resistor
    10 kΩ, R3 resistor 10 kΩ, C1 capacitor 100 nF, R4 resistor 2 kΩ, D1 led; the MCU wires carry the
    port hooks Write() / WriteRead().">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/pcal9555_i2c_interface.hpp`](../../inc/pcal9555_i2c_interface.hpp)

## PCAL9555A pinout, TSSOP-24

<!-- figure: chip-pinout -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-pinout.dark.svg">
  <img src="chip-pinout.svg"
    alt="Pinout of the PCAL9555A in TSSOP-24, top view: 24 pins, each with its name, its electrical type as a
    colour and the driver hook that uses it.">
</picture>
<!-- /figure -->

Hardware description: [`docs/hardware.yml`](../hardware.yml)

Drawn from:

- [`docs/hardware.yml`](../hardware.yml)
- [`inc/pcal9555_i2c_interface.hpp`](../../inc/pcal9555_i2c_interface.hpp)

## The layers, one of them yours

<!-- figure: driver-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="driver-layers.dark.svg">
  <img src="driver-layers.svg"
    alt="Layer diagram: your application calls the PCAL9555 core, which sends register reads and writes
    through the I2C port contract to the port class you write, which drives the chip; the core and the
    contract are the same on every MCU.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/driver-layers.py`](../../docs/diagrams/figures/driver-layers.py)

Drawn from:

- `public calls` in [`inc/pcal9555.hpp`](../../inc/pcal9555.hpp)
- `I2cInterface` in [`inc/pcal9555_i2c_interface.hpp`](../../inc/pcal9555_i2c_interface.hpp)

## One pin call, and what each outcome leaves

<!-- figure: pin-write -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="pin-write.dark.svg">
  <img src="pin-write.svg"
    alt="Flow chart of SetOutputs(): compute the new pair from the driver's copy, write it, read it back when
    Verify::ReadBack is passed, then Ok, Bus or WriteMismatch; ReadOutputLatch() reads what the chip
    holds.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/pin-write.py`](../../docs/diagrams/figures/pin-write.py)

Drawn from:

- `SetOutputs, writeMask, storeMask, writePairChecked, ReadOutputLatch` in [`src/pcal9555.ipp`](../../src/pcal9555.ipp)
- `Verify` in [`inc/pcal9555_types.hpp`](../../inc/pcal9555_types.hpp)
- `kOutputPort0` in [`inc/pcal9555_registers.hpp`](../../inc/pcal9555_registers.hpp)

## The register map

<!-- figure: register-map -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="register-map.dark.svg">
  <img src="register-map.svg"
    alt="Register map: the four standard register pairs at 0x00 to 0x07 on both parts and the Agile I/O
    registers at 0x40 to 0x4F on the PCAL9555A only, each with its power-up value, its Config field and
    the calls that use it.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/register-map.py`](../../docs/diagrams/figures/register-map.py)

Drawn from:

- `register constants` in [`inc/pcal9555_registers.hpp`](../../inc/pcal9555_registers.hpp)
- `StandardPairs, AgilePairs, OutputPortConfig` in [`inc/pcal9555.hpp`](../../inc/pcal9555.hpp)
- `calls` in [`src/pcal9555.ipp`](../../src/pcal9555.ipp)

## What EnsureInitialized() does

<!-- figure: start-up -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="start-up.dark.svg">
  <img src="start-up.svg"
    alt="Flow chart of EnsureInitialized(): check the configuration and the address, bring the bus up, check
    that the chip answers, write every register with the outputs before the directions, read them all
    back, then started; each failing step returns its error.">
</picture>
<!-- /figure -->

Figure module: [`docs/diagrams/figures/start-up.py`](../../docs/diagrams/figures/start-up.py)

Drawn from:

- `EnsureInitialized, initialize, checkPresence, writeAll, verifyAll` in [`src/pcal9555.ipp`](../../src/pcal9555.ipp)
- `StandardPairs, AgilePairs` in [`inc/pcal9555.hpp`](../../inc/pcal9555.hpp)
- `Validate, kMinI2cAddress, kMaxI2cAddress` in [`inc/pcal9555_types.hpp`](../../inc/pcal9555_types.hpp)
- `register constants` in [`inc/pcal9555_registers.hpp`](../../inc/pcal9555_registers.hpp)
