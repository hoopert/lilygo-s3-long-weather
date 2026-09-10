// Wi-Fi provisioning, clock, and over-the-air updates.
//
// Provisioning is a captive portal rather than credentials compiled into the
// firmware, which is what lets the install instructions be "flash this binary,
// join a Wi-Fi network from your phone" with no toolchain and no editing of
// source. The portal runs non-blocking so the panel keeps rendering - and keeps
// telling you what it is waiting for - while you are still typing on your phone.
//
// OTA matters more than usual here: once this is screwed to a bulkhead you do
// not want to unmount it to change a font.
#pragma once

#include <Arduino.h>
#include <stdint.h>

enum class NetState : uint8_t {
    Booting,
    Connecting,
    Portal,       // captive portal up, waiting to be configured
    Connected,
    Failed,
};

void net_begin();
void net_tick();

NetState net_state();
const char *net_state_text();

bool     net_connected();
String   net_ssid();
String   net_ip();
int      net_rssi();          // dBm
uint8_t  net_signal_bars();   // 0-4, for the UI
const char *net_ap_name();

// Stored preferences, set from the portal.
float net_pref_latitude();
float net_pref_longitude();
bool  net_pref_imperial();

// Forget every saved network - the known-network list and WiFiManager's own
// last credentials - and reboot into the setup portal.
void net_forget_and_restart();

// Opens the setup portal for one more network *without* forgetting any that
// are already saved, and without dropping the current connection if there is
// one (WiFiManager keeps STA up alongside the portal's AP). Auto-closes after
// WIFI_ADD_NETWORK_TIMEOUT_S if nobody finishes it.
void net_add_network_portal();

// How many networks are remembered, and whether the add/setup portal's AP is
// reachable right now - for the System screen's status line and button label.
uint8_t net_known_network_count();
bool    net_portal_active();

// True once SNTP has produced a plausible wall clock.
bool net_time_valid();
