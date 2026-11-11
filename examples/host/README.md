# PCAL9555 host example

The driver on your PC, with no hardware: the host port in
[`host_pcal9555_port.hpp`](host_pcal9555_port.hpp) connects the real driver to a simulated PCAL9555A (the protocol
model the host tests use), and [`main.cpp`](main.cpp) starts it, walks a high bit across port 0, then lets a
simulated button on P1_3 raise the interrupt and reads it.

## Build and run

From the repository root, with CMake 3.16+ and a C++20 compiler:

```bash
cmake -S examples/host -B build-example
cmake --build build-example
./build-example/pcal9555_host_example
```

## Expected output

```text
PCAL9555 driver 0.1.0 on the host
pcal9555: initialised
interrupt from 0x0800, inputs 0xF780, INT released
```

`0x0800` is P1_3 in the interrupt status; the inputs show P1_3 low and P0_7, the last output of the walk, high.
The host tests build this folder too and run the example (CTest `pcal9555_host_example`), so CI fails if it stops
compiling or a call fails.

## Files

| File | What it is |
| --- | --- |
| [`main.cpp`](main.cpp) | The example: start, write outputs, read the interrupt status and the inputs |
| [`host_pcal9555_port.hpp`](host_pcal9555_port.hpp) | The host port: bus hooks over the simulated chip, and `Log()` |
| [`CMakeLists.txt`](CMakeLists.txt) | Standalone project; adds the driver with `add_subdirectory(../..)` |
