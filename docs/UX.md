# Interaction contract

This is the agreement between the design in
[DESIGN_PROMPT.md](DESIGN_PROMPT.md) and the firmware. If a refined design
changes a binding or a timing, change it here too — this file is what the two
sides check themselves against.

## The premise

A 640×180 strip on a bulkhead is glanced at far more often than it is used. So
the design splits sharply in two:

- **The glance layer** must be readable from four feet without touching
  anything: how warm is it, what is it doing, is it going to rain.
- **The detail layer** holds everything else and is never more than one tap
  away.

Every gesture below exists to keep those two layers separate. Nothing on the
glance layer is there because it fit; nothing in the detail layer is hidden
because it was unimportant.

## Gesture map

| Gesture | Where | Result |
|---|---|---|
| **Tap** | An hour column | Hour Detail panel, expanding out of that column |
| **Tap** | A neighbour hour beside the Hour Detail panel | Re-points the panel at that hour without closing it |
| **Tap** | The "Now" zone (left 208px) | Now Detail overlay |
| **Tap** | The pressure cell in Now Detail | Pressure Detail overlay |
| **Tap** | The panel, or anywhere else, with an overlay open | Close it |
| **Swipe right** (finger left → right) | Today | Opens the System drawer, sliding in from the left |
| **Swipe left** (finger right → left) | System | Puts the drawer away, sliding out to the left |
| **Swipe left** (finger right → left) | Today | Opens the ten-day Forecast, sliding in from the right |
| **Swipe right** (finger left → right) | Forecast | Puts it away, sliding out to the right |
| **Swipe down** | Any screen | Quick Settings sheet drops from the top edge |
| **Swipe up** | Quick Settings | Close it (so does a tap on the dimmed content below) |
| **Swipe up / down** | Hour, Now or Pressure detail | Close it |
| **Long press (700ms)** | Anywhere | Force a forecast refresh |

Anything not in the table is dropped: a swipe left on Forecast does nothing, a
swipe right on System does nothing, a sideways drag on Quick Settings adjusts
the brightness slider and nothing else.

### Interaction contexts

Every place a finger can land is a *context* - a screen, or an overlay on top
of one - and each context declares the swipes it accepts. One dispatcher
(`ui_handle_swipe()` in `screen_manager.cpp`) consults the active context and
either performs the swipe or drops it. It never falls through to whatever is
underneath. Two consequences:

- **Screens live at positions on a strip, not in a carousel.** Today is home
  (position 0). System is a drawer at -1. A swipe only moves to a neighbour that
  exists, so the strip cannot wrap round on itself. Farther from home stacks on
  top: a screen slides in *over* its neighbour on the way out and slides *out*
  to reveal it on the way back.
- **A control that owns a drag keeps it.** The brightness slider clears
  `LV_OBJ_FLAG_GESTURE_BUBBLE`, so a drag along it is a drag and only a drag.
  Quick Settings additionally accepts only swipe-up, so even a sideways flick
  on its background does nothing.

### Why dismiss is over-served on the detail overlays

Tap, swipe up, *and* swipe down all close the Hour and Now overlays. Three ways
to leave is redundant by design: the single fastest way to make a touch panel
feel broken is to be stuck in a view with no obvious way out, and on a
wall-mounted screen there is no back button, no home gesture, and no way to
force-quit. Quick Settings is the one exception, for the slider's sake; its
drag handle says which way out.

### Swipe / tap disambiguation

A swipe is a swipe and a tap is a tap. LVGL would otherwise deliver
`LV_EVENT_CLICKED` on release to whatever was under the finger when it first
touched down - however long the finger stays down after the swipe, and on the
screen that has since slid away - so a swipe left across the hourly strip
would change screen **and** open an hour's detail on top of it.

The moment a gesture is recognised, `ui_note_gesture()` tells LVGL to forget
the press (`lv_indev_wait_release`), so nothing else from that press is
delivered to anything. As a second guard, `ui_gesture_recent()` stays true
until the next touch begins and every click handler checks it. See
[`screen_manager.h`](../firmware/src/ui/screen_manager.h).

## The button

The board exposes exactly one usable button — see
[HARDWARE.md](HARDWARE.md#so-one-button) for why — so it carries a grammar
rather than a single action:

| Press | Result |
|---|---|
| **Short** | The bar's lowest division → its middle → its highest → Auto → … |
| **Double** | Straight back to Auto |
| **Long (800ms)** | Blank the display. Any touch or press wakes it. |

The rule a hand reaching for an unlabelled button in the dark can rely on is
*every press is brighter than the last, until it hands back to Auto.* From Auto
the first press lands on the bar's lowest division; from a level set on the
bar, on the next of the three rungs above it. A level set either way holds
until the sun moves the panel into a different part of its day - dawn, day,
dusk, night, the small hours - or the panel resets; then Auto takes over.

Long press fires on the threshold rather than on release, so the display blanks
under your thumb instead of after you let go.

## Screens

| # | Name | Contents |
|---|---|---|
| 0 | **Today** (home) | Current conditions on the left: the hero temperature, `FEELS 71°` under it, then the day's high in sunset and low in sky (colour is the label), then the clock and the town; the condition glyph top-right with its name set small beneath it. On the right, a column of row labels (`TEMP`, `UV`, a droplet and `%`, `WIND`) and the next seven hours - hour, glyph, temperature, the trend ribbon, UV index on a sky-to-purple scale, chance of rain as a bare number with its bar (`-` when there is none), wind - each hour a tap away from Hour Detail. The design drew ten columns; eight was the first change asked for on the glass, seven with labels the second. |
| +1 | **Forecast** (swipe left) | Ten days in ten columns: weekday, daytime glyph, high on the colour ramp, low, and the chance of precipitation as a bare number behind a droplet - or a snowflake when the day's weather is snow; `-` when there is none. Today is on a raised slab. |
| −1 | **System** (swipe right) | A title bar with free heap and PSRAM (the dot turns sunset under 40K heap) and the one action, **CHANGE NETWORK**, which asks for a second tap before rebooting into the setup portal. Below a rivet-dotted rule, three 200px columns: network with signal bars, IP address, location with coordinates; update host, touch controller (raw digitiser coordinates while a finger is down), display. A footer carries uptime, version and build date. Brightness is not here - it is one swipe away in Quick Settings. |

Three screens on one strip, home in the middle. A page indicator sits
at the bottom centre of every screen — a short turquoise bar for the current
position, small rivet-grey dots for the others — drawn as a position in a strip
rather than as something pressable.

## Overlays

All four live on LVGL's top layer, so they survive a screen change and are
written once rather than per screen.

**Hour Detail** — a 300px panel in the centre of the strip that expands out
of the column you tapped (220ms), with a turquoise top edge and a shadow.
Headed by the condition and whether it is the hour you are living in, then
the hour in Title 30 and the glyph. Six metrics in a three-column, two-row
grid, values in Title 30: temperature (on the ramp), feels-like, humidity;
chance of rain (with the accumulation as a small suffix), wind with its
cardinal, gusts. Either side, three neighbour hours at full strength - hour,
glyph, temperature - each a tap target that re-points the panel without
closing it. A tap on the panel closes it; no printed hint, it is learned in
one tap.

**Now Detail** — a 192px sun arc from sunrise to sunset, the lower half
below the screen, with the sun itself riding the track on a ground-coloured
puck at the current position; `DAYLIGHT` and the two times sit inside it.
Beside it, six Title 30 values: high/low, humidity, UV (sunset-orange at 6
and above); gusts with cardinal, pressure, visibility. Pressure is spoken,
not numeric: the reading sits subdued in the label and the cell shows an
outlook word (`Clear`, `Clearing Slow`, `Storm Risk`...) from the 3-hour
trend with a six-hour sparkline beneath it. Tapping the cell opens Pressure
Detail; tapping anywhere else closes.

**Pressure Detail** — the outlook word and its trend caption (`FALLING
FAST`, `STEADY`...); a 24-hour graph of sea-level pressure on a fixed
1006-1022 hPa range with the last three hours in the outlook colour; and
four body effects (joint pain, migraine, sinus and ears, heart strain) each
with a LOW / MEDIUM / HIGH word. Any HIGH turns the outlook word and the
header glyph sunset. The rules are `design/logic.json`, implemented in
`ui/pressure_logic.cpp` and checked against the JSON in CI.

**Quick Settings** — the whole screen. A brightness bar the full width and a
fingertip tall, divided into nine: tap a division and the bar fills to it and
the glass takes that level at once; drag and both follow the finger for fine
tuning. Beside it the level and an **AUTO** pill, filled turquoise while the
sun is driving and `surface-hi` once a level has been set by hand; beneath,
what the dimmer is doing (`SUN-DRIVEN · DIMS AT 7:18 PM`, `MANUAL · AUTO AT
DUSK`). The second row is the forecast's age with a refresh glyph beside it,
and the Wi-Fi bars with the SSID. Nothing the System screen already shows. A
tap on empty space or a swipe up closes it.

## Night Mode

After 23:00 local, thirty seconds after the last touch, the Today screen
cross-fades over 1500ms to a clock: 128px digits in dim oat, `h:mm`, a little
right of centre to clear the wider bezel, at backlight 16 - the dimmest level
this glass can show legibly. Left of the digits, the condition as a glyph
alone, four fifths their height; right of them, a sunrise glyph over the time
of the next sunrise. Both at 69% so the time stays the brightest thing on the
glass. It is a nightlight that knows the time, not a screen. A
manual level does not prevent it. Any touch brings the weather back the same
way, at whatever level was set before the clock - the manual one if there was
one, otherwise the sun's. Off never enters it. (The design's §5 night layout, a
dimmer weather strip, was built and replaced by the clock after the third
flash.)

## States before there is data

A wall-mounted display showing nothing is indistinguishable from a broken one,
and boot is exactly when a new owner is watching it. So the panel always says
what it is doing:

From the first frame until the first forecast lands, the panel is on a
**boot screen** (design/SPEC.md §6): two rivet rows, a quarter-turn of
turquoise circling a 56px track once every 1.6 seconds, `AIRSTREAM WEATHER`,
and a status line that cross-fades through the steps with the remaining ones
listed beneath it in the quietest grey. It is a rate, not a progress bar,
because nothing on the panel knows how long the router will take.

| State | On screen |
|---|---|
| No Wi-Fi configured | The **setup screen** (§7): `GET STARTED`, the three steps, the network name in a turquoise chip, the eight-digit password grouped 4+4, and a QR code carrying the standard `WIFI:` URI so a phone camera joins in one tap |
| Connecting | `CONNECTING TO WI-FI` — then `FINDING LOCATION · FETCHING FORECAST` |
| Locating | `FINDING LOCATION` — then `FETCHING FORECAST` |
| Fetching | `FETCHING FORECAST` |
| Fetch failed | `FETCHING FORECAST` — `NO ANSWER YET · TRYING AGAIN`; retries every 60s by itself |
| Forecast ready | Today fades in over the boot screen in 400ms |
| Connection lost, data still valid | Everything stays on screen with a small orange Wi-Fi-off mark; the forecast's age is one swipe down, in Quick Settings |

The setup network is WPA2. Its password is eight digits (WPA2's minimum),
generated on first boot, kept in NVS, and rotated by **CHANGE NETWORK**; it
never appears in the repository or the firmware image.

## Motion

| What | Duration | Easing |
|---|---|---|
| Screen to screen | 280ms | horizontal slide, in-out |
| Overlay in | 220ms | slide up 24px + fade, ease-out |
| Brightness ramp (solar, night clock) | 1500ms | cosine — deliberately imperceptible |
| Brightness set by hand | 80ms | the glass follows the finger |
| Wake from blank | 250ms | fast; waking should feel instant |
| Press feedback | 90ms | fill changes to `surface-hi` (no scale: a zoomed widget needs an alpha layer this 16-bit build cannot draw) |
| Night Mode in / out | 1500ms | cross-fade, ease in-out, locked to the brightness ramp |

Nothing else animates. The temperature does not count up, the ribbon does not
draw itself in, and values do not slide. A panel you look at fifty times a day
should be still.

## Deliberate deviations from the design brief

Two things in [DESIGN_PROMPT.md](DESIGN_PROMPT.md) were changed during
implementation, both for legibility at real size:

1. **Wind in the strip is an arrow and a number, not cardinal text.** The
   first build shipped `NW 8` because a rotated triangle at 12px read as a
   smudge; the design pass (design/SPEC.md §1) replaced it with the 11px
   `navigation` glyph turned to where the wind is going, plus the integer
   speed, and dropped the cardinal from the strip. The cardinal text survives
   in Hour Detail, where there is room for it.
2. **Hourly temperatures drop the degree sign.** A degree mark costs about 9px
   in a 42px column and carries no information the 72px hero temperature has not
   already established. Dropping it is what lets `100` fit (in the 24px cut).

The degree mark on the hero *is* a separate label aligned to the number's cap
height, as the brief asks — which also happens to be what stops a three-digit
temperature colliding with the condition icon.
