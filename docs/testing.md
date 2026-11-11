# Testing and CI

How the driver is checked: host tests against a model of the chip, a fuzzer that fails, loses and replaces bus
transfers, compiled documentation, an install test, ESP32 and Cortex-M builds, static analysis and measured quality
gates, on every pull request.

## Host tests

The tests build the real driver against a protocol model of the PCA9555 and PCAL9555A: the command byte and the
pointer that toggles within a register pair, power-up values, 16 pins with outputs, open-drain stages and an
external level a test sets, the interrupt with its mask, status and input latch, a power-on reset in the middle of a
session that reloads every power-up value on its own (section 6.4, `reset_after`), a write that arrives but is
reported failed (`stored_then_fail`), and injected faults (a transfer
that fails, no chip at the address, a part that NACKs or aliases the Agile I/O command bytes, a register that
drops writes, writes acknowledged and lost, reserved bits that read 1, a deaf device that acknowledges everything)
and bus line modes (SDA floating high, so no address is acknowledged, or stuck low). They build as ISO C++20 with
`-pedantic-errors` and the firmware's flags, `-fno-exceptions -fno-rtti`, and warnings as errors, on GCC and Clang.

The model stands alone (handbook KB-TST-030): [`tests/mock_pcal9555.hpp`](../tests/mock_pcal9555.hpp) includes
no driver header and carries its own register map and power-up values, each with its data sheet section, so a
constant the driver gets wrong cannot hide behind the same wrong constant in the model.
[`tests/test_model.cpp`](../tests/test_model.cpp) checks the two maps against each other with `static_assert`:

```cpp
--8<-- "tests/test_model.cpp:cross-check"
```

Two mock ports drive it: `BarePort` has only the required hooks, so every failed transfer is `Bus`; `FullPort`
adds `Log()` and `GetLastFailure()`, so an address NACK is `NoDevice`.

Every public header is also compiled alone, as ISO C++20 with `-pedantic-errors`, by the target
`pcal9555_header_checks`; each generated unit asserts `__cplusplus == 202002L` (handbook KB-CPP20-001). The
Cortex-M cross-build compiles the same units, and the ESP-IDF build compiles
`examples/esp32/main/pcal9555_cxx20_check.cpp` at `-std=c++20 -pedantic-errors`, because ESP-IDF builds the rest of
the application in a newer GNU dialect.

```bash
cmake -S . -B build -DPCAL9555_BUILD_TESTS=ON
cmake --build build
ctest --test-dir build --output-on-failure
```

| File | Covers |
| --- | --- |
| [`tests/test_pcal9555.cpp`](../tests/test_pcal9555.cpp) | Lifecycle, presence and identity checks, configuration, pins, Agile I/O, the variant, the log hook |
| [`tests/test_diagnostics.cpp`](../tests/test_diagnostics.cpp) | `NoDevice` with and without `GetLastFailure()`, the floating, stuck and deaf bus on start-up and every read, `ReadRegister()`, `GetLastInputs()`, `GetLastOutput()`, the interrupt latch, the optional-hook concepts |
| [`tests/test_model.cpp`](../tests/test_model.cpp) | The model's register map against the driver's, at compile time; the model implements exactly the command bytes `reg::IsImplemented()` allows |
| [`tests/test_bus_faults.cpp`](../tests/test_bus_faults.cpp) | Every transfer of every operation, the start-up included, failed in turn and reported as `Bus`, through both mock port types |
| [`tests/fuzz/`](../tests/fuzz/fuzz_pcal9555.cpp) | The fuzz target; CTest replays its corpus (`pcal9555_fuzz_pcal9555_corpus`), see below |
| [`tests/test_errors.cpp`](../tests/test_errors.cpp) | The base `Error` values are 0 to 9; every chip-specific value (16 and up) is returned by some call |
| [`tests/test_version.cpp`](../tests/test_version.cpp) | The version getters follow the generated header (also against a 9.8.7 probe) |
| [`tests/test_kconfig.cpp`](../tests/test_kconfig.cpp) | `KconfigDefaults()` with and without `CONFIG_` values |
| [`tests/compile_fail/`](../tests/compile_fail/not_a_port.cpp) | Code that must not compile, for its `EXPECT:` diagnostic: a class that is not a port, a hook with a narrower length or without `noexcept`, a misdeclared optional `Log()` or `GetLastFailure()` |
| [`tests/mock_pcal9555.hpp`](../tests/mock_pcal9555.hpp) | The protocol model, independent of the driver's headers; the bus decoding and the line modes are in `mock_pcal9555_bus.hpp` |
| [`tests/check_version_copy.cmake`](../tests/check_version_copy.cmake) | `_config/Doxyfile` carries the CMake version |
| [`examples/host/`](../examples/host/README.md) | The host example, built with the test flags and run (`pcal9555_host_example`) |
| [`tests/check_install.cmake`](../tests/check_install.cmake) | Installs into a scratch prefix and builds [`tests/install_consumer/`](../tests/install_consumer/CMakeLists.txt) with `find_package(pcal9555)` |
| [`tests/check_doxygen.cmake`](../tests/check_doxygen.cmake) | Doxygen with `_config/Doxyfile` and warnings as errors, when `doxygen` is installed |

## Fuzz tests

What the driver reads comes from the bus, so read data is the untrusted input.
[`tests/fuzz/fuzz_pcal9555.cpp`](../tests/fuzz/fuzz_pcal9555.cpp) lets libFuzzer choose the part that is fitted
(which may not be the one configured), the address, a configuration, then up to 64 driver calls, and on every
transaction what the bus does: pass it to the model, fail it (as an address NACK the port can or cannot place, or
another bus error), replace the bytes read, or acknowledge a write and lose it. After every call it checks the
driver's promises and aborts, naming the line, when one breaks:

- a failed call returns an `Error` the enum defines and `T{}`, and `NoDevice` only after the port reported an
  address NACK;
- `ReadRegister()` makes one transfer for a command byte the configured part implements and none otherwise, and
  every interrupt status read is in `GetLatchedInterrupts()`;
- before `EnsureInitialized()` no call makes a transaction, and `InvalidArgument` or `Unsupported` means nothing was
  sent;
- **a pin call that returns `Ok` on an honest bus left the chip holding what `GetConfig()` says**, and a read-back
  call returns the chip's register, not the driver's copy;
- the fuzzer can also put the chip through a power-on reset between two calls: a `SetDirection()` that returns `Ok`
  on an honest bus with an output left kept the output pair and 0x4F as the copy says, `DeviceReset` leaves the
  driver uninitialised, and `CheckForReset()` sends at most one read plus one per register whose last write failed;
  and it can store a write while reporting it failed, after which a later honest `Ok` still leaves the chip as
  `GetConfig()` says.

```bash
# libFuzzer needs Clang (on macOS, Homebrew's LLVM)
cmake -S . -B build-fuzz -DCMAKE_CXX_COMPILER=clang++ -DPCAL9555_BUILD_TESTS=ON -DPCAL9555_BUILD_FUZZERS=ON
cmake --build build-fuzz --target fuzz_pcal9555
./build-fuzz/tests/fuzz/fuzz_pcal9555 -max_total_time=300 tests/fuzz/corpus/fuzz_pcal9555
```

The same file builds without libFuzzer as `fuzz_replay_fuzz_pcal9555`, and CTest replays every input in
[`tests/fuzz/corpus/fuzz_pcal9555/`](../tests/fuzz/corpus/fuzz_pcal9555) on GCC and Clang. CI fuzzes for 120 s on
every pull request from the seeds plus a corpus it keeps between runs (job `Host quality / fuzz`). 200 seconds of
local fuzzing with ASan and UBSan (15.2 million runs, 546 coverage edges) broke no promise.

## Quality gates

The `Host quality` job measures the library code, `inc/` and `src/` only, and fails when a value is over the limits
of this repository's assurance level, `high` (set in [`.kokeb/handbook.yml`](../.kokeb/handbook.yml), defined by the
handbook's ADR-0020):

| Gate | Tool and how it is measured | Limit at `high` |
| --- | --- | --- |
| Complexity | lizard 1.24.1, modified cyclomatic complexity (`-m`: a `switch` counts once) per function | at most 15 |
| Function length | lizard, lines of code per function | at most 60 |
| Parameters | lizard, per function | at most 6 |
| Line coverage | GCC 14 `--coverage -O0`, gcovr 8.7 `--merge-lines` (a template's lines count once across its instances), `--exclude-unreachable-branches --exclude-throw-branches` | at least 85 % |
| Branch coverage | same run | at least 75 % |
| MC/DC | GCC `-fcondition-coverage`, same run | measured, not gated at `high` (100 % at `safety`) |
| Sanitizers | Clang, `-fsanitize=address,undefined -fno-sanitize-recover=all`, every CTest | no report |
| Fuzzing | libFuzzer with ASan and UBSan, 120 s per target | no broken promise |

Each run's values are in its step summary and its `host-quality-reports` artifact. To run the coverage and
complexity gates locally:

```bash
cmake -S . -B build-cov -DCMAKE_CXX_COMPILER=g++-14 -DPCAL9555_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="--coverage -O0 -fcondition-coverage"
cmake --build build-cov && ctest --test-dir build-cov
gcovr --root . --filter inc/ --filter src/ --merge-lines --exclude-unreachable-branches --txt build-cov
lizard -m -C 15 -L 60 -a 6 inc src
```

clang-tidy 20 ([`_config/.clang-tidy`](../_config/.clang-tidy): bugprone, clang-analyzer, cppcoreguidelines,
modernize, performance, portability and readability, each disabled check with its reason) and cppcheck (strict)
run in the `C++ lint` and `Repository checks` jobs and fail on any finding.

## Documentation snippets

Every C++ block on this site is included from [`tests/docs/`](../tests/docs/my_port.hpp), which the host build
compiles (target `pcal9555_doc_snippets`) with the driver instantiated over the documented port. A change to
the API that breaks a page breaks the build first. The README's example is a generated copy of the same file;
`kokeb-docs check` fails when it drifts.

## CI workflows

Everything runs from [`ci.yml`](../.github/workflows/ci.yml), which calls one workflow file per check and ends in
the `CI gate` job, the single required check (`CI gate / gate`).

| Job | What it checks |
| --- | --- |
| Host tests | GCC, then Clang (one job), build and run every test above, then build and run `examples/host` on its own |
| Host quality | The gates above: complexity, coverage, sanitizers, fuzzing |
| Cross-compile | `arm-none-eabi-g++` for Cortex-M0+, M3, M4F, M7F and M33F, warnings as errors |
| C++ lint | clang-format and clang-tidy over the sources |
| Docs | Strict site build (`_config/mkdocs.yml`), figure and generated-region drift, Doxygen (`_config/Doxyfile`), offline link check, Lighthouse |
| Repository checks | One job: DCO on pull requests, the org standard, Markdown and YAML lint, the [Kokeb Labs handbook](https://github.com/kokeb-labs/handbook) rules for `.kokeb/handbook.yml` (by rule ID), template drift (warns only) and cppcheck over the driver sources; a second job runs the handbook's source checks (include guards, bus requirements, raw access, the C++20 profile) at a pinned commit |
| CI gate | Passes only if every job above passed: the one required check |

The **ESP32 examples** job builds every app in `examples/esp32/app_config.yml`, for each listed ESP-IDF version and
build type, with the repository root as the driver's component, and lints the example sources. The gate needs it
too.

The workflows call the shared, versioned workflows of
[kokeb-labs/ci-tools](https://github.com/kokeb-labs/ci-tools) at `@v1`.

## On hardware

An MCU example ([Examples](examples.md)) is the bench test: it blinks an LED on P0_0 and logs the inputs twice a
second. Press a button on an input and watch its bit change. The other bench steps are in
[Troubleshooting](troubleshooting.md#bench-checks); the driver is **not verified on hardware** until they pass.
Record the board and the result in the pull request's bench-status section.
