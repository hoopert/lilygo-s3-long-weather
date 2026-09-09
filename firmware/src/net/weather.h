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
    float   dew_point;
    int16_t cloud_cover;      // percent
    int16_t code;             // WMO code
    bool    is_day;
};

struct WxData {
    bool   valid;
    time_t fetched_at;

    // Current conditions
    float   temp;
    float   apparent;
    float   humidity;
    float   pressure;
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

    // Forward hours, starting at the current hour
    WxHour  hours[WX_HOURLY_FETCH];
    uint8_t hour_count;

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
