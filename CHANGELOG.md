# Changelog

All notable changes to this repository are listed here. Versions follow [Semantic Versioning](https://semver.org/).
Entries under **Breaking** need source changes in code that used the affected API; any of them calls for a major
version bump (a minor bump before 1.0).

## [Unreleased]

### Changed

- The repository moved to kokeb-labs-drivers; URLs follow.
- Hardware page: the pinout, the block diagram and the application circuit are now figures drawn from
  `docs/hardware.yml`, which also generates the pin table (replacing the hand-written one) and the parts table.
  Every pin, block and part cites the datasheet section and page; values the datasheet does not give are marked as
  not confirmed. `kokeb-docs hardware --kicad-netlist` exports the circuit.
- Synced with the driver template: `tools/preflight.sh` runs clang-tidy over every file CI lints, as CI does, and
  the hardware-lessons review adds the switch-off, channel-in-doubt and fuzz-skip lessons.

## [0.1.0] - 2026-11-09

### Breaking

- `Error::DeviceReset` (18) is new. A `switch` over `Error` without `default:` must add it (`-Wswitch`).
  `SetDirection()` and `ApplyConfig()` now return it, and so does a setter about to rewrite the register the reset
  check reads, when the expander went through a power-on reset since the start; the driver is then uninitialised,
  so call `EnsureInitialized()` again.
Since v0.1.0-rc.1:

- `Config::verify_writes` is removed. Read-back is now chosen per call: every pin and Agile I/O setter
  (`SetDirection()`, `SetOutputs()`, `WriteOutputs()`, `ToggleOutputs()`, `SetPolarityInversion()`,
  `SetDriveStrength()`, `SetPull()`, `SetInputLatch()`, `SetInterruptEnabled()`, `SetOutputMode()`) takes a last
  argument `Verify verify = Verify::None`, a new enum in `pcal9555_types.hpp`. Code that set
  `verify_writes = false` drops that line; code that wants the read-back passes `Verify::ReadBack` to the calls
  that need it. A value that is not an enumerator returns `InvalidArgument` with no I/O.
- `Error` is `enum class [[nodiscard]] Error`: a call whose `Error` is dropped (for example `Validate(config);` as a
  statement) now warns, and fails a `-Werror` build. Use the value, or cast it to `void` on purpose.
- `Error::NoDevice` (17) is new. A `switch` over `Error` without `default:` must add it (`-Wswitch`). It is
  returned only with a port that has the new optional `GetLastFailure()` hook; a port without it sees `Bus` as
  before.
- `GetConfig()` on a temporary driver (an rvalue) is deleted, since the reference it returned would dangle; call it
  on a named driver.
- `reg::kDriveBitsPerPin` is a `uint32_t` (was `uint8_t`).

The port hooks keep their signatures: `EnsureInitialized()`, `Write()` and `WriteRead()` are unchanged, and the
new hook is optional, so an existing port compiles as it is. A port whose hooks were not `noexcept`, or took
another length type than `std::size_t`, no longer satisfies `I2cPort` (it broke the documented contract before).

### Added

- `CheckForReset()`: finds a power-on reset (section 6.4) by reading one register whose copy differs from its
  power-up value; one read, none when the copy is all power-up values, plus one per register whose last write
  failed when no other register is left to read.
- The bus requirements as `constexpr` values in `pcal9555_i2c_interface.hpp`, with their data sheet sections:
  `kI2cAddress` (0x20, section 6.1), `kMaxClockHz` (400 kHz, section 13 table 33) and `kRepeatedStart` (section
  7.2). The host and ESP32 example ports check their clock, their address and the repeated START against them with
  `static_assert`. `kDefaultI2cAddress` stays, with the same value.
- The optional port hook `bool GetLastFailure(I2cFailure& failure) noexcept` and `enum class I2cFailure`
  (`Other`, `AddressNack`, `DataNack`). When a transfer fails and the port reports `AddressNack`, every call returns
  `Error::NoDevice` instead of `Bus`, so an absent or unpowered chip is told apart from a bus fault. The host
  example port implements it; the ESP32 port does not, because `i2c_master` does not say which byte was NACKed.
- Raw access and diagnostics: `ReadRegister(command)` (one read, no retry, any command byte the configured part
  implements, `reg::IsImplemented()`), `GetLastInputs()`, `GetLastOutput()` and an interrupt latch,
  `GetLatchedInterrupts()` and `ClearLatchedInterrupts()`, which keeps every interrupt status the driver read
  after the input read that clears it. None of the accessors does I/O, and the hot path gains two compares per
  read and no transfer.
- `Config::operator==` (every field is an integer or an enumeration).
- Tests: a model independent of the driver's headers, with its own register map and data sheet sections, and
  `tests/test_model.cpp`, which compares the two maps at compile time; bus line modes (SDA floating or stuck low)
  and a deaf device against the start-up and every read; `NoDevice` with and without the hook; compile-fail tests
  for a narrowed hook parameter, a hook without `noexcept` and misdeclared optional hooks. The fuzzer also
  chooses address NACKs and whether the port can tell them, and checks that `NoDevice` appears only after one.
- Strict-dialect header checks (`pcal9555_header_checks`): every public header compiled alone as ISO C++20 with
  `-pedantic-errors` on GCC and Clang, in the Cortex-M cross-build, and in the ESP-IDF build through
  `examples/esp32/main/pcal9555_cxx20_check.cpp`.
- The review skills `cpp20-profile`, `misra-review` and `hardware-lessons-review`, and a read-only reviewer
  definition, from the driver template.

- First version of the driver for the NXP PCA9555 and PCAL9555A 16-bit I2C GPIO expanders, written on the
  Kokeb Labs driver template.
- `pcal9555::PCAL9555<Port>` with the configuration in the constructor and one start-up call,
  `EnsureInitialized()`: presence check by reading the input ports, every register written and read back as the
  identity check, and on a PCAL9555A the reserved bits of 0x4F checked before any write. `ApplyConfig()`,
  `Deinitialize()` (every pin an input).
- Pin calls by mask, each one register-pair transfer from the driver's copy of the registers: `SetDirection()`,
  `SetOutputs()`, `WriteOutputs()`, `ToggleOutputs()`, `SetPolarityInversion()`, `ReadInputs()`, `ReadPin()`.
- PCAL9555A Agile I/O: `SetDriveStrength()`, `SetPull()`, `SetInputLatch()`, `SetInterruptEnabled()`,
  `ReadInterruptStatus()`, `SetOutputMode()`; `Config::variant` selects the part and the Agile I/O calls return
  `Unsupported` on a PCA9555.
- `Config` with the power-up value of every register, a `constexpr` `Validate()`, `PinMask()`,
  `AddressFromPins()` and `DriveStrengthAll()`; Kconfig options for the variant, the output levels and the
  directions.
- Host tests over a protocol model of both parts, with every transfer of every operation failed in turn; the host
  and ESP32 examples. Not verified on hardware.
- clang-tidy runs the CERT C++ checks (`cert-*`) as well; the library code had no finding.
- The `Log()` hook is documented as `void Log(LogLevel, const char* message)`: the message is plain text, and the
  examples print it with `"%s"` instead of using it as a format string (no C variadic function, CERT DCL50-CPP;
  no format string from data). A port with the old variadic signature still compiles.
- After `WriteMismatch` every setter's copy holds what was asked for, `SetDriveStrength()` included; retry an
  absolute call as it was, and after `ToggleOutputs()` resend with `WriteOutputs(GetConfig().output_high_mask)` (a
  repeated toggle would undo it).
- `ReadOutputLatch()`, `ReadInputMask()` and `ReadPolarityInversion()` read what the chip holds, not the driver's
  copy; the Output Port registers read back the output latch, not the pin, so a lost write shows there.
- `Config::verify_writes` (default on): once started, every write is read back, and a register that did not keep its
  value returns `Error::WriteMismatch` (16), the driver's first chip-specific error. An I2C acknowledge carries no
  checksum, and the data sheet allows for data corruption (PCAL9555A section 8.3). A register that does not keep
  its value after the start is `WriteMismatch`; `DeviceIdMismatch` now means only "another part answers", at
  start-up.
- Three chip figures drawn from the source: the register map with each pair's owner and power-up value
  (Hardware), what one pin call does with `verify_writes` and how a handler retries (API guide), and the
  `EnsureInitialized()` steps with the error each one returns (Contract).
- A libFuzzer target over failed, lost and replaced transfers (`tests/fuzz/`, `-DPCAL9555_BUILD_FUZZERS=ON`); its
  corpus replays as a CTest on every compiler, and CI fuzzes every pull request.
- API guide: "Read back what the chip holds" and "Use it from a handler" (start-up, retries, recovery, locks,
  timeouts).
- `docs/datasheets.yml` (handbook ADR-0019): the PCAL9555A (Rev. 2) and PCA9555 (Rev. 10) data sheets with their
  SHA-256, and every topic the driver takes from them. `kokeb-docs gen` builds from it the README card, the
  Datasheet map page (`docs/datasheet.md`) and a box of the sections each page relies on. The README is restructured
  as the front door: at a glance, wiring, port, first code, what it does, and which page to read for what.
- Documentation site with the Doxygen API reference: <https://kokeb-labs-drivers.github.io/driver-pcal9555/>.
- Releases carry a source archive, an SPDX SBOM, one zip per example firmware build, `SHA256SUMS`, and signed
  build provenance for every file (check a download with `gh attestation verify`).

### Changed

- The `misra-review`, `cpp20-profile` and `new-driver-from-template` skills and `docs/repo-setup.md` follow the
  template: reading the MISRA report, the known tool misfires, enumeration ranges without a sentinel, and the
  settings tool.
- `tools/preflight.sh` and the `verify` and `hardware-lessons-review` skills follow the template: every check CI runs
  that a workstation can run, in one command, and the rules for device resets, faults the chip clears itself, dropped
  writes, switch-offs that no uncertainty refuses and a model that changes state on its own.
- Follows the driver template's portable ownership: copyright belongs to `The driver-pcal9555 authors` (`LICENSE`, every
  source header, `driver.yml` `authors`, the docs footer). `tools/check_standalone.py` runs in the *Handbook source
  checks* job and keeps the code and docs about this chip only. `AGENTS.md` follows the template's structure; the
  issue and PR templates ask for the board, MCU and port in one field.
- The port concepts match each hook's address against exactly one `noexcept` signature with
  `detail::ExactHook<Port, Sig>::Take(&Port::Hook)` instead of probing it with a converting `detail::Exactly<T>`:
  no conversion operator is left. A misdeclared hook is still a compile error. `kAlwaysFalse` takes a class and
  the enumeration helpers an enumeration (`ClassType`, `Enumeration`), each with a compile-fail test.
  `HasGetLastFailure` matches `bool GetLastFailure(I2cFailure&) noexcept` the same way.
- Both datasheets are listed with the kind `Datasheet`, so the generated cards and the Datasheet map name the
  datasheet on every line that carries its revision date.
- Include guards (`#ifndef PCAL9555_..._HPP`) replace `#pragma once` in every header, the `.ipp` and the test
  and example headers.
- Every target builds as ISO C++20 (`CMAKE_CXX_EXTENSIONS OFF`, `-pedantic-errors`, the C++20 deprecation
  warnings as errors), the tests and fuzzers with the standard library's bounds checks.
- The port concept checks the hooks exactly: each argument's type, `noexcept` and the `bool` result. The optional
  hooks are detected by named concepts in one place, and one declared with another signature is a compile error.
- Narrow operands are widened to `uint32_t` before every shift and bitwise operation, enumerations are checked
  with one helper before use, and the bus helpers take spans with the size fixed at compile time; a zero-length
  transfer is refused before any I/O.
- `Validate()` compares the whole `Config` with its power-up value for a PCA9555, so an Agile I/O field added later
  is covered without a change there.
- **Behaviour change:** the start-up identity check writes `reg::kIdentityPattern` (0x55AA) to the polarity
  inversion pair and reads it back before any configured value is written (two more transfers; at most 24 for a
  PCAL9555A start-up). A floating or stuck line, or a device that acknowledges everything and reads a fixed byte,
  now fails with `DeviceIdMismatch` whatever the `Config`; before, a `Config` whose registers matched that byte
  passed. The polarity pair changes only how inputs read, so no pin is driven before the identity is confirmed.
- A first drive strength pair that reads back differently no longer marks pins 8 to 15 as changed in
  `GetConfig()`: their pair was never sent.
- An optional hook declared with any other parameter list (`GetLastFailure()` with no argument or an rvalue
  reference, `Log()` without the level) is a compile error instead of being ignored.
- `Deinitialize()` clears `GetLastInputs()`, `GetLastOutput()` and the interrupt latch.
- The presence check's input read is documented as clearing an interrupt left pending from before the start.
- The documentation describes only this driver against its data sheets: the Status page explains each design
  choice (read-back on request, the identity pattern, outputs before directions, no waits) by data sheet section,
  and the API guide, Testing and Porting pages give bench steps as procedures.
- The source checks job runs the handbook's `checks/source_checks.py` at its C++20-profile commit; the managed
  files (skills, `_config/.clang-tidy`, `ci-repo-checks.yml`) follow driver template a4a1056.

- CI runs fewer jobs, from the driver template: the host tests build GCC and Clang in one job; DCO, the org
  standard, Markdown and YAML lint, the handbook rules, template drift and cppcheck are steps of one `Repository
  checks` job (`ci-repo-checks.yml`, replacing `ci-handbook.yml`, `ci-cpp-analysis.yml`, `ci-markdown-lint.yml` and
  `ci-yaml-lint.yml`); cross-compile and host quality use ci-tools' one-job layouts. No check is dropped. The
  workflows have no manual (`workflow_dispatch`) trigger any more.
- **Behaviour change:** a pin call no longer reads its write back by default (it did with the old
  `verify_writes`, which defaulted to on). A timing-critical call now makes only the transfer the data sheet needs,
  and a write the chip acknowledged but lost is no longer reported unless the call passes `Verify::ReadBack`, which
  costs one more I2C read per register written and returns `WriteMismatch` on a difference. `EnsureInitialized()`
  and `ApplyConfig()` still read every register back, unconditionally.
- The README and the Testing page link to the handbook, which is now public.
- MISRA C++:2023 conformance: parentheses make operator precedence explicit (Rule 8.0.1); the compile-time
  test vectors pass arguments of the parameter's exact type through the new `detail` literals `_u8`, `_u16` and
  `_u32`, which do not compile for a value out of range (Rule 7.0.6); every `if ... else if` chain ends with an
  `else` (Rule 9.4.1); `ToString(Error)` ends its `switch` with `default:` and the host tests build with
  `-Wswitch-enum`, so a new enumerator still warns (Rule 9.4.2); the register write loop has a constant bound
  (Rule 9.5.1); the private `writeAll()` and `verifyAll()` declarations use the definitions' parameter names
  (Rule 13.3.3); `Config::operator==` is a `[[nodiscard]]` hidden friend (Rule 16.6.1); and the `.ipp` include
  at the end of `pcal9555.hpp` carries the deviation marker `MD-CPP-009` (Rule 19.0.3). Behaviour is unchanged.

### Fixed

- After a power-on reset of the expander, `SetDirection()` on any pin turned the old outputs back on, high and
  push-pull (table 4), and `Verify::ReadBack` passed: it wrote only the configuration pair. It now checks for a
  reset first (`DeviceReset`, nothing written) and writes 0x4F, the output pair and the configuration pair from the
  copy, in the data sheet's order (section 6.2.12). Cost: one read plus one write pair on a PCA9555, plus a 1-byte
  write on a PCAL9555A.
- On a PCAL9555A with an unmasked input, `ReadInputs()` and `ReadPin()` cleared the interrupt status before the
  interrupt latch saw it (sections 6.2.11, 6.5). They now read the status pair into `GetLatchedInterrupts()` first:
  one more read per input read while an input is unmasked.
- A write whose transfer failed with `Bus` could still have reached the chip (a NACK or a lost ACK after the
  bytes), but the copy kept the old value and later setters built on it: a failed `SetDirection(..., Input)` left
  the pin an output while the copy said input, and a following `ToggleOutputs()` inverted a stale level. The copy
  now takes the riskier of the old and the asked value (configuration: output if either; output port: the written
  level; drive strength: the stronger code; 0x4F: push-pull if either; the other pairs: the written value), the
  register is marked unknown, and the next call that builds on it reads it first (one more read, only after a
  failure). A successful write of the pair, `ReadOutputLatch()`, `ReadInputMask()`, `ReadPolarityInversion()`,
  `ApplyConfig()` and `EnsureInitialized()` make it known again; the reset check never reads an unknown register.
  `NoDevice` (an address NACK) leaves the copy unchanged. An unknown register read back at its power-up value is
  `DeviceReset` (falsely, once, when the failed write's old or new value was the power-up value); with no other
  register to check, the reset check reads the unknown ones. A failed `ApplyConfig()` now leaves the new
  values of the registers written before the failure, the failed one unknown and the rest at their old values,
  instead of the previous configuration.
- `SetDirection(..., Direction::Input)` that leaves every pin an input writes only the configuration pair, with no
  reset check; releasing while other outputs remain writes the configuration pair before 0x4F and the output pair,
  so a failure after it leaves the pins released.
- The chip model can go through a power-on reset on its own in the middle of a session (`reset_after`), and the
  fuzzer chooses resets between calls.

