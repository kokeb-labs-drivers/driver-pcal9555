/**
 * @file main.cpp
 * @brief Builds the driver from the installed package only: headers, version header and .ipp.
 */
#include <cstddef>
#include <cstdint>

#include "pcal9555.hpp"

namespace {

// A port that ACKs everything and answers every read with 0: enough to instantiate the whole
// driver template.
class NullPort : public pcal9555::I2cInterface<NullPort> {
public:
  bool EnsureInitialized() noexcept {
    return true;
  }
  bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept {
    (void)addr;
    (void)data;
    (void)len;
    return true;
  }
  bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx,
                 std::size_t rx_len) noexcept {
    (void)addr;
    (void)tx;
    (void)tx_len;
    for (std::size_t i = 0; i < rx_len; ++i) {
      rx[i] = 0;
    }
    return true;
  }
};

} // namespace

int main() {
  NullPort port;
  pcal9555::PCAL9555<NullPort> io(port);
  // The registers read back 0 instead of the power-up values written, so EnsureInitialized() must
  // fail with DeviceIdMismatch: proof the template compiled and ran.
  const auto r = io.EnsureInitialized();
  return (r.error == pcal9555::Error::DeviceIdMismatch && pcal9555::GetDriverVersion()[0] != '\0')
             ? 0
             : 1;
}
