// The multi-screen framework.
//
// Today ships two screens, but the panel is destined for a trailer where it
// will eventually also show tank levels, battery state and inside/outside
// temperatures. So screens are a registry rather than a hardcoded pair: a new
// one is a struct, a create function, an update function, and one call to
// screens_register(). Navigation, transitions, the page indicator and the
// gesture plumbing all come for free.
//
// See docs/ARCHITECTURE.md for a worked example of adding a screen.
#pragma once

#include <lvgl.h>

struct ScreenDef {
    const char *name;
    const char *icon;              // glyph from icons.h, shown in the indicator
    lv_obj_t *(*create)(lv_obj_t *parent);
    void      (*update)(lv_obj_t *root);
};

// Register before calling screens_begin(). Order is left-to-right swipe order.
void screens_register(const ScreenDef &def);

// Builds every registered screen and shows the first.
void screens_begin();

void screens_show(int index, bool animate);
void screens_next();
void screens_prev();

int  screens_count();
int  screens_current();

// Swipe/click disambiguation. LVGL delivers LV_EVENT_CLICKED on release even
// when the same press already produced a gesture, so every gesture handler
// calls ui_note_gesture() and every click handler ignores the click while
// ui_gesture_recent() is true. Without this, swiping left across the hourly
// strip changes screen and opens an hour detail overlay at the same time.
void ui_note_gesture();
bool ui_gesture_recent();

// Re-runs the active screen's update function. Cheap enough to call on every
// data change; screens are expected to no-op when nothing they show moved.
void screens_update_active();

// Re-runs every screen's update function. Used after a data refresh so a screen
// swiped to later is already current rather than briefly stale.
void screens_update_all();
