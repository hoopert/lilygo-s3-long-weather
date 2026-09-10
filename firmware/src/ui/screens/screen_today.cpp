#include "screen_today.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "display/backlight.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "ui/format.h"
#include "ui/icons.h"
#include "ui/overlays.h"
#include "ui/screen_manager.h"
#include "ui/theme.h"

// The home screen: the Now zone on the left (design/SPEC.md §1) and a strip
// of eight hours on the right, each just an hour, a glyph and a temperature -
// tap one for everything else. The design's denser strip (ribbon, rain bars,
// wind arrows across ten columns) was built and then judged too crammed on
// the glass; Hour Detail carries what came off it.
//
// Night Mode (§5) is a second layout on the same screen: two full-size
// transparent layers hold the widgets that belong to only one of the
// layouts; the hero temperature, its marks and the condition line sit on
// the root and recolour instead. Crossing between them is one animation.

namespace {

// --- one hour column, centred in the strip's height ---------------------------
constexpr int kHourY   = 24;    // hour label, Micro
constexpr int kIconY   = 46;    // 32px glyph
constexpr int kTempY   = 90;    // Title 30
constexpr int kSlabY   = 12;    // the NOW slab
constexpr int kSlabH   = 132;
constexpr int kSepY    = 30;    // column hairlines
constexpr int kSepH    = 96;

// --- the "Now" zone --------------------------------------------------------
//
// The hero temperature is 72px and the degree mark is a separate label aligned
// to its right, rather than part of the same string. That is partly because the
// design wants the mark set against the cap height rather than the baseline,
// and partly because it is what keeps "100" from running into the condition
// icon: the icon sits at a fixed x, and a three-digit hero would collide with
// it if the degree sign were baked into the same auto-sized label. The unit
// letter hangs off the degree mark in turn, smaller and dimmer.
constexpr int kHeroY      = 22;
constexpr int kDegreeDY   = 12;    // hero top -> degree top (y34, cap-aligned)
constexpr int kUnitDY     = 18;    // degree top -> unit top (y52)
constexpr int kNowIconX   = 146;
constexpr int kNowIconY   = 10;
constexpr int kConditionY = 98;
constexpr int kMetaY      = 124;
constexpr int kPlaceY     = 146;
constexpr int kPlaceW     = 190;   // the place line must fit x10-x200

// --- the seam (SPEC §1: "the one ornament") --------------------------------
constexpr int kSeamX       = LAYOUT_STRIP_X - 1;   // 207
constexpr int kSeamY       = 12;
constexpr int kSeamH       = 148;                  // to y160
constexpr int kRivetX      = LAYOUT_STRIP_X - 2;   // 206, a 2px dot astride the line
constexpr int kRivetY0     = 16;
constexpr int kRivetPitch  = 16;
constexpr int kRivetCount  = 10;                   // y16 .. y160

// --- Night Mode (SPEC §5) --------------------------------------------------
constexpr int kNightHourY = 44;
constexpr int kNightTempY = 62;

struct HourWidgets {
    lv_obj_t *cell;        // invisible hit target, carries the index
    lv_obj_t *bg;          // "now" slab
    lv_obj_t *hour;
    lv_obj_t *icon;
    lv_obj_t *temp;
};

struct NightWidgets {
    lv_obj_t *hour;
    lv_obj_t *temp;
};

struct TodayUi {
    // Layers. Everything only the day layout shows is a child of `day`;
    // everything only the night layout shows is a child of `night`.
    lv_obj_t *day;
    lv_obj_t *night;

    // Root: present in both layouts, recoloured between them.
    lv_obj_t *hero;
    lv_obj_t *hero_unit;     // the degree mark
    lv_obj_t *hero_suffix;   // F or C
    lv_obj_t *condition;
    lv_obj_t *seam;
    lv_obj_t *rivets[kRivetCount];

    // Day layer.
    lv_obj_t *now_icon;
    lv_obj_t *meta;
    lv_obj_t *place;
    lv_obj_t *status;
    HourWidgets hours[WX_HOURLY_SLOTS];

    // Night layer.
    lv_obj_t *night_clock;
    NightWidgets night_hours[LAYOUT_NIGHT_COLS];

    // Cross-fade state. night_mix is 0 in daylight, 255 in Night Mode.
    lv_obj_t *indicator;     // the manager's page indicator, found lazily
    bool      night_target;
    uint8_t   night_mix;
};

TodayUi s_ui = {};

int column_x(int i)       { return LAYOUT_COLUMNS_X + i * LAYOUT_HOUR_COL_W; }
int night_column_x(int j) { return LAYOUT_COLUMNS_X + j * LAYOUT_NIGHT_COL_W; }

void hour_clicked(lv_event_t *e) {
    if (ui_gesture_recent()) return;   // this was the tail of a swipe
    const int index = int(intptr_t(lv_event_get_user_data(e)));
    backlight_note_activity();
    overlays_show_hour(index);
}

void now_clicked(lv_event_t *e) {
    LV_UNUSED(e);
    if (ui_gesture_recent()) return;
    backlight_note_activity();
    overlays_show_now();
}

void long_pressed(lv_event_t *e) {
    LV_UNUSED(e);
    // Press and hold anywhere to force a refresh. It is the gesture people
    // reach for when a panel looks stale, so it should do the obvious thing
    // rather than nothing.
    weather_request_refresh();
    backlight_note_activity();
}

// A full-screen, invisible, non-clickable container. Events from its children
// bubble through it to the screen, so the long-press-to-refresh still works
// from inside a layer.
lv_obj_t *make_layer(lv_obj_t *parent) {
    lv_obj_t *l = theme_decor(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_size(l, UI_WIDTH, UI_HEIGHT);
    lv_obj_add_flag(l, LV_OBJ_FLAG_EVENT_BUBBLE);
    return l;
}

void upper_ascii(char *s) {
    for (; *s; s++) *s = char(toupper(static_cast<unsigned char>(*s)));
}

// ---------------------------------------------------------------------------
// Night Mode cross-fade.
//
// One value, 0..255, drives everything: the day layer's opacity falls as the
// night layer's rises, the hero and condition mix between their two colours,
// the seam sinks from rivet to surface-hi, and the page indicator goes with
// the day chrome. Whichever layer is fully transparent at the end is also
// hidden, so a dormant layout costs no draw time and takes no taps.
// ---------------------------------------------------------------------------
lv_color_t mix_toward_night(uint32_t day, uint32_t night, uint8_t v) {
    // lv_color_mix(c1, c2, ratio): ratio 255 is all c1.
    return lv_color_mix(lv_color_hex(night), lv_color_hex(day), v);
}

void apply_night_mix(uint8_t v) {
    s_ui.night_mix = v;

    lv_obj_set_style_opa(s_ui.day, 255 - v, 0);
    lv_obj_set_style_opa(s_ui.night, v, 0);
    if (s_ui.indicator) lv_obj_set_style_opa(s_ui.indicator, 255 - v, 0);

    if (v == 255) lv_obj_add_flag(s_ui.day, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_clear_flag(s_ui.day, LV_OBJ_FLAG_HIDDEN);
    if (v == 0)   lv_obj_add_flag(s_ui.night, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_clear_flag(s_ui.night, LV_OBJ_FLAG_HIDDEN);

    const lv_color_t hero = mix_toward_night(COL_OAT, COL_NIGHT_HERO, v);
    lv_obj_set_style_text_color(s_ui.hero, hero, 0);
    lv_obj_set_style_text_color(s_ui.hero_unit, hero, 0);
    lv_obj_set_style_text_color(
        s_ui.hero_suffix, mix_toward_night(COL_ALUMINUM_DIM, COL_NIGHT_DIM, v), 0);
    lv_obj_set_style_text_color(
        s_ui.condition, mix_toward_night(COL_ALUMINUM, COL_ALUMINUM_DIM, v), 0);

    const lv_color_t seam = mix_toward_night(COL_RIVET, COL_SURFACE_HI, v);
    lv_obj_set_style_bg_color(s_ui.seam, seam, 0);
    for (lv_obj_t *r : s_ui.rivets) lv_obj_set_style_bg_color(r, seam, 0);
}

void night_anim_cb(void *var, int32_t v) {
    LV_UNUSED(var);
    apply_night_mix(uint8_t(v));
}

void set_night(bool on) {
    if (on == s_ui.night_target) return;
    s_ui.night_target = on;

    const int32_t from = s_ui.night_mix;
    const int32_t to   = on ? 255 : 0;

    // A reversal mid-fade continues from where the fade is, over the time
    // the remaining distance deserves, rather than snapping and restarting.
    lv_anim_del(&s_ui, night_anim_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, &s_ui);
    lv_anim_set_exec_cb(&a, night_anim_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, uint32_t(UI_NIGHT_FADE_MS) * uint32_t(abs(int(to - from))) / 255u);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
lv_obj_t *make_column_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                            int x, int y, int w) {
    lv_obj_t *l = theme_label(parent, font, color, "");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, w);
    lv_obj_set_pos(l, x, y);
    // Nothing in a column may wrap. Clipping is the right failure mode here -
    // a value that runs slightly long should be trimmed, not pushed onto a
    // second line that overlaps the row beneath it.
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

lv_obj_t *create(lv_obj_t *parent) {
    s_ui = {};

    lv_obj_add_event_cb(parent, long_pressed, LV_EVENT_LONG_PRESSED, nullptr);

    // --- Now zone hit target, beneath everything -----------------------------
    lv_obj_t *now_hit = lv_obj_create(parent);
    lv_obj_remove_style_all(now_hit);
    lv_obj_set_pos(now_hit, 0, 0);
    lv_obj_set_size(now_hit, LAYOUT_NOW_W, UI_HEIGHT - 12);
    // EVENT_BUBBLE so long presses reach the screen's handler. Gestures already
    // bubble by default; ordinary events do not.
    lv_obj_add_flag(now_hit, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(now_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(now_hit, now_clicked, LV_EVENT_CLICKED, nullptr);

    // --- the seam between the zones, on the root -----------------------------
    s_ui.seam = theme_decor(parent);
    lv_obj_add_style(s_ui.seam, &style_hairline, 0);
    lv_obj_set_size(s_ui.seam, 1, kSeamH);
    lv_obj_set_pos(s_ui.seam, kSeamX, kSeamY);

    for (int i = 0; i < kRivetCount; i++) {
        lv_obj_t *r = theme_decor(parent);
        lv_obj_set_size(r, 2, 2);
        lv_obj_set_pos(r, kRivetX, kRivetY0 + i * kRivetPitch);
        lv_obj_set_style_radius(r, 1, 0);
        lv_obj_set_style_bg_opa(r, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(r, lv_color_hex(COL_RIVET), 0);
        s_ui.rivets[i] = r;
    }

    // --- day layer ------------------------------------------------------------
    s_ui.day = make_layer(parent);
    lv_obj_t *day = s_ui.day;

    s_ui.now_icon = theme_label(day, &icons_lg, COL_ALUMINUM, ICON_WX_OVERCAST);
    lv_obj_set_pos(s_ui.now_icon, kNowIconX, kNowIconY);

    s_ui.meta = theme_label(day, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_style_text_letter_space(s_ui.meta, 1, 0);
    lv_obj_set_pos(s_ui.meta, LAYOUT_SAFE, kMetaY);

    // Untracked, unlike the meta line above it: with the town, the clock and
    // the age of the data all on one line, the tracking is what would push the
    // "x MIN AGO" off the end for any town longer than DENVER, and that is the
    // one piece of this line the panel must never lose.
    s_ui.place = theme_label(day, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_width(s_ui.place, kPlaceW);
    lv_label_set_long_mode(s_ui.place, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_ui.place, LAYOUT_SAFE, kPlaceY);

    // Connection trouble marker. It lives at the bottom of the Now zone rather
    // than the top-right of the screen, where it would sit on top of the last
    // hour column.
    s_ui.status = theme_label(day, &icons_ui, COL_SUNSET, "");
    lv_obj_set_pos(s_ui.status, LAYOUT_NOW_W - 26, kPlaceY - 2);

    for (int i = 0; i < WX_HOURLY_SLOTS; i++) {
        HourWidgets &w = s_ui.hours[i];
        const int x = column_x(i);

        w.bg = theme_decor(day);
        lv_obj_set_pos(w.bg, x, kSlabY);
        lv_obj_set_size(w.bg, LAYOUT_HOUR_COL_W - 1, kSlabH);
        lv_obj_set_style_radius(w.bg, 5, 0);
        lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(w.bg, lv_color_hex(COL_SURFACE), 0);

        // The rivet hairline between columns. Skipped before the first column,
        // where the zone seam already does the job.
        if (i > 0) {
            lv_obj_t *sep = theme_decor(day);
            lv_obj_add_style(sep, &style_hairline, 0);
            lv_obj_set_size(sep, 1, kSepH);
            lv_obj_set_pos(sep, x, kSepY);
            lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
        }

        w.hour = make_column_label(day, &font_micro, COL_ALUMINUM_DIM, x, kHourY,
                                   LAYOUT_HOUR_COL_W);
        lv_obj_set_style_text_letter_space(w.hour, 1, 0);
        w.icon = make_column_label(day, &icons_md, COL_ALUMINUM, x, kIconY,
                                   LAYOUT_HOUR_COL_W);
        w.temp = make_column_label(day, &font_title, COL_ALUMINUM, x, kTempY,
                                   LAYOUT_HOUR_COL_W);

        // The hit target goes on top of everything else in the column.
        w.cell = lv_obj_create(day);
        lv_obj_remove_style_all(w.cell);
        lv_obj_set_pos(w.cell, x, 0);
        lv_obj_set_size(w.cell, LAYOUT_HOUR_COL_W, UI_HEIGHT - 12);
        lv_obj_add_flag(w.cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(w.cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(w.cell, hour_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(intptr_t(i)));
    }

    // --- night layer ----------------------------------------------------------
    s_ui.night = make_layer(parent);
    lv_obj_t *night = s_ui.night;

    s_ui.night_clock = theme_label(night, &font_micro, COL_NIGHT_DIM, "");
    lv_obj_set_style_text_letter_space(s_ui.night_clock, 1, 0);
    lv_obj_set_pos(s_ui.night_clock, LAYOUT_SAFE, kPlaceY);

    for (int j = 0; j < LAYOUT_NIGHT_COLS; j++) {
        NightWidgets &n = s_ui.night_hours[j];
        const int x = night_column_x(j);
        n.hour = make_column_label(night, &font_micro, COL_NIGHT_DIM, x, kNightHourY,
                                   LAYOUT_NIGHT_COL_W);
        lv_obj_set_style_text_letter_space(n.hour, 1, 0);
        n.temp = make_column_label(night, &font_title, COL_ALUMINUM_DIM, x, kNightTempY,
                                   LAYOUT_NIGHT_COL_W);
    }

    // --- root: shared between the layouts ------------------------------------
    s_ui.hero = theme_label(parent, &font_hero, COL_OAT, "--");
    lv_obj_set_style_text_letter_space(s_ui.hero, -1, 0);
    lv_obj_set_pos(s_ui.hero, LAYOUT_SAFE, kHeroY);

    s_ui.hero_unit = theme_label(parent, &font_title, COL_OAT, "°");
    s_ui.hero_suffix = theme_label(parent, &font_label, COL_ALUMINUM_DIM, "");

    s_ui.condition = theme_label(parent, &font_body, COL_ALUMINUM, "");
    lv_obj_set_pos(s_ui.condition, LAYOUT_SAFE, kConditionY);

    apply_night_mix(0);
    return parent;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void place_hero_marks() {
    // Re-anchored after every change because the hero is auto-sized: 9 -> 10
    // moves the degree mark, and the unit letter has to move with it.
    lv_obj_align_to(s_ui.hero_unit, s_ui.hero, LV_ALIGN_OUT_RIGHT_TOP, 2, kDegreeDY);
    lv_obj_align_to(s_ui.hero_suffix, s_ui.hero_unit, LV_ALIGN_OUT_RIGHT_TOP, 4, kUnitDY);
}

void clear_column(HourWidgets &w) {
    lv_label_set_text(w.hour, "");
    lv_label_set_text(w.icon, "");
    lv_label_set_text(w.temp, "");
    lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
}

void update(lv_obj_t *root) {
    if (s_ui.indicator == nullptr) s_ui.indicator = screens_indicator(root);

    // The layout follows the dimmer: deep night in Auto with nobody about is
    // the night layout, and the presence boost from any touch is what brings
    // the day back. The fade runs on its own once started.
    set_night(backlight_is_deep_night());

    WxData d;
    weather_snapshot(d);

    char buf[64];

    // Before the first forecast lands, the panel says what it is doing. A
    // wall-mounted display that shows nothing is indistinguishable from one
    // that is broken, and this is exactly when a new owner is watching it.
    if (!d.valid) {
        lv_label_set_text(s_ui.hero, "--");
        lv_label_set_text(s_ui.hero_suffix, "");
        place_hero_marks();
        lv_label_set_text(s_ui.condition,
                          net_connected() ? "Getting forecast" : "Setting up");
        // Normally unseen: the boot and setup screens cover this until the
        // first forecast lands. It remains for the case where they cannot.
        snprintf(buf, sizeof(buf), "%s", net_connected() ? weather_status_text()
                                                         : net_state_text());
        lv_label_set_text(s_ui.meta, buf);
        lv_label_set_text(s_ui.place, "");
        lv_label_set_text(s_ui.status, "");
        lv_label_set_text(s_ui.night_clock, "");
        for (auto &w : s_ui.hours) clear_column(w);
        for (auto &n : s_ui.night_hours) {
            lv_label_set_text(n.hour, "");
            lv_label_set_text(n.temp, "");
        }
        return;
    }

    // --- Now ----------------------------------------------------------------
    fmt_temp_plain(d.temp, buf, sizeof(buf));
    lv_label_set_text(s_ui.hero, buf);
    lv_label_set_text(s_ui.hero_suffix, d.imperial ? "F" : "C");
    place_hero_marks();

    lv_label_set_text(s_ui.now_icon, icon_for(wx_icon_for(d.code, d.is_day)));
    lv_obj_set_style_text_color(s_ui.now_icon,
                                d.is_day ? lv_color_hex(COL_OAT)
                                         : lv_color_hex(COL_ALUMINUM), 0);

    lv_label_set_text(s_ui.condition, wx_condition_text(d.code));

    snprintf(buf, sizeof(buf), "FEELS %d°  ·  H %d° L %d°",
             int(lroundf(d.apparent)), int(lroundf(d.temp_max)),
             int(lroundf(d.temp_min)));
    lv_label_set_text(s_ui.meta, buf);

    char town[sizeof(d.location)], clock[16], age[24];
    snprintf(town, sizeof(town), "%s", d.location[0] ? d.location : "HERE");
    upper_ascii(town);
    fmt_clock_compact(time(nullptr), d.utc_offset, clock, sizeof(clock));
    fmt_relative(weather_seconds_since_update(), age, sizeof(age));
    snprintf(buf, sizeof(buf), "%s  ·  %s  ·  %s", town, clock, age);
    lv_label_set_text(s_ui.place, buf);

    fmt_clock(time(nullptr), d.utc_offset, clock, sizeof(clock));
    lv_label_set_text(s_ui.night_clock, clock);

    // A quiet marker when the network has gone away but the data is still
    // worth showing. Silence would be worse than a stale reading; a stale
    // reading presented as current would be worse still, which is why the
    // "x min ago" on the line to its left is never omitted.
    lv_label_set_text(s_ui.status, net_connected() ? "" : ICON_WIFI_OFF);

    // --- hourly strip -------------------------------------------------------
    const int count = d.hour_count < WX_HOURLY_SLOTS ? d.hour_count : WX_HOURLY_SLOTS;

    for (int i = 0; i < WX_HOURLY_SLOTS; i++) {
        HourWidgets &w = s_ui.hours[i];

        if (i >= count) {
            clear_column(w);
            continue;
        }

        const WxHour &h = d.hours[i];
        const bool is_now = is_current_hour(h.time, d.utc_offset);

        if (is_now) {
            lv_label_set_text(w.hour, "NOW");
            lv_obj_set_style_text_color(w.hour, lv_color_hex(COL_TURQUOISE), 0);
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_COVER, 0);
        } else {
            fmt_hour(h.time, d.utc_offset, buf, sizeof(buf));
            lv_label_set_text(w.hour, buf);
            lv_obj_set_style_text_color(w.hour, lv_color_hex(COL_ALUMINUM_DIM), 0);
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        }

        const lv_color_t tc = theme_temp_color(h.temp, d.imperial);

        lv_label_set_text(w.icon, icon_for(wx_icon_for(h.code, h.is_day)));
        lv_obj_set_style_text_color(w.icon, tc, 0);

        fmt_temp_plain(h.temp, buf, sizeof(buf));
        lv_label_set_text(w.temp, buf);
        lv_obj_set_style_text_color(w.temp, tc, 0);
        // 30px Jost Medium measures ~52px at "100", the column's full width;
        // the 24px cut measures ~41px. Swap per label at three characters.
        lv_obj_set_style_text_font(
            w.temp, strlen(buf) >= 3 ? &font_hour_narrow : &font_title, 0);
    }

    // --- night strip: every other hour, so the four columns span the same
    // eight hours the day strip does, at half the density --------------------
    for (int j = 0; j < LAYOUT_NIGHT_COLS; j++) {
        NightWidgets &n = s_ui.night_hours[j];
        const int i = j * 2;
        if (i >= count) {
            lv_label_set_text(n.hour, "");
            lv_label_set_text(n.temp, "");
            continue;
        }
        const WxHour &h = d.hours[i];
        if (is_current_hour(h.time, d.utc_offset)) {
            lv_label_set_text(n.hour, "NOW");
        } else {
            fmt_hour(h.time, d.utc_offset, buf, sizeof(buf));
            lv_label_set_text(n.hour, buf);
        }
        fmt_temp_plain(h.temp, buf, sizeof(buf));
        lv_label_set_text(n.temp, buf);
    }
}

const ScreenDef kDef = {
    "Today",
    ICON_SUN,
    create,
    update,
    0,                                             // home
    UI_SWIPE_LEFT | UI_SWIPE_RIGHT | UI_SWIPE_DOWN,
};

}  // namespace

const ScreenDef &screen_today_def() { return kDef; }
