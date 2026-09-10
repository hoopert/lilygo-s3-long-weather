# Architecture

Written for the version of you that comes back to this in eight months to add
tank levels.

## Shape

```
                    core 1  (Arduino loop)              core 0
        ┌──────────────────────────────────────┐   ┌──────────────────┐
        │  loop()                              │   │  weather task    │
        │   ├─ net_tick()      portal, OTA     │   │   ├─ DNS         │
        │   ├─ buttons_tick()  press grammar   │   │   ├─ TLS         │
        │   ├─ backlight_tick() fade one step  │   │   ├─ HTTP GET    │
        │   └─ lv_timer_handler()              │   │   └─ JSON parse  │
        │        ├─ touch_read_cb  (50Hz)      │   └────────┬─────────┘
        │        └─ flush_cb → panel_push      │            │
        └───────────────────┬──────────────────┘            │
                            │        ┌───────────────┐      │
                            └────────┤   WxData      ├──────┘
                              reads  │  + mutex      │  writes
                                     └───────────────┘
```

The important property: **nothing on the render path ever blocks on the
network.** A slow TLS handshake or a DNS timeout costs nothing visible, because
it happens on the other core and the UI only ever reads a completed snapshot.

The handoff is a plain struct behind a mutex rather than a queue, because the UI
never wants a history — only the latest state — and a snapshot copy is simpler
to reason about than a stream of deltas.

## Layers

```
firmware/src/
├── main.cpp              boot order, LVGL wiring, the loop
├── display/
│   ├── panel.{h,cpp}     AXS15231B over QSPI
│   └── backlight.{h,cpp} LEDC PWM + the auto-dimming engine
├── input/
│   ├── touch.{h,cpp}     dual-controller probe and read
│   └── buttons.{h,cpp}   BOOT press grammar
├── net/
│   ├── net_manager.{h,cpp}  Wi-Fi portal, SNTP, OTA
│   └── weather.{h,cpp}      Open-Meteo client + the data model
└── ui/
    ├── theme.{h,cpp}        palette, type scale, temperature ramp
    ├── format.{h,cpp}       time and temperature formatting
    ├── icons.h              glyph codepoints
    ├── screen_manager.{h,cpp}  the screen registry and navigation
    ├── overlays.{h,cpp}     the three detail overlays
    ├── screens/             one file per screen
    └── fonts/               generated; do not edit by hand
```

Dependencies point downward only. `ui/` reads from `net/` and `display/`;
neither of those knows `ui/` exists. The one exception is deliberate:
`net/weather.cpp` calls `backlight_set_sun()` after each fetch, because the
sunrise and sunset times arrive with the forecast and the dimmer is the thing
that needs them.

## Rotation

The panel is physically 180×640. The UI is 640×180. LVGL never knows: it
renders the UI unrotated, full-frame, into one 640×180 buffer in PSRAM, and the
panel driver turns each finished frame into the glass's space on the way out.

```cpp
s_disp_drv.hor_res      = UI_WIDTH;    // 640 - the UI, not the panel
s_disp_drv.ver_res      = UI_HEIGHT;   // 180
s_disp_drv.full_refresh = 1;
s_disp_drv.sw_rotate    = 0;
```

`panel_push_frame()` does the turn, one 80-row band at a time through a
DMA-capable chunk in internal SRAM, using LVGL's own definitions of the two
rotations so `UI_ROTATION` keeps meaning what it always did:

```
ROT_270: panel(x, y) = frame(X = y,       Y = 179 - x)
ROT_90:  panel(x, y) = frame(X = 639 - y, Y = x)
```

`touch_read_cb()` applies the inverse to the digitiser's raw 180×640
coordinates, so a tap lands where it was made under either setting. To turn
the UI round, change `UI_ROTATION` in `config.h`; nothing else moves.

**Why not LVGL's `sw_rotate`?** It was the first design, and it produced a
torn, barely legible image on this glass. LVGL rotates each dirty area through
a scratch buffer of `LV_DISP_ROT_MAX_BUF` and flushes one narrow column band
per chunk, each with its own partial address window - and those windows land
wherever the dirty rectangle happened to be. The vendor's own demo only ever
writes full-height bands at aligned offsets; this UI's label-sized updates do
not. The driver-side turn writes exactly one window per frame - the full
screen, the same window the boot self-test uses - and that is the single
pattern proven to look right here. It also sidesteps stock LVGL's refusal to
combine `sw_rotate` with `full_refresh`, which the vendor works around by
patching LVGL (their "if you turn on software rotation, do not update or
replace LVGL" comment).

The cost is a whole-frame redraw on any change: roughly 10ms for LVGL to
render 640×180 into PSRAM, 12ms to rotate, 14ms on the wire at 32MHz over four
lines. LVGL only redraws when something is invalidated, so at rest the panel
receives nothing.

## Adding a screen

This is the part the project was structured for. A new screen is one file plus
one line.

**1.** Create `firmware/src/ui/screens/screen_tanks.{h,cpp}`:

```cpp
// screen_tanks.h
#pragma once
#include "ui/screen_manager.h"
const ScreenDef &screen_tanks_def();
```

```cpp
// screen_tanks.cpp
#include "screen_tanks.h"
#include "ui/theme.h"
#include "ui/icons.h"

namespace {

lv_obj_t *s_fresh = nullptr;

// Called once at boot. Build the widget tree onto `parent`, which is the
// screen. Keep handles to anything that changes.
lv_obj_t *create(lv_obj_t *parent) {
    lv_obj_t *title = theme_label(parent, &font_title, COL_ALUMINUM, "Tanks");
    lv_obj_set_pos(title, LAYOUT_SAFE, LAYOUT_SAFE - 4);

    s_fresh = theme_label(parent, &font_hour, COL_TURQUOISE, "--");
    lv_obj_set_pos(s_fresh, LAYOUT_SAFE, 60);
    return parent;
}

// Called about once a second while visible, and once for every screen whenever
// new data lands. Set values; do not build anything here.
void update(lv_obj_t *root) {
    LV_UNUSED(root);
    lv_label_set_text_fmt(s_fresh, "%d%%", read_fresh_water_percent());
}

const ScreenDef kDef = { "Tanks", ICON_DROP, create, update };

}  // namespace

const ScreenDef &screen_tanks_def() { return kDef; }
```

**2.** Register it in `main.cpp`. Registration order is swipe order:

```cpp
screens_register(screen_today_def());
screens_register(screen_tanks_def());     // <- new
screens_register(screen_system_def());
```

That is all. Navigation, the slide transition, the page indicator, gesture
routing, and the update cadence are all handled. The registry holds eight
screens; raise `kMaxScreens` if you need more.

### Conventions worth keeping

- **Build in `create`, set values in `update`.** `update` runs every second;
  creating widgets there leaks.
- **Use the theme.** `theme_label()`, the `COL_*` tokens, the five type cuts and
  the `LAYOUT_*` constants. A screen that invents its own colors will look wrong
  next to the others, and a design refresh will miss it.
- **`theme_decor()` for anything decorative.** `lv_obj_create()` makes objects
  clickable by default, and a 1px hairline that silently eats taps is a
  genuinely annoying bug to find.
- **Clip, do not wrap.** Set `LV_LABEL_LONG_CLIP` on anything in a narrow
  column. A wrapped line overlaps the row beneath it and looks like corruption.
- **Check `ui_gesture_recent()` in click handlers.** See
  [UX.md](UX.md#swipe--tap-disambiguation).

## Adding a data source

`weather.cpp` is the template. The shape that works here:

1. A `struct` of plain values — no pointers, no dynamic allocation — so a
   snapshot is a `memcpy`.
2. A task on core 0 that fills a local copy, then takes the mutex only to swap
   it in. Hold the lock for the assignment, never for the I/O.
3. A `..._snapshot(T &out)` for the UI and a consume-once update flag so the UI
   can redraw on change instead of polling.

Keep an eye on stack: a `WxData` is about 1.8KB, and the render path already
carries one. `weather_seconds_since_update()` deliberately reads a single field
under the mutex rather than taking a whole snapshot, for exactly that reason.

## Fonts

Eight cuts, generated by `tools/build_fonts.sh` into `firmware/src/ui/fonts/`
and committed, so a normal build needs no Node toolchain.

| Cut | Face | Size | Coverage |
|---|---|---|---|
| `font_hero` | Jost\* SemiBold | 72 | digits and marks |
| `font_title` | Jost\* Medium | 30 | full ASCII |
| `font_hour` | Jost\* Medium | 24 | digits and marks |
| `font_body` | Jost\* Regular | 20 | full ASCII |
| `font_label` | Jost\* Medium | 15 | full ASCII |
| `font_micro` | Jost\* Medium | 12 | full ASCII |
| `icons_lg` | Material Symbols Rounded | 56 | 14 weather glyphs |
| `icons_sm` | Material Symbols Rounded | 20 | weather + chrome |
| `icons_ui` | Material Symbols Rounded | 16 | chrome |

To add an icon: find its codepoint, add it to `WX_ICONS` or `UI_ICONS` in
`tools/build_fonts.sh`, add a `#define` to `ui/icons.h`, and re-run the script.
A glyph referenced in `icons.h` but missing from the subset renders as an empty
box — that mismatch is the only way to break this, so the two lists carry
comments pointing at each other.

## Things that will bite

| | |
|---|---|
| `LV_COLOR_16_SWAP` must be `1` | The AXS15231B wants big-endian RGB565; the ESP32 is little-endian and SPI transmits in memory order. Wrong value gives a recognisable but lurid image. |
| Do not turn `sw_rotate` back on | Stock LVGL refuses it alongside `full_refresh` (black screen, one error line), and without `full_refresh` it flushes partial windows that this glass renders torn. See [Rotation](#rotation). The driver rotates instead. |
| Serial can starve the main loop | With USB CDC on boot and no host attached, each write blocks up to 100ms. Anything logging per-frame makes the UI, the button and the Wi-Fi portal all go unresponsive while the device looks fine. `main.cpp` sets `Serial.setTxTimeoutMs(0)` so logging drops instead of blocking. |
| The panel can be asked what state it is in | `panel_report()` reads RDDID / RDDPM / RDDCOLMOD back over QSPI opcode `0x03` at 4MHz on a second, `NO_DUMMY` device handle and prints them at boot. A black screen with `display ON, sleep out` on the console is a pixel-path or backlight fault; `no reply on QSPI` is a wiring, power or reset fault. Don't debug a black panel without this line. |
| The vendor's init table has a one-byte bug that leaves this glass black | LilyGO's table encodes each entry's post-command delay in flag bits of the length byte (`0x40` = 20ms, `0x80` = 200ms). Its SLPIN entry carries `0x20` - neither flag - so SLPOUT follows SLPIN with no gap and the controller ignores it; the panel never wakes. (The same typo makes it send 32 zero parameter bytes, which turn out to be harmless.) Found by the boot probe: eight configurations, then a five-way bisection of the table, on the device. `kInitDcs` in `panel_init_tables.h` is the resting sequence; the bisection tables are kept for the next revision. |
| The pixel write path is the shipped factory binary's | CS held low for the whole frame, first chunk as opcode `0x32` + `0x002C00`, later chunks as raw data with no opcode or address (`WriteMode::kQuadHeld`). The vendor's `#else` path (CS toggled per chunk, `0x3C` continue) also works; both were probed. |
| `PANEL_BOOT_PROBE` cycles alternatives at boot | When set, boot walks candidate init tables for 2.5s each with a two-colour fill and a step number on the console. One flash answers "which does this glass want". It is what found the row above. Leave it at 0 once known - it lengthens every boot. |
| Touch coordinates **are** rotated by us | LVGL sees the UI unrotated, so `touch_read_cb()` maps the digitiser's raw 180×640 point into 640×180 with the inverse of `panel_push_frame()`'s turn. See [Rotation](#rotation). |
| Gesture limits are not settable in `lv_conf.h` | LVGL 8.4 hardcodes `LV_INDEV_DEF_GESTURE_LIMIT` and `LV_INDEV_DEF_LONG_PRESS_TIME` without an `#ifndef` guard. They are set on the indev driver in `main.cpp`. |
| Deleting an object inside its own event handler | Use `lv_obj_del_async()`. `overlays_dismiss()` does. |
| PSRAM is required | The frame buffer is 230KB. `main.cpp` fails loudly at boot rather than faulting somewhere unhelpful later. |
| CST3530 must be re-armed after every read | Write `0xD00002AB` or it stops reporting entirely after the first contact. |
| Known networks are remembered separately from WiFiManager's own credential | `net_manager.cpp` keeps its own list in the `wifinets` NVS namespace and upserts into it from `setSaveConfigCallback`, which WiFiManager fires only when the portal just saved and connected new credentials - never on an ordinary boot rejoining a network it already knew. `net_begin()` tries the strongest known SSID first and falls back to WiFiManager's `autoConnect()` (single last-saved credential) only if none are in range, which keeps a unit upgraded from earlier firmware working unmodified. `net_tick()`'s retry loop is a two-tick, non-blocking scan-then-join state machine (`RetryPhase`) - a blocking `WiFi.scanNetworks()` on the render loop would stall LVGL for the length of a Wi-Fi scan. |
