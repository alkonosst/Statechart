/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Unit tests for the Statechart library.
 *
 * Each test group covers one feature area. Tests use Unity (available via PlatformIO).
 *
 * Coverage:
 *   - Simple state transitions
 *   - Entry and exit actions
 *   - Transition actions
 *   - Guards (allow / deny)
 *   - Multiple transitions for same event (fallthrough with guard)
 *   - All guards failing returns false (no fallback transition)
 *   - Event inheritance (child -> parent)
 *   - Composite states with initial() child
 *   - Internal transitions (no exit/entry, current unchanged)
 *   - Internal transition does not fire onExit
 *   - Internal transition inherited from parent
 *   - Shallow history
 *   - Deep history
 *   - reset() clears saved history
 *   - isInState()
 *   - reset()
 *   - reset() before start() is a no-op
 *   - dispatch() before start() returns false
 *   - start() idempotency
 *   - hasStarted() before and after start()
 *   - MaxStates overflow sets isValid() to false
 */

#ifdef ARDUINO
#  include <Arduino.h>
#endif

#include <unity.h>

#include <Statechart.h>

using namespace Statechart;

/* ---------------------------------------------------------------------------------------------- */
/*                                    Shared States and Events                                    */
/* ---------------------------------------------------------------------------------------------- */

// Simple two-state enum used by most tests.
enum class S : uint8_t { A = 0, B = 1, C = 2, D = 3, PARENT = 4, CHILD1 = 5, CHILD2 = 6 };
enum class E : uint8_t { GO = 0, BACK = 1, OTHER = 2, INTERNAL = 3 };

/* ---------------------------------------------------------------------------------------------- */
/*                                   Simple external transition                                   */
/* ---------------------------------------------------------------------------------------------- */

void test_simple_transition() {
  HSM<S, E, 4, 4> fsm("t_simple", S::A);
  fsm.addState(S::A).on(E::GO, S::B);
  fsm.addState(S::B);
  fsm.start();

  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());
  bool handled = fsm.dispatch(E::GO);
  TEST_ASSERT_TRUE(handled);
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                             Entry and exit actions order test                                  */
/* ---------------------------------------------------------------------------------------------- */

void test_entry_exit_order() {
  static int call_order[4];
  static int call_index = 0;
  call_index            = 0;

  HSM<S, E, 4, 4> fsm("t_entry_exit", S::A);
  fsm.addState(S::A)
    .onEnter([] { call_order[call_index++] = 1; }) // enter A
    .onExit([] { call_order[call_index++] = 2; })  // exit A
    .on(E::GO, S::B);
  fsm.addState(S::B).onEnter([] { call_order[call_index++] = 3; }); // enter B

  fsm.start(); // fires enter A
  fsm.dispatch(E::GO);

  TEST_ASSERT_EQUAL(3, call_index);
  TEST_ASSERT_EQUAL(1, call_order[0]); // enter A on start
  TEST_ASSERT_EQUAL(2, call_order[1]); // exit A on transition
  TEST_ASSERT_EQUAL(3, call_order[2]); // enter B on transition
}

/* ---------------------------------------------------------------------------------------------- */
/*                       Transition action is called between exit and entry                       */
/* ---------------------------------------------------------------------------------------------- */

void test_transition_action_order() {
  static int seq[4];
  static int idx = 0;
  idx            = 0;

  HSM<S, E, 4, 4> fsm("t_action_order", S::A);

  // transition action
  fsm.addState(S::A).onExit([] { seq[idx++] = 1; }).on(E::GO, S::B, [] { seq[idx++] = 2; });
  fsm.addState(S::B).onEnter([] { seq[idx++] = 3; });

  fsm.start();
  fsm.dispatch(E::GO);

  TEST_ASSERT_EQUAL(3, idx);
  TEST_ASSERT_EQUAL(1, seq[0]); // exit first
  TEST_ASSERT_EQUAL(2, seq[1]); // then transition action
  TEST_ASSERT_EQUAL(3, seq[2]); // then entry
}

/* ---------------------------------------------------------------------------------------------- */
/*                                  Guard allows transition test                                  */
/* ---------------------------------------------------------------------------------------------- */

void test_guard_allows() {
  HSM<S, E, 4, 4> fsm("t_guard_allow", S::A);
  fsm.addState(S::A).on(E::GO, S::B, [] { return true; });
  fsm.addState(S::B);
  fsm.start();

  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                  Guard denies transition test                                  */
/* ---------------------------------------------------------------------------------------------- */

void test_guard_denies() {
  HSM<S, E, 4, 4> fsm("t_guard_deny", S::A);
  fsm.addState(S::A).on(E::GO, S::B, [] { return false; });
  fsm.addState(S::B);
  fsm.start();

  TEST_ASSERT_FALSE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState()); // unchanged
}

/* ---------------------------------------------------------------------------------------------- */
/*                 Multiple transitions for the same event, fallthrough with guard                */
/* ---------------------------------------------------------------------------------------------- */

void test_guard_fallthrough() {
  static int counter = 0;
  counter            = 0;

  HSM<S, E, 4, 4> fsm("t_fallthrough", S::A);
  fsm.addState(S::A)
    .on(E::GO, S::B, [] { return ++counter <= 2; }) // first two dispatches take this
    .on(E::GO, S::C);                               // third dispatch takes this
  fsm.addState(S::B).on(E::BACK, S::A);
  fsm.addState(S::C);
  fsm.start();

  fsm.dispatch(E::GO);
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
  fsm.dispatch(E::BACK);

  fsm.dispatch(E::GO);
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
  fsm.dispatch(E::BACK);

  fsm.dispatch(E::GO); // counter = 3, first guard fails -> falls to S::C
  TEST_ASSERT_EQUAL((int)S::C, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                  Unhandled event returns false                                 */
/* ---------------------------------------------------------------------------------------------- */

void test_unhandled_event() {
  HSM<S, E, 4, 4> fsm("t_unhandled", S::A);
  fsm.addState(S::A);
  fsm.start();

  TEST_ASSERT_FALSE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                         Event inheritance - child propagates to parent                         */
/* ---------------------------------------------------------------------------------------------- */

void test_event_inheritance() {
  HSM<S, E, 4, 4> fsm("t_inherit", S::CHILD1);
  // PARENT handles GO, CHILD1 does not -> should inherit
  fsm.addState(S::PARENT).on(E::GO, S::C);
  fsm.addState(S::CHILD1).parent(S::PARENT);
  fsm.addState(S::C);
  fsm.start();

  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState());
  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::C, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                              Composite state with initial() child                              */
/* ---------------------------------------------------------------------------------------------- */

void test_initial_child() {
  HSM<S, E, 4, 4> fsm("t_initial", S::A);
  // Transition into PARENT directly; should land in CHILD1 via initial().
  fsm.addState(S::A).on(E::GO, S::PARENT);
  fsm.addState(S::PARENT).initial(S::CHILD1);
  fsm.addState(S::CHILD1).parent(S::PARENT);
  fsm.addState(S::CHILD2).parent(S::PARENT);
  fsm.start();

  fsm.dispatch(E::GO);
  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                        Internal transition does not change current state                       */
/* ---------------------------------------------------------------------------------------------- */

void test_internal_transition() {
  static int action_count = 0;
  action_count            = 0;

  static int enter_count = 0;
  enter_count            = 0;

  HSM<S, E, 4, 4> fsm("t_internal", S::A);
  fsm.addState(S::A).onEnter([] { enter_count++; }).onInternal(E::GO, [] { action_count++; });
  fsm.start();

  TEST_ASSERT_EQUAL(1, enter_count); // entered once on start
  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState()); // no state change
  TEST_ASSERT_EQUAL(2, action_count);
  TEST_ASSERT_EQUAL(1, enter_count); // onEnter not called again
}

/* ---------------------------------------------------------------------------------------------- */
/*                                 Internal transition with guard                                 */
/* ---------------------------------------------------------------------------------------------- */

void test_internal_transition_guard() {
  static int action_count = 0;
  action_count            = 0;
  static bool allow       = false;

  HSM<S, E, 4, 4> fsm("t_internal_guard", S::A);
  fsm.addState(S::A).onInternal(
    E::GO,
    [] { return allow; },
    [] { action_count++; });
  fsm.start();

  TEST_ASSERT_FALSE(fsm.dispatch(E::GO)); // guard denies
  TEST_ASSERT_EQUAL(0, action_count);

  allow = true;
  TEST_ASSERT_TRUE(fsm.dispatch(E::GO)); // guard allows
  TEST_ASSERT_EQUAL(1, action_count);
}

/* ---------------------------------------------------------------------------------------------- */
/*                              Shallow history restores direct child                             */
/* ---------------------------------------------------------------------------------------------- */

void test_shallow_history() {
  // Hierarchy: A --GO--> PARENT[H initial:CHILD1] { CHILD1 --GO--> CHILD2 }
  //            PARENT --BACK--> A
  // Sequence: start at A -> go to PARENT -> enter CHILD1 (initial)
  //           -> go to CHILD2 -> go back to A (exits CHILD2, saves H=CHILD2 in PARENT)
  //           -> go to PARENT again -> history restores CHILD2 (not CHILD1)

  HSM<S, E, 4, 4> fsm("t_shallow", S::A);
  fsm.addState(S::A).on(E::GO, S::PARENT);
  fsm.addState(S::PARENT).initial(S::CHILD1).withHistory().on(E::BACK, S::A);
  fsm.addState(S::CHILD1).parent(S::PARENT).on(E::GO, S::CHILD2);
  fsm.addState(S::CHILD2).parent(S::PARENT);
  fsm.start();

  fsm.dispatch(E::GO); // A -> PARENT -> CHILD1 (initial)
  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState());

  fsm.dispatch(E::GO); // CHILD1 -> CHILD2
  TEST_ASSERT_EQUAL((int)S::CHILD2, (int)fsm.getCurrentState());

  fsm.dispatch(E::BACK); // PARENT (from CHILD2) -> A, saves H=CHILD2
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());

  fsm.dispatch(E::GO); // A -> PARENT -> history -> CHILD2
  TEST_ASSERT_EQUAL((int)S::CHILD2, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                              Deep history restores full leaf path                              */
/* ---------------------------------------------------------------------------------------------- */

void test_deep_history() {
  // Hierarchy: A --GO--> PARENT[H* initial:CHILD1]
  //              CHILD1 --GO--> CHILD2
  //            PARENT --BACK--> A
  // After reaching CHILD2 and going back, re-entering PARENT should restore CHILD2.

  HSM<S, E, 4, 4> fsm("t_deep", S::A);
  fsm.addState(S::A).on(E::GO, S::PARENT);
  fsm.addState(S::PARENT).initial(S::CHILD1).withDeepHistory().on(E::BACK, S::A);
  fsm.addState(S::CHILD1).parent(S::PARENT).on(E::GO, S::CHILD2);
  fsm.addState(S::CHILD2).parent(S::PARENT);
  fsm.start();

  fsm.dispatch(E::GO);   // A -> PARENT -> CHILD1
  fsm.dispatch(E::GO);   // CHILD1 -> CHILD2
  fsm.dispatch(E::BACK); // -> A, deep saves CHILD2
  fsm.dispatch(E::GO);   // A -> PARENT -> deep history -> CHILD2
  TEST_ASSERT_EQUAL((int)S::CHILD2, (int)fsm.getCurrentState());

  // History is updated on every exit, so re-entry always restores the last active child
  fsm.dispatch(E::BACK);
  fsm.dispatch(E::GO);
  TEST_ASSERT_EQUAL((int)S::CHILD2, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                     isInState() returns true for current and all ancestors                     */
/* ---------------------------------------------------------------------------------------------- */

void test_is_in_state() {
  HSM<S, E, 4, 4> fsm("t_isinstate", S::CHILD1);
  fsm.addState(S::PARENT);
  fsm.addState(S::CHILD1).parent(S::PARENT);
  fsm.start();

  TEST_ASSERT_TRUE(fsm.isInState(S::CHILD1));
  TEST_ASSERT_TRUE(fsm.isInState(S::PARENT)); // ancestor check
  TEST_ASSERT_FALSE(fsm.isInState(S::CHILD2));
  TEST_ASSERT_FALSE(fsm.isInState(S::A));
}

/* ---------------------------------------------------------------------------------------------- */
/*                       reset() returns to initial state and re-fires entry                      */
/* ---------------------------------------------------------------------------------------------- */

void test_reset() {
  static int enter_count = 0;
  enter_count            = 0;

  HSM<S, E, 4, 4> fsm("t_reset", S::A);
  fsm.addState(S::A).onEnter([] { enter_count++; }).on(E::GO, S::B);
  fsm.addState(S::B);
  fsm.start(); // enter A -> enter_count = 1

  fsm.dispatch(E::GO); // -> B
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());

  fsm.reset(); // exit B, enter A -> enter_count = 2
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());
  TEST_ASSERT_EQUAL(2, enter_count);
}

/* ---------------------------------------------------------------------------------------------- */
/*                             dispatch() before start() returns false                            */
/* ---------------------------------------------------------------------------------------------- */

void test_dispatch_before_start() {
  HSM<S, E, 4, 4> fsm("t_no_start", S::A);
  fsm.addState(S::A).on(E::GO, S::B);
  fsm.addState(S::B);

  TEST_ASSERT_FALSE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                        start() called twice is a no-op (no double entry)                       */
/* ---------------------------------------------------------------------------------------------- */

void test_start_idempotent() {
  static int enter_count = 0;
  enter_count            = 0;

  HSM<S, E, 4, 4> fsm("t_start_idem", S::A);
  fsm.addState(S::A).onEnter([] { enter_count++; });
  fsm.start();
  fsm.start(); // second call is no-op

  TEST_ASSERT_EQUAL(1, enter_count);
}

/* ---------------------------------------------------------------------------------------------- */
/*                    LCA exits and entries are correct across hierarchy levels                   */
/* ---------------------------------------------------------------------------------------------- */

void test_lca_exits_entries() {
  // Hierarchy:
  //   PARENT
  //     CHILD1 --GO--> CHILD2
  //     CHILD2
  // Transitioning CHILD1 -> CHILD2 should only exit CHILD1 and enter CHILD2,
  // NOT exit/enter PARENT (since it is the LCA).

  static int parent_enter = 0;
  static int parent_exit  = 0;
  parent_enter            = 0;
  parent_exit             = 0;

  HSM<S, E, 4, 4> fsm("t_lca", S::CHILD1);
  fsm.addState(S::PARENT).onEnter([] { parent_enter++; }).onExit([] { parent_exit++; });
  fsm.addState(S::CHILD1).parent(S::PARENT).on(E::GO, S::CHILD2);
  fsm.addState(S::CHILD2).parent(S::PARENT);
  fsm.start(); // enters PARENT then CHILD1 -> parent_enter = 1

  fsm.dispatch(E::GO);                // CHILD1 -> CHILD2; LCA = PARENT, must not exit/enter PARENT
  TEST_ASSERT_EQUAL(1, parent_enter); // not incremented again
  TEST_ASSERT_EQUAL(0, parent_exit);  // not exited
  TEST_ASSERT_EQUAL((int)S::CHILD2, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*              reset() clears all saved history (next entry goes to initial())                   */
/* ---------------------------------------------------------------------------------------------- */

void test_reset_clears_history() {
  HSM<S, E, 4, 4> fsm("t_reset_history", S::A);
  fsm.addState(S::A).on(E::GO, S::PARENT);
  fsm.addState(S::PARENT).initial(S::CHILD1).withHistory().on(E::BACK, S::A);
  fsm.addState(S::CHILD1).parent(S::PARENT).on(E::GO, S::CHILD2);
  fsm.addState(S::CHILD2).parent(S::PARENT);
  fsm.start();

  fsm.dispatch(E::GO);   // A -> PARENT -> CHILD1
  fsm.dispatch(E::GO);   // CHILD1 -> CHILD2
  fsm.dispatch(E::BACK); // -> A, saves H=CHILD2 in PARENT

  fsm.reset(); // clears all history

  fsm.dispatch(E::GO); // A -> PARENT -> initial() -> CHILD1 (history was cleared)
  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*         Internal transition defined on parent is inherited and executed by child               */
/* ---------------------------------------------------------------------------------------------- */

void test_internal_inherited_from_parent() {
  static int action_count = 0;
  action_count            = 0;

  HSM<S, E, 4, 4> fsm("t_internal_inherit", S::CHILD1);
  fsm.addState(S::PARENT).onInternal(E::GO, [] { action_count++; });
  fsm.addState(S::CHILD1).parent(S::PARENT);
  fsm.start();

  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState());
  TEST_ASSERT_TRUE(fsm.dispatch(E::GO)); // GO not on CHILD1; inherited from PARENT as internal
  TEST_ASSERT_EQUAL(1, action_count);
  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState()); // no state change
}

/* ---------------------------------------------------------------------------------------------- */
/*               All guards failing on an event causes dispatch() to return false                 */
/* ---------------------------------------------------------------------------------------------- */

void test_all_guards_fail_returns_false() {
  HSM<S, E, 4, 4> fsm("t_all_guards_fail", S::A);
  fsm.addState(S::A).on(E::GO, S::B, [] { return false; }).on(E::GO, S::C, [] { return false; });
  fsm.addState(S::B);
  fsm.addState(S::C);
  fsm.start();

  TEST_ASSERT_FALSE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*               Exceeding MaxStates during construction sets isValid() to false                  */
/* ---------------------------------------------------------------------------------------------- */

void test_overflow_isvalid() {
  HSM<S, E, 4, 4> fsm("t_overflow", S::A); // MaxStates = 4
  fsm.addState(S::A);
  fsm.addState(S::B);
  fsm.addState(S::C);
  fsm.addState(S::D);
  TEST_ASSERT_TRUE(fsm.isValid());

  fsm.addState(S::PARENT); // exceeds MaxStates
  TEST_ASSERT_FALSE(fsm.isValid());

  fsm.start(); // start() on an invalid HSM is a no-op
  TEST_ASSERT_FALSE(fsm.hasStarted());
}

/* ---------------------------------------------------------------------------------------------- */
/*                      hasStarted() returns false before start(), true after                     */
/* ---------------------------------------------------------------------------------------------- */

void test_has_started() {
  HSM<S, E, 4, 4> fsm("t_has_started", S::A);
  fsm.addState(S::A);

  TEST_ASSERT_FALSE(fsm.hasStarted());
  fsm.start();
  TEST_ASSERT_TRUE(fsm.hasStarted());
}

/* ---------------------------------------------------------------------------------------------- */
/*                         reset() before start() is a no-op (no entry fired)                    */
/* ---------------------------------------------------------------------------------------------- */

void test_reset_before_start_noop() {
  static int enter_count = 0;
  enter_count            = 0;

  HSM<S, E, 4, 4> fsm("t_reset_no_start", S::A);
  fsm.addState(S::A).onEnter([] { enter_count++; });

  fsm.reset(); // no-op: start() has not been called
  TEST_ASSERT_EQUAL(0, enter_count);
}

/* ---------------------------------------------------------------------------------------------- */
/*                   Internal transition does not fire onExit of the active state                 */
/* ---------------------------------------------------------------------------------------------- */

void test_internal_exit_not_fired() {
  static int exit_count = 0;
  exit_count            = 0;

  HSM<S, E, 4, 4> fsm("t_internal_no_exit", S::A);
  fsm.addState(S::A).onExit([] { exit_count++; }).onInternal(E::GO, [] {});
  fsm.start();

  fsm.dispatch(E::GO);
  fsm.dispatch(E::GO);
  TEST_ASSERT_EQUAL(0, exit_count);
}

/* ---------------------------------------------------------------------------------------------- */
/*               addState() on the same id returns the existing slot (idempotent)                 */
/* ---------------------------------------------------------------------------------------------- */

void test_add_state_twice_same_id() {
  // Configuration may be split across multiple addState() calls on the same id.
  HSM<S, E, 4, 4> fsm("t_add_twice", S::A);
  fsm.addState(S::A).onEnter([] {});
  fsm.addState(S::A).on(E::GO, S::B); // same id again -> reuses the existing slot
  fsm.addState(S::B);
  fsm.start();

  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                 Exceeding MaxTransitions with external transitions sets overflow               */
/* ---------------------------------------------------------------------------------------------- */

void test_max_transitions_overflow_external() {
  HSM<S, E, 4, 4> fsm("t_tr_over_ext", S::A); // MaxTransitions = 4
  fsm.addState(S::A)
    .on(E::GO, S::B)
    .on(E::BACK, S::B)
    .on(E::OTHER, S::B)
    .on(E::INTERNAL, S::B);           // pool is now full (4)
  fsm.addState(S::B).on(E::GO, S::A); // exceeds MaxTransitions

  TEST_ASSERT_FALSE(fsm.isValid());
}

/* ---------------------------------------------------------------------------------------------- */
/*                 Exceeding MaxTransitions with internal transitions sets overflow               */
/* ---------------------------------------------------------------------------------------------- */

void test_max_transitions_overflow_internal() {
  HSM<S, E, 4, 4> fsm("t_tr_over_int", S::A); // MaxTransitions = 4
  fsm.addState(S::A)
    .onInternal(E::GO, [] {})
    .onInternal(E::BACK, [] {})
    .onInternal(E::OTHER, [] {})
    .onInternal(E::INTERNAL, [] {});           // pool is now full (4)
  fsm.addState(S::B).onInternal(E::GO, [] {}); // exceeds MaxTransitions

  TEST_ASSERT_FALSE(fsm.isValid());
}

/* ---------------------------------------------------------------------------------------------- */
/*                 Internal transition with an empty action just swallows the event               */
/* ---------------------------------------------------------------------------------------------- */

void test_internal_empty_action() {
  HSM<S, E, 4, 4> fsm("t_internal_noaction", S::A);
  fsm.addState(S::A).onInternal(E::GO, {}); // internal transition without an action
  fsm.start();

  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));                    // handled, but nothing runs
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState()); // no state change
}

/* ---------------------------------------------------------------------------------------------- */
/*           Dispatch skips a same-state transition registered for a different event              */
/* ---------------------------------------------------------------------------------------------- */

void test_dispatch_skips_other_event_same_state() {
  // A owns BACK (first in the pool) and GO (second). Dispatching GO must skip the BACK transition
  // that belongs to the same state before reaching the matching GO transition.
  HSM<S, E, 4, 4> fsm("t_skip_event", S::A);
  fsm.addState(S::A).on(E::BACK, S::C).on(E::GO, S::B);
  fsm.addState(S::B);
  fsm.addState(S::C);
  fsm.start();

  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*              Defensive: child referencing an unregistered parent and target                    */
/* ---------------------------------------------------------------------------------------------- */

void test_malformed_unregistered_states() {
  // A child references a parent that was never registered, and transitions to an unregistered
  // target. Lookups that miss must stop the walk cleanly instead of crashing.
  HSM<S, E, 4, 4> fsm("t_malformed", S::CHILD1);
  fsm.addState(S::CHILD1).parent(S::PARENT).on(E::GO, S::B); // PARENT and B never registered
  fsm.start();
  TEST_ASSERT_EQUAL((int)S::CHILD1, (int)fsm.getCurrentState());

  TEST_ASSERT_TRUE(fsm.dispatch(E::GO)); // CHILD1 -> B (unregistered, becomes the current leaf)
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*           Defensive: event propagation and isInState() stop at an unregistered parent          */
/* ---------------------------------------------------------------------------------------------- */

void test_malformed_event_propagation() {
  HSM<S, E, 4, 4> fsm("t_malformed2", S::CHILD1);
  fsm.addState(S::CHILD1).parent(S::PARENT); // PARENT never registered, no handler for GO
  fsm.start();

  TEST_ASSERT_FALSE(fsm.dispatch(E::GO)); // propagation to PARENT misses -> unhandled
  TEST_ASSERT_FALSE(fsm.isInState(S::A)); // ancestor walk stops at the unregistered PARENT
}

/* ---------------------------------------------------------------------------------------------- */
/*               Defensive: a parent cycle does not cause an infinite loop                        */
/* ---------------------------------------------------------------------------------------------- */

void test_cyclic_config_no_infinite_loop() {
  // A <-> B form a parent cycle. The MaxStates cap on the enter/LCA walks must bound the iteration
  // so neither start() nor dispatch() hangs.
  HSM<S, E, 4, 4> fsm("t_cycle", S::A);
  fsm.addState(S::A).parent(S::B).on(E::GO, S::B);
  fsm.addState(S::B).parent(S::A);
  fsm.start();
  TEST_ASSERT_EQUAL((int)S::A, (int)fsm.getCurrentState());

  TEST_ASSERT_TRUE(fsm.dispatch(E::GO));
  TEST_ASSERT_EQUAL((int)S::B, (int)fsm.getCurrentState());
}

/* ---------------------------------------------------------------------------------------------- */
/*                                             Runners                                            */
/* ---------------------------------------------------------------------------------------------- */

void setUp(void) {
  // set stuff up here
}

void tearDown(void) {
  // clean stuff up here
}

int runUnityTests(void) {
  UNITY_BEGIN();

  RUN_TEST(test_simple_transition);
  RUN_TEST(test_entry_exit_order);
  RUN_TEST(test_transition_action_order);
  RUN_TEST(test_guard_allows);
  RUN_TEST(test_guard_denies);
  RUN_TEST(test_guard_fallthrough);
  RUN_TEST(test_unhandled_event);
  RUN_TEST(test_event_inheritance);
  RUN_TEST(test_initial_child);
  RUN_TEST(test_internal_transition);
  RUN_TEST(test_internal_transition_guard);
  RUN_TEST(test_shallow_history);
  RUN_TEST(test_deep_history);
  RUN_TEST(test_is_in_state);
  RUN_TEST(test_reset);
  RUN_TEST(test_dispatch_before_start);
  RUN_TEST(test_start_idempotent);
  RUN_TEST(test_lca_exits_entries);
  RUN_TEST(test_reset_clears_history);
  RUN_TEST(test_internal_inherited_from_parent);
  RUN_TEST(test_all_guards_fail_returns_false);
  RUN_TEST(test_overflow_isvalid);
  RUN_TEST(test_has_started);
  RUN_TEST(test_reset_before_start_noop);
  RUN_TEST(test_internal_exit_not_fired);
  RUN_TEST(test_add_state_twice_same_id);
  RUN_TEST(test_max_transitions_overflow_external);
  RUN_TEST(test_max_transitions_overflow_internal);
  RUN_TEST(test_internal_empty_action);
  RUN_TEST(test_dispatch_skips_other_event_same_state);
  RUN_TEST(test_malformed_unregistered_states);
  RUN_TEST(test_malformed_event_propagation);
  RUN_TEST(test_cyclic_config_no_infinite_loop);

  return UNITY_END();
}

// For native
int main(void) { return runUnityTests(); }

// For Arduino framework
#ifdef ARDUINO
void setup() {
  delay(2000);
  runUnityTests();
}
void loop() {}
#endif
