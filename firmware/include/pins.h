// Pin map for the LilyGO T-Display-S3-Long (ESP32-S3R8, 16MB flash, 8MB PSRAM).
//
// Values are taken from the vendor's examples/factory/pins_config.h in
// Xinyuan-LilyGO/T-Display-S3-Long. Do not edit these unless you have a
// different board revision in front of you and a schematic to check against.
#pragma once

// ---------------------------------------------------------------------------
// Display - AXS15231B, 180x640, driven over QSPI
// ---------------------------------------------------------------------------
#define PANEL_WIDTH   180   // physical, portrait
#define PANEL_HEIGHT  640

#define PIN_LCD_CS    12
#define PIN_LCD_SCK   17
#define PIN_LCD_D0    13
#define PIN_LCD_D1    18
#define PIN_LCD_D2    21
#define PIN_LCD_D3    14
#define PIN_LCD_RST   16
#define PIN_LCD_BL     1    // backlight, PWM-capable via LEDC

#define LCD_SPI_HOST      SPI2_HOST
#define LCD_SPI_FREQUENCY 32000000
#define LCD_SPI_MODE      SPI_MODE0

// Bytes pushed per QSPI transaction. The vendor uses 28800 bytes (14400
// pixels); larger chunks gain little and cost DMA-capable heap.
#define LCD_SEND_BUF_PIXELS (28800 / 2)

// ---------------------------------------------------------------------------
// Touch - shared I2C bus. Two panel revisions exist in the wild; the driver
// probes for both at boot. See docs/HARDWARE.md.
//   rev A: AXS15231B integrated touch  @ 0x3B
//   rev B: CST3530 (Hynitron CST66xx)  @ 0x58
// ---------------------------------------------------------------------------
#define PIN_TP_SDA    15
#define PIN_TP_SCL    10
#define PIN_TP_IRQ    11
#define PIN_TP_RST     2

#define TOUCH_ADDR_AXS15231B  0x3B
#define TOUCH_ADDR_CST66XX    0x58

// ---------------------------------------------------------------------------
// Buttons
//
// The board has two side buttons silkscreened BOOT and RST. Only BOOT is
// usable from firmware - RST is wired straight to the ESP32 EN line and resets
// the chip in hardware before any code can observe it.
//
// The vendor header also defines PIN_BUTTON_2 as GPIO21, but GPIO21 is the
// LCD's QSPI D2 line on this board, so it is not a button and must not be
// read. Everything the second button would have done is on BOOT's press
// grammar (short / double / long) and on the touch gestures instead.
// ---------------------------------------------------------------------------
#define PIN_BUTTON_BOOT 0   // active LOW, has an external pull-up
