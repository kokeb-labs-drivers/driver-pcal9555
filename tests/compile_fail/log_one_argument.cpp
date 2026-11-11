/**
 * @file log_one_argument.cpp
 * @brief Must NOT compile: a Log hook without the level is rejected, not silently ignored
 * (handbook KB-CPP20-007).
 *
 * EXPECT: Log hook must be
 */
#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"

class OneArgumentLogPort : public pcal9555_test::MockPortBase<OneArgumentLogPort> {
public:
  void Log(const char* message) noexcept {
    (void)message;
  }
};

int main() {
  OneArgumentLogPort port;
  pcal9555::PCAL9555<OneArgumentLogPort> io(port);
  return static_cast<int>(io.EnsureInitialized().error);
}
