// Icon glyphs, as UTF-8 literals into the Material Symbols Rounded subset.
//
// Icons are font glyphs rather than bitmaps, which means they recolor with a
// style property, scale by swapping the font, and cost nothing to draw beyond
// the text they already are. The codepoints here must stay in sync with the
// WX_ICONS and UI_ICONS lists in tools/build_fonts.sh - a glyph referenced here
// but missing from the subset renders as an empty box.
#pragma once

#include "net/weather.h"

// --- weather conditions (icons_lg 56px, icons_sm 20px) ---------------------
#define ICON_WX_CLEAR_DAY        "\xEE\xA0\x9A"   // U+E81A sunny
#define ICON_WX_CLEAR_NIGHT      "\xEE\xBD\x9E"   // U+EF5E nightlight
#define ICON_WX_PARTLY_DAY       "\xEF\x85\xB2"   // U+F172 partly_cloudy_day
#define ICON_WX_PARTLY_NIGHT     "\xEE\xA9\x86"   // U+EA46 partly_cloudy_night
#define ICON_WX_OVERCAST         "\xEE\x8A\xBD"   // U+E2BD cloud
#define ICON_WX_FOG              "\xEE\xA0\x98"   // U+E818 foggy
#define ICON_WX_DRIZZLE          "\xEF\x98\x9E"   // U+F61E rainy_light
#define ICON_WX_RAIN             "\xEF\x85\xB6"   // U+F176 rainy
#define ICON_WX_HEAVY_RAIN       "\xEF\x98\x9F"   // U+F61F rainy_heavy
#define ICON_WX_FREEZING_RAIN    "\xEF\x98\x9D"   // U+F61D rainy_snow
#define ICON_WX_SNOW             "\xEE\x8B\x8D"   // U+E2CD weather_snowy
#define ICON_WX_HEAVY_SNOW       "\xEF\x98\x9C"   // U+F61C snowing_heavy
#define ICON_WX_THUNDER          "\xEE\xAF\x9B"   // U+EBDB thunderstorm
#define ICON_WX_THUNDER_HAIL     "\xEF\x99\xBF"   // U+F67F weather_hail

// --- chrome (icons_sm 20px, icons_ui 16px) --------------------------------
#define ICON_DROP                "\xEE\x9E\x98"   // U+E798 water_drop
#define ICON_WIND                "\xEE\xBF\x98"   // U+EFD8 air
#define ICON_WIFI                "\xEE\x98\xBE"   // U+E63E wifi
#define ICON_WIFI_OFF            "\xEE\x99\x88"   // U+E648 wifi_off
#define ICON_REFRESH             "\xEE\x97\x95"   // U+E5D5 refresh
#define ICON_SETTINGS            "\xEE\xA2\xB8"   // U+E8B8 settings
#define ICON_BRIGHTNESS_AUTO     "\xEE\x86\xAB"   // U+E1AB brightness_auto
#define ICON_SUN                 "\xEE\x94\x98"   // U+E518 light_mode
#define ICON_MOON                "\xEE\x94\x9C"   // U+E51C dark_mode
#define ICON_CLOCK               "\xEE\x86\x92"   // U+E192 schedule
#define ICON_CLOSE               "\xEE\x85\x8C"   // U+E14C close
#define ICON_ERROR               "\xEE\x80\x80"   // U+E000 error
#define ICON_DONE                "\xEE\xA1\xB6"   // U+E876 done
#define ICON_NAV                 "\xEE\x95\x9D"   // U+E55D navigation
#define ICON_BATTERY             "\xEE\x86\xA4"   // U+E1A4 battery_full

// Glyph for a condition, given the WMO code and whether the sun is up.
const char *icon_for(WxIcon icon);
