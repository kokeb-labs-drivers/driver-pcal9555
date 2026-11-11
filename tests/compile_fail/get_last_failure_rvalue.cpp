/**
 * @file get_last_failure_rvalue.cpp
 * @brief Must NOT compile: a GetLastFailure hook that takes an rvalue reference could never report
 * to the driver, so it is rejected, not silently ignored (handbook KB-CPP20-007).
 *
 * EXPECT: GetLastFailure hook must be
 */
#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"

class RvaluePort : public pcal9555_test::MockPortBase<RvaluePort> {
public:
  bool GetLastFailure(pcal9555::I2cFailure&& failure) noexcept {
    failure = state.last_failure;
    return true;
  }
};

int main() {
  RvaluePort port;
  pcal9555::PCAL9555<RvaluePort> io(port);
  return static_cast<int>(io.EnsureInitialized().error);
}
