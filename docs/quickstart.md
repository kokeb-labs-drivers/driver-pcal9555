# Quickstart

Run the driver on your PC first, with no hardware; then blink an LED and read a button from your MCU: wire the
chip, write a small port class, call the driver.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCAL9555A Rev. 2:

    - Pins and package: 5 Pinning information, [p. 4](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=4)
    - Application circuit: 8 Application design-in information, [p. 20](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=20)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

## Try it on your PC

[`examples/host/`](../examples/host/README.md) runs the real driver against a simulated PCAL9555A, through a host
port. From the repository root, with CMake and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/pcal9555_host_example
```

```text
PCAL9555 driver 0.1.0 on the host
pcal9555: initialised
interrupt from 0x0800, inputs 0xF780, INT released
```

The calls in [`main.cpp`](../examples/host/main.cpp) are the ones below; only the port differs on an MCU. CI builds
and runs this example on every pull request.

## 1. Wire it

| MCU | PCA9555 / PCAL9555A | Notes |
| --- | --- | --- |
| I2C SCL | SCL | Pull-up to VDD; 4.7 kΩ suits 100 kHz and 400 kHz |
| I2C SDA | SDA | Pull-up to VDD |
| GND | A0, A1, A2 | Address `0x20` (`kI2cAddress`); never leave them open |
| 3.3 V | VDD | 100 nF to GND close to the pin |
| GND | VSS | |
| | P0_0 | LED and resistor to GND, for this example |
| | P1_0 | Push button to GND, and a pull-up (the PCAL9555A has one built in) |

INT is optional: leave it open for this example, or pull it up and wire it to an MCU input. The
[Hardware](hardware.md) page has the full pin list.

## 2. Add the driver

Add the repository as a submodule and link the `pcal9555::pcal9555` CMake target. [Installation](installation.md)
covers CMake on any MCU, the installed package and ESP-IDF.

```bash
git submodule add https://github.com/kokeb-labs-drivers/driver-pcal9555.git external/driver-pcal9555
```

## 3. Write the port

The port is the only platform code: it brings the bus up and moves bytes. Replace the `platform_*` calls with your
SDK's.

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

[Porting](porting.md) explains each method, the optional hook and the transfer rules.

## 4. Blink and read

```cpp title="main.cpp"
--8<-- "tests/docs/quickstart.cpp:quickstart"
```

What each step does:

1. `EnsureInitialized()` calls your port's `EnsureInitialized()`, reads the input ports to see that a chip ACKs
   0x20, writes `Config{}` (the power-up values: every pin an input, every output latch high) and reads each
   register back. A second call returns `Ok` at once with no I/O.
2. `SetOutputs()` sets the output latch of P0_0 high first, then `SetDirection()` makes it an output, so the pin
   never drives low in between. Each is one 3-byte write.
3. `ReadPin(8)` reads input port 1 and returns P1_0; the button pulls it low when pressed.

This example is built by the host CI, so it compiles against the current headers.

## If it does not work

| Symptom | First thing to check |
| --- | --- |
| `EnsureInitialized()` returns `Bus` | Nothing ACKs the address: A2..A0 levels, SDA and SCL pull-ups, VDD ([Troubleshooting](troubleshooting.md)) |
| `EnsureInitialized()` returns `DeviceIdMismatch` | Another part answers at that address, or `Config::variant` names a PCAL9555A on a PCA9555 |
| `EnsureInitialized()` returns `Unsupported` | An Agile I/O field is set in `Config` while `variant` is `Pca9555` |
| The LED does not light | The pin is still an input: `SetDirection(..., Direction::Output)` |

## Next

- [API guide](api-guide.md): outputs, inputs, interrupts, errors.
- [Agile I/O](agile-io.md): the PCAL9555A features.
- [Configuration](configuration.md): every `Config` field and Kconfig.
- [Examples](examples.md): the host example and the MCU examples.
