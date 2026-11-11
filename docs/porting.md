# Porting

Run the driver on a new MCU by writing one class: the port.

<!-- generated: datasheet-refs -->
??? abstract "Datasheet sections this page relies on"

    From NXP PCAL9555A Rev. 2:

    - Address pins and the 7-bit address: 6.1 Device address, [p. 5](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=5)
    - Register pairs, pointer toggling and burst transfers: 7 Bus transactions; 7.1 Writing to the port registers; 7.2 Reading the port registers, [p. 14](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=14)
    - I2C timing and INT timing: 13 Dynamic characteristics, [p. 32](https://www.nxp.com/docs/en/data-sheet/PCAL9555A.pdf#page=32)

    All of them, with the driver API for each: [Datasheet map](datasheet.md).
<!-- /generated -->

The driver never calls your SDK. It calls hooks on a class you derive from `pcal9555::I2cInterface<Self>` (in
[`inc/pcal9555_i2c_interface.hpp`](../inc/pcal9555_i2c_interface.hpp)). The base uses CRTP and declares no hook
itself; the driver template is constrained by the `pcal9555::I2cPort` concept, so calls are resolved at compile
time (no virtual functions, nothing to allocate) and a missing or misdeclared hook is a compile error at the point
of use.

## What you implement

| Method | Required | Contract |
| --- | --- | --- |
| `bool EnsureInitialized() noexcept` | yes | Bring the bus up on the first call; return `true` at once afterwards. `false` makes the driver's `EnsureInitialized()` return `Bus` |
| `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | yes | START, `addr` + W, `len` bytes, STOP. `false` on a NACK or any bus error |
| `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | yes | START, `addr` + W, `tx`, repeated START, `addr` + R, `rx_len` bytes (NACK the last), STOP |
| `void Log(LogLevel, const char* message) noexcept` | no | Print one line; `message` is plain text, never a format string |
| `bool GetLastFailure(I2cFailure& failure) noexcept` | no | After `Write()` or `WriteRead()` returned `false`: set `failure` and return `true` when the controller knows why, return `false` when it cannot tell |

`addr` is the 7-bit address the driver was constructed with; shift it yourself if your SDK wants the 8-bit form.
The chip needs no delay and has no reset pin, so there is no `DelayUs()` and no `GpioSet()` hook.

## Bus requirements

What the chip needs from the bus is in [`inc/pcal9555_i2c_interface.hpp`](../inc/pcal9555_i2c_interface.hpp) as
`constexpr` values (handbook KB-CPP-123). Check your port's settings against them with `static_assert`, so that a
port that clocks too fast does not compile:

| Constant | Value | Meaning | Data sheet |
| --- | --- | --- | --- |
| `kI2cAddress` | `0x20` | The base 7-bit address, A2, A1 and A0 low; the pins add 0 to 7 (`AddressFromPins()`, 0x20 to 0x27) | PCAL9555A 6.1, figure 4 |
| `kMaxClockHz` | `400'000` | The highest SCL frequency: standard or fast mode | PCAL9555A 13, table 33 (f_SCL) |
| `kRepeatedStart` | `true` | A register read is a write of the command byte, a repeated START, then the read | PCAL9555A 7.2 |

```cpp
inline constexpr uint32_t kMyClockHz = 400'000;
static_assert(kMyClockHz <= pcal9555::kMaxClockHz, "SCL above the PCAL9555's maximum");
static_assert(pcal9555::kRepeatedStart, "WriteRead() must use a repeated START");
```

Both example ports do this, and the ESP32 port also checks that its address is 0x20 to 0x27.

**NACKs.** The chip NACKs no transfer the driver sends by design: it has no reset command and no busy state, and
the driver sends only the command bytes the configured part implements (0x00 to 0x07, and on the PCAL9555A 0x40
to 0x4D and 0x4F). The NACK the master sends on the last byte it reads ends the read; it is not a failure. A NACK
the port reports is therefore always a fault: no chip at the address (`NoDevice` with `GetLastFailure()`, `Bus`
without it), or a part that refuses a command byte, such as a PCA9555 configured as a PCAL9555A, if it NACKs the Agile I/O
command byte (`Bus`; see [Hardware](hardware.md#unimplemented-command-bytes)). The
driver never probes with an empty or zero-length transfer: every transfer carries the command byte (handbook
KB-CPP-130).

**Pins.** The chip has no control input the port drives: no reset, no enable, no chip select. A2 to A0 are strapped
on the board. INT is an open-drain output of the chip, active low, idle high through the board's pull-up; read it
on an MCU input. The expander's own pins power up as inputs (configuration registers 0xFFFF), and
`EnsureInitialized()` writes the output levels before the directions, so a pin that becomes an output starts at its
configured level (handbook KB-HW-018).

## The port

```cpp title="my_port.hpp"
--8<-- "tests/docs/my_port.hpp:port"
```

Rules the transfers must follow:

- **One call, one transaction.** The command byte is the first byte of `Write()` and the `tx` of `WriteRead()`;
  never split a pair: the chip toggles between the two registers of a pair only within one transaction.
- **Repeated START for `WriteRead()`**, no STOP between the write and the read.
- **Raw bytes only.** Do not retry; the driver owns the framing.
- **Return `false` for a NACK or a bus error.** The driver turns it into `Error::Bus`, or `Error::NoDevice` when
  `GetLastFailure()` says nothing acknowledged the address.
- **Bounded time-outs.** A stuck bus fails the call; it does not block forever.
- **Up to 400 kHz** (`kMaxClockHz`). Both parts support standard and fast mode.

## Optional hooks

The driver finds the optional hooks at compile time with C++20 `requires`, in one place
(`pcal9555::detail`, in `inc/pcal9555_i2c_interface.hpp`). A hook that is declared with the wrong signature, for
example without `noexcept` or returning `void`, is a compile error that names it; it is never silently ignored.

| Hook | Used for | Without it |
| --- | --- | --- |
| `Log()` | One line per lifecycle event or failure | Nothing is logged |
| `GetLastFailure()` | Telling an address NACK from other failures, so that an absent or unpowered chip is `NoDevice` | Every failed transfer is `Bus` |

`GetLastFailure()` reports on the last failed transfer only, with `I2cFailure::AddressNack` (nothing acknowledged
the address), `DataNack` (the address was acknowledged, a data byte was not) or `Other` (a stuck line, lost
arbitration, a timeout). Only `AddressNack` changes what the driver returns. Return `false` when the controller
cannot tell: ESP-IDF's `i2c_master`, for one, reports a NACK without saying which byte it was, so the ESP32 example
port has no `GetLastFailure()` and an absent chip is `Bus` with it.

A port with both hooks:

```cpp
--8<-- "tests/docs/ports.cpp:full-port"
```

## Several chips on one bus

One port can serve up to eight expanders: each driver object has its own address and its own copy of the
registers.

```cpp
--8<-- "tests/docs/ports.cpp:two-chips"
```

The port must then serialise the bus if the drivers run in different tasks ([Contract](contract.md#threading)).

## Reference implementations

Every port lives with its example, in `examples/<platform>/`; the driver has none. [Platforms](platforms.md) lists
them and how to add one.

- [`examples/host/host_pcal9555_port.hpp`](../examples/host/host_pcal9555_port.hpp): any PC, against a simulated
  chip, with `Log()` and `GetLastFailure()`. Start from it for a host build or a test bench; it is also the
  smallest complete port.
- [`examples/esp32/main/esp32_pcal9555_port.hpp`](../examples/esp32/main/esp32_pcal9555_port.hpp): ESP-IDF, with
  the log hook. Start from it for any ESP-IDF target.

## Check your port

1. Build it: the ports on this page are compiled by the host CI from [`tests/docs/`](../tests/docs/my_port.hpp),
   with the driver instantiated over them.
2. Call `EnsureInitialized()`: `Ok` proves that writes and repeated-START reads work, since every register is read
   back.
3. Toggle an output with `ToggleOutputs()` and watch it, then read an input with `ReadPin()`.

## Threads

The driver is not thread-safe. Give each driver object one owner task, or guard every call with one mutex. When
several chips share a bus, the port must also serialise the bus. See [Contract](contract.md#threading).

## Editor setup

The clang-format and clang-tidy configuration is in `_config/`, and the repository root has no copy of it. Tools
that take a path use it directly; CI does the same:

| Tool | Setting |
| --- | --- |
| Command line | `clang-format --style=file:_config/.clang-format`, `clang-tidy --config-file=_config/.clang-tidy -p build` |
| VS Code (C/C++ extension) | `"C_Cpp.clang_format_style": "file:${workspaceFolder}/_config/.clang-format"` and `"C_Cpp.codeAnalysis.clangTidy.args": ["--config-file=${workspaceFolder}/_config/.clang-tidy"]` |
| clangd (VS Code clangd, Neovim, Emacs), CLion | These find `.clang-format` and `.clang-tidy` only by name, from the file upwards: copy both to the root once (`cp _config/.clang-format _config/.clang-tidy .`). `.gitignore` lists the copies, so they never reach a commit; copy again after `_config/` changes |

Configure with `-DCMAKE_EXPORT_COMPILE_COMMANDS=ON` (clangd: `--compile-commands-dir=build`) so the editor sees the
same flags as the build.
