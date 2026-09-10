// AXS15231B QSPI panel driver for the T-Display-S3-Long.
//
// Adapted from the vendor driver in Xinyuan-LilyGO/T-Display-S3-Long
// (examples/factory/AXS15231B.cpp, MIT). The vendor source carries two pixel
// paths behind #ifdef LCD_SPI_DMA; the binary they ship is built with the
// DMA one, and that is the path reproduced here - polled rather than queued
// (roughly 14ms for a full 180x640 frame at 32MHz over four data lines,
// comfortably inside the 33ms refresh budget, with none of the DMA-completion
// bookkeeping the queued version hand-manages across flush calls), but the
// same bytes on the same wires with CS held the same way.
#pragma once

#include <stdint.h>

// Brings up the QSPI bus, runs the AXS15231B init sequence, then prints
// panel_report(). The bus parameters, init table and pixel write path are
// the shipped factory binary's, exactly - see kResting in panel.cpp.
void panel_init();

// Diagnostic. Cycles through the candidate bus/init/write configurations,
// filling the glass with a two-colour split for each and printing a step
// number, so one flash answers "which of these does this glass want".
// Enabled by PANEL_BOOT_PROBE in config.h; adds ~30s to boot.
void panel_boot_probe();

// Reads the controller's identity and power-mode registers back over QSPI and
// prints them. The "power mode" line is the fastest way to tell a panel that
// never heard a command from one that is on and showing whatever it was sent.
void panel_report();

// Blits a rectangle of RGB565 pixels in the panel's own 180x640 space. Blocks
// until the transfer completes. Partial windows are supported by the
// controller but the UI never uses them - see panel_push_frame().
void panel_push_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *pixels);

// Streams one complete UI frame (UI_WIDTH x UI_HEIGHT, RGB565 in panel byte
// order, row-major) to the glass, rotating it into the panel's 180x640 space
// on the way per UI_ROTATION. This is the path LVGL uses: the UI is rendered
// unrotated into a full-size buffer and the driver turns it, so every write
// to the panel is the same full-screen window as the boot self-test - the
// one window pattern proven on this glass. Blocks for the whole transfer,
// about 25ms.
void panel_push_frame(const uint16_t *frame);

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
