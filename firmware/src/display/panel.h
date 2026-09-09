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

// Brings up the QSPI bus and runs the AXS15231B init sequence.
void panel_init();

// Blits a rectangle of RGB565 pixels. Blocks until the transfer completes.
void panel_push_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *pixels);

// Panel sleep. The backlight is controlled separately - see backlight.h.
void panel_sleep();
void panel_wake();
