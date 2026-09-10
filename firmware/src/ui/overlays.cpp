#include "overlays.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

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
lv_obj_t *s_qs_bl_status = nullptr;
lv_obj_t *s_qs_bars = nullptr;
lv_obj_t *s_qs_ssid = nullptr;
bool      s_slider_held = false;

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
        case OverlayKind::Now:           return UI_SWIPE_UP | UI_SWIPE_DOWN;
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
    s_qs_slider = s_qs_auto_pill = s_qs_updated = s_qs_level = nullptr;
    s_qs_bl_status = s_qs_bars = s_qs_ssid = nullptr;
    s_slider_held = false;
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
                        ? LAYOUT_COLUMNS_X + hour_index * LAYOUT_HOUR_COL_W
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
// Quick settings - a 640x130 sheet dropped from the top edge over a scrim
// (design/SPEC.md §4): brightness, refresh, and the connection facts you
// want when something is wrong. The sheet, not the scrim, is what slides.
// ---------------------------------------------------------------------------
namespace {

constexpr int kSheetH      = 130;
constexpr int kSheetRadius = 8;
// LVGL rounds all four corners or none, so the sheet is drawn taller than it
// is and parked kSheetRadius above the screen: the top corners are clipped
// away and only the bottom ones show. Every y inside the sheet is offset
// by the same amount.
constexpr int kSheetTop    = -kSheetRadius;
constexpr int kSheetDrawH  = kSheetH + kSheetRadius;
constexpr int kInY         = kSheetRadius;          // sheet-local y of screen y0

constexpr int kEyebrowY = 42;
constexpr int kControlY = 58;
constexpr int kStatusY  = 96;

constexpr int kBrightnessX = 10;
constexpr int kAutoX       = 322;
constexpr int kForecastX   = 418;
constexpr int kWifiX       = 556;

const uint8_t kWifiBarHeights[4] = {6, 10, 14, 18};

void sheet_y_cb(void *o, int32_t v) { lv_obj_set_y(static_cast<lv_obj_t *>(o), v); }

// "SUN-DRIVEN · DIMS AT 7:18 PM" or "MANUAL · AUTO IN 3H 42M": what the
// brightness is doing and when it will next change on its own. The dimming
// begins a twilight ramp before sunset, so that is the time quoted.
void brightness_status(char *out, size_t len) {
    if (backlight_mode() == BacklightMode::Manual) {
        const uint32_t left = backlight_manual_remaining_ms() / 60000u;
        if (left >= 60) {
            snprintf(out, len, "MANUAL  ·  AUTO IN %uH %02uM", unsigned(left / 60), unsigned(left % 60));
        } else {
            snprintf(out, len, "MANUAL  ·  AUTO IN %uM", unsigned(left));
        }
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
    lv_obj_t *root = make_backdrop(true);
    s_kind = OverlayKind::QuickSettings;

    // The sheet. Clickable so a tap on it stops there rather than reaching the
    // scrim's dismiss handler; gestures still bubble to the layer.
    lv_obj_t *sheet = lv_obj_create(root);
    lv_obj_remove_style_all(sheet);
    lv_obj_set_size(sheet, UI_WIDTH, kSheetDrawH);
    lv_obj_set_pos(sheet, 0, kSheetTop);
    lv_obj_set_style_radius(sheet, kSheetRadius, 0);
    lv_obj_set_style_bg_color(sheet, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_bg_opa(sheet, LV_OPA_COVER, 0);
    lv_obj_set_style_shadow_color(sheet, lv_color_hex(COL_GROUND), 0);
    lv_obj_set_style_shadow_opa(sheet, LV_OPA_50, 0);
    lv_obj_set_style_shadow_width(sheet, 12, 0);
    lv_obj_set_style_shadow_ofs_y(sheet, 6, 0);
    lv_obj_clear_flag(sheet, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(sheet, LV_OBJ_FLAG_CLICKABLE);

    // Drops in from above the screen, 220ms ease-out.
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, sheet);
    lv_anim_set_time(&a, UI_OVERLAY_ANIM_MS);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_values(&a, kSheetTop - kSheetH, kSheetTop);
    lv_anim_set_exec_cb(&a, sheet_y_cb);
    lv_anim_start(&a);

    // Drag handle, bottom centre.
    lv_obj_t *handle = theme_decor(sheet);
    lv_obj_set_size(handle, 32, 3);
    lv_obj_set_style_radius(handle, 2, 0);
    lv_obj_set_style_bg_opa(handle, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(handle, lv_color_hex(COL_RIVET), 0);
    lv_obj_align(handle, LV_ALIGN_BOTTOM_MID, 0, -5);

    // Header row: eyebrow left, the two addresses right, rule beneath.
    eyebrow(sheet, LAYOUT_SAFE, kInY + 8, "QUICK SETTINGS");

    char addr[64];
    if (net_connected()) {
        snprintf(addr, sizeof(addr), "%s  ·  %s.local", net_ip().c_str(), OTA_HOSTNAME);
    } else {
        snprintf(addr, sizeof(addr), "NOT CONNECTED  ·  JOIN \"%s\"", net_ap_name());
    }
    lv_obj_t *addr_l = eyebrow(sheet, 0, kInY + 8, addr);
    lv_obj_align(addr_l, LV_ALIGN_TOP_RIGHT, -LAYOUT_SAFE, kInY + 8);

    lv_obj_t *rule = theme_decor(sheet);
    lv_obj_add_style(rule, &style_hairline, 0);
    lv_obj_set_size(rule, UI_WIDTH - LAYOUT_SAFE * 2, 1);
    lv_obj_set_pos(rule, LAYOUT_SAFE, kInY + 30);

    // --- Brightness ---------------------------------------------------------
    eyebrow(sheet, kBrightnessX, kInY + kEyebrowY, "BRIGHTNESS");
    s_qs_level = eyebrow(sheet, kBrightnessX + 96, kInY + kEyebrowY, "", COL_ALUMINUM);

    lv_obj_t *slider = lv_slider_create(sheet);
    lv_obj_set_pos(slider, kBrightnessX, kInY + kControlY + 6);
    lv_obj_set_size(slider, 296, 10);
    lv_slider_set_range(slider, BL_LEVEL_MIN, 255);
    lv_slider_set_value(slider, backlight_target_level(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_RIVET), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(slider, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(slider, 5, LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_OAT), LV_PART_INDICATOR);
    lv_obj_set_style_radius(slider, 5, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COL_OAT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 5, LV_PART_KNOB);   // 10px track + 5 each side = 20px knob
    lv_obj_add_event_cb(slider, slider_cb, LV_EVENT_ALL, nullptr);
    // The slider owns every touch that starts on it. Without this, LVGL also
    // reports the drag as a horizontal gesture on the layer above, and the
    // sheet used to close - and the screen beneath used to change - halfway
    // through setting the brightness.
    lv_obj_clear_flag(slider, LV_OBJ_FLAG_GESTURE_BUBBLE);
    s_qs_slider = slider;

    s_qs_bl_status = eyebrow(sheet, kBrightnessX, kInY + kStatusY, "");

    // --- AUTO ---------------------------------------------------------------
    s_qs_auto_pill = pill(sheet, kAutoX, kInY + kControlY, 72, 24, "AUTO",
                          &font_micro, COL_GROUND, COL_TURQUOISE, auto_pill_cb);

    // --- Forecast -----------------------------------------------------------
    // Refresh, with the age of the data beneath it - the question "is this
    // stale?" is the only reason anyone opens this sheet in a hurry. The glyph
    // is its own label in the icon cut: the pill's text font is Jost, which
    // has no icon glyphs, and LVGL logs a missing-glyph warning on every
    // redraw for a codepoint it cannot draw.
    eyebrow(sheet, kForecastX, kInY + kEyebrowY, "FORECAST");
    lv_obj_t *refresh = pill(sheet, kForecastX, kInY + kControlY, 118, 24, "REFRESH",
                             &font_micro, COL_ALUMINUM, COL_SURFACE_HI, refresh_cb);
    lv_obj_t *refresh_lbl = lv_obj_get_child(refresh, 0);
    lv_obj_set_style_text_letter_space(refresh_lbl, 1, 0);
    lv_obj_align(refresh_lbl, LV_ALIGN_CENTER, 8, 0);
    lv_obj_t *refresh_ic = theme_label(refresh, &icons_xs, COL_ALUMINUM, ICON_REFRESH);
    lv_obj_align_to(refresh_ic, refresh_lbl, LV_ALIGN_OUT_LEFT_MID, -5, 0);

    s_qs_updated = eyebrow(sheet, kForecastX, kInY + kStatusY, "");

    // --- Wi-Fi --------------------------------------------------------------
    eyebrow(sheet, kWifiX, kInY + kEyebrowY, "WI-FI");
    s_qs_bars = theme_signal_bars(sheet, kWifiX, kInY + kControlY + 3, kWifiBarHeights);
    s_qs_ssid = eyebrow(sheet, kWifiX, kInY + kStatusY, "", COL_ALUMINUM);
    lv_obj_set_width(s_qs_ssid, UI_WIDTH - LAYOUT_SAFE - kWifiX);
    lv_label_set_long_mode(s_qs_ssid, LV_LABEL_LONG_CLIP);

    overlays_tick();
}

void overlays_tick() {
    if (s_kind == OverlayKind::Hour && s_hour_pending >= 0) {
        const int index = s_hour_pending;
        s_hour_pending = -1;
        overlays_show_hour(index);
        return;
    }
    if (s_kind != OverlayKind::QuickSettings || s_root == nullptr) return;

    // Do not fight the finger that is dragging the slider.
    if (s_qs_slider && !s_slider_held) {
        lv_slider_set_value(s_qs_slider, backlight_target_level(), LV_ANIM_OFF);
    }

    char buf[48];
    if (s_qs_level) {
        const int pct = int(lroundf(backlight_target_level() * 100.0f / 255.0f));
        snprintf(buf, sizeof(buf), "%d%%", pct);
        lv_label_set_text(s_qs_level, buf);
    }

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
        lv_label_set_text(s_qs_ssid, up ? net_ssid().c_str() : "OFFLINE");
        lv_obj_set_style_text_color(
            s_qs_ssid, lv_color_hex(up ? COL_ALUMINUM : COL_SUNSET), 0);
    }
}
