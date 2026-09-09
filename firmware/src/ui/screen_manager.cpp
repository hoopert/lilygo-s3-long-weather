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

uint32_t s_last_gesture_ms = 0;

void gesture_cb(lv_event_t *e) {
    LV_UNUSED(e);
    backlight_note_activity();
    ui_note_gesture();

    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_get_act());

    // An open overlay owns vertical gestures - dismissing it has to be the
    // first thing a downward swipe does, or the quick-settings sheet would
    // reopen the moment you tried to close it.
    if (overlays_active()) {
        if (dir == LV_DIR_BOTTOM || dir == LV_DIR_TOP) overlays_dismiss();
        return;
    }

    switch (dir) {
        case LV_DIR_LEFT:   screens_next(); break;
        case LV_DIR_RIGHT:  screens_prev(); break;
        case LV_DIR_BOTTOM: overlays_show_quick_settings(); break;
        default: break;
    }
}

// The page indicator: one dot per screen, the active one drawn as a short
// turquoise bar rather than a larger dot, so it reads as a position in a strip
// rather than as a button you might be able to press.
void build_indicator(lv_obj_t *screen, int index) {
    lv_obj_t *row = theme_decor(screen);
    lv_obj_set_size(row, LV_SIZE_CONTENT, 6);
    lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(row, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(row, 4, 0);
    lv_obj_align(row, LV_ALIGN_BOTTOM_MID, 0, -3);

    for (int i = 0; i < s_count; i++) {
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

void ui_note_gesture() { s_last_gesture_ms = millis(); }

bool ui_gesture_recent() {
    return (millis() - s_last_gesture_ms) < GESTURE_CLICK_SUPPRESS_MS;
}

void screens_register(const ScreenDef &def) {
    if (s_count >= kMaxScreens) {
        Serial.println("[ui] screen registry full");
        return;
    }
    s_defs[s_count++] = def;
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
    s_current = 0;
    if (s_roots[0]) lv_scr_load(s_roots[0]);
    screens_update_all();
}

void screens_show(int index, bool animate) {
    if (!s_started || s_count == 0) return;
    if (index < 0) index = s_count - 1;
    if (index >= s_count) index = 0;
    if (index == s_current) return;

    // Slide in the direction of travel, wrapping the short way round so a
    // two-screen panel does not appear to rewind when it wraps.
    const bool forward = (index == (s_current + 1) % s_count);
    const lv_scr_load_anim_t anim =
        !animate ? LV_SCR_LOAD_ANIM_NONE
                 : (forward ? LV_SCR_LOAD_ANIM_MOVE_LEFT : LV_SCR_LOAD_ANIM_MOVE_RIGHT);

    s_current = index;
    if (s_defs[index].update) s_defs[index].update(s_roots[index]);
    lv_scr_load_anim(s_roots[index], anim, animate ? UI_SCREEN_ANIM_MS : 0, 0, false);
}

void screens_next() { screens_show((s_current + 1) % (s_count ? s_count : 1), true); }
void screens_prev() { screens_show((s_current - 1 + s_count) % (s_count ? s_count : 1), true); }

int screens_count()   { return s_count; }
int screens_current() { return s_current; }

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
