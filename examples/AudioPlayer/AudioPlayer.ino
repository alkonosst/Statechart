/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** Explanation of this example:
 * - This example demonstrates an audio player that remembers the active playback mode (Normal,
 *   Shuffle, Repeat) across pause and stop events. Shallow history on the Playing composite state
 *   saves the last active direct child on exit and restores it on the next re-entry, so the user
 *   resumes exactly where they left off without any extra tracking code.
 * - The example covers the following features:
 *   - withHistory() [H shallow]: on re-entry to a composite state, the last active direct child is
 *     restored instead of always starting from initial()
 *   - reset() clears all saved history so the next entry starts from initial()
 *
 * State hierarchy:
 *   Stopped
 *   Playing [H shallow, initial: Normal]
 *     Normal
 *     Shuffle
 *     Repeat
 *   Paused
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t { Stopped, Playing, Normal, Shuffle, Repeat, Paused };
enum class Event : uint8_t { Play, Pause, Stop, NextMode };

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 6, 8> hsm("AudioPlayer", State::Stopped);

void setupHSM() {
  // Setup transitions
  hsm.addState(State::Stopped)
    .onEnter([] { Serial.println("  [AP] Stopped"); })
    .on(Event::Play, State::Playing);

  // withHistory() saves the last active direct child (Normal/Shuffle/Repeat) so that Play after
  // Pause resumes that mode instead of falling back to Normal.
  hsm.addState(State::Playing)
    .initial(State::Normal)
    .withHistory()
    .onEnter([] { Serial.println("  [AP] Playing (enter)"); })
    .onExit([] { Serial.println("  [AP] Playing (exit)"); })
    .on(Event::Pause, State::Paused)
    .on(Event::Stop, State::Stopped);

  hsm.addState(State::Normal)
    .parent(State::Playing)
    .onEnter([] { Serial.println("  [AP] Mode: Normal"); })
    .on(Event::NextMode, State::Shuffle);

  hsm.addState(State::Shuffle)
    .parent(State::Playing)
    .onEnter([] { Serial.println("  [AP] Mode: Shuffle"); })
    .on(Event::NextMode, State::Repeat);

  hsm.addState(State::Repeat)
    .parent(State::Playing)
    .onEnter([] { Serial.println("  [AP] Mode: Repeat"); })
    .on(Event::NextMode, State::Normal);

  hsm.addState(State::Paused)
    .onEnter([] { Serial.println("  [AP] Paused"); })
    .on(Event::Play, State::Playing)
    .on(Event::Stop, State::Stopped);

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
  Serial.println("Statechart - Audio Player");
  Serial.println("-------------------------");

  setupHSM();

  // Play and switch to Shuffle
  Serial.println("\n-- Play and switch to Shuffle --");
  hsm.dispatch(Event::Play);     // Stopped -> Playing -> Normal (initial)
  hsm.dispatch(Event::NextMode); // Normal  -> Shuffle

  // Pause and resume: history restores Shuffle
  Serial.println("\n-- Pause and resume (history should restore Shuffle) --");
  hsm.dispatch(Event::Pause); // Paused (Playing saves history: Shuffle)
  hsm.dispatch(Event::Play);  // Playing -> Shuffle (history!)

  // Switch to Repeat, stop and play: history still active
  Serial.println("\n-- Switch to Repeat, stop and play again --");
  hsm.dispatch(Event::NextMode); // Shuffle -> Repeat
  hsm.dispatch(Event::Stop);     // Stopped (Playing saves history: Repeat)
  hsm.dispatch(Event::Play);     // Playing -> Repeat (history!)

  // reset() clears all history
  Serial.println("\n-- reset() clears history: next Play starts from Normal --");
  hsm.dispatch(Event::Stop); // Stopped
  hsm.reset();               // exits Stopped, clears all history, re-enters Stopped
  hsm.dispatch(Event::Play); // Playing -> Normal (initial, history gone)
}

void loop() { delay(1000); }