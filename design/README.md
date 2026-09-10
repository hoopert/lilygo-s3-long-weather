# Handoff: Airstream Weather Panel — design refinement pass

For Claude Code, working in `hoopert/lilygo-s3-long-weather`
(branch `claude/lilygo-s3-weather-app-5z5m8s`).

## Overview

A refinement pass on the shipped 640 × 180 LVGL 8 weather panel UI
(LilyGo T-Display S3 Long, ESP32-S3, AXS15231B, RGB565). Same palette, motion
timings, and gesture map; changed layouts, one retired font cut, one new
overlay (Pressure Detail), and one new screen state (Night Mode).

## About the design files

`artboards/*.dc.html` are **HTML design references** — they show intended
look and behavior at 1:1 pixel scale but are not production code. The task is
to apply the changes to the existing LVGL firmware
(`firmware/src/ui/…`), using its established widget patterns. `exports/*.png`
are 1:1 renders of each artboard for visual diffing.

## Fidelity

**High-fidelity.** Every coordinate, size, and color in SPEC.md is a firmware
value, measured in app space (640 × 180 after rotation), matching how
`screen_today.cpp` places widgets today. Recreate pixel-perfectly.

## File roles & precedence

1. `tokens.json` — mechanical. Keys are the real firmware constant names;
   apply to `firmware/src/ui/theme.h`, `theme.cpp`, and the `k*` constants in
   `screen_today.cpp`. Diff-worthy changes: `font_hour.size` 24 → **30**
   (see Fonts below) and the `layout.today` block.
2. `SPEC.md` — the contract. Per-artboard measurements, everything the tokens
   schema can't express (§Beyond the tokens), and the `docs/UX.md` deltas at
   the end. Where SPEC.md and an artboard PNG disagree, SPEC.md wins.
3. `logic.json` — machine-readable pressure-outlook bands and the four
   biometric threshold rules (mirrors SPEC.md §3/§3B; same content, parseable).

## Implementation order

1. Apply `tokens.json` (theme + k-constants).
2. Fonts: hourly temps move to 30px, but **keep the 24px cut too** — 30px
   does not fit three digits in a 42px column, so labels fall back to
   `font_hour_narrow` at ≥100°. Net: one new cut generated, none removed
   (see SPEC.md §Font cuts). Add Material Symbols codepoints for the new glyphs:
   `navigation`, `speed`, `rheumatology`, `neurology`, `hearing`,
   `cardiology` (subset per existing pipeline in `icons.h`).
3. Today screen per SPEC §1: 42px columns from x210 (the strip insets to the
   10px safe line), 30px hour temps with the 3-digit fallback, wind arrow + speed, precip
   bars, seam rivet dots, re-spaced column rhythm.
4. Hour Detail per §2: center panel replacing the full-screen overlay,
   neighbor-hour tap targets, 3 × 2 grid.
5. Now Detail per §3: 192px sun arc with sun-glyph knob, 30px metric values,
   pressure micro trend module (sparkline + outlook word, tap → Pressure
   Detail).
6. **New:** Pressure Detail overlay per §3B + `logic.json` (24h graph, body
   effects). Requires retaining 24 hourly pressure samples plus RH and temp —
   see `data_requirements` in logic.json; extend the weather store in
   `firmware/src/net/weather.cpp` accordingly.
7. System page per §1B — three 200px columns, memory + status dot in the top
   bar, uptime/build to a footer line, brightness removed (it lives in Quick
   Settings).
8. Quick Settings per §4 (130px top sheet), Night Mode per §5 (new state),
   Boot per §6. Setup per §7 — this one changes behavior, not just layout:
   the provisioning AP becomes **WPA2-protected** with an 8-digit password
   generated at first boot and persisted in NVS, shown on screen alongside an
   `lv_qrcode` Wi-Fi join code. Note WPA2's 8-character PSK minimum.
9. Update `docs/UX.md` per SPEC's final section (press-feedback scale, panel
   overlays, new pressure drill-down gesture).

## Adapting to other devices

This edition targets the T-Display S3 Long's 640 × 180 canvas. To port:

- Keep all layout in **app space** with a single rotation/mapping layer at
  the driver, as the firmware does now — never bake device orientation into
  screen code.
- The handoff schema is per-canvas: a new device gets its own
  `tokens.json` (`canvas`, `layout`) while `colors`, `temperature_ramp`,
  `type` roles, `motion`, and `logic.json` carry over unchanged. Don't scale
  the 640 × 180 layout to a different aspect — re-derive the zone split and
  column count from the design rules in SPEC.md (43px hour columns, 10px safe
  margins, zone seam, type roles).
- Type sizes are bitmap cuts: regenerate per device resolution if the glance
  distance differs; keep the five-role scale.

## Files

- `tokens.json`, `logic.json`, `SPEC.md` — as above
- `artboards/Redesign Artboards.dc.html` — all 10 artboards + swatches, type
  specimen, motion spec, with measurement annotations
- `artboards/Current UI (shipped, reference).dc.html` — the shipped UI
  recreated 1:1, for before/after diffing
- `exports/01…08` PNGs — one per artboard (01b System and 03b Pressure Detail
  are new)
