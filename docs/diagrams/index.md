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

## What the PCAL9555A does

<!-- figure: chip-capabilities -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-capabilities.dark.svg">
  <img src="chip-capabilities.svg"
    alt="Capability map of the PCAL9555A: Pin direction (supported): Makes each of the 16 pins an input or an
    output; every pin is an input after power-on; Outputs (supported): Sets, writes or toggles latched
    outputs that sink up to 25 mA, enough to light an LED; Inputs and polarity (supported): Reads the
    level of every pin, each one optionally inverted; Interrupt (supported): Pulls the open-drain INT
    low on an input change; the PCAL9555A adds a mask and the source per pin; Pull-up and pull-down
    (supported): Enables a 100 kΩ pull-up or pull-down per pin, so a button needs no external resistor;
    Input latch (supported): Keeps a short input change until the input port is read, so a pulse is not
    missed; Output drive strength (supported): Sets one of four output current levels per pin, to tune
    the edge rate and the switching noise; Open-drain outputs (supported): Switches each 8-pin port
    between push-pull and open-drain outputs; Address and reset (supported): Finds the chip at one of
    eight addresses and notices a power-on reset mid-session.">
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

## PCAL9555A

<!-- figure: chip-hero -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-hero.dark.svg">
  <img src="chip-hero.svg"
    alt="Overview of the PCAL9555A, a 16-bit I/O expander that adds sixteen inputs or outputs on an I2C bus,
    with an interrupt line for input changes and, as the Agile I/O of the PCAL9555A, pull resistors,
    input latches and drive strength per pin. A MCU talks to it over I2C; it drives LEDs and loads; it
    reads back Buttons and switches. Headline figures: 16 I/O pins in two 8-bit ports; 1.65 to 5.5 V
    supply; 5 V tolerant I/O; 400 kHz I2C, Fast-mode; 25 mA output sink, enough for an LED; 8 chips on
    one bus, address 0x20 to 0x27; 1.0 µA standby, typical at 3.3 V.">
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

## Wiring: Typical application

<!-- figure: chip-wiring-typical -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="chip-wiring-typical.dark.svg">
  <img src="chip-wiring-typical.svg"
    alt="Wiring of the PCAL9555A for the Typical application: MCU I2C SCL to SCL (Write() / WriteRead()); MCU
    I2C SDA to SDA (Write() / WriteRead()); MCU GPIO (IRQ) to INT. Supplies, grounds and the support
    parts are listed under the drawing.">
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

## One call through the layers

<!-- figure: one-call-through-the-layers -->
<picture>
  <source media="(prefers-color-scheme: dark)" srcset="one-call-through-the-layers.dark.svg">
  <img src="one-call-through-the-layers.svg"
    alt="One call through the layers: your code calls SetOutputs(PinMask(3), false), the core exchanges
    frames with the chip through your port and returns the result to your code.">
</picture>
<!-- /figure -->

Hardware description: [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

Drawn from:

- [`docs/model/one-call-through-the-layers.seq.yml`](../model/one-call-through-the-layers.seq.yml)

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
