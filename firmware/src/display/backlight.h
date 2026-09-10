// Backlight control and the auto-dimming engine.
//
// The T-Display-S3-Long has no ambient light sensor, so there is no way to
// measure the cabin. Instead the panel infers it, from three signals it does
// have and that between them cover every case that actually comes up:
//
//   1. The sun. Open-Meteo returns sunrise and sunset for the panel's own
//      location, so brightness follows a real solar curve that stays correct
//      when the trailer moves. This is the ambient baseline.
//   2. Intent. An explicit level - the brightness bar or the BOOT button -
//      wins over the sun and holds until the sun moves the panel into a
//      different part of its day (dawn, day, dusk, night, the small hours)
//      or the panel resets. Then Auto takes over again.
//   3. Presence. In the small hours, with nobody about, the panel becomes a
//      night clock at the deep-night floor. Any touch brings back whatever
//      level was set before - manual or solar - not a boost.
//
// Solar transitions are cosine-eased over BL_FADE_MS; a manual set follows
// the finger. You should never catch the sun changing the brightness - only
// notice that it was already right.
#pragma once

#include <stdint.h>
#include <time.h>

enum class BacklightMode : uint8_t {
    Auto,      // following the solar curve
    Manual,    // an explicit level; holds until the next solar phase change
    Off,       // display blanked; any touch or button press wakes it
};

void backlight_init();

// Drive the fade. Call every loop iteration.
void backlight_tick();

// Tell the dimmer someone is here. Safe to call at high frequency.
void backlight_note_activity();

// Sun times as seconds-since-local-midnight. Fed by the weather service after
// each successful fetch. Until this is called the engine uses a plausible
// fallback curve (see backlight.cpp), so it behaves sensibly before first fix.
void backlight_set_sun(int sunrise_sod, int sunset_sod);

// Current local time, as seconds since local midnight, or -1 if the clock has
// not been set yet. Shared with the UI so the header clock and the dimmer can
// never disagree about what time it is.
int  backlight_local_seconds_of_day();
void backlight_set_utc_offset(long seconds);
long backlight_utc_offset();

// Write a level to the panel right now, bypassing the fade, and make it the
// point the next fade starts from. For the boot self-test, which runs before
// the main loop and therefore before backlight_tick() ever gets a turn.
void backlight_set_immediate(uint8_t level);

// --- control surface -------------------------------------------------------
void          backlight_set_manual(uint8_t level);   // BL_LEVEL_MIN-255, enters Manual, applies now
void          backlight_set_auto();
void          backlight_cycle_step();                // BOOT short press: lowest, middle, highest, Auto
const uint8_t *backlight_presets();                  // BL_PRESET_COUNT levels, dimmest first
void          backlight_toggle_off();                // BOOT long press
bool          backlight_is_off();
BacklightMode backlight_mode();

// The level the engine is aiming for, and the one currently on the panel.
// Both 0-255 in perceptual units, not PWM duty.
uint8_t backlight_target_level();
uint8_t backlight_current_level();

// The solar anchors the curve is built on, as seconds since local midnight,
// and whether the panel is in the daytime part of its curve. Quick Settings
// turns these into "DIMS AT 7:18 PM" / "AUTO AT DUSK".
int  backlight_sunrise_sod();
int  backlight_sunset_sod();
bool backlight_is_daytime();

// True while the panel is a night clock: the small hours, nobody in front of
// the panel for BL_PRESENCE_HOLD_MS, and not Off. The Today screen shows the
// clock on this and the weather again the moment it clears - a touch is what
// ends the night.
bool backlight_is_deep_night();
