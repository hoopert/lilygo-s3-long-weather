// Everything you might reasonably want to change lives in this file.
//
// You should not need to edit it at all for a normal install: Wi-Fi, location
// and units are all set from the phone-based setup portal the panel raises on
// first boot. This file is the escape hatch for the things a portal field
// cannot express.
#pragma once

#include "pins.h"

// ---------------------------------------------------------------------------
// Orientation
//
// The panel is physically 180x640 (tall and narrow). The UI is designed for
// 640x180 (wide and short). LVGL renders it unrotated and the panel driver
// turns each frame on the way to the glass (panel_push_frame), applying the
// matching inverse to touch (touch_read_cb). The LVGL enum is used purely as
// a label for which way round; LVGL's own sw_rotate is not involved.
//
// If the UI comes up upside-down once mounted, swap ROT_270 for ROT_90 here.
// That is the entire fix - touch follows automatically.
// ---------------------------------------------------------------------------
#define UI_ROTATION      LV_DISP_ROT_270
#define UI_WIDTH         PANEL_HEIGHT   // 640 - the app-space width
#define UI_HEIGHT        PANEL_WIDTH    // 180 - the app-space height

// If taps land mirrored, flip these. The rotation above is already applied
// to touch input; these correct for panel-to-digitiser mounting differences
// on top of that. The System screen has a Touch Test that prints
// raw and mapped coordinates so you can check this in about ten seconds.
#define TOUCH_INVERT_X   false
#define TOUCH_INVERT_Y   false

// ---------------------------------------------------------------------------
// Boot self-test
//
// For the first ~700ms after power-on, drive the panel directly - no LVGL -
// with the top half of the native (portrait) panel in turquoise and the bottom
// half in sunset orange, at full backlight. It doubles as a power-on splash,
// and it answers two questions at once:
//
//   - Anything at all?   Then the driver, the SPI bus and the backlight work,
//                        and any remaining blackness is LVGL-side.
//   - Which end is orange?  That is the panel's row 640. LV_DISP_ROT_270 puts
//                        the UI's LEFT edge at the turquoise end, LV_DISP_ROT_90
//                        at the orange end. Touch needs no change either way:
//                        LVGL rotates the raw coordinates itself.
// ---------------------------------------------------------------------------
// Off in normal builds: the boot splash is the UI's job (docs/BOOT_ANIMATION.md).
// Turn on to prove the panel path without LVGL when a screen goes black.
#define PANEL_BOOT_SELF_TEST    0
#define PANEL_BOOT_SELF_TEST_MS 600

// Boot probe: before the self-test, try each candidate panel init in turn
// for PANEL_BOOT_PROBE_HOLD_MS each, printing a step number to the console.
// Set to 1 only while finding out what a panel wants; it lengthens every boot.
// It has answered: round one found the vendor's short init table at fault and
// round two bisected it to the missing delay between SLPIN and SLPOUT. Left in
// place, off, for the next panel revision.
#define PANEL_BOOT_PROBE         0
#define PANEL_BOOT_PROBE_HOLD_MS 2500

// ---------------------------------------------------------------------------
// Setup portal
// ---------------------------------------------------------------------------
#define WIFI_AP_NAME     "Airstream-Weather"
// The setup network is WPA2. Its password is not here: eight digits are
// generated on first boot, kept in NVS, shown on the setup screen (and in
// its QR code), and rotated by CHANGE NETWORK. WPA2-PSK needs eight
// characters, which is why it is not six.
#define WIFI_AP_PASSWORD_LEN 8
#define OTA_HOSTNAME     "airstream-weather"
#define SETUP_PORTAL_TIMEOUT_S 0       // 0 = stay up until configured

// ---------------------------------------------------------------------------
// Weather
//
// Data comes from Open-Meteo (https://open-meteo.com). It needs no API key and
// no account, which is the single biggest reason this project installs in two
// minutes instead of twenty.
//
// Leave LAT/LON at 0 to have the panel locate itself from its IP address on
// first boot. That is the right default for a trailer: park somewhere new,
// and the forecast follows you. Set them explicitly in the setup portal to
// pin the panel to a fixed location.
// ---------------------------------------------------------------------------
#define WX_DEFAULT_LAT   0.0f
#define WX_DEFAULT_LON   0.0f
#define WX_DEFAULT_UNITS_IMPERIAL true   // fahrenheit + mph + inches

#define WX_HOURLY_SLOTS       7          // hours shown on the Today strip, after a column of row labels
#define WX_DAILY_DAYS         10         // days on the Forecast screen
#define WX_HOURLY_FETCH       36         // hours parsed and kept in memory
#define WX_PRESSURE_HISTORY   25         // hourly MSL pressure, -24h .. now, for the trend
#define WX_REFRESH_INTERVAL_S (10 * 60)  // successful refresh cadence
#define WX_RETRY_INTERVAL_S   60         // after a failed fetch
#define WX_QUICK_RETRIES      2          // immediate retries of a dropped transfer...
#define WX_QUICK_RETRY_S      4          // ...this far apart, before the slow cadence
#define WX_HTTP_TIMEOUT_MS    12000
#define WX_BODY_MAX           (256 * 1024)  // a forecast body is ~35KB; this is the ceiling, in PSRAM

// ---------------------------------------------------------------------------
// Backlight and auto-dimming
//
// The board has no ambient light sensor, so "auto brightness" is driven by the
// sun instead: Open-Meteo returns sunrise and sunset for the panel's actual
// location, and brightness follows that curve. Because the location tracks the
// trailer, so does the dimming - it stays correct when you move a thousand
// miles west, with nothing to reconfigure.
//
// A manual level (the brightness bar, or the BOOT button) holds until the
// sun moves the panel into a different part of its day - dawn, day, dusk,
// night, the small hours - or the panel resets; then Auto takes over again.
// In the small hours, with nobody about, the panel drops to the night clock
// at BL_LEVEL_DEEPNIGHT; a touch brings back whatever level was set before.
// ---------------------------------------------------------------------------
#define BL_PWM_FREQ_HZ    2000   // above audible, and a rate the LED driver is known to track
#define BL_PWM_RESOLUTION 12     // duty 0-4095; brightness levels stay 0-255

#define BL_LEVEL_DAY      255    // full sun through the trailer windows
#define BL_LEVEL_DUSK     140    // civil twilight
#define BL_LEVEL_NIGHT     45    // evening, lights on inside
// Measured on the glass: 12 shows nothing, 14 is the first level that
// produces light, 16 is legible, 18 is easy to read.
#define BL_LEVEL_DEEPNIGHT 16    // small hours - a nightlight, not a screen
#define BL_LEVEL_MIN       14    // never fully dark unless explicitly off

// Minutes of ramp either side of sunrise/sunset. The transition is slow enough
// that you never catch it happening.
#define BL_TWILIGHT_RAMP_MIN 45

// Local wall-clock hours bounding the DEEPNIGHT floor.
#define BL_DEEPNIGHT_START_H 23
#define BL_DEEPNIGHT_END_H    6

// Presence: a touch within this long of now keeps the panel out of the
// night clock. Solar changes ease over BL_FADE_MS; a manual set is as good
// as immediate, because a finger on the bar expects the glass to follow it.
#define BL_PRESENCE_HOLD_MS 30000
#define BL_FADE_MS          1500
#define BL_MANUAL_FADE_MS   80

// The brightness bar's nine divisions, dimmest first. The BOOT button steps
// through the first, the middle and the last, then back to Auto. Spaced so
// each step looks like roughly the same change, which a linear ladder does
// not (the eye reads luminance on a curve).
#define BL_PRESETS       { 18, 30, 45, 65, 90, 120, 160, 205, 255 }
#define BL_PRESET_COUNT  9

// ---------------------------------------------------------------------------
// Interaction timing
// ---------------------------------------------------------------------------
#define BTN_DEBOUNCE_MS      35
#define BTN_LONG_PRESS_MS    800
#define BTN_DOUBLE_GAP_MS    350

#define GESTURE_MIN_DISTANCE 40    // px of travel before a swipe counts
// The System drawer opens behind a gate: a swipe from one edge of the glass
// to the other (a touch beginning within UI_EDGE_SWIPE_PX of one side and
// ending within it of the other) shows a settings button that wants three
// taps, each within UI_GATE_WINDOW_MS of the last, or it fades.
#define UI_EDGE_SWIPE_PX     30
#define UI_GATE_TAPS         3
#define UI_GATE_WINDOW_MS    1000

#define UI_SCREEN_ANIM_MS    280
#define UI_OVERLAY_ANIM_MS   220
#define UI_VALUE_FADE_MS     400
// Night Mode cross-fade (design/SPEC.md §5). Tied to the backlight fade so
// the layout and the brightness arrive together.
#define UI_NIGHT_FADE_MS     BL_FADE_MS

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
