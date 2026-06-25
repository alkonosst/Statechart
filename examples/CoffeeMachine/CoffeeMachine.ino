/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Coffee Machine Example Overview:
 * - This example demonstrates a coffee machine controller with a multi-level state hierarchy. The
 *   machine can be powered on and off, brew espresso through a two-step grind-then-extract
 *   sequence, or steam milk. An event defined on a composite parent is automatically inherited by
 *   all substates at any depth.
 * - The example covers the following features:
 *   - Composite states with parent() and initial().
 *   - Multi-level hierarchy (3 levels: Active -> Brewing -> Grinding/Extracting).
 *   - on(event, target, guard): guarded transition without action.
 *   - Event inheritance: PowerOff defined once on Active and inherited by all substates at any
 *     depth (Idle, Grinding, Extracting, Steaming).
 *
 * State hierarchy:
 *   Standby
 *   Active  [initial: Idle]
 *     Idle
 *     Brewing  [initial: Grinding]
 *       Grinding
 *       Extracting
 *     Steaming
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t { Standby, Active, Idle, Brewing, Grinding, Extracting, Steaming };
enum class Event : uint8_t { PowerOn, PowerOff, Brew, Steam, GrindDone, BrewDone, SteamDone };

// External state
static bool water_ok = true;

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 7, 7> hsm("CoffeeMachine", State::Standby);

void setupHSM() {
  // Setup transitions
  hsm.addState(State::Standby)
    .onEnter([] { Serial.println("  [CM] Standby - display off"); })
    // Guard: only power on if the water tank is filled
    .on(Event::PowerOn, State::Active, [] { return water_ok; });

  // Active is the composite parent for all operational states.
  // PowerOff is defined here once and inherited by every child at any depth.
  hsm.addState(State::Active)
    .initial(State::Idle)
    .onEnter([] { Serial.println("  [CM] Boiler heating..."); })
    .onExit([] { Serial.println("  [CM] Powering off"); })
    .on(Event::PowerOff, State::Standby);

  hsm.addState(State::Idle)
    .parent(State::Active)
    .onEnter([] { Serial.println("  [CM] Ready - select beverage"); })
    .on(Event::Brew, State::Brewing)
    .on(Event::Steam, State::Steaming);

  // Brewing is a nested composite: its own initial child is Grinding.
  hsm.addState(State::Brewing)
    .parent(State::Active)
    .initial(State::Grinding)
    .onEnter([] { Serial.println("  [CM] Brew sequence start"); })
    .onExit([] { Serial.println("  [CM] Brew sequence done"); });

  hsm.addState(State::Grinding)
    .parent(State::Brewing)
    .onEnter([] { Serial.println("  [CM] Grinding beans..."); })
    .on(Event::GrindDone, State::Extracting);

  hsm.addState(State::Extracting)
    .parent(State::Brewing)
    .onEnter([] { Serial.println("  [CM] Extracting espresso..."); })
    .on(Event::BrewDone, State::Idle);

  hsm.addState(State::Steaming)
    .parent(State::Active)
    .onEnter([] { Serial.println("  [CM] Steaming milk..."); })
    .on(Event::SteamDone, State::Idle);

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
  Serial.println("Statechart - Coffee Machine Example");
  Serial.println("-----------------------------------");

  setupHSM();

  // Guard: PowerOn refused without water
  Serial.println("\n-- PowerOn refused (no water in tank) --");
  water_ok = false;
  hsm.dispatch(Event::PowerOn); // guard fails, stays in Standby

  // Full brew cycle
  Serial.println("\n-- Full brew cycle --");
  water_ok = true;
  hsm.dispatch(Event::PowerOn);   // Standby    -> Active -> Idle (initial)
  hsm.dispatch(Event::Brew);      // Idle       -> Brewing -> Grinding (initial)
  hsm.dispatch(Event::GrindDone); // Grinding   -> Extracting
  hsm.dispatch(Event::BrewDone);  // Extracting -> Idle

  // Steam milk
  Serial.println("\n-- Steam milk --");
  hsm.dispatch(Event::Steam);     // Idle     -> Steaming
  hsm.dispatch(Event::SteamDone); // Steaming -> Idle

  // PowerOff from Idle (inherited from Active)
  Serial.println("\n-- PowerOff from Idle (event inherited from Active) --");
  hsm.dispatch(Event::PowerOff); // Idle inherits PowerOff -> Standby

  // PowerOff from deep inside hierarchy
  Serial.println("\n-- PowerOff while grinding (3 levels deep) --");
  hsm.dispatch(Event::PowerOn);   // Standby    -> Active     -> Idle
  hsm.dispatch(Event::Brew);      // Idle       -> Brewing    -> Grinding
  hsm.dispatch(Event::GrindDone); // Grinding   -> Extracting

  // Extracting inherits PowerOff through Brewing -> Active
  hsm.dispatch(Event::PowerOff); // -> Standby
}

void loop() { delay(1000); }