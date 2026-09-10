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
hint says which way out.

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
| 2 | **System** | Network, IP, OTA hostname, location, touch controller and live raw coordinates, brightness mode, free memory, uptime, build. Plus **Change Wi-Fi Network**, which asks for a second tap before rebooting into the setup portal. |

Two screens is the minimum that makes a swipe meaningful. A page indicator sits
at the bottom centre of every screen — a short turquoise bar for the current
position, small rivet-grey dots for the others — drawn as a position in a strip
rather than as something pressable.

## Overlays

All three live on LVGL's top layer, so they survive a screen change and are
written once rather than per screen.

**Hour Detail** — eight metrics in a four-column, two-row grid: temperature
(colored on the ramp), feels-like, chance of rain, amount, wind with cardinal
direction, gusts, humidity, dew point. Headed by the hour, the condition, and
whether it is the hour you are currently in.

**Now Detail** — a sun arc from sunrise to sunset with the current position
marked, and beside it: high/low, humidity, UV (turning sunset-orange at 6 and
above), gusts, pressure, chance of rain today.

**Quick Settings** — a brightness slider with an **AUTO** pill that is filled
turquoise while auto-dimming is engaged and outlined once an explicit level has
taken over; a refresh button with the age of the data beside it; and the SSID,
IP, signal strength and OTA hostname.

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
| Press feedback | immediate | fill changes to `surface-hi` |

Nothing else animates. The temperature does not count up, the ribbon does not
draw itself in, and values do not slide. A panel you look at fifty times a day
should be still.

## Deliberate deviations from the design brief

Two things in [DESIGN_PROMPT.md](DESIGN_PROMPT.md) were changed during
implementation, both for legibility at real size:

1. **Wind is shown as cardinal text (`NW 8`) rather than a rotated arrow.** At
   12px in a 43px column a rotated triangle is a smudge, and `NW` is both more
   precise and faster to read. The arrow idea survives in the detail overlays,
   where there is room for it.
2. **Hourly temperatures drop the degree sign.** A degree mark costs about 9px
   in a 43px column and carries no information the 72px hero temperature has not
   already established. Dropping it is what lets `100` fit.

The degree mark on the hero *is* a separate label aligned to the number's cap
height, as the brief asks — which also happens to be what stops a three-digit
temperature colliding with the condition icon.
