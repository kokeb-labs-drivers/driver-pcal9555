---
hide:
  - navigation
  - toc
---

<div class="kb-hero" markdown>
<span class="kb-kicker">Driver</span>

# PCAL9555 Driver

<p>A C++20 driver for the NXP PCA9555 and PCAL9555A 16-bit I2C GPIO expanders. It drives outputs, reads inputs
and interrupt sources, and on the PCAL9555A sets drive strength, pull resistors, input latches and open-drain
ports, on any MCU.</p>

[Quickstart](quickstart.md){ .md-button .md-button--primary }
[API guide](api-guide.md){ .md-button }
[Source on GitHub](https://github.com/kokeb-labs-drivers/driver-pcal9555){ .md-button }
</div>

The driver has no platform code. You give it a small class that brings the bus up and moves bytes; it does the
rest: the presence and identity checks, the register pairs, the write order that avoids glitches, and a copy of
every register so that a pin change is one transfer, and the check that finds a power-on reset before a stale copy
is written over it.

<div class="kb-chip" markdown>
<div class="kb-chip__head">
<span class="kb-chip__kicker">16-bit I2C GPIO expander</span>
<span class="kb-chip__name">PCA9555 / PCAL9555A</span>
<span class="kb-chip__vendor">NXP Semiconductors</span>
</div>

Function
:   16 I/O pins in two 8-pin ports; each pin an input or a latched output, with input polarity inversion

Interface
:   I2C up to 400 kHz; 7-bit address `0x20` to `0x27` from the A2, A1 and A0 pins

Interrupt
:   Open-drain INT on any input change, cleared by reading the input port

Agile I/O (PCAL9555A)
:   Four drive strengths per pin, 100 kΩ pull-up or pull-down per pin, input latch, interrupt mask and status,
    push-pull or open-drain per port

Supply
:   PCA9555 2.3 V to 5.5 V; PCAL9555A 1.65 V to 5.5 V
</div>


## What you get

- **One object per chip.** `PCAL9555<YourPort>` takes your port, the address and a `Config`, and does no I/O until
  `EnsureInitialized()`. See [API guide](api-guide.md).
- **Both parts, one API.** `Config::variant` names the part; on a PCA9555 the Agile I/O calls return
  `Unsupported` without touching the bus. See [Agile I/O](agile-io.md).
- **Errors you can act on.** Every call returns `Result<T>` with an `Error` such as `Bus`, `DeviceIdMismatch` or
  `Unsupported`. See [API guide](api-guide.md#handle-errors).
- **Datasheet defaults.** `Config{}` holds the power-up value of every register. See
  [Configuration](configuration.md).
- **A tiny port.** Three bus hooks and an optional log hook, checked by a concept at compile time. The chip needs
  no delays and has no reset pin. See [Porting](porting.md).
- **No heap, no exceptions, no RTTI.** The host tests build with `-fno-exceptions -fno-rtti`. See
  [Contract](contract.md).
- **Tested without hardware.** Host tests run the driver against a protocol model of the chip, with every bus
  transfer failed in turn. See [Testing and CI](testing.md).

## Datasheet

<!-- generated: datasheet-card -->
| Document | Revision | Get it |
| --- | --- | --- |
| Datasheet: PCAL9555A Low-voltage 16-bit I2C-bus GPIO with Agile I/O, interrupt and weak pull-up (NXP PCAL9555A) | Rev. 2, 2014-12-19 | [PDF](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf) · [vendor page](https://www.nxp.com/products/PCAL9555A) |
| Datasheet: PCA9555 16-bit I2C-bus and SMBus I/O port with interrupt (NXP PCA9555) | Rev. 10, 2017-11-08 | [PDF](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf) · [vendor page](https://www.nxp.com/products/PCA9555) |

Written against these revisions; the SHA-256 of each file is in [`docs/datasheets.yml`](datasheets.yml). Where each part
of the driver is in the datasheet: [Datasheet map](datasheet.md).
<!-- /generated -->

## Where to go next

<div class="grid cards" markdown>

-   :material-rocket-launch-outline: **[Quickstart](quickstart.md)**

    ---

    Wire the chip, write the port, blink an LED and read a button.

-   :material-chip: **[Hardware](hardware.md)**

    ---

    Pins, addresses, bus transactions, interrupt and power.

-   :material-file-document-check-outline: **[Contract](contract.md)**

    ---

    What the driver promises, what it needs, threading and errors.

-   :material-code-braces: **[API guide](api-guide.md)**

    ---

    Start, drive outputs, read inputs, handle interrupts and errors, task by task.

-   :material-tune-variant: **[Agile I/O](agile-io.md)**

    ---

    The PCAL9555A extras: drive strength, pull resistors, latches, interrupt mask.

-   :material-book-open-variant: **[API reference](api-reference.md)**

    ---

    Every class and member, generated from the headers.

</div>
