#include "net_manager.h"

#include <ArduinoOTA.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <time.h>

#include "config.h"
#include "net/weather.h"

namespace {

WiFiManager s_wm;
Preferences s_prefs;
NetState    s_state = NetState::Booting;
bool        s_ota_started = false;
bool        s_portal_started = false;

char s_lat_buf[16]   = "";
char s_lon_buf[16]   = "";
char s_units_buf[4]  = "F";

WiFiManagerParameter s_p_hint(
    "<p style='margin-top:1em'><b>Location</b><br>"
    "Leave blank and the panel will find itself from your IP address, and keep "
    "doing so every time you move. Fill these in to pin it to one place.</p>");
WiFiManagerParameter s_p_lat("lat", "Latitude (optional)", "", 15);
WiFiManagerParameter s_p_lon("lon", "Longitude (optional)", "", 15);
WiFiManagerParameter s_p_units("units", "Units: F or C", "F", 3);

float s_lat = WX_DEFAULT_LAT;
float s_lon = WX_DEFAULT_LON;
bool  s_imperial = WX_DEFAULT_UNITS_IMPERIAL;

void load_prefs() {
    // Read-write rather than read-only, even though this only reads. Opening a
    // namespace read-only before it exists makes nvs_open fail, and the Arduino
    // Preferences wrapper logs that at error level - so a completely healthy
    // first boot prints "nvs_open failed: NOT_FOUND", which is alarming and
    // means nothing. Opening read-write creates the namespace instead.
    s_prefs.begin("airstream", false);
    s_lat      = s_prefs.getFloat("lat", WX_DEFAULT_LAT);
    s_lon      = s_prefs.getFloat("lon", WX_DEFAULT_LON);
    s_imperial = s_prefs.getBool("imperial", WX_DEFAULT_UNITS_IMPERIAL);
    s_prefs.end();

    if (s_lat != 0.0f) snprintf(s_lat_buf, sizeof(s_lat_buf), "%.4f", s_lat);
    if (s_lon != 0.0f) snprintf(s_lon_buf, sizeof(s_lon_buf), "%.4f", s_lon);
    s_units_buf[0] = s_imperial ? 'F' : 'C';
    s_units_buf[1] = '\0';
}

void save_params_callback() {
    const char *lat = s_p_lat.getValue();
    const char *lon = s_p_lon.getValue();
    const char *units = s_p_units.getValue();

    // Blank means "keep finding me by IP", which is not the same as 0,0 in the
    // Gulf of Guinea, so only overwrite when something was actually typed.
    if (lat != nullptr && *lat != '\0') s_lat = atof(lat);
    if (lon != nullptr && *lon != '\0') s_lon = atof(lon);
    if (units != nullptr && (*units == 'c' || *units == 'C')) {
        s_imperial = false;
    } else if (units != nullptr && (*units == 'f' || *units == 'F')) {
        s_imperial = true;
    }

    s_prefs.begin("airstream", false);
    s_prefs.putFloat("lat", s_lat);
    s_prefs.putFloat("lon", s_lon);
    s_prefs.putBool("imperial", s_imperial);
    s_prefs.end();

    weather_set_location(s_lat, s_lon);
    weather_set_units(s_imperial);

    Serial.printf("[net] saved prefs: %.4f, %.4f, %s\n",
                  s_lat, s_lon, s_imperial ? "imperial" : "metric");
}

void start_ota() {
    if (s_ota_started) return;
    ArduinoOTA.setHostname(OTA_HOSTNAME);
    ArduinoOTA.begin();
    s_ota_started = true;
    Serial.printf("[net] OTA ready at %s.local\n", OTA_HOSTNAME);
}

}  // namespace

void net_begin() {
    load_prefs();

    WiFi.mode(WIFI_STA);

    s_wm.addParameter(&s_p_hint);
    s_wm.addParameter(&s_p_lat);
    s_wm.addParameter(&s_p_lon);
    s_wm.addParameter(&s_p_units);
    s_wm.setSaveParamsCallback(save_params_callback);

    // Non-blocking, so the UI keeps running and can show setup instructions on
    // the panel itself while the portal waits.
    s_wm.setConfigPortalBlocking(false);
    s_wm.setConfigPortalTimeout(SETUP_PORTAL_TIMEOUT_S);
    s_wm.setDarkMode(true);
    s_wm.setTitle("Airstream Weather");
    s_wm.setConnectTimeout(20);

    s_state = NetState::Connecting;

    if (s_wm.autoConnect(WIFI_AP_NAME)) {
        s_state = NetState::Connected;
    } else {
        s_portal_started = true;
        s_state = NetState::Portal;
    }

    // Keep the RTC in UTC and apply the location's offset where it is needed.
    // Open-Meteo already returns a DST-correct utc_offset_seconds for wherever
    // the trailer is, which is a great deal simpler than carrying a tz database
    // and re-deriving the rule ourselves.
    configTime(0, 0, NTP_SERVER_1, NTP_SERVER_2);

    weather_set_location(s_lat, s_lon);
    weather_set_units(s_imperial);
}

void net_tick() {
    if (s_portal_started) s_wm.process();

    const bool up = WiFi.status() == WL_CONNECTED;
    if (up) {
        if (s_state != NetState::Connected) {
            s_state = NetState::Connected;
            Serial.printf("[net] connected: %s  %s\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        }
        start_ota();
        ArduinoOTA.handle();
    } else if (s_state == NetState::Connected) {
        s_state = NetState::Connecting;
    }
}

NetState net_state() { return s_state; }

const char *net_state_text() {
    switch (s_state) {
        case NetState::Booting:    return "STARTING UP";
        case NetState::Connecting: return "CONNECTING TO WI-FI";
        case NetState::Portal:     return "SETUP NEEDED";
        case NetState::Connected:  return "CONNECTED";
        case NetState::Failed:     return "WI-FI FAILED";
    }
    return "";
}

bool   net_connected() { return WiFi.status() == WL_CONNECTED; }
String net_ssid()      { return net_connected() ? WiFi.SSID() : String(""); }
String net_ip()        { return net_connected() ? WiFi.localIP().toString() : String("--"); }
int    net_rssi()      { return net_connected() ? WiFi.RSSI() : -127; }

uint8_t net_signal_bars() {
    if (!net_connected()) return 0;
    const int rssi = WiFi.RSSI();
    if (rssi >= -55) return 4;
    if (rssi >= -66) return 3;
    if (rssi >= -77) return 2;
    if (rssi >= -88) return 1;
    return 0;
}

const char *net_ap_name() { return WIFI_AP_NAME; }

float net_pref_latitude()  { return s_lat; }
float net_pref_longitude() { return s_lon; }
bool  net_pref_imperial()  { return s_imperial; }

void net_forget_and_restart() {
    s_wm.resetSettings();
    delay(250);
    ESP.restart();
}

bool net_time_valid() {
    // Anything before 2020-09 means SNTP has not answered yet.
    return time(nullptr) > 1600000000;
}
