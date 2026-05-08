/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** Explanation of this example:
 * - This example demonstrates a temperature process controller with a deep state hierarchy. The
 *   controller initializes, calibrates, then runs a heating/holding/cooling cycle. A fault can
 *   interrupt at any point; deep history on Running ensures the exact leaf state is restored after
 *   fault recovery, not just the entry of the top-level composite.
 * - The example covers the following features:
 *   - Deep hierarchy (Operational -> Running -> Heating/Holding/Cooling).
 *   - withDeepHistory() [H*] on Running: restores the exact leaf state after fault recovery.
 *   - onInternal() on a composite state: heartbeat counter without state change, inherited by all
 *     children.
 *   - Event inheritance: FaultDetected defined once on Operational and inherited by all substates.
 *   - Guards reading external signals (temperature vs setpoint).
 *   - isInState() to query whether the machine is inside a composite state.
 *
 * State hierarchy:
 *   Operational  [initial: Init]
 *     Init
 *     Calibrating
 *     Running  [H* deep, initial: Heating]
 *       Heating
 *       Holding
 *       Cooling
 *   Fault
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t {
  Operational,
  Init,
  Calibrating,
  Running,
  Heating,
  Holding,
  Cooling,
  Fault,
};

enum class Event : uint8_t {
  Start,
  CalOk,
  SetpointReached,
  DeviationHigh,
  StartCool,
  CoolDone,
  FaultDetected,
  FaultAck,
  Heartbeat,
};

// External state
static float temperature        = 25.0f;
static float setpoint           = 60.0f;
static uint32_t heartbeat_count = 0;

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 8, 9> hsm("TemperatureController", State::Operational);

void setupHSM() {
  // Setup transitions

  // Operational is the composite root. FaultDetected is defined here once and inherited by all
  // children. Heartbeat is an internal transition: counts without changing state or triggering
  // exit/entry callbacks, and is also inherited by all children.
  hsm.addState(State::Operational)
    .initial(State::Init)
    .onEnter([] { Serial.println("  [TC] System operational"); })
    .on(Event::FaultDetected,
      State::Fault,
      [] { Serial.println("  [TC] *** FAULT *** actuators off"); })
    .onInternal(Event::Heartbeat, [] {
      heartbeat_count++;
      Serial.printf("  [TC] Heartbeat #%lu\n", heartbeat_count);
    });

  hsm.addState(State::Init)
    .parent(State::Operational)
    .onEnter([] { Serial.println("  [TC] Init"); })
    .on(Event::Start, State::Calibrating);

  hsm.addState(State::Calibrating)
    .parent(State::Operational)
    .onEnter([] { Serial.println("  [TC] Calibrating sensors..."); })
    .on(Event::CalOk, State::Running, [] { Serial.println("  [TC] Calibration ok"); });

  // withDeepHistory() saves the exact leaf state (e.g. Cooling) on exit. When Fault is acknowledged
  // and the machine re-enters Running, it restores that leaf directly instead of starting from
  // Heating.
  hsm.addState(State::Running)
    .parent(State::Operational)
    .initial(State::Heating)
    .withDeepHistory()
    .onEnter([] { Serial.println("  [TC] Running (enter)"); })
    .onExit([] { Serial.println("  [TC] Running (exit)"); });

  hsm.addState(State::Heating)
    .parent(State::Running)
    .onEnter([] {
      Serial.println("  [TC] Heating: resistor ON");
      temperature = 25.0f;
    })
    .onExit([] { Serial.println("  [TC] Heating: resistor OFF"); })
    .on(
      Event::SetpointReached,
      State::Holding,
      [] { return temperature >= setpoint; },
      [] { Serial.println("  [TC] Setpoint reached"); });

  hsm.addState(State::Holding)
    .parent(State::Running)
    .onEnter([] { Serial.println("  [TC] Holding temperature"); })
    .on(
      Event::DeviationHigh,
      State::Heating,
      [] { return (setpoint - temperature) > 5.0f; },
      [] { Serial.println("  [TC] Deviation too high, reheating"); })
    .on(Event::StartCool, State::Cooling, [] { Serial.println("  [TC] Starting cooling"); });

  hsm.addState(State::Cooling)
    .parent(State::Running)
    .onEnter([] { Serial.println("  [TC] Cooling: fan ON"); })
    .onExit([] { Serial.println("  [TC] Cooling: fan OFF"); })
    .on(Event::CoolDone, State::Holding, [] { Serial.println("  [TC] Cooling done"); });

  // Fault is outside Operational, so FaultDetected does not apply here.
  // FaultAck targets Running directly so deep history restores the exact leaf.
  hsm.addState(State::Fault)
    .onEnter([] { Serial.println("  [TC] Fault active"); })
    .onExit([] { Serial.println("  [TC] Fault cleared"); })
    .on(Event::FaultAck, State::Running, [] {
      Serial.println("  [TC] Fault acknowledged, restarting");
    });

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

  Serial.println("-----------------------------------");
  Serial.println("Statechart - Temperature Controller");
  Serial.println("-----------------------------------");

  setupHSM();

  // Startup sequence
  Serial.println("\n-- Startup sequence --");
  hsm.dispatch(Event::Start); // Init        -> Calibrating
  hsm.dispatch(Event::CalOk); // Calibrating -> Running -> Heating (initial)

  // Heartbeat (internal transition, inherited from Operational)
  Serial.println("\n-- Heartbeat (internal, no state change) --");
  hsm.dispatch(Event::Heartbeat);
  hsm.dispatch(Event::Heartbeat);
  Serial.printf("  heartbeat_count = %lu\n", heartbeat_count);

  // Heating cycle
  Serial.println("\n-- Heating cycle --");
  temperature = 60.0f;
  hsm.dispatch(Event::SetpointReached); // Heating -> Holding
  hsm.dispatch(Event::StartCool);       // Holding -> Cooling
  hsm.dispatch(Event::CoolDone);        // Cooling -> Holding

  // Fault while in Holding; deep history saves Holding
  Serial.println("\n-- Fault while in Holding (deep history will save Holding) --");
  hsm.dispatch(Event::FaultDetected); // Holding -> Fault (inherited from Operational)
  hsm.dispatch(Event::FaultAck);      // Fault   -> Running -> Holding (H*)

  Serial.printf("\n  isInState(Running) = %s\n", hsm.isInState(State::Running) ? "true" : "false");
  Serial.printf("  isInState(Holding) = %s\n", hsm.isInState(State::Holding) ? "true" : "false");
  Serial.printf("  isInState(Heating) = %s\n", hsm.isInState(State::Heating) ? "true" : "false");
}

void loop() { delay(1000); }