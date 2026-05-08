/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** Explanation of this example:
 * - This example demonstrates a water pump controller that uses only raw function pointers for all
 *   callbacks (std::function disabled). When STATECHART_NO_STD_FUNCTION is defined, the library
 *   replaces std::function with void(*)() for actions and bool(*)() for guards. Callbacks must be
 *   plain free functions or non-capturing lambdas (which decay to function pointers). Lambdas with
 *   captures are not allowed.
 * - The example covers the following features:
 *   - STATECHART_NO_STD_FUNCTION: replaces std::function with raw function pointers.
 *   - Less overhead: no std::function overhead, zero heap usage, no dynamic allocations.
 *
 * State hierarchy (flat):
 *   Off -> Running -> Off
 *              |
 *              v
 *           Error -> Off
 */

#include <Arduino.h>

// STATECHART_NO_STD_FUNCTION must be defined before the library include.
#define STATECHART_NO_STD_FUNCTION
#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t { Off, Running, Error };
enum class Event : uint8_t { Start, Stop, Fault, Reset };

// External state
static bool pipe_primed = false;
static int fault_code   = 0;

// Callbacks: plain free functions or non-capturing lambdas (which decay to function pointers).
// Lambdas with captures are not allowed.
bool isPrimed() { return pipe_primed; }
void onEnterOff() { Serial.println("  [Pump] Off"); }
void onEnterRunning() { Serial.println("  [Pump] Motor ON"); }
void onExitRunning() { Serial.println("  [Pump] Motor OFF"); }
void onEnterError() { Serial.printf("  [Pump] *** FAULT code %d ***\n", fault_code); }
void onStart() { Serial.println("  [Pump] Pipe primed, starting..."); }
void onFault() { Serial.println("  [Pump] Dry-run detected, stopping"); }
void onReset() {
  Serial.println("  [Pump] Fault cleared");
  fault_code = 0;
}

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 3, 4> hsm("Pump", State::Off);

void setupHSM() {
  // Setup transitions
  hsm.addState(State::Off)
    .onEnter(onEnterOff)
    // Guard: pipe must be primed before starting
    .on(Event::Start, State::Running, isPrimed, onStart);

  hsm.addState(State::Running)
    .onEnter(onEnterRunning)
    .onExit(onExitRunning)
    .on(Event::Stop, State::Off)
    .on(Event::Fault, State::Error, onFault);

  hsm.addState(State::Error).onEnter(onEnterError).on(Event::Reset, State::Off, onReset);

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

  Serial.println("----------------------------");
  Serial.println("Statechart - Pump Controller");
  Serial.println("----------------------------");

  setupHSM();

  // Start refused without priming
  Serial.println("\n-- Start without priming (guard fails) --");
  pipe_primed = false;
  hsm.dispatch(Event::Start); // guard fails, stays in Off

  // Prime and start
  Serial.println("\n-- Prime pipe and start --");
  pipe_primed = true;
  hsm.dispatch(Event::Start); // guard passes -> Running

  // Normal stop
  Serial.println("\n-- Normal stop --");
  hsm.dispatch(Event::Stop); // Running -> Off

  // Start and trigger a fault
  Serial.println("\n-- Start and trigger dry-run fault --");
  hsm.dispatch(Event::Start); // -> Running
  fault_code = 42;
  hsm.dispatch(Event::Fault); // -> Error
  hsm.dispatch(Event::Reset); // -> Off
}

void loop() { delay(1000); }