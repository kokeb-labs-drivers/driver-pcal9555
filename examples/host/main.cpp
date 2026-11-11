/**
 * @file main.cpp
 * @brief The PCAL9555 driver on your PC: drive outputs and read inputs of a simulated PCAL9555A.
 *
 * No hardware and no SDK: the host port (host_pcal9555_port.hpp) connects the real driver to the
 * protocol model the host tests use. The same calls run on an MCU with that MCU's port
 * (examples/esp32/ for ESP-IDF). Exit code 0 when every call succeeded.
 */
#include <cstdint>
#include <cstdio>

#include "host_pcal9555_port.hpp"
#include "pcal9555.hpp"

int main() {
  pcal9555::Config config{};
  config.variant = pcal9555::Variant::Pcal9555a;
  config.output_high_mask = 0x0000; // port 0: outputs, start low
  config.input_mask = 0xFF00;       // port 1: inputs
  config.interrupt_mask = 0x00FF;   // port 1 raises INT

  HostPcal9555Port port;
  pcal9555::PCAL9555<HostPcal9555Port> io(port, 0x20, config); // no I/O yet

  std::printf("PCAL9555 driver %s on the host\n", pcal9555::GetDriverVersion());
  if (const auto r = io.EnsureInitialized(); !r) {
    std::printf("Initialize failed: %s\n", pcal9555::ToString(r.error));
    return 1;
  }

  // Walk a single high bit across port 0, as an LED bar would show it.
  for (uint8_t pin = 0; pin < 8; ++pin) {
    if (const auto r = io.WriteOutputs(pcal9555::PinMask(pin)); !r) {
      std::printf("write failed: %s\n", pcal9555::ToString(r.error));
      return 1;
    }
  }

  // Something outside pulls P1_3 low: INT asserts, the status names the pin, a read clears it.
  port.state.chip.SetExternal(0xF7FF);
  const auto source = io.ReadInterruptStatus();
  const auto levels = io.ReadInputs();
  if (!source || !levels) {
    std::printf("read failed\n");
    return 1;
  }
  std::printf("interrupt from 0x%04X, inputs 0x%04X, INT %s\n", static_cast<unsigned>(source.value),
              static_cast<unsigned>(levels.value),
              port.state.chip.InterruptAsserted() ? "still low" : "released");
  return source.value == 0x0800U && !port.state.chip.InterruptAsserted() ? 0 : 1;
}
