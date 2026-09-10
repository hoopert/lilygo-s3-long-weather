# Airstream Weather Panel

A wall-mounted weather display for the **LilyGO T-Display S3 Long** — current
conditions and the next ten hours on a 640×180 strip, designed to be read from
across a trailer and interrogated by touch when you want more.

Built as a first project for the board before it takes on the bigger job of
monitoring and controlling things in an Airstream, so the architecture is
deliberately ready for more screens than it currently ships with.

```
┌──────────────────────────────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┬───────┐
│                              │       │       │       │       │       │       │       │       │
│  ╭───╮                  ☀    │  NOW  │  3PM  │  4PM  │  5PM  │  6PM  │  7PM  │  8PM  │  9PM  │
│  │ 7 │2°F                    │   ☀   │   ☀   │   ⛅   │   ⛅   │   ☁   │   🌧   │   🌧   │   ☁   │
│  ╰───╯                       │       │       │       │       │       │       │       │       │
│                              │  72   │  74   │  73   │  70   │  66   │  62   │  59   │  57   │
│  Partly Cloudy               │       │       │       │       │       │       │       │       │
│  FEELS 71° · H 84° L 58°     │       │       │       │       │       │       │       │       │
│                              │       │       │       │       │       │       │       │       │
│  DENVER · 2:35P · 4 MIN AGO  │       │       │       │       │       │       │       │       │
└──────────────────────────────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┴───────┘
   ← 208px "Now" zone ─────────→  ← eight 52px hour columns from x210; tap one for the detail ──→
```

## Install

**The short version:** plug the board into a computer, open the web installer,
press one button. No toolchain, no API key, no editing source.

1. Open the installer page in Chrome, Edge, or Opera on a desktop.
2. Press **Install firmware** and pick the board's serial port.
3. When the panel comes up, scan the code on its screen with your phone (or
   join `Airstream-Weather` with the password shown). A setup page opens by
   itself — choose your network, save, done.

Leave the location fields blank and the panel finds itself from its IP address,
and keeps doing so every time you move. That is the right default for something
bolted into a trailer.

Full instructions, including the command-line and PlatformIO paths:
**[docs/INSTALL.md](docs/INSTALL.md)**

## Using it

| Gesture | What happens |
|---|---|
| **Tap an hour column** | That hour in a panel that opens out of the column — feels-like, humidity, rain chance *and* amount, wind, gusts. Tap a neighbouring hour to move the panel; tap the panel to close it |
| **Tap the big temperature** | Now in full — a sun arc showing where you are in the day, high/low, UV, gusts, visibility, and the pressure outlook in words |
| **Tap the pressure** | The last 24 hours of pressure, the trend, and what it means for joints, migraines, sinuses and heart |
| **Swipe left** | The ten-day forecast (swipe right to come back) |
| **Swipe right** | Open the System drawer (swipe left to put it away) |
| **Swipe down** | Quick settings drop from the top: brightness, refresh, Wi-Fi |
| **Swipe up** | Close whatever is open |
| **Press and hold** | Fetch a new forecast now |
| **BOOT button** — short | Step brightness down one rung, wrapping back to Auto |
| **BOOT button** — double | Straight back to Auto brightness |
| **BOOT button** — hold | Blank the display; any touch or press wakes it |

The full interaction contract, including motion timings and the reasoning behind
each binding, is in **[docs/UX.md](docs/UX.md)**.

## Auto-dimming without a light sensor

The board has no ambient light sensor, so the panel infers the cabin from three
signals it does have:

1. **The sun.** Open-Meteo returns sunrise and sunset for the panel's own
   location, and brightness follows that curve — full through the day, ramping
   down across 45 minutes of twilight either side, low in the evening, and a
   deep-night floor in the small hours. Because the location tracks the trailer,
   so does the dimming: park a thousand miles west and it stays correct with
   nothing to reconfigure.
2. **Presence.** Any touch means somebody is standing in front of it, so it
   lifts to full and eases back down 30 seconds later.
3. **Intent.** An explicit brightness choice overrides both, then expires after
   four hours — so a nudge at midnight does not leave the panel dark all the
   next day.

Every transition is cosine-eased over 1.5 seconds. You should never catch the
panel changing brightness; you should only notice that it was already right.

All of it is tunable at the top of
[`firmware/include/config.h`](firmware/include/config.h).

## What's in here

| Path | |
|---|---|
| [`docs/DESIGN_PROMPT.md`](docs/DESIGN_PROMPT.md) | **The prompt to paste into Claude Design** to produce the artboards this UI is built from |
| [`docs/INSTALL.md`](docs/INSTALL.md) | Every install path, from one-click to source |
| [`docs/HARDWARE.md`](docs/HARDWARE.md) | Pinout, the two touch-controller revisions, mounting and power notes |
| [`docs/UX.md`](docs/UX.md) | The interaction contract — gestures, states, motion |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | How it fits together, and how to add a screen |
| [`docs/DESIGN_PLAN.md`](docs/DESIGN_PLAN.md) | The designer's hand-off in `design/`, and the phased plan to apply it |
| [`docs/BACKLOG.md`](docs/BACKLOG.md) | What is known and deliberately not done yet |
| `firmware/` | PlatformIO project |
| `tools/build_fonts.sh` | Regenerates the eight LVGL font cuts |
| `tools/flash.sh`, `tools/flash.ps1` | Command-line flashing |
| `tools/web-installer/` | The one-click browser installer |

## Design

The type is **Jost\*** — an open interpretation of Futura, the geometric sans
that Streamline Moderne was drawn in and the same design language that produced
the Airstream Clipper in 1936. Icons are **Material Symbols Rounded**, which
shares that construction logic, so numerals and glyphs read as one hand.

The palette is dark-first, because a wall panel in a trailer is read at night as
often as at noon: near-black ground, brushed-aluminum off-white for text, warm
oat for the hero temperature, and Airstream turquoise for water and active
states. Hourly temperatures are colored along a ramp from cold sky-blue to hot
sunset-orange, so the strip reads as a heat map before you read any number.
Between the columns run 1px rivet seams — the one ornament the design allows
itself.

Nothing is pure white or pure black anywhere.

## Notes

- Weather comes from [Open-Meteo](https://open-meteo.com): no API key, no
  account, no rate-limit signup. That is the single biggest reason this
  installs in two minutes rather than twenty.
- Over-the-air updates are enabled (`airstream-weather.local`), which matters
  for something you would otherwise have to unscrew from a bulkhead to change.
- Verified by compiling for the target: **RAM 37%, flash 20%** of what the board
  has. There is a lot of room for the tank sensors and battery monitor later.

## License

MIT. Jost\* is SIL OFL 1.1; Material Symbols is Apache 2.0. The AXS15231B panel
driver is adapted from LilyGO's MIT-licensed vendor examples.
