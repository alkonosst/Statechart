/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef STATECHART_NO_STD_FUNCTION
#  include <functional>
#  include <type_traits>
#endif

namespace Statechart {

/**
 * @brief Internal implementation details. Not part of the public API.
 */
namespace detail {

/**
 * @brief Sentinel value - represents an absent/invalid state.
 * Uses the maximum representable value of `TState` (e.g. `0xFF` for `uint8_t`).
 * @tparam TState Enum class type for states.
 */
template <typename TState>
constexpr TState NO_STATE() {
  return static_cast<TState>(~0);
}

/**
 * @brief Wrapper for state entry, exit, and transition actions.
 *
 * Only accepts callables that return `void`. Passing a callable with a non-void return type is a
 * compile-time error, preventing accidental misuse as a guard.
 *
 * Controlled by `STATECHART_NO_STD_FUNCTION`:
 * - Default: wraps `std::function<void()>` - supports lambdas with captures.
 * - Defined: raw `void(*)()` pointer - zero heap, no captures.
 */
#ifdef STATECHART_NO_STD_FUNCTION
using Action = void (*)();
#else
struct Action {
  std::function<void()> _fn;

  Action() = default;

  template <typename Fn, typename std::enable_if<
                           std::is_void<typename std::result_of<Fn()>::type>::value, int>::type = 0>
  Action(Fn&& fn)
      : _fn(std::forward<Fn>(fn)) {}

  explicit operator bool() const { return static_cast<bool>(_fn); }
  void operator()() const { _fn(); }
};
#endif

/**
 * @brief Wrapper for transition conditions.
 *
 * Only accepts callables that return a non-void type (typically `bool`). A default-constructed
 * (empty) Guard always allows the transition.
 *
 * Controlled by `STATECHART_NO_STD_FUNCTION`:
 * - Default: wraps `std::function<bool()>` - supports lambdas with captures.
 * - Defined: raw `bool(*)()` pointer - zero heap, no captures.
 */
#ifdef STATECHART_NO_STD_FUNCTION
using Guard = bool (*)();
#else
struct Guard {
  std::function<bool()> _fn;

  Guard() = default;

  template <typename Fn,
    typename std::enable_if<!std::is_void<typename std::result_of<Fn()>::type>::value, int>::type =
      0>
  Guard(Fn&& fn)
      : _fn(std::forward<Fn>(fn)) {}

  explicit operator bool() const { return static_cast<bool>(_fn); }
  bool operator()() const { return _fn(); }
};
#endif

/**
 * @brief Controls how a composite state restores its active child when re-entered after leaving.
 */
enum class HistoryType : uint8_t {
  None,    // No history; uses `initial()` child on re-entry.
  Shallow, // `[H]`  Restores the direct child that was active when leaving.
  Deep,    // `[H*]` Restores the full active leaf path recursively.
};

/**
 * @brief Controls the semantics of a transition.
 */
enum class TransitionKind : uint8_t {
  // Standard UML: exit current state chain, run action, enter target state chain.
  External,

  // Run action only; no exit/entry triggered, current state does NOT change.
  Internal,
};

/**
 * @brief A single event handler attached to a state.
 *
 * Multiple transitions for the same event are evaluated in definition order. The first one whose
 * guard returns `true` is taken (fallthrough-with-guard pattern).
 *
 * @tparam TState Enum class type for states.
 * @tparam TEvent Enum class type for events.
 */
template <typename TState, typename TEvent>
struct Transition {
  TState owner;
  TEvent event;
  TState target;
  Guard guard;
  Action action;
  TransitionKind kind = TransitionKind::External;
  bool valid          = false;
};

/**
 * @brief Data record for one state. Stored in a fixed-size array inside `HSM` (no heap).
 *
 * Transitions are NOT stored here — they live in a shared pool inside `HSM`,
 * identified by their `owner` field.
 *
 * @tparam TState Enum class type for states.
 * @tparam TEvent Enum class type for events.
 */
template <typename TState, typename TEvent>
struct StateDescriptor {
  TState id;
  TState parent;            ///< `NO_STATE()` if this is a root state.
  TState initial_child;     ///< First child entered when no history is available.
  TState last_active_child; ///< Saved by exit chain for history restoration.

  Action on_enter;
  Action on_exit;

  HistoryType history = HistoryType::None;

  bool isRoot() const { return parent == NO_STATE<TState>(); }
  bool hasInitial() const { return initial_child != NO_STATE<TState>(); }
  bool hasHistory() const { return history != HistoryType::None; }
  bool isShallow() const { return history == HistoryType::Shallow; }
  bool isDeep() const { return history == HistoryType::Deep; }
};

} // namespace detail
} // namespace Statechart
