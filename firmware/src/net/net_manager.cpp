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

// ---------------------------------------------------------------------------
// Known networks
//
// A separate Preferences namespace from "airstream" prefs above, so wiping
// location/units and wiping saved networks stay independent operations. Keys
// are "n" (count) and "s0".."s7" / "p0".."p7" (ssid/password per slot) rather
// than one JSON blob - plain strings are trivial to upsert one at a time and
// there is no reason to pull ArduinoJson into a path this small.
// ---------------------------------------------------------------------------
struct KnownNet {
    String ssid;
    String pass;
};
KnownNet s_known[WIFI_MAX_KNOWN_NETWORKS];
uint8_t  s_known_count = 0;

enum class RetryPhase : uint8_t { Idle, Scanning };
RetryPhase s_retry_phase = RetryPhase::Idle;
uint32_t   s_retry_last_attempt_ms = 0;

void load_known_networks() {
    Preferences p;
    p.begin("wifinets", false);   // read-write: see load_prefs()'s comment on nvs_open
    const uint8_t n = p.isKey("n") ? p.getUChar("n") : 0;
    s_known_count = 0;
    for (uint8_t i = 0; i < n && i < WIFI_MAX_KNOWN_NETWORKS; i++) {
        char key[4];
        snprintf(key, sizeof(key), "s%u", i);
        const String ssid = p.getString(key, "");
        if (ssid.length() == 0) continue;
        snprintf(key, sizeof(key), "p%u", i);
        s_known[s_known_count].ssid = ssid;
        s_known[s_known_count].pass = p.getString(key, "");
        s_known_count++;
    }
    p.end();
    if (s_known_count > 0) {
        Serial.printf("[net] %u known network(s) loaded\n", unsigned(s_known_count));
    }
}

void save_known_networks() {
    Preferences p;
    p.begin("wifinets", false);
    p.putUChar("n", s_known_count);
    for (uint8_t i = 0; i < s_known_count; i++) {
        char key[4];
        snprintf(key, sizeof(key), "s%u", i);
        p.putString(key, s_known[i].ssid);
        snprintf(key, sizeof(key), "p%u", i);
        p.putString(key, s_known[i].pass);
    }
    p.end();
}

void clear_known_networks() {
    Preferences p;
    p.begin("wifinets", false);
    p.clear();
    p.end();
    s_known_count = 0;
}

// Adds a network, or updates its password if the SSID is already known
// (covers a network's password changing without the panel forgetting the
// others). Returns true if this SSID is new to the list.
bool upsert_known_network(const String &ssid, const String &pass) {
    if (ssid.length() == 0) return false;
    for (uint8_t i = 0; i < s_known_count; i++) {
        if (s_known[i].ssid == ssid) {
            const bool changed = s_known[i].pass != pass;
            s_known[i].pass = pass;
            if (changed) save_known_networks();
            return false;
        }
    }
    if (s_known_count >= WIFI_MAX_KNOWN_NETWORKS) {
        Serial.printf("[net] known-network list is full (%u); not remembering \"%s\" - "
                      "forget one via CHANGE WI-FI NETWORK first\n",
                      unsigned(WIFI_MAX_KNOWN_NETWORKS), ssid.c_str());
        return false;
    }
    s_known[s_known_count].ssid = ssid;
    s_known[s_known_count].pass = pass;
    s_known_count++;
    save_known_networks();
    return true;
}

// Synchronous scan-and-join used once at boot: picks the strongest visible AP
// that matches a known SSID and connects to it, blocking up to timeout_ms.
// Boot has nothing else to do yet, so a blocking scan here costs nothing that
// WiFiManager's own autoConnect() wasn't already going to cost.
bool connect_to_strongest_known(uint32_t timeout_ms) {
    if (s_known_count == 0) return false;

    const int n = WiFi.scanNetworks();
    int8_t  best = -1;
    int32_t best_rssi = -1000;
    for (int i = 0; i < n; i++) {
        const String ssid = WiFi.SSID(i);
        for (uint8_t k = 0; k < s_known_count; k++) {
            if (ssid == s_known[k].ssid && WiFi.RSSI(i) > best_rssi) {
                best_rssi = WiFi.RSSI(i);
                best = int8_t(k);
            }
        }
    }
    WiFi.scanDelete();
    if (best < 0) {
        Serial.println("[net] no known network in range");
        return false;
    }

    Serial.printf("[net] joining strongest known network: %s (%d dBm)\n",
                  s_known[best].ssid.c_str(), int(best_rssi));
    WiFi.begin(s_known[best].ssid.c_str(), s_known[best].pass.c_str());
    const uint32_t start = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - start < timeout_ms) delay(50);
    return WiFi.status() == WL_CONNECTED;
}

// Non-blocking version of the same join, driven from net_tick() while the
// panel is otherwise idle-disconnected. One scan-and-join attempt at a time,
// spaced WIFI_RETRY_INTERVAL_MS apart, never blocking the render loop.
void retry_known_networks_tick() {
    switch (s_retry_phase) {
    case RetryPhase::Idle:
        if (millis() - s_retry_last_attempt_ms < WIFI_RETRY_INTERVAL_MS) return;
        s_retry_last_attempt_ms = millis();
        WiFi.scanNetworks(/*async=*/true);
        s_retry_phase = RetryPhase::Scanning;
        return;

    case RetryPhase::Scanning: {
        const int n = WiFi.scanComplete();
        if (n == WIFI_SCAN_RUNNING) return;   // still scanning, check again next tick

        if (n > 0) {
            int8_t  best = -1;
            int32_t best_rssi = -1000;
            for (int i = 0; i < n; i++) {
                const String ssid = WiFi.SSID(i);
                for (uint8_t k = 0; k < s_known_count; k++) {
                    if (ssid == s_known[k].ssid && WiFi.RSSI(i) > best_rssi) {
                        best_rssi = WiFi.RSSI(i);
                        best = int8_t(k);
                    }
                }
            }
            if (best >= 0) {
                Serial.printf("[net] retry: joining %s (%d dBm)\n",
                              s_known[best].ssid.c_str(), int(best_rssi));
                WiFi.begin(s_known[best].ssid.c_str(), s_known[best].pass.c_str());
            }
        }
        WiFi.scanDelete();
        s_retry_phase = RetryPhase::Idle;
        return;
    }
    }
}

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
    // isKey() first: the getters log an error-level line for a missing key
    // even though they return the default, so a clean first boot would print
    // three of them right where someone is looking for a real problem.
    s_lat      = s_prefs.isKey("lat")      ? s_prefs.getFloat("lat")     : WX_DEFAULT_LAT;
    s_lon      = s_prefs.isKey("lon")      ? s_prefs.getFloat("lon")     : WX_DEFAULT_LON;
    s_imperial = s_prefs.isKey("imperial") ? s_prefs.getBool("imperial") : WX_DEFAULT_UNITS_IMPERIAL;
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

// Fires only when the portal (either flow: first-time setup or ADD NETWORK)
// just saved credentials and connected with them - never on an ordinary boot
// reconnecting to a network already known. That is exactly "a network worth
// remembering", so upsert it.
void save_wifi_callback() {
    const String ssid = s_wm.getWiFiSSID();
    const String pass = s_wm.getWiFiPass();
    if (upsert_known_network(ssid, pass)) {
        Serial.printf("[net] remembered new network: %s (%u known now)\n",
                      ssid.c_str(), unsigned(s_known_count));
    }
}

}  // namespace

void net_begin() {
    load_prefs();
    load_known_networks();

    WiFi.mode(WIFI_STA);

    s_wm.addParameter(&s_p_hint);
    s_wm.addParameter(&s_p_lat);
    s_wm.addParameter(&s_p_lon);
    s_wm.addParameter(&s_p_units);
    s_wm.setSaveParamsCallback(save_params_callback);
    s_wm.setSaveConfigCallback(save_wifi_callback);

    // Non-blocking, so the UI keeps running and can show setup instructions on
    // the panel itself while the portal waits.
    s_wm.setConfigPortalBlocking(false);
    s_wm.setConfigPortalTimeout(SETUP_PORTAL_TIMEOUT_S);
    s_wm.setDarkMode(true);
    s_wm.setTitle("Airstream Weather");
    s_wm.setConnectTimeout(20);

    s_state = NetState::Connecting;

    // Try every known network first and join whichever is strongest, rather
    // than WiFiManager's own single last-saved credential. Falls back to
    // WiFiManager's autoConnect (covering an empty list - first boot ever, or
    // a unit upgraded from firmware that only remembered one network - and
    // then the portal) if nothing known is in range.
    bool connected = s_known_count > 0 && connect_to_strongest_known(15000);
    if (!connected) connected = s_wm.autoConnect(WIFI_AP_NAME);

    if (connected) {
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
    if (s_portal_started) {
        s_wm.process();
        if (!s_wm.getConfigPortalActive()) s_portal_started = false;
    }

    const bool up = WiFi.status() == WL_CONNECTED;
    if (up) {
        if (s_state != NetState::Connected) {
            s_state = NetState::Connected;
            Serial.printf("[net] connected: %s  %s\n",
                          WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        }
        s_retry_phase = RetryPhase::Idle;
        start_ota();
        ArduinoOTA.handle();
        return;
    }

    if (s_state == NetState::Connected) {
        s_state = NetState::Connecting;
        Serial.println("[net] connection lost; will keep trying known networks");
    }

    // Keep retrying the known-network list in the background for as long as
    // there is anything in it, rather than dropping straight to the setup
    // portal on a transient signal loss. The portal from net_begin() (first
    // boot with nothing known) or net_add_network_portal() can be running at
    // the same time; s_wm.process() above already serviced it this tick.
    if (s_known_count > 0 && s_state != NetState::Booting) {
        retry_known_networks_tick();
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
    clear_known_networks();
    s_wm.resetSettings();
    delay(250);
    ESP.restart();
}

void net_add_network_portal() {
    if (s_wm.getConfigPortalActive()) return;   // already up; one at a time
    // WiFiManager keeps STA connected alongside the portal's AP when already
    // online (it only tears STA down first if there is nothing to preserve),
    // so this does not interrupt the forecast while a second network is added.
    s_wm.setConfigPortalTimeout(WIFI_ADD_NETWORK_TIMEOUT_S);
    s_wm.startConfigPortal(WIFI_AP_NAME);
    s_portal_started = true;
    Serial.println("[net] ADD NETWORK: portal open");
}

uint8_t net_known_network_count() { return s_known_count; }
bool    net_portal_active()       { return s_wm.getConfigPortalActive(); }

bool net_time_valid() {
    // Anything before 2020-09 means SNTP has not answered yet.
    return time(nullptr) > 1600000000;
}
