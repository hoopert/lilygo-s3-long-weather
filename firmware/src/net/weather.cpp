#include "weather.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <string.h>

#include "display/backlight.h"
#include "ui/pressure_logic.h"

namespace {

SemaphoreHandle_t s_mutex = nullptr;
WxData   s_data = {};
WxStatus s_status = WxStatus::Idle;
volatile bool s_update_flag = false;
volatile bool s_refresh_requested = false;

float s_lat = WX_DEFAULT_LAT;
float s_lon = WX_DEFAULT_LON;
bool  s_imperial = WX_DEFAULT_UNITS_IMPERIAL;
bool  s_located = false;

// ArduinoJson's document grows to a few tens of KB parsing three days of hourly
// data. That is more than we want to take out of internal SRAM, where LVGL's
// heap and the TLS buffers live, so the document is allocated from PSRAM.
struct PsramAllocator : ArduinoJson::Allocator {
    void *allocate(size_t size) override {
        return heap_caps_malloc(size, MALLOC_CAP_SPIRAM);
    }
    void deallocate(void *pointer) override {
        heap_caps_free(pointer);
    }
    void *reallocate(void *ptr, size_t new_size) override {
        return heap_caps_realloc(ptr, new_size, MALLOC_CAP_SPIRAM);
    }
};
PsramAllocator s_allocator;

void set_status(WxStatus s) { s_status = s; }

// A response body in PSRAM. data is null when the read failed; len is then
// how far it got, which is the number worth logging.
struct Body {
    char  *data;
    size_t len;
};

// Reads until the server closes the connection (HTTP/1.0, so it will) or
// nothing arrives for WX_HTTP_TIMEOUT_MS. Grows the buffer in 16KB steps up
// to WX_BODY_MAX; a forecast is ~30KB.
Body read_body(WiFiClient &stream) {
    size_t cap = 16 * 1024, len = 0;
    char *buf = static_cast<char *>(heap_caps_malloc(cap, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) return {nullptr, 0};

    uint32_t last_data_ms = millis();
    for (;;) {
        const int avail = stream.available();
        if (avail > 0) {
            if (len + size_t(avail) + 1 > cap) {
                cap = cap * 2 > WX_BODY_MAX ? WX_BODY_MAX : cap * 2;
                if (len + size_t(avail) + 1 > cap) break;   // over the cap: give up
                char *grown = static_cast<char *>(heap_caps_realloc(buf, cap, MALLOC_CAP_SPIRAM));
                if (grown == nullptr) break;
                buf = grown;
            }
            const int n = stream.read(reinterpret_cast<uint8_t *>(buf + len), avail);
            if (n > 0) { len += size_t(n); last_data_ms = millis(); }
            continue;
        }
        if (!stream.connected()) {
            buf[len] = '\0';
            return {buf, len};
        }
        if (millis() - last_data_ms > WX_HTTP_TIMEOUT_MS) break;
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    heap_caps_free(buf);
    return {nullptr, len};
}

// ---------------------------------------------------------------------------
// IP geolocation
//
// Only used when no coordinates have been configured. ip-api.com needs no key
// over plain HTTP, and the accuracy - the right town, usually - is exactly
// right for a weather forecast and no more precise than anyone would want a
// device broadcasting anyway.
// ---------------------------------------------------------------------------
bool geolocate_by_ip() {
    set_status(WxStatus::Locating);

    WiFiClient client;
    HTTPClient http;
    http.setTimeout(WX_HTTP_TIMEOUT_MS);
    if (!http.begin(client, "http://ip-api.com/json/?fields=status,city,regionName,lat,lon")) {
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        http.end();
        return false;
    }

    JsonDocument doc(&s_allocator);
    const DeserializationError err = deserializeJson(doc, http.getStream());
    http.end();
    if (err || doc["status"] != "success") return false;

    s_lat = doc["lat"] | 0.0f;
    s_lon = doc["lon"] | 0.0f;
    if (s_lat == 0.0f && s_lon == 0.0f) return false;

    const char *city = doc["city"] | "";
    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        strncpy(s_data.location, city, sizeof(s_data.location) - 1);
        s_data.location[sizeof(s_data.location) - 1] = '\0';
        xSemaphoreGive(s_mutex);
    }

    s_located = true;
    Serial.printf("[wx] located by IP: %s (%.3f, %.3f)\n", city, s_lat, s_lon);
    return true;
}

// Open-Meteo does not return a place name, so when the panel was pinned to
// explicit coordinates we derive one from the IANA timezone it does return.
// "America/Los_Angeles" becomes "Los Angeles", which is close enough to be
// useful and never wrong in a way that matters.
void location_from_timezone(const char *tz, char *out, size_t out_len) {
    if (tz == nullptr || *tz == '\0') return;
    const char *slash = strrchr(tz, '/');
    const char *name = slash ? slash + 1 : tz;
    strncpy(out, name, out_len - 1);
    out[out_len - 1] = '\0';
    for (size_t i = 0; out[i] != '\0'; i++) {
        if (out[i] == '_') out[i] = ' ';
    }
}

String build_url() {
    String url = "https://api.open-meteo.com/v1/forecast";
    url += "?latitude=" + String(s_lat, 4);
    url += "&longitude=" + String(s_lon, 4);
    url += "&current=temperature_2m,apparent_temperature,relative_humidity_2m,is_day,"
           "weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,pressure_msl";
    url += "&hourly=temperature_2m,apparent_temperature,precipitation_probability,"
           "precipitation,weather_code,wind_speed_10m,wind_direction_10m,wind_gusts_10m,"
           "relative_humidity_2m,visibility,uv_index,is_day,pressure_msl";
    url += "&daily=temperature_2m_max,temperature_2m_min,sunrise,sunset,"
           "precipitation_probability_max,weather_code,wind_speed_10m_max,"
           "uv_index_max,precipitation_sum";
    // Sea-level pressure, not surface: the outlook bands and body-effect
    // thresholds (design/logic.json) are written for MSL, and at altitude the
    // surface reading is hundreds of hPa lower. past_hours gives the 24
    // samples the trend and the pressure graph are drawn from.
    // Ten days of daily rows for the Forecast screen; the hourly rows are
    // bounded in hours, not days, so the body does not carry 240 hours of
    // fifteen variables for a strip that shows eight.
    url += "&timezone=auto&timeformat=unixtime&forecast_days=" + String(WX_DAILY_DAYS);
    url += "&forecast_hours=" + String(WX_HOURLY_FETCH) + "&past_hours=24";
    if (s_imperial) {
        url += "&temperature_unit=fahrenheit&wind_speed_unit=mph&precipitation_unit=inch";
    }
    return url;
}

bool fetch_forecast() {
    set_status(WxStatus::Fetching);

    WiFiClientSecure client;
    // Open-Meteo is a public, unauthenticated, read-only endpoint and the panel
    // carries no secret to leak, so pinning a CA bundle here would buy nothing
    // but a yearly expiry outage in a device mounted behind a wall panel.
    client.setInsecure();
    client.setTimeout(WX_HTTP_TIMEOUT_MS / 1000);

    HTTPClient http;
    http.setTimeout(WX_HTTP_TIMEOUT_MS);
    http.setConnectTimeout(WX_HTTP_TIMEOUT_MS);
    // HTTP/1.0, so the server cannot answer with Transfer-Encoding: chunked.
    // The JSON parser below reads http.getStream() directly, and a chunked
    // body arrives with hex chunk-size lines interleaved with the JSON - the
    // first byte the parser sees is a chunk size, not '{', and it fails with
    // InvalidInput. Open-Meteo chunks its HTTPS responses by default.
    http.useHTTP10(true);
    if (!http.begin(client, build_url())) {
        set_status(WxStatus::ErrorNetwork);
        return false;
    }

    const int code = http.GET();
    if (code != HTTP_CODE_OK) {
        Serial.printf("[wx] HTTP %d\n", code);
        http.end();
        set_status(WxStatus::ErrorNetwork);
        return false;
    }

    // The body is read whole, as fast as the radio delivers it, and parsed
    // afterwards. Parsing straight from the TLS stream was the first build's
    // way, and it worked while the response was small; with a day of history
    // added the parse ran slower than the server's patience, the connection
    // was reset mid-body, and the parser reported IncompleteInput at about
    // the same point every time.
    Body body = read_body(http.getStream());
    http.end();
    if (body.data == nullptr) {
        Serial.printf("[wx] body read failed after %u bytes\n", unsigned(body.len));
        set_status(WxStatus::ErrorNetwork);
        return false;
    }

    JsonDocument doc(&s_allocator);
    const DeserializationError err = deserializeJson(doc, body.data, body.len);
    heap_caps_free(body.data);
    if (err) {
        Serial.printf("[wx] parse failed: %s (HTTP %d, %u bytes)\n",
                      err.c_str(), code, unsigned(body.len));
        set_status(WxStatus::ErrorParse);
        return false;
    }

    JsonObject current = doc["current"];
    JsonObject hourly  = doc["hourly"];
    JsonObject daily   = doc["daily"];
    if (current.isNull() || hourly.isNull() || daily.isNull()) {
        set_status(WxStatus::ErrorParse);
        return false;
    }

    WxData next = {};
    next.imperial   = s_imperial;
    next.latitude   = doc["latitude"]  | s_lat;
    next.longitude  = doc["longitude"] | s_lon;
    next.utc_offset = doc["utc_offset_seconds"] | 0L;

    next.temp      = current["temperature_2m"]      | NAN;
    next.apparent  = current["apparent_temperature"]| NAN;
    next.humidity  = current["relative_humidity_2m"]| NAN;
    next.pressure  = current["pressure_msl"]        | NAN;
    next.wind      = current["wind_speed_10m"]      | NAN;
    next.gust      = current["wind_gusts_10m"]      | NAN;
    next.wind_dir  = current["wind_direction_10m"]  | 0;
    next.code      = current["weather_code"]        | -1;
    next.is_day    = (current["is_day"] | 1) != 0;
    const time_t current_time = current["time"] | time(nullptr);

    // daily[0] is today; Open-Meteo starts the daily block at the current date
    // in the resolved timezone.
    next.temp_max        = daily["temperature_2m_max"][0] | NAN;
    next.temp_min        = daily["temperature_2m_min"][0] | NAN;
    next.sunrise         = daily["sunrise"][0]            | time_t(0);
    next.sunset          = daily["sunset"][0]             | time_t(0);
    next.precip_prob_max = daily["precipitation_probability_max"][0] | 0;

    {
        JsonArray d_time = daily["time"];
        JsonArray d_max  = daily["temperature_2m_max"];
        JsonArray d_min  = daily["temperature_2m_min"];
        JsonArray d_prob = daily["precipitation_probability_max"];
        JsonArray d_code = daily["weather_code"];
        JsonArray d_wind = daily["wind_speed_10m_max"];
        JsonArray d_uv   = daily["uv_index_max"];
        JsonArray d_psum = daily["precipitation_sum"];
        JsonArray d_rise = daily["sunrise"];
        JsonArray d_set  = daily["sunset"];
        uint8_t k = 0;
        for (size_t i = 0; i < d_time.size() && k < WX_DAILY_DAYS; i++, k++) {
            WxDay &day = next.days[k];
            day.time            = d_time[i] | time_t(0);
            day.temp_max        = d_max[i]  | NAN;
            day.temp_min        = d_min[i]  | NAN;
            day.precip_prob_max = d_prob[i] | 0;
            day.code            = d_code[i] | -1;
            day.wind_max        = d_wind[i] | NAN;
            day.uv_max          = d_uv[i]   | NAN;
            day.precip_sum      = d_psum[i] | 0.0f;
            day.sunrise         = d_rise[i] | time_t(0);
            day.sunset          = d_set[i]  | time_t(0);
        }
        next.day_count = k;
    }

    JsonArray h_time = hourly["time"];
    const size_t h_len = h_time.size();

    // Start at the hour we are currently inside, so the first column reads
    // "NOW" rather than an hour that has already gone.
    size_t start = 0;
    while (start < h_len && (time_t(h_time[start] | 0) + 3600) <= current_time) start++;

    JsonArray h_temp   = hourly["temperature_2m"];
    JsonArray h_app    = hourly["apparent_temperature"];
    JsonArray h_pprob  = hourly["precipitation_probability"];
    JsonArray h_pamt   = hourly["precipitation"];
    JsonArray h_code   = hourly["weather_code"];
    JsonArray h_wind   = hourly["wind_speed_10m"];
    JsonArray h_gust   = hourly["wind_gusts_10m"];
    JsonArray h_dir    = hourly["wind_direction_10m"];
    JsonArray h_hum    = hourly["relative_humidity_2m"];
    JsonArray h_vis    = hourly["visibility"];
    JsonArray h_uv     = hourly["uv_index"];
    JsonArray h_isday  = hourly["is_day"];
    JsonArray h_pmsl   = hourly["pressure_msl"];

    uint8_t n = 0;
    for (size_t i = start; i < h_len && n < WX_HOURLY_FETCH; i++, n++) {
        WxHour &s = next.hours[n];
        s.time          = h_time[i]  | time_t(0);
        s.temp          = h_temp[i]  | NAN;
        s.apparent      = h_app[i]   | NAN;
        s.precip_prob   = h_pprob[i] | 0;
        s.precip_amount = h_pamt[i]  | 0.0f;
        s.code          = h_code[i]  | -1;
        s.wind          = h_wind[i]  | NAN;
        s.gust          = h_gust[i]  | NAN;
        s.wind_dir      = h_dir[i]   | 0;
        s.humidity      = h_hum[i]   | NAN;
        s.uv            = h_uv[i]    | NAN;
        s.pressure      = h_pmsl[i]  | NAN;
        s.is_day        = (h_isday[i] | 1) != 0;
    }
    next.hour_count = n;

    // The pressure history: the 24 hours before `start` plus `start` itself,
    // oldest first. A fresh forecast at 00:xx still has past_hours behind it,
    // so this is normally the full 25; it is shorter only if the API returned
    // less than it was asked for.
    {
        const size_t first = start >= (WX_PRESSURE_HISTORY - 1) ? start - (WX_PRESSURE_HISTORY - 1) : 0;
        uint8_t k = 0;
        for (size_t i = first; i <= start && i < h_len && k < WX_PRESSURE_HISTORY; i++, k++) {
            next.pressure_history[k] = h_pmsl[i] | NAN;
        }
        next.pressure_history_count = k;
        next.pressure_delta_3h = pressure_delta_3h(next.pressure_history, k, next.pressure);
    }

    // UV and visibility have no "current" equivalent in the request, so the
    // hour we are standing in is the best available reading.
    if (n > 0) {
        next.uv_index   = h_uv[start]  | 0.0f;
        next.visibility = h_vis[start] | 0.0f;
    }

    if (!s_located) {
        location_from_timezone(doc["timezone"] | "", next.location, sizeof(next.location));
    }

    next.valid = true;
    next.fetched_at = time(nullptr);

    if (xSemaphoreTake(s_mutex, portMAX_DELAY) == pdTRUE) {
        // A location resolved by IP is better than one derived from a timezone,
        // so keep the existing name when we have one.
        if (s_located && s_data.location[0] != '\0') {
            strncpy(next.location, s_data.location, sizeof(next.location) - 1);
            next.location[sizeof(next.location) - 1] = '\0';
        }
        s_data = next;
        xSemaphoreGive(s_mutex);
    }

    // Hand the sun times to the dimmer. This is what makes auto-brightness
    // follow the trailer rather than a hardcoded schedule.
    backlight_set_utc_offset(next.utc_offset);
    if (next.sunrise > 0 && next.sunset > 0) {
        const int sunrise_sod = int((next.sunrise + next.utc_offset) % 86400);
        const int sunset_sod  = int((next.sunset  + next.utc_offset) % 86400);
        backlight_set_sun(sunrise_sod, sunset_sod);
    }

    s_update_flag = true;
    set_status(WxStatus::Ok);
    Serial.printf("[wx] %s: %.0f deg, %u hours\n", next.location, next.temp, next.hour_count);
    Serial.printf("[wx] pressure %.1f hPa msl, 3h %+.1f (%u samples): %s\n",
                  next.pressure, next.pressure_delta_3h, unsigned(next.pressure_history_count),
                  pressure_outlook(next.pressure_delta_3h).word);
    return true;
}

void weather_task(void *) {
    // A short settle before the first fetch: DHCP and SNTP are usually still
    // finishing when this task starts, and a forecast timestamped from a 1970
    // clock lines the hourly strip up against the wrong hours.
    vTaskDelay(pdMS_TO_TICKS(1500));

    uint32_t next_attempt_ms = 0;

    for (;;) {
        const uint32_t now = millis();
        const bool due = (int32_t)(now - next_attempt_ms) >= 0;

        if ((due || s_refresh_requested) && WiFi.status() == WL_CONNECTED) {
            s_refresh_requested = false;

            if (s_lat == 0.0f && s_lon == 0.0f) geolocate_by_ip();

            // A transfer that drops mid-body (a TLS receive error shows as
            // IncompleteInput) is retried after a short pause rather than
            // in a minute: the first fetch after setup runs while the portal
            // is being torn down and the phone is still leaving the AP, and
            // waiting WX_RETRY_INTERVAL_S for that is the difference between
            // a forecast in ten seconds and one in three minutes.
            bool ok = false;
            if (!(s_lat == 0.0f && s_lon == 0.0f)) {
                for (int attempt = 0; attempt < WX_QUICK_RETRIES + 1 && !ok; attempt++) {
                    if (attempt > 0) {
                        Serial.printf("[wx] retry %d/%d in %ds\n", attempt, WX_QUICK_RETRIES,
                                      WX_QUICK_RETRY_S);
                        vTaskDelay(pdMS_TO_TICKS(WX_QUICK_RETRY_S * 1000));
                        if (WiFi.status() != WL_CONNECTED) break;
                    }
                    ok = fetch_forecast();
                }
            }

            next_attempt_ms = millis() +
                              (ok ? WX_REFRESH_INTERVAL_S : WX_RETRY_INTERVAL_S) * 1000UL;
        }

        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

}  // namespace

void weather_begin() {
    if (s_mutex == nullptr) s_mutex = xSemaphoreCreateMutex();
    strncpy(s_data.location, "", sizeof(s_data.location));

    // Core 0 keeps TLS and JSON work off the core running LVGL. The TLS
    // handshake is the stack high-water mark by some margin; 12KB leaves room
    // for it plus the parser, with the JSON document itself out in PSRAM.
    xTaskCreatePinnedToCore(weather_task, "weather", 12288, nullptr, 3, nullptr, 0);
}

void weather_set_location(float lat, float lon) {
    s_lat = lat;
    s_lon = lon;
    s_located = false;
    s_refresh_requested = true;
}

void weather_set_units(bool imperial) {
    if (imperial == s_imperial) return;
    s_imperial = imperial;
    s_refresh_requested = true;
}

void weather_request_refresh() { s_refresh_requested = true; }

void weather_snapshot(WxData &out) {
    if (s_mutex == nullptr) { out = {}; return; }
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        out = s_data;
        xSemaphoreGive(s_mutex);
    } else {
        out = {};   // zeroed, not merely flagged - callers read fields freely
    }
}

bool weather_consume_update_flag() {
    if (!s_update_flag) return false;
    s_update_flag = false;
    return true;
}

WxStatus weather_status() { return s_status; }

const char *weather_status_text() {
    switch (s_status) {
        case WxStatus::Idle:         return "STARTING";
        case WxStatus::Locating:     return "FINDING LOCATION";
        case WxStatus::Fetching:     return "FETCHING FORECAST";
        case WxStatus::Ok:           return "UP TO DATE";
        case WxStatus::ErrorNetwork: return "NO CONNECTION";
        case WxStatus::ErrorParse:   return "BAD RESPONSE";
    }
    return "";
}

uint32_t weather_seconds_since_update() {
    // Deliberately not via weather_snapshot(): WxData is around 1.8KB and this
    // is called from inside screen updates that already hold a copy on the
    // stack. Reading the one field we need keeps the render path shallow.
    if (s_mutex == nullptr) return UINT32_MAX;

    time_t fetched = 0;
    bool valid = false;
    if (xSemaphoreTake(s_mutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        fetched = s_data.fetched_at;
        valid = s_data.valid;
        xSemaphoreGive(s_mutex);
    }
    if (!valid || fetched == 0) return UINT32_MAX;

    const time_t now = time(nullptr);
    if (now < fetched) return 0;
    return uint32_t(now - fetched);
}

// ---------------------------------------------------------------------------
// WMO 4677 weather codes. Open-Meteo emits the subset below.
// ---------------------------------------------------------------------------
WxIcon wx_icon_for(int16_t code, bool is_day) {
    switch (code) {
        case 0:  return is_day ? WxIcon::ClearDay : WxIcon::ClearNight;
        case 1:
        case 2:  return is_day ? WxIcon::PartlyDay : WxIcon::PartlyNight;
        case 3:  return WxIcon::Overcast;
        case 45:
        case 48: return WxIcon::Fog;
        case 51:
        case 53:
        case 55: return WxIcon::Drizzle;
        case 56:
        case 57:
        case 66:
        case 67: return WxIcon::FreezingRain;
        case 61:
        case 63:
        case 80:
        case 81: return WxIcon::Rain;
        case 65:
        case 82: return WxIcon::HeavyRain;
        case 71:
        case 73:
        case 77:
        case 85: return WxIcon::Snow;
        case 75:
        case 86: return WxIcon::HeavySnow;
        case 95: return WxIcon::Thunder;
        case 96:
        case 99: return WxIcon::ThunderHail;
        default: return WxIcon::Unknown;
    }
}

const char *wx_condition_text(int16_t code) {
    switch (code) {
        case 0:  return "Clear";
        case 1:  return "Mainly Clear";
        case 2:  return "Partly Cloudy";
        case 3:  return "Overcast";
        case 45: return "Fog";
        case 48: return "Freezing Fog";
        case 51: return "Light Drizzle";
        case 53: return "Drizzle";
        case 55: return "Heavy Drizzle";
        case 56:
        case 57: return "Freezing Drizzle";
        case 61: return "Light Rain";
        case 63: return "Rain";
        case 65: return "Heavy Rain";
        case 66:
        case 67: return "Freezing Rain";
        case 71: return "Light Snow";
        case 73: return "Snow";
        case 75: return "Heavy Snow";
        case 77: return "Snow Grains";
        case 80: return "Light Showers";
        case 81: return "Showers";
        case 82: return "Violent Showers";
        case 85: return "Snow Showers";
        case 86: return "Heavy Snow Showers";
        case 95: return "Thunderstorm";
        case 96: return "Thunderstorm, Hail";
        case 99: return "Severe Thunderstorm";
        default: return "--";
    }
}

const char *wx_cardinal(int16_t degrees) {
    static const char *kPoints[] = {"N", "NNE", "NE", "ENE", "E", "ESE", "SE", "SSE",
                                    "S", "SSW", "SW", "WSW", "W", "WNW", "NW", "NNW"};
    int idx = int((degrees + 11.25f) / 22.5f) % 16;
    if (idx < 0) idx += 16;
    return kPoints[idx];
}
