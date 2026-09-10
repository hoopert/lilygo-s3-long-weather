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
| Strip geometry | Eight columns, each just an hour, a 32px glyph and a temperature, centred in the strip's height; the last column ends short of the right edge. `NOW` in turquoise on a raised slab. No ribbon, rain or wind rows: those live in Hour Detail. |
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
| Sheet | Swipe down: a panel drops from the top over a dimmed Today. Three columns: brightness with a status line (`SUN-DRIVEN · DIMS AT 7:18 PM`), `AUTO`, `REFRESH` with the data age, Wi-Fi bars with the SSID. |
| Slider | Dragging the slider changes brightness and does **not** close the sheet or change screen. Tap the dimmed area or swipe up to close. |
| System | Three wide columns; `airstream-weather.local` on one line; memory in the title bar with a dot; uptime, version and build date in the footer; no brightness row. |
| CHANGE NETWORK | Two taps, then the panel reboots to the setup screen with a **new** password. |

## Night Mode (after 23:00 local, brightness on Auto, no touch for 30s)

| Check | Right looks like |
|---|---|
| Entry | Over about 1.5s the strip's glyphs fade away and four wide columns of hour + temperature take their place; the big temperature goes a duller oat; the clock alone on the place line; no page indicator. The backlight settles at level 16, the dimmest that is legible on this glass (14 is the first that lights at all). |
| Exit | Any touch brings the day layout back the same way. |
| Not triggered | A manual brightness (slider or BOOT button) never enters it, however dark. |

## If something is wrong

Note the row, what you saw instead, and a photo if it is visual. The
console's first lines after reset plus the `[wx] pressure` line are enough
for data problems.
