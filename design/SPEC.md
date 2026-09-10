# Airstream Weather Panel — design refinement pass

Eight artboards, 640 × 180 at 1:1 (icon sheet taller), night palette. Source
artboards live in `design/artboards/`, PNG exports in `design/exports/`,
machine handoff in `design/tokens.json`. All coordinates below are LVGL
label-box tops in app space (640 × 180 after rotation), matching how
`screen_today.cpp` places widgets today.

Palette, ramp and motion timings are unchanged from what ships. What this pass
changes: hourly temperatures move from the 24px `font_hour` cut to the 30px
Title cut; hourly wind returns to the arrow-plus-speed form; a precipitation
probability bar is added under each hour; the Hour Detail and Quick Settings
overlays become partial panels instead of full-screen replacements; a drawn
Night Mode state is added; the rivet seam gains its dots.

## Font cuts

`font_hour` (24px) is **retired** — hourly temperatures now use `font_title`
(30px). No new cut needs generating; delete `font_hour` from
`tools/build_fonts.sh` and reclaim its flash. Every other cut is unchanged.

## 1 · Today

Zone split at **x = 208**, seam 1px `rivet` from y12 to y160 at x = 207, with
**2px `rivet` dots every 16px at x = 206, starting y16** (the rivet line — the
one ornament).

Now zone (all x = 10 unless noted):

| Element | Position | Cut / color |
|---|---|---|
| Hero temperature | (10, 22) | Hero 72, `oat`, tracking −1 |
| Degree mark | hero right + 2, y34 (cap-aligned) | Title 30, `oat` |
| Unit suffix `F` | degree right + 4, y52 | Label 15, `aluminum-dim` |
| Condition icon 56px | (146, 10) | icons_lg, `oat` day / `aluminum` night |
| Condition text | (10, 98) | Body 20, `aluminum` |
| Meta line `FEELS 71° · H 84° L 58°` | (10, 124) | Micro, `aluminum-dim` |
| Place line `DENVER · 2:35P · 4 MIN AGO` | (10, 146) | Micro, `aluminum-dim`, tracking 0.5 — must fit within x10–200 (clock drops the M-space, LV_LABEL_LONG_CLIP) |

Hourly strip — ten 43px columns from x = 208, hairline separators 1px `rivet`
at 50% opacity, y20–y112:

| Row | y | Spec |
|---|---|---|
| Hour label | 6 | Micro, `aluminum-dim`; current hour `NOW` in `turquoise` |
| Condition glyph 20px | 22 | icons_sm, colored by temperature ramp |
| Temperature | 42 | **Title 30**, ramp color, no degree sign |
| Trend ribbon | 78, h 30 | 2px polyline through hour centers, ramp-colored; fill below fades with squared falloff from 70/255 max opacity (unchanged algorithm, band moved/resized) |
| Precip % | 112 | Micro, `turquoise`, shown only ≥ 10% |
| Precip bar | bottom edge y152 | 2px wide, centered, height = prob × 0.24 (0–24px), `turquoise`; suppressed with the % |
| Wind | 156 | `navigation` glyph 11px (icons_ui) rotated to (wind_dir + 180)°, hidden < 3 mph, + integer speed, Micro, `sky` |

`NOW` column keeps its `surface` slab (x208, y4, 42 × 158, r5). Page indicator
unchanged: 12×3 `turquoise` bar + 4×3 `rivet` dots, gap 4, bottom y174.

## 2 · Hour Detail

No longer a full-screen replacement. A **center panel at x170–470 (300px,
full height)** expands out of the tapped column (220ms ease-out), `surface`
bg, 1px `rivet` side borders, **2px `turquoise` top border**, shadow 0 offset /
18px spread `ground` at 70%. **Tap anywhere on the panel to dismiss**; no printed hint — the interaction
is learned in one tap. Tapping a visible neighbor hour retargets instead.

Either side of the panel shows **three neighbor hours at full opacity**, each
a tap target that re-points the panel at that hour: 43px columns at
x20 / x63 / x106 (left) and x491 / x534 / x577 (right), 1px `rivet` hairlines
between; hour label Micro y36, glyph 20px y56, temperature Title 30 (ramp) y82.

Panel content (x relative to panel edge): eyebrow Micro (12, 10) carries the
condition — `FORECAST · PARTLY CLOUDY`; hour Title 30 (12, 26), never wraps;
24px condition glyph top-right (right 12, y14), ramp-colored; rule 274 × 1 at
y64. Metric grid **3 × 2**, columns panel-x 12 / 116 / 214, rows y68 / y120;
Micro label over **Title 30 value**: TEMP (ramp), FEELS LIKE, HUMIDITY /
RAIN CHANCE (`turquoise`, accumulation as a 12px `aluminum-dim` suffix —
`20% 0.02"`), WIND + cardinal (`sky`, `MPH` suffix in 12px `aluminum-dim`),
GUSTS (`sky`, same suffix). Cloud cover, dew point, and the standalone amount
item are dropped.

## 3 · Now Detail

Full-screen overlay; tap anywhere dismisses (no printed hint — learned in one
tap). Header: glyph 20px (10, 12), Title 30 (38, 6), place + clock Micro
top-right, rule 620 × 1 at y40.

Sun arc grows to **192px** and uses the full display height: visible half
212 × 108 at (14, 58), baseline y162. Track 3px `rivet`, indicator 3px `oat`,
LVGL arc rotation 180°, angles 0–180. The position marker is the **sun
itself**: `light_mode` glyph 22px `oat` riding the arc at the current
position, seated on a 28px `ground`-colored puck so the track passes behind
it (implement as the arc knob with an image style). Inside the arc:
`DAYLIGHT` in **Label 15, `oat`** (matching the arc) centered at y108,
sunrise `6:42 AM` at (30, 148), sunset `7:18 PM` at (158, 148).

Metrics 3 × 2 at cols x248 / x388 / x512, rows y54 / y112 — Micro label over
**Title 30 value**, unit suffixes (`MPH` / `MI`) in 12px `aluminum-dim`:
HIGH / LOW (`oat`), HUMIDITY, UV INDEX (`sunset` ≥ 6), GUSTS + cardinal
(`sky`), PRESSURE (micro module, below), VISIBILITY.

**Pressure is a micro trend module, spoken not numeric.** Most people have no
reference point for hPa, so the cell shows: Micro label with the raw reading
subdued (`PRESSURE 1008`); an outlook word in **Body 20** from the 3-hour
trend bands (below); and a **56 × 14 sparkline** (2px polyline, last 6 hours,
7 points) beneath it, in the word's color. Risk words render `sunset`, calm
words `aluminum`. **Tapping the cell opens the Pressure Detail overlay
(§3B)** — the cell is the drill-down target, no affordance chrome.

Outlook bands (Δ = current − 3h ago, hPa):

| Δ3h | Word | Color |
|---|---|---|
| ≤ −3.0 | Storm Risk | sunset |
| −3.0 to −1.0 | Clearing Slow | aluminum |
| −1.0 to +1.0 | Clear | aluminum |
| +1.0 to +3.0 | Clearing | aluminum |
| ≥ +3.0 | Wind Risk | sunset |

Any HIGH body-effect risk (§3B) also bubbles the word color to `sunset`.

## 3B · Pressure Detail (new overlay)

Full-screen overlay opened by tapping the pressure cell in Now Detail; tap
anywhere closes. Header: `speed` glyph 20px (10, 12) in the outlook color,
`Pressure` Title 30 (38, 6), raw reading Micro top-right
(`1008 HPA · −3.2 IN 3H`), rule 620 × 1 at y40. Vertical 1px `rivet` seams at
x160 and x432, y52–160, split three regions:

- **Outlook** (x10–160): outlook word Title 30 (10, 54), trend caption Micro
  (10, 92) — `FALLING FAST` / `FALLING` / `STEADY` / `RISING` / `RISING FAST`
  per the Δ bands.
- **24h graph** (x180–420): plot 240 × 90 at (180, 52). Gridlines 1px `rivet`
  at 1020 / 1015 / 1010 hPa; hPa labels 12px #4C555B inside-left; time labels
  Micro below at y150 — `−24H` x180, `−12H` x288, `NOW` x392 in `turquoise`.
  Polyline 2px through 24 hourly points: hours −24…−3 in `aluminum-dim`, the
  last 3 hours in the outlook color. Y-range fixed 1006–1022 hPa
  (y = 52 + (1022 − p) × 5.5), clamped.
- **Body effects** (x448–630): eyebrow Micro y48; four rows at y64 / y89 /
  y114 / y139 — glyph 16px `aluminum-dim` (`rheumatology`, `neurology`,
  `hearing`, `cardiology`), label Micro `aluminum` x472, risk dot 7px +
  risk word Micro right-aligned at x630. Risk colors: LOW `turquoise`,
  MEDIUM `oat`, HIGH `sunset`.

Body-effect thresholds (Δ = 3h change, P = current hPa, RH = humidity %,
T = °C):

- **Joint pain** — HIGH: Δ ≤ −2.5 or (P < 1008 and RH > 80);
  MEDIUM: Δ ≤ −1.0 or P < 1005; else LOW.
- **Migraine** — HIGH: |Δ| ≥ 2.5; MEDIUM: |Δ| ≥ 1.2; else LOW.
- **Sinus & ears** — HIGH: Δ ≤ −3.0; MEDIUM: Δ ≤ −1.5 or Δ ≥ +3.0; else LOW.
- **Heart strain** — HIGH: T < 5° and P > 1022; MEDIUM: T < 10° and P > 1018;
  else LOW.

Any HIGH bubbles `sunset` up to the header glyph here and to the pressure
cell's word in Now Detail.

## 4 · Quick Settings

A **640 × 130 top sheet** (was full-screen), `surface`, radius 0/0/8/8,
shadow 0/6/12 `ground` 50%, sliding down 220ms ease-out from y = −130. The
Today content behind dims to 30%. Drag handle 32 × 3 `rivet`, bottom-center.

Header row y8: eyebrow left, `IP · OTA HOST` Micro right; rule y30. Three
control columns, eyebrow y42, control y58, status Micro y96:

- **Brightness** x10: value Micro at x106; slider 296 × 10 r5 at y64 —
  track `rivet`, fill + 20px knob `oat`; status `SUN-DRIVEN · DIMS AT 7:18 PM`.
- **AUTO pill** 72 × 24 r12 at x322: filled `turquoise` / `ground` text when
  auto; `surface-hi` / `aluminum-dim` text when a manual level holds.
- **Forecast** x418: pill 118 × 24 r12 `surface-hi` with `refresh` glyph 13px +
  `REFRESH`; status `4 MIN AGO` (must clear the Wi-Fi column at x556).
- **Wi-Fi** x556: four signal bars 4px wide, heights 6/10/14/18, gap 2, lit
  `turquoise` per 25% of RSSI range, unlit `rivet`; SSID Micro `aluminum` y96.

## 5 · Night Mode

New drawn state (shipped firmware only dims the backlight). Enters when the
auto-dim target ≤ `BL_LEVEL_DEEPNIGHT`, exits on presence boost; both ways
1500ms cosine.

Hero geometry unchanged; hero recolors `oat` → **#B3AB9C** (oat mixed 45%
toward `aluminum-dim`), condition Body in `aluminum-dim`, clock Micro
**#4C555B** at the place position. Strip compresses to **5 columns × 86px**
from x208: hour label Micro #4C555B at y44, temperature Title 30
`aluminum-dim` (ramp suppressed) at y62. No icons, ribbon, precip, wind, or
page indicator. Seam dims to `surface-hi`.

## 6 · Boot / Connecting

Rivet motif rows — 2px dots every 16px from x16 — at y20 and y158. Progress
arc 56px at (176, 62): track 3px `rivet`, indicator a 90° `turquoise` arc
rotating one revolution per 1.6s, linear. Text block at x252: eyebrow Micro
`AIRSTREAM WEATHER` y62; status Body 20 `aluminum` y80 stepping
`CONNECTING TO WI-FI` → `FINDING LOCATION` → `FETCHING FORECAST` via 400ms
cross-fade; queue line Micro #4C555B y108. Never a blank screen, never a
wordless spinner.

## 7 · Setup / No Wi-Fi

Eyebrow Micro `sunset` `NO WI-FI CONFIGURED` (10, 16); instruction Body
(10, 38); **AP chip** at (10, 74): 52px pill r26, `surface` bg, 1px
`turquoise` border, 24px padding — `wifi` glyph 24px + `Airstream-Weather` in
Title 30, both `turquoise` (the only turquoise on screen). Footnote Micro
(10, 146). Steps column Micro at x480, y38, 29px pitch: JOIN → PICK YOUR HOME
WI-FI → DONE.

## 8 · Icon set

Material Symbols Rounded stays (wght 400, FILL 0) — it shares Jost's geometric
construction, recolors as text, and is already subset by
`tools/build_fonts.sh`. Codepoints unchanged from `icons.h`. Color rules:

- Hourly strip: every glyph takes its column's temperature-ramp color.
- Now zone / overlays: sun-bearing → `oat`; cloud, fog, night → `aluminum`;
  rain, drizzle, thunder → `turquoise`; snow, freezing → `sky`.

## Beyond the tokens

Things `tokens.json` cannot express, with their measurements:

- **kPrecipBarW 2, kPrecipBarMaxH 24, kPrecipBarBottomY 152** — the hourly
  probability bar (suppressed < 10%).
- **Wind arrow**: icons_ui `navigation` at 11px, `lv_obj_set_style_transform_angle`
  = (wind_dir + 180) × 10, pivot center; hidden < 3 mph. Cardinal text drops
  from the strip (survives in overlays).
- **Seam rivet dots**: 2px, every 16px, x206, y16 start.
- **kDetailPanelX 170, kDetailPanelW 300** — Hour Detail center panel; 2px
  `turquoise` top border; neighbor-hour columns at x20/63/106 and
  x491/534/577 are tap targets that retarget the panel.
- **kSheetH 130** — Quick Settings sheet height; drag handle 32 × 3.
- **Night Mode**: new screen state, spec in §5; colors #B3AB9C and #4C555B are
  derived, not tokens.
- **Sun arc**: 192px, sun-glyph knob on a 28px ground puck.
- **Pressure module**: kPressureSparkW 56, kPressureSparkH 14 (6h, 7 pts);
  Pressure Detail overlay per §3B (graph 240×90 at 180,52; seams x160/x432;
  bio rows y64/89/114/139). Needs 24h of hourly pressure retained + RH and
  temp for the body-effect thresholds.
- **Press feedback**: 90ms scale to 0.97 + `surface-hi` fill (docs/UX.md
  currently specifies fill only — update it alongside this).

## docs/UX.md deltas

Gesture map unchanged. Three contract edits: press feedback gains the 90ms /
0.97 scale; Hour Detail becomes a center panel — tap the panel to close, tap a
visible neighbor hour to retarget it; Quick Settings becomes a top sheet (tap
the dimmed content below to dismiss).
