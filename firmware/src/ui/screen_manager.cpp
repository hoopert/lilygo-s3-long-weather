#include "screen_manager.h"

#include <Arduino.h>

#include "config.h"
#include "display/backlight.h"
#include "theme.h"
#include "ui/overlays.h"

namespace {

constexpr int kMaxScreens = 8;

ScreenDef s_defs[kMaxScreens];
lv_obj_t *s_roots[kMaxScreens] = {nullptr};
lv_obj_t *s_indicators[kMaxScreens] = {nullptr};
int s_count = 0;
int s_current = 0;
bool s_started = false;

bool s_gesture_this_press = false;

void gesture_cb(lv_event_t *e) {
    LV_UNUSED(e);
    ui_handle_swipe(lv_indev_get_gesture_dir(lv_indev_get_act()));
}

uint8_t swipe_bit(lv_dir_t dir) {
    switch (dir) {
        case LV_DIR_LEFT:   return UI_SWIPE_LEFT;
        case LV_DIR_RIGHT:  return UI_SWIPE_RIGHT;
        case LV_DIR_TOP:    return UI_SWIPE_UP;
        case LV_DIR_BOTTOM: return UI_SWIPE_DOWN;
        default:            return UI_SWIPE_NONE;
    }
}

int index_of_position(int position) {
    for (int i = 0; i < s_count; i++) {
        if (s_defs[i].position == position) return i;
    }
    return -1;
}

bool is_drawer(int index) {
    return index >= 0 && (s_defs[index].flags & UI_SCREEN_DRAWER);
}

// The page indicator: one dot per screen in strip order (by position), the
// active one drawn as a short turquoise bar rather than a larger dot, so it
// reads as a place on the strip rather than as a button you might press.
// Drawers are not places on the strip: they get no dot and no indicator.
void build_indicator(lv_obj_t *screen, int index) {
    if (is_drawer(index)) {
        s_indicators[index] = nullptr;
        return;
    }
    lv_obj_t *row = theme_decor(screen);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 6);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -3);

    // Walk positions from leftmost to rightmost rather than registry order.
    int lo = 127, hi = -128;
    for (int i = 0; i < s_count; i++) {
        if (s_defs[i].position < lo) lo = s_defs[i].position;
        if (s_defs[i].position > hi) hi = s_defs[i].position;
    }
    for (int p = lo; p <= hi; p++) {
        const int i = index_of_position(p);
        if (i < 0 || is_drawer(i)) continue;
        lv_obj_t *dot = theme_decor(row);
        const bool active = (i == index);
        lv_obj_set_size(dot, active ? 12 : 4, 3);
        lv_obj_set_style_radius(dot, 2, 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(
            dot, lv_color_hex(active ? COL_TURQUOISE : COL_RIVET), 0);
    }
    s_indicators[index] = row;
}

}  // namespace

void ui_note_gesture() {
    s_gesture_this_press = true;

    // The rest of this press belongs to the swipe. LVGL would otherwise send
    // RELEASED and CLICKED to whatever was under the finger when it first
    // touched down - on the screen that has since slid away - however long
    // the finger stays down after the swipe. wait_release makes it forget
    // the press instead: nothing more is delivered until the next touch.
    lv_indev_t *indev = lv_indev_get_act();
    if (indev) {
        lv_obj_t *act = lv_indev_get_obj_act();
        if (act) lv_obj_clear_state(act, LV_STATE_PRESSED);
        lv_indev_wait_release(indev);
    }
}

void ui_note_press_start() { s_gesture_this_press = false; }

bool ui_gesture_recent() { return s_gesture_this_press; }

void screens_register(const ScreenDef &def) {
    if (s_count >= kMaxScreens) {
        Serial.println("[ui] screen registry full");
        return;
    }
    if (index_of_position(def.position) >= 0) {
        Serial.printf("[ui] screen \"%s\" duplicates position %d; not registered\n",
                      def.name, int(def.position));
        return;
    }
    s_defs[s_count++] = def;
}

void ui_handle_swipe(lv_dir_t dir) {
    backlight_note_activity();
    ui_note_gesture();

    const uint8_t bit = swipe_bit(dir);
    if (bit == UI_SWIPE_NONE) return;

    // An open overlay is the active context. It gets the swipe or nothing
    // does - never the screen beneath it.
    if (overlays_active()) {
        if (overlays_swipes() & bit) overlays_dismiss();
        return;
    }

    if (!s_started) return;
    const ScreenDef &cur = s_defs[s_current];
    if (!(cur.swipes & bit)) return;

    // A swipe never lands in a drawer; see UI_SCREEN_DRAWER.
    auto step = [&](int position) {
        if (!is_drawer(index_of_position(position))) screens_show_position(position, true);
    };
    switch (dir) {
        case LV_DIR_RIGHT:  step(cur.position - 1); break;
        case LV_DIR_LEFT:   step(cur.position + 1); break;
        case LV_DIR_BOTTOM: overlays_show_quick_settings(); break;
        default: break;
    }
}

void screens_begin() {
    for (int i = 0; i < s_count; i++) {
        lv_obj_t *scr = lv_obj_create(nullptr);
        lv_obj_add_style(scr, &style_screen, 0);
        lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

        lv_obj_add_event_cb(scr, gesture_cb, LV_EVENT_GESTURE, nullptr);

        if (s_defs[i].create) s_defs[i].create(scr);
        build_indicator(scr, i);

        s_roots[i] = scr;
    }

    s_started = true;
    const int home = index_of_position(0);
    if (home < 0) Serial.println("[ui] no screen at position 0; showing the first registered");
    s_current = home < 0 ? 0 : home;
    if (s_roots[s_current]) lv_scr_load(s_roots[s_current]);
    screens_update_all();
}

void screens_reveal(uint32_t fade_ms) {
    if (!s_started || s_roots[s_current] == nullptr) return;
    if (lv_scr_act() == s_roots[s_current]) return;
    lv_scr_load_anim(s_roots[s_current], LV_SCR_LOAD_ANIM_FADE_IN, fade_ms, 0, true);
}

void screens_show(int index, bool animate) {
    if (!s_started || index < 0 || index >= s_count || index == s_current) return;

    // Stack order is distance from home. Moving outward, the new screen
    // slides in OVER the current one from its side of the strip; moving back
    // toward home, the current screen slides OUT the way it came, revealing
    // what was beneath. Same rule for every pair, so a future third screen
    // needs no special case.
    const int from = s_defs[s_current].position;
    const int to   = s_defs[index].position;
    const bool outward = abs(to) > abs(from);
    lv_scr_load_anim_t anim = LV_SCR_LOAD_ANIM_NONE;
    if (animate) {
        if (outward) {
            anim = (to < from) ? LV_SCR_LOAD_ANIM_OVER_RIGHT   // new enters from the left
                               : LV_SCR_LOAD_ANIM_OVER_LEFT;   // new enters from the right
        } else {
            anim = (to > from) ? LV_SCR_LOAD_ANIM_OUT_LEFT     // current leaves to the left
                               : LV_SCR_LOAD_ANIM_OUT_RIGHT;   // current leaves to the right
        }
    }

    s_current = index;
    if (s_defs[index].update) s_defs[index].update(s_roots[index]);
    lv_scr_load_anim(s_roots[index], anim, animate ? UI_SCREEN_ANIM_MS : 0, 0, false);
}

void screens_show_position(int position, bool animate) {
    const int i = index_of_position(position);
    if (i >= 0) screens_show(i, animate);
}

void screens_next() { if (s_started) screens_show_position(s_defs[s_current].position + 1, true); }
void screens_prev() { if (s_started) screens_show_position(s_defs[s_current].position - 1, true); }

int screens_count()   { return s_count; }
int screens_current() { return s_current; }
int screens_current_position() { return s_started ? s_defs[s_current].position : 0; }

lv_obj_t *screens_indicator(lv_obj_t *root) {
    for (int i = 0; i < s_count; i++) {
        if (s_roots[i] == root) return s_indicators[i];
    }
    return nullptr;
}

void screens_update_active() {
    if (!s_started) return;
    if (s_defs[s_current].update) s_defs[s_current].update(s_roots[s_current]);
}

void screens_update_all() {
    if (!s_started) return;
    for (int i = 0; i < s_count; i++) {
        if (s_defs[i].update) s_defs[i].update(s_roots[i]);
    }
}
