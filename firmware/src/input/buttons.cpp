#include "buttons.h"

#include <Arduino.h>

#include "config.h"

namespace {

ButtonCallback s_on_short  = nullptr;
ButtonCallback s_on_double = nullptr;
ButtonCallback s_on_long   = nullptr;

bool     s_stable_down = false;      // debounced state
bool     s_raw_last    = false;
uint32_t s_raw_change_ms = 0;

uint32_t s_press_start_ms = 0;
bool     s_long_fired = false;

// A completed short press parks here while we wait to see whether a second one
// arrives and turns it into a double.
bool     s_awaiting_double = false;
uint32_t s_first_release_ms = 0;

void fire(ButtonCallback cb) { if (cb) cb(); }

}  // namespace

void buttons_init() {
    pinMode(PIN_BUTTON_BOOT, INPUT_PULLUP);
    s_raw_last = s_stable_down = false;
    s_raw_change_ms = millis();
}

void buttons_on_short(ButtonCallback cb)  { s_on_short = cb; }
void buttons_on_double(ButtonCallback cb) { s_on_double = cb; }
void buttons_on_long(ButtonCallback cb)   { s_on_long = cb; }

void buttons_tick() {
    const uint32_t now = millis();
    const bool raw = digitalRead(PIN_BUTTON_BOOT) == LOW;   // active low

    if (raw != s_raw_last) {
        s_raw_last = raw;
        s_raw_change_ms = now;
    }

    // Accept the raw level only once it has held still for the debounce window.
    if (raw != s_stable_down && (now - s_raw_change_ms) >= BTN_DEBOUNCE_MS) {
        s_stable_down = raw;

        if (s_stable_down) {
            s_press_start_ms = now;
            s_long_fired = false;

            if (s_awaiting_double) {
                s_awaiting_double = false;
                fire(s_on_double);
                // Swallow this press entirely - it was the second half of a
                // double, not the start of something new.
                s_press_start_ms = 0;
            }
        } else if (s_press_start_ms != 0 && !s_long_fired) {
            s_awaiting_double = true;
            s_first_release_ms = now;
        }
    }

    // Long press fires while still held.
    if (s_stable_down && !s_long_fired && s_press_start_ms != 0 &&
        (now - s_press_start_ms) >= BTN_LONG_PRESS_MS) {
        s_long_fired = true;
        s_awaiting_double = false;
        fire(s_on_long);
    }

    // No second press arrived in time, so the first one really was a single.
    if (s_awaiting_double && (now - s_first_release_ms) >= BTN_DOUBLE_GAP_MS) {
        s_awaiting_double = false;
        fire(s_on_short);
    }
}
