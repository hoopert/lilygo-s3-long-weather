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
| **Tap** | An hour column | Hour Detail overlay for that hour |
| **Tap** | The "Now" zone (left 208px) | Now Detail overlay |
| **Tap** | Anywhere, with an overlay open | Close it |
| **Swipe right** (finger left → right) | Today | Opens the System drawer, sliding in from the left |
| **Swipe left** (finger right → left) | System | Puts the drawer away, sliding out to the left |
| **Swipe down** | Any screen | Quick Settings sheet |
| **Swipe up** | Quick Settings | Close it |
| **Swipe up / down** | Hour or Now detail | Close it |
| **Long press (700ms)** | Anywhere | Force a forecast refresh |

Anything not in the table is dropped: a swipe left on Today does nothing, a
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

LVGL delivers `LV_EVENT_CLICKED` on release even when the same press already
produced a gesture. Without handling that, a swipe left across the hourly strip
would change screen **and** open an hour detail overlay on top of it.

Every gesture handler calls `ui_note_gesture()`; every click handler ignores the
click while `ui_gesture_recent()` is true (450ms). See
[`screen_manager.h`](../firmware/src/ui/screen_manager.h).

## The button

The board exposes exactly one usable button — see
[HARDWARE.md](HARDWARE.md#so-one-button) for why — so it carries a grammar
rather than a single action:

| Press | Result |
|---|---|
| **Short** | Step brightness down one rung: 100% → 70% → 43% → 24% → 10% → Auto → … |
| **Double** | Straight back to Auto |
| **Long (800ms)** | Blank the display. Any touch or press wakes it. |

The rule a hand reaching for an unlabelled button in the dark can rely on is
*every press is dimmer than the last, until it wraps back to Auto.* Entering the
ladder from Auto lands on the first rung genuinely dimmer than what is currently
on screen, so the first press always visibly does something.

Long press fires on the threshold rather than on release, so the display blanks
under your thumb instead of after you let go.

## Screens

| # | Name | Contents |
|---|---|---|
| 1 | **Today** | Current conditions and the next ten hours |
| 2 | **System** | A title bar with free heap and PSRAM (the dot turns sunset under 40K heap) and the one action, **CHANGE NETWORK**, which asks for a second tap before rebooting into the setup portal. Below a rivet-dotted rule, three 200px columns: network with signal bars, IP address, location with coordinates; update host, touch controller (raw digitiser coordinates while a finger is down), display. A footer carries uptime, version and build date. Brightness is not here - it is one swipe away in Quick Settings. |

Two screens is the minimum that makes a swipe meaningful. A page indicator sits
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

**Quick Settings** — a 130px sheet that drops from the top edge over a 70%
scrim, so the screen beneath stays legible at 30%. Three control columns:
brightness (the level, a slider, and what the dimmer is doing - `SUN-DRIVEN ·
DIMS AT 7:18 PM` or `MANUAL · AUTO IN 3H 42M`) with an **AUTO** pill that is
filled turquoise while auto-dimming is engaged and drops to `surface-hi` once
an explicit level has taken over; forecast (a **REFRESH** pill with the age of
the data beneath it); and Wi-Fi (four signal bars and the SSID). The IP and
OTA hostname sit in the header. A tap on the scrim or a swipe up closes it;
the drag handle at the bottom says so.

## Night Mode

After 23:00 local, in Auto, with nobody in front of the panel, the backlight
is aiming at its deep-night floor and the Today screen cross-fades over 1500ms
to a second layout on the same screen: the hero temperature in a dimmer oat,
the condition in `aluminum-dim`, the clock alone on the place line, and the
strip reduced to five wide columns of hour and temperature - no icons, ribbon,
rain, wind, or page indicator. It is a nightlight that happens to know the
temperature, not a screen. Any touch is a presence boost, which lifts the
backlight target and brings the day layout back the same way. Manual and Off
modes never enter it. See design/SPEC.md §5.

## States before there is data

A wall-mounted display showing nothing is indistinguishable from a broken one,
and boot is exactly when a new owner is watching it. So the panel always says
what it is doing:

| State | On screen |
|---|---|
| No Wi-Fi configured | `JOIN WI-FI "Airstream-Weather" FROM YOUR PHONE` |
| Connecting | `CONNECTING TO WI-FI` |
| Locating | `FINDING LOCATION` |
| Fetching | `FETCHING FORECAST` |
| Fetch failed | `NO CONNECTION` — retries every 60s by itself |
| Connection lost, data still valid | Everything stays on screen with a small orange Wi-Fi-off mark; the "x min ago" line is never omitted, so stale data can never present itself as current |

## Motion

| What | Duration | Easing |
|---|---|---|
| Screen to screen | 280ms | horizontal slide, in-out |
| Overlay in | 220ms | slide up 24px + fade, ease-out |
| Brightness ramp | 1500ms | cosine — deliberately imperceptible |
| Wake from blank | 250ms | fast; waking should feel instant |
| Press feedback | 90ms | scale to 0.97, fill changes to `surface-hi` |
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
