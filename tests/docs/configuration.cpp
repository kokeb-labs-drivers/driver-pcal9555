/**
 * @file configuration.cpp
 * @brief The Configuration page's examples, compiled by the host build (never run).
 */
#include "my_port.hpp"
#include "pcal9555.hpp"
#include "pcal9555_kconfig.hpp"

// --8<-- [start:custom]
constexpr pcal9555::Config BoardConfig() {
  pcal9555::Config c{};                     // power-up values: every pin an input
  c.variant = pcal9555::Variant::Pcal9555a; // the part fitted has Agile I/O
  c.output_high_mask = 0x0000;              // port 0 starts low...
  c.input_mask = 0xFF00;                    // ...and drives; port 1 stays inputs
  c.pull_enable_mask = 0xFF00;              // pull resistors on port 1 only
  c.interrupt_mask = 0x00FF;                // port 1 raises INT, port 0 does not
  c.port0_mode = pcal9555::OutputMode::OpenDrain;
  return c;
}
static_assert(pcal9555::Validate(BoardConfig()) == pcal9555::Error::Ok); // checked at build time

pcal9555::Error StartBoard(MyPort& port) {
  static pcal9555::PCAL9555<MyPort> io(port, 0x20, BoardConfig()); // stored, no I/O
  return io.EnsureInitialized().error;                             // written and read back
}
// --8<-- [end:custom]

// --8<-- [start:pca9555]
constexpr pcal9555::Config PlainPca9555() {
  pcal9555::Config c{}; // variant = Pca9555: the registers both parts share
  c.input_mask = 0xFFF0;
  c.pull_up_mask = 0; // an Agile I/O field: Validate() and EnsureInitialized() say Unsupported
  return c;
}
static_assert(pcal9555::Validate(PlainPca9555()) == pcal9555::Error::Unsupported);
// --8<-- [end:pca9555]

// --8<-- [start:reconfigure]
pcal9555::Error Reconfigure(pcal9555::PCAL9555<MyPort>& io) {
  pcal9555::Config c = io.GetConfig(); // the driver's copy, pin changes included
  c.interrupt_mask = 0x0000;           // every pin raises INT from now on
  return io.ApplyConfig(c).error;      // running: written and read back; else stored
}
// --8<-- [end:reconfigure]

// --8<-- [start:kconfig]
pcal9555::Error StartFromMenuconfig(MyPort& port) {
  static pcal9555::PCAL9555<MyPort> io(port, 0x20, pcal9555::KconfigDefaults());
  return io.EnsureInitialized().error;
}
// --8<-- [end:kconfig]
