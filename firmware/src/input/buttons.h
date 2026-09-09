// The BOOT button, and the press grammar layered on top of it.
//
// This board gives firmware exactly one button. RST is wired to the ESP32 EN
// line and resets the chip before any code can see it, and the vendor header's
// "button 2" on GPIO21 is really the LCD's QSPI D2 line. So the one button has
// to carry the whole physical control surface:
//
//   short   step the backlight down one rung, wrapping back through Auto
//   double  jump straight back to Auto brightness
//   long    blank the display; any touch or press wakes it
//
// Long press fires on the threshold rather than on release, so the display
// blanks under your thumb instead of after you let go.
#pragma once

#include <stdint.h>

using ButtonCallback = void (*)();

void buttons_init();
void buttons_tick();

void buttons_on_short(ButtonCallback cb);
void buttons_on_double(ButtonCallback cb);
void buttons_on_long(ButtonCallback cb);
