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

// The home screen, to design/SPEC.md §1, at seven hours rather than ten: the
// strip's rows (glyph, temperature, trend ribbon, UV, chance of rain and its
// bar, wind) are the design's, in 52px columns instead of 42, with a column
// of row labels in front of them. Night Mode is a second layer on the same
// screen: a clock, the condition glyph beside it, and the next sunrise. Crossing
// between the layouts is one animation that fades the weather out and the
// clock in.

namespace {

// --- vertical rhythm of one hour column (SPEC §1, layout.today tokens) -------
constexpr int kHourY      = 6;     // hour label
constexpr int kIconY      = 22;    // condition glyph
constexpr int kTempY      = 42;    // temperature, Title 30 (line box 32, baseline 25 in)
constexpr int kRibbonY    = 76;    // top of the trend band
constexpr int kRibbonH    = 24;
constexpr int kUvY        = 102;   // UV index
constexpr int kPrecipY    = 120;   // chance of rain
constexpr int kBarBottomY = 148;   // the bar grows up from here, 2px wide
constexpr int kBarMaxH    = 10;    // 100% - height is prob / 10
constexpr int kWindY      = 150;
constexpr int kSepY       = 20;    // column hairlines run y20-y112
constexpr int kSepH       = 92;
constexpr int kSlabH      = 172;   // the NOW slab: above the hour label to below the wind

// The row labels sit in the first column of the strip, right-aligned so they
// read as the heading of the row they name. TEMP shares the numerals'
// baseline: Micro's line box is 14 with the baseline 11 in, Title's is 32
// with the baseline 25 in.
constexpr int kLabelRight = LAYOUT_HOURS_X - 6;
constexpr int kLabelTempY = kTempY + 25 - 11;

// --- the "Now" zone --------------------------------------------------------
//
// The hero temperature is 72px and the degree mark is a separate label aligned
// to its right, rather than part of the same string. That is partly because the
// design wants the mark set against the cap height rather than the baseline,
// and partly because it is what keeps "100" from running into the condition
// icon: the icon sits at a fixed x, and a three-digit hero would collide with
// it if the degree sign were baked into the same auto-sized label. The unit
// letter hangs off the degree mark in turn, smaller and dimmer.
//
// Under the hero, in reading order: how it feels, then the day's high and low,
// coloured rather than lettered (red is the high, blue the low), then the
// clock and the town. The condition sits under its own icon, on the right.
constexpr int kHeroY      = 22;    // digits y24-y74
constexpr int kDegreeDY   = 12;    // hero top -> degree top (y34, cap-aligned)
constexpr int kUnitDY     = 18;    // degree top -> unit top (y52)
constexpr int kNowIconX   = 138;   // 56px glyph, centred on x166
constexpr int kNowIconY   = 10;
constexpr int kConditionX = 130;   // centred under the icon, 72 wide
constexpr int kConditionY = 66;
constexpr int kConditionW = 72;
constexpr int kFeelsY     = 82;
constexpr int kHighLowY   = 102;
constexpr int kPlaceY     = 146;
constexpr int kPlaceW     = 190;   // the place line must fit x10-x200

// --- the seam (SPEC §1: "the one ornament") --------------------------------
constexpr int kSeamX       = LAYOUT_STRIP_X - 4;   // 204: the aluminum line
constexpr int kSeamY       = 12;
constexpr int kSeamH       = 148;                  // to y160
constexpr int kRivetX      = LAYOUT_STRIP_X;       // 208: the rivets, a few px right of it
constexpr int kRivetY0     = 16;
constexpr int kRivetPitch  = 16;
constexpr int kRivetCount  = 10;                   // y16 .. y160

// --- Night Mode: a clock, a glyph and the sunrise ---------------------------
// The 128px cut's line box is 95px tall. Placed by eye on the glass: the
// first try (top at y18) sat high, the second (centred, top at y52) sat low;
// this is the middle of the two. Shifted right a little because the bezel on
// this orientation's left edge is the wider one.
//
// Its digits run y37-y127. The condition glyph to the left and the sunrise
// column to the right are centred on that band, at 69% so the time stays
// the brightest thing on a screen meant to be glanced at from a bunk. Both
// are placed against the measured width of the time, since "1:05" is a good
// deal narrower than "12:05".
constexpr int kClockY       = 35;
constexpr int kClockX       = 12;
constexpr int kClockCenterX = kClockX + UI_WIDTH / 2;
constexpr int kClockMidY    = 82;
constexpr int kNightGap     = 18;                  // between the time and its companions
constexpr int kNightIconW   = 84;                  // icons_xl advance
constexpr int kNightOpa     = 176;                 // 69%

struct HourWidgets {
    lv_obj_t *cell;        // invisible hit target, carries the index
    lv_obj_t *bg;          // "now" slab
    lv_obj_t *hour;
    lv_obj_t *icon;
    lv_obj_t *temp;
    lv_obj_t *uv;
    lv_obj_t *precip;
    lv_obj_t *bar;         // 2px chance-of-rain bar under the percentage
    lv_obj_t *wind_arrow;  // compass glyph for where the wind blows
    lv_obj_t *wind_speed;
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
    lv_obj_t *seam;
    lv_obj_t *rivets[kRivetCount];

    // Day layer.
    lv_obj_t *now_icon;
    lv_obj_t *condition;
    lv_obj_t *feels;
    lv_obj_t *high;
    lv_obj_t *low;
    lv_obj_t *place;
    lv_obj_t *status;
    lv_obj_t *ribbon;
    lv_color_t *ribbon_buf;
    HourWidgets hours[WX_HOURLY_SLOTS];

    // Night layer.
    lv_obj_t *night_clock;
    lv_obj_t *night_icon;
    lv_obj_t *night_sunrise_icon;
    lv_obj_t *night_sunrise;

    // Cross-fade state. night_mix is 0 in daylight, 255 in Night Mode.
    lv_obj_t *indicator;     // the manager's page indicator, found lazily
    bool      night_target;
    uint8_t   night_mix;
};

TodayUi s_ui = {};

int column_x(int i)      { return LAYOUT_HOURS_X + i * LAYOUT_HOUR_COL_W; }
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

// The compass glyph for a bearing, to the nearest 45 degrees.
const char *wind_arrow_glyph(int toward_deg) {
    static const char *const kArrows[8] = {
        ICON_ARROW_N, ICON_ARROW_NE, ICON_ARROW_E, ICON_ARROW_SE,
        ICON_ARROW_S, ICON_ARROW_SW, ICON_ARROW_W, ICON_ARROW_NW,
    };
    const int octant = ((toward_deg % 360 + 360) % 360 + 22) / 45 % 8;
    return kArrows[octant];
}

void upper_ascii(char *s) {
    for (; *s; s++) *s = char(toupper(static_cast<unsigned char>(*s)));
}

int text_width(const char *s, const lv_font_t *font, int letter_space = 0) {
    return int(lv_txt_get_width(s, uint32_t(strlen(s)), font, letter_space, LV_TEXT_FLAG_NONE));
}

// ---------------------------------------------------------------------------
// Night Mode cross-fade.
//
// One value, 0..255, drives everything: the day layer, the hero, the seam and
// the page indicator fade out as the night layer fades in. Whichever layer is
// fully transparent at the end is also hidden, so a dormant layout costs no
// draw time and takes no taps.
// ---------------------------------------------------------------------------
void apply_night_mix(uint8_t v) {
    s_ui.night_mix = v;

    lv_obj_set_style_opa(s_ui.day, 255 - v, 0);
    lv_obj_set_style_opa(s_ui.night, v, 0);
    if (s_ui.indicator) lv_obj_set_style_opa(s_ui.indicator, 255 - v, 0);

    if (v == 255) lv_obj_add_flag(s_ui.day, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_clear_flag(s_ui.day, LV_OBJ_FLAG_HIDDEN);
    if (v == 0)   lv_obj_add_flag(s_ui.night, LV_OBJ_FLAG_HIDDEN);
    else          lv_obj_clear_flag(s_ui.night, LV_OBJ_FLAG_HIDDEN);

    const lv_opa_t day_opa = 255 - v;
    for (lv_obj_t *o : {s_ui.hero, s_ui.hero_unit, s_ui.hero_suffix, s_ui.seam}) {
        lv_obj_set_style_opa(o, day_opa, 0);
    }
    for (lv_obj_t *r : s_ui.rivets) lv_obj_set_style_opa(r, day_opa, 0);
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
// The temperature ribbon.
//
// A 2px polyline through the hourly temperatures with a gradient fading away
// beneath it. It is drawn into a canvas rather than assembled from widgets
// because the fill has to follow the curve rather than stair-step under it,
// and because it only needs redrawing when the forecast changes - roughly
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

    const int pad = 3;                       // keep the curve off the band edges
    const int usable = kRibbonH - pad * 2;

    auto y_for = [&](float temp) -> float {
        const float norm = (temp - tmin) / span;    // 0 at coldest, 1 at hottest
        return float(pad) + float(usable) * (1.0f - norm);
    };

    // Sample every x in the band, interpolating between hour points.
    for (int x = 0; x < LAYOUT_STRIP_W; x++) {
        const int abs_x = LAYOUT_HOURS_X + x;

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

// A row label in the strip's first column, its right edge a few px short of
// the first hour.
lv_obj_t *make_row_label(lv_obj_t *parent, const char *text, int y) {
    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, kLabelRight - text_width(text, &font_micro, 1), y);
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

    // The condition, set small under its icon and allowed to wrap: "PARTLY
    // CLOUDY" is two lines of Micro, and a caption belongs with its picture.
    s_ui.condition = theme_label(day, &font_micro, COL_ALUMINUM, "");
    lv_obj_set_style_text_letter_space(s_ui.condition, 1, 0);
    lv_obj_set_style_text_align(s_ui.condition, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(s_ui.condition, kConditionW);
    lv_label_set_long_mode(s_ui.condition, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_ui.condition, kConditionX, kConditionY);

    s_ui.feels = theme_label(day, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_style_text_letter_space(s_ui.feels, 1, 0);
    lv_obj_set_pos(s_ui.feels, LAYOUT_SAFE, kFeelsY);

    // High and low, told apart by colour alone: the warm one is the high.
    s_ui.high = theme_label(day, &font_label, COL_SUNSET, "");
    lv_obj_set_pos(s_ui.high, LAYOUT_SAFE, kHighLowY);
    s_ui.low = theme_label(day, &font_label, COL_SKY, "");
    lv_obj_set_pos(s_ui.low, LAYOUT_SAFE, kHighLowY);

    // Untracked: with the clock and the town on one line, tracking is what
    // would push a long town name off the end.
    s_ui.place = theme_label(day, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_width(s_ui.place, kPlaceW);
    lv_label_set_long_mode(s_ui.place, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(s_ui.place, LAYOUT_SAFE, kPlaceY);

    // Connection trouble marker. It lives at the bottom of the Now zone rather
    // than the top-right of the screen, where it would sit on top of the last
    // hour column.
    s_ui.status = theme_label(day, &icons_ui, COL_SUNSET, "");
    lv_obj_set_pos(s_ui.status, LAYOUT_NOW_W - 26, kPlaceY - 2);

    // Trend ribbon, behind the columns. TRUE_COLOR_ALPHA so the fill can fade
    // rather than sit on a rectangle of background that would show as a seam
    // against the page.
    s_ui.ribbon_buf = static_cast<lv_color_t *>(
        heap_caps_malloc(LV_CANVAS_BUF_SIZE_TRUE_COLOR_ALPHA(LAYOUT_STRIP_W, kRibbonH),
                         MALLOC_CAP_SPIRAM));
    if (s_ui.ribbon_buf) {
        s_ui.ribbon = lv_canvas_create(day);
        lv_canvas_set_buffer(s_ui.ribbon, s_ui.ribbon_buf, LAYOUT_STRIP_W, kRibbonH,
                             LV_IMG_CF_TRUE_COLOR_ALPHA);
        lv_obj_set_pos(s_ui.ribbon, LAYOUT_HOURS_X, kRibbonY);
        lv_canvas_fill_bg(s_ui.ribbon, lv_color_hex(COL_GROUND), LV_OPA_TRANSP);
        lv_obj_clear_flag(s_ui.ribbon, LV_OBJ_FLAG_CLICKABLE);
    }

    // --- the row labels, in the strip's first column ------------------------
    make_row_label(day, "TEMP", kLabelTempY);
    make_row_label(day, "UV", kUvY);
    make_row_label(day, "WIND", kWindY);
    {
        // A droplet in front of the percent sign, so the row reads as "chance
        // of rain" without spelling it out.
        lv_obj_t *pct = make_row_label(day, "%", kPrecipY);
        lv_obj_t *drop = theme_label(day, &icons_xs, COL_ALUMINUM_DIM, ICON_DROP);
        lv_obj_set_pos(drop, lv_obj_get_x(pct) - 13, kPrecipY + 2);
    }

    for (int i = 0; i < WX_HOURLY_SLOTS; i++) {
        HourWidgets &w = s_ui.hours[i];
        const int x = column_x(i);
        const int cx = column_center(i);

        w.bg = theme_decor(day);
        lv_obj_set_pos(w.bg, x, 0);
        lv_obj_set_size(w.bg, LAYOUT_HOUR_COL_W - 1, kSlabH);
        lv_obj_set_style_radius(w.bg, 5, 0);
        lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(w.bg, lv_color_hex(COL_SURFACE), 0);

        // The rivet hairline between hours. None before the first: the label
        // column ends where the NOW slab begins.
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
        w.icon = make_column_label(day, &icons_sm, COL_ALUMINUM, x, kIconY,
                                   LAYOUT_HOUR_COL_W);
        w.temp = make_column_label(day, &font_title, COL_ALUMINUM, x, kTempY,
                                   LAYOUT_HOUR_COL_W);
        w.uv = make_column_label(day, &font_micro, COL_SKY, x, kUvY,
                                 LAYOUT_HOUR_COL_W);
        w.precip = make_column_label(day, &font_micro, COL_TURQUOISE, x, kPrecipY,
                                     LAYOUT_HOUR_COL_W);

        w.bar = theme_decor(day);
        lv_obj_set_size(w.bar, 2, kBarMaxH);
        lv_obj_set_pos(w.bar, cx - 1, kBarBottomY - kBarMaxH);
        lv_obj_set_style_bg_opa(w.bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(w.bar, lv_color_hex(COL_TURQUOISE), 0);
        lv_obj_add_flag(w.bar, LV_OBJ_FLAG_HIDDEN);

        // The wind row: an arrow and a number, placed as a pair in update()
        // once the number's width is known. The arrow is one of eight compass
        // glyphs, not a rotated one: rotating any widget in LVGL 8.4 needs an
        // alpha layer, which this 16-bit build cannot make (it logs a warning
        // per frame and draws nothing). It points where the wind is going,
        // which is the way a weathervane's tail points and the way people
        // read an arrow.
        w.wind_arrow = theme_label(day, &icons_xs, COL_SKY, ICON_ARROW_N);
        lv_obj_set_size(w.wind_arrow, 12, 12);
        lv_obj_set_style_text_align(w.wind_arrow, LV_TEXT_ALIGN_CENTER, 0);
        lv_obj_set_pos(w.wind_arrow, cx - 6, kWindY);
        lv_obj_add_flag(w.wind_arrow, LV_OBJ_FLAG_HIDDEN);

        w.wind_speed = theme_label(day, &font_micro, COL_SKY, "");
        lv_obj_set_pos(w.wind_speed, cx, kWindY);

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

    // The clock. Dim oat, the colour the hero takes at night in the design,
    // in the one cut big enough to read from a bunk at backlight 16.
    s_ui.night_clock = theme_label(night, &font_clock, COL_NIGHT_HERO, "");
    lv_obj_set_width(s_ui.night_clock, UI_WIDTH);
    lv_obj_set_style_text_align(s_ui.night_clock, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_ui.night_clock, kClockX, kClockY);

    // The condition, as a glyph alone, four fifths the height of the digits.
    s_ui.night_icon = theme_label(night, &icons_xl, COL_NIGHT_HERO, "");
    lv_obj_set_style_opa(s_ui.night_icon, kNightOpa, 0);
    lv_obj_set_pos(s_ui.night_icon, 0, kClockMidY - icons_xl.line_height / 2);

    // The next sunrise: the one thing worth knowing at 3am is how long until
    // it is not 3am. Icon over time, the pair as tall as the glyph.
    s_ui.night_sunrise_icon = theme_label(night, &icons_md, COL_NIGHT_HERO, ICON_SUNRISE);
    lv_obj_set_style_opa(s_ui.night_sunrise_icon, kNightOpa, 0);
    s_ui.night_sunrise = theme_label(night, &font_body, COL_NIGHT_HERO, "");
    lv_obj_set_style_opa(s_ui.night_sunrise, kNightOpa, 0);

    // --- root: shared between the layouts ------------------------------------
    s_ui.hero = theme_label(parent, &font_hero, COL_OAT, "--");
    lv_obj_set_style_text_letter_space(s_ui.hero, -1, 0);
    lv_obj_set_pos(s_ui.hero, LAYOUT_SAFE, kHeroY);

    s_ui.hero_unit = theme_label(parent, &font_title, COL_OAT, "°");
    s_ui.hero_suffix = theme_label(parent, &font_label, COL_ALUMINUM_DIM, "");

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
    lv_label_set_text(w.uv, "");
    lv_label_set_text(w.precip, "");
    lv_label_set_text(w.wind_speed, "");
    lv_obj_add_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(w.wind_arrow, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
}

void set_wind(HourWidgets &w, int i, const WxHour &h) {
    // A 2mph breeze is not information.
    if (isnan(h.wind) || h.wind < 3.0f) {
        lv_label_set_text(w.wind_speed, "");
        lv_obj_add_flag(w.wind_arrow, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", int(lroundf(h.wind)));
    lv_label_set_text(w.wind_speed, buf);

    // Centre the arrow-gap-number group in the column.
    const int text_w = text_width(buf, &font_micro);
    const int group_w = 12 + 2 + text_w;
    const int start = column_x(i) + (LAYOUT_HOUR_COL_W - group_w) / 2;
    lv_obj_set_pos(w.wind_arrow, start, kWindY + 1);
    lv_obj_set_pos(w.wind_speed, start + 14, kWindY);

    // wind_dir is where the wind comes from; the arrow shows where it goes.
    lv_label_set_text(w.wind_arrow, wind_arrow_glyph((int(lroundf(h.wind_dir)) + 180) % 360));
    lv_obj_clear_flag(w.wind_arrow, LV_OBJ_FLAG_HIDDEN);
}

void set_uv(HourWidgets &w, const WxHour &h) {
    if (isnan(h.uv)) {
        lv_label_set_text(w.uv, "");
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", int(lroundf(h.uv)));
    lv_label_set_text(w.uv, buf);
    lv_obj_set_style_text_color(w.uv, theme_uv_color(h.uv), 0);
}

void set_precip(HourWidgets &w, const WxHour &h) {
    // The row is labelled "%", so the cells hold the bare number. No chance
    // at all is a dash rather than a zero or a blank: a dash says "checked,
    // nothing", where a blank could mean the data never came.
    if (h.precip_prob <= 0) {
        lv_label_set_text(w.precip, "-");
        lv_obj_add_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", h.precip_prob);
    lv_label_set_text(w.precip, buf);

    int bar_h = int(lroundf(float(h.precip_prob) * float(kBarMaxH) / 100.0f));
    if (bar_h < 1) bar_h = 1;
    if (bar_h > kBarMaxH) bar_h = kBarMaxH;
    lv_obj_set_height(w.bar, bar_h);
    lv_obj_set_y(w.bar, kBarBottomY - bar_h);
    lv_obj_clear_flag(w.bar, LV_OBJ_FLAG_HIDDEN);
}

// The sunrise that is still ahead: today's if it has not happened, else
// tomorrow's, else today's plus a day when the forecast is only one day deep.
time_t next_sunrise(const WxData &d, time_t now) {
    if (d.sunrise > now) return d.sunrise;
    if (d.day_count > 1 && d.days[1].sunrise > now) return d.days[1].sunrise;
    return d.sunrise + 24 * 3600;
}

void update_night(const WxData &d, time_t now) {
    char clock[16];
    fmt_clock_hm(now, d.utc_offset, clock, sizeof(clock));
    lv_label_set_text(s_ui.night_clock, clock);

    // The companions hug the measured time, not the label's full width.
    const int half = text_width(clock, &font_clock) / 2;
    const int left  = kClockCenterX - half;
    const int right = kClockCenterX + half;

    lv_label_set_text(s_ui.night_icon, icon_for(wx_icon_for(d.code, d.is_day)));
    lv_obj_set_x(s_ui.night_icon, left - kNightGap - kNightIconW);

    char rise[16];
    fmt_clock(next_sunrise(d, now), d.utc_offset, rise, sizeof(rise));
    lv_label_set_text(s_ui.night_sunrise, rise);

    // Icon over time, the two as tall as the glyph beside the clock and
    // centred on the same band; the column is as wide as the time.
    const int col_w = text_width(rise, &font_body);
    const int col_x = right + kNightGap;
    const int icon_w = 32;
    const int top = kClockMidY - kNightIconW / 2 + 4;
    lv_obj_set_pos(s_ui.night_sunrise_icon, col_x + (col_w - icon_w) / 2, top);
    lv_obj_set_pos(s_ui.night_sunrise, col_x,
                   kClockMidY + kNightIconW / 2 - 4 - font_body.line_height);
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
        lv_label_set_text(s_ui.condition, "");
        lv_label_set_text(s_ui.now_icon, "");
        lv_label_set_text(s_ui.feels, net_connected() ? "GETTING FORECAST" : "SETTING UP");
        // Normally unseen: the boot and setup screens cover this until the
        // first forecast lands. It remains for the case where they cannot.
        snprintf(buf, sizeof(buf), "%s", net_connected() ? weather_status_text()
                                                         : net_state_text());
        lv_label_set_text(s_ui.high, "");
        lv_label_set_text(s_ui.low, "");
        lv_label_set_text(s_ui.place, buf);
        lv_label_set_text(s_ui.status, "");
        lv_label_set_text(s_ui.night_clock, "");
        lv_label_set_text(s_ui.night_icon, "");
        lv_label_set_text(s_ui.night_sunrise, "");
        for (auto &w : s_ui.hours) clear_column(w);
        if (s_ui.ribbon) lv_canvas_fill_bg(s_ui.ribbon, lv_color_hex(COL_GROUND), LV_OPA_TRANSP);
        return;
    }

    const time_t now = time(nullptr);

    // --- Now ----------------------------------------------------------------
    fmt_temp_plain(d.temp, buf, sizeof(buf));
    lv_label_set_text(s_ui.hero, buf);
    lv_label_set_text(s_ui.hero_suffix, d.imperial ? "F" : "C");
    place_hero_marks();

    lv_label_set_text(s_ui.now_icon, icon_for(wx_icon_for(d.code, d.is_day)));
    lv_obj_set_style_text_color(s_ui.now_icon,
                                d.is_day ? lv_color_hex(COL_OAT)
                                         : lv_color_hex(COL_ALUMINUM), 0);

    snprintf(buf, sizeof(buf), "%s", wx_condition_text(d.code));
    upper_ascii(buf);
    lv_label_set_text(s_ui.condition, buf);

    snprintf(buf, sizeof(buf), "FEELS %d°", int(lroundf(d.apparent)));
    lv_label_set_text(s_ui.feels, buf);

    fmt_temp(d.temp_max, buf, sizeof(buf));
    lv_label_set_text(s_ui.high, buf);
    fmt_temp(d.temp_min, buf, sizeof(buf));
    lv_label_set_text(s_ui.low, buf);
    lv_obj_align_to(s_ui.low, s_ui.high, LV_ALIGN_OUT_RIGHT_TOP, 10, 0);

    // The clock first, then the town: the time of day is the thing this line
    // is glanced at for, and the town is the thing that never changes.
    char town[sizeof(d.location)], clock[16];
    snprintf(town, sizeof(town), "%s", d.location[0] ? d.location : "HERE");
    upper_ascii(town);
    fmt_clock(now, d.utc_offset, clock, sizeof(clock));
    snprintf(buf, sizeof(buf), "%s  ·  %s", clock, town);
    lv_label_set_text(s_ui.place, buf);

    // A quiet marker when the network has gone away but the data is still
    // worth showing. Silence would be worse than a stale reading; the age of
    // the data is one tap away, in Quick Settings.
    lv_label_set_text(s_ui.status, net_connected() ? "" : ICON_WIFI_OFF);

    update_night(d, now);

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
        // 30px Jost Medium measures ~52px at "100", wider than the column;
        // the 24px cut measures ~41px. Swap per label at three characters
        // (>= 100 F or <= -10) - both cuts centre in the same column.
        lv_obj_set_style_text_font(
            w.temp, strlen(buf) >= 3 ? &font_hour_narrow : &font_title, 0);

        set_uv(w, h);
        set_precip(w, h);
        set_wind(w, i, h);
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
