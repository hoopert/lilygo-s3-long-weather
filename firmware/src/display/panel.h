// AXS15231B QSPI panel driver for the T-Display-S3-Long.
//
// Adapted from the vendor driver in Xinyuan-LilyGO/T-Display-S3-Long
// (examples/factory/AXS15231B.cpp, MIT). The vendor ships two transfer paths:
// an interrupt-driven DMA queue and a simpler polling loop. This uses the
// polling path. It is roughly 14ms for a full 180x640 frame at 32MHz over four
// data lines, which is comfortably inside our 33ms refresh budget, and it
// removes an entire class of DMA-completion race that the queued path has to
// hand-manage across flush calls.
#pragma once

#include <stdint.h>

// Brings up the QSPI bus, runs the AXS15231B init sequence, then prints
// panel_report().
void panel_init();

// Reads the controller's identity and power-mode registers back over QSPI and
// prints them. The "power mode" line is the fastest way to tell a panel that
// never heard a command from one that is on and showing whatever it was sent.
void panel_report();

// Blits a rectangle of RGB565 pixels. Blocks until the transfer completes.
void panel_push_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *pixels);

// Fills the whole panel: rows [0, split) in `top`, rows [split, 640) in
// `bottom`. Talks to the panel directly with no LVGL involved, which is what
// makes it a useful diagnostic - if this shows and the UI does not, the fault
// is LVGL-side; if this is black too, it is the driver or the backlight.
// Colours are RGB565 in the panel's byte order (use panel_rgb565()).
void panel_fill_split(uint16_t top, uint16_t bottom, uint16_t split);

// RGB565 in the byte order the panel expects on the wire.
uint16_t panel_rgb565(uint8_t r, uint8_t g, uint8_t b);

// How many times panel_push_pixels() has been called since boot. Exposed so
// the heartbeat can tell a "LVGL never flushes" fault from a "flushes land but
// nothing shows" fault without a debugger.
uint32_t panel_flush_count();

// Panel sleep. The backlight is controlled separately - see backlight.h.
void panel_sleep();
void panel_wake();
