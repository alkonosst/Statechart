/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "StatechartLogs.h"
#include "StatechartStateBuilder.h"
#include "StatechartTypes.h"

namespace Statechart {

/**
 * @brief Hierarchical State Machine with static storage (no heap).
 *
 * @tparam TState Enum class type for states (uint8_t recommended in embedded).
 * @tparam TEvent Enum class type for events (uint8_t recommended in embedded).
 * @tparam MaxStates Total capacity for registered states.
 * @tparam MaxTransitions  Total transition pool capacity (sum of all transitions across all
 * states).
 *
 * Typical usage:
 * ```cpp
 * HSM<MyState, MyEvent, 8, 4> fsm("MyMachine", MyState::IDLE);
 * fsm.addState(MyState::IDLE).onEnter(...).on(MyEvent::START, MyState::RUNNING);
 * fsm.start();
 * fsm.dispatch(MyEvent::START);
 * ```
 */
template <typename TState, typename TEvent, size_t MaxStates, size_t MaxTransitions>
class HSM {
  public:
  using DescType    = detail::StateDescriptor<TState, TEvent>;
  using BuilderType = StateBuilder<TState, TEvent>;

  /**
   * @brief Constructs the HSM with a name and an initial state.
   * @param name          Human-readable name used in log messages.
   * @param initial_state State entered when `start()` is called.
   */
  explicit HSM(const char* name, TState initial_state)
      : _name(name)
      , _initial(initial_state)
      , _current(initial_state)
      , _state_count(0)
      , _transition_count(0)
      , _started(false)
      , _overflow(false) {
    for (size_t i = 0; i < MaxStates; i++) {
      _states[i].id                = detail::NO_STATE<TState>();
      _states[i].parent            = detail::NO_STATE<TState>();
      _states[i].initial_child     = detail::NO_STATE<TState>();
      _states[i].last_active_child = detail::NO_STATE<TState>();
      _states[i].history           = detail::HistoryType::None;
    }
  }

  /**
   * @brief Returns a fluent builder for configuring the given state.
   *
   * Creates the state slot if it does not exist yet. Calling `addState()` more than once on the
   * same id is safe and can be used to split configuration across multiple functions.
   *
   * @param id State to register or retrieve.
   * @return `BuilderType` Fluent builder for this state.
   */
  BuilderType addState(TState id) {
    DescType* desc = _findOrCreate(id);
    if (!desc) {
      STATECHART_LOGE("[%s] MaxStates (%zu) exceeded, cannot register state %d",
        _name,
        MaxStates,
        static_cast<int>(id));
      _overflow = true;
      return BuilderType(_states[MaxStates - 1],
        _overflow,
        _transitions,
        _transition_count,
        MaxTransitions); // safe fallback, no crash
    }
    // Reset only the configuration fields so addState() can be called multiple times on the same id
    // (e.g. split across functions) without losing data.
    desc->id = id;
    return BuilderType(*desc, _overflow, _transitions, _transition_count, MaxTransitions);
  }

  /**
   * @brief Runs the entry chain for the initial state and marks the machine as started.
   * Calling `start()` more than once is a no-op (logs a warning).
   */
  void start() {
    if (_started) {
      STATECHART_LOGW("[%s] start() called more than once", _name);
      return;
    }
    if (!isValid()) {
      STATECHART_LOGE("[%s] start() called on an invalid HSM (construction overflow)", _name);
      return;
    }
    _started = true;
    STATECHART_LOGI("[%s] start -> initial state: %d", _name, static_cast<int>(_initial));
    _enterState(_initial);
  }

  /**
   * @brief Dispatches an event to the current state.
   *
   * If the current state does not handle the event, it propagates up the parent
   * chain (inherited event handling). Among multiple transitions registered for
   * the same event, the first one whose guard passes is taken.
   *
   * @param event Event to process.
   * @return `true` if a transition was taken, `false` if the event was not handled.
   */
  bool dispatch(TEvent event) {
    if (!_started) {
      STATECHART_LOGE("[%s] dispatch() called before start()", _name);
      return false;
    }

    STATECHART_LOGD("[%s] dispatch event:%d - current:%d",
      _name,
      static_cast<int>(event),
      static_cast<int>(_current));

    // Walk up the hierarchy until a matching transition with a passing guard is found.
    // This implements event inheritance.
    TState search = _current;
    while (search != detail::NO_STATE<TState>()) {
      DescType* desc = _find(search);
      if (!desc) break;

      for (size_t i = 0; i < _transition_count; i++) {
        auto& t = _transitions[i];
        if (t.owner != search || t.event != event) continue;

        // Guard check - if it returns false, continue to the next transition for the same event
        // (fallthrough with guard pattern).
        if (t.guard && !t.guard()) {
          STATECHART_LOGV("[%s] guard rejected transition event:%d -> %d",
            _name,
            static_cast<int>(event),
            static_cast<int>(t.target));
          continue;
        }

        t.kind == detail::TransitionKind::Internal ? _doInternal(t) : _doExternal(t);
        return true;
      }

      // No match in this state - propagate to parent.
      search = desc->parent;
    }

    STATECHART_LOGD("[%s] event:%d not handled in state:%d",
      _name,
      static_cast<int>(event),
      static_cast<int>(_current));
    return false;
  }

  /**
   * @brief Resets the HSM to its initial state.
   *
   * Runs the exit chain from the current state up to the root, clears all saved history, then runs
   * the entry chain into the initial state. Does nothing if `start()` has not been called yet.
   */
  void reset() {
    if (!_started) return;
    STATECHART_LOGI("[%s] reset", _name);
    _exitToAncestor(_current, detail::NO_STATE<TState>());
    // Clear all saved history across all states.
    for (size_t i = 0; i < _state_count; i++) {
      _states[i].last_active_child = detail::NO_STATE<TState>();
    }
    _current = _initial;
    _enterState(_initial);
  }

  /** @brief Returns the currently active leaf state. */
  TState getCurrentState() const { return _current; }

  /** @brief Returns `true` if `start()` has been called. */
  bool hasStarted() const { return _started; }

  /** @brief Returns the name string passed to the constructor. */
  const char* getName() const { return _name; }

  /**
   * @brief Returns `true` if no construction overflow occurred.
   *
   * Returns `false` if `MaxStates` or `MaxTransitions` was exceeded during configuration.
   * Check this after all `addState()` calls and before `start()`.
   */
  bool isValid() const { return !_overflow; }

  /**
   * @brief Returns `true` if the machine is currently in the given state or any of its substates.
   *
   * Walks up the ancestor chain from the current state, so it returns `true` for both the active
   * leaf state and any composite state that contains it.
   *
   * @param state State to test.
   * @return `true` if `state` is the current state or an ancestor of it.
   */
  bool isInState(TState state) const {
    for (TState c = _current; c != detail::NO_STATE<TState>();) {
      if (c == state) return true;
      const DescType* desc = _find(c);
      c                    = desc ? desc->parent : detail::NO_STATE<TState>();
    }
    return false;
  }

  /**
   * @brief Logs a human-readable dump of all registered states and their transitions.
   * Output is written via `STATECHART_LOGI`. Requires log level >= Info.
   */
  void debugDump() const {
#if STATECHART_LOG_LEVEL >= 3
    STATECHART_LOGI("=== HSM [%s] ===", _name);
    STATECHART_LOGI("  current=%d - states=%zu", static_cast<int>(_current), _state_count);
    for (size_t i = 0; i < _state_count; i++) {
      const DescType& d = _states[i];
      size_t t_count    = 0;
      for (size_t j = 0; j < _transition_count; j++) {
        if (_transitions[j].owner == d.id) t_count++;
      }
      STATECHART_LOGI("  [%d] parent=%d initial=%d history=%s transitions=%zu",
        static_cast<int>(d.id),
        d.isRoot() ? -1 : static_cast<int>(d.parent),
        d.hasInitial() ? static_cast<int>(d.initial_child) : -1,
        d.isDeep() ? "deep[H*]" : (d.isShallow() ? "shallow[H]" : "none"),
        t_count);
      for (size_t j = 0; j < _transition_count; j++) {
        const auto& t = _transitions[j];
        if (t.owner != d.id) continue;
        if (t.kind == detail::TransitionKind::Internal) {
          STATECHART_LOGI("    event:%d Internal guard:%s action:%s",
            static_cast<int>(t.event),
            (t.guard ? "y" : "n"),
            (t.action ? "y" : "n"));
        } else {
          STATECHART_LOGI("    event:%d -> %d guard:%s action:%s",
            static_cast<int>(t.event),
            static_cast<int>(t.target),
            (t.guard ? "y" : "n"),
            (t.action ? "y" : "n"));
        }
      }
    }
    STATECHART_LOGI("===================");
#else
    return;
#endif
  }

  private:
  const char* _name;
  TState _initial;
  TState _current;
  size_t _state_count;
  size_t _transition_count;
  bool _started;
  bool _overflow;
  DescType _states[MaxStates];
  detail::Transition<TState, TEvent> _transitions[MaxTransitions];

  // Descriptor lookup

  DescType* _find(TState id) {
    for (size_t i = 0; i < _state_count; i++)
      if (_states[i].id == id) return &_states[i];
    return nullptr;
  }

  const DescType* _find(TState id) const {
    for (size_t i = 0; i < _state_count; i++)
      if (_states[i].id == id) return &_states[i];
    return nullptr;
  }

  DescType* _findOrCreate(TState id) {
    DescType* existing = _find(id);
    if (existing) return existing;
    if (_state_count >= MaxStates) return nullptr;
    DescType& d         = _states[_state_count++];
    d.id                = id;
    d.parent            = detail::NO_STATE<TState>();
    d.initial_child     = detail::NO_STATE<TState>();
    d.last_active_child = detail::NO_STATE<TState>();
    d.history           = detail::HistoryType::None;
    return &d;
  }

  /**
   * @brief LCA (Lowest Common Ancestor): Finds the deepest state that is an ancestor of both 'a'
   * and 'b'.
   *
   * The LCA is the pivot for a transition: exits run from source up to (but not including) the LCA,
   * and entries run from the LCA down to the target.
   *
   * Algorithm:
   *
   * 1. Collect all ancestors of 'a' (including 'a' itself) into a flat array.
   *
   * 2. Walk up from 'b'; first ancestor also in the 'a' set is the LCA.
   *
   * Returns NO_STATE() if 'a' and 'b' share no common ancestor (separate trees - should not happen
   * in a well-formed HSM).
   *
   * @param a The first state.
   * @param b The second state.
   * @return TState The lowest common ancestor of 'a' and 'b'.
   */
  TState _lca(TState a, TState b) const {
    TState ancestors_of_a[MaxStates];
    size_t count = 0;

    for (TState c = a; c != detail::NO_STATE<TState>() && count < MaxStates;) {
      ancestors_of_a[count++] = c;
      const DescType* d       = _find(c);
      c                       = d ? d->parent : detail::NO_STATE<TState>();
    }

    for (TState c = b; c != detail::NO_STATE<TState>();) {
      for (size_t i = 0; i < count; i++) {
        if (ancestors_of_a[i] == c) return c;
      }
      const DescType* d = _find(c);
      c                 = d ? d->parent : detail::NO_STATE<TState>();
    }

    return detail::NO_STATE<TState>();
  }

  /**
   * @brief Exit chain - Exits states from 'from' up to (but not including) 'ancestor'.
   *
   * Also saves history along the way:
   *
   * - For a Shallow-history parent: saves the direct child being exited (overwrites on every exit
   *   so it always reflects the latest active child).
   *
   * - For a Deep-history parent: propagates down through every level, saving each child in its
   *   respective parent so that on re-entry the full leaf path can be reconstructed level by level.
   *
   * @param from The state to start exiting from.
   * @param ancestor The state to stop exiting at (exclusive).
   */
  void _exitToAncestor(TState from, TState ancestor) {
    for (TState c = from; c != detail::NO_STATE<TState>() && c != ancestor;) {
      DescType* desc = _find(c);
      if (!desc) break;

      if (desc->on_exit) {
        STATECHART_LOGV("[%s] onExit %d", _name, static_cast<int>(c));
        desc->on_exit();
      }

      // Save history in the parent. Both shallow and deep save the direct child at each level; the
      // difference is that deep also recurses, so every ancestor with [H*] along the path gets
      // updated with its own direct child.
      if (desc->parent != detail::NO_STATE<TState>()) {
        DescType* parent_desc = _find(desc->parent);
        if (parent_desc && parent_desc->hasHistory()) {
          // Always overwrite so the most recently active child is remembered.
          parent_desc->last_active_child = c;
          STATECHART_LOGV("[%s] history save: parent=%d <- child=%d (%s)",
            _name,
            static_cast<int>(desc->parent),
            static_cast<int>(c),
            parent_desc->isDeep() ? "deep[H*]" : "shallow[H]");
        }
      }

      c = desc->parent;
    }
  }

  /**
   * @brief Entry chain - Enters states from the root down to the target state.
   *
   * Builds the path from 'target' up to the root, then executes onEnter top-down. After the
   * entries, calls _resolveLeaf() to handle composite states (initial child or history
   * restoration).
   *
   * @param target The state to enter.
   */
  void _enterState(TState target) {
    TState path[MaxStates];
    size_t path_len = 0;

    for (TState c = target; c != detail::NO_STATE<TState>() && path_len < MaxStates;) {
      path[path_len++] = c;
      DescType* d      = _find(c);
      c                = d ? d->parent : detail::NO_STATE<TState>();
    }

    // Execute entries top-down (path is stored bottom-up, so iterate in reverse).
    for (size_t i = path_len; i > 0; i--) {
      DescType* d = _find(path[i - 1]);
      if (d && d->on_enter) {
        STATECHART_LOGV("[%s] onEnter %d", _name, static_cast<int>(d->id));
        d->on_enter();
      }
    }

    _resolveLeaf(target);
  }

  /**
   * @brief Entry chain - Enters states from 'ancestor' (exclusive) down to 'target' (inclusive).
   *
   * Same as _enterState but only executes entries for states strictly between 'ancestor'
   * (exclusive) and 'target' (inclusive). Used by transitions where the LCA is the pivot: exits run
   * from source up to (but not including) the LCA, and entries run from the LCA down to the target.
   *
   * @param ancestor The state to start entering from (exclusive).
   * @param target The state to enter (inclusive).
   */
  void _enterFromAncestor(TState ancestor, TState target) {
    TState path[MaxStates];
    size_t path_len = 0;

    for (TState c = target;
         c != detail::NO_STATE<TState>() && c != ancestor && path_len < MaxStates;) {
      path[path_len++] = c;
      DescType* d      = _find(c);
      c                = d ? d->parent : detail::NO_STATE<TState>();
    }

    for (size_t i = path_len; i > 0; i--) {
      DescType* d = _find(path[i - 1]);
      if (d && d->on_enter) {
        STATECHART_LOGV("[%s] onEnter %d", _name, static_cast<int>(d->id));
        d->on_enter();
      }
    }

    _resolveLeaf(target);
  }

  /**
   * @brief Leaf resolution - Determines the final active (leaf) state after entering a state.
   *
   * After entering a state, determines the final active (leaf) state. Priority order:
   *
   * 1. History with a saved child - restores via recursive _enterFromAncestor. For Shallow: only
   * one level; for Deep: each level re-triggers its own history via _resolveLeaf recursion,
   * reconstructing the full path.
   *
   * 2. Explicit initial child - enters it via _enterFromAncestor.
   *
   * 3. 'target' has no children - it IS the leaf; set _current.
   *
   * Without this, a transition targeting a composite state would leave _current pointing at the
   * composite itself, which is invalid in UML statecharts.
   *
   * @param target The state to resolve from.
   */
  void _resolveLeaf(TState target) {
    DescType* desc = _find(target);
    if (!desc) {
      _current = target;
      return;
    }

    // Priority 1: history with a saved child.
    if (desc->hasHistory() && desc->last_active_child != detail::NO_STATE<TState>()) {
      TState child = desc->last_active_child;

      // Shallow history is consumed after use. The next re-entry (without an intervening exit that
      // saves history) will fall through to initial(). Deep history is persistent - it keeps
      // restoring the same leaf until a new exit updates last_active_child again.
      if (desc->isShallow()) {
        desc->last_active_child = detail::NO_STATE<TState>();
      }

      STATECHART_LOGV("[%s] %s restore -> %d",
        _name,
        desc->isDeep() ? "deep[H*]" : "shallow[H]",
        static_cast<int>(child));

      _enterFromAncestor(target, child);
      return;
    }

    // Priority 2: explicit initial child.
    if (desc->hasInitial()) {
      STATECHART_LOGV("[%s] initial -> %d", _name, static_cast<int>(desc->initial_child));
      _enterFromAncestor(target, desc->initial_child);
      return;
    }

    // Priority 3: target is a leaf state.
    _current = target;
  }

  // Internal transition
  void _doInternal(const detail::Transition<TState, TEvent>& t) {
    STATECHART_LOGD("[%s] Internal ev in state:%d", _name, static_cast<int>(_current));
    if (t.action) t.action();
    // _current deliberately not changed.
  }

  /**
   * @brief Executes an external transition: exit current -> action -> enter target.
   *
   * 1. Find LCA of current state and target.
   * 2. Run exit chain from current up to (not including) LCA.
   * 3. Run transition action.
   * 4. Run entry chain from LCA down to target (+ leaf resolution).
   *
   * @param t The transition to execute.
   */
  void _doExternal(const detail::Transition<TState, TEvent>& t) {
    TState source = _current;
    TState target = t.target;

    STATECHART_LOGD("[%s] External %d -> %d",
      _name,
      static_cast<int>(source),
      static_cast<int>(target));

    TState ancestor = _lca(source, target);

    _exitToAncestor(source, ancestor);

    if (t.action) {
      STATECHART_LOGV("[%s] transition action", _name);
      t.action();
    }

    _enterFromAncestor(ancestor, target);

    STATECHART_LOGD("[%s] current -> %d", _name, static_cast<int>(_current));
  }
};

} // namespace Statechart
