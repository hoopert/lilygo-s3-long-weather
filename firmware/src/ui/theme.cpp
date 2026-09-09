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
