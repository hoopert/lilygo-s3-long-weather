#!/usr/bin/env bash
#
# Regenerate the LVGL font cuts used by the Airstream Weather Panel.
#
# The firmware ships the generated .c files in firmware/src/ui/fonts/, so you only
# need to run this if you want to change the family, the sizes, or the glyph coverage.
#
# Requires: node + npm, curl
#   npm install -g lv_font_conv@1.5.2
#
# Usage:  ./tools/build_fonts.sh
#
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
OUT_DIR="$REPO_ROOT/firmware/src/ui/fonts"
WORK_DIR="$(mktemp -d)"
trap 'rm -rf "$WORK_DIR"' EXIT

command -v lv_font_conv >/dev/null 2>&1 || {
  echo "lv_font_conv not found. Install it with:  npm install -g lv_font_conv@1.5.2" >&2
  exit 1
}

# ---------------------------------------------------------------------------
# Source faces
#
# Jost* is an open interpretation of Futura (SIL OFL 1.1). Futura is the
# Streamline Moderne geometric sans that the Airstream's 1930s design language
# was drawn in, which is why it is the right voice for this panel. The URLs below
# are the static instances served by the Google Fonts CSS API; they are pinned to
# a specific version so this script is reproducible.
# ---------------------------------------------------------------------------
JOST_REGULAR="https://fonts.gstatic.com/s/jost/v20/92zPtBhPNqw79Ij1E865zBUv7myjJQVG.ttf"
JOST_MEDIUM="https://fonts.gstatic.com/s/jost/v20/92zPtBhPNqw79Ij1E865zBUv7myRJQVG.ttf"
JOST_SEMIBOLD="https://fonts.gstatic.com/s/jost/v20/92zPtBhPNqw79Ij1E865zBUv7mx9IgVG.ttf"

echo "==> Downloading Jost* static instances"
curl -sSfL -o "$WORK_DIR/Jost-Regular.ttf"  "$JOST_REGULAR"
curl -sSfL -o "$WORK_DIR/Jost-Medium.ttf"   "$JOST_MEDIUM"
curl -sSfL -o "$WORK_DIR/Jost-SemiBold.ttf" "$JOST_SEMIBOLD"

# Glyph coverage.
#   ASCII printable  0x20-0x7E
#   U+00B0 degree, U+00B7 middot, U+2013 en dash
#   (wind arrows are drawn as vector triangles, not glyphs - Jost has no arrows)
FULL_RANGE='0x20-0x7E,0xB0,0xB7,0x2013'
# The hero cut only ever renders a temperature, so it carries digits and the
# handful of marks that can appear beside one. This keeps a 72px face under 20KB.
HERO_RANGE='0x2C-0x3A,0xB0,0x20'

mkdir -p "$OUT_DIR"

# bpp 4 gives 16 levels of antialiasing, which is indistinguishable from bpp 8 on
# this panel at half the flash cost. --no-compress trades a little flash for a
# meaningful cut in per-glyph render time, which matters on the full-refresh path.
gen() {
  local face="$1" size="$2" range="$3" name="$4"
  echo "==> ${name}  (${face} @ ${size}px)"
  lv_font_conv \
    --font "$WORK_DIR/${face}.ttf" \
    --range "$range" \
    --size "$size" \
    --bpp 4 \
    --no-compress \
    --format lvgl \
    --lv-include lvgl.h \
    --output "$OUT_DIR/${name}.c"
}

gen Jost-SemiBold 72 "$HERO_RANGE" font_hero
gen Jost-Medium   30 "$FULL_RANGE" font_title
# The hourly columns are 43px wide and have to hold "100" without clipping, so
# they get their own cut rather than borrowing font_title. See the note on
# kTempY in screen_today.cpp.
gen Jost-Medium   24 "$HERO_RANGE" font_hour_narrow   # 3-digit fallback for the 30px hourly cut (font_title)
gen Jost-Regular  20 "$FULL_RANGE" font_body
gen Jost-Medium   15 "$FULL_RANGE" font_label
gen Jost-Medium   12 "$FULL_RANGE" font_micro

# ---------------------------------------------------------------------------
# Icons
#
# Material Symbols Rounded (Apache 2.0), subset to the glyphs we actually draw.
# The full face is 4390 glyphs and 1.2MB; these two cuts come to under 30KB.
#
# Rounded is the right variant here: uniform stroke weight, geometric
# construction, round terminals - the same drawing logic as Jost*, so the icons
# and the numerals look like they came from one hand.
#
# The codepoints must stay in sync with kIcon* in firmware/src/ui/icons.h.
# ---------------------------------------------------------------------------
MATERIAL_SYMBOLS="https://fonts.gstatic.com/s/materialsymbolsrounded/v372/syl0-zNym6YjUruM-QrEh7-nyTnjDwKNJ_190FjpZIvDmUSVOK7BDB_Qb9vUSzq3wzLK-P0J-V_Zs-QtQth3-jOcbTCVpeRL2w5rwZu2rIelXxI.ttf"
curl -sSfL -o "$WORK_DIR/MaterialSymbolsRounded.ttf" "$MATERIAL_SYMBOLS"

# Weather conditions, drawn at both hero and hourly-column size.
WX_ICONS='0xE81A,0xEF5E,0xF172,0xEA46,0xE2BD,0xE818,0xF61E,0xF176,0xF61F,0xF61D,0xE2CD,0xF61C,0xEBDB,0xF67F'
# Chrome: droplet, wind, wifi, wifi-off, refresh, settings, brightness-auto,
# sun, moon, schedule, close, error, done, navigation arrow, battery.
UI_ICONS='0xE798,0xEFD8,0xE63E,0xE648,0xE5D5,0xE8B8,0xE1AB,0xE518,0xE51C,0xE192,0xE14C,0xE000,0xE876,0xE55D,0xE1A4,0xE9E4,0xE128,0xE10E,0xE023,0xE09C'
# Tiny cut for glyphs set inline with Micro text: the hourly wind arrow and
# the refresh pill. navigation, refresh.
# navigation, refresh, and the eight compass arrows (north .. north_west):
# the wind arrow is one of these rather than a rotated `navigation`, because
# rotating any widget in LVGL 8.4 needs an alpha layer, which needs
# LV_COLOR_SCREEN_TRANSP, which this 16-bit build does not have.
XS_ICONS='0xE55D,0xE5D5,0xF1DF,0xF1E0,0xF1E1,0xF1E2,0xF1E3,0xF1E4,0xF1E5,0xF1E6'

gen MaterialSymbolsRounded 56 "$WX_ICONS"              icons_lg
gen MaterialSymbolsRounded 20 "$WX_ICONS,$UI_ICONS"    icons_sm
gen MaterialSymbolsRounded 16 "$UI_ICONS"              icons_ui
gen MaterialSymbolsRounded 11 "$XS_ICONS"              icons_xs

echo
echo "==> Generated into $OUT_DIR:"
ls -la "$OUT_DIR"/*.c | awk '{printf "    %-28s %8.1f KB\n", $9, $5/1024}'
echo
echo "Fonts are declared in firmware/src/ui/theme.h - no further wiring needed."
