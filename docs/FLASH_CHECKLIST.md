# Flash checklist: the design pass on the glass

Everything in `docs/DESIGN_PLAN.md` is merged. This is what to look at after
flashing, in the order it will appear, with what "right" looks like. Each
row is something no render could verify - touch targets, live data, motion.
Tick what passes; anything else is the fix round.

## First boot (fresh unit, or after CHANGE NETWORK)

| Check | Right looks like |
|---|---|
| Boot screen from the first frame | Two rows of rivet dots, a turquoise quarter-arc circling a small track, `AIRSTREAM WEATHER`, and `CONNECTING TO WI-FI` with the next steps in faint grey beneath. Never black, never a bare spinner. |
| Setup screen | Within ~20s of a fresh unit: `GET STARTED`, three numbered steps, the network name in a turquoise pill, an eight-digit password grouped 4+4, and a QR code on a light square with `SCAN TO JOIN` above it. |
| Scan the code | The phone's camera offers to join `Airstream-Weather` in one tap. The setup page opens by itself. |
| Type it instead | Joining by hand with the eight digits also works; the password is the one on the glass. |
| Hand-off | After saving, the screen returns to the boot layout: `FINDING LOCATION`, then `FETCHING FORECAST`, then Today fades in over it (about half a second). |
| Console | `[net] generated a setup network password` on the very first boot only; `[wx] pressure NNNN.N hPa msl, 3h +N.N (25 samples): <word>` after each fetch; `stack=` on the `[loop]` heartbeat. |

## Today

| Check | Right looks like |
|---|---|
| Strip geometry | Eight 52px columns; the last one ends short of the right edge. `NOW` in turquoise on a raised slab. |
| Rain | A percentage in turquoise **and** a thin bar under it, taller with the chance. Both absent below 10%. |
| Wind | A small arrow and a number in sky blue. The arrow is one of eight compass glyphs and points where the wind is *going* (a north wind points down). Absent below 3 mph. |
| Hourly temperatures | 30px, on the colour ramp, no degree sign. A three-digit value (100°F) drops to a smaller cut and still fits its column. |
| Forecast screen | Swipe left from Today: ten day columns - `TODAY` then weekdays, a daytime glyph, the high in 30px on the ramp, the low beneath in grey, a turquoise rain percentage where it is 10% or more. Swipe right brings Today back. |
| Now zone | Big temperature with the degree mark up by its cap and a small `F` below that; icon top-right of the zone; condition; `FEELS 71° · H 84° L 58°`; `DENVER · 2:35P · 4 MIN AGO`. The last line fits with a long town name. |
| Seam | A hairline between the zones with tiny rivet dots down it. |
| Press feedback | Every pill and button lightens while held. (It does not shrink: the design's 0.97 scale needs an LVGL alpha layer this 16-bit build cannot draw.) |

## Hour Detail

| Check | Right looks like |
|---|---|
| Open | Tapping a column grows a panel out of that column into the centre, with a turquoise top edge and a shadow. Three hours show either side. |
| Retarget | Tapping one of the side hours moves the panel to it without closing. Tapping the panel closes it. |
| Beyond the strip | From the last column, the right-hand neighbours are hours the strip does not show; tapping one still works. |
| Values | Six big numbers: temperature (ramp colour), feels-like, humidity; rain chance with the amount in small type after it, wind with its cardinal, gusts, the last two with `MPH`. |

## Now Detail and Pressure Detail

| Check | Right looks like |
|---|---|
| Sun arc | A large half-arc with the sun glyph sitting **on** the track at the right time of day, `DAYLIGHT` inside, sunrise and sunset times below. After sunset the glyph rests at the right end and the word reads `NIGHT`. |
| Pressure cell | `PRESSURE 1012` in small type, an outlook word (`Clear`, `Clearing Slow`, `Storm Risk`...) beneath, and a tiny six-hour sparkline. |
| Pressure Detail | Tapping that cell opens a third overlay: the word and a caption (`STEADY`, `FALLING`...), a 24-hour graph with the last three hours coloured, and four body effects each with LOW / MEDIUM / HIGH. Tap anywhere closes. |
| Colour rule | If any body effect reads HIGH, the outlook word and the barometer glyph in the header turn orange. |

## Quick Settings and System

| Check | Right looks like |
|---|---|
| Quick Settings | Swipe down: a full screen. A brightness bar the full width and a fingertip tall, in nine divisions, with the level and a status line (`SUN-DRIVEN · DIMS AT 7:18 PM` or `MANUAL · AUTO AT DUSK`) and an `AUTO` pill; below, the forecast's age with a refresh glyph beside it, and the Wi-Fi bars with the SSID. No IP or update address here (System has them). |
| The bar | Tap a division: the bar fills to it and the glass changes brightness at once. Drag: the fill and the backlight follow the finger with no lag. Neither closes the screen or changes screen. Tap empty space or swipe up to close. |
| System | Three wide columns; `airstream-weather.local` on one line; memory in the title bar with a dot; uptime, version and build date in the footer; no brightness row. |
| CHANGE NETWORK | Two taps, then the panel reboots to the setup screen with a **new** password. |

## Night Mode (after 23:00 local, brightness on Auto, no touch for 30s)

| Check | Right looks like |
|---|---|
| Entry | Thirty seconds after the last touch, in the small hours, the whole weather screen fades out over about 1.5s and a big clock fades in: `h:mm` in 128px dim oat, centred, nothing else. The backlight settles at level 16, the dimmest that is legible on this glass (14 is the first that lights at all). A manual level does not prevent it. |
| Exit | Any touch brings the weather back the same way, at whatever level was set before the clock: the manual one if there was one, otherwise the sun's. |
| Manual levels | A level set on the bar or the button holds through the night and the day until the sun moves the panel into a new part of its day (dawn, day, dusk, night, small hours) or the panel resets; then Auto takes over. |

## If something is wrong

Note the row, what you saw instead, and a photo if it is visual. The
console's first lines after reset plus the `[wx] pressure` line are enough
for data problems.
