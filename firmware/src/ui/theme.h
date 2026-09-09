// "Polished Aluminum & Desert Dusk" - the visual language of the panel.
//
// Every color, font and layout constant used by the UI resolves through this
// header, so retuning the design after a pass in Claude Design is a matter of
// editing numbers in one place rather than hunting literals across screens.
// docs/DESIGN_PROMPT.md is the source these values came from and should be
// updated alongside them.
#pragma once

#include <lvgl.h>

#include "config.h"

// ---------------------------------------------------------------------------
// Palette
//
// Dark-first, because a wall panel in a trailer is read at night as often as at
// noon and a bright rectangle at 2am is a hostile object. Note that neither
// pure black nor pure white appears: the ground carries a cool cast and the
// "white" is the off-white of brushed aluminum, which is what stops the panel
// reading as a computer display bolted to a 1970s interior.
// ---------------------------------------------------------------------------
#define COL_GROUND       0x0E1113   // page background
#define COL_SURFACE      0x171B1E   // raised panels, hour columns
#define COL_SURFACE_HI   0x22282C   // pressed / selected
#define COL_RIVET        0x2E353A   // hairlines and seams
#define COL_ALUMINUM     0xC9D1D6   // primary text
#define COL_ALUMINUM_DIM 0x7C878E   // secondary text
#define COL_OAT          0xE8DCC8   // the hero temperature; the one warm light
#define COL_TURQUOISE    0x3FBFB0   // THE Airstream accent - water, active state
#define COL_SUNSET       0xE2703A   // heat, alerts
#define COL_SKY          0x6FA8C7   // wind, cold

// ---------------------------------------------------------------------------
// Type scale. Five cuts, no more - see docs/DESIGN_PROMPT.md.
// ---------------------------------------------------------------------------
LV_FONT_DECLARE(font_hero);    // Jost* SemiBold 72 - temperature only
LV_FONT_DECLARE(font_title);   // Jost* Medium   30
LV_FONT_DECLARE(font_hour);    // Jost* Medium   24 - hourly temperatures
LV_FONT_DECLARE(font_body);    // Jost* Regular  20
LV_FONT_DECLARE(font_label);   // Jost* Medium   15
LV_FONT_DECLARE(font_micro);   // Jost* Medium   12, tracked, uppercase
LV_FONT_DECLARE(icons_lg);     // Material Symbols Rounded 56
LV_FONT_DECLARE(icons_sm);     // Material Symbols Rounded 20
LV_FONT_DECLARE(icons_ui);     // Material Symbols Rounded 16

// ---------------------------------------------------------------------------
// Layout. The panel is 640x180 in app space; these are the seams.
// ---------------------------------------------------------------------------
#define LAYOUT_SAFE        10
#define LAYOUT_NOW_W       208            // left "Now" zone
#define LAYOUT_STRIP_X     LAYOUT_NOW_W   // hourly strip origin
#define LAYOUT_STRIP_W     (UI_WIDTH - LAYOUT_STRIP_X)
#define LAYOUT_HOUR_COL_W  (LAYOUT_STRIP_W / WX_HOURLY_SLOTS)
#define LAYOUT_HEADER_H    22

void theme_init();

// Shared styles, initialised by theme_init().
extern lv_style_t style_screen;
extern lv_style_t style_surface;
extern lv_style_t style_hairline;
extern lv_style_t style_hero;
extern lv_style_t style_title;
extern lv_style_t style_body;
extern lv_style_t style_label;
extern lv_style_t style_micro;

// The temperature ramp: sky -> turquoise -> oat -> sunset. Reading the strip as
// a heat map before reading any number is the whole point of it, so this is
// applied to hourly values and to the trend ribbon alike. Input is in whatever
// unit the panel is currently displaying.
lv_color_t theme_temp_color(float temp, bool imperial);

// Convenience: a label with a style and a parent, one call instead of four.
lv_obj_t *theme_label(lv_obj_t *parent, const lv_font_t *font, uint32_t color,
                      const char *text);

// A bare, non-interactive object for rules, seams and indicator dots.
// lv_obj_create() makes objects clickable by default, and a 1px decorative
// hairline that silently eats taps is a genuinely annoying bug to track down.
lv_obj_t *theme_decor(lv_obj_t *parent);
