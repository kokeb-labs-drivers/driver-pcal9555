# PCAL9555 ESP32 examples

ESP-IDF applications that run the PCAL9555 driver on an ESP32-C6, built by CI on every pull request.

## Wiring (ESP32-C6 DevKitC-1)

| ESP32-C6 | PCA9555 / PCAL9555A | Notes |
| --- | --- | --- |
| GPIO6 | SDA | I2C0, 400 kHz; internal pull-ups enabled, add 4.7 kΩ to 3V3 |
| GPIO7 | SCL | |
| GND | A0, A1, A2 | Address `0x20` |
| 3V3 | VDD | 100 nF to GND at the pin |
| GND | VSS | |
| | P0_0 | LED and 1 kΩ to GND |
| | P1_0 to P1_7 | Optional buttons to GND |

INT is not used by this example.

## Build and flash

```bash
git submodule update --init scripts      # the ESP-IDF build scripts
. $IDF_PATH/export.sh                     # ESP-IDF v5.5
./scripts/build_app.sh list
./scripts/build_app.sh pcal9555_basic_example Debug
./scripts/flash_app.sh flash_monitor pcal9555_basic_example Debug
```

`idf.py menuconfig` shows "PCA9555 / PCAL9555A GPIO expander" under Component config: the part fitted, the pins
driven high and the pins configured as inputs that `pcal9555::KconfigDefaults()` returns. The example makes P0_0 an
output on top of that.

## Expected output

```text
I (312) pcal9555_example: PCAL9555 driver 0.1.0
pcal9555: initialised
I (318) pcal9555_example: initialised at 0x20 (PCA9555)
I (820) pcal9555_example: inputs 0xFFFF
I (1320) pcal9555_example: inputs 0xFFFE
```

Bit 0 follows the LED; a pressed button clears its bit. An `EnsureInitialized failed: Bus` line means nothing ACKs 0x20:
check the address pins, the pull-ups and the supply first. **Not verified on hardware** yet: the output above is
what the code prints, not a capture.

## Files

| File | What it is |
| --- | --- |
| [`main/pcal9555_basic_example.cpp`](main/pcal9555_basic_example.cpp) | The example app |
| [`main/esp32_pcal9555_port.hpp`](main/esp32_pcal9555_port.hpp) | The ESP-IDF port: bus and log |
| [`CMakeLists.txt`](CMakeLists.txt) | The ESP-IDF project; `EXTRA_COMPONENT_DIRS` points at the repository root, which is the component |
| [`app_config.yml`](app_config.yml) | The app matrix for the scripts, CI and the docs |
