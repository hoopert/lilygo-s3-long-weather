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

    // UI_SCREEN_* bits. A drawer is off the wayfinder: it has no dot in the
    // page indicator, draws none of its own, and no swipe from a neighbour
    // reaches it - only screens_show_position() does, which the System gate
    // calls after its three taps. Screens that omit the field are ordinary.
    uint8_t  flags;
};

enum : uint8_t {
    UI_SCREEN_DRAWER = 1 << 0,
};

// Register before calling screens_begin(). Positions must be unique and must
// include 0.
void screens_register(const ScreenDef &def);

// Builds every registered screen and shows home (position 0).
void screens_begin();

// Loads the current screen over whatever LVGL is showing - the boot screen,
// which is not part of the strip - with a fade, deleting the old screen once
// it has gone. A no-op if the current screen is already active.
void screens_reveal(uint32_t fade_ms);

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

// Swipe/click disambiguation. A swipe is a swipe and a tap is a tap: once a
// press has produced a gesture, nothing else from that press counts.
// ui_note_gesture() tells LVGL to forget the press (lv_indev_wait_release),
// so no RELEASED or CLICKED reaches the object first touched - which is on
// the screen that has just slid away. ui_gesture_recent() is the belt to
// that brace: true from the gesture until the next touch begins, which the
// touch driver reports through ui_note_press_start().
void ui_note_gesture();
void ui_note_press_start();
bool ui_gesture_recent();

// The page indicator the manager drew on this screen's root, or nullptr before
// screens_begin(). A screen that hides its chrome (Today's Night Mode) fades
// this along with everything else; it must not delete or move it.
lv_obj_t *screens_indicator(lv_obj_t *root);

// Re-runs the active screen's update function. Cheap enough to call on every
// data change; screens are expected to no-op when nothing they show moved.
void screens_update_active();

// Re-runs every screen's update function. Used after a data refresh so a screen
// swiped to later is already current rather than briefly stale.
void screens_update_all();
