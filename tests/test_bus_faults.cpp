/**
 * @file test_bus_faults.cpp
 * @brief Every public operation reports a failed bus transfer as Error::Bus, whichever transfer
 * fails.
 *
 * Each operation is run once on a clean bus to count its transfers, then once per transfer with
 * exactly that transfer failing. This reaches every "the transfer failed, return the error" path in
 * the driver, which a single fault test per operation would not. Every sweep runs through both
 * mock port types, because each port type is a separate instantiation of the driver template.
 */
#include <cstdio>

#include "mock_pcal9555.hpp"
#include "pcal9555.hpp"
#include "test_harness.hpp"

using pcal9555::Error;
using pcal9555_test::BarePort;
using pcal9555_test::FullPort;

namespace {

pcal9555::Config Pcal() {
  pcal9555::Config c{};
  c.variant = pcal9555::Variant::Pcal9555a;
  return c;
}

/// Runs `op` with each of its transfers failing in turn; every run must return Error::Bus.
template <typename Port, typename Op>
void SweepBusFaults(const char* name, const pcal9555::Config& config, Op op) {
  int transfers = 0;
  {
    Port clean;
    pcal9555::PCAL9555<Port> io(clean, pcal9555::kI2cAddress, config);
    REQUIRE(io.EnsureInitialized().ok());
    const int before = clean.state.transfers;
    CHECK_EQ(op(io), Error::Ok);
    transfers = clean.state.transfers - before;
  }
  CHECK(transfers > 0);
  for (int i = 0; i < transfers; ++i) {
    Port port;
    pcal9555::PCAL9555<Port> io(port, pcal9555::kI2cAddress, config);
    REQUIRE(io.EnsureInitialized().ok());
    port.state.fail_transfer = port.state.transfers + i;
    const Error e = op(io);
    if (e != Error::Bus) {
      std::printf("    %s: transfer %d of %d failed, got %s\n", name, i + 1, transfers,
                  pcal9555::ToString(e));
    }
    CHECK_EQ(e, Error::Bus);
  }
}

/// EnsureInitialized with each of its transfers failing in turn; a retry then succeeds.
template <typename Port>
void SweepStart(const pcal9555::Config& config) {
  int transfers = 0;
  {
    Port clean;
    pcal9555::PCAL9555<Port> io(clean, pcal9555::kI2cAddress, config);
    REQUIRE(io.EnsureInitialized().ok());
    transfers = clean.state.transfers;
  }
  CHECK(transfers > 0);
  for (int i = 0; i < transfers; ++i) {
    Port port;
    port.state.fail_transfer = i;
    pcal9555::PCAL9555<Port> io(port, pcal9555::kI2cAddress, config);
    CHECK_EQ(io.EnsureInitialized().error, Error::Bus);
    CHECK(!io.IsInitialized());
    CHECK(io.EnsureInitialized().ok());
  }
}

/// The pin calls, without (the default) or with `Verify::ReadBack`.
template <typename Port>
void SweepPinCalls(pcal9555::Verify v) {
  using D = pcal9555::PCAL9555<Port>;
  using pcal9555::Bank;
  using pcal9555::Direction;
  using pcal9555::DriveStrength;
  using pcal9555::OutputMode;
  using pcal9555::Pull;
  const pcal9555::Config pca{};
  SweepBusFaults<Port>("SetDirection", pca,
                       [v](D& d) { return d.SetDirection(1, Direction::Output, v).error; });
  SweepBusFaults<Port>("SetOutputs", pca, [v](D& d) { return d.SetOutputs(1, false, v).error; });
  SweepBusFaults<Port>("WriteOutputs", pca, [v](D& d) { return d.WriteOutputs(0, v).error; });
  SweepBusFaults<Port>("ToggleOutputs", pca, [v](D& d) { return d.ToggleOutputs(1, v).error; });
  SweepBusFaults<Port>("SetPolarityInversion", pca,
                       [v](D& d) { return d.SetPolarityInversion(1, true, v).error; });
  SweepBusFaults<Port>("ReadInputs", pca, [](D& d) { return d.ReadInputs().error; });
  SweepBusFaults<Port>("ReadPin", pca, [](D& d) { return d.ReadPin(9).error; });
  SweepBusFaults<Port>("SetDriveStrength", Pcal(), [v](D& d) {
    return d.SetDriveStrength(pcal9555::kAllPins, DriveStrength::Half, v).error;
  });
  SweepBusFaults<Port>("SetPull Down", Pcal(),
                       [v](D& d) { return d.SetPull(1, Pull::Down, v).error; });
  SweepBusFaults<Port>("SetPull None", Pcal(),
                       [v](D& d) { return d.SetPull(1, Pull::None, v).error; });
  SweepBusFaults<Port>("SetInputLatch", Pcal(),
                       [v](D& d) { return d.SetInputLatch(1, true, v).error; });
  SweepBusFaults<Port>("SetInterruptEnabled", Pcal(),
                       [v](D& d) { return d.SetInterruptEnabled(1, true, v).error; });
  SweepBusFaults<Port>("ReadInterruptStatus", Pcal(),
                       [](D& d) { return d.ReadInterruptStatus().error; });
  SweepBusFaults<Port>("SetOutputMode", Pcal(), [v](D& d) {
    return d.SetOutputMode(Bank::Port0, OutputMode::OpenDrain, v).error;
  });
}

template <typename Port>
void SweepEverything() {
  using D = pcal9555::PCAL9555<Port>;
  const pcal9555::Config pca{};
  SweepStart<Port>(pca);
  SweepStart<Port>(Pcal());
  SweepBusFaults<Port>("ApplyConfig", Pcal(), [](D& d) { return d.ApplyConfig(Pcal()).error; });
  // Deinitialize first, so that the restart is a full start-up.
  SweepBusFaults<Port>("Deinitialize + EnsureInitialized", Pcal(), [](D& d) {
    const Error e = d.Deinitialize().error;
    return e != Error::Ok ? e : d.EnsureInitialized().error;
  });
  SweepBusFaults<Port>("ApplyConfig PCA9555", pca, [](D& d) { return d.ApplyConfig({}).error; });
  SweepBusFaults<Port>("Deinitialize", pca, [](D& d) { return d.Deinitialize().error; });
  SweepPinCalls<Port>(pcal9555::Verify::None);
  SweepPinCalls<Port>(pcal9555::Verify::ReadBack);
}

} // namespace

TEST_CASE("Every transfer of every operation can fail and is reported as Bus (BarePort)") {
  SweepEverything<BarePort>();
}

TEST_CASE("Every transfer of every operation can fail and is reported as Bus (FullPort)") {
  SweepEverything<FullPort>();
}
