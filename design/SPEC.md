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

Hourly temperatures move to the 30px Title size, **but 30px does not hold
three digits in a 42px column** — Jost Medium measures ~52px at `100`, where
the 24px cut measures ~41px. So both cuts ship:

- `font_hour` becomes **30px** (was 24) — used for 1–2 digit temperatures,
  which is every hour in a normal day.
- `font_hour_narrow` **stays at 24px** — the existing cut, kept rather than
  retired, and selected per label whenever the formatted value is 3
  characters (≥ 100 °F or ≤ −10 °F).

Cost, stated plainly: this is one **new** 30px cut generated in
`tools/build_fonts.sh` and no cut removed, so hourly type costs more flash
than today rather than less. Swap the label's font at update time
(`lv_obj_set_style_text_font`) — no relayout needed, both cuts are centered
in the same 42px column.

Every other cut is unchanged.

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

Hourly strip — **ten 42px columns from x = 210**, ending at x630, the 10px
safe line. (The shipped `208 + i × 43` pitch ends at 638, inside the masked
corner; the strip zone still splits at x208, but the drawn columns inset to
respect the margin.) Hairline separators 1px `rivet` at 50% opacity, y20–y112:

| Row | y | Spec |
|---|---|---|
| Hour label | 6 | Micro, `aluminum-dim`; current hour `NOW` in `turquoise` |
| Condition glyph 20px | 22 | icons_sm, colored by temperature ramp |
| Temperature | 42 | **Title 30**, ramp color, no degree sign; falls back to the 24px `font_hour_narrow` cut at 3 digits (see Font cuts) |
| Trend ribbon | 78, h 30, x210 w420 | 2px polyline through hour centers, ramp-colored; fill below fades with squared falloff from 70/255 max opacity (unchanged algorithm, band moved/resized) |
| Precip % | 112 | Micro, `turquoise`, shown only ≥ 10% |
| Precip bar | bottom edge y152 | 2px wide, centered, height = prob × 0.24 (0–24px), `turquoise`; suppressed with the % |
| Wind | 156 | `navigation` glyph 11px (icons_ui) rotated to (wind_dir + 180)°, hidden < 3 mph, + integer speed, Micro, `sky` |

`NOW` column keeps its `surface` slab (x210, y4, 41 × 158, r5). Page indicator
unchanged: 12×3 `turquoise` bar + 4×3 `rivet` dots, gap 4, bottom y174.

## 1B · System

The second page (swipe left from Today). The shipped screen packs eight
fields into four 147px columns, which is what forces values like
`airstream-weather.local` and `213K / 7842K PSRAM` to wrap. This pass widens
the grid to **three 200px columns** and moves two things out of it.

Top bar: `System` Title 30 (10, 6). **Memory moves here**: a 7px status dot
plus `HEAP 213K · PSRAM 7842K` Micro at (124, 14) — dot `turquoise`,
`sunset` when free heap < 40K. `CHANGE NETWORK` pill 168 × 24 r12,
`surface-hi`, right-aligned at y8 (its touch target extends to the bar edges
for a 44px height). Header rule 620 × 1 at y40, punctuated by **2px rivet
dots every 16px at y44** — the seam motif read horizontally.

Grid: three 200px columns at x10 / x220 / x430, vertical 1px `rivet`
hairlines at x219 and x429 (y56–134); rows at y56 and y104, each a Micro
label with its value 6px below. Human values take **Body 20** `aluminum`;
machine strings take **Label 15** so nothing wraps at 200px:

| | col 1 | col 2 | col 3 |
|---|---|---|---|
| row 1 | NETWORK · `Basecamp` (Body) | IP ADDRESS · `192.168.1.87` (Body) | LOCATION · `Denver` (Body) + coords Micro |
| row 2 | UPDATE HOST (Label) | TOUCH CONTROLLER (Label) | DISPLAY `640 × 180` + `RGB565` Micro |

Wi-Fi **signal bars** sit beside the network value at (116, 62): four 4px
bars, heights 5 / 8 / 11 / 14, lit `turquoise` per 25% of RSSI range, unlit
`rivet`.

**Uptime and build leave the grid** for a footer line in Micro #4C555B at
(10, 148): `UP 3H 42M · v1.0.0 · BUILD 2026-09-09`. Page indicator unchanged,
second page active.

**Brightness is removed** from this screen — it lives in Quick Settings, one
swipe away, and duplicating it here earned a grid cell for nothing.

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
**#4C555B** at the place position. Strip compresses to **5 columns × 84px**
from x210 (ending at the 630 safe line): hour label Micro #4C555B at y44, temperature Title 30
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

The AP is **password-protected**, and the screen carries three ways in: the
name, the password, and a scannable join code.

Header: eyebrow `GET STARTED` Micro in `aluminum-dim` (10, 16) — neutral, not
an error; instruction `Join this network from another device:` Body (10, 34).

Left column — **steps** Micro at (10, 78), 29px pitch, no wrap:
`1 JOIN THE NETWORK` / `2 PICK YOUR WI-FI` / `3 DONE`.

Center column — the **network identity**, in the visual middle where the eye
lands: AP chip at (178, 68), 46px pill r23, `surface` bg, 1px `turquoise`
border, 18px padding, width fits content — `wifi` glyph 22px +
`Airstream-Weather` in Title 30, both `turquoise`. Password below:
`PASSWORD` Micro `aluminum-dim` (178, 120), value Title 30 `oat` (178, 134),
grouped 4 + 4. Set both labels to line-height 1 (LVGL: `lv_style_set_text_line_space(0)`)
so the 30px cut’s box ends at y162, clear of the 10px bottom safe line. Turquoise stays on the network identity and `oat` on the
password so the two read as separate facts. ~25px gutter to the QR.

**QR join code** at (521, 54), 99 × 99, with its caption above: QR version 3, 29 × 29 modules, ECC
level L, 3px per module plus a 6px quiet zone. Dark modules `ground` on an
`aluminum` field — scanners need a light ground, so this patch inverts
deliberately. Payload is the standard Wi-Fi URI, which iOS and Android both
join directly from the camera:

```
WIFI:T:WPA;S:Airstream-Weather;P:<password>;;
```

Caption `SCAN TO JOIN` Micro centered above it at y28, 14px clear of the code. Implement with LVGL's
built-in `lv_qrcode_create` (dark = `COL_GROUND`, light = `COL_ALUMINUM`,
size 87 + 6px padding) — no new assets, no external encoder.

**Password rule.** 8 digits, generated once at first boot and persisted in
NVS; rotates only on factory reset. WPA2-PSK enforces an **8-character
minimum**, so a 6-digit password cannot be used on a protected AP — the extra
two digits are a protocol requirement, not a design choice. Display grouped
4 + 4 for readability at four feet.

## 8 · Icon set

Material Symbols Rounded stays (wght 400, FILL 0) — it shares Jost's geometric
construction, recolors as text, and is already subset by
`tools/build_fonts.sh`. Codepoints unchanged from `icons.h`. Color rules:

- Hourly strip: every glyph takes its column's temperature-ramp color.
- Now zone / overlays: sun-bearing → `oat`; cloud, fog, night → `aluminum`;
  rain, drizzle, thunder → `turquoise`; snow, freezing → `sky`.

## Beyond the tokens

Things `tokens.json` cannot express, with their measurements:

- **kStripX 210, kHourColW 42** — the drawn hourly columns (the 208 zone
  split is unchanged; only the drawn strip insets to the safe line).
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
- **Setup AP**: `AP_PASSWORD_LEN 8` (NVS-persisted, first-boot random),
  `lv_qrcode` 87px + 6px padding at (521, 54), payload
  `WIFI:T:WPA;S:<ssid>;P:<pw>;;`. The AP changes from open to WPA2 — softAP
  init needs the password argument.
- **Press feedback**: 90ms scale to 0.97 + `surface-hi` fill (docs/UX.md
  currently specifies fill only — update it alongside this).

## docs/UX.md deltas

Gesture map unchanged. Three contract edits: press feedback gains the 90ms /
0.97 scale; Hour Detail becomes a center panel — tap the panel to close, tap a
visible neighbor hour to retarget it; Quick Settings becomes a top sheet (tap
the dimmed content below to dismiss).
