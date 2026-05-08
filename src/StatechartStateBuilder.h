/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "StatechartTypes.h"

namespace Statechart {

/**
 * @brief Fluent builder for configuring a single state in the HSM. Returned by `HSM::addState(id)`.
 *
 * @tparam TState Enum class type for states.
 * @tparam TEvent Enum class type for events.
 */
template <typename TState, typename TEvent>
class StateBuilder {
  public:
  using DescType  = detail::StateDescriptor<TState, TEvent>;
  using TransType = detail::Transition<TState, TEvent>;

  explicit StateBuilder(DescType& desc, bool& overflow, TransType* pool, size_t& pool_count,
    size_t pool_max)
      : _desc(desc)
      , _overflow(overflow)
      , _pool(pool)
      , _pool_count(pool_count)
      , _pool_max(pool_max) {}

  /* ----------------------------------------- Hierarchy ---------------------------------------- */

  /**
   * @brief Sets the parent state. Children inherit unhandled events from their parent.
   * @param state Parent state id.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& parent(TState state) {
    _desc.parent = state;
    return *this;
  }

  /**
   * @brief Sets the default child entered when this composite state is entered without an active
   * history. Required for composite states to be UML-compliant.
   * @param child Child state id.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& initial(TState child) {
    _desc.initial_child = child;
    return *this;
  }

  /* --------------------------------------- Entry / Exit --------------------------------------- */

  /**
   * @brief Sets the callback invoked when this state is entered.
   * @param fn Action to execute on entry.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& onEnter(detail::Action fn) {
    _desc.on_enter = std::move(fn);
    return *this;
  }

  /**
   * @brief Sets the callback invoked when this state is exited.
   * @param fn Action to execute on exit.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& onExit(detail::Action fn) {
    _desc.on_exit = std::move(fn);
    return *this;
  }

  /* ----------------------------------- External transitions ----------------------------------- */
  // Standard UML transitions: exit current -> action -> enter target.

  /**
   * @brief Adds an unconditional external transition.
   * @param event Event that triggers this transition.
   * @param target State to transition to.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& on(TEvent event, TState target) { return _addExternal(event, target, {}, {}); }

  /**
   * @brief Adds an unconditional external transition with a transition action.
   * @param event Event that triggers this transition.
   * @param target State to transition to.
   * @param action Action executed between the exit and entry chains. The callable signature must be
   * `void()`.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& on(TEvent event, TState target, detail::Action action) {
    return _addExternal(event, target, {}, std::move(action));
  }

  /**
   * @brief Adds a guarded external transition. Skipped if the guard returns `false`.
   * @param event Event that triggers this transition.
   * @param target State to transition to.
   * @param guard Condition evaluated before taking the transition. The callable signature must be
   * `bool()`.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& on(TEvent event, TState target, detail::Guard guard) {
    return _addExternal(event, target, std::move(guard), {});
  }

  /**
   * @brief Adds a guarded external transition with a transition action.
   * @param event Event that triggers this transition.
   * @param target State to transition to.
   * @param guard Condition evaluated before taking the transition. The callable signature must be
   * `bool()`.
   * @param action Action executed between the exit and entry chains. The callable signature must be
   * `void()`.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& on(TEvent event, TState target, detail::Guard guard, detail::Action action) {
    return _addExternal(event, target, std::move(guard), std::move(action));
  }

  /* ----------------------------------- Internal transitions ----------------------------------- */
  // No exit/entry is triggered. Current state does NOT change.

  /**
   * @brief Adds an unconditional internal transition. No exit/entry is triggered.
   * @param event Event that triggers this transition.
   * @param action Action to execute. Current state does NOT change. The callable signature must be
   * `void()`.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& onInternal(TEvent event, detail::Action action) {
    return _addInternal(event, {}, std::move(action));
  }

  /**
   * @brief Adds a guarded internal transition. No exit/entry is triggered.
   * @param event Event that triggers this transition.
   * @param guard Condition evaluated before taking the transition. The callable signature must be
   * `bool()`.
   * @param action Action to execute. Current state does NOT change. The callable signature must be
   * `void()`.
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& onInternal(TEvent event, detail::Guard guard, detail::Action action) {
    return _addInternal(event, std::move(guard), std::move(action));
  }

  /* ------------------------------------------ History ----------------------------------------- */

  /**
   * @brief Enables shallow history `[H]` for this composite state.
   *
   * On re-entry, restores the direct child that was active when this state was last exited.
   * If no child has been saved yet, falls back to `initial()`.
   *
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& withHistory() {
    _desc.history           = detail::HistoryType::Shallow;
    _desc.last_active_child = detail::NO_STATE<TState>();
    return *this;
  }

  /**
   * @brief Enables deep history `[H*]` for this composite state.
   *
   * On re-entry, restores the full active leaf path recursively. Each level re-triggers its own
   * history resolution, reconstructing the exact substate that was active at every depth.
   *
   * @return `StateBuilder&` Reference to this builder for chaining.
   */
  StateBuilder& withDeepHistory() {
    _desc.history           = detail::HistoryType::Deep;
    _desc.last_active_child = detail::NO_STATE<TState>();
    return *this;
  }

  private:
  DescType& _desc;
  bool& _overflow;
  TransType* _pool;
  size_t& _pool_count;
  size_t _pool_max;

  StateBuilder& _addExternal(TEvent event, TState target, detail::Guard guard,
    detail::Action action) {
    if (_pool_count < _pool_max) {
      auto& t  = _pool[_pool_count++];
      t.owner  = _desc.id;
      t.event  = event;
      t.target = target;
      t.guard  = std::move(guard);
      t.action = std::move(action);
      t.kind   = detail::TransitionKind::External;
      t.valid  = true;
    } else {
      _overflow = true;
    }
    return *this;
  }

  StateBuilder& _addInternal(TEvent event, detail::Guard guard, detail::Action action) {
    if (_pool_count < _pool_max) {
      auto& t  = _pool[_pool_count++];
      t.owner  = _desc.id;
      t.event  = event;
      t.target = detail::NO_STATE<TState>();
      t.guard  = std::move(guard);
      t.action = std::move(action);
      t.kind   = detail::TransitionKind::Internal;
      t.valid  = true;
    } else {
      _overflow = true;
    }
    return *this;
  }
};

} // namespace Statechart
