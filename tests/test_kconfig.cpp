/**
 * @file test_kconfig.cpp
 * @brief KconfigDefaults(): the defaults without Kconfig, and menuconfig values when they are set.
 *
 * Built twice: without CONFIG_ macros (KCONFIG_SET undefined) and with the values an sdkconfig.h
 * would define (KCONFIG_SET=1, see tests/CMakeLists.txt).
 */
#include "pcal9555_kconfig.hpp"
#include "test_harness.hpp"

#ifdef KCONFIG_SET
TEST_CASE("Kconfig values reach the Config") {
  constexpr pcal9555::Config kC = pcal9555::KconfigDefaults();
  CHECK_EQ(kC.variant, pcal9555::Variant::Pcal9555a);
  CHECK_EQ(kC.output_high_mask, 0x00F0U);
  CHECK_EQ(kC.input_mask, 0xFF0FU);
  CHECK_EQ(kC.interrupt_mask, 0xFFFFU); // not a Kconfig option: power-up value
  CHECK_EQ(pcal9555::Validate(kC), pcal9555::Error::Ok);
}
#else
TEST_CASE("Without Kconfig, KconfigDefaults() equals Config{}") {
  constexpr pcal9555::Config kC = pcal9555::KconfigDefaults();
  constexpr pcal9555::Config kD{};
  CHECK_EQ(kC.variant, kD.variant);
  CHECK_EQ(kC.output_high_mask, kD.output_high_mask);
  CHECK_EQ(kC.input_mask, kD.input_mask);
}
#endif
