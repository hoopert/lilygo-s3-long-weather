// Small formatting helpers shared by the screens and the overlays.
//
// All of them take a UTC timestamp plus the location's offset rather than
// relying on the C library's timezone state. The panel keeps its RTC in UTC and
// gets a DST-correct offset from Open-Meteo for wherever it currently is, which
// avoids carrying a tz database around for a device that changes timezone by
// being towed.
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <time.h>

void fmt_hour(time_t utc, long offset, char *out, size_t len);      // "2PM"
void fmt_clock(time_t utc, long offset, char *out, size_t len);     // "2:35 PM"
void fmt_temp(float value, char *out, size_t len);                  // "72°"
void fmt_temp_plain(float value, char *out, size_t len);             // "72"
void fmt_temp_signed(float value, char *out, size_t len);           // "-4°"
void fmt_relative(uint32_t seconds_ago, char *out, size_t len);     // "3 MIN AGO"

// True when the timestamp falls inside the hour we are currently living in.
bool is_current_hour(time_t utc, long offset);
