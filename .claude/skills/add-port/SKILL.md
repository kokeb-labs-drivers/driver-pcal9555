---
name: add-port
description: Write a platform port and example for the driver in examples/<platform>/ (ESP32/ESP-IDF, STM32 HAL, a host/Linux port). Use when the driver must run on a new MCU or OS.
---

# Add a port

The port is a class derived from the driver's CRTP base (`inc/<chip>_<bus>_interface.hpp`) that the `<Bus>Port`
concept accepts. The contract is the Porting page (`docs/porting.md`); `tests/docs/my_port.hpp` is the minimal
form.

The driver is MCU-agnostic: a port never goes into `inc/` or `src/`. Each platform is one self-contained folder,
`examples/<platform>/`, holding its port (`<platform>_<chip>_port.hpp`), an example program and a README. Add a
platform only together with a CI job that builds its example, and add its row to `docs/platforms.md`. Plain CMake
covers STM32, RP2040, nRF and the host; the root `CMakeLists.txt` also covers ESP-IDF. Add a Zephyr, Arduino or
PlatformIO manifest only when a product needs it, and never at the cost of a second copy of the sources.

## Every platform

1. Implement the required hooks with exactly the documented signatures (`noexcept`, `bool`): `EnsureInitialized()`
   (idempotent bus bring-up) and the bus hooks. Move raw bytes only: no retries, no frame parsing.
2. No `DelayUs()` and no `GpioSet()`: the PCA9555 and PCAL9555A need no wait and have no reset pin.
3. Optionally `Log(LogLevel, const char* fmt, ...)`, formatting with the platform's logger.
4. Bounded time-outs on every bus call; return `false` on any error, a NACK included.
5. PCA9555 / PCAL9555A bus rules: I2C up to 400 kHz, 7-bit address 0x20 to 0x27; `Write()` is one transaction
   (command byte first, at most two data bytes), `WriteRead()` uses a repeated START. INT is the application's,
   not the port's: wire it to an MCU input and let a task call the driver.

## ESP32 (ESP-IDF)

The project (`examples/esp32/CMakeLists.txt`) lists the repository root in `EXTRA_COMPONENT_DIRS`: the root
`CMakeLists.txt` registers the component under `ESP_PLATFORM`, with `_config/Kconfig`. Start from
`examples/esp32/main/esp32_<chip>_port.hpp`: `spi_master` / `i2c_master` / `uart` drivers,
`ESP_LOG*` for the log hook. Build it through the example
(`app_config.yml`); the ESP32 build runs in CI.

## STM32 (STM32Cube HAL)

`HAL_SPI_TransmitReceive()` with the CS pin driven around it (`HAL_GPIO_WritePin`), or `HAL_I2C_Master_Transmit`
/ `HAL_I2C_Mem_Read`, with a finite timeout.
Return `HAL_OK` as `true`.

## Host (Linux or tests)

`examples/host/host_<chip>_port.hpp` runs the driver against the protocol model of the tests; it is the smallest
complete port and the quickstart's first step.

Linux `i2c-dev` (`ioctl(I2C_RDWR)` with two messages for `WriteRead()`). For tests, derive from the mock in
`tests/mock_<chip>_bus.hpp` instead.

## Check it

1. Compile a copy of the port in `tests/docs/` if it should appear on the Porting page.
2. On hardware: `EnsureInitialized()` returns `Ok` (reads, writes and the read-back identity check work), then
   the Porting page's checks. Until then, mark the port **not verified on hardware**.
