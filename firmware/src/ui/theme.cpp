#include "theme.h"

#include <math.h>

#include "icons.h"

lv_style_t style_screen;
lv_style_t style_surface;
lv_style_t style_hairline;
lv_style_t style_hero;
lv_style_t style_title;
lv_style_t style_body;
lv_style_t style_label;
lv_style_t style_micro;
lv_style_t style_pressed;
lv_style_t style_press_transition;

namespace {
const lv_style_prop_t kPressProps[] = {
    LV_STYLE_TRANSFORM_ZOOM, LV_STYLE_BG_COLOR, LV_STYLE_PROP_INV,
};
lv_style_transition_dsc_t s_press_tr;
}  // namespace

namespace {

void init_text_style(lv_style_t *s, const lv_font_t *font, uint32_t color,
                     lv_coord_t tracking = 0) {
    lv_style_init(s);
    lv_style_set_text_font(s, font);
    lv_style_set_text_color(s, lv_color_hex(color));
    lv_style_set_text_letter_space(s, tracking);
}

lv_color_t mix_hex(uint32_t a, uint32_t b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    const uint8_t ar = (a >> 16) & 0xFF, ag = (a >> 8) & 0xFF, ab = a & 0xFF;
    const uint8_t br = (b >> 16) & 0xFF, bg = (b >> 8) & 0xFF, bb = b & 0xFF;
    return lv_color_make(uint8_t(ar + (br - ar) * t),
                         uint8_t(ag + (bg - ag) * t),
                         uint8_t(ab + (bb - ab) * t));
}

}  // namespace

void theme_init() {
    lv_style_init(&style_screen);
    lv_style_set_bg_color(&style_screen, lv_color_hex(COL_GROUND));
    lv_style_set_bg_opa(&style_screen, LV_OPA_COVER);
    lv_style_set_border_width(&style_screen, 0);
    lv_style_set_pad_all(&style_screen, 0);
    lv_style_set_radius(&style_screen, 0);

    lv_style_init(&style_surface);
    lv_style_set_bg_color(&style_surface, lv_color_hex(COL_SURFACE));
    lv_style_set_bg_opa(&style_surface, LV_OPA_COVER);
    lv_style_set_border_width(&style_surface, 0);
    lv_style_set_pad_all(&style_surface, 0);
    lv_style_set_radius(&style_surface, 6);

    // The rivet seam. Airstreams are defined by riveted aluminum panel joins,
    // and a 1px hairline between hour columns is the whole of the ornament
    // budget for this design.
    // Press feedback (design/SPEC.md, "Beyond the tokens"): scale to 0.97 and
    // fill surface-hi, 90ms each way. transform_zoom is 256 = 1.0.
    lv_style_transition_dsc_init(&s_press_tr, kPressProps, lv_anim_path_ease_out, 90, 0, nullptr);
    lv_style_init(&style_pressed);
    lv_style_set_transform_zoom(&style_pressed, 248);
    lv_style_set_bg_color(&style_pressed, lv_color_hex(COL_SURFACE_HI));
    lv_style_set_transition(&style_pressed, &s_press_tr);
    lv_style_init(&style_press_transition);
    lv_style_set_transition(&style_press_transition, &s_press_tr);

    lv_style_init(&style_hairline);
    lv_style_set_bg_color(&style_hairline, lv_color_hex(COL_RIVET));
    lv_style_set_bg_opa(&style_hairline, LV_OPA_COVER);
    lv_style_set_border_width(&style_hairline, 0);
    lv_style_set_radius(&style_hairline, 0);

    // Negative tracking on the hero cut: at 72px, Jost*'s default sidebearings
    // open the numerals up more than the composition wants.
    init_text_style(&style_hero,  &font_hero,  COL_OAT, -1);
    init_text_style(&style_title, &font_title, COL_ALUMINUM);
    init_text_style(&style_body,  &font_body,  COL_ALUMINUM);
    init_text_style(&style_label, &font_label, COL_ALUMINUM_DIM);
    init_text_style(&style_micro, &font_micro, COL_ALUMINUM_DIM, 1);
}

lv_color_t theme_temp_color(float temp, bool imperial) {
    if (isnan(temp)) return lv_color_hex(COL_ALUMINUM_DIM);

    // Stops in Fahrenheit; the Celsius equivalents are the same physical
    // temperatures, so the ramp means the same thing in either unit.
    float f = imperial ? temp : (temp * 9.0f / 5.0f + 32.0f);

    if (f <= 32.0f) return lv_color_hex(COL_SKY);
    if (f <= 50.0f) return mix_hex(COL_SKY, COL_TURQUOISE, (f - 32.0f) / 18.0f);
    if (f <= 68.0f) return mix_hex(COL_TURQUOISE, COL_OAT, (f - 50.0f) / 18.0f);
    if (f <= 90.0f) return mix_hex(COL_OAT, COL_SUNSET, (f - 68.0f) / 22.0f);
    return lv_color_hex(COL_SUNSET);
}

lv_obj_t *theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                      const char *text) {
    lv_obj_t *l = lv_label_create(parent);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    lv_label_set_text(l, text ? text : "");
    return l;
}

lv_obj_t *theme_decor(lv_obj_t *parent) {
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_clear_flag(o, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

lv_obj_t *theme_signal_bars(lv_obj_t *parent, int x, int y, const uint8_t heights[4]) {
    const int total_h = heights[3];
    lv_obj_t *row = theme_decor(parent);
    lv_obj_set_pos(row, x, y);
    lv_obj_set_size(row, 4 * 4 + 3 * 2, total_h);
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = theme_decor(row);
        lv_obj_set_size(bar, 4, heights[i]);
        lv_obj_set_pos(bar, i * 6, total_h - heights[i]);
        lv_obj_set_style_radius(bar, 1, 0);
        lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
        lv_obj_set_style_bg_color(bar, lv_color_hex(COL_RIVET), 0);
    }
    return row;
}

void theme_signal_bars_set(lv_obj_t *bars, uint8_t lit) {
    if (bars == nullptr) return;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *bar = lv_obj_get_child(bars, i);
        if (bar == nullptr) break;
        lv_obj_set_style_bg_color(
            bar, lv_color_hex(i < lit ? COL_TURQUOISE : COL_RIVET), 0);
    }
}

lv_obj_t *theme_rivet_row(lv_obj_t *parent, int x0, int x1, int y) {
    // A 2px-wide line with a 2px dash and a 14px gap is a 2x2 dot every 16px.
    // lv_line keeps a pointer to its points rather than copying them, so each
    // row owns a pair and frees it when the widget goes.
    lv_point_t *pts = static_cast<lv_point_t *>(lv_mem_alloc(sizeof(lv_point_t) * 2));
    if (pts == nullptr) return nullptr;
    pts[0] = {lv_coord_t(x0), lv_coord_t(y + 1)};
    pts[1] = {lv_coord_t(x1), lv_coord_t(y + 1)};
    lv_obj_t *line = lv_line_create(parent);
    lv_obj_remove_style_all(line);
    lv_obj_clear_flag(line, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    lv_line_set_points(line, pts, 2);
    lv_obj_add_event_cb(line, [](lv_event_t *e) {
        lv_mem_free(lv_event_get_user_data(e));
    }, LV_EVENT_DELETE, pts);
    lv_obj_set_style_line_width(line, 2, 0);
    lv_obj_set_style_line_color(line, lv_color_hex(COL_RIVET), 0);
    lv_obj_set_style_line_dash_width(line, 2, 0);
    lv_obj_set_style_line_dash_gap(line, 14, 0);
    lv_obj_set_style_line_rounded(line, false, 0);
    return line;
}

const char *icon_for(WxIcon icon) {
    switch (icon) {
        case WxIcon::ClearDay:     return ICON_WX_CLEAR_DAY;
        case WxIcon::ClearNight:   return ICON_WX_CLEAR_NIGHT;
        case WxIcon::PartlyDay:    return ICON_WX_PARTLY_DAY;
        case WxIcon::PartlyNight:  return ICON_WX_PARTLY_NIGHT;
        case WxIcon::Overcast:     return ICON_WX_OVERCAST;
        case WxIcon::Fog:          return ICON_WX_FOG;
        case WxIcon::Drizzle:      return ICON_WX_DRIZZLE;
        case WxIcon::Rain:         return ICON_WX_RAIN;
        case WxIcon::HeavyRain:    return ICON_WX_HEAVY_RAIN;
        case WxIcon::FreezingRain: return ICON_WX_FREEZING_RAIN;
        case WxIcon::Snow:         return ICON_WX_SNOW;
        case WxIcon::HeavySnow:    return ICON_WX_HEAVY_SNOW;
        case WxIcon::Thunder:      return ICON_WX_THUNDER;
        case WxIcon::ThunderHail:  return ICON_WX_THUNDER_HAIL;
        default:                   return ICON_WX_OVERCAST;
    }
}

void theme_press_feedback(lv_obj_t *obj) {
    lv_obj_set_style_transform_pivot_x(obj, lv_obj_get_width(obj) / 2, 0);
    lv_obj_set_style_transform_pivot_y(obj, lv_obj_get_height(obj) / 2, 0);
    lv_obj_add_style(obj, &style_press_transition, 0);
    lv_obj_add_style(obj, &style_pressed, LV_STATE_PRESSED);
}
