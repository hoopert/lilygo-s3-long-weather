// Capacitive touch for the T-Display-S3-Long.
//
// Two panel revisions ship under the same product name and the same silkscreen,
// with different touch controllers on the same I2C bus:
//
//   rev A  AXS15231B @ 0x3B  - touch integrated into the display controller
//   rev B  CST3530   @ 0x58  - a separate Hynitron CST66xx-family part
//
// There is no marking that distinguishes them, so the driver probes for both at
// boot and binds to whichever answers. The System screen reports which one was
// found, which is also the fastest way to tell a wiring fault from a revision
// you did not expect.
#pragma once

#include <stdint.h>

enum class TouchChip : uint8_t { None, AXS15231B, CST66XX };

struct TouchPoint {
    bool     pressed;
    uint16_t x;   // raw panel coordinates: 0..179
    uint16_t y;   // raw panel coordinates: 0..639
};

// Resets the controller and probes the bus. Safe to call once Wire is free.
bool touch_init();

// Latest sample. Polls the I2C bus, so call it from exactly one place - the
// LVGL input callback. Everything else wants touch_last().
TouchPoint touch_read();

// The most recent sample, without touching the bus. Safe to call from anywhere
// and at any rate; the System screen's touch readout uses it.
TouchPoint touch_last();

TouchChip   touch_chip();
const char *touch_chip_name();
