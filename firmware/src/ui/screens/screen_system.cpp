#include "screen_system.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_heap_caps.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "input/touch.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "ui/format.h"
#include "ui/icons.h"
#include "ui/theme.h"

// Injected by tools/version.py from `git describe` at build time - "1.1.0"
// on a tagged release, "1.1.0-3-gabc1234" three commits past one - so a
// photo of the panel says exactly which build is on it. The fallback is for
// a build outside PlatformIO.
#ifndef FIRMWARE_VERSION
#define FIRMWARE_VERSION "unknown"
#endif

// The System drawer: a title bar carrying the one action and the signal
// bars, a rivet-dotted rule, and a three-column grid of facts. Memory is a
// pair of bars with a colour, because a number of kilobytes tells nobody
// whether the panel is healthy.

namespace {

constexpr int kTitleY     = 6;
constexpr int kRuleY      = 40;
constexpr int kRivetY     = 44;
constexpr int kGridX[3]   = {10, 220, 430};
constexpr int kColW       = 200;
constexpr int kRow1Y      = 56;
constexpr int kRow2Y      = 104;
constexpr int kValueDY    = 18;     // label top -> value top (Micro + 6px)
constexpr int kFooterY    = 148;
constexpr int kButtonW    = 168;
constexpr int kButtonH    = 24;
constexpr int kBarsW      = 22;     // the signal bars, right of the button
constexpr int kMemBarW    = 150;
constexpr int kMemBarH    = 8;

const uint8_t kBarHeights[4] = {5, 8, 11, 14};

enum Cell { CELL_NETWORK, CELL_IP, CELL_LOCATION, CELL_HOST, CELL_TOUCH, CELL_COUNT };

struct MemBar {
    lv_obj_t *label;    // "HEAP  149K FREE"
    lv_obj_t *track;
    lv_obj_t *fill;
};

struct Ui {
    lv_obj_t *values[CELL_COUNT];
    lv_obj_t *bars;         // signal bars, in the title bar
    lv_obj_t *coords;       // Micro, after the town
    MemBar    heap;
    MemBar    psram;
    lv_obj_t *footer;
    lv_obj_t *forget_btn;
    lv_obj_t *forget_lbl;
    bool      confirm_forget;
    uint32_t  confirm_at;
};

Ui s_ui = {};

// A Micro label over a value. Human values take Body 20; machine strings
// (hostnames, chip identifiers) take Label 15 so they fit in 200px.
lv_obj_t *cell(lv_obj_t *parent, int col, int row_y, const char *label,
               const lv_font_t *value_font) {
    const int x = kGridX[col];
    lv_obj_t *l = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, label);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, row_y);

    lv_obj_t *v = theme_label(parent, value_font, COL_ALUMINUM, "--");
    lv_obj_set_width(v, kColW - 6);
    lv_label_set_long_mode(v, LV_LABEL_LONG_CLIP);
    lv_obj_set_pos(v, x, row_y + kValueDY);
    return v;
}

void set_value(Cell c, const char *fmt, ...) {
    if (s_ui.values[c] == nullptr) return;
    char buf[64];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    lv_label_set_text(s_ui.values[c], buf);
}

// A memory bar: the fill is what is in use; the colour says whether what is
// left is comfortable (turquoise), getting tight (oat) or a problem (sunset).
MemBar mem_bar(lv_obj_t *parent, int x, int y) {
    MemBar m;
    m.label = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, "");
    lv_obj_set_style_text_letter_space(m.label, 1, 0);
    lv_obj_set_pos(m.label, x, y);

    m.track = theme_decor(parent);
    lv_obj_set_pos(m.track, x, y + 15);
    lv_obj_set_size(m.track, kMemBarW, kMemBarH);
    lv_obj_set_style_radius(m.track, kMemBarH / 2, 0);
    lv_obj_set_style_bg_opa(m.track, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m.track, lv_color_hex(COL_RIVET), 0);

    m.fill = theme_decor(m.track);
    lv_obj_set_pos(m.fill, 0, 0);
    lv_obj_set_size(m.fill, kMemBarH, kMemBarH);
    lv_obj_set_style_radius(m.fill, kMemBarH / 2, 0);
    lv_obj_set_style_bg_opa(m.fill, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(m.fill, lv_color_hex(COL_TURQUOISE), 0);
    return m;
}

void mem_bar_set(MemBar &m, const char *name, size_t free_bytes, size_t total_bytes,
                 size_t tight_below, size_t problem_below) {
    if (m.label == nullptr || total_bytes == 0) return;
    char buf[32];
    if (free_bytes >= 1024 * 1024) {
        snprintf(buf, sizeof(buf), "%s  %.1fM FREE", name, free_bytes / (1024.0f * 1024.0f));
    } else {
        snprintf(buf, sizeof(buf), "%s  %uK FREE", name, unsigned(free_bytes / 1024));
    }
    lv_label_set_text(m.label, buf);

    const float used = 1.0f - float(free_bytes) / float(total_bytes);
    int w = int(lroundf(used * kMemBarW));
    if (w < kMemBarH) w = kMemBarH;
    if (w > kMemBarW) w = kMemBarW;
    lv_obj_set_width(m.fill, w);

    uint32_t color = COL_TURQUOISE;
    if (free_bytes < problem_below) color = COL_SUNSET;
    else if (free_bytes < tight_below) color = COL_OAT;
    lv_obj_set_style_bg_color(m.fill, lv_color_hex(color), 0);
}

void reset_forget_pill() {
    s_ui.confirm_forget = false;
    lv_label_set_text(s_ui.forget_lbl, "FORGET NETWORK");
    lv_obj_set_style_bg_color(s_ui.forget_btn, lv_color_hex(COL_SURFACE_HI), 0);
    lv_obj_set_style_text_color(s_ui.forget_lbl, lv_color_hex(COL_ALUMINUM_DIM), 0);
}

// Forgetting Wi-Fi reboots the panel into its setup portal, so it asks twice.
// One stray tap on a wall-mounted screen should not take the trailer's weather
// offline until somebody finds their phone.
void forget_cb(lv_event_t *e) {
    LV_UNUSED(e);
    if (ui_gesture_recent()) return;
    if (!s_ui.confirm_forget) {
        s_ui.confirm_forget = true;
        s_ui.confirm_at = millis();
        lv_label_set_text(s_ui.forget_lbl, "TAP AGAIN TO CONFIRM");
        lv_obj_set_style_bg_color(s_ui.forget_btn, lv_color_hex(COL_SUNSET), 0);
        lv_obj_set_style_text_color(s_ui.forget_lbl, lv_color_hex(COL_GROUND), 0);
        return;
    }
    net_forget_and_restart();
}

lv_obj_t *create(lv_obj_t *parent) {
    s_ui = {};

    // --- title bar ------------------------------------------------------------
    lv_obj_t *t = theme_label(parent, &font_title, COL_ALUMINUM, "System");
    lv_obj_set_pos(t, LAYOUT_SAFE, kTitleY);

    // The one action, with the signal bars to its right at the edge. A 24px
    // pill with its touch target grown to 44px so it is as easy to hit as it
    // is quiet to look at.
    const int bars_x = UI_WIDTH - LAYOUT_SAFE - kBarsW;
    s_ui.forget_btn = lv_btn_create(parent);
    lv_obj_remove_style_all(s_ui.forget_btn);
    lv_obj_set_size(s_ui.forget_btn, kButtonW, kButtonH);
    lv_obj_set_pos(s_ui.forget_btn, bars_x - 12 - kButtonW, 8);
    lv_obj_set_style_radius(s_ui.forget_btn, kButtonH / 2, 0);
    lv_obj_set_style_bg_opa(s_ui.forget_btn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(s_ui.forget_btn, lv_color_hex(COL_SURFACE_HI), 0);
    lv_obj_set_ext_click_area(s_ui.forget_btn, 10);
    theme_press_feedback(s_ui.forget_btn);
    lv_obj_add_event_cb(s_ui.forget_btn, forget_cb, LV_EVENT_CLICKED, nullptr);

    s_ui.forget_lbl = theme_label(s_ui.forget_btn, &font_micro, COL_ALUMINUM_DIM,
                                  "FORGET NETWORK");
    lv_obj_set_style_text_letter_space(s_ui.forget_lbl, 1, 0);
    lv_obj_center(s_ui.forget_lbl);

    s_ui.bars = theme_signal_bars(parent, bars_x, 8 + (kButtonH - kBarHeights[3]) / 2, kBarHeights);

    lv_obj_t *rule = theme_decor(parent);
    lv_obj_add_style(rule, &style_hairline, 0);
    lv_obj_set_size(rule, UI_WIDTH - LAYOUT_SAFE * 2, 1);
    lv_obj_set_pos(rule, LAYOUT_SAFE, kRuleY);
    theme_rivet_row(parent, 16, UI_WIDTH - 16, kRivetY);

    // --- grid: three columns, no dividers --------------------------------------
    s_ui.values[CELL_NETWORK]  = cell(parent, 0, kRow1Y, "NETWORK", &font_body);
    s_ui.values[CELL_IP]       = cell(parent, 1, kRow1Y, "IP ADDRESS", &font_body);
    s_ui.values[CELL_LOCATION] = cell(parent, 2, kRow1Y, "LOCATION", &font_body);
    s_ui.values[CELL_HOST]     = cell(parent, 0, kRow2Y, "UPDATE HOST", &font_label);
    s_ui.values[CELL_TOUCH]    = cell(parent, 1, kRow2Y, "TOUCH CONTROLLER", &font_label);

    s_ui.coords = theme_label(parent, &font_micro, COL_ALUMINUM_DIM, "");

    // Memory: two bars in the last cell of the second row.
    s_ui.heap  = mem_bar(parent, kGridX[2], kRow2Y);
    s_ui.psram = mem_bar(parent, kGridX[2], kRow2Y + 24);

    // --- footer ---------------------------------------------------------------
    s_ui.footer = theme_label(parent, &font_micro, COL_NIGHT_DIM, "");
    lv_obj_set_style_text_letter_space(s_ui.footer, 1, 0);
    lv_obj_set_pos(s_ui.footer, LAYOUT_SAFE, kFooterY);

    set_value(CELL_HOST, "%s.local", OTA_HOSTNAME);
    return parent;
}

// "Sep  9 2026" -> "2026-09-09". The compiler's date is the build date the
// footer wants, and a bulkhead-mounted panel is identified by photograph.
void build_date(char *out, size_t len) {
    static const char kMonths[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char *d = __DATE__;
    int month = 0;
    for (int i = 0; i < 12; i++) {
        if (strncmp(d, kMonths + i * 3, 3) == 0) { month = i + 1; break; }
    }
    const int day = atoi(d + 4);
    const int year = atoi(d + 7);
    snprintf(out, len, "%04d-%02d-%02d", year, month, day);
}

void update(lv_obj_t *root) {
    LV_UNUSED(root);

    // Let a half-pressed confirmation lapse rather than sitting armed forever.
    if (s_ui.confirm_forget && millis() - s_ui.confirm_at > 6000) reset_forget_pill();

    char buf[64];

    // --- row 1 ----------------------------------------------------------------
    const bool up = net_connected();
    if (up) {
        set_value(CELL_NETWORK, "%s", net_ssid().c_str());
    } else if (net_state() == NetState::Portal) {
        set_value(CELL_NETWORK, "AP: %s", net_ap_name());
    } else {
        set_value(CELL_NETWORK, "%s", net_state_text());
    }
    theme_signal_bars_set(s_ui.bars, net_signal_bars());
    set_value(CELL_IP, "%s", net_ip().c_str());

    WxData d;
    weather_snapshot(d);
    if (d.valid) {
        set_value(CELL_LOCATION, "%s", d.location[0] ? d.location : "?");
        snprintf(buf, sizeof(buf), "%.2f, %.2f", d.latitude, d.longitude);
    } else {
        set_value(CELL_LOCATION, "%s", weather_status_text());
        buf[0] = '\0';
    }
    // The coordinates sit on the town's baseline; the town's width changes
    // with the town, so re-anchor after every update.
    lv_label_set_text(s_ui.coords, buf);
    lv_obj_update_layout(s_ui.values[CELL_LOCATION]);
    const int town_w = lv_txt_get_width(lv_label_get_text(s_ui.values[CELL_LOCATION]), 0xFFFF,
                                        &font_body, 0, LV_TEXT_FLAG_NONE);
    lv_obj_set_pos(s_ui.coords, kGridX[2] + (town_w < kColW - 90 ? town_w : kColW - 90) + 8,
                   kRow1Y + kValueDY + 8);

    // --- row 2 ----------------------------------------------------------------
    // While a finger is down, the touch cell shows the raw digitiser reading
    // instead of the chip: press each corner and watch it move. That is the
    // ten-second answer to "is touch working, and is the rotation right".
    const TouchPoint p = touch_last();
    if (p.pressed) {
        set_value(CELL_TOUCH, "%u, %u", unsigned(p.x), unsigned(p.y));
        lv_obj_set_style_text_color(s_ui.values[CELL_TOUCH], lv_color_hex(COL_TURQUOISE), 0);
    } else {
        set_value(CELL_TOUCH, "%s", touch_chip_name());
        lv_obj_set_style_text_color(s_ui.values[CELL_TOUCH], lv_color_hex(COL_ALUMINUM), 0);
    }

    // Internal heap: a TLS fetch needs ~50K of it; under 40K a fetch can fail.
    mem_bar_set(s_ui.heap, "HEAP", ESP.getFreeHeap(), ESP.getHeapSize(), 80 * 1024, 40 * 1024);
    // PSRAM: the frame buffer and the forecast live here; 8MB, and nothing
    // here should ever get near the end of it.
    mem_bar_set(s_ui.psram, "PSRAM", ESP.getFreePsram(), ESP.getPsramSize(),
                2 * 1024 * 1024, 512 * 1024);

    // --- footer ---------------------------------------------------------------
    char date[16];
    build_date(date, sizeof(date));
    const uint32_t up_s = millis() / 1000;
    snprintf(buf, sizeof(buf), "UP %uH %02uM  ·  v" FIRMWARE_VERSION "  ·  BUILD %s",
             unsigned(up_s / 3600), unsigned((up_s % 3600) / 60), date);
    lv_label_set_text(s_ui.footer, buf);
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
