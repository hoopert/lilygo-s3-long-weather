# Claude Design Prompt — Airstream Weather Panel

Copy everything between the `---` rules into Claude Design.

The prompt is written to be **self-contained**: it carries the hardware constraints,
the render-engine constraints (LVGL 8 on an ESP32-S3), the brand direction, and the
exact content model. Do not trim the constraint sections — they are the reason the
output will be buildable rather than merely pretty.

---

## ROLE

You are designing the complete UI for a **wall-mounted 3.4" touchscreen weather panel**
installed in the galley bulkhead of a **1970s Airstream travel trailer**. This is a
physical product surface, not a web page. Every pixel you place is a real pixel on a
real panel that someone glances at from four feet away while making coffee.

## THE CANVAS — READ THIS TWICE

- **Artboard size: exactly 640 × 180 px. Landscape. No exceptions, no scaling, no @2x.**
- This is a **3.56:1 letterbox strip** — roughly the proportions of a car dashboard or
  a Cinemascope frame. It is NOT a phone, NOT a tablet, NOT a card. Do not design a
  centered card floating in space. **The design must run edge to edge, corner to
  corner, and use the full horizontal run.** Horizontal space is abundant; vertical
  space is precious and nearly exhausted.
- Design at 1:1. 1 design px = 1 device px. A 12px label is genuinely 12px tall.
- The physical panel is ~73mm × 20mm of glass. At a 4-foot glance distance, the
  smallest legible text is about **12px**, and the primary reading target wants to be
  **60px or larger**.
- **Safe margins: 10px on all four sides.** The bezel is tight and the corners of the
  panel are slightly masked. Nothing critical inside 10px of any edge.

## RENDER ENGINE CONSTRAINTS (LVGL 8 on ESP32-S3)

Design only with primitives this engine can actually draw. Anything outside this list
will not survive the port and will be substituted with something worse.

**Available:**
- Rectangles with per-corner radius, borders, and outlines
- Linear gradients — **two stops only, vertical or horizontal only** (no radial, no
  angular, no multi-stop, no mesh)
- Arcs / rings (great for gauges, dials, progress)
- Polylines and 1–4px strokes (great for trend lines and sparklines)
- Box shadows on rectangles (soft, single color, offset + spread)
- Bitmap images and recolorable monochrome masks
- Text with per-object color, letter-spacing, and line-height
- Opacity on any object

**NOT available — do not use:**
- Backdrop blur / frosted glass / any blur filter
- Gradients on text or on strokes
- Radial or multi-stop gradients
- Text shadows, text outlines, or text along a path
- Blend modes beyond normal/additive
- True transparency compositing against live video

**Color depth is RGB565 (16-bit).** Large, slow gradients **will band visibly**. Prefer
flat fills and short, high-contrast gradient runs. Avoid subtle 5%-opacity washes across
large areas; they will posterize into stripes.

## TYPOGRAPHY

The Airstream's design language is **Streamline Moderne** — the same 1930s aerodynamic
optimism that produced the Zephyr locomotive and the Airstream Clipper. The correct
type voice is **geometric sans**, Futura-lineage: perfect-circle bowls, single-story
`a`, high-contrast cap-to-x-height ratio, generous letter-spacing on small caps.

- **Use `Jost*`** (an open Futura interpretation) as the primary family. If unavailable,
  use `Montserrat`. Do not substitute Inter, Roboto, SF, Helvetica, or any neo-grotesque
  — they read as "software" and will kill the period feel.
- Numerals must be **lining and tabular** — temperatures change every minute and must
  not shift the layout as digits change width.

Type scale (fixed — the firmware ships exactly these cuts, so design to them):

| Role | Font | Size | Tracking | Use |
|---|---|---|---|---|
| Hero | Jost* SemiBold | 72px | -2% | The current temperature, and nothing else |
| Title | Jost* Medium | 30px | 0 | Hour-column temperatures, overlay headlines |
| Body | Jost* Regular | 20px | 0 | Condition text, secondary values |
| Label | Jost* Medium | 15px | 0 | Hour labels, unit suffixes, wind speeds |
| Micro | Jost* Medium | 12px | **+8%, UPPERCASE** | Section eyebrows, status, metadata |

Never use more than five type sizes on one artboard. Never center long text.

## COLOR — "Polished Aluminum & Desert Dusk"

Dark-first. The panel lives indoors in a trailer, is often viewed at night, and a bright
UI at 2am is a hostile object. Night is the default; day is the variant.

**Night palette (default):**

| Token | Hex | Use |
|---|---|---|
| `ground` | `#0E1113` | Page background — near-black with a cool cast, not pure black |
| `surface` | `#171B1E` | Raised panel / hour columns |
| `surface-hi` | `#22282C` | Selected or pressed state |
| `rivet` | `#2E353A` | 1px separators, hairlines, the "seam" between panels |
| `aluminum` | `#C9D1D6` | Primary text — brushed-metal off-white, never `#FFFFFF` |
| `aluminum-dim` | `#7C878E` | Secondary text |
| `oat` | `#E8DCC8` | Warm cream — the hero temperature, the one warm light in the dark |
| `turquoise` | `#3FBFB0` | THE Airstream accent. Precipitation, water, active states, selection |
| `sunset` | `#E2703A` | Heat, high-UV, alerts, "hot" end of the temperature ramp |
| `sky` | `#6FA8C7` | Wind, cool end of the temperature ramp |

**Day palette (variant):** same structure, inverted — `ground #F3F0E9` (warm oat paper),
`surface #FFFFFF`, `aluminum #1E2427` for text, accents unchanged but darkened ~12% for
contrast on light ground.

**The rivet line is the signature.** Airstreams are defined by riveted aluminum panel
seams. Use a **1px `rivet`-colored vertical hairline between each hour column**, and a
single 1px horizontal hairline separating the header strip. Optionally punctuate long
seams with 2px dots every 16px — an actual rivet line. This is the one ornamental move
allowed; do not add others.

**Temperature must be color-encoded**, mapping a continuous ramp:
`sky #6FA8C7` (≤32°F) → `turquoise #3FBFB0` (50°F) → `oat #E8DCC8` (68°F) →
`sunset #E2703A` (≥90°F). Apply this ramp to the hourly temperature values and to the
trend ribbon. It should be readable as a heat map at a glance, before any number is read.

## LAYOUT — SCREEN 1: "TODAY" (the primary artboard)

One 640×180 frame, split into two zones by a vertical rivet seam at **x = 208**.

**LEFT ZONE — "NOW" (0–208px):** the glance target. Readable from across the trailer.
- Current temperature as the **72px hero** in `oat`, with a small `°` and unit suffix.
  Baseline-align the degree mark to the cap height, not the baseline.
- A **56×56 weather condition icon** — see the icon direction below.
- Condition text (`Body`, `aluminum`) — "Partly Cloudy", "Light Rain".
- A single line of `Micro` metadata: `FEELS 71°  ·  H 84°  L 58°`
- Location + last-updated time in `Micro`, `aluminum-dim`, bottom-left.

**RIGHT ZONE — "NEXT 10 HOURS" (208–640px):** ten equal columns, ~43px each,
separated by `rivet` hairlines.

Each column, top to bottom:
1. Hour label — `Micro`, uppercase, `aluminum-dim`. `2PM`, `3PM`… Mark the current
   hour as `NOW` in `turquoise`.
2. A 20×20 condition glyph.
3. Temperature — `Title`, colored by the temperature ramp.
4. Precipitation chance — `Label` in `turquoise`, **shown only when ≥ 10%**. Below it, a
   2px-wide vertical `turquoise` bar whose height encodes the probability (0–24px).
   Suppress the whole element at 0%; an empty column is better than a row of "0%".
5. Wind — `Label` in `sky`: a small direction arrow glyph (rotated triangle, pointing
   the way the wind is *going*) plus integer speed. Suppress the arrow below 3mph.

**Behind the columns**, draw a **temperature trend ribbon**: a 2px polyline through the
ten temperature points, with a soft two-stop gradient fill fading to transparent below
it, at ~25% opacity. It sits *behind* the text and *above* the surface. This is what
makes the panel beautiful — the shape of the day is legible before any number is read.
Keep it subtle; it is a ground, not a figure.

## THE OTHER ARTBOARDS

Design these as separate 640×180 artboards. They are overlays and states, and the
firmware needs them specified as precisely as Screen 1.

2. **Hour Detail overlay** — raised from tapping one hour column. The tapped column stays
   in place and highlights (`surface-hi`, `turquoise` 2px top border); the rest of the
   strip dims to 30%. A detail panel slides in over the left zone showing that hour's
   full data: temperature, feels-like, precipitation probability *and* accumulation,
   wind speed / gust / cardinal direction, humidity, cloud cover, dew point. Arrange as
   a **two-column label/value grid**, `Micro` labels above `Body` values. Include a
   clear affordance for dismissal.

3. **Now Detail overlay** — from tapping the left zone. Full current conditions:
   sunrise / sunset with a horizon arc showing the sun's current position along it,
   today's high/low, humidity, UV index, pressure with 3-hour trend arrow, wind gust,
   visibility. The sun arc is the hero element — an LVGL arc, `oat` on `rivet`, with a
   filled dot at the current position.

4. **Quick Settings sheet** — swipe down from the top edge. A panel covering the top
   ~130px, sliding down over the content. Contains: a brightness slider (with an `AUTO`
   pill that lights `turquoise` when auto-dimming is engaged), a refresh button showing
   time-since-last-update, Wi-Fi SSID + signal bars, and the device IP in `Micro`.

5. **Night Mode state** — the Today screen as it renders at 2am at 10% backlight. Reduce
   to essentials: hero temperature, condition, and a compressed 5-hour strip. Deep-dim
   everything non-essential. Accents desaturate toward `aluminum-dim`. This should feel
   like an instrument panel at cruising altitude.

6. **Boot / Connecting state** — an Airstream silhouette or a riveted-seam motif,
   `turquoise` progress arc, and honest status text (`CONNECTING TO WI-FI`,
   `FETCHING FORECAST`). It must never show a blank screen or a spinner with no words.

7. **Setup / No Wi-Fi state** — instructs the user to join the `Airstream-Weather` access
   point. Show the AP name large and legible from four feet, since this is read while
   holding a phone in the other hand.

8. **Weather icon set** — a single artboard laying out all glyphs at both **56px** and
   **20px**: clear day, clear night, partly cloudy day, partly cloudy night, overcast,
   fog, drizzle, rain, heavy rain, freezing rain, snow, heavy snow, thunderstorm,
   thunderstorm with hail. Style: **2px uniform-weight strokes, rounded caps, geometric
   construction, monochrome with a single `turquoise` accent stroke for precipitation
   and `oat` for the sun.** They must remain legible at 20px — test by designing the
   20px versions first, then scaling up. Do not use filled/duotone/gradient icons.

## MOTION

Specify durations and easing for each of these; the firmware implements them literally.
- Screen-to-screen: 280ms horizontal slide, ease-in-out
- Overlay in: 220ms slide-up + fade, ease-out
- Value change: 400ms cross-fade on the number, no motion on the container
- Brightness ramp: 1500ms, cosine ease — should be imperceptible
- Press feedback: 90ms scale to 0.97 + `surface-hi` fill

## WHAT WOULD MAKE THIS FAIL

- Designing a phone layout letterboxed into a wide frame
- Any element inside the 10px safe margin
- Text below 12px
- Pure `#FFFFFF` or pure `#000000` anywhere
- Neo-grotesque type (Inter/Roboto/Helvetica)
- Blur, glass, or radial gradients
- Decorative flourishes beyond the single rivet-seam motif
- More than one accent color competing for attention in the same zone
- A design that is beautiful at 4x zoom but unreadable at 1:1

## DELIVERABLE

Eight artboards, each exactly 640×180 (the icon sheet may be taller), laid out on one
canvas in the numbered order above, each labeled. Include a small swatch strip showing
the eleven color tokens with their hex values and token names, and a type specimen strip
showing the five cuts. Annotate key measurements — zone boundaries, column widths, and
the vertical position of each text baseline — as the firmware will be built directly
from these numbers.

---

## After Claude Design produces the artboards

Feed the result back into the firmware with:

> Here are the finished artboards. Update `firmware/src/ui/theme.cpp` with the final
> token values, and rebuild `screen_today.cpp` to match the annotated measurements
> exactly. Keep the existing `ScreenManager` registration and gesture bindings intact.

The firmware in this repository already implements the layout, palette, gesture map, and
motion timings described above, so the design work is a **refinement pass**, not a
rewrite. `docs/UX.md` is the contract between the two.
