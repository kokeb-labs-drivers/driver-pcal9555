/**
 * @file get_last_failure_misdeclared.cpp
 * @brief Must NOT compile: an optional GetLastFailure hook with the wrong signature is rejected,
 * not ignored (handbook KB-CPP20-007): a `void` hook could not say that it cannot tell.
 *
 * EXPECT: GetLastFailure hook must be
 */
#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"

class VoidFailurePort : public pcal9555_test::MockPortBase<VoidFailurePort> {
public:
  void GetLastFailure(pcal9555::I2cFailure& failure) noexcept {
    failure = pcal9555::I2cFailure::Other;
  }
};

int main() {
  VoidFailurePort port;
  pcal9555::PCAL9555<VoidFailurePort> io(port);
  return static_cast<int>(io.EnsureInitialized().error);
}
