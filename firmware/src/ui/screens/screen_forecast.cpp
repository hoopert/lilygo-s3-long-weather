#include "screen_forecast.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display/backlight.h"
#include "net/weather.h"
#include "ui/format.h"
#include "ui/icons.h"
#include "ui/overlays.h"
#include "ui/screen_manager.h"
#include "ui/theme.h"

// Ten days across the safe width, each a column of weekday, glyph, high, low
// and the chance of rain. It reads like the hourly strip, one level up, and
// a tap on a day opens Day Detail the way a tap on an hour opens Hour Detail.

namespace {

constexpr int kDayY    = 14;
constexpr int kIconY   = 34;    // 32px glyph
constexpr int kHighY   = 72;    // Title 30
constexpr int kLowY    = 106;   // Body 20, dim
constexpr int kRainY   = 136;   // Micro, turquoise: a droplet or a snowflake, then the chance
constexpr int kSepY    = 24;
constexpr int kSepH    = 128;

struct DayWidgets {
    lv_obj_t *bg;      // today's slab
    lv_obj_t *day;
    lv_obj_t *icon;
    lv_obj_t *high;
    lv_obj_t *low;
    lv_obj_t *rain_icon;   // droplet, or a snowflake when the day's precipitation is snow
    lv_obj_t *rain;        // the chance, as a bare number; "-" when there is none
};

DayWidgets s_days[WX_DAILY_DAYS] = {};

int column_x(int i) { return LAYOUT_SAFE + i * LAYOUT_DAY_COL_W; }

lv_obj_t *column_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color, int x, int y) {
    lv_obj_t *l = theme_label(parent, font, color, "");
    lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(l, LAYOUT_DAY_COL_W);
    lv_obj_set_pos(l, x, y);
    lv_label_set_long_mode(l, LV_LABEL_LONG_CLIP);
    return l;
}

void long_pressed(lv_event_t *e) {
    LV_UNUSED(e);
    weather_request_refresh();
    backlight_note_activity();
}

void day_clicked(lv_event_t *e) {
    if (ui_gesture_recent()) return;   // the tail of a swipe
    backlight_note_activity();
    overlays_show_day(int(intptr_t(lv_event_get_user_data(e))));
}

lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_add_event_cb(parent, long_pressed, LV_EVENT_LONG_PRESSED, nullptr);

    for (int i = 0; i < WX_DAILY_DAYS; i++) {
        DayWidgets &w = s_days[i];
        const int x = column_x(i);

        w.bg = theme_decor(parent);
        lv_obj_set_pos(w.bg, x, 6);
        lv_obj_set_size(w.bg, LAYOUT_DAY_COL_W - 1, UI_HEIGHT - 24);
        lv_obj_set_style_radius(w.bg, 5, 0);
        lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        lv_obj_set_style_bg_color(w.bg, lv_color_hex(COL_SURFACE), 0);

        if (i > 0) {
            lv_obj_t *sep = theme_decor(parent);
            lv_obj_add_style(sep, &style_hairline, 0);
            lv_obj_set_size(sep, 1, kSepH);
            lv_obj_set_pos(sep, x, kSepY);
            lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
        }

        w.day = column_label(parent, &font_micro, COL_ALUMINUM_DIM, x, kDayY);
        lv_obj_set_style_text_letter_space(w.day, 1, 0);
        w.icon = column_label(parent, &icons_md, COL_ALUMINUM, x, kIconY);
        w.high = column_label(parent, &font_title, COL_ALUMINUM, x, kHighY);
        w.low  = column_label(parent, &font_body, COL_ALUMINUM_DIM, x, kLowY);
        w.rain_icon = theme_label(parent, &icons_xs, COL_TURQUOISE, ICON_DROP);
        lv_obj_set_pos(w.rain_icon, x, kRainY + 2);
        w.rain = theme_label(parent, &font_micro, COL_TURQUOISE, "");
        lv_obj_set_pos(w.rain, x, kRainY);

        // The hit target, on top of the column. EVENT_BUBBLE so the long
        // press still reaches the screen's refresh handler.
        lv_obj_t *cell = lv_obj_create(parent);
        lv_obj_remove_style_all(cell);
        lv_obj_set_pos(cell, x, 0);
        lv_obj_set_size(cell, LAYOUT_DAY_COL_W, UI_HEIGHT - 12);
        lv_obj_add_flag(cell, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_EVENT_BUBBLE);
        lv_obj_clear_flag(cell, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_event_cb(cell, day_clicked, LV_EVENT_CLICKED,
                            reinterpret_cast<void *>(intptr_t(i)));
    }
    return parent;
}

// The chance of precipitation, with the kind of precipitation as its icon:
// a snowflake when the day's condition is snow, a droplet otherwise. The
// number carries no percent sign, and no chance at all is a dash. Icon and
// number are centred in the column as a pair.
void set_rain(DayWidgets &w, int i, const WxDay &day) {
    char buf[8];
    if (day.precip_prob_max <= 0) snprintf(buf, sizeof(buf), "-");
    else                          snprintf(buf, sizeof(buf), "%d", day.precip_prob_max);
    lv_label_set_text(w.rain, buf);

    const WxIcon kind = wx_icon_for(day.code, true);
    const bool snow = kind == WxIcon::Snow || kind == WxIcon::HeavySnow;
    lv_label_set_text(w.rain_icon, snow ? ICON_SNOWFLAKE : ICON_DROP);
    lv_obj_clear_flag(w.rain_icon, LV_OBJ_FLAG_HIDDEN);

    const int text_w = int(lv_txt_get_width(buf, uint32_t(strlen(buf)), &font_micro, 0,
                                            LV_TEXT_FLAG_NONE));
    const int group_w = 11 + 3 + text_w;
    const int start = column_x(i) + (LAYOUT_DAY_COL_W - group_w) / 2;
    lv_obj_set_pos(w.rain_icon, start, kRainY + 2);
    lv_obj_set_pos(w.rain, start + 14, kRainY);
}

void update(lv_obj_t *root) {
    LV_UNUSED(root);
    WxData d;
    weather_snapshot(d);
    char buf[16];

    for (int i = 0; i < WX_DAILY_DAYS; i++) {
        DayWidgets &w = s_days[i];
        if (!d.valid || i >= d.day_count) {
            lv_label_set_text(w.day, "");
            lv_label_set_text(w.icon, "");
            lv_label_set_text(w.high, "");
            lv_label_set_text(w.low, "");
            lv_label_set_text(w.rain, "");
            lv_obj_add_flag(w.rain_icon, LV_OBJ_FLAG_HIDDEN);
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
            continue;
        }
        const WxDay &day = d.days[i];

        if (i == 0) {
            lv_label_set_text(w.day, "TODAY");
            lv_obj_set_style_text_color(w.day, lv_color_hex(COL_TURQUOISE), 0);
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_COVER, 0);
        } else {
            fmt_weekday(day.time, d.utc_offset, buf, sizeof(buf));
            lv_label_set_text(w.day, buf);
            lv_obj_set_style_text_color(w.day, lv_color_hex(COL_ALUMINUM_DIM), 0);
            lv_obj_set_style_bg_opa(w.bg, LV_OPA_TRANSP, 0);
        }

        // A day's glyph is its daytime one: nobody plans around the night.
        lv_label_set_text(w.icon, icon_for(wx_icon_for(day.code, true)));
        lv_obj_set_style_text_color(w.icon, theme_temp_color(day.temp_max, d.imperial), 0);

        fmt_temp_plain(day.temp_max, buf, sizeof(buf));
        lv_label_set_text(w.high, buf);
        lv_obj_set_style_text_color(w.high, theme_temp_color(day.temp_max, d.imperial), 0);
        lv_obj_set_style_text_font(w.high, strlen(buf) >= 3 ? &font_hour_narrow : &font_title, 0);

        fmt_temp_plain(day.temp_min, buf, sizeof(buf));
        lv_label_set_text(w.low, buf);

        set_rain(w, i, day);
    }
}

const ScreenDef kDef = {
    "Forecast",
    ICON_WX_PARTLY_DAY,
    create,
    update,
    1,                                             // one to the left of home
    UI_SWIPE_RIGHT | UI_SWIPE_DOWN,
};

}  // namespace

const ScreenDef &screen_forecast_def() { return kDef; }
