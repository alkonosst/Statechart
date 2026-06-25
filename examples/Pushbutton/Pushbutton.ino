/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Pushbutton Example Overview:
 * - This example demonstrates a pushbutton driver that absorbs electrical bounce and supports
 *   configurable auto-repeat while held. Internal transitions let the state machine react to events
 *   without triggering exit/entry callbacks and without changing the active state.
 * - The example covers the following features:
 *   - onInternal(event, action): react to an event without changing state or triggering any
 *     exit/entry callbacks.
 *   - onInternal(event, guard, action): conditional internal transition; the guard can enable or
 *     disable the reaction at runtime.
 *
 * State hierarchy (flat):
 *   Idle -> Pressed -> Held
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t { Idle, Pressed, Held };
enum class Event : uint8_t { Down, Up, HoldTimeout };

// External state
static int bounce_count    = 0;
static int repeat_count    = 0;
static bool repeat_enabled = true;

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 3, 6> hsm("Pushbutton", State::Idle);

void setupHSM() {
  // Setup transitions
  hsm.addState(State::Idle)
    .onEnter([] { Serial.println("  [Btn] Released"); })
    .on(Event::Down, State::Pressed);

  hsm.addState(State::Pressed)
    .onEnter([] {
      Serial.println("  [Btn] Pressed");
      bounce_count = 0;
    })
    // Spurious Down signals absorbed internally: no exit/entry, no state change
    .onInternal(Event::Down,
      [] {
        bounce_count++;
        Serial.printf("  [Btn] Bounce #%d absorbed\n", bounce_count);
      })
    .on(Event::Up, State::Idle)
    .on(Event::HoldTimeout, State::Held);

  hsm.addState(State::Held)
    .onEnter([] {
      Serial.println("  [Btn] Hold detected");
      repeat_count = 0;
    })
    // Auto-repeat fires only when enabled; guard disables it at runtime
    .onInternal(
      Event::HoldTimeout,
      [] { return repeat_enabled; },
      [] {
        repeat_count++;
        Serial.printf("  [Btn] Auto-repeat #%d\n", repeat_count);
      })
    .on(Event::Up, State::Idle);

  // Validate the machine configuration before starting. Checks for construction overflow (too many
  // states or transitions) and other common mistakes. Always call this before start() to catch
  // errors early.
  if (!hsm.isValid()) {
    Serial.println("HSM sizing invalid!");
    while (true)
      ;
  }

  // Start the machine: runs the entry chain for the initial state and marks the machine as started.
  hsm.start();

  // Debug dump shows the full state and transition configuration in a human-readable format.
  // Needs `STATECHART_LOG_LEVEL >= Info` to print.
  hsm.debugDump();
}

void setup() {
  Serial.begin(115200);
  delay(2000);

  Serial.println("-------------------------------");
  Serial.println("Statechart - Pushbutton Example");
  Serial.println("-------------------------------");

  setupHSM();

  // Simple press with bounce
  Serial.println("\n-- Simple press with bounces --");
  hsm.dispatch(Event::Down); // Idle -> Pressed
  hsm.dispatch(Event::Down); // internal: bounce #1 (no state change)
  hsm.dispatch(Event::Down); // internal: bounce #2 (no state change)
  hsm.dispatch(Event::Up);   // Pressed -> Idle
  Serial.printf("  Bounces absorbed: %d\n", bounce_count);

  // Hold with auto-repeat enabled
  Serial.println("\n-- Hold with auto-repeat (enabled) --");
  repeat_enabled = true;
  hsm.dispatch(Event::Down);        // Idle    -> Pressed
  hsm.dispatch(Event::HoldTimeout); // Pressed -> Held
  hsm.dispatch(Event::HoldTimeout); // internal: repeat #1
  hsm.dispatch(Event::HoldTimeout); // internal: repeat #2
  hsm.dispatch(Event::HoldTimeout); // internal: repeat #3
  hsm.dispatch(Event::Up);          // Held    -> Idle

  // Hold with auto-repeat disabled
  Serial.println("\n-- Hold with auto-repeat (disabled) --");
  repeat_enabled = false;
  hsm.dispatch(Event::Down);        // Idle    -> Pressed
  hsm.dispatch(Event::HoldTimeout); // Pressed -> Held
  hsm.dispatch(Event::HoldTimeout); // guard fails: no repeat, no state change
  hsm.dispatch(Event::HoldTimeout); // guard fails: no repeat, no state change
  hsm.dispatch(Event::Up);          // Held    -> Idle
  Serial.println("  (no repeat events fired)");
}

void loop() { delay(1000); }