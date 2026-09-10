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
// 640x180 (wide and short), so LVGL rotates the framebuffer by 90 degrees.
//
// If the UI comes up upside-down once mounted, swap ROT_270 for ROT_90 here.
// That is the entire fix: LVGL applies the same rotation to touch input, so
// TOUCH_INVERT_X/Y below stay as they are (lv_indev.c, indev_pointer_proc).
// ---------------------------------------------------------------------------
#define UI_ROTATION      LV_DISP_ROT_270
#define UI_WIDTH         PANEL_HEIGHT   // 640 - the app-space width
#define UI_HEIGHT        PANEL_WIDTH    // 180 - the app-space height

// If taps land mirrored, flip these. LVGL already applies the 90-degree
// rotation to touch input; these correct for panel-to-digitiser mounting
// differences on top of that. The System screen has a Touch Test that prints
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
#define PANEL_BOOT_SELF_TEST    1
#define PANEL_BOOT_SELF_TEST_MS 3000

// Boot probe: before the self-test, try each candidate panel init in turn
// for PANEL_BOOT_PROBE_HOLD_MS each, printing a step number to the console.
// Set to 1 only while finding out what a panel wants; it lengthens every boot.
// Round one (8 steps) found the fault - the vendor's short init table; round
// two (5 steps, current) bisects that table. Set to 0 once it has answered.
#define PANEL_BOOT_PROBE         1
#define PANEL_BOOT_PROBE_HOLD_MS 2500

// ---------------------------------------------------------------------------
// Setup portal
// ---------------------------------------------------------------------------
#define WIFI_AP_NAME     "Airstream-Weather"
#define WIFI_AP_PASSWORD ""            // open network; the portal is local-only
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

#define WX_HOURLY_SLOTS       10         // hours shown on the Today strip
#define WX_HOURLY_FETCH       36         // hours parsed and kept in memory
#define WX_REFRESH_INTERVAL_S (10 * 60)  // successful refresh cadence
#define WX_RETRY_INTERVAL_S   60         // after a failed fetch
#define WX_HTTP_TIMEOUT_MS    12000

// ---------------------------------------------------------------------------
// Backlight and auto-dimming
//
// The board has no ambient light sensor, so "auto brightness" is driven by the
// sun instead: Open-Meteo returns sunrise and sunset for the panel's actual
// location, and brightness follows that curve. Because the location tracks the
// trailer, so does the dimming - it stays correct when you move a thousand
// miles west, with nothing to reconfigure.
//
// Touch adds a presence boost on top: any interaction lifts the panel to
// ACTIVE for a while, then it eases back down to the ambient target.
// ---------------------------------------------------------------------------
#define BL_PWM_FREQ_HZ    2000   // above audible, and a rate the LED driver is known to track
#define BL_PWM_RESOLUTION 12     // duty 0-4095; brightness levels stay 0-255

#define BL_LEVEL_DAY      255    // full sun through the trailer windows
#define BL_LEVEL_DUSK     140    // civil twilight
#define BL_LEVEL_NIGHT     45    // evening, lights on inside
#define BL_LEVEL_DEEPNIGHT 12    // small hours - a nightlight, not a screen
#define BL_LEVEL_MIN        4    // never fully dark unless explicitly off

// Minutes of ramp either side of sunrise/sunset. The transition is slow enough
// that you never catch it happening.
#define BL_TWILIGHT_RAMP_MIN 45

// Local wall-clock hours bounding the DEEPNIGHT floor.
#define BL_DEEPNIGHT_START_H 23
#define BL_DEEPNIGHT_END_H    6

// Presence boost: touch lifts brightness to at least this level, held for this
// long past the last interaction, then eased back over BL_FADE_MS.
#define BL_LEVEL_ACTIVE     255
#define BL_PRESENCE_HOLD_MS 30000
#define BL_FADE_MS          1500

// Manual override (BOOT button / brightness slider) reverts to auto after this
// long, so a nudge at midnight does not leave the panel dark all next day.
#define BL_MANUAL_REVERT_MS (4UL * 60UL * 60UL * 1000UL)

// Brightness steps the BOOT button cycles through on a short press.
#define BL_MANUAL_STEPS { 255, 180, 110, 60, 25 }

// ---------------------------------------------------------------------------
// Interaction timing
// ---------------------------------------------------------------------------
#define BTN_DEBOUNCE_MS      35
#define BTN_LONG_PRESS_MS    800
#define BTN_DOUBLE_GAP_MS    350

#define GESTURE_MIN_DISTANCE 40    // px of travel before a swipe counts

// LVGL still delivers a click when a swipe starts and ends on the same widget,
// so a swipe across an hour column would change screens AND open that hour's
// detail overlay. Clicks are ignored for this long after a gesture fires.
#define GESTURE_CLICK_SUPPRESS_MS 450
#define UI_SCREEN_ANIM_MS    280
#define UI_OVERLAY_ANIM_MS   220
#define UI_VALUE_FADE_MS     400

// ---------------------------------------------------------------------------
// Time
// ---------------------------------------------------------------------------
#define NTP_SERVER_1 "pool.ntp.org"
#define NTP_SERVER_2 "time.nist.gov"
