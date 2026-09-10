#include "screen_today.h"

#include <math.h>
#include <stdio.h>

#include "config.h"
#include "display/backlight.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "ui/format.h"
#include "ui/icons.h"
#include "ui/overlays.h"
#include "ui/screen_manager.h"
#include "ui/theme.h"

namespace {

// --- vertical rhythm of one hour column ------------------------------------
// Worked out against the 180px height with the page indicator reserved at the
// bottom. Everything below is measured from these, so retuning the layout after
// a design pass means changing numbers here and nowhere else.
constexpr int kHourY   = 6;     // hour label
constexpr int kIconY   = 22;    // condition glyph
constexpr int kTempY   = 44;    // temperature
constexpr int kRibbonY = 84;    // top of the trend band
constexpr int kRibbonH = 34;
constexpr int kPrecipY = 122;
constexpr int kWindY   = 140;

// --- the "Now" zone --------------------------------------------------------
//
// The hero temperature is 72px and the degree mark is a separate label aligned
// to its right, rather than part of the same string. That is partly because the
// design wants the mark set against the cap height rather than the baseline,
// and partly because it is what keeps "100" from running into the condition
// icon: the icon sits at a fixed x, and a three-digit hero would collide with
// it if the degree sign were baked into the same auto-sized label.
constexpr int kHeroY      = 24;
constexpr int kNowIconX   = 148;
constexpr int kNowIconY   = 8;
constexpr int kConditionY = 100;
constexpr int kMetaY      = 124;
constexpr int kPlaceY     = 146;

struct HourWidgets {
    lv_obj_t *cell;      // invisible hit target, carries the index
    lv_obj_t *bg;        // "now" highlight
    lv_obj_t *hour;
    lv_obj_t *icon;
    lv_obj_t *temp;
    lv_obj_t *precip;
    lv_obj_t *wind;
};

struct TodayUi {
    lv_obj_t *hero;
    lv_obj_t *hero_unit;
    lv_obj_t *now_icon;
    lv_obj_t *condition;
    lv_obj_t *meta;
    lv_obj_t *place;
    lv_obj_t *status;
    lv_obj_t *ribbon;
    lv_color_t *ribbon_buf;
    HourWidgets hours[WX_HOURLY_SLOTS];
};

TodayUi s_ui = {};

int column_x(int i)      { return LAYOUT_STRIP_X + i * LAYOUT_HOUR_COL_W; }
int column_center(int i) { return column_x(i) + LAYOUT_HOUR_COL_W / 2; }

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

// ---------------------------------------------------------------------------
// The temperature ribbon.
//
// A 2px polyline through the ten hourly temperatures with a gradient fading
// away beneath it. It is drawn into a canvas rather than assembled from
// widgets because the fill has to follow the curve rather than stair-step under
// it, and because it only needs redrawing when the forecast changes - roughly
// once every ten minutes, which makes a per-pixel loop free.
//
// This is the element that lets you read the shape of the day before reading a
// single number, which is the whole argument for putting a forecast on a strip
// this wide.
// ---------------------------------------------------------------------------
void draw_ribbon(const WxData &d, int count) {
    if (s_ui.ribbon == nullptr || count < 2) return;

    // The per-pixel loop below is cheap but not free, and the screen update
    // runs once a second to move the clock along. The curve only changes when
    // a new forecast lands, so redraw on that and nothing else.
    static time_t s_drawn_for = -1;
    if (d.fetched_at == s_drawn_for) return;
    s_drawn_for = d.fetched_at;

    lv_canvas_fill_bg(s_ui.ribbon, lv_color_hex(COL_GROUND), LV_OPA_TRANSP);

    float tmin = INFINITY, tmax = -INFINITY;
    for (int i = 0; i < count; i++) {
        const float t = d.hours[i].temp;
        if (isnan(t)) continue;
        if (t < tmin) tmin = t;
        if (t > tmax) tmax = t;
    }
    if (!isfinite(tmin) || !isfinite(tmax)) return;

    // A flat forecast should read as a flat line across the middle of the band,
    // not as noise amplified to fill it.
    float span = tmax - tmin;
    if (span < 4.0f) {
        const float mid = (tmax + tmin) * 0.5f;
        tmin = mid - 2.0f;
        tmax = mid + 2.0f;
        span = 4.0f;
    }

    const int pad = 4;                       // keep the curve off the band edges
    const int usable = kRibbonH - pad * 2;

    auto y_for = [&](float temp) -> float {
        const float norm = (temp - tmin) / span;    // 0 at coldest, 1 at hottest
        return float(pad) + float(usable) * (1.0f - norm);
    };

    // Sample every x in the band, interpolating between hour points.
    for (int x = 0; x < LAYOUT_STRIP_W; x++) {
        const int abs_x = LAYOUT_STRIP_X + x;

        // Which two hour centres does this column fall between?
        float fpos = float(abs_x - column_center(0)) / float(LAYOUT_HOUR_COL_W);
        if (fpos < 0.0f) fpos = 0.0f;
        if (fpos > float(count - 1)) fpos = float(count - 1);

        const int i0 = int(fpos);
        const int i1 = (i0 + 1 < count) ? i0 + 1 : i0;
        const float frac = fpos - float(i0);

        const float t0 = d.hours[i0].temp;
        const float t1 = d.hours[i1].temp;
        if (isnan(t0) || isnan(t1)) continue;

        const float temp = t0 + (t1 - t0) * frac;
        const int line_y = int(lroundf(y_for(temp)));
        const lv_color_t c = theme_temp_color(temp, d.imperial);

        // Fill below the curve, fading out with distance. Squaring the falloff
        // keeps the top of the fill close to the line and the bottom genuinely
        // gone, rather than leaving a visible hard edge at the band boundary.
        for (int y = line_y; y < kRibbonH; y++) {
            const float f = 1.0f - float(y - line_y) / float(kRibbonH - line_y + 1);
            const lv_opa_t opa = lv_opa_t(fminf(255.0f, 70.0f * f * f));
            if (opa < 6) continue;
            lv_canvas_set_px_color(s_ui.ribbon, x, y, c);
            lv_canvas_set_px_opa(s_ui.ribbon, x, y, opa);
        }

        // The line itself, 2px, at full strength.
        for (int dy = 0; dy < 2; dy++) {
            const int y = line_y + dy;
            if (y < 0 || y >= kRibbonH) continue;
            lv_canvas_set_px_color(s_ui.ribbon, x, y, c);
            lv_canvas_set_px_opa(s_ui.ribbon, x, y, LV_OPA_COVER);
        }
    }

    lv_obj_invalidate(s_ui.ribbon);
}

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------
lv_obj_t *create(lv_obj_t *parent) {
    s_ui = {};

    lv_obj_add_event_cb(parent, long_pressed, LV_EVENT_LONG_PRESSED, nullptr);

    // --- Now zone -----------------------------------------------------------
    lv_obj_t *now_hit = lv_obj_create(parent);
    lv_obj_remove_style_all(now_hit);
    lv_obj_set_pos(now_hit, 0, 0);
    lv_obj_set_size(now_hit, LAYOUT_NOW_W, UI_HEIGHT - 12);
    // EVENT_BUBBLE so long presses reach the screen's handler. Gestures already
    // bubble by default; ordinary events do not.
    lv_obj_add_flag(now_hit, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_clear_flag(now_hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(now_hit, now_clicked, LV_EVENT_CLICKED, nullptr);

    s_ui.hero = theme_label(parent, &font_hero, COL_OAT, "--");
    lv_obj_set_style_text_letter_space(s_ui.hero, -1, 0);
    lv_obj_set_pos(s_ui.hero, LAYOUT_SAFE, kHeroY);

    s_ui.hero_unit = theme_label(parent, &font_title, COL_OAT, "°");

    s_ui.now_icon = theme_label(parent, &icons_lg, COL_ALUMINUM, ICON_WX_OVERCAST);
    lv_obj_set_pos(s_ui.now_icon, kNowIconX, kNowIconY);

    s_ui.condition = theme_label(parent, &font_body, COL_ALUMINUM, "");
    lv_obj_set_pos(s_ui.condition, LAYOUT_SAFE, kConditionY);

    s_ui.meta = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_style_text_letter_space(s_ui.meta, 1, 0);
    lv_obj_set_pos(s_ui.meta, LAYOUT_SAFE, kMetaY);

    s_ui.place = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_style_text_letter_space(s_ui.place, 1, 0);
    lv_obj_set_pos(s_ui.place, LAYOUT_SAFE, kPlaceY);

    // Connection trouble marker. It lives at the bottom of the Now zone rather
    // than the top-right of the screen, where it would sit on top of the last
    // hour column.
    s_ui.status = theme_label(parent, &icons_ui, COL_SUNSET, "");
    lv_obj_set_pos(s_ui.status, LAYOUT_NOW_W - 26, kPlaceY - 2);

    // --- the seam between the zones ----------------------------------------
    lv_obj_t *seam = theme_decor(parent);
    lv_obj_add_style(seam, &style_hairline, 0);
    lv_obj_set_size(seam, 1, UI_HEIGHT - 32);
    lv_obj_set_pos(seam, LAYOUT_STRIP_X - 1, 12);

    // --- trend ribbon (behind the columns) ---------------------------------
    // TRUE_COLOR_ALPHA so the fill can fade rather than sit on a rectangle of
    // background that would show as a seam against the page.
    s_ui.ribbon_buf = static_cast<lv_color_t *>(
        heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(LAYOUT_STRIP_W, kRibbonH),
                         MALLOC_CAP_SPIRAM));
    if (s_ui.ribbon_buf) {
        s_ui.ribbon = lv_canvas_create(parent);
        lv_canvas_set_buffer(s_ui.ribbon, s_ui.ribbon_buf, LAYOUT_STRIP_W, kRibbonH,
                             LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_obj_set_pos(s_ui.ribbon, LAYOUT_STRIP_X, kRibbonY);
        lv_canvas_fill_bg(s_ui.ribbon, lv_color_hex(COL_GROUND), LV_OPA_TRANSP);
        lv_obj_clear_flag(s_ui.ribbon, LV_OBJ_FLAG_CLICKABLE);
    }

    // --- hour columns -------------------------------------------------------
    for (int i = 0; i < WX_HOURLY_SLOTS; i++) {
        HourWidgets &w = s_ui.hours[i];
        const int x = column_x(i);

        w.bg = theme_decor(parent);
        lv_obj_set_pos(w.bg, x, 4);
        lv_obj_set_size(w.bg, LAYOUT_HOUR_COL_W - 1, UI_HEIGHT - 22);
        lv_obj_set_style_radius(w.bg, 5, 0);
        lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(w.bg, lv_color_hex(COL_SURFACE), 0);

        // The rivet hairline between columns. Skipped before the first column,
        // where the zone seam already does the job.
        if (i > 0) {
            lv_obj_t *sep = theme_decor(parent);
            lv_obj_add_style(sep, &style_hairline, 0);
            lv_obj_set_size(sep, 1, 96);
            lv_obj_set_pos(sep, x, 20);
            lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
        }

        w.hour = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, "--");
        lv_obj_set_style_text_letter_space(w.hour, 1, 0);
        lv_obj_set_style_text_align(w.hour, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(w.hour, LAYOUT_HOUR_COL_W);
        lv_obj_set_pos(w.hour, x, kHourY);

        w.icon = theme_label(parent, &icons_sm, COL_ALUMINUM, "");
        lv_obj_set_style_text_align(w.icon, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(w.icon, LAYOUT_HOUR_COL_W);
        lv_obj_set_pos(w.icon, x, kIconY);

        w.temp = theme_label(parent, &font_hour, COL_ALUMINUM, "--");
        lv_obj_set_style_text_align(w.temp, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(w.temp, LAYOUT_HOUR_COL_W);
        lv_obj_set_pos(w.temp, x, kTempY);

        w.precip = theme_label(parent, &font_micro, COL_TURQUOISE, "");
        lv_obj_set_style_text_align(w.precip, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(w.precip, LAYOUT_HOUR_COL_W);
        lv_obj_set_pos(w.precip, x, kPrecipY);

        w.wind = theme_label(parent, &font_micro, COL_SKY, "");
        lv_obj_set_style_text_align(w.wind, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_width(w.wind, LAYOUT_HOUR_COL_W);
        lv_obj_set_pos(w.wind, x, kWindY);

        // Nothing in a 43px column may wrap. Clipping is the right failure mode
        // here - a value that runs slightly long should be trimmed, not pushed
        // onto a second line that overlaps the row beneath it.
        for (lv_obj_t *l : {w.hour, w.icon, w.temp, w.precip, w.wind}) {
            lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
        }

        // The hit target goes on top of everything else in the column.
        w.cell = lv_obj_create(parent);
        lv_obj_remove_style_all(w.cell);
        lv_obj_set_pos(w.cell, x, 0);
        lv_obj_set_size(w.cell, LAYOUT_HOUR_COL_W, UI_HEIGHT - 12);
        lv_obj_add_flag(w.cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(w.cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(w.cell, hour_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(intptr_t(i)));
    }

    return parent;
}

// ---------------------------------------------------------------------------
// Update
// ---------------------------------------------------------------------------
void update(lv_obj_t *root) {
    LV_UNUSED(root);

    WxData d;
    weather_snapshot(d);

    char buf[64];

    // Before the first forecast lands, the panel says what it is doing. A
    // wall-mounted display that shows nothing is indistinguishable from one
    // that is broken, and this is exactly when a new owner is watching it.
    if (!d.valid) {
        lv_label_set_text(s_ui.hero, "--");
        lv_obj_align_to(s_ui.hero_unit, s_ui.hero, LV_ALIGN_OUT_RIGHT_TOP, 2, 12);
        lv_label_set_text(s_ui.condition,
                          net_connected() ? "Getting forecast" : "Setting up");
        if (net_state() == NetState::Portal) {
            snprintf(buf, sizeof(buf), "JOIN WI-FI \"%s\" FROM YOUR PHONE", net_ap_name());
        } else {
            snprintf(buf, sizeof(buf), "%s", net_connected() ? weather_status_text()
                                                             : net_state_text());
        }
        lv_label_set_text(s_ui.meta, buf);
        lv_label_set_text(s_ui.place, "");
        lv_label_set_text(s_ui.status, "");
        for (auto &w : s_ui.hours) {
            lv_label_set_text(w.hour, "");
            lv_label_set_text(w.icon, "");
            lv_label_set_text(w.temp, "");
            lv_label_set_text(w.precip, "");
            lv_label_set_text(w.wind, "");
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        }
        if (s_ui.ribbon) lv_canvas_fill_bg(s_ui.ribbon, lv_color_hex(COL_GROUND), LV_OPA_TRANSP);
        return;
    }

    // --- Now ----------------------------------------------------------------
    fmt_temp_plain(d.temp, buf, sizeof(buf));
    lv_label_set_text(s_ui.hero, buf);
    // Re-anchored after every change because the hero is auto-sized: 9 -> 10
    // moves the degree mark, and it has to move with it.
    lv_obj_align_to(s_ui.hero_unit, s_ui.hero, LV_ALIGN_OUT_RIGHT_TOP, 2, 12);

    lv_label_set_text(s_ui.now_icon, icon_for(wx_icon_for(d.code, d.is_day)));
    lv_obj_set_style_text_color(s_ui.now_icon,
                                d.is_day ? lv_color_hex(COL_OAT)
                                         : lv_color_hex(COL_ALUMINUM), 0);

    lv_label_set_text(s_ui.condition, wx_condition_text(d.code));

    snprintf(buf, sizeof(buf), "FEELS %d°  ·  H %d°  L %d°",
             int(lroundf(d.apparent)), int(lroundf(d.temp_max)),
             int(lroundf(d.temp_min)));
    lv_label_set_text(s_ui.meta, buf);

    char clock[16], age[24];
    fmt_clock(time(nullptr), d.utc_offset, clock, sizeof(clock));
    fmt_relative(weather_seconds_since_update(), age, sizeof(age));
    snprintf(buf, sizeof(buf), "%s  ·  %s  ·  %s",
             d.location[0] ? d.location : "HERE", clock, age);
    lv_label_set_text(s_ui.place, buf);

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
            lv_label_set_text(w.hour, "");
            lv_label_set_text(w.icon, "");
            lv_label_set_text(w.temp, "");
            lv_label_set_text(w.precip, "");
            lv_label_set_text(w.wind, "");
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
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

        // Below 10% the number is noise. An empty cell reads as "no rain"
        // faster than a column of zeroes does, and it keeps the strip quiet
        // enough that a real chance of rain stands out.
        if (h.precip_prob >= 10) {
            snprintf(buf, sizeof(buf), "%d%%", h.precip_prob);
            lv_label_set_text(w.precip, buf);
        } else {
            lv_label_set_text(w.precip, "");
        }

        // Same logic for wind: a 2mph breeze is not information.
        if (!isnan(h.wind) && h.wind >= 3.0f) {
            snprintf(buf, sizeof(buf), "%s %d", wx_cardinal(h.wind_dir),
                     int(lroundf(h.wind)));
            lv_label_set_text(w.wind, buf);
        } else {
            lv_label_set_text(w.wind, "");
        }
    }

    draw_ribbon(d, count);
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
