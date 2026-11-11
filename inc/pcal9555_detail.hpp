/**
 * @file pcal9555_detail.hpp
 * @brief Compile-time helpers of the PCAL9555 driver: exact hook matching and enumeration checks.
 * @copyright Copyright (c) 2026 The driver-pcal9555 authors. SPDX-License-Identifier: MIT
 *
 * @details
 * Everything here is `constexpr` or used only in unevaluated operands, so it adds no code to the
 * run-time path (handbook C++20 profile, KB-CPP20-006, -008, -025, -026).
 */
#ifndef PCAL9555_DETAIL_HPP
#define PCAL9555_DETAIL_HPP

#include <concepts>
#include <cstdint>
#include <type_traits>

namespace pcal9555::detail {

/**
 * @brief Matches a port hook against exactly one signature, `R(Args...) noexcept`, as a member
 * function of `Port` (`const` or not) (KB-CPP20-006).
 *
 * `ExactHook<Port, Sig>::Take(&Port::Hook)` is well-formed only when `&Port::Hook` names a member
 * function of exactly that type: the address of an overloaded or template hook resolves to the
 * one that matches, and a hook with a narrower or different parameter, another result type or no
 * `noexcept` has no match. No value is converted. Declared only: it appears in
 * requires-expressions, never in evaluated code.
 *
 * @tparam Port The port class.
 * @tparam Sig The hook's function type, `noexcept`; a `Sig` without `noexcept` has no definition.
 */
template <typename Port, typename Sig>
  requires std::is_class_v<Port> && std::is_function_v<Sig>
struct ExactHook;

/// The `noexcept` signatures: the only ones a hook may have.
template <typename Port, typename R, typename... Args>
  requires std::is_class_v<Port>
struct ExactHook<Port, R(Args...) noexcept> {
  /// Takes the address of a non-`const` hook of exactly this signature.
  static void Take(R (Port::*hook)(Args...) noexcept) noexcept;
  /// Takes the address of a `const` hook of exactly this signature.
  static void Take(R (Port::*hook)(Args...) const noexcept) noexcept;
};

/// A class type: what `kAlwaysFalse` depends on, the port or another class of the branch.
template <typename T>
concept ClassType = std::is_class_v<T>;

/// `false` for every class `T`: the condition of a `static_assert` in a branch that must not be
/// instantiated (KB-CPP20-008).
template <ClassType>
inline constexpr bool kAlwaysFalse = false;

/// A scoped or unscoped enumeration: what the enumeration helpers take (KB-CPP20-005, -026).
template <typename E>
concept Enumeration = std::is_enum_v<E>;

/**
 * @brief The value of an enumerator in its underlying type, our stand-in for C++23
 * `std::to_underlying` (KB-CPP20-002, -026).
 */
template <Enumeration E>
[[nodiscard]] constexpr std::underlying_type_t<E> ToUnderlying(E e) noexcept {
  return static_cast<std::underlying_type_t<E>>(e);
}

/**
 * @brief Whether `e` holds an enumerator of a contiguous enumeration that starts at 0 and ends at
 * `last`. A value made by `static_cast` from an integer can be anything its underlying type holds
 * (KB-CPP20-025).
 */
template <Enumeration E>
[[nodiscard]] constexpr bool IsEnumerator(E e, E last) noexcept {
  return static_cast<uint32_t>(ToUnderlying(e)) <= static_cast<uint32_t>(ToUnderlying(last));
}

/// Not `constexpr`: a literal operator below calls it only for a value its type cannot hold, so
/// that literal is not a constant expression and does not compile.
inline void LiteralOutOfRange() noexcept {}

/**
 * @brief A `uint8_t` constant, `0x4F_u8`: an argument of exactly the parameter's type (MISRA
 * C++:2023 Rule 7.0.6). A value above 0xFF does not compile.
 */
consteval uint8_t operator""_u8(unsigned long long value) noexcept {
  if (value > 0xFFULL) {
    LiteralOutOfRange();
  }
  return static_cast<uint8_t>(value);
}

/// @brief A `uint16_t` constant, `0x1234_u16`. A value above 0xFFFF does not compile.
consteval uint16_t operator""_u16(unsigned long long value) noexcept {
  if (value > 0xFFFFULL) {
    LiteralOutOfRange();
  }
  return static_cast<uint16_t>(value);
}

/**
 * @brief A `uint32_t` constant, `0xFFFF'FFFF_u32`: also on targets where `uint32_t` is
 * `unsigned long` and a `U` literal is `unsigned int`. A value above 0xFFFF'FFFF does not compile.
 */
consteval uint32_t operator""_u32(unsigned long long value) noexcept {
  if (value > 0xFFFF'FFFFULL) {
    LiteralOutOfRange();
  }
  return static_cast<uint32_t>(value);
}

} // namespace pcal9555::detail

#endif // PCAL9555_DETAIL_HPP
