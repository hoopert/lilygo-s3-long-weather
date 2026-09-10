// Weather model and the Open-Meteo client.
//
// Open-Meteo needs no API key and no account, which is why installing this
// project is "flash it, pick your Wi-Fi, done" rather than "flash it, register
// for a key, paste the key, reflash". https://open-meteo.com
//
// The fetch runs on its own task pinned to core 0, so a slow TLS handshake can
// never stall the LVGL render loop on core 1. The UI reads a snapshot under a
// mutex; it never touches the network path.
#pragma once

#include <stdint.h>
#include <time.h>

#include "config.h"

enum class WxIcon : uint8_t {
    ClearDay, ClearNight,
    PartlyDay, PartlyNight,
    Overcast, Fog,
    Drizzle, Rain, HeavyRain, FreezingRain,
    Snow, HeavySnow,
    Thunder, ThunderHail,
    Unknown,
};

enum class WxStatus : uint8_t {
    Idle,           // nothing attempted yet
    Locating,       // resolving position from IP
    Fetching,
    Ok,
    ErrorNetwork,
    ErrorParse,
};

struct WxHour {
    time_t  time;             // unix, UTC
    float   temp;
    float   apparent;
    int16_t precip_prob;      // percent
    float   precip_amount;    // mm or inches, matching the unit setting
    float   wind;
    float   gust;
    int16_t wind_dir;         // degrees the wind is coming FROM
    float   humidity;
    float   uv;               // UV index for the hour
    float   pressure;         // hPa at mean sea level
    int16_t code;             // WMO code
    bool    is_day;
};

struct WxDay {
    time_t  time;             // local midnight, as unix UTC
    float   temp_max;
    float   temp_min;
    int16_t precip_prob_max;  // percent
    float   precip_sum;       // mm or inches, matching the unit setting
    float   wind_max;
    float   uv_max;
    time_t  sunrise;          // unix, UTC
    time_t  sunset;
    int16_t code;             // WMO code for the day
};

struct WxData {
    bool   valid;
    time_t fetched_at;

    // Current conditions
    float   temp;
    float   apparent;
    float   humidity;
    float   pressure;         // hPa at mean sea level (pressure_msl), never surface
    float   wind;
    float   gust;
    int16_t wind_dir;
    int16_t code;
    bool    is_day;
    float   uv_index;
    float   visibility;

    // Today
    float   temp_max;
    float   temp_min;
    time_t  sunrise;          // unix, UTC
    time_t  sunset;
    int16_t precip_prob_max;

    // The daily forecast, days[0] being today in the resolved timezone.
    WxDay   days[WX_DAILY_DAYS];
    uint8_t day_count;

    // Forward hours, starting at the current hour. hours[0] is the hour we
    // are living in; hour_count is how many of the WX_HOURLY_FETCH slots are
    // filled (fewer only near the end of the forecast window).
    WxHour  hours[WX_HOURLY_FETCH];
    uint8_t hour_count;

    // Hourly sea-level pressure ending at the current hour, oldest first:
    // pressure_history[pressure_history_count - 1] is this hour, and the
    // sample three back is what the 3-hour trend compares against. Fed by
    // Open-Meteo's past_hours=24; NAN where a sample is missing.
    float   pressure_history[WX_PRESSURE_HISTORY];
    uint8_t pressure_history_count;
    float   pressure_delta_3h;    // now - 3h ago, hPa; NAN until there is history

    // Where and when
    float   latitude;
    float   longitude;
    long    utc_offset;       // seconds, includes DST
    char    location[48];
    bool    imperial;
};

// --- lifecycle -------------------------------------------------------------

// Starts the fetch task. Call once, after Wi-Fi is up.
void weather_begin();

// Location. Passing 0/0 makes the panel geolocate itself by IP on the next
// fetch - the right default for something bolted into a trailer.
void weather_set_location(float lat, float lon);
void weather_set_units(bool imperial);

// Ask for a refresh now, out of cadence. Used by the pull-to-refresh gesture
// and the button on the quick-settings sheet.
void weather_request_refresh();

// --- reading ---------------------------------------------------------------

// Copies the current snapshot under the mutex. Always safe to call from the UI.
void weather_snapshot(WxData &out);

// True once since the last call - lets the UI redraw only when data changed.
bool weather_consume_update_flag();

WxStatus    weather_status();
const char *weather_status_text();
uint32_t    weather_seconds_since_update();

// --- presentation helpers --------------------------------------------------
WxIcon      wx_icon_for(int16_t wmo_code, bool is_day);
const char *wx_condition_text(int16_t wmo_code);

// "N", "NNE", ... for a bearing in degrees.
const char *wx_cardinal(int16_t degrees);
