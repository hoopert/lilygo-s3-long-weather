// Detail overlays: the "tap to go deeper" half of the gesture map.
//
// A 640x180 strip cannot show everything at once and should not try. The Today
// screen shows what you want at a glance from across the trailer; these
// overlays carry everything else, one tap away, and they live on LVGL's top
// layer so they survive a screen change and never have to be duplicated per
// screen.
//
// Dismiss is deliberately over-served on the read-only overlays - tap
// anywhere, swipe up, or swipe down - because the one thing that makes a touch
// panel feel broken is being stuck in a view you cannot back out of. The
// quick-settings sheet is the exception: it holds a horizontal slider, so it
// accepts only swipe-up, and a drag along the slider stays a drag.
#pragma once

#include <lvgl.h>

void overlays_init();

void overlays_show_hour(int hour_index);
void overlays_show_now();
void overlays_show_quick_settings();

void overlays_dismiss();
bool overlays_active();

// UI_SWIPE_* mask the open overlay accepts (all of them dismiss it). Zero when
// nothing is open. See screen_manager.h, "Interaction contexts".
uint8_t overlays_swipes();

// Refresh live values in whichever overlay is open (brightness, time since
// last update). Cheap; call from the UI tick.
void overlays_tick();
