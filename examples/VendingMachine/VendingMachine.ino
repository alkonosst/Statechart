/**
 * SPDX-FileCopyrightText: 2026 Maximiliano Ramirez <maximiliano.ramirezbravo@gmail.com>
 *
 * SPDX-License-Identifier: MIT
 */

/** Explanation of this example:
 * - This example demonstrates a coin-operated vending machine with a 150-cent product price. It
 *   models coin accumulation, product selection with stock check, and cancellation flow.
 * - The example covers the following features:
 *   - on(event, target, action): unconditional transition with a side-effect action.
 *   - on(event, target, guard, action): guarded transition with action.
 *   - Fallthrough guard pattern: multiple transitions registered for the same event on the same
 *     state are evaluated in registration order; the first whose guard passes is taken. A
 *     transition without a guard always passes (unconditional fallback).
 *
 * State hierarchy (flat):
 *   Idle <-> HasCredit -> Dispensing -> Idle
 */

#include <Arduino.h>

#include <Statechart.h>
using namespace Statechart;

// States and events
enum class State : uint8_t { Idle, HasCredit, Dispensing };
enum class Event : uint8_t { InsertCoin, SelectProduct, Cancel, ProductDropped };

// External state
static int credit          = 0;
static bool item_available = true;

const int COIN_VALUE = 50;  // cents
const int PRICE      = 150; // cents

// HSM instance: template parameters are <StateType, EventType, MaxStates, MaxTransitions>
// How to choose MaxStates and MaxTransitions:
//   - MaxStates: total number of states in the machine
//   - MaxTransitions: total number of transitions across all states; every on() call adds one
HSM<State, Event, 3, 7> hsm("VendingMachine", State::Idle);

void setupHSM() {
  // Setup transitions
  hsm
    .addState(State::Idle)
    // Enough credit after this coin: proceed to HasCredit
    .on(
      Event::InsertCoin,
      State::HasCredit,
      [] { return credit + COIN_VALUE >= PRICE; },
      [] {
        credit += COIN_VALUE;
        Serial.printf("  [VM] Coin +%d, credit: %d - sufficient\n", COIN_VALUE, credit);
      })
    // Not enough credit: stay in Idle (self-transition, fallthrough from above)
    .on(Event::InsertCoin, State::Idle, [] {
      credit += COIN_VALUE;
      Serial.printf("  [VM] Coin +%d, credit: %d/%d\n", COIN_VALUE, credit, PRICE);
    });

  hsm.addState(State::HasCredit)
    .onEnter([] { Serial.printf("  [VM] Credit OK (%d cents)\n", credit); })
    // Item available: dispense it
    .on(
      Event::SelectProduct,
      State::Dispensing,
      [] { return item_available; },
      [] {
        credit -= PRICE;
        Serial.println("  [VM] Dispensing product...");
      })
    // Item not available: self-transition (fallthrough from above)
    .on(Event::SelectProduct,
      State::HasCredit,
      [] { Serial.println("  [VM] Out of stock, try another product"); })
    .on(Event::Cancel,
      State::Idle,
      [] {
        Serial.printf("  [VM] Returning %d cents\n", credit);
        credit = 0;
      })
    .on(Event::InsertCoin, State::HasCredit, [] {
      credit += COIN_VALUE;
      Serial.printf("  [VM] Coin +%d, total: %d\n", COIN_VALUE, credit);
    });

  hsm.addState(State::Dispensing)
    .onEnter([] { Serial.println("  [VM] *clunk*"); })
    .on(Event::ProductDropped, State::Idle, [] {
      Serial.println("  [VM] Product delivered");
      if (credit > 0) {
        Serial.printf("  [VM] Change: %d cents\n", credit);
        credit = 0;
      }
    });

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
  Serial.println("Statechart - Vending Machine");
  Serial.println("----------------------------");

  setupHSM();

  // Insert coins one by one until enough
  Serial.println("\n-- Insert coins one by one (price: 150 cents) --");
  credit = 0;
  hsm.dispatch(Event::InsertCoin); // 50  - not enough, self-transition
  hsm.dispatch(Event::InsertCoin); // 100 - not enough, self-transition
  hsm.dispatch(Event::InsertCoin); // 150 - enough -> HasCredit

  // Select and receive product
  Serial.println("\n-- Select product (available) --");
  hsm.dispatch(Event::SelectProduct);  // guard passes -> Dispensing
  hsm.dispatch(Event::ProductDropped); // -> Idle

  // Cancel mid-session
  Serial.println("\n-- Insert coins, then cancel --");
  credit = 0;
  hsm.dispatch(Event::InsertCoin); // 50  -> self
  hsm.dispatch(Event::InsertCoin); // 100 -> self
  hsm.dispatch(Event::InsertCoin); // 150 -> HasCredit
  hsm.dispatch(Event::Cancel);     // returns 150 -> Idle

  // Fallthrough when item is out of stock
  Serial.println("\n-- Select product (out of stock, then restocked) --");
  credit         = 0;
  item_available = false;
  hsm.dispatch(Event::InsertCoin);    // 50  -> self
  hsm.dispatch(Event::InsertCoin);    // 100 -> self
  hsm.dispatch(Event::InsertCoin);    // 150 -> HasCredit
  hsm.dispatch(Event::SelectProduct); // guard fails (no stock) -> self-transition
  item_available = true;
  hsm.dispatch(Event::SelectProduct); // guard passes -> Dispensing
  hsm.dispatch(Event::ProductDropped);
}

void loop() { delay(1000); }
