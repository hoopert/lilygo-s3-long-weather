#include "format.h"

#include <math.h>
#include <stdio.h>

namespace {

// Breaks a UTC timestamp into local hour/minute without touching the C
// library's global timezone.
void local_hm(time_t utc, long offset, int &hour, int &minute) {
    long long local = static_cast<long long>(utc) + offset;
    long long sod = local % 86400;
    if (sod < 0) sod += 86400;
    hour = int(sod / 3600);
    minute = int((sod % 3600) / 60);
}

}  // namespace

void fmt_hour(time_t utc, long offset, char *out, size_t len) {
    int h, m;
    local_hm(utc, offset, h, m);
    const char *suffix = (h < 12) ? "AM" : "PM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, len, "%d%s", h12, suffix);
}

void fmt_clock(time_t utc, long offset, char *out, size_t len) {
    int h, m;
    local_hm(utc, offset, h, m);
    const char *suffix = (h < 12) ? "AM" : "PM";
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, len, "%d:%02d %s", h12, m, suffix);
}

void fmt_clock_compact(time_t utc, long offset, char *out, size_t len) {
    int h, m;
    local_hm(utc, offset, h, m);
    int h12 = h % 12;
    if (h12 == 0) h12 = 12;
    snprintf(out, len, "%d:%02d%c", h12, m, (h < 12) ? 'A' : 'P');
}

void fmt_weekday(time_t utc, long offset, char *out, size_t len) {
    static const char *const kDays[7] = {"THU", "FRI", "SAT", "SUN", "MON", "TUE", "WED"};
    long long local = static_cast<long long>(utc) + offset;
    long long days = local / 86400;
    if (local < 0 && local % 86400 != 0) days--;
    // 1970-01-01 was a Thursday.
    snprintf(out, len, "%s", kDays[((days % 7) + 7) % 7]);
}

void fmt_temp(float value, char *out, size_t len) {
    if (isnan(value)) { snprintf(out, len, "--°"); return; }
    snprintf(out, len, "%d°", int(lroundf(value)));
}

// The hourly columns are 43px wide. A degree sign costs about 9px there and
// carries no information the hero temperature has not already established, so
// the strip drops it and keeps room for three digits.
void fmt_temp_plain(float value, char *out, size_t len) {
    if (isnan(value)) { snprintf(out, len, "--"); return; }
    snprintf(out, len, "%d", int(lroundf(value)));
}

void fmt_temp_signed(float value, char *out, size_t len) {
    if (isnan(value)) { snprintf(out, len, "--°"); return; }
    snprintf(out, len, "%+d°", int(lroundf(value)));
}

void fmt_relative(uint32_t seconds_ago, char *out, size_t len) {
    if (seconds_ago == UINT32_MAX) { snprintf(out, len, "NEVER"); return; }
    if (seconds_ago < 60)          { snprintf(out, len, "JUST NOW"); return; }
    if (seconds_ago < 3600) {
        snprintf(out, len, "%u MIN AGO", unsigned(seconds_ago / 60));
        return;
    }
    const unsigned hours = seconds_ago / 3600;
    snprintf(out, len, "%u HR AGO", hours);
}

bool is_current_hour(time_t utc, long offset) {
    const time_t now = time(nullptr);
    if (now < 1600000000) return false;
    const long long a = (static_cast<long long>(utc) + offset) / 3600;
    const long long b = (static_cast<long long>(now) + offset) / 3600;
    return a == b;
}
