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

The panel is physically 180×640. The UI is 640×180. LVGL bridges that with
**software rotation**, not the panel's MADCTL register:

```cpp
s_disp_drv.hor_res      = PANEL_WIDTH;   // 180 - the physical panel
s_disp_drv.ver_res      = PANEL_HEIGHT;  // 640
s_disp_drv.sw_rotate    = 1;
s_disp_drv.rotated      = LV_DISP_ROT_90;
s_disp_drv.full_refresh = 1;             // required alongside sw_rotate
```

`hor_res` and `ver_res` describe the **panel**, not the UI. LVGL then reports
640×180 to the application and rotates each frame on its way out.

**`full_refresh` must stay 0.** `draw_buf_rotate()` in `lv_refr.c` begins:

```c
if(disp_refr->driver->full_refresh && drv->sw_rotate) {
    LV_LOG_ERROR("cannot rotate a full refreshed display!");
    return;
}
```

That `return` happens before any flush, so the panel never receives a pixel:
a permanently black screen with one error line on the serial console and no
other symptom. LilyGO's factory example *does* set both — it ships a patched
LVGL, which is what its "if you turn on software rotation, do not update or
replace LVGL" comment is warning about. Against stock LVGL the two are
mutually exclusive, and partial refresh is the better fit here anyway: most
updates are a single label, so a small dirty rectangle beats repainting
640x180 every second.

Two further consequences worth knowing:

**Touch input is rotated by LVGL, not by us.** `indev_pointer_proc()` applies the
same 90° transform to pointer coordinates that it applies to the framebuffer, so
`touch_read_cb()` must report **raw panel coordinates**. Pre-rotating them lands
every tap 90° from where it was made. This is the single easiest thing to get
wrong in this file.

**Rotation happens in chunks.** LVGL rotates each dirty area through a scratch
buffer of `LV_DISP_ROT_MAX_BUF`, flushing once per chunk, so one logical redraw
becomes several `panel_push_pixels` calls. Growing that constant means fewer
chunks, but it is carved out of the `LV_MEM_SIZE` pool, so the two move together.

The draw buffers are a tenth of the screen each and live in **internal SRAM**,
because LVGL renders into them pixel by pixel and internal memory is far faster
for that than PSRAM. They fall back to PSRAM if internal allocation fails.

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
| `sw_rotate` + `full_refresh` is a black screen | Stock LVGL refuses the combination and returns before flushing. See [Rotation](#rotation). Copying LilyGO's example verbatim walks straight into this, because theirs runs on a patched LVGL. |
| Serial can starve the main loop | With USB CDC on boot and no host attached, each write blocks up to 100ms. Anything logging per-frame makes the UI, the button and the Wi-Fi portal all go unresponsive while the device looks fine. `main.cpp` sets `Serial.setTxTimeoutMs(0)` so logging drops instead of blocking. |
| Touch coordinates must **not** be pre-rotated | LVGL already does it. See [Rotation](#rotation). |
| Gesture limits are not settable in `lv_conf.h` | LVGL 8.4 hardcodes `LV_INDEV_DEF_GESTURE_LIMIT` and `LV_INDEV_DEF_LONG_PRESS_TIME` without an `#ifndef` guard. They are set on the indev driver in `main.cpp`. |
| Deleting an object inside its own event handler | Use `lv_obj_del_async()`. `overlays_dismiss()` does. |
| PSRAM is required | The framebuffers are 450KB. `main.cpp` fails loudly at boot rather than faulting somewhere unhelpful later. |
| CST3530 must be re-armed after every read | Write `0xD00002AB` or it stops reporting entirely after the first contact. |
