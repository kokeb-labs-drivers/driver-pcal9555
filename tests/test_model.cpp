/**
 * @file test_model.cpp
 * @brief The chip model's register map against the driver's, at compile time (handbook KB-TST-030).
 *
 * tests/mock_pcal9555.hpp is written from the data sheets without the driver's headers, so a
 * constant the driver gets wrong is not copied into the model. These checks make the two agree, or
 * the build fails: a difference is a mistake in one of them, found against the data sheet section
 * the model names.
 */
#include <cstdint>

#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"
#include "test_harness.hpp"

namespace {

namespace chip = pcal9555_test::chip;
namespace reg = pcal9555::reg;

// --8<-- [start:cross-check]
// Command bytes (section 6.2.1, table 4).
static_assert(chip::kInputPort0 == reg::kInputPort0 && chip::kOutputPort0 == reg::kOutputPort0 &&
              chip::kPolarityPort0 == reg::kPolarityPort0 &&
              chip::kConfigPort0 == reg::kConfigPort0);
static_assert(chip::kDriveStrength0 == reg::kDriveStrength0 &&
              chip::kDriveStrength2 == reg::kDriveStrength2 &&
              chip::kInputLatch0 == reg::kInputLatch0 && chip::kPullEnable0 == reg::kPullEnable0 &&
              chip::kPullSelect0 == reg::kPullSelect0 &&
              chip::kInterruptMask0 == reg::kInterruptMask0 &&
              chip::kInterruptStatus0 == reg::kInterruptStatus0 &&
              chip::kOutputPortConfig == reg::kOutputPortConfig);
// The output port configuration register (section 6.2.12, table 27).
static_assert(chip::kOden0 == reg::kOutputConfigOden0 && chip::kOden1 == reg::kOutputConfigOden1 &&
              chip::kOutputConfigReserved == reg::kOutputConfigReservedMask);
// Power-up values (table 4).
static_assert(chip::kOutputReset == reg::kOutputReset &&
              chip::kPolarityReset == reg::kPolarityReset &&
              chip::kConfigReset == reg::kConfigReset &&
              chip::kInputLatchReset == reg::kInputLatchReset &&
              chip::kPullEnableReset == reg::kPullEnableReset &&
              chip::kPullSelectReset == reg::kPullSelectReset &&
              chip::kInterruptMaskReset == reg::kInterruptMaskReset &&
              chip::kOutputPortConfigReset == reg::kOutputPortConfigReset);
static_assert((static_cast<uint32_t>(chip::kDriveStrengthReset) << 16U |
               chip::kDriveStrengthReset) == reg::kDriveStrengthReset);
// The implemented command bytes: the model NACKs exactly the ones the driver never sends.
static_assert(chip::kLastStandard == reg::kConfigPort0 + 1U);
// The bus (section 6.1, section 13 table 33).
static_assert(chip::kBaseAddress == pcal9555::kI2cAddress &&
              chip::kMaxClockHz == pcal9555::kMaxClockHz);
// --8<-- [end:cross-check]

} // namespace

TEST_CASE("The model implements exactly the command bytes the driver may send") {
  for (const bool agile : {false, true}) {
    for (uint32_t command = 0; command <= 0xFFU; ++command) {
      pcal9555_test::Pcal9555Model model;
      model.variant = agile ? pcal9555_test::Pcal9555Model::Part::Pcal9555a
                            : pcal9555_test::Pcal9555Model::Part::Pca9555;
      uint8_t byte = 0;
      const auto c = static_cast<uint8_t>(command);
      CHECK_EQ(model.WriteRead(c, &byte, 1), reg::IsImplemented(c, agile));
    }
  }
}
