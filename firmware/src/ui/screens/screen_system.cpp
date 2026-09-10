#include "screen_system.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>

#include "config.h"
#include "display/backlight.h"
#include "input/touch.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "ui/format.h"
#include "ui/icons.h"
#include "ui/theme.h"

// Bumped by hand. Shown on this screen so a photo of the panel is enough to
// tell which build is running on it.
#define FIRMWARE_VERSION "1.0.0"

namespace {

struct Field {
    lv_obj_t *value;
};

lv_obj_t *s_wifi_icon = nullptr;
lv_obj_t *s_touch_dot = nullptr;
Field s_fields[10] = {};
bool  s_confirm_forget = false;
lv_obj_t *s_forget_btn = nullptr;
lv_obj_t *s_forget_lbl = nullptr;
uint32_t s_confirm_at = 0;

// Four columns of label/value pairs across the strip, two rows deep, in the
// same Micro-over-Body pattern the overlays use.
void field(lv_obj_t *parent, int slot, const char *label) {
    const int col_w = (UI_WIDTH - LAYOUT_SAFE * 2) / 4;
    const int x = LAYOUT_SAFE + (slot % 4) * col_w;
    const int y = 44 + (slot / 4) * 46;

    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, label);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);

    lv_obj_t *v = theme_label(parent, &font_label, COL_ALUMINUM, "--");
    lv_obj_set_width(v, col_w - 8);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_pos(v, x, y + 15);
    s_fields[slot].value = v;
}

void set_field(int slot, const char *fmt, ...) {
    if (s_fields[slot].value == nullptr) return;
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(s_fields[slot].value, buf);
}

// Forgetting Wi-Fi reboots the panel into its setup portal, so it asks twice.
// One stray tap on a wall-mounted screen should not take the trailer's weather
// offline until somebody finds their phone.
void forget_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (ui_gesture_recent()) return;
    if (!s_confirm_forget) {
        s_confirm_forget = true;
        s_confirm_at = millis();
        lv_label_set_text(s_forget_lbl, "TAP AGAIN TO CONFIRM");
        lv_obj_set_style_bg_color(s_forget_btn, lv_color_hex(COL_SUNSET), 0);
        lv_obj_set_style_text_color(s_forget_lbl, lv_color_hex(COL_GROUND), 0);
        return;
    }
    net_forget_and_restart();
}

lv_obj_t *create(lv_obj_t *parent) {
    s_confirm_forget = false;

    lv_obj_t *t = theme_label(parent, &font_title, COL_ALUMINUM, "System");
    lv_obj_set_pos(t, LAYOUT_SAFE, LAYOUT_SAFE - 4);

    s_wifi_icon = theme_label(parent, &icons_sm, COL_ALUMINUM_DIM, ICON_WIFI_OFF);
    lv_obj_set_pos(t, LAYOUT_SAFE, LAYOUT_SAFE - 4);
    lv_obj_set_pos(s_wifi_icon, LAYOUT_SAFE + 92, LAYOUT_SAFE);

    // A small dot that lights turquoise while the digitiser reports a contact.
    // This is the ten-second answer to "is touch working, and are my
    // TOUCH_INVERT_* flags right" - press each corner and watch the readout.
    s_touch_dot = lv_obj_create(parent);
    lv_obj_remove_style_all(s_touch_dot);
    lv_obj_set_size(s_touch_dot, 8, 8);
    lv_obj_set_style_radius(s_touch_dot, 4, 0);
    lv_obj_set_style_bg_opa(s_touch_dot, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_touch_dot, lv_color_hex(COL_RIVET), 0);
    lv_obj_set_pos(s_touch_dot, LAYOUT_SAFE + 124, LAYOUT_SAFE + 6);

    lv_obj_t *rule = lv_obj_create(parent);
    lv_obj_remove_style_all(rule);
    lv_obj_add_style(rule, &style_hairline, 0);
    lv_obj_set_size(rule, UI_WIDTH - LAYOUT_SAFE * 2, 1);
    lv_obj_set_pos(rule, LAYOUT_SAFE, LAYOUT_SAFE + 26);

    field(parent, 0, "NETWORK");
    field(parent, 1, "IP ADDRESS");
    field(parent, 2, "UPDATE HOST");
    field(parent, 3, "LOCATION");
    field(parent, 4, "TOUCH / RAW XY");
    field(parent, 5, "BRIGHTNESS");
    field(parent, 6, "MEMORY FREE");
    field(parent, 7, "UPTIME / BUILD");

    s_forget_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(s_forget_btn);
    lv_obj_set_size(s_forget_btn, 168, 22);
    lv_obj_align(s_forget_btn, LV_ALIGN_TOP_RIGHT, -LAYOUT_SAFE, LAYOUT_SAFE - 2);
    lv_obj_set_style_radius(s_forget_btn, 11, 0);
    lv_obj_set_style_bg_opa(s_forget_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_forget_btn, lv_color_hex(COL_SURFACE), 0);
    lv_obj_add_event_cb(s_forget_btn, forget_cb, LV_EVENT_CLICKED, nullptr);

    s_forget_lbl = theme_label(s_forget_btn, &font_micro, COL_ALUMINUM_DIM,
                               "CHANGE WI-FI NETWORK");
    lv_obj_set_style_text_letter_space(s_forget_lbl, 1, 0);
    lv_obj_center(s_forget_lbl);

    return parent;
}

void update(lv_obj_t *root) {
    LV_UNUSED(root);

    // Let a half-pressed confirmation lapse rather than sitting armed forever.
    if (s_confirm_forget && millis() - s_confirm_at > 6000) {
        s_confirm_forget = false;
        lv_label_set_text(s_forget_lbl, "CHANGE WI-FI NETWORK");
        lv_obj_set_style_bg_color(s_forget_btn, lv_color_hex(COL_SURFACE), 0);
        lv_obj_set_style_text_color(s_forget_lbl, lv_color_hex(COL_ALUMINUM_DIM), 0);
    }

    const bool up = net_connected();
    lv_label_set_text(s_wifi_icon, up ? ICON_WIFI : ICON_WIFI_OFF);
    lv_obj_set_style_text_color(
        s_wifi_icon, lv_color_hex(up ? COL_TURQUOISE : COL_SUNSET), 0);

    if (up) {
        set_field(0, "%s  %d dBm", net_ssid().c_str(), net_rssi());
    } else if (net_state() == NetState::Portal) {
        set_field(0, "AP: %s", net_ap_name());
    } else {
        set_field(0, "%s", net_state_text());
    }

    set_field(1, "%s", net_ip().c_str());
    set_field(2, "%s.local", OTA_HOSTNAME);

    WxData d;
    weather_snapshot(d);
    if (d.valid) {
        set_field(3, "%s  %.2f, %.2f",
                  d.location[0] ? d.location : "?", d.latitude, d.longitude);
    } else {
        set_field(3, "%s", weather_status_text());
    }

    const TouchPoint p = touch_last();
    set_field(4, "%s", touch_chip_name());
    lv_obj_set_style_bg_color(
        s_touch_dot, lv_color_hex(p.pressed ? COL_TURQUOISE : COL_RIVET), 0);
    if (p.pressed) {
        set_field(4, "%u, %u", unsigned(p.x), unsigned(p.y));
    }

    const char *mode = "AUTO";
    switch (backlight_mode()) {
        case BacklightMode::Manual: mode = "MANUAL"; break;
        case BacklightMode::Off:    mode = "OFF"; break;
        default: break;
    }
    set_field(5, "%s  %d%%", mode,
              int(lroundf(backlight_current_level() * 100.0f / 255.0f)));

    set_field(6, "%uK / %uK PSRAM",
              unsigned(ESP.getFreeHeap() / 1024),
              unsigned(ESP.getFreePsram() / 1024));

    const uint32_t up_s = millis() / 1000;
    set_field(7, "%uh %um  ·  v" FIRMWARE_VERSION,
              unsigned(up_s / 3600), unsigned((up_s % 3600) / 60));
}

const ScreenDef kDef = {
    "System",
    ICON_SETTINGS,
    create,
    update,
    -1,                                            // a drawer to the left of home
    UI_SWIPE_LEFT | UI_SWIPE_RIGHT | UI_SWIPE_DOWN,
};

}  // namespace

const ScreenDef &screen_system_def() { return kDef; }
