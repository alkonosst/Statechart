/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/**
 * Audio Player Native Example Overview:
 * - Mirrors the Audio Player Arduino example, swapping Serial for printf. Useful for native builds.
 * - Build and run locally:
 *   PowerShell: $env:EXAMPLE="examples/AudioPlayerNative"; pio run -e native-example -t exec
 *   bash/WSL  : export EXAMPLE="examples/AudioPlayerNative"; pio run -e native-example -t exec
 */

#include <cstdio>

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

bool setupHSM() {
  // Setup transitions
  hsm.addState(State::Stopped)
    .onEnter([] { printf("  [AP] Stopped\n"); })
    .on(Event::Play, State::Playing);

  // withHistory() saves the last active direct child (Normal/Shuffle/Repeat) so that Play after
  // Pause resumes that mode instead of falling back to Normal.
  hsm.addState(State::Playing)
    .initial(State::Normal)
    .withHistory()
    .onEnter([] { printf("  [AP] Playing (enter)\n"); })
    .onExit([] { printf("  [AP] Playing (exit)\n"); })
    .on(Event::Pause, State::Paused)
    .on(Event::Stop, State::Stopped);

  hsm.addState(State::Normal)
    .parent(State::Playing)
    .onEnter([] { printf("  [AP] Mode: Normal\n"); })
    .on(Event::NextMode, State::Shuffle);

  hsm.addState(State::Shuffle)
    .parent(State::Playing)
    .onEnter([] { printf("  [AP] Mode: Shuffle\n"); })
    .on(Event::NextMode, State::Repeat);

  hsm.addState(State::Repeat)
    .parent(State::Playing)
    .onEnter([] { printf("  [AP] Mode: Repeat\n"); })
    .on(Event::NextMode, State::Normal);

  hsm.addState(State::Paused)
    .onEnter([] { printf("  [AP] Paused\n"); })
    .on(Event::Play, State::Playing)
    .on(Event::Stop, State::Stopped);

  // Validate the machine configuration before starting. Checks for construction overflow (too many
  // states or transitions) and other common mistakes. Always call this before start() to catch
  // errors early.
  if (!hsm.isValid()) {
    printf("HSM sizing invalid!\n");
    return false;
  }

  // Start the machine: runs the entry chain for the initial state and marks the machine as started.
  hsm.start();

  // Debug dump shows the full state and transition configuration in a human-readable format.
  // Needs `STATECHART_LOG_LEVEL >= Info` to print.
  hsm.debugDump();

  return true;
}

int main() {
  printf("----------------------------------------\n");
  printf("Statechart - Audio Player Native Example\n");
  printf("----------------------------------------\n");

  if (!setupHSM()) return 1;

  // Play and switch to Shuffle
  printf("\n-- Play and switch to Shuffle --\n");
  hsm.dispatch(Event::Play);     // Stopped -> Playing -> Normal (initial)
  hsm.dispatch(Event::NextMode); // Normal  -> Shuffle

  // Pause and resume: history restores Shuffle
  printf("\n-- Pause and resume (history should restore Shuffle) --\n");
  hsm.dispatch(Event::Pause); // Paused (Playing saves history: Shuffle)
  hsm.dispatch(Event::Play);  // Playing -> Shuffle (history!)

  // Switch to Repeat, stop and play: history still active
  printf("\n-- Switch to Repeat, stop and play again --\n");
  hsm.dispatch(Event::NextMode); // Shuffle -> Repeat
  hsm.dispatch(Event::Stop);     // Stopped (Playing saves history: Repeat)
  hsm.dispatch(Event::Play);     // Playing -> Repeat (history!)

  // reset() clears all history
  printf("\n-- reset() clears history: next Play starts from Normal --\n");
  hsm.dispatch(Event::Stop); // Stopped
  hsm.reset();               // exits Stopped, clears all history, re-enters Stopped
  hsm.dispatch(Event::Play); // Playing -> Normal (initial, history gone)

  return 0;
}