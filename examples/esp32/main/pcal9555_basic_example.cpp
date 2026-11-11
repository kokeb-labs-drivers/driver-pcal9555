/**
 * @file pcal9555_basic_example.cpp
 * @brief Initialise the expander from the menuconfig values, blink P0_0 and log the inputs.
 *
 * Wiring: examples/esp32/README.md. Expected output: one "initialised" line, then an
 * "inputs 0x...." line twice a second while the LED on P0_0 blinks. Every Result is checked and
 * logged by name.
 */
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp32_pcal9555_port.hpp"
#include "pcal9555.hpp"
#include "pcal9555_kconfig.hpp"

namespace {
constexpr const char* kTag = "pcal9555_example";
constexpr uint16_t kLed = pcal9555::PinMask(0); // P0_0
/// The menuconfig values, with P0_0 an output: it drives the LED.
pcal9555::Config ExampleConfig() {
  pcal9555::Config config = pcal9555::KconfigDefaults();
  config.input_mask = static_cast<uint16_t>(static_cast<uint32_t>(config.input_mask) &
                                            ~static_cast<uint32_t>(kLed));
  return config;
}

} // namespace

extern "C" void app_main(void) {
  static Esp32Pcal9555Port port; // static: the driver keeps a reference
  static pcal9555::PCAL9555<Esp32Pcal9555Port> io(port, kEsp32Pcal9555Address, ExampleConfig());

  ESP_LOGI(kTag, "PCAL9555 driver %s", pcal9555::GetDriverVersion());
  const auto init = io.EnsureInitialized();
  if (!init) {
    ESP_LOGE(kTag, "EnsureInitialized failed: %s", pcal9555::ToString(init.error));
    return;
  }
  ESP_LOGI(kTag, "initialised at 0x%02X (%s)", io.GetAddress(),
           io.GetConfig().variant == pcal9555::Variant::Pcal9555a ? "PCAL9555A" : "PCA9555");

  while (true) {
    // A supply dip resets the expander without a word (section 6.4): look for it each period.
    if (io.CheckForReset().error == pcal9555::Error::DeviceReset) {
      ESP_LOGW(kTag, "the expander reset: starting it again");
      (void)io.EnsureInitialized();
    }
    const auto toggled = io.ToggleOutputs(kLed);
    const auto in = io.ReadInputs();
    if (toggled && in) {
      ESP_LOGI(kTag, "inputs 0x%04X", static_cast<unsigned>(in.value));
    } else {
      ESP_LOGW(kTag, "bus failed: %s", pcal9555::ToString(!toggled ? toggled.error : in.error));
      // The chip may have lost power: start again, which writes every register.
      (void)io.Deinitialize();
      (void)io.EnsureInitialized();
    }
    vTaskDelay(pdMS_TO_TICKS(500));
  }
}
