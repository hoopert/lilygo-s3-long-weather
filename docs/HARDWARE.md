# Hardware notes

Everything here was checked against LilyGO's own
[T-Display-S3-Long](https://github.com/Xinyuan-LilyGO/T-Display-S3-Long)
examples and board definition rather than inferred, because several of the
board's published pin assignments are misleading in ways that cost real time.

## The board

| | |
|---|---|
| MCU | ESP32-S3R8 — dual-core Xtensa LX7 @ 240MHz |
| Flash | 16MB (QIO) |
| PSRAM | 8MB (OPI) — required; the framebuffers do not fit without it |
| Display | 3.4" 180×640 IPS, AXS15231B controller, QSPI |
| Touch | Capacitive — **two different controllers ship under this name**, see below |
| USB | Single USB-C, native CDC |
| Other | SY6970 PMU, QWIIC connector, optional microSD slot |

The panel is 180 wide × 640 tall in its own coordinate space. The UI runs
landscape at 640×180, and LVGL rotates the framebuffer in software. See
[ARCHITECTURE.md](ARCHITECTURE.md#rotation) for why that is done in software
rather than with the panel's MADCTL register.

## Pin map

From the vendor's `examples/factory/pins_config.h`. Mirrored in
[`firmware/include/pins.h`](../firmware/include/pins.h).

| Function | GPIO | |
|---|---|---|
| LCD CS | 12 | |
| LCD SCK | 17 | |
| LCD D0 | 13 | QSPI data 0 |
| LCD D1 | 18 | |
| LCD D2 | **21** | see the button warning below |
| LCD D3 | 14 | |
| LCD RST | 16 | |
| **LCD backlight** | **1** | PWM via LEDC — this is the dimming path |
| Touch SDA | 15 | |
| Touch SCL | 10 | |
| Touch IRQ | 11 | not used; the driver polls |
| Touch RST | 2 | |
| **BOOT button** | **0** | active low, external pull-up |
| SD CS / MOSI / MISO / SCLK | 38 / 39 / 41 / 40 | unused by this firmware |

### Two traps in the vendor header

**`PIN_BUTTON_2` is not a button.** The vendor header defines it as GPIO21,
which on this board is the LCD's QSPI **D2** line. Reading it as a button does
nothing useful, and driving it would corrupt the display. This firmware does not
touch it.

**`PIN_BAT_VOLT` collides with `TP_RST`.** Both are defined as GPIO2. Battery
voltage sensing is therefore not wired up here; the touch reset wins, because a
panel that responds to touch is worth more than a battery percentage.

### So: one button

RST is wired directly to the ESP32 EN line and resets the chip in hardware
before any code can observe it. Combined with the GPIO21 conflict above, that
leaves **BOOT (GPIO0) as the only user button firmware can read.**

Rather than pretend otherwise, the firmware gives that one button a full press
grammar — short, double, and long — and puts everything else on touch. See
[`firmware/src/input/buttons.cpp`](../firmware/src/input/buttons.cpp).

## Two touch controllers

Boards sold as "T-Display S3 Long" ship with one of two digitisers, on the same
I2C bus, with no external marking to tell them apart:

| Revision | Controller | I2C address | Protocol |
|---|---|---|---|
| Earlier | AXS15231B (integrated into the display controller) | `0x3B` | 8-byte read command, 8-byte reply |
| Later | CST3530 (Hynitron CST66xx family) | `0x58` | Big-endian register `0xD0070000`, 9-byte reply, re-arm with `0xD00002AB` |

LilyGO's own repository switched from one to the other partway through, so which
one you get depends on when the board was made.

[`firmware/src/input/touch.cpp`](../firmware/src/input/touch.cpp) probes both
addresses at boot and binds to whichever answers. The **System** screen names
the controller it found, which is also the fastest way to tell a revision
surprise from a wiring fault:

- `AXS15231B @ 0x3B` or `CST3530 @ 0x58` — working
- `none detected` — neither answered. Check that nothing else is on the QWIIC
  connector holding the bus, and that GPIO2 (touch reset) is not being driven by
  something else.

The CST3530 path is written from the vendor driver's report format rather than
by bundling Hynitron's full driver, which is several thousand lines of
firmware-update machinery this project does not need. It reads the first contact
only — the UI is single-touch throughout — and verifies the report checksum in
the single-contact case.

## Backlight and dimming

The backlight is on **GPIO1**, driven through LEDC PWM at **2kHz**. LilyGO's own
`GFX_AXS15231B_Image` example fades this pin 0–255 at exactly that frequency, so
PWM dimming on this hardware is known-good rather than assumed.

This firmware uses **12-bit** PWM resolution instead of 8. Brightness levels in
`config.h` are perceptual 0–255 and are squared on the way to duty, because
perceived brightness goes roughly as the square root of light output — a linear
duty ramp spends most of its travel in a range the eye reads as "already on".
Twelve bits gives the bottom of that curve somewhere to live: the deep-night
level of 12 lands on a duty of 9/4095, which would round to nothing at 8 bits.

There is **no ambient light sensor** on this board. See the auto-dimming section
in the [README](../README.md#auto-dimming-without-a-light-sensor) for what is
used instead.

## Mounting in a trailer

- **Orientation.** Landscape, long edge horizontal. If it comes up upside down
  once mounted, that is a one-line fix — see the troubleshooting table in
  [INSTALL.md](INSTALL.md#troubleshooting).
- **Viewing distance.** The layout is designed for a glance from around four
  feet. Nothing on it is smaller than 12px, and the current temperature is 72px.
- **Power.** USB-C, 5V. Current draw is modest but the backlight dominates it,
  so a panel left at full brightness overnight costs meaningfully more than one
  on the auto curve — another reason the dimming defaults are worth leaving on
  if you are running off a house battery.
- **Heat.** The ESP32-S3 runs warm at 240MHz with Wi-Fi up. Leave some air
  behind the board rather than sealing it into a cavity.
- **Cable.** Plan for the USB-C connector's depth and bend radius behind the
  panel before you cut the hole.

## Headroom for what comes next

The current firmware uses about 37% of RAM and 20% of flash. For an Airstream
monitoring build, the obvious next additions and where they would go:

- **Tank levels / battery / temperatures** — a new screen, registered in one
  line. See [ARCHITECTURE.md](ARCHITECTURE.md#adding-a-screen).
- **The QWIIC connector** shares the touch I2C bus (SDA 15, SCL 10). A BME280 or
  similar sits on it directly; just avoid address collisions with `0x3B`/`0x58`.
- **The SY6970 PMU** is on the same bus and can report battery state over I2C,
  which sidesteps the GPIO2 conflict that blocks the analog path.
