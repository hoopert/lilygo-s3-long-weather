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

// ---------------------------------------------------------------------------
// Interaction contexts
//
// Every place a finger can land - a screen, or an overlay on top of one - is a
// context, and a context says which swipes it accepts. A swipe the active
// context does not list is dropped, not passed down to whatever is beneath.
// That one rule is what stops a drag along the brightness slider from also
// changing screens, and what keeps a two-screen strip from wrapping round on
// itself in both directions.
//
// Screens declare their policy in their ScreenDef; overlays declare theirs in
// overlays_swipes(). ui_handle_swipe() is the single dispatcher both the
// screen gesture handler and the overlay layer's handler feed into.
// ---------------------------------------------------------------------------
enum : uint8_t {
    UI_SWIPE_LEFT  = 1 << 0,   // finger moved right-to-left
    UI_SWIPE_RIGHT = 1 << 1,   // finger moved left-to-right
    UI_SWIPE_UP    = 1 << 2,
    UI_SWIPE_DOWN  = 1 << 3,
    UI_SWIPE_NONE  = 0,
};

struct ScreenDef {
    const char *name;
    const char *icon;              // glyph from icons.h, shown in the indicator
    lv_obj_t *(*create)(lv_obj_t *parent);
    void      (*update)(lv_obj_t *root);

    // Where the screen sits on the strip. 0 is home. Negative positions are
    // drawers to the LEFT of home, revealed by swiping right (finger moving
    // left-to-right) and put away by swiping left; positive positions are the
    // mirror image. Farther from home stacks on top: a screen slides in OVER
    // its neighbour on the way out from home and slides OUT to reveal it on
    // the way back, so the strip reads as a stack of cards rather than a
    // carousel, and nothing wraps.
    int8_t   position;

    // UI_SWIPE_* mask this screen accepts. Left/right also need a neighbour
    // at position∓1 to do anything; down opens Quick Settings.
    uint8_t  swipes;
};

// Register before calling screens_begin(). Positions must be unique and must
// include 0.
void screens_register(const ScreenDef &def);

// Builds every registered screen and shows home (position 0).
void screens_begin();

// Navigation is by position, not registry index, so a screen only ever moves
// to a neighbour it actually has. Unknown positions are ignored.
void screens_show_position(int position, bool animate);
void screens_show(int index, bool animate);   // by registry index
void screens_next();                          // position + 1, if it exists
void screens_prev();                          // position - 1, if it exists

int  screens_count();
int  screens_current();            // registry index
int  screens_current_position();

// The dispatcher. Consults the active overlay first, then the current screen,
// and drops anything the active context did not ask for.
void ui_handle_swipe(lv_dir_t dir);

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
