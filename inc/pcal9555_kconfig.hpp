/**
 * @file pcal9555_kconfig.hpp
 * @brief Optional ESP-IDF layer: a `Config` built from the `CONFIG_PCAL9555_*` Kconfig options.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Include this header only where you want menuconfig to choose the defaults; the driver never
 * includes it and no driver signature depends on it. Under ESP-IDF the values come from
 * `sdkconfig.h`. Without Kconfig every option falls back to the power-up value, so
 * `KconfigDefaults()` equals `Config{}`.
 *
 * One configuration per program: the inline function below depends on the `CONFIG_` macros, so
 * every translation unit of one program must see the same values (ESP-IDF has one `sdkconfig.h`
 * per build; each host test is its own program). No driver header includes this one (handbook
 * KB-CPP20-018).
 */
#ifndef PCAL9555_KCONFIG_HPP
#define PCAL9555_KCONFIG_HPP

#ifdef __has_include
#if __has_include("sdkconfig.h")
#include "sdkconfig.h"
#endif
#endif

#include "pcal9555_types.hpp"

#ifndef CONFIG_PCAL9555_OUTPUT_HIGH_MASK
/** Output pins driven high when Kconfig does not set it: all, the power-up value. */
#define CONFIG_PCAL9555_OUTPUT_HIGH_MASK 0xFFFF
#endif
#ifndef CONFIG_PCAL9555_INPUT_MASK
/** Pins configured as inputs when Kconfig does not set it: all, the power-up value. */
#define CONFIG_PCAL9555_INPUT_MASK 0xFFFF
#endif

namespace pcal9555 {

/**
 * @brief The configuration menuconfig selected, on top of the power-up values.
 * @return `Config{}` with the variant, the output levels and the directions taken from Kconfig.
 */
constexpr Config KconfigDefaults() noexcept {
  Config c{};
#ifdef CONFIG_PCAL9555_VARIANT_PCAL9555A
  c.variant = Variant::Pcal9555a;
#else
  c.variant = Variant::Pca9555;
#endif
  c.output_high_mask = static_cast<uint16_t>(CONFIG_PCAL9555_OUTPUT_HIGH_MASK);
  c.input_mask = static_cast<uint16_t>(CONFIG_PCAL9555_INPUT_MASK);
  return c;
}

} // namespace pcal9555

#endif // PCAL9555_KCONFIG_HPP
