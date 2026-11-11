/**
 * @file get_last_failure_no_argument.cpp
 * @brief Must NOT compile: a GetLastFailure hook with another parameter list is rejected, not
 * silently ignored (handbook KB-CPP20-007).
 *
 * EXPECT: GetLastFailure hook must be
 */
#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"

class NoArgumentPort : public pcal9555_test::MockPortBase<NoArgumentPort> {
public:
  bool GetLastFailure() noexcept {
    return state.last_failure == pcal9555::I2cFailure::AddressNack;
  }
};

int main() {
  NoArgumentPort port;
  pcal9555::PCAL9555<NoArgumentPort> io(port);
  return static_cast<int>(io.EnsureInitialized().error);
}
