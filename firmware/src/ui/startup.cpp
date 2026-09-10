#include "startup.h"

#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#include "config.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "ui/icons.h"
#include "ui/screen_manager.h"
#include "ui/theme.h"

namespace {

// --- boot (§6) ---------------------------------------------------------------
constexpr int kRivetY0    = 20;
constexpr int kRivetY1    = 158;
constexpr int kArcX       = 176;
constexpr int kArcY       = 62;
constexpr int kArcSize    = 56;
constexpr int kArcPeriodMs = 1600;
constexpr int kTextX      = 252;
constexpr int kEyebrowY   = 62;
constexpr int kStatusY    = 80;
constexpr int kQueueY     = 108;
constexpr int kStatusFadeMs = 400;

// --- setup (§7) --------------------------------------------------------------
constexpr int kStepsX     = 10;
constexpr int kStepsY     = 78;
constexpr int kStepPitch  = 29;
constexpr int kChipX      = 178;
constexpr int kChipY      = 68;
constexpr int kChipH      = 46;
constexpr int kPasswordLabelY = 120;
constexpr int kPasswordY  = 134;
constexpr int kQrX        = 521;
constexpr int kQrY        = 54;
constexpr int kQrModules  = 87;    // 29 modules x 3px
constexpr int kQrQuiet    = 6;
constexpr int kQrCaptionY = 28;

enum class Layout : uint8_t { Boot, Setup };

struct Ui {
    lv_obj_t *screen;
    lv_obj_t *boot;         // layer
    lv_obj_t *setup;        // layer
    lv_obj_t *arc;
    lv_obj_t *status[2];    // two labels so a change can cross-fade
    lv_obj_t *queue;
    lv_obj_t *password;
    lv_obj_t *qr;
    int       status_front; // which of status[] is showing
    char      status_text[32];
    Layout    layout;
    bool      done;
};

Ui s_ui = {};

lv_obj_t *make_layer(lv_obj_t *parent) {
    lv_obj_t *l = theme_decor(parent);
    lv_obj_set_pos(l, 0, 0);
    lv_obj_set_size(l, UI_WIDTH, UI_HEIGHT);
    return l;
}

void arc_spin_cb(void *o, int32_t v) {
    lv_arc_set_rotation(static_cast<lv_obj_t *>(o), uint16_t(v));
}

void opa_cb(void *o, int32_t v) {
    lv_obj_set_style_opa(static_cast<lv_obj_t *>(o), lv_opa_t(v), 0);
}

void fade(lv_obj_t *obj, lv_opa_t from, lv_opa_t to, uint32_t ms) {
    lv_anim_del(obj, opa_cb);
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, obj);
    lv_anim_set_exec_cb(&a, opa_cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_time(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_in_out);
    lv_anim_start(&a);
}

lv_obj_t *micro(lv_obj_t *parent, int x, int y, const char *text, uint32_t color) {
    lv_obj_t *l = theme_label(parent, &font_micro, color, text);
    lv_obj_set_style_text_letter_space(l, 1, 0);
    lv_obj_set_pos(l, x, y);
    return l;
}

void build_boot(lv_obj_t *parent) {
    theme_rivet_row(parent, 16, UI_WIDTH - 16, kRivetY0);
    theme_rivet_row(parent, 16, UI_WIDTH - 16, kRivetY1);

    // A quarter-turn of turquoise going round a rivet track, once every
    // 1.6 seconds, linear: a rate, not a progress bar, because nothing here
    // knows how long the router will take.
    lv_obj_t *arc = lv_arc_create(parent);
    lv_obj_set_size(arc, kArcSize, kArcSize);
    lv_obj_set_pos(arc, kArcX, kArcY);
    lv_arc_set_bg_angles(arc, 0, 360);
    lv_arc_set_angles(arc, 0, 90);
    lv_arc_set_rotation(arc, 0);
    lv_obj_remove_style(arc, nullptr, LV_PART_KNOB);
    lv_obj_clear_flag(arc, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_RIVET), LV_PART_MAIN);
    lv_obj_set_style_arc_width(arc, 3, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(arc, lv_color_hex(COL_TURQUOISE), LV_PART_INDICATOR);
    lv_obj_set_style_arc_rounded(arc, true, LV_PART_INDICATOR);
    s_ui.arc = arc;

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, arc);
    lv_anim_set_exec_cb(&a, arc_spin_cb);
    lv_anim_set_values(&a, 0, 360);
    lv_anim_set_time(&a, kArcPeriodMs);
    lv_anim_set_path_cb(&a, lv_anim_path_linear);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    micro(parent, kTextX, kEyebrowY, "AIRSTREAM WEATHER", COL_ALUMINUM_DIM);
    for (int i = 0; i < 2; i++) {
        lv_obj_t *l = theme_label(parent, &font_body, COL_ALUMINUM, "");
        lv_obj_set_pos(l, kTextX, kStatusY);
        lv_obj_set_style_opa(l, i == 0 ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
        s_ui.status[i] = l;
    }
    s_ui.queue = micro(parent, kTextX, kQueueY, "", COL_NIGHT_DIM);
}

void build_setup(lv_obj_t *parent) {
    micro(parent, LAYOUT_SAFE, 16, "GET STARTED", COL_ALUMINUM_DIM);
    lv_obj_t *instr = theme_label(parent, &font_body, COL_ALUMINUM,
                                  "Join this network from another device:");
    lv_obj_set_pos(instr, LAYOUT_SAFE, 34);

    const char *steps[3] = {"1   JOIN THE NETWORK", "2   PICK YOUR WI-FI", "3   DONE"};
    for (int i = 0; i < 3; i++) {
        micro(parent, kStepsX, kStepsY + i * kStepPitch, steps[i], COL_ALUMINUM_DIM);
    }

    // The network identity, in the visual middle where the eye lands.
    lv_obj_t *chip = lv_obj_create(parent);
    lv_obj_remove_style_all(chip);
    lv_obj_set_pos(chip, kChipX, kChipY);
    lv_obj_set_size(chip, LV_SIZE_CONTENT, kChipH);
    lv_obj_set_style_radius(chip, kChipH / 2, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(COL_SURFACE), 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(COL_TURQUOISE), 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_pad_hor(chip, 18, 0);
    lv_obj_set_style_pad_ver(chip, 0, 0);
    lv_obj_set_style_pad_column(chip, 8, 0);
    lv_obj_set_flex_flow(chip, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(chip, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(chip, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_SCROLLABLE);
    theme_label(chip, &icons_sm, COL_TURQUOISE, ICON_WIFI);
    theme_label(chip, &font_title, COL_TURQUOISE, net_ap_name());

    micro(parent, kChipX, kPasswordLabelY, "PASSWORD", COL_ALUMINUM_DIM);
    s_ui.password = theme_label(parent, &font_title, COL_OAT, "");
    lv_obj_set_style_text_line_space(s_ui.password, 0, 0);
    lv_obj_set_pos(s_ui.password, kChipX, kPasswordY);

    // The join code. Dark modules on an aluminum field: scanners need a light
    // ground, so this patch inverts the palette on purpose.
    lv_obj_t *cap = micro(parent, kQrX, kQrCaptionY, "SCAN TO JOIN", COL_ALUMINUM_DIM);
    lv_obj_set_width(cap, kQrModules + kQrQuiet * 2);
    lv_obj_set_style_text_align(cap, LV_TEXT_ALIGN_CENTER, 0);

    s_ui.qr = lv_qrcode_create(parent, kQrModules, lv_color_hex(COL_GROUND),
                               lv_color_hex(COL_ALUMINUM));
    lv_obj_set_pos(s_ui.qr, kQrX, kQrY);
    lv_obj_set_style_pad_all(s_ui.qr, kQrQuiet, 0);
    lv_obj_set_style_bg_color(s_ui.qr, lv_color_hex(COL_ALUMINUM), 0);
    lv_obj_set_style_bg_opa(s_ui.qr, LV_OPA_COVER, 0);
}

void fill_setup() {
    const char *pw = net_ap_password();
    char grouped[16];
    if (strlen(pw) == 8) {
        snprintf(grouped, sizeof(grouped), "%.4s %.4s", pw, pw + 4);
    } else {
        snprintf(grouped, sizeof(grouped), "%s", pw);
    }
    lv_label_set_text(s_ui.password, grouped);

    // The standard Wi-Fi URI; iOS and Android both join straight from the
    // camera. The name and an 8-digit password have nothing to escape.
    char uri[96];
    snprintf(uri, sizeof(uri), "WIFI:T:WPA;S:%s;P:%s;;", net_ap_name(), pw);
    lv_qrcode_update(s_ui.qr, uri, uint32_t(strlen(uri)));
}

void set_status(const char *text, const char *queue) {
    if (strcmp(text, s_ui.status_text) != 0) {
        snprintf(s_ui.status_text, sizeof(s_ui.status_text), "%s", text);
        lv_obj_t *out = s_ui.status[s_ui.status_front];
        lv_obj_t *in  = s_ui.status[1 - s_ui.status_front];
        lv_label_set_text(in, text);
        fade(out, LV_OPA_COVER, LV_OPA_TRANSP, kStatusFadeMs);
        fade(in, LV_OPA_TRANSP, LV_OPA_COVER, kStatusFadeMs);
        s_ui.status_front = 1 - s_ui.status_front;
    }
    lv_label_set_text(s_ui.queue, queue);
}

void set_layout(Layout l) {
    if (l == s_ui.layout) return;
    s_ui.layout = l;
    lv_obj_t *show = (l == Layout::Boot) ? s_ui.boot : s_ui.setup;
    lv_obj_t *hide = (l == Layout::Boot) ? s_ui.setup : s_ui.boot;
    if (l == Layout::Setup) fill_setup();
    lv_obj_clear_flag(show, LV_OBJ_FLAG_HIDDEN);
    fade(show, LV_OPA_TRANSP, LV_OPA_COVER, kStatusFadeMs);
    fade(hide, LV_OPA_COVER, LV_OPA_TRANSP, kStatusFadeMs);
    // The hidden layer is hidden for real once its fade is over; until then
    // it is simply transparent.
    lv_anim_t *a = lv_anim_get(hide, opa_cb);
    if (a) lv_anim_set_ready_cb(a, [](lv_anim_t *an) {
        lv_obj_add_flag(static_cast<lv_obj_t *>(an->var), LV_OBJ_FLAG_HIDDEN);
    });
}

}  // namespace

void startup_show() {
    s_ui = {};
    s_ui.layout = Layout::Boot;

    lv_obj_t *scr = lv_obj_create(nullptr);
    lv_obj_add_style(scr, &style_screen, 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.screen = scr;

    s_ui.boot = make_layer(scr);
    build_boot(s_ui.boot);

    s_ui.setup = make_layer(scr);
    build_setup(s_ui.setup);
    lv_obj_add_flag(s_ui.setup, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(s_ui.setup, LV_OPA_TRANSP, 0);

    set_status("CONNECTING TO WI-FI", "THEN FINDING LOCATION  ·  FETCHING FORECAST");
    lv_scr_load(scr);
}

bool startup_active() { return s_ui.screen != nullptr && !s_ui.done; }

void startup_tick() {
    if (!startup_active()) return;

    WxData d;
    weather_snapshot(d);
    if (d.valid) {
        // Hand off. The strip's home screen fades in over this one, and this
        // screen is deleted once it has gone.
        s_ui.done = true;
        screens_reveal(kStatusFadeMs);
        s_ui.screen = nullptr;
        return;
    }

    if (net_state() == NetState::Portal) {
        set_layout(Layout::Setup);
        return;
    }
    set_layout(Layout::Boot);

    if (!net_connected()) {
        set_status("CONNECTING TO WI-FI", "THEN FINDING LOCATION  ·  FETCHING FORECAST");
        return;
    }
    switch (weather_status()) {
        case WxStatus::Idle:
        case WxStatus::Locating:
            set_status("FINDING LOCATION", "THEN FETCHING FORECAST");
            break;
        case WxStatus::Fetching:
        case WxStatus::Ok:
            set_status("FETCHING FORECAST", "");
            break;
        case WxStatus::ErrorNetwork:
        case WxStatus::ErrorParse:
            set_status("FETCHING FORECAST", "NO ANSWER YET  ·  TRYING AGAIN");
            break;
    }
}
