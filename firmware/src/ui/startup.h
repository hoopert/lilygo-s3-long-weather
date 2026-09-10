// The boot and setup screens (design/SPEC.md §6 and §7).
//
// From the first frame until the first forecast lands the panel is on one of
// these rather than on an empty Today: a boot screen that says what it is
// waiting for, or, when there is no Wi-Fi to join, a setup screen with the
// network's name, its password and a code a phone camera can join from.
// Never a blank screen, never a wordless spinner.
//
// The startup screen lives outside the swipe strip: it is not registered
// with the screen manager, accepts no gestures, and is deleted when it hands
// off to Today. Nothing here is skippable because there is nothing to skip to.
#pragma once

// Creates the boot screen and loads it. Call after screens_begin() has built
// the strip, and before the first lv_timer_handler().
void startup_show();

// Drives the status text and the hand-off. Call every loop iteration.
void startup_tick();

bool startup_active();
