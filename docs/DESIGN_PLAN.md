# Design refinement: implementation plan

The plan for applying `design/` (the designer's hand-off: `SPEC.md`,
`tokens.json`, `logic.json`, ten artboards) to the firmware. Each phase is one
PR, flashable and reviewable on its own, ordered so the most visible change
lands first and the riskiest behaviour change lands last. Every phase names
what "done" looks like on the glass.

Precedence when the hand-off disagrees with itself: `SPEC.md` wins over a
PNG; `tokens.json` is applied mechanically. Where the hand-off disagrees with
something that shipped *after* the designer branched, this document says which
wins and why.

## What the review found

Things worth knowing before any phase starts.

| # | Finding | Consequence |
|---|---|---|
| F1 | **Pressure must be sea-level corrected.** The firmware fetches `surface_pressure`; at Denver's altitude that is ~830 hPa. The design's fixed 1006-1022 hPa graph range, the outlook bands and all four body-effect thresholds assume `pressure_msl`. | Phase 3 switches the current and hourly fields to `pressure_msl`. Nothing in the design changes; it was always written for MSL. |
| F2 | **The type cuts already match `tokens.json`** (72 / 30 / 24 / 20 / 15 / 12). The one change is hourly temperatures at 30px, and the firmware already carries a 30px Jost Medium cut (`font_title`). | No new font is generated. Hourly labels use `font_title`; the 24px `font_hour` is renamed `font_hour_narrow` and kept as the three-digit fallback. Zero flash cost, where SPEC expected one new cut. |
| F3 | **System is now a drawer on the left**, opened by swiping right (PR #16, after the designer branched). SPEC §1B says "swipe left from Today". | The drawer stays. The indicator already walks positions, so System's dot sits left of Today's, matching the artboard. UX.md keeps PR #16's gesture map. |
| F4 | **The Wi-Fi rework is backlogged** (BACKLOG #8). §1B's `CHANGE NETWORK` pill is the existing forget-and-reprovision action. | Phase 6 keeps one pill with today's behaviour. When #8 lands it gets a sibling. |
| F5 | **`transform_angle` on a label** (the wind arrow) is supported on any widget in LVGL 8.3+, and this is 8.4. | No image asset for the arrow; the `navigation` glyph rotates in place. |
| F6 | **QR code**: LVGL 8.4 ships `lv_qrcode` (`extra/libs/qrcode`), off by default. | `LV_USE_QRCODE 1` in `lv_conf.h`, ~4KB flash. No external encoder. |
| F7 | **The setup AP becomes WPA2** (§7). WiFiManager's `autoConnect(name, password)` already supports it. The password is generated once (8 digits, `esp_random`), kept in NVS, rotated by CHANGE NETWORK. | A behaviour change, not a layout one, so it is the last phase and its own PR. |
| F8 | **Night Mode** is a *drawn* state (§5), not a brightness level. Entry rule as specified - auto-dim target ≤ `BL_LEVEL_DEEPNIGHT` - means a manual brightness never triggers it. | Lives in the Today screen as a second layout, toggled from `backlight_tick()`'s target. |
| F9 | **Hour Detail's neighbour columns** make the overlay a full-screen composition (six neighbour tap targets around a centre panel), not a smaller sheet. | Built on the existing top-layer overlay root; the interaction-context framework's swipe policy is unchanged (up/down dismiss). |
| F10 | **`WxData` grows** from 36 forward hours to 24 past + 36 forward, with three fields added per hour. `weather_snapshot()` copies the whole struct onto the caller's stack. | ~3.6KB per snapshot on a 12KB loop stack. Fine, but Phase 3 measures it and adds a stack-high-water line to the heartbeat. |
| F11 | `docs/BOOT_ANIMATION.md` listed the type sizes wrong (64/28/16/14). | Corrected in this PR. |

## Decisions taken (defaults; say so if any is wrong)

- **D1** The System drawer keeps PR #16's swipe-right open / swipe-left close (F3).
- **D2** The setup AP becomes password-protected as designed (F7). A unit already provisioned is unaffected; the password only matters when the portal is up.
- **D3** Body-effect copy ships verbatim from `logic.json` ("Storm Risk", "HEART STRAIN"). It is a barometer heuristic, presented as one.
- **D4** Night Mode is Auto-only (F8); BOOT-button brightness never enters it.
- **D5** Hourly temperatures reuse `font_title` rather than generating a second 30px cut (F2).
- **D6** Glyphs the spec sizes at 22px (`wifi` in the setup chip, `light_mode` on the sun arc) use the existing 20px `icons_sm` cut. One new cut is generated, `icons_xs` at 11px, for the wind arrow and the refresh pill glyph; the four body-effect glyphs and `speed` join `icons_ui` / `icons_sm`.

## Phases

### Phase 1 - Foundation (this PR + one code PR)

Everything later phases lean on, with no visible change except the hourly
temperature size.

- Merge `design/` into `main` (this PR) with this plan.
- `lv_conf.h`: `LV_USE_QRCODE 1`.
- Fonts: `font_hour` → `font_hour_narrow`; `tools/build_fonts.sh` adds
  `icons_xs` (11px: `navigation` E55D, `refresh` E5D5) and the new codepoints
  `speed` E9E4, `rheumatology` (F0xx per sheet), `neurology`, `hearing` E023,
  `cardiology` to `icons_ui` and `icons_sm`; `icons.h` gains their macros.
- `theme.h/.cpp`: apply `tokens.json` (colours and motion are unchanged;
  `LAYOUT_*` unchanged). Add the two derived night colours as named constants
  with a comment that they are derived, not tokens.
- Press feedback: a shared pressed-state style with `transform_zoom` 248/256
  and `surface-hi` fill, 90ms transition, applied by `pill()` and the System
  buttons.
- **Done when:** builds; hourly temperatures render at 30px with the 24px
  fallback at three digits; everything else pixel-identical.

### Phase 2 - Today (§1) and Night Mode (§5) - merged

Implementation notes: the night strip shows every other hour so its five
columns span the same ten hours as the day strip; the page indicator is
found through `screens_indicator()` and fades with the day chrome; the place
line is untracked so `LOS ANGELES · 2:35P · 4 MIN AGO` still fits in 190px.

The most visible phase.

- Strip: ten 42px columns from x210, hairlines at 50%, `NOW` slab 41×158;
  glyph ramp-coloured; trend ribbon at y78 h30 x210 w420; precip % at y112
  and the 2px bar (height = prob × 0.24, bottom y152), both suppressed below
  10%; wind row at y156 as rotated `navigation` + integer speed, hidden
  below 3 mph; cardinal text dropped from the strip.
- Now zone: hero at (10,22), degree and unit per §1, icon at (146,10),
  condition y98, meta y124, place y146 with the M-less clock.
- Seam rivet dots: 2px every 16px at x206 from y16, as `theme_decor`.
- Night Mode: second layout on the same screen - five 84px columns, no
  icons/ribbon/precip/wind/indicator, hero in #B3AB9C, seam in `surface-hi`;
  1500ms cosine cross-fade both ways, driven from the backlight target.
- **Done when:** the Today artboard matches the glass at 1:1 in daylight;
  after 23:00 local in Auto the panel cross-fades to the night layout and a
  touch brings the day layout back.

### Phase 3 - Data: pressure history and MSL - merged

Implementation notes: the forward `hours[]` array is unchanged (hours[0] is
still the current hour, so no screen needed a `now_index`); the history is
a separate `pressure_history[25]` ending at the current hour, with
`pressure_delta_3h` computed once per fetch. `ui/pressure_logic.{h,cpp}` is
checked against `design/logic.json` by `tools/check_pressure_logic.py`
(8736 threshold-straddling cases) in a `logic-check` CI job. The heartbeat
prints `stack=` (the loop task's high-water mark, bytes).

Pure data, no UI. Unblocks Phases 4 and 5.

- Open-Meteo: add `past_hours=24`; hourly `pressure_msl`; current
  `pressure_msl` replaces `surface_pressure` (F1). RH and temp are already
  hourly.
- `WxData`: `WxHour` gains `pressure`; the array becomes 24 past + 36 forward
  with `now_index`; `hour_count` semantics documented.
- `ui/pressure_logic.{h,cpp}`: pure functions from `logic.json` -
  `pressure_delta_3h()`, `pressure_outlook(delta)` → word/caption/colour,
  `pressure_risks(delta, p, rh, t)` → four risk levels, and the bubble-up
  rule. No LVGL includes, so it compiles on the host.
- CI: a host-side check that compiles `pressure_logic.cpp` with `g++` and
  runs a handful of table cases from `logic.json`. Cheap, and it is the one
  piece of this design that is a spec rather than a picture.
- Heartbeat gains the loop task's stack high-water mark (F10).
- **Done when:** the console prints the 3h delta and outlook word after each
  fetch; the host check passes in CI.

### Phase 4 - Now Detail (§3) and Pressure Detail (§3B) - merged

Implementation notes: the sun is a 20px `icons_sm` glyph (the design's
22px has no cut) on a 28px puck positioned by angle; the arc object is the
full 192px circle with its lower half off-screen. Visibility comes from
Open-Meteo in metres and is shown in MI or KM. `DAYLIGHT` reads `NIGHT`
when `is_day` is false. The 24h graph right-aligns on NOW at 10px per hour
so a short history still ends at the right edge.

- Now Detail: 192px arc with the sun glyph riding it on a 28px ground puck
  (positioned by angle, not an image knob); `DAYLIGHT` and the two times
  inside; metrics 3×2 with unit suffixes in 12px; pressure cell as label +
  outlook word + 56×14 six-hour sparkline (an `lv_line`); tapping the cell
  opens Pressure Detail.
- Pressure Detail: new overlay kind. Outlook region; 24h `lv_line` graph
  240×90 with fixed 1006-1022 range, gridlines and labels; body-effects
  column with glyphs, risk dots and words. Tap anywhere closes; the
  interaction context accepts up/down.
- **Done when:** both artboards match at 1:1 with live data; the outlook word
  and header glyph go `sunset` when any risk is HIGH.

### Phase 5 - Hour Detail (§2) - merged

Implementation notes: a neighbour tap is applied on the next UI tick rather
than inside its own click event, because the re-render deletes the tapped
object; neighbours may reach past the ten-hour strip into the 36 fetched
hours, and a panel opened on one of those expands from its resting place;
the panel's 2px turquoise top edge is a separate bar because LVGL borders
are one width on every side.

- Centre panel x170-470 with `turquoise` top border and shadow, expanding
  from the tapped column (220ms); three neighbour hours each side as tap
  targets that retarget the panel; 3×2 metric grid; cloud cover, dew point
  and the standalone amount dropped.
- **Done when:** tapping an hour opens the panel on it; tapping a neighbour
  moves the panel without closing; tapping the panel closes it.

### Phase 6 - Quick Settings (§4) and System (§1B) - merged

Implementation notes: the sheet is drawn 8px taller and parked 8px above the
screen so LVGL's all-or-nothing corner radius shows only at the bottom; the
rivet row is one dashed `lv_line` (`theme_rivet_row`) rather than forty
objects; the signal bars are shared (`theme_signal_bars`) between the sheet
and System; the build date comes from `__DATE__`.

- Quick Settings: 640×130 top sheet over a 70% `ground` scrim; slides down
  220ms; drag handle; three control columns and the Wi-Fi bars; tap on the
  scrim or swipe up closes; the slider keeps owning its drags (PR #16).
- System: three 200px columns; memory dot + line in the top bar; signal bars
  beside the network; DISPLAY and TOUCH CONTROLLER cells; uptime/build to
  the footer; brightness removed; one `CHANGE NETWORK` pill (F4).
- **Done when:** both artboards match; long values (`airstream-weather.local`,
  the coordinates) no longer wrap.

### Phase 7 - Boot / Connecting (§6) and Setup (§7) - merged

Implementation notes: both layouts are layers on one LVGL screen
(`ui/startup.cpp`) that lives outside the strip - not registered, no
gestures - and is deleted by `screens_reveal()` once Today has faded in
over it. The status line is two labels that cross-fade. The setup password
is eight `esp_random()` digits under the NVS key `appw`; `CHANGE NETWORK`
rotates it before forgetting the network. `WIFI_AP_PASSWORD` is gone from
config.h - there is nothing to compile in.

The one phase that changes behaviour.

- Boot screen: a real screen shown from first frame until the forecast lands
  or the portal is needed, per the hand-off contract in `BOOT_ANIMATION.md`:
  rivet rows, 56px rotating arc, status stepping `CONNECTING TO WI-FI` →
  `FINDING LOCATION` → `FETCHING FORECAST` with 400ms cross-fades, queue
  line. Tap skips nothing here - there is nothing to skip to.
- Setup screen: replaces the `JOIN WI-FI ...` text on Today. Steps column,
  AP chip, password 4+4, `lv_qrcode` with the `WIFI:T:WPA;...` payload.
- AP becomes WPA2: 8-digit password generated at first boot, NVS-persisted,
  rotated by CHANGE NETWORK; `autoConnect(WIFI_AP_NAME, password)`.
  INSTALL.md and the installer page's "Then" steps updated - joining now
  needs the password or the code.
- **Done when:** a fresh unit boots to the boot screen, falls to Setup with a
  scannable code, joins from a phone camera, and lands on Today.

### Phase 8 - Docs - merged

- `UX.md` per SPEC's final section (press feedback scale, panel overlays,
  pressure drill-down); README and installer feature/gesture text; the
  BOOT_ANIMATION brief marked as delivered by §6; ARCHITECTURE's file map
  and bite list (opa recursion, `lv_line` point ownership, corner radius).

## Two flashes

The phases above are PR-sized for review; the board is flashed twice.

| Flash | Carries | Why this split |
|---|---|---|
| **1 - the redesign** | Phases 1, 2, 6, 5, 3, 4: foundation, Today + Night Mode, Quick Settings + System, Hour Detail, pressure data + logic, Now Detail + Pressure Detail | Everything the artboards show, every new feature, one flash. Layout is verified against the renders at 1:1 before each PR merges, and the pressure logic is host-tested in CI, so the only thing the first flash can find is what no render shows: touch targets, live data, motion on the real glass. |
| **2 - boot, setup, WPA2, and the fix round** | Phases 7 and 8, plus whatever flash 1 turned up | The one behaviour change (a password on the setup AP) rides with the corrections from flash 1, so it is tested on a panel whose UI is already known-good. |

PR order inside flash 1: 1 → 2 → 6 → 5 → 3 → 4, each merged on green
without a flash between them. Six PRs, one reflash, then a report of what to
check on the glass.

**Status.** Flash 1 merged as PRs #18, #19, #20, #21, #22, #23; flash 2's
phases 7 and 8 followed as #24 and #25 before the board was flashed at all,
so one flash carries everything and the fix round is whatever that flash
turns up. What to check on the glass is listed in
[docs/FLASH_CHECKLIST.md](FLASH_CHECKLIST.md).

**Fix round, from the first flash.** Findings F5 (a rotated `navigation`
glyph) and the press-feedback scale were wrong: LVGL 8.4 renders any
transformed widget through an alpha layer, and without
`LV_COLOR_SCREEN_TRANSP` (32-bit only) the layer cannot be created - the
widget is skipped and a warning is logged every frame. The wind arrow is now
one of eight compass glyphs in `icons_xs`; press feedback is the fill change
alone. The portal's synchronous network scan (13s with a phone attached)
is now preloaded and asynchronous, and a forecast transfer that drops
mid-body is retried within seconds rather than after a minute.
