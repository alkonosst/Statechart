/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Traffic Light Example Overview:
 * - This example demonstrates a simple traffic light controller using the HSM framework. It models
 *   a traffic light that cycles through Red, Green, and Yellow states on each Tick event.
 * - The example covers the following features:
 *   - Flat FSM (no composite states, no hierarchy)
 *   - on() unconditional transitions
 *   - onEnter / onExit callbacks
 *   - getCurrentState() to query the active leaf state
 *   - hasStarted() to check whether start() has been called
 *   - isValid() to detect construction overflow
 *   - debugDump() to inspect the full machine layout
 *   - reset() to restart from the initial state
 *
 * State hierarchy (flat):
 *   Red -> Green -> Yellow -> Red  (tick-driven cycle)
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t { Red, Green, Yellow };
enum class Event : uint8_t { Tick };

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 3, 3> hsm("TrafficLight", State::Red);

// Helper
const char* stateName(State s) {
  switch (s) {
    case State::Red: return "Red";
    case State::Green: return "Green";
    case State::Yellow: return "Yellow";
    default: return "?";
  }
}

void setupHSM() {
  // Setup transitions
  hsm.addState(State::Red)
    .onEnter([] { Serial.println("  [TL] RED    - stop"); })
    .onExit([] { Serial.println("  [TL]        (leaving Red)"); })
    .on(Event::Tick, State::Green);

  hsm.addState(State::Green)
    .onEnter([] { Serial.println("  [TL] GREEN  - go"); })
    .on(Event::Tick, State::Yellow);

  hsm.addState(State::Yellow)
    .onEnter([] { Serial.println("  [TL] YELLOW - slow down"); })
    .on(Event::Tick, State::Red);

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

  Serial.println("----------------------------------");
  Serial.println("Statechart - Traffic Light Example");
  Serial.println("----------------------------------");

  // hasStarted() is false before start()
  Serial.printf("  hasStarted() before start: %s\n", hsm.hasStarted() ? "true" : "false");

  setupHSM();

  // hasStarted() is true after start()
  Serial.printf("  hasStarted() after  start: %s\n", hsm.hasStarted() ? "true" : "false");

  // One full cycle
  Serial.println("\n-- One full cycle --");
  hsm.dispatch(Event::Tick); // Red    -> Green
  hsm.dispatch(Event::Tick); // Green  -> Yellow
  hsm.dispatch(Event::Tick); // Yellow -> Red

  Serial.printf("  getCurrentState() = %s\n", stateName(hsm.getCurrentState()));

  // Partial cycle, then reset
  Serial.println("\n-- Partial cycle, then reset --");
  hsm.dispatch(Event::Tick); // Red -> Green

  Serial.printf("  Before reset: %s\n", stateName(hsm.getCurrentState()));
  hsm.reset(); // exits Green, re-enters Red
  Serial.printf("  After  reset: %s\n", stateName(hsm.getCurrentState()));
}

void loop() { delay(1000); }
