# Configuration

Every setting the driver writes, where it comes from, and how menuconfig sets it.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    - Power-up value of every register: NXP PCAL9555A Rev. 2, Table 4 Command byte (power-up default), [p. 6](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=6)
    - Open-drain ports and the 0x4F write order: NXP PCAL9555A Rev. 2, 6.2.12 Output port configuration register (4Fh), [p. 12](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=12)
    - Registers, command bytes and power-up values: NXP PCA9555 Rev. 10, 6.2 Registers; 6.2.1 Command byte (Table 4), [p. 6](https://www.nxp.com/docs/en/data-sheet/PCA9555.pdf#page=6)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Config

One `pcal9555::Config` holds every register the driver writes. Pass it to the constructor; `EnsureInitialized()`
writes it and reads it back, and `ApplyConfig()` replaces it later. Its defaults are the power-up values of the
registers (PCAL9555A data sheet table 4), so `Config{}` changes nothing on a freshly powered chip.

```cpp
--8<-- "tests/docs/configuration.cpp:custom"
```

Bit n of each mask is pin n (P0_0 to P0_7 are pins 0 to 7, P1_0 to P1_7 pins 8 to 15).

| Field | Register | Power-up | Meaning |
| --- | --- | --- | --- |
| `variant` | (driver side) | `Variant::Pca9555` | Which part is fitted; `Pcal9555a` enables the fields below the line |
| `output_high_mask` | Output port 0x02, 0x03 | `0xFFFF` | 1 drives the pin high when it is an output |
| `polarity_invert_mask` | Polarity inversion 0x04, 0x05 | `0x0000` | 1 inverts how the input pin reads |
| `input_mask` | Configuration 0x06, 0x07 | `0xFFFF` | 1 = input, 0 = output |
| `drive_strength_codes` | Drive strength 0x40 to 0x43 | `0xFFFFFFFF` | Two bits per pin (`DriveStrength`), pin n at bits 2n+1..2n |
| `input_latch_mask` | Input latch 0x44, 0x45 | `0x0000` | 1 latches input changes until the port is read |
| `pull_enable_mask` | Pull enable 0x46, 0x47 | `0xFFFF` | 1 connects the pin's 100 kΩ resistor |
| `pull_up_mask` | Pull selection 0x48, 0x49 | `0xFFFF` | 1 = pull-up, 0 = pull-down |
| `interrupt_mask` | Interrupt mask 0x4A, 0x4B | `0xFFFF` | 1 = the pin raises no interrupt |
| `port0_mode`, `port1_mode` | Output port configuration 0x4F | `PushPull` | `OpenDrain` for a whole port |

The fields from `drive_strength_codes` down exist on the PCAL9555A only. With `variant = Variant::Pca9555` they must
keep their power-up values:

```cpp
--8<-- "tests/docs/configuration.cpp:pca9555"
```

## Write order

`EnsureInitialized()` and `ApplyConfig()` write the Agile I/O bank first (drive strength, input latch, pull
selection, pull enable, interrupt mask, then 0x4F), then the output port, the polarity inversion and last the
configuration registers. The output stage and the output levels are therefore in place before any pin becomes an
output, as the data sheet recommends for 0x4F (section 6.2.12), and no pin glitches to the power-up level.

## Validate

`pcal9555::Validate(config)` checks a configuration without touching the chip, and is `constexpr`, so a
configuration built at compile time can be checked with `static_assert`. It returns `InvalidArgument` when an enum
holds a value that is not an enumerator, and `Unsupported` when `variant` is `Pca9555` and an Agile I/O field differs
from its power-up value. `EnsureInitialized()` and `ApplyConfig()` run it before any I/O. Every mask value is legal.

## Change it at run time

```cpp
--8<-- "tests/docs/configuration.cpp:reconfigure"
```

`GetConfig()` is the driver's copy of the registers: the configuration passed in, updated by every successful pin
call. `ApplyConfig()` on a running driver writes every register and reads each back (`DeviceIdMismatch` if one does
not hold its value); before the start it only stores the configuration. The variant of a running driver cannot
change (`InvalidArgument`): `Deinitialize()` first. The pin calls ([API guide](api-guide.md)) change one register
pair each and are cheaper for a few pins.

## Kconfig

Under ESP-IDF, `idf.py menuconfig` shows **PCA9555 / PCAL9555A GPIO expander** when the driver is a component of
the project ([Installation](installation.md)); the menu is [`_config/Kconfig`](../_config/Kconfig). The options
reach the code only through [`inc/pcal9555_kconfig.hpp`](../inc/pcal9555_kconfig.hpp), never through a driver
signature:

```cpp
--8<-- "tests/docs/configuration.cpp:kconfig"
```

| Option | Default | `Config` field |
| --- | --- | --- |
| `CONFIG_PCAL9555_VARIANT_PCA9555`, `_PCAL9555A` (a choice) | PCA9555 | `variant` |
| `CONFIG_PCAL9555_OUTPUT_HIGH_MASK` (hex) | `0xFFFF` | `output_high_mask` |
| `CONFIG_PCAL9555_INPUT_MASK` (hex) | `0xFFFF` | `input_mask` |

The I2C address is a constructor argument, not a `Config` field, so it is not a Kconfig option. Without Kconfig
(plain CMake, other platforms) `KconfigDefaults()` returns `Config{}`. Both cases are host tests.

Next: [Porting](porting.md).
