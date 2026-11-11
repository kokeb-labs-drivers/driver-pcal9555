/**
 * @file log_not_noexcept.cpp
 * @brief Must NOT compile: an optional Log hook that is not noexcept is rejected, not ignored
 * (handbook KB-CPP20-007).
 *
 * EXPECT: Log hook must be
 */
#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"

class ThrowingLogPort : public pcal9555_test::MockPortBase<ThrowingLogPort> {
public:
  void Log(pcal9555::LogLevel level, const char* message) {
    (void)level;
    (void)message;
  }
};

int main() {
  ThrowingLogPort port;
  pcal9555::PCAL9555<ThrowingLogPort> io(port);
  return static_cast<int>(io.EnsureInitialized().error);
}
