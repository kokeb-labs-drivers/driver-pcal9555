// clang-format off: the hook table below must keep one row per line.
/**
 * @file pcal9555_i2c_interface.hpp
 * @brief The I2C port of the PCAL9555 driver: the bus requirements, the CRTP base, the `I2cPort`
 * concept, the optional port hooks and the register framing.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * The driver never calls an SDK. It calls hooks on a class you derive from
 * `pcal9555::I2cInterface<Self>`:
 *
 * | Hook | Contract |
 * | --- | --- |
 * | `bool EnsureInitialized() noexcept` | Bring the bus up on the first call; `true` at once afterwards |
 * | `bool Write(uint8_t addr, const uint8_t* data, std::size_t len) noexcept` | START, `addr` + W, `len` bytes, STOP; `false` on a NACK or bus error |
 * | `bool WriteRead(uint8_t addr, const uint8_t* tx, std::size_t tx_len, uint8_t* rx, std::size_t rx_len) noexcept` | START, `addr` + W, `tx`, repeated START, `addr` + R, `rx_len` bytes (NACK on the last), STOP |
 * | `bool GetLastFailure(I2cFailure& failure) noexcept` (optional) | After `Write()` or `WriteRead()` returned `false`: why. `true` and `failure` set when the port knows; `false` when it cannot tell |
 *
 * `addr` is the 7-bit address. The port moves raw bytes: the command byte travels in the payload,
 * and the port never retries or configures the chip. What the chip needs from the bus is below as
 * `constexpr` values (`kI2cAddress`, `kMaxClockHz`, `kRepeatedStart`); a port checks its settings
 * against them with `static_assert`.
 *
 * Section and table numbers refer to the NXP PCAL9555A data sheet, Rev. 2 (19 December 2014).
 */
// clang-format on
#ifndef PCAL9555_I2C_INTERFACE_HPP
#define PCAL9555_I2C_INTERFACE_HPP

#include <array>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <span>

#include "pcal9555_detail.hpp"
#include "pcal9555_types.hpp"

namespace pcal9555 {

/**
 * @name Bus requirements
 * What the PCA9555 and PCAL9555A need from the I2C bus. A port and a board configuration check
 * their settings against these with `static_assert` (handbook KB-CPP-123).
 * @{
 */
/// Base 7-bit I2C address, 0100 A2 A1 A0 with every address pin low (section 6.1, figure 4); the
/// pins add 0 to 7 (`AddressFromPins()`).
inline constexpr uint8_t kI2cAddress = kMinI2cAddress;
/// Highest SCL frequency: fast mode, 400 kHz (section 13, table 33, f_SCL).
inline constexpr uint32_t kMaxClockHz = 400'000U;
/// `true`: a register read is a combined write-read with a repeated START (section 7.2): the
/// command byte is written, then the address is sent again with R/W = 1 after a restart.
inline constexpr bool kRepeatedStart = true;
/** @} */

/**
 * @brief Why a `Write()` or `WriteRead()` hook returned `false`, as the optional `GetLastFailure()`
 * hook reports it.
 *
 * Only `AddressNack` changes what the driver returns: nothing acknowledged the address byte, so
 * nothing is at the address, and the driver returns `Error::NoDevice`. Every other failure, and
 * every failure of a port without the hook, is `Error::Bus`.
 */
enum class I2cFailure : uint8_t {
  Other = 0,       ///< Arbitration lost, a stuck line, a timeout, or a NACK the port cannot place.
  AddressNack = 1, ///< No device acknowledged the address byte.
  DataNack = 2,    ///< The device acknowledged its address, then did not acknowledge a data byte.
};

/**
 * @brief CRTP base of the platform I2C port.
 *
 * Derive `class MyPort : public pcal9555::I2cInterface<MyPort>` and implement the hooks in this
 * file's description. The base declares no hook itself (a forwarding hook would turn a missing
 * override into infinite recursion) and no data; the driver checks your class with the `I2cPort`
 * concept, so a missing or misdeclared hook is a compile error that names it.
 *
 * @tparam Derived Your port class.
 */
template <typename Derived>
class I2cInterface {
public:
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface(const I2cInterface&) = delete;
  /** @brief Ports are not copied: the driver holds a reference. */
  I2cInterface& operator=(const I2cInterface&) = delete;

protected:
  /** @brief Only the derived port is constructed. */
  I2cInterface() noexcept = default;
  /** @brief Destroyed through the derived port only. */
  ~I2cInterface() noexcept = default;
};

/**
 * @brief Satisfied by a class that derives from `I2cInterface<Port>` and implements the required
 * hooks with exactly the types above, `noexcept` and returning `bool`.
 *
 * Each hook's address is matched with `detail::ExactHook`, which takes a member function of exactly
 * the signature and converts no value: a hook declared with a narrower length or without
 * `noexcept` does not satisfy the concept (handbook KB-CPP20-006).
 *
 * @tparam Port The port class.
 */
template <typename Port>
concept I2cPort = std::derived_from<Port, I2cInterface<Port>> && requires {
  detail::ExactHook<Port, bool() noexcept>::Take(&Port::EnsureInitialized);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t) noexcept>::Take(&Port::Write);
  detail::ExactHook<Port, bool(uint8_t, const uint8_t*, std::size_t, uint8_t*,
                               std::size_t) noexcept>::Take(&Port::WriteRead);
};

namespace detail {

// Optional hooks, all in this one place (KB-CPP-036): a `Declares*` concept only tests the name,
// the `Has*` concept the exact signature, `noexcept` and the result type. The driver rejects a hook
// that is declared but does not match (KB-CPP20-007).

/// The port declares a member called `GetLastFailure`, whatever its signature: a single member
/// is found by its address, an overloaded one by a call with the documented argument.
template <typename Port>
concept DeclaresGetLastFailure = requires {
  &Port::GetLastFailure;
} || requires(Port& port, I2cFailure& failure) { port.GetLastFailure(failure); };

/// The port explains a failed transfer: `bool GetLastFailure(I2cFailure& failure) noexcept`. The
/// parameter must be a non-const lvalue reference, so a hook that takes the value by copy, and
/// could not set it, is rejected.
template <typename Port>
concept HasGetLastFailure =
    requires { ExactHook<Port, bool(I2cFailure&) noexcept>::Take(&Port::GetLastFailure); };

/// The port declares a member called `Log`, whatever its signature.
template <typename Port>
concept DeclaresLog =
    requires { &Port::Log; } || requires(Port& port) { port.Log(LogLevel::Info, ""); };

/// The port takes log lines: `void Log(LogLevel, const char* message) noexcept`.
template <typename Port>
concept HasLog =
    requires { ExactHook<Port, void(LogLevel, const char*) noexcept>::Take(&Port::Log); };

/// Log through the port, or nothing when the port has no `Log()`. The driver never formats text.
template <typename Port>
void Log(Port& port, [[maybe_unused]] LogLevel level,
         [[maybe_unused]] const char* message) noexcept {
  if constexpr (HasLog<Port>) {
    port.Log(level, message);
  } else {
    (void)port;
  }
}

/// The error for a transfer the port reported as failed: `NoDevice` when the port says nothing
/// acknowledged the address, `Bus` otherwise (also when the port has no `GetLastFailure()`).
template <typename Port>
[[nodiscard]] Error TransferError(Port& port) noexcept {
  if constexpr (HasGetLastFailure<Port>) {
    I2cFailure failure = I2cFailure::Other;
    if (port.GetLastFailure(failure) && (failure == I2cFailure::AddressNack)) {
      return Error::NoDevice;
    }
  } else {
    (void)port;
  }
  return Error::Bus;
}

/// Longest register burst the driver issues, in data bytes: one register pair.
inline constexpr std::size_t kMaxBurst = 2;

/// A burst size the register helpers accept: 1 to `kMaxBurst` bytes, checked at compile time.
template <std::size_t N>
concept BurstSize = (N >= 1U) && (N <= kMaxBurst);

/// Register access over I2C: the command byte is the first payload byte; reads use a repeated
/// START (`kRepeatedStart`). Within one transfer the chip toggles between the two registers of a
/// pair (section 7). No transfer is ever empty: a zero-length burst is refused before any I/O
/// (handbook KB-CPP-130).
template <I2cPort Port>
class RegisterBus {
public:
  RegisterBus(Port& port, uint8_t address) noexcept : port_(port), address_(address) {}

  Port& GetPort() & noexcept {
    return port_;
  }

  [[nodiscard]] uint8_t GetAddress() const noexcept {
    return address_;
  }

  /// Read `out.size()` registers from `reg`; the size is checked at compile time.
  template <std::size_t N>
    requires BurstSize<N>
  Error Read(uint8_t reg, std::span<uint8_t, N> out) noexcept {
    return Read(reg, std::span<uint8_t>{out});
  }

  /// Read `out.size()` registers from `reg`. `InvalidArgument` (nothing sent) for 0 or more than
  /// `kMaxBurst` bytes, `NoDevice` or `Bus` when the transfer fails.
  Error Read(uint8_t reg, std::span<uint8_t> out) noexcept {
    if (out.empty() || (out.size() > kMaxBurst)) {
      return Error::InvalidArgument;
    }
    const std::array<uint8_t, 1> command{reg};
    return port_.WriteRead(address_, command.data(), command.size(), out.data(), out.size())
               ? Error::Ok
               : TransferError(port_);
  }

  /// Write `data.size()` registers from `reg`; the size is checked at compile time.
  template <std::size_t N>
    requires BurstSize<N>
  Error Write(uint8_t reg, std::span<const uint8_t, N> data) noexcept {
    return Write(reg, std::span<const uint8_t>{data});
  }

  /// Write `data.size()` registers from `reg`. `InvalidArgument` (nothing sent) for 0 or more than
  /// `kMaxBurst` bytes, `NoDevice` or `Bus` when the transfer fails.
  Error Write(uint8_t reg, std::span<const uint8_t> data) noexcept {
    if (data.empty() || (data.size() > kMaxBurst)) {
      return Error::InvalidArgument;
    }
    std::array<uint8_t, 1U + kMaxBurst> frame{};
    frame[0] = reg;
    const std::size_t count = data.size();
    for (std::size_t i = 0U; i < count; ++i) {
      frame[1U + i] = data[i];
    }
    return port_.Write(address_, frame.data(), count + 1U) ? Error::Ok : TransferError(port_);
  }

private:
  Port& port_;
  uint8_t address_;
};

} // namespace detail

} // namespace pcal9555

#endif // PCAL9555_I2C_INTERFACE_HPP
