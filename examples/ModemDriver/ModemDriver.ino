/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** Explanation of this example:
 * - This example demonstrates a cellular modem driver with a multi-level state hierarchy. The modem
 *   powers on, registers on the network, connects, and sends data with automatic retry. Events
 *   defined on composite parent states are inherited by all children, so Reset and SimFail only
 *   need to be configured once on Online and they apply everywhere inside it.
 * - The example covers the following features:
 *   - Composite states with parent(), initial(), and event inheritance.
 *   - withHistory() [H shallow] on Connected: resumes Sending if that was the active child.
 *   - Guards with external state (battery level, retry count).
 *   - Multiple transitions for the same event with guards (guard fallthrough pattern).
 *   - onEnter / onExit per state.
 *
 * State hierarchy:
 *   PowerOff
 *   Booting
 *   Online  [initial: Idle]
 *     Idle
 *     Registering
 *     Connected  [H shallow, initial: IdleConn]
 *       IdleConn
 *       Sending
 *     Error
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t {
  PowerOff,
  Booting,
  Online,
  Idle,
  Registering,
  Connected,
  IdleConn,
  Sending,
  Error,
};

enum class Event : uint8_t {
  PowerOn,
  Ready,
  Connect,
  RegOk,
  Send,
  Ack,
  Fail,
  Lost,
  Reset,
  SimFail,
};

// External state
static int battery_level = 80;
static int retry_count   = 0;

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 9, 14> hsm("ModemDriver", State::PowerOff);

void setupHSM() {
  // Setup transitions
  hsm.addState(State::PowerOff)
    .onEnter([] { Serial.println("  [Modem] Power off"); })
    .on(Event::PowerOn, State::Booting, [] { Serial.println("  [Modem] Booting..."); });

  hsm.addState(State::Booting)
    .onEnter([] {
      Serial.println("  [Modem] Boot sequence running");
      retry_count = 0;
    })
    .on(Event::Ready, State::Online)
    .on(Event::Fail, State::PowerOff, [] { Serial.println("  [Modem] Boot failed"); });

  // Online is the composite parent for all network states.
  // Reset and SimFail are defined here once and inherited by every child at any depth.
  hsm.addState(State::Online)
    .initial(State::Idle)
    .onEnter([] { Serial.println("  [Modem] Online (enter)"); })
    .onExit([] { Serial.println("  [Modem] Online (exit)"); })
    .on(Event::Reset, State::PowerOff, [] { Serial.println("  [Modem] RESET -> power off"); })
    .on(Event::SimFail, State::PowerOff, [] {
      Serial.println("  [Modem] SIM invalid -> power off");
    });

  hsm.addState(State::Idle)
    .parent(State::Online)
    .onEnter([] { Serial.println("  [Modem] Idle"); })
    .on(
      Event::Connect,
      State::Registering,
      [] { return battery_level > 20; }, // guard: enough battery
      [] { Serial.println("  [Modem] Starting network registration"); });

  hsm.addState(State::Registering)
    .parent(State::Online)
    .onEnter([] { Serial.println("  [Modem] Registering on network..."); })
    .on(Event::RegOk, State::Connected)
    .on(Event::Fail, State::Idle, [] { Serial.println("  [Modem] Registration failed"); });

  // withHistory() saves the last active direct child (IdleConn/Sending) so that if the signal
  // is lost and then regained, the machine resumes where it was instead of starting from IdleConn.
  hsm.addState(State::Connected)
    .parent(State::Online)
    .initial(State::IdleConn)
    .withHistory()
    .onEnter([] { Serial.println("  [Modem] Connected (enter)"); })
    .onExit([] { Serial.println("  [Modem] Connected (exit)"); })
    .on(Event::Lost, State::Registering, [] { Serial.println("  [Modem] Signal lost"); });

  hsm.addState(State::IdleConn)
    .parent(State::Connected)
    .onEnter([] { Serial.println("  [Modem] Connection idle, ready to send"); })
    .on(Event::Send, State::Sending);

  // Multiple transitions for Fail: the first tries a retry (guard), then falls through to Error.
  hsm.addState(State::Sending)
    .parent(State::Connected)
    .onEnter([] { Serial.println("  [Modem] Sending data..."); })
    .onExit([] { Serial.println("  [Modem] Send done"); })
    .on(Event::Ack,
      State::IdleConn,
      [] {
        Serial.println("  [Modem] ACK ok");
        retry_count = 0;
      })
    .on(
      Event::Fail,
      State::IdleConn,
      [] { return ++retry_count <= 3; }, // guard: allow up to 3 retries
      [] { Serial.printf("  [Modem] Retry %d/3\n", retry_count); })
    .on(Event::Fail, State::Error, [] {
      Serial.println("  [Modem] Max retries reached -> ERROR");
    });

  hsm.addState(State::Error)
    .parent(State::Online)
    .onEnter([] { Serial.println("  [Modem] *** ERROR ***"); })
    .on(Event::Reset, State::PowerOff);

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

  Serial.println("-------------------------");
  Serial.println("Statechart - Modem Driver");
  Serial.println("-------------------------");

  setupHSM();

  // Normal sequence
  Serial.println("\n-- Normal sequence --");
  hsm.dispatch(Event::PowerOn); // PowerOff    -> Booting
  hsm.dispatch(Event::Ready);   // Booting     -> Online -> Idle (initial)
  hsm.dispatch(Event::Connect); // Idle        -> Registering
  hsm.dispatch(Event::RegOk);   // Registering -> Connected -> IdleConn (initial)
  hsm.dispatch(Event::Send);    // IdleConn    -> Sending
  hsm.dispatch(Event::Ack);     // Sending     -> IdleConn

  // Reset inherited from Online (while in IdleConn)
  Serial.println("\n-- Reset inherited from Online (while in IdleConn) --");
  hsm.dispatch(Event::Reset); // IdleConn inherits Reset from Online -> PowerOff

  // Retry guard fallthrough, then Error
  Serial.println("\n-- Retry guard fallthrough, then Error --");
  hsm.dispatch(Event::PowerOn); // PowerOff    -> Booting
  hsm.dispatch(Event::Ready);   // Booting     -> Online -> Idle
  hsm.dispatch(Event::Connect); // Idle        -> Registering
  hsm.dispatch(Event::RegOk);   // Registering -> Connected -> IdleConn
  hsm.dispatch(Event::Send);    // IdleConn    -> Sending
  hsm.dispatch(Event::Fail);    // retry 1     -> IdleConn
  hsm.dispatch(Event::Send);    // IdleConn    -> Sending
  hsm.dispatch(Event::Fail);    // retry 2     -> IdleConn
  hsm.dispatch(Event::Send);    // IdleConn    -> Sending
  hsm.dispatch(Event::Fail);    // retry 3     -> IdleConn
  hsm.dispatch(Event::Send);    // IdleConn    -> Sending
  hsm.dispatch(Event::Fail);    // guard fails (count=4) -> Error
}

void loop() { delay(1000); }