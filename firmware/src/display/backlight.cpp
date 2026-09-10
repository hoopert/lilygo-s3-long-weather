#include "backlight.h"

#include <Arduino.h>
#include <math.h>

#include "config.h"

namespace {

constexpr int kLedcChannel = 1;
constexpr int kDutyMax = (1 << BL_PWM_RESOLUTION) - 1;

const uint8_t kManualSteps[] = BL_MANUAL_STEPS;
constexpr size_t kManualStepCount = sizeof(kManualSteps) / sizeof(kManualSteps[0]);

BacklightMode s_mode = BacklightMode::Auto;

// Fade state. Levels are perceptual 0-255; the gamma curve to PWM duty is
// applied at the very end, in apply_duty().
float    s_current  = 0.0f;
uint8_t  s_target   = BL_LEVEL_DAY;
float    s_fade_from = 0.0f;
uint32_t s_fade_start_ms = 0;
uint32_t s_fade_len_ms = BL_FADE_MS;

uint32_t s_last_activity_ms = 0;
uint32_t s_manual_set_ms = 0;
uint8_t  s_manual_level = BL_LEVEL_DAY;
size_t   s_manual_index = 0;

// Sun times as seconds since local midnight. The fallback is a temperate
// mid-latitude day, which is wrong by at most a couple of hours anywhere the
// trailer is likely to be and only applies during the first ~15 seconds after
// boot, before the first forecast lands.
int  s_sunrise_sod = 6 * 3600 + 30 * 60;
int  s_sunset_sod  = 19 * 3600 + 30 * 60;

long s_utc_offset = 0;
// SNTP answers within seconds of Wi-Fi; the UTC offset only arrives with the
// first forecast. In between, "local time" would be UTC - which for anyone
// west of Greenwich in the evening reads as the small hours and drops the
// panel to its deep-night floor moments after it connects. Until the offset
// is known the time of day is reported as unknown, and unknown means DAY.
bool s_have_utc_offset = false;

// Perceived brightness is roughly the square root of luminous output, so a
// linear PWM ramp spends most of its travel in a range the eye reads as "on".
// Squaring the input undoes that and makes the levels in config.h mean what
// they look like. The floor keeps a nominally-lit panel from rounding to black
// at the very bottom of the range.
uint32_t level_to_duty(float level) {
    if (level <= 0.0f) return 0;
    float norm = level / 255.0f;
    if (norm > 1.0f) norm = 1.0f;
    uint32_t duty = static_cast<uint32_t>(lroundf(norm * norm * kDutyMax));
    return duty == 0 ? 1 : duty;
}

void apply_duty(float level) {
    ledcWrite(kLedcChannel, level_to_duty(level));
}

void start_fade_to(uint8_t level, uint32_t duration_ms) {
    if (level == s_target && s_fade_len_ms == duration_ms) return;
    s_fade_from     = s_current;
    s_target        = level;
    s_fade_start_ms = millis();
    s_fade_len_ms   = duration_ms == 0 ? 1 : duration_ms;
}

bool in_deep_night(int sod) {
    const int start = BL_DEEPNIGHT_START_H * 3600;
    const int end   = BL_DEEPNIGHT_END_H * 3600;
    // The window wraps midnight whenever start > end, which is the normal case.
    return (start > end) ? (sod >= start || sod < end)
                         : (sod >= start && sod < end);
}

float lerp(float a, float b, float t) {
    if (t < 0.0f) t = 0.0f;
    if (t > 1.0f) t = 1.0f;
    return a + (b - a) * t;
}

// The solar curve. Four anchor levels joined by linear ramps of
// BL_TWILIGHT_RAMP_MIN either side of each solar event:
//
//   NIGHT ....\ sunrise-R  DUSK  sunrise+R /.... DAY ....\ sunset-R  DUSK  sunset+R /.... NIGHT
//
// with a DEEPNIGHT floor clamped over the small hours.
uint8_t ambient_level_for(int sod) {
    const int ramp = BL_TWILIGHT_RAMP_MIN * 60;

    const uint8_t night = in_deep_night(sod) ? BL_LEVEL_DEEPNIGHT : BL_LEVEL_NIGHT;

    float level;
    if (sod < s_sunrise_sod - ramp) {
        level = night;
    } else if (sod < s_sunrise_sod) {
        level = lerp(night, BL_LEVEL_DUSK,
                     float(sod - (s_sunrise_sod - ramp)) / float(ramp));
    } else if (sod < s_sunrise_sod + ramp) {
        level = lerp(BL_LEVEL_DUSK, BL_LEVEL_DAY,
                     float(sod - s_sunrise_sod) / float(ramp));
    } else if (sod < s_sunset_sod - ramp) {
        level = BL_LEVEL_DAY;
    } else if (sod < s_sunset_sod) {
        level = lerp(BL_LEVEL_DAY, BL_LEVEL_DUSK,
                     float(sod - (s_sunset_sod - ramp)) / float(ramp));
    } else if (sod < s_sunset_sod + ramp) {
        level = lerp(BL_LEVEL_DUSK, night,
                     float(sod - s_sunset_sod) / float(ramp));
    } else {
        level = night;
    }

    if (level < BL_LEVEL_MIN) level = BL_LEVEL_MIN;
    return static_cast<uint8_t>(lroundf(level));
}

uint8_t compute_target() {
    const uint32_t now = millis();

    if (s_mode == BacklightMode::Off) return 0;

    if (s_mode == BacklightMode::Manual) {
        if (now - s_manual_set_ms < BL_MANUAL_REVERT_MS) return s_manual_level;
        s_mode = BacklightMode::Auto;   // the override has expired
    }

    const int sod = backlight_local_seconds_of_day();
    // Without a clock there is no curve to follow, so stay bright rather than
    // guess dark - a panel that is too dim to read looks broken.
    uint8_t level = (sod < 0) ? BL_LEVEL_DAY : ambient_level_for(sod);

    // Presence boost. Somebody is in front of the panel; give them full output
    // regardless of what the sun is doing.
    if (now - s_last_activity_ms < BL_PRESENCE_HOLD_MS) {
        if (BL_LEVEL_ACTIVE > level) level = BL_LEVEL_ACTIVE;
    }
    return level;
}

}  // namespace

void backlight_init() {
    ledcSetup(kLedcChannel, BL_PWM_FREQ_HZ, BL_PWM_RESOLUTION);
    ledcAttachPin(PIN_LCD_BL, kLedcChannel);

    // Come up dark and let the first fade bring the panel in. Snapping to full
    // brightness at power-on is jarring in a dark trailer.
    s_current = 0.0f;
    apply_duty(0.0f);

    s_last_activity_ms = millis();
    s_fade_start_ms = millis();
    s_fade_from = 0.0f;
    s_target = BL_LEVEL_DAY;
}

void backlight_tick() {
    const uint8_t want = compute_target();
    if (want != s_target) {
        // Waking from Off should feel instant; everything else eases.
        const uint32_t ms = (s_target == 0 && want > 0) ? 250 : BL_FADE_MS;
        start_fade_to(want, ms);
    }

    const uint32_t elapsed = millis() - s_fade_start_ms;
    if (elapsed >= s_fade_len_ms) {
        if (s_current != float(s_target)) {
            s_current = s_target;
            apply_duty(s_current);
        }
        return;
    }

    // Cosine ease-in-out. The flat ends are what make the transition
    // imperceptible - a linear ramp is visible at both the start and the stop.
    const float p = float(elapsed) / float(s_fade_len_ms);
    const float eased = 0.5f - 0.5f * cosf(float(M_PI) * p);
    s_current = s_fade_from + (float(s_target) - s_fade_from) * eased;
    apply_duty(s_current);
}

void backlight_set_immediate(uint8_t level) {
    s_current   = level;
    s_fade_from = level;
    s_target    = level;
    apply_duty(s_current);
}

void backlight_note_activity() {
    s_last_activity_ms = millis();
    if (s_mode == BacklightMode::Off) s_mode = BacklightMode::Auto;
}

void backlight_set_sun(int sunrise_sod, int sunset_sod) {
    if (sunrise_sod < 0 || sunset_sod < 0) return;
    if (sunset_sod <= sunrise_sod) return;   // polar edge case; keep the fallback
    s_sunrise_sod = sunrise_sod;
    s_sunset_sod  = sunset_sod;
}

void backlight_set_utc_offset(long seconds) {
    s_utc_offset = seconds;
    s_have_utc_offset = true;
}
long backlight_utc_offset() { return s_utc_offset; }

int backlight_local_seconds_of_day() {
    const time_t now = time(nullptr);
    // Anything before 2021 means SNTP has not answered yet; and UTC alone is
    // not a time of day anywhere the panel is likely to be mounted.
    if (now < 1600000000 || !s_have_utc_offset) return -1;
    long long local = static_cast<long long>(now) + s_utc_offset;
    int sod = static_cast<int>(local % 86400);
    if (sod < 0) sod += 86400;
    return sod;
}

void backlight_set_manual(uint8_t level) {
    s_mode = BacklightMode::Manual;
    s_manual_level = level;
    s_manual_set_ms = millis();
}

void backlight_set_auto() {
    s_mode = BacklightMode::Auto;
}

void backlight_cycle_step() {
    // The BOOT button is the only user button the board exposes (RST resets the
    // chip in hardware), so a short press walks a fixed ladder rather than
    // nudging by a delta. The rule is simply "each press is dimmer than the
    // last, until it wraps back to Auto" - which is what a hand reaching for an
    // unlabelled button in the dark is expecting.
    if (s_mode == BacklightMode::Off) {
        s_mode = BacklightMode::Auto;
        backlight_note_activity();
        return;
    }

    if (s_mode == BacklightMode::Auto) {
        // Entering the ladder: land on the first rung genuinely dimmer than
        // what is on screen, so the first press visibly does something.
        s_manual_index = kManualStepCount - 1;
        for (size_t i = 0; i < kManualStepCount; i++) {
            if (kManualSteps[i] < s_target) { s_manual_index = i; break; }
        }
        backlight_set_manual(kManualSteps[s_manual_index]);
    } else if (s_manual_index + 1 >= kManualStepCount) {
        backlight_set_auto();          // past the dimmest rung, back to Auto
    } else {
        s_manual_index++;
        backlight_set_manual(kManualSteps[s_manual_index]);
    }

    backlight_note_activity();
}

void backlight_toggle_off() {
    s_mode = (s_mode == BacklightMode::Off) ? BacklightMode::Auto : BacklightMode::Off;
    if (s_mode == BacklightMode::Auto) backlight_note_activity();
}

bool          backlight_is_off()        { return s_mode == BacklightMode::Off; }
int      backlight_sunrise_sod() { return s_sunrise_sod; }
int      backlight_sunset_sod()  { return s_sunset_sod; }
uint32_t backlight_manual_remaining_ms() {
    if (s_mode != BacklightMode::Manual) return 0;
    const uint32_t held = millis() - s_manual_set_ms;
    return held < BL_MANUAL_REVERT_MS ? BL_MANUAL_REVERT_MS - held : 0;
}
bool          backlight_is_deep_night() {
    return s_mode == BacklightMode::Auto && s_target <= BL_LEVEL_DEEPNIGHT;
}
BacklightMode backlight_mode()          { return s_mode; }
uint8_t       backlight_target_level()  { return s_target; }
uint8_t       backlight_current_level() { return static_cast<uint8_t>(lroundf(s_current)); }
