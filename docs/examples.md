# Examples

One folder per platform under `examples/`, each with its own port, built by CI on every pull request. The driver
itself has no platform code; [Platforms](platforms.md) lists what exists.

## Host

[`examples/host/`](../examples/host/README.md) runs the driver on any PC against a simulated PCAL9555, through the
host port [`host_pcal9555_port.hpp`](../examples/host/host_pcal9555_port.hpp). No hardware:

```bash
cmake -S examples/host -B build-example && cmake --build build-example && ./build-example/pcal9555_host_example
```

The host tests build it with warnings as errors and run it (CTest `pcal9555_host_example`).

## ESP32

ESP-IDF applications that run the driver on an ESP32-C6 DevKitC-1; the wiring is in the
[example folder's README](../examples/esp32/README.md) and the port in
[`esp32_pcal9555_port.hpp`](../examples/esp32/main/esp32_pcal9555_port.hpp). The project points
`EXTRA_COMPONENT_DIRS` at the repository root, which is the driver's ESP-IDF component.

### The apps

<!-- generated: examples -->
| App | What it shows | Source | CI |
| --- | --- | --- | --- |
| `pcal9555_basic_example` :material-star:{ title="featured" } | Initialise the PCA9555/PCAL9555A from menuconfig values, blink P0_0 and log the inputs twice a second | [`pcal9555_basic_example.cpp`](../examples/esp32/main/pcal9555_basic_example.cpp) | yes |

Build and flash one (from `examples/esp32`, ESP-IDF environment loaded):

```bash
./scripts/build_app.sh pcal9555_basic_example Release
./scripts/flash_app.sh flash_monitor pcal9555_basic_example Release
```

Source of this table: [`examples/esp32/app_config.yml`](../examples/esp32/app_config.yml).
<!-- /generated -->

### Set up once

```bash
git submodule update --init examples/esp32/scripts   # the ESP-IDF build scripts
cd examples/esp32
. $IDF_PATH/export.sh                                 # ESP-IDF v5.5
./scripts/build_app.sh list                           # every app and build type
```

The scripts read [`app_config.yml`](../examples/esp32/app_config.yml) (the same file this page's table comes
from) and build each app as its own firmware image.

### What to read in an example

| You want to | Read |
| --- | --- |
| See the lifecycle and every `Result` checked | `pcal9555_basic_example.cpp` |
| Write an ESP-IDF port | `esp32_pcal9555_port.hpp` |
| Use menuconfig values | `pcal9555_kconfig.hpp` and [`_config/Kconfig`](../_config/Kconfig) |
