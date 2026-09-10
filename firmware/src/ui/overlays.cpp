#include "overlays.h"

#include <math.h>
#include <stdio.h>

#include "config.h"
#include "display/backlight.h"
#include "format.h"
#include "icons.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "screen_manager.h"
#include "theme.h"

namespace {

enum class OverlayKind { None, Hour, Now, QuickSettings };

lv_obj_t   *s_root = nullptr;
OverlayKind s_kind = OverlayKind::None;

// Live-updating widgets in the quick-settings sheet.
lv_obj_t *s_qs_slider = nullptr;
lv_obj_t *s_qs_auto_pill = nullptr;
lv_obj_t *s_qs_updated = nullptr;
lv_obj_t *s_qs_level = nullptr;
bool      s_slider_held = false;

void dismiss_cb(lv_event_t *e) {
    LV_UNUSED(e);
    overlays_dismiss();
}

// Swipes over a control should scroll past it, not press it.
bool swallow_click() { return ui_gesture_recent(); }

lv_obj_t *make_backdrop() {
    overlays_dismiss();

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_GROUND), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, dismiss_cb, LV_EVENT_CLICKED, nullptr);

    // Slide up and fade in together. Motion alone reads as a panel appearing
    // from nowhere; opacity alone reads as a flash.
    lv_obj_set_y(root, 24);
    lv_obj_set_style_opa(root, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_time(&a, UI_OVERLAY_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    lv_anim_set_values(&a, 24, 0);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
        lv_obj_set_y(static_cast<lv_obj_t *>(o), v);
    });
    lv_anim_start(&a);

    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(o), v, 0);
    });
    lv_anim_start(&a);

    s_root = root;
    return root;
}

// A label/value pair. Micro uppercase label above a Body value is the panel's
// one and only way of presenting a datum, used everywhere, so a reader learns
// the pattern once.
void metric(lv_obj_t *parent, int x, int y, const char *label, const char *value,
            uint32_t value_color = COL_ALUMINUM) {
    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, label);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);

    lv_obj_t *v = theme_label(parent, &font_body, value_color, value);
    lv_obj_set_pos(v, x, y + 14);
}

// Same tile, but coloured from the temperature ramp rather than a token.
void metric_c(lv_obj_t *parent, int x, int y, const char *label, const char *value,
              lv_color_t value_color) {
    metric(parent, x, y, label, value);
    lv_obj_t *v = lv_obj_get_child(parent, -1);   // the value label just added
    if (v) lv_obj_set_style_text_color(v, value_color, 0);
}

void header(lv_obj_t *parent, const char *icon_glyph, const char *title,
            const char *subtitle) {
    if (icon_glyph) {
        lv_obj_t *ic = theme_label(parent, &icons_sm, COL_OAT, icon_glyph);
        lv_obj_set_pos(ic, LAYOUT_SAFE, LAYOUT_SAFE + 2);
    }
    lv_obj_t *t = theme_label(parent, &font_title, COL_ALUMINUM, title);
    lv_obj_set_pos(t, LAYOUT_SAFE + (icon_glyph ? 28 : 0), LAYOUT_SAFE - 4);

    if (subtitle) {
        lv_obj_t *s = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, subtitle);
        lv_obj_set_style_text_letter_space(s, 1, 0);
        lv_obj_align(s, LV_ALIGN_TOP_RIGHT, -LAYOUT_SAFE, LAYOUT_SAFE);
    }

    // The rivet seam under the header, echoing the one on the Today screen.
    lv_obj_t *rule = theme_decor(parent);
    lv_obj_add_style(rule, &style_hairline, 0);
    lv_obj_set_size(rule, UI_WIDTH - LAYOUT_SAFE * 2, 1);
    lv_obj_set_pos(rule, LAYOUT_SAFE, LAYOUT_SAFE + 30);
}

void hint(lv_obj_t *parent, const char *text) {
    lv_obj_t *h = theme_label(parent, &font_micro, COL_RIVET, text);
    lv_obj_set_style_text_letter_space(h, 1, 0);
    lv_obj_align(h, LV_ALIGN_BOTTOM_RIGHT, -LAYOUT_SAFE, -LAYOUT_SAFE + 2);
}

// --- quick settings callbacks ---------------------------------------------

void slider_cb(lv_event_t *e) {
    lv_obj_t *slider = lv_event_get_target(e);
    const lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_PRESSED)  s_slider_held = true;
    if (code == LV_EVENT_RELEASED) s_slider_held = false;

    if (code == LV_EVENT_VALUE_CHANGED) {
        backlight_set_manual(uint8_t(lv_slider_get_value(slider)));
        backlight_note_activity();
    }
}

void auto_pill_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (swallow_click()) return;
    backlight_set_auto();
    backlight_note_activity();
}

void refresh_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (swallow_click()) return;
    weather_request_refresh();
    backlight_note_activity();
}

// A pill button. LVGL buttons default to a look that belongs to a different
// design language entirely, so every one of them is restyled from scratch.
lv_obj_t *pill(lv_obj_t *parent, int x, int y, int w, int h, const char *text,
               const lv_font_t *font, uint32_t fg, uint32_t bg,
               lv_event_cb_t cb) {
    lv_obj_t *b = lv_btn_create(parent);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, x, y);
    lv_obj_set_size(b, w, h);
    lv_obj_set_style_radius(b, h / 2, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(COL_SURFACE_HI), LV_STATE_PRESSED);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *l = theme_label(b, font, fg, text);
    lv_obj_center(l);
    return b;
}

}  // namespace

namespace {
// Overlays live on LVGL's top layer, which is created with a NULL parent and so
// does not carry LV_OBJ_FLAG_GESTURE_BUBBLE. That means gestures made over an
// overlay stop at the top layer and never reach the screen's handler, so the
// swipe-to-dismiss bindings have to be registered here rather than inherited.
void layer_gesture_cb(lv_event_t *e) {
    LV_UNUSED(e);
    ui_note_gesture();
    if (s_root != nullptr) overlays_dismiss();
}
}  // namespace

void overlays_init() {
    lv_obj_clear_flag(lv_layer_top(), LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(lv_layer_top(), layer_gesture_cb, LV_EVENT_GESTURE, nullptr);
}

bool overlays_active() { return s_root != nullptr; }

void overlays_dismiss() {
    if (s_root == nullptr) return;
    // Deleted asynchronously because the usual caller is a click handler on the
    // overlay itself, and freeing an object while LVGL is still walking its
    // event chain is a use-after-free.
    lv_obj_del_async(s_root);
    s_root = nullptr;
    s_kind = OverlayKind::None;
    s_qs_slider = s_qs_auto_pill = s_qs_updated = s_qs_level = nullptr;
    s_slider_held = false;
}

// ---------------------------------------------------------------------------
// Hour detail - everything Open-Meteo knows about one hour of the forecast.
// ---------------------------------------------------------------------------
void overlays_show_hour(int hour_index) {
    WxData d;
    weather_snapshot(d);
    if (!d.valid || hour_index < 0 || hour_index >= d.hour_count) return;

    const WxHour &h = d.hours[hour_index];
    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::Hour;

    char title[48], buf[32], sub[48];
    fmt_clock(h.time, d.utc_offset, buf, sizeof(buf));
    snprintf(title, sizeof(title), "%s  ·  %s", buf, wx_condition_text(h.code));
    snprintf(sub, sizeof(sub), "%s", is_current_hour(h.time, d.utc_offset)
                                         ? "THIS HOUR" : "FORECAST");
    header(root, icon_for(wx_icon_for(h.code, h.is_day)), title, sub);

    // Four columns across the 640px width, two rows. Eight metrics is the most
    // this strip can hold without the values crowding their own labels.
    const int col_w = (UI_WIDTH - LAYOUT_SAFE * 2) / 4;
    const int row1 = 52, row2 = 104;

    char v[8][24];
    fmt_temp(h.temp, v[0], sizeof(v[0]));
    fmt_temp(h.apparent, v[1], sizeof(v[1]));
    snprintf(v[2], sizeof(v[2]), "%d%%", h.precip_prob);
    snprintf(v[3], sizeof(v[3]), d.imperial ? "%.2f\"" : "%.1fmm", h.precip_amount);
    snprintf(v[4], sizeof(v[4]), "%.0f %s", h.wind, d.imperial ? "mph" : "km/h");
    snprintf(v[5], sizeof(v[5]), "%.0f %s", h.gust, d.imperial ? "mph" : "km/h");
    snprintf(v[6], sizeof(v[6]), "%.0f%%", h.humidity);
    fmt_temp(h.dew_point, v[7], sizeof(v[7]));

    metric_c(root, LAYOUT_SAFE + col_w * 0, row1, "TEMPERATURE", v[0],
             theme_temp_color(h.temp, d.imperial));
    metric(root, LAYOUT_SAFE + col_w * 1, row1, "FEELS LIKE", v[1]);
    metric(root, LAYOUT_SAFE + col_w * 2, row1, "CHANCE OF RAIN", v[2],
           h.precip_prob >= 10 ? COL_TURQUOISE : COL_ALUMINUM_DIM);
    metric(root, LAYOUT_SAFE + col_w * 3, row1, "AMOUNT", v[3],
           h.precip_amount > 0 ? COL_TURQUOISE : COL_ALUMINUM_DIM);

    char wind_label[24];
    snprintf(wind_label, sizeof(wind_label), "WIND  %s", wx_cardinal(h.wind_dir));
    metric(root, LAYOUT_SAFE + col_w * 0, row2, wind_label, v[4], COL_SKY);
    metric(root, LAYOUT_SAFE + col_w * 1, row2, "GUSTING", v[5], COL_SKY);
    metric(root, LAYOUT_SAFE + col_w * 2, row2, "HUMIDITY", v[6]);
    metric(root, LAYOUT_SAFE + col_w * 3, row2, "DEW POINT", v[7]);

    hint(root, "TAP TO CLOSE");
}

// ---------------------------------------------------------------------------
// Now detail - current conditions, with the day's sun arc as the hero.
// ---------------------------------------------------------------------------
void overlays_show_now() {
    WxData d;
    weather_snapshot(d);
    if (!d.valid) return;

    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::Now;

    header(root, icon_for(wx_icon_for(d.code, d.is_day)),
           wx_condition_text(d.code), d.location);

    // The sun arc. A 180-degree arc from sunrise to sunset with a filled dot at
    // the current position says "where are we in the day" faster than two
    // timestamps do, and it is the single most Airstream-looking element on the
    // panel - an instrument, not a readout.
    const int arc_size = 96;
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_set_size(arc, arc_size, arc_size);
    lv_obj_set_pos(arc, LAYOUT_SAFE + 6, 48);
    lv_arc_set_rotation(arc, 180);
    lv_arc_set_bg_angles(arc, 0, 180);
    lv_arc_set_range(arc, 0, 1000);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_RIVET), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_OAT), LV_PART_INDICATOR);

    int32_t progress = 0;
    if (d.sunrise > 0 && d.sunset > d.sunrise) {
        const time_t now = time(nullptr);
        const float f = float(now - d.sunrise) / float(d.sunset - d.sunrise);
        progress = int32_t(fmaxf(0.0f, fminf(1.0f, f)) * 1000.0f);
    }
    lv_arc_set_value(arc, progress);

    char rise[16], set[16];
    fmt_clock(d.sunrise, d.utc_offset, rise, sizeof(rise));
    fmt_clock(d.sunset, d.utc_offset, set, sizeof(set));

    lv_obj_t *r = theme_label(root, &font_micro, COL_ALUMINUM_DIM, rise);
    lv_obj_set_pos(r, LAYOUT_SAFE, 48 + arc_size / 2 + 6);
    lv_obj_t *s = theme_label(root, &font_micro, COL_ALUMINUM_DIM, set);
    lv_obj_set_pos(s, LAYOUT_SAFE + arc_size - 20, 48 + arc_size / 2 + 6);

    lv_obj_t *sun_icon = theme_label(root, &icons_ui, COL_OAT, ICON_SUN);
    lv_obj_set_pos(sun_icon, LAYOUT_SAFE + arc_size / 2 - 2, 48 + arc_size / 2 - 22);

    // Metrics fill the remaining width to the right of the arc.
    const int gx = LAYOUT_SAFE + arc_size + 34;
    const int col_w = (UI_WIDTH - gx - LAYOUT_SAFE) / 3;
    const int row1 = 52, row2 = 104;

    char v[6][24];
    snprintf(v[0], sizeof(v[0]), "%d° / %d°",
             int(lroundf(d.temp_max)), int(lroundf(d.temp_min)));
    snprintf(v[1], sizeof(v[1]), "%.0f%%", d.humidity);
    snprintf(v[2], sizeof(v[2]), "%.1f", d.uv_index);
    snprintf(v[3], sizeof(v[3]), "%.0f %s", d.gust, d.imperial ? "mph" : "km/h");
    snprintf(v[4], sizeof(v[4]), "%.0f hPa", d.pressure);
    snprintf(v[5], sizeof(v[5]), "%d%%", d.precip_prob_max);

    metric(root, gx + col_w * 0, row1, "HIGH / LOW", v[0], COL_OAT);
    metric(root, gx + col_w * 1, row1, "HUMIDITY", v[1]);
    metric(root, gx + col_w * 2, row1, "UV INDEX", v[2],
           d.uv_index >= 6.0f ? COL_SUNSET : COL_ALUMINUM);

    char gust_label[24];
    snprintf(gust_label, sizeof(gust_label), "GUSTS  %s", wx_cardinal(d.wind_dir));
    metric(root, gx + col_w * 0, row2, gust_label, v[3], COL_SKY);
    metric(root, gx + col_w * 1, row2, "PRESSURE", v[4]);
    metric(root, gx + col_w * 2, row2, "RAIN TODAY", v[5],
           d.precip_prob_max >= 10 ? COL_TURQUOISE : COL_ALUMINUM_DIM);

    hint(root, "TAP TO CLOSE");
}

// ---------------------------------------------------------------------------
// Quick settings - brightness, refresh, and the connection facts you want when
// something is wrong.
// ---------------------------------------------------------------------------
void overlays_show_quick_settings() {
    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::QuickSettings;

    header(root, ICON_SETTINGS, "Quick Settings", nullptr);

    lv_obj_t *bl = theme_label(root, &font_micro, COL_ALUMINUM_DIM, "BRIGHTNESS");
    lv_obj_set_style_text_letter_space(bl, 1, 0);
    lv_obj_set_pos(bl, LAYOUT_SAFE, 50);

    s_qs_level = theme_label(root, &font_micro, COL_ALUMINUM, "");
    lv_obj_set_pos(s_qs_level, LAYOUT_SAFE + 96, 50);

    lv_obj_t *slider = lv_slider_create(root);
    lv_obj_set_pos(slider, LAYOUT_SAFE, 70);
    lv_obj_set_size(slider, 300, 10);
    lv_slider_set_range(slider, BL_LEVEL_MIN, 255);
    lv_slider_set_value(slider, backlight_target_level(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_RIVET), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_OAT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_OAT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_ALL, nullptr);
    s_qs_slider = slider;

    s_qs_auto_pill = pill(root, LAYOUT_SAFE + 316, 64, 78, 24, "AUTO",
                          &font_micro, COL_GROUND, COL_TURQUOISE, auto_pill_cb);

    // Refresh, with the age of the data on the button itself - the question
    // "is this stale?" is the only reason anyone opens this sheet in a hurry.
    // Text only: the pill's font is Jost, which has no icon glyphs, and LVGL
    // logs a missing-glyph warning on every redraw for a codepoint it cannot
    // draw. An icon here needs its own label in icons_ui.
    pill(root, LAYOUT_SAFE + 404, 64, 96, 24, "REFRESH",
         &font_micro, COL_ALUMINUM, COL_SURFACE, refresh_cb);

    s_qs_updated = theme_label(root, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_style_text_letter_space(s_qs_updated, 1, 0);
    lv_obj_set_pos(s_qs_updated, LAYOUT_SAFE + 508, 70);

    // Connection facts.
    char wifi_line[80];
    if (net_connected()) {
        snprintf(wifi_line, sizeof(wifi_line), "%s  ·  %s  ·  %d dBm  ·  %s.local",
                 net_ssid().c_str(), net_ip().c_str(), net_rssi(), OTA_HOSTNAME);
    } else {
        snprintf(wifi_line, sizeof(wifi_line),
                 "NOT CONNECTED  ·  JOIN \"%s\" TO SET UP", net_ap_name());
    }
    lv_obj_t *w = theme_label(root, &font_micro, COL_ALUMINUM_DIM, wifi_line);
    lv_obj_set_style_text_letter_space(w, 1, 0);
    lv_obj_set_pos(w, LAYOUT_SAFE, 116);

    lv_obj_t *ic = theme_label(root, &icons_ui,
                               net_connected() ? COL_TURQUOISE : COL_SUNSET,
                               net_connected() ? ICON_WIFI : ICON_WIFI_OFF);
    lv_obj_align(ic, LV_ALIGN_TOP_RIGHT, -LAYOUT_SAFE, 114);

    hint(root, "SWIPE UP TO CLOSE");
    overlays_tick();
}

void overlays_tick() {
    if (s_kind != OverlayKind::QuickSettings || s_root == nullptr) return;

    // Do not fight the finger that is dragging the slider.
    if (s_qs_slider && !s_slider_held) {
        lv_slider_set_value(s_qs_slider, backlight_target_level(), LV_ANIM_OFF);
    }

    if (s_qs_level) {
        char buf[24];
        const int pct = int(lroundf(backlight_target_level() * 100.0f / 255.0f));
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s_qs_level, buf);
    }

    // The AUTO pill is filled while auto-dimming is engaged and outlined once
    // an explicit level has taken over, so its state is legible without a
    // legend.
    if (s_qs_auto_pill) {
        const bool is_auto = backlight_mode() == BacklightMode::Auto;
        lv_obj_set_style_bg_color(
            s_qs_auto_pill, lv_color_hex(is_auto ? COL_TURQUOISE : COL_SURFACE), 0);
        lv_obj_t *lbl = lv_obj_get_child(s_qs_auto_pill, 0);
        if (lbl) {
            lv_obj_set_style_text_color(
                lbl, lv_color_hex(is_auto ? COL_GROUND : COL_ALUMINUM_DIM), 0);
        }
    }

    if (s_qs_updated) {
        char buf[24];
        fmt_relative(weather_seconds_since_update(), buf, sizeof(buf));
        lv_label_set_text(s_qs_updated, buf);
    }
}
