#include "overlays.h"

#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "display/backlight.h"
#include "format.h"
#include "icons.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "pressure_logic.h"
#include "screen_manager.h"
#include "theme.h"

namespace {

enum class OverlayKind { None, Hour, Now, Pressure, QuickSettings };

lv_obj_t   *s_root = nullptr;
OverlayKind s_kind = OverlayKind::None;

// Live-updating widgets in Quick Settings.
lv_obj_t *s_qs_bar = nullptr;        // the brightness bar
lv_obj_t *s_qs_fill_clip = nullptr;  // clips the filled part to a square right edge
lv_obj_t *s_qs_fill = nullptr;       // its filled part
lv_obj_t *s_qs_auto_pill = nullptr;
lv_obj_t *s_qs_updated = nullptr;
lv_obj_t *s_qs_level = nullptr;
lv_obj_t *s_qs_bl_status = nullptr;
lv_obj_t *s_qs_bars = nullptr;
lv_obj_t *s_qs_ssid = nullptr;
bool      s_bar_held = false;
int       s_bar_press_x = 0;
bool      s_bar_dragging = false;

// Hour Detail: the centre panel and the neighbour columns either side of it,
// rebuilt in place when a neighbour is tapped.
lv_obj_t *s_hour_panel = nullptr;
lv_obj_t *s_hour_content = nullptr;
lv_obj_t *s_hour_neighbours = nullptr;
int       s_hour_index = -1;
int       s_hour_from_x = 0;     // the column the panel expands out of
int       s_hour_pending = -1;   // a neighbour tap, applied on the next tick

void dismiss_cb(lv_event_t *e) {
    LV_UNUSED(e);
    // A swipe the sheet declined still ends in a release, and LVGL turns that
    // into a click on the backdrop. Without this, a sideways drag on Quick
    // Settings would close it by the back door.
    if (ui_gesture_recent()) return;
    overlays_dismiss();
}

// Swipes over a control should scroll past it, not press it.
bool swallow_click() { return ui_gesture_recent(); }

// The full-screen root every overlay sits on. The detail overlays paint it
// opaque `ground` and slide the whole thing up; the Quick Settings sheet uses
// it as a 70% scrim over the screen beneath and slides its own panel down.
lv_obj_t *make_backdrop(bool scrim = false) {
    overlays_dismiss();

    lv_obj_t *root = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(root);
    lv_obj_set_size(root, UI_WIDTH, UI_HEIGHT);
    lv_obj_set_style_bg_color(root, lv_color_hex(COL_GROUND), 0);
    lv_obj_set_style_bg_opa(root, scrim ? LV_OPA_70 : LV_OPA_COVER, 0);
    lv_obj_clear_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(root, dismiss_cb, LV_EVENT_CLICKED, nullptr);

    // Slide up and fade in together. Motion alone reads as a panel appearing
    // from nowhere; opacity alone reads as a flash.
    lv_obj_set_style_opa(root, LV_OPA_TRANSP, 0);

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, root);
    lv_anim_set_time(&a, UI_OVERLAY_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);

    if (!scrim) {
        lv_obj_set_y(root, 24);
        lv_anim_set_values(&a, 24, 0);
        lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
            lv_obj_set_y(static_cast<lv_obj_t *>(o), v);
        });
        lv_anim_start(&a);
    }

    lv_anim_set_values(&a, LV_OPA_TRANSP, LV_OPA_COVER);
    lv_anim_set_exec_cb(&a, [](void *o, int32_t v) {
        lv_obj_set_style_opa(static_cast<lv_obj_t *>(o), v, 0);
    });
    lv_anim_start(&a);

    s_root = root;
    return root;
}

// A Micro eyebrow, the label above every control on the sheet.
lv_obj_t *eyebrow(lv_obj_t *parent, int x, int y, const char *text,
                  uint32_t color = COL_ALUMINUM_DIM) {
    lv_obj_t *l = theme_label(parent, &font_micro, color, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);
    return l;
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
            const char *subtitle, uint32_t icon_color = COL_OAT) {
    if (icon_glyph) {
        lv_obj_t *ic = theme_label(parent, &icons_sm, icon_color, icon_glyph);
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

// A polyline that owns a copy of its points. lv_line keeps a pointer rather
// than copying, so the copy lives in LVGL's heap and goes with the widget.
lv_obj_t *polyline(lv_obj_t *parent, const lv_point_t *pts, uint16_t n, int width,
                   uint32_t color) {
    lv_point_t *copy = static_cast<lv_point_t *>(lv_mem_alloc(sizeof(lv_point_t) * n));
    if (copy == nullptr) return nullptr;
    memcpy(copy, pts, sizeof(lv_point_t) * n);
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_line_set_points(line, copy, n);
    lv_obj_set_style_line_width(line, width, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_add_event_cb(line, [](lv_event_t *e) {
        lv_mem_free(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, copy);
    return line;
}

// The temperature in Celsius, whatever the panel displays in: the body-effect
// rules are written in degrees C.
float temp_c(const WxData &d) {
    return d.imperial ? (d.temp - 32.0f) * 5.0f / 9.0f : d.temp;
}

// The colour the outlook word, the pressure glyph and the recent trend take:
// sunset for a risk band or any HIGH body effect, aluminum otherwise.
uint32_t outlook_color(const WxData &d) {
    const PressureOutlook o = pressure_outlook(d.pressure_delta_3h);
    const PressureRisks r = pressure_risks(d.pressure_delta_3h, d.pressure, d.humidity, temp_c(d));
    return (o.risk || r.any_high) ? COL_SUNSET : COL_ALUMINUM;
}

void pressure_cell_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (swallow_click()) return;
    backlight_note_activity();
    overlays_show_pressure();
}

// --- quick settings callbacks ---------------------------------------------

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
    theme_press_feedback(b);
    if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *l = theme_label(b, font, fg, text);
    lv_obj_center(l);
    return b;
}

}  // namespace

namespace {
// Overlays live on LVGL's top layer, which is created with a NULL parent and so
// does not carry LV_OBJ_FLAG_GESTURE_BUBBLE. That means gestures made over an
// overlay stop at the top layer and never reach the screen's handler, so they
// are fed into the same dispatcher from here.
void layer_gesture_cb(lv_event_t *e) {
    LV_UNUSED(e);
    ui_handle_swipe(lv_indev_get_gesture_dir(lv_indev_get_act()));
}
}  // namespace

uint8_t overlays_swipes() {
    switch (s_kind) {
        case OverlayKind::Hour:
        case OverlayKind::Now:
        case OverlayKind::Pressure:      return UI_SWIPE_UP | UI_SWIPE_DOWN;
        case OverlayKind::QuickSettings: return UI_SWIPE_UP;
        case OverlayKind::None:          break;
    }
    return UI_SWIPE_NONE;
}

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
    s_qs_bar = s_qs_fill_clip = s_qs_fill = s_qs_auto_pill = s_qs_updated = s_qs_level = nullptr;
    s_qs_bl_status = s_qs_bars = s_qs_ssid = nullptr;
    s_bar_held = s_bar_dragging = false;
    s_hour_panel = s_hour_content = s_hour_neighbours = nullptr;
    s_hour_index = s_hour_pending = -1;
}

// ---------------------------------------------------------------------------
// Hour detail (design/SPEC.md §2) - a 300px panel in the centre of the strip
// that expands out of the column you tapped, with three neighbour hours on
// either side that re-point it. Everything Open-Meteo knows about one hour
// that is worth a glance; cloud cover and dew point are not.
// ---------------------------------------------------------------------------
namespace {

constexpr int kPanelX      = 170;
constexpr int kPanelW      = 300;
constexpr int kPanelPad    = 12;
constexpr int kPanelRuleY  = 64;
constexpr int kPanelColX[3] = {12, 116, 214};
constexpr int kPanelRow1Y  = 68;
constexpr int kPanelRow2Y  = 120;
constexpr int kPanelValueDY = 14;

constexpr int kNeighbourW   = 43;
constexpr int kNeighbourLeftX[3]  = {20, 63, 106};    // hour-3, hour-2, hour-1
constexpr int kNeighbourRightX[3] = {491, 534, 577};  // hour+1, hour+2, hour+3
constexpr int kNeighbourHourY = 36;
constexpr int kNeighbourIconY = 56;
constexpr int kNeighbourTempY = 82;

// The tapped neighbour is one of the objects a re-render deletes, so the
// re-render waits for the next tick rather than pulling the floor out from
// under the event that asked for it.
void neighbour_clicked(lv_event_t *e) {
    if (swallow_click()) return;
    backlight_note_activity();
    s_hour_pending = int(intptr_t(lv_event_get_user_data(e)));
}

// Micro label over a Title 30 value, with an optional 12px suffix hanging off
// the value's baseline - the unit, or the rain accumulation after its chance.
void panel_metric(lv_obj_t *parent, int x, int y, const char *label,
                  const char *value, lv_color_t color, const char *suffix) {
    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, label);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);

    lv_obj_t *v = theme_label(parent, &font_title, COL_ALUMINUM, value);
    lv_obj_set_style_text_color(v, color, 0);
    lv_obj_set_pos(v, x, y + kPanelValueDY);

    if (suffix && *suffix) {
        lv_obj_t *sfx = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, suffix);
        lv_obj_set_style_text_letter_space(sfx, 1, 0);
        lv_obj_align_to(sfx, v, LV_ALIGN_OUT_RIGHT_BOTTOM, 4, -6);
    }
}

// One neighbour column: hour, glyph, temperature, and a full-height tap target.
void neighbour_column(lv_obj_t *parent, int x, const WxData &d, int index) {
    if (index < 0 || index >= d.hour_count) return;
    const WxHour &h = d.hours[index];
    const lv_color_t tc = theme_temp_color(h.temp, d.imperial);
    char buf[16];

    lv_obj_t *hour = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, "");
    if (is_current_hour(h.time, d.utc_offset)) {
        lv_label_set_text(hour, "NOW");
        lv_obj_set_style_text_color(hour, lv_color_hex(COL_TURQUOISE), 0);
    } else {
        fmt_hour(h.time, d.utc_offset, buf, sizeof(buf));
        lv_label_set_text(hour, buf);
    }
    lv_obj_set_style_text_letter_space(hour, 1, 0);
    lv_obj_set_style_text_align(hour, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(hour, kNeighbourW);
    lv_obj_set_pos(hour, x, kNeighbourHourY);

    lv_obj_t *icon = theme_label(parent, &icons_sm, COL_ALUMINUM,
                                 icon_for(wx_icon_for(h.code, h.is_day)));
    lv_obj_set_style_text_color(icon, tc, 0);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(icon, kNeighbourW);
    lv_obj_set_pos(icon, x, kNeighbourIconY);

    fmt_temp_plain(h.temp, buf, sizeof(buf));
    lv_obj_t *temp = theme_label(parent, &font_title, COL_ALUMINUM, buf);
    lv_obj_set_style_text_font(temp, strlen(buf) >= 3 ? &font_hour_narrow : &font_title, 0);
    lv_obj_set_style_text_color(temp, tc, 0);
    lv_obj_set_style_text_align(temp, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_width(temp, kNeighbourW);
    lv_label_set_long_mode(temp, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(temp, x, kNeighbourTempY);

    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_set_pos(hit, x, 0);
    lv_obj_set_size(hit, kNeighbourW, UI_HEIGHT);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hit, neighbour_clicked, LV_EVENT_CLICKED,
                        reinterpret_cast<void *>(intptr_t(index)));
}

void hour_panel_render(const WxData &d, int index) {
    const WxHour &h = d.hours[index];
    const bool now = is_current_hour(h.time, d.utc_offset);
    const lv_color_t tc = theme_temp_color(h.temp, d.imperial);
    char buf[48], val[24], sfx[24];

    lv_obj_clean(s_hour_content);
    lv_obj_clean(s_hour_neighbours);

    // --- neighbours ---------------------------------------------------------
    for (int k = 0; k < 3; k++) {
        neighbour_column(s_hour_neighbours, kNeighbourLeftX[k], d, index - 3 + k);
        neighbour_column(s_hour_neighbours, kNeighbourRightX[k], d, index + 1 + k);
    }
    const int hair_x[4] = {kNeighbourLeftX[1] - 1, kNeighbourLeftX[2] - 1,
                           kNeighbourRightX[1] - 1, kNeighbourRightX[2] - 1};
    for (int x : hair_x) {
        lv_obj_t *sep = theme_decor(s_hour_neighbours);
        lv_obj_add_style(sep, &style_hairline, 0);
        lv_obj_set_size(sep, 1, kNeighbourTempY + 30 - kNeighbourHourY);
        lv_obj_set_pos(sep, x, kNeighbourHourY);
        lv_obj_set_style_bg_opa(sep, LV_OPA_50, 0);
    }

    // --- panel header -------------------------------------------------------
    lv_obj_t *c = s_hour_content;
    snprintf(buf, sizeof(buf), "%s  ·  %s", now ? "THIS HOUR" : "FORECAST",
             wx_condition_text(h.code));
    lv_obj_t *eye = theme_label(c, &font_micro, COL_ALUMINUM_DIM, buf);
    lv_obj_set_style_text_letter_space(eye, 1, 0);
    lv_obj_set_width(eye, kPanelW - kPanelPad * 2 - 30);
    lv_label_set_long_mode(eye, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(eye, kPanelPad, 10);

    fmt_clock(h.time, d.utc_offset, buf, sizeof(buf));
    lv_obj_t *title = theme_label(c, &font_title, COL_ALUMINUM, buf);
    lv_obj_set_pos(title, kPanelPad, 26);

    lv_obj_t *glyph = theme_label(c, &icons_sm, COL_ALUMINUM,
                                  icon_for(wx_icon_for(h.code, h.is_day)));
    lv_obj_set_style_text_color(glyph, tc, 0);
    lv_obj_align(glyph, LV_ALIGN_TOP_RIGHT, -kPanelPad, 14);

    lv_obj_t *rule = theme_decor(c);
    lv_obj_add_style(rule, &style_hairline, 0);
    lv_obj_set_size(rule, kPanelW - kPanelPad * 2 - 2, 1);
    lv_obj_set_pos(rule, kPanelPad, kPanelRuleY);

    // --- metric grid, 3 x 2 -------------------------------------------------
    fmt_temp(h.temp, val, sizeof(val));
    panel_metric(c, kPanelColX[0], kPanelRow1Y, "TEMP", val, tc, nullptr);

    fmt_temp(h.apparent, val, sizeof(val));
    panel_metric(c, kPanelColX[1], kPanelRow1Y, "FEELS LIKE", val,
                 lv_color_hex(COL_ALUMINUM), nullptr);

    snprintf(val, sizeof(val), "%.0f%%", h.humidity);
    panel_metric(c, kPanelColX[2], kPanelRow1Y, "HUMIDITY", val,
                 lv_color_hex(COL_ALUMINUM), nullptr);

    snprintf(val, sizeof(val), "%d%%", h.precip_prob);
    if (h.precip_amount > 0.0f) {
        snprintf(sfx, sizeof(sfx), d.imperial ? "%.2f\"" : "%.1fMM", h.precip_amount);
    } else {
        sfx[0] = '\0';
    }
    panel_metric(c, kPanelColX[0], kPanelRow2Y, "RAIN CHANCE", val,
                 lv_color_hex(h.precip_prob >= 10 ? COL_TURQUOISE : COL_ALUMINUM_DIM), sfx);

    const char *unit = d.imperial ? "MPH" : "KM/H";
    snprintf(buf, sizeof(buf), "WIND  %s", wx_cardinal(h.wind_dir));
    snprintf(val, sizeof(val), "%.0f", h.wind);
    panel_metric(c, kPanelColX[1], kPanelRow2Y, buf, val, lv_color_hex(COL_SKY), unit);

    snprintf(val, sizeof(val), "%.0f", h.gust);
    panel_metric(c, kPanelColX[2], kPanelRow2Y, "GUSTS", val, lv_color_hex(COL_SKY), unit);
}

void panel_geom_cb(void *o, int32_t v) {
    // v runs 0..256: the panel's rectangle interpolates from the tapped
    // column's to its resting place, and the content fades up behind it.
    lv_obj_t *panel = static_cast<lv_obj_t *>(o);
    const int from_x = s_hour_from_x;
    const int from_w = LAYOUT_HOUR_COL_W;
    const int x = from_x + ((kPanelX - from_x) * v) / 256;
    const int w = from_w + ((kPanelW - from_w) * v) / 256;
    lv_obj_set_pos(panel, x, 0);
    lv_obj_set_width(panel, w);
    if (s_hour_content) lv_obj_set_style_opa(s_hour_content, lv_opa_t(v > 255 ? 255 : v), 0);
}

}  // namespace

void overlays_show_hour(int hour_index) {
    WxData d;
    weather_snapshot(d);
    if (!d.valid || hour_index < 0 || hour_index >= d.hour_count) return;

    // Already open: re-point the panel without closing it.
    if (s_kind == OverlayKind::Hour && s_hour_panel) {
        s_hour_index = hour_index;
        hour_panel_render(d, hour_index);
        return;
    }

    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::Hour;
    s_hour_index = hour_index;

    s_hour_neighbours = theme_decor(root);
    lv_obj_set_pos(s_hour_neighbours, 0, 0);
    lv_obj_set_size(s_hour_neighbours, UI_WIDTH, UI_HEIGHT);

    // The panel: surface, rivet sides, turquoise top edge, a soft shadow so it
    // sits above the neighbours rather than between them. Clickable so a tap
    // anywhere on it dismisses; no printed hint - it is learned in one tap.
    lv_obj_t *panel = lv_obj_create(root);
    lv_obj_remove_style_all(panel);
    lv_obj_set_size(panel, kPanelW, UI_HEIGHT);
    lv_obj_set_pos(panel, kPanelX, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(COL_RIVET), 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_side(panel, LV_BORDER_SIDE_LEFT | LV_BORDER_SIDE_RIGHT, 0);
    lv_obj_set_style_shadow_color(panel, lv_color_hex(COL_GROUND), 0);
    lv_obj_set_style_shadow_opa(panel, LV_OPA_70, 0);
    lv_obj_set_style_shadow_width(panel, 18, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(panel, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(panel, dismiss_cb, LV_EVENT_CLICKED, nullptr);
    s_hour_panel = panel;

    lv_obj_t *top = theme_decor(panel);
    lv_obj_set_size(top, LV_PCT(100), 2);
    lv_obj_set_pos(top, 0, 0);
    lv_obj_set_style_bg_opa(top, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(top, lv_color_hex(COL_TURQUOISE), 0);

    s_hour_content = theme_decor(panel);
    lv_obj_set_pos(s_hour_content, 0, 0);
    lv_obj_set_size(s_hour_content, kPanelW, UI_HEIGHT);

    hour_panel_render(d, hour_index);

    // Expand out of the tapped column. Hours past the strip (a neighbour of
    // a neighbour) have no column; they open from the panel's own place.
    s_hour_from_x = hour_index < WX_HOURLY_SLOTS
                        ? LAYOUT_HOURS_X + hour_index * LAYOUT_HOUR_COL_W
                        : kPanelX;
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, panel);
    lv_anim_set_time(&a, UI_OVERLAY_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_values(&a, 0, 256);
    lv_anim_set_exec_cb(&a, panel_geom_cb);
    lv_anim_start(&a);
    panel_geom_cb(panel, 0);
}

// ---------------------------------------------------------------------------
// Now detail (design/SPEC.md §3) - current conditions, with the day's sun arc
// as the hero and a spoken pressure trend that opens its own overlay.
// ---------------------------------------------------------------------------
namespace {

constexpr int kArcSize    = 192;
constexpr int kArcCX      = 120;    // arc centre; the baseline is the bottom edge
constexpr int kArcCY      = 162;
constexpr int kArcR       = 94;     // where the 3px track's centre line runs
constexpr int kPuckSize   = 28;
constexpr int kNowColX[3] = {248, 388, 512};
constexpr int kNowRow1Y   = 54;
constexpr int kNowRow2Y   = 112;
constexpr int kSparkW     = 56;
constexpr int kSparkH     = 14;
constexpr int kSparkPts   = 7;      // the last six hours

// Micro label, Title 30 value, optional 12px suffix - the Now Detail cell.
void now_metric(lv_obj_t *parent, int x, int y, const char *label, const char *value,
                uint32_t color, const char *suffix) {
    panel_metric(parent, x, y, label, value, lv_color_hex(color), suffix);
}

// The pressure cell: reading subdued in the label, outlook word in Body 20,
// and a 56x14 sparkline of the last six hours in the word's colour. The whole
// cell is the tap target for Pressure Detail; no affordance chrome.
void pressure_cell(lv_obj_t *parent, int x, int y, const WxData &d) {
    const PressureOutlook o = pressure_outlook(d.pressure_delta_3h);
    const uint32_t color = outlook_color(d);
    char buf[32];

    if (isnan(d.pressure)) snprintf(buf, sizeof(buf), "PRESSURE");
    else snprintf(buf, sizeof(buf), "PRESSURE  %.0f", d.pressure);
    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, buf);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);

    lv_obj_t *w = theme_label(parent, &font_body, color, o.word);
    lv_obj_set_pos(w, x, y + kPanelValueDY);

    const int n = d.pressure_history_count < kSparkPts ? d.pressure_history_count : kSparkPts;
    if (n >= 2) {
        const float *h = d.pressure_history + (d.pressure_history_count - n);
        float lo = INFINITY, hi = -INFINITY;
        for (int i = 0; i < n; i++) {
            if (isnan(h[i])) continue;
            lo = fminf(lo, h[i]);
            hi = fmaxf(hi, h[i]);
        }
        if (isfinite(lo) && isfinite(hi)) {
            // A flat six hours is a flat line through the middle, not noise
            // stretched to fill 14px.
            float span = hi - lo;
            if (span < 2.0f) { const float mid = (hi + lo) * 0.5f; lo = mid - 1.0f; span = 2.0f; }
            lv_point_t pts[kSparkPts];
            const int y0 = y + kPanelValueDY + 24;
            float last = h[0];
            for (int i = 0; i < n; i++) {
                if (!isnan(h[i])) last = h[i];
                pts[i].x = lv_coord_t(x + (kSparkW * i) / (n - 1));
                pts[i].y = lv_coord_t(y0 + 1 + int((kSparkH - 2) * (1.0f - (last - lo) / span)));
            }
            polyline(parent, pts, uint16_t(n), 2, color);
        }
    }

    lv_obj_t *hit = lv_obj_create(parent);
    lv_obj_remove_style_all(hit);
    lv_obj_set_pos(hit, x - 6, y - 6);
    lv_obj_set_size(hit, 124, 66);
    lv_obj_add_flag(hit, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(hit, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(hit, pressure_cell_cb, LV_EVENT_CLICKED, nullptr);
}

}  // namespace

void overlays_show_now() {
    WxData d;
    weather_snapshot(d);
    if (!d.valid) return;

    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::Now;

    char clock[16], sub[72], buf[32];
    fmt_clock(time(nullptr), d.utc_offset, clock, sizeof(clock));
    snprintf(sub, sizeof(sub), "%s  ·  %s", d.location[0] ? d.location : "HERE", clock);
    for (char *c = sub; *c; c++) *c = char(toupper(static_cast<unsigned char>(*c)));
    header(root, icon_for(wx_icon_for(d.code, d.is_day)), wx_condition_text(d.code), sub);

    // The sun arc. A 180-degree arc from sunrise to sunset with the sun itself
    // riding it says "where are we in the day" faster than two timestamps do,
    // and it is the single most Airstream-looking element on the panel - an
    // instrument, not a readout. The lower half of the circle is below the
    // screen; only the daylight half shows.
    lv_obj_t *arc = lv_arc_create(root);
    lv_obj_set_size(arc, kArcSize, kArcSize);
    lv_obj_set_pos(arc, kArcCX - kArcSize / 2, kArcCY - kArcSize / 2);
    lv_arc_set_rotation(arc, 180);
    lv_arc_set_bg_angles(arc, 0, 180);
    lv_arc_set_range(arc, 0, 1000);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_RIVET), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_OAT), LV_PART_INDICATOR);

    float f = 0.0f;
    if (d.sunrise > 0 && d.sunset > d.sunrise) {
        const time_t now = time(nullptr);
        f = fmaxf(0.0f, fminf(1.0f, float(now - d.sunrise) / float(d.sunset - d.sunrise)));
    }
    lv_arc_set_value(arc, int32_t(f * 1000.0f));

    // The sun: a 20px glyph on a 28px ground puck, so the track passes behind
    // it. Angle 180 is the left end (sunrise), 270 the top, 360 the right.
    const float a = (180.0f + 180.0f * f) * float(M_PI) / 180.0f;
    const int sx = kArcCX + int(lroundf(kArcR * cosf(a)));
    const int sy = kArcCY + int(lroundf(kArcR * sinf(a)));
    lv_obj_t *puck = theme_decor(root);
    lv_obj_set_size(puck, kPuckSize, kPuckSize);
    lv_obj_set_style_radius(puck, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(puck, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(puck, lv_color_hex(COL_GROUND), 0);
    lv_obj_set_pos(puck, sx - kPuckSize / 2, sy - kPuckSize / 2);
    lv_obj_t *sun = theme_label(puck, &icons_sm, COL_OAT, ICON_SUN);
    lv_obj_center(sun);

    lv_obj_t *dl = theme_label(root, &font_label, COL_OAT, d.is_day ? "DAYLIGHT" : "NIGHT");
    lv_obj_set_style_text_letter_space(dl, 1, 0);
    lv_obj_set_width(dl, 120);
    lv_obj_set_style_text_align(dl, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(dl, kArcCX - 60, 108);

    char rise[16], set[16];
    fmt_clock(d.sunrise, d.utc_offset, rise, sizeof(rise));
    fmt_clock(d.sunset, d.utc_offset, set, sizeof(set));
    lv_obj_t *r = theme_label(root, &font_micro, COL_ALUMINUM_DIM, rise);
    lv_obj_set_style_text_letter_space(r, 1, 0);
    lv_obj_set_pos(r, 30, 148);
    lv_obj_t *st = theme_label(root, &font_micro, COL_ALUMINUM_DIM, set);
    lv_obj_set_style_text_letter_space(st, 1, 0);
    lv_obj_set_pos(st, 158, 148);

    // --- metrics, 3 x 2 -------------------------------------------------------
    snprintf(buf, sizeof(buf), "%d° / %d°", int(lroundf(d.temp_max)), int(lroundf(d.temp_min)));
    now_metric(root, kNowColX[0], kNowRow1Y, "HIGH / LOW", buf, COL_OAT, nullptr);

    snprintf(buf, sizeof(buf), "%.0f%%", d.humidity);
    now_metric(root, kNowColX[1], kNowRow1Y, "HUMIDITY", buf, COL_ALUMINUM, nullptr);

    snprintf(buf, sizeof(buf), "%.1f", d.uv_index);
    now_metric(root, kNowColX[2], kNowRow1Y, "UV INDEX", buf,
               d.uv_index >= 6.0f ? COL_SUNSET : COL_ALUMINUM, nullptr);

    char label[24];
    snprintf(label, sizeof(label), "GUSTS  %s", wx_cardinal(d.wind_dir));
    snprintf(buf, sizeof(buf), "%.0f", d.gust);
    now_metric(root, kNowColX[0], kNowRow2Y, label, buf, COL_SKY, d.imperial ? "MPH" : "KM/H");

    pressure_cell(root, kNowColX[1], kNowRow2Y, d);

    // Open-Meteo reports visibility in metres whatever the unit setting.
    const float vis = d.imperial ? d.visibility / 1609.34f : d.visibility / 1000.0f;
    snprintf(buf, sizeof(buf), vis >= 10.0f ? "%.0f" : "%.1f", vis);
    now_metric(root, kNowColX[2], kNowRow2Y, "VISIBILITY", buf, COL_ALUMINUM,
               d.imperial ? "MI" : "KM");
}

// ---------------------------------------------------------------------------
// Pressure detail (design/SPEC.md §3B) - the outlook in words, the last 24
// hours as a graph, and what the trend means for a body.
// ---------------------------------------------------------------------------
namespace {

constexpr int kPxSeam1     = 160;
constexpr int kPxSeam2     = 432;
constexpr int kPxSeamY     = 52;
constexpr int kPxSeamH     = 108;
constexpr int kPxPlotX     = 180;
constexpr int kPxPlotY     = 52;
constexpr int kPxPlotW     = 240;
constexpr int kPxPlotH     = 90;
constexpr float kPxTop     = 1022.0f;   // hPa at the plot's top edge
constexpr float kPxBottom  = 1006.0f;   // and its bottom: 90px / 16 hPa
constexpr int kPxBodyX     = 448;
constexpr int kPxBodyLblX  = 472;
constexpr int kPxBodyRight = 630;
constexpr int kPxRowY[4]   = {64, 89, 114, 139};

int plot_y(float hpa) {
    if (isnan(hpa)) hpa = kPxBottom;
    float y = kPxPlotY + (kPxTop - hpa) * (float(kPxPlotH) / (kPxTop - kPxBottom));
    if (y < kPxPlotY) y = kPxPlotY;
    if (y > kPxPlotY + kPxPlotH) y = kPxPlotY + kPxPlotH;
    return int(lroundf(y));
}

void body_row(lv_obj_t *parent, int y, const char *glyph, const char *label, RiskLevel level) {
    lv_obj_t *g = theme_label(parent, &icons_ui, COL_ALUMINUM_DIM, glyph);
    lv_obj_set_pos(g, kPxBodyX, y);

    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM, label);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, kPxBodyLblX, y + 2);

    uint32_t color = COL_TURQUOISE;
    if (level == RiskLevel::Medium) color = COL_OAT;
    if (level == RiskLevel::High)   color = COL_SUNSET;

    lv_obj_t *w = theme_label(parent, &font_micro, color, risk_word(level));
    lv_obj_set_style_text_letter_space(w, 1, 0);
    lv_obj_align(w, LV_ALIGN_TOP_RIGHT, kPxBodyRight - UI_WIDTH, y + 2);

    lv_obj_t *dot = theme_decor(parent);
    lv_obj_set_size(dot, 7, 7);
    lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
    lv_obj_align_to(dot, w, LV_ALIGN_OUT_LEFT_MID, -6, 0);
}

}  // namespace

void overlays_show_pressure() {
    WxData d;
    weather_snapshot(d);
    if (!d.valid) return;

    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::Pressure;

    const PressureOutlook o = pressure_outlook(d.pressure_delta_3h);
    const PressureRisks r = pressure_risks(d.pressure_delta_3h, d.pressure, d.humidity, temp_c(d));
    const uint32_t color = outlook_color(d);
    char buf[48];

    if (isnan(d.pressure_delta_3h)) {
        snprintf(buf, sizeof(buf), "%.0f HPA", d.pressure);
    } else {
        snprintf(buf, sizeof(buf), "%.0f HPA  ·  %+.1f IN 3H", d.pressure, d.pressure_delta_3h);
    }
    header(root, ICON_SPEED, "Pressure", buf, color);

    for (int x : {kPxSeam1, kPxSeam2}) {
        lv_obj_t *seam = theme_decor(root);
        lv_obj_add_style(seam, &style_hairline, 0);
        lv_obj_set_size(seam, 1, kPxSeamH);
        lv_obj_set_pos(seam, x, kPxSeamY);
    }

    // --- outlook ------------------------------------------------------------
    lv_obj_t *word = theme_label(root, &font_title, color, o.word);
    lv_obj_set_width(word, kPxSeam1 - LAYOUT_SAFE - 4);
    lv_label_set_long_mode(word, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(word, LAYOUT_SAFE, 54);
    lv_obj_t *cap = theme_label(root, &font_micro, COL_ALUMINUM_DIM, o.caption);
    lv_obj_set_style_text_letter_space(cap, 1, 0);
    lv_obj_set_pos(cap, LAYOUT_SAFE, 92);

    // --- 24h graph ----------------------------------------------------------
    for (float g : {1020.0f, 1015.0f, 1010.0f}) {
        lv_obj_t *grid = theme_decor(root);
        lv_obj_add_style(grid, &style_hairline, 0);
        lv_obj_set_size(grid, kPxPlotW, 1);
        lv_obj_set_pos(grid, kPxPlotX, plot_y(g));
        snprintf(buf, sizeof(buf), "%.0f", g);
        lv_obj_t *gl = theme_label(root, &font_micro, COL_NIGHT_DIM, buf);
        lv_obj_set_pos(gl, kPxPlotX + 3, plot_y(g) - 13);
    }
    struct { const char *text; int x; uint32_t color; } ticks[3] = {
        {"-24H", kPxPlotX, COL_ALUMINUM_DIM}, {"-12H", 288, COL_ALUMINUM_DIM}, {"NOW", 392, COL_TURQUOISE}};
    for (auto &t : ticks) {
        lv_obj_t *tl = theme_label(root, &font_micro, t.color, t.text);
        lv_obj_set_style_text_letter_space(tl, 1, 0);
        lv_obj_set_pos(tl, t.x, 150);
    }

    const int n = d.pressure_history_count;
    if (n >= 2) {
        // Right-aligned on NOW, one point per hour at 10px, so a short history
        // still ends at the right edge. The last three hours take the outlook
        // colour; the joint point belongs to both segments.
        lv_point_t pts[WX_PRESSURE_HISTORY];
        float last = NAN;
        for (int i = 0; i < n; i++) {
            if (!isnan(d.pressure_history[i])) last = d.pressure_history[i];
            pts[i].x = lv_coord_t(kPxPlotX + kPxPlotW - (n - 1 - i) * 10);
            pts[i].y = lv_coord_t(plot_y(last));
        }
        const int split = n > 4 ? n - 4 : 0;
        if (split > 0) polyline(root, pts, uint16_t(split + 1), 2, COL_ALUMINUM_DIM);
        polyline(root, pts + split, uint16_t(n - split), 2, color);
    }

    // --- body effects -------------------------------------------------------
    lv_obj_t *eye = theme_label(root, &font_micro, COL_ALUMINUM_DIM, "BODY EFFECTS");
    lv_obj_set_style_text_letter_space(eye, 1, 0);
    lv_obj_set_pos(eye, kPxBodyX, 48);
    body_row(root, kPxRowY[0], ICON_RHEUMATOLOGY, "JOINT PAIN",   r.joint_pain);
    body_row(root, kPxRowY[1], ICON_NEUROLOGY,    "MIGRAINE",     r.migraine);
    body_row(root, kPxRowY[2], ICON_HEARING,      "SINUS & EARS", r.sinus_ears);
    body_row(root, kPxRowY[3], ICON_CARDIOLOGY,   "HEART STRAIN", r.heart_strain);
}

// ---------------------------------------------------------------------------
// Quick settings - the whole screen: a brightness bar you can tap or drag,
// then the forecast's age with a refresh, and the Wi-Fi. Nothing here that
// the System screen already says.
// ---------------------------------------------------------------------------
namespace {

constexpr int kQsRow1Y      = 14;    // BRIGHTNESS eyebrow, level, and the status line
constexpr int kQsBarY       = 34;
constexpr int kQsBarH       = 52;    // a fingertip, and a little
constexpr int kQsAutoGap    = 10;    // between the bar's right end and the AUTO button
constexpr int kQsBarW       = UI_WIDTH - LAYOUT_SAFE * 2 - kQsBarH - kQsAutoGap;   // 558
constexpr int kQsRow2Y      = 112;   // FORECAST / WI-FI eyebrows
constexpr int kQsValueY     = 130;
constexpr int kQsWifiX      = 400;
constexpr int kQsHandleY    = 170;
constexpr int kQsDragSlop   = 6;     // px of travel before a press becomes a drag

const uint8_t kWifiBarHeights[4] = {6, 10, 14, 18};

// --- the bar's scale ----------------------------------------------------------
//
// Nine divisions, each a preset from BL_PRESETS. A preset fills the bar to the
// END of its division, so tapping the third division lights three of them;
// a drag runs continuously along the same scale, with the very left edge at
// BL_LEVEL_MIN. fraction_for() and level_for() are inverses.
float fraction_for(uint8_t level) {
    const uint8_t *p = backlight_presets();
    const int n = BL_PRESET_COUNT;
    if (level <= BL_LEVEL_MIN) return 0.0f;
    uint8_t lo = BL_LEVEL_MIN;
    for (int k = 0; k < n; k++) {
        if (level <= p[k]) {
            const float t = (p[k] == lo) ? 1.0f : float(level - lo) / float(p[k] - lo);
            return (float(k) + t) / float(n);
        }
        lo = p[k];
    }
    return 1.0f;
}

uint8_t level_for(float fraction) {
    const uint8_t *p = backlight_presets();
    const int n = BL_PRESET_COUNT;
    if (fraction <= 0.0f) return BL_LEVEL_MIN;
    if (fraction >= 1.0f) return p[n - 1];
    const float pos = fraction * float(n);
    int k = int(pos);
    if (k >= n) k = n - 1;
    const float t = pos - float(k);
    const uint8_t lo = (k == 0) ? BL_LEVEL_MIN : p[k - 1];
    return uint8_t(lroundf(float(lo) + (float(p[k]) - float(lo)) * t));
}

// The fill is a pill as wide as the level, drawn inside a plain rectangle
// that clips it: its left end shows rounded, its right end is cut square at
// the level - until the bar is full, when the pill is exactly the bar's width
// and its own rounded right end shows, as if the whole thing were masked.
void bar_show_level(uint8_t level) {
    if (s_qs_fill && s_qs_fill_clip) {
        const int r = kQsBarH / 2;
        int w = int(lroundf(fraction_for(level) * float(kQsBarW)));
        if (level <= BL_LEVEL_MIN) w = 0;
        else if (w < r) w = r;
        lv_obj_set_width(s_qs_fill_clip, w);
        lv_obj_set_width(s_qs_fill, w >= kQsBarW ? kQsBarW : w + r);
    }
    if (s_qs_level) {
        char buf[8];
        snprintf(buf, sizeof(buf), "%d%%", int(lroundf(level * 100.0f / 255.0f)));
        lv_label_set_text(s_qs_level, buf);
    }
}

// Where the finger is, as a fraction of the bar's width.
float bar_fraction_under_finger() {
    lv_indev_t *indev = lv_indev_get_act();
    if (indev == nullptr || s_qs_bar == nullptr) return 0.0f;
    lv_point_t pt;
    lv_indev_get_point(indev, &pt);
    lv_area_t a;
    lv_obj_get_coords(s_qs_bar, &a);
    float f = float(pt.x - a.x1) / float(kQsBarW);
    if (f < 0.0f) f = 0.0f;
    if (f > 1.0f) f = 1.0f;
    return f;
}

// A press lands on a division and takes its preset at once; a drag past a few
// pixels of slop turns into fine tuning along the whole scale. Either way the
// backlight and the bar move with the finger, not after it.
void bar_cb(lv_event_t *e) {
    const lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        s_bar_held = true;
        s_bar_dragging = false;
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        s_bar_press_x = pt.x;
        const float f = bar_fraction_under_finger();
        int k = int(f * BL_PRESET_COUNT);
        if (k >= BL_PRESET_COUNT) k = BL_PRESET_COUNT - 1;
        const uint8_t level = backlight_presets()[k];
        backlight_set_manual(level);
        bar_show_level(level);
    } else if (code == LV_EVENT_PRESSING) {
        lv_point_t pt;
        lv_indev_get_point(lv_indev_get_act(), &pt);
        if (!s_bar_dragging && abs(int(pt.x) - s_bar_press_x) < kQsDragSlop) return;
        s_bar_dragging = true;
        const uint8_t level = level_for(bar_fraction_under_finger());
        if (level != backlight_target_level()) {
            backlight_set_manual(level);
            bar_show_level(level);
        }
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_bar_held = false;
        s_bar_dragging = false;
    }
}

// "SUN-DRIVEN · DIMS AT 7:18 PM" or "MANUAL · AUTO AT DUSK": what the
// brightness is doing and when it will next change on its own.
void brightness_status(char *out, size_t len) {
    if (backlight_mode() == BacklightMode::Manual) {
        snprintf(out, len, "MANUAL  ·  AUTO AT %s", backlight_is_daytime() ? "DUSK" : "DAWN");
        return;
    }
    const int ramp = BL_TWILIGHT_RAMP_MIN * 60;
    const int dim_at    = backlight_sunset_sod() - ramp;
    const int bright_at = backlight_sunrise_sod() - ramp;
    const int sod = backlight_local_seconds_of_day();
    char clock[16];
    if (sod < 0) {
        snprintf(out, len, "SUN-DRIVEN  ·  WAITING FOR THE CLOCK");
    } else if (sod < bright_at || sod >= dim_at) {
        fmt_clock(time_t(bright_at), 0, clock, sizeof(clock));
        snprintf(out, len, "SUN-DRIVEN  ·  BRIGHTENS AT %s", clock);
    } else {
        fmt_clock(time_t(dim_at), 0, clock, sizeof(clock));
        snprintf(out, len, "SUN-DRIVEN  ·  DIMS AT %s", clock);
    }
}

}  // namespace

void overlays_show_quick_settings() {
    lv_obj_t *root = make_backdrop();
    s_kind = OverlayKind::QuickSettings;

    // --- Brightness: eyebrow, level and status on one line, the bar under it
    eyebrow(root, LAYOUT_SAFE, kQsRow1Y, "BRIGHTNESS");
    s_qs_level = eyebrow(root, LAYOUT_SAFE + 96, kQsRow1Y, "", COL_ALUMINUM);
    s_qs_bl_status = eyebrow(root, 0, kQsRow1Y, "");
    lv_obj_align(s_qs_bl_status, LV_ALIGN_TOP_RIGHT, -LAYOUT_SAFE, kQsRow1Y);

    // The bar: a rivet track, a fingertip tall, with the filled part in oat and
    // eight hairlines marking nine divisions. It owns every touch that starts
    // on it - no gesture bubbles out of a drag along it - and it does not
    // bubble clicks to the backdrop either.
    lv_obj_t *bar = lv_obj_create(root);
    lv_obj_remove_style_all(bar);
    lv_obj_set_pos(bar, LAYOUT_SAFE, kQsBarY);
    lv_obj_set_size(bar, kQsBarW, kQsBarH);
    lv_obj_set_style_radius(bar, kQsBarH / 2, 0);
    lv_obj_set_style_bg_color(bar, lv_color_hex(COL_RIVET), 0);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
    lv_obj_clear_flag(bar, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(bar, bar_cb, LV_EVENT_ALL, nullptr);
    s_qs_bar = bar;

    s_qs_fill_clip = theme_decor(bar);
    lv_obj_set_pos(s_qs_fill_clip, 0, 0);
    lv_obj_set_size(s_qs_fill_clip, 0, kQsBarH);
    s_qs_fill = theme_decor(s_qs_fill_clip);
    lv_obj_set_pos(s_qs_fill, 0, 0);
    lv_obj_set_size(s_qs_fill, kQsBarH, kQsBarH);
    lv_obj_set_style_radius(s_qs_fill, kQsBarH / 2, 0);
    lv_obj_set_style_bg_color(s_qs_fill, lv_color_hex(COL_OAT), 0);
    lv_obj_set_style_bg_opa(s_qs_fill, LV_OPA_COVER, 0);

    for (int k = 1; k < BL_PRESET_COUNT; k++) {
        lv_obj_t *div = theme_decor(bar);
        lv_obj_set_size(div, 1, kQsBarH);
        lv_obj_set_pos(div, (kQsBarW * k) / BL_PRESET_COUNT, 0);
        lv_obj_set_style_bg_color(div, lv_color_hex(COL_GROUND), 0);
        lv_obj_set_style_bg_opa(div, LV_OPA_60, 0);
    }

    // AUTO: a round button the bar's height, a little clear of its right end.
    s_qs_auto_pill = pill(root, LAYOUT_SAFE + kQsBarW + kQsAutoGap, kQsBarY, kQsBarH, kQsBarH,
                          "AUTO", &font_micro, COL_GROUND, COL_TURQUOISE, auto_pill_cb);

    // --- Forecast -----------------------------------------------------------
    eyebrow(root, LAYOUT_SAFE, kQsRow2Y, "FORECAST");
    s_qs_updated = theme_label(root, &font_body, COL_ALUMINUM, "");
    lv_obj_set_pos(s_qs_updated, LAYOUT_SAFE, kQsValueY);

    // Refresh is a glyph beside the age, with a fingertip of hit area.
    lv_obj_t *refresh = lv_obj_create(root);
    lv_obj_remove_style_all(refresh);
    lv_obj_set_size(refresh, 44, 44);
    lv_obj_add_flag(refresh, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_clear_flag(refresh, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(refresh, refresh_cb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *refresh_ic = theme_label(refresh, &icons_sm, COL_TURQUOISE, ICON_REFRESH);
    lv_obj_center(refresh_ic);

    // --- Wi-Fi --------------------------------------------------------------
    eyebrow(root, kQsWifiX, kQsRow2Y, "WI-FI");
    s_qs_bars = theme_signal_bars(root, kQsWifiX, kQsValueY + 4, kWifiBarHeights);
    s_qs_ssid = theme_label(root, &font_body, COL_ALUMINUM, "");
    lv_obj_set_pos(s_qs_ssid, kQsWifiX + 34, kQsValueY);
    lv_obj_set_width(s_qs_ssid, UI_WIDTH - LAYOUT_SAFE - kQsWifiX - 34);
    lv_label_set_long_mode(s_qs_ssid, LV_LABEL_LONG_CLIP);

    // The drag handle: the way out, said without words.
    lv_obj_t *handle = theme_decor(root);
    lv_obj_set_size(handle, 32, 3);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(COL_RIVET), 0);
    lv_obj_set_pos(handle, (UI_WIDTH - 32) / 2, kQsHandleY);

    overlays_tick();
    lv_obj_update_layout(s_qs_updated);
    lv_obj_align_to(refresh, s_qs_updated, LV_ALIGN_OUT_RIGHT_MID, 4, 0);
}

void overlays_tick() {
    if (s_kind == OverlayKind::Hour && s_hour_pending >= 0) {
        const int index = s_hour_pending;
        s_hour_pending = -1;
        overlays_show_hour(index);
        return;
    }
    if (s_kind != OverlayKind::QuickSettings || s_root == nullptr) return;

    // Do not fight the finger that is on the bar.
    if (!s_bar_held) bar_show_level(backlight_target_level());

    char buf[48];
    if (s_qs_bl_status) {
        brightness_status(buf, sizeof(buf));
        lv_label_set_text(s_qs_bl_status, buf);
    }

    // The AUTO pill is filled while auto-dimming is engaged and dropped to
    // surface-hi once an explicit level has taken over, so its state is
    // legible without a legend.
    if (s_qs_auto_pill) {
        const bool is_auto = backlight_mode() == BacklightMode::Auto;
        lv_obj_set_style_bg_color(
            s_qs_auto_pill, lv_color_hex(is_auto ? COL_TURQUOISE : COL_SURFACE_HI), 0);
        lv_obj_t *lbl = lv_obj_get_child(s_qs_auto_pill, 0);
        if (lbl) {
            lv_obj_set_style_text_color(
                lbl, lv_color_hex(is_auto ? COL_GROUND : COL_ALUMINUM_DIM), 0);
        }
    }

    if (s_qs_updated) {
        fmt_relative(weather_seconds_since_update(), buf, sizeof(buf));
        lv_label_set_text(s_qs_updated, buf);
    }

    theme_signal_bars_set(s_qs_bars, net_signal_bars());
    if (s_qs_ssid) {
        const bool up = net_connected();
        lv_label_set_text(s_qs_ssid, up ? net_ssid().c_str() : "Offline");
        lv_obj_set_style_text_color(
            s_qs_ssid, lv_color_hex(up ? COL_ALUMINUM : COL_SUNSET), 0);
    }
}
