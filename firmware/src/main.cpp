// Airstream Weather Panel - LilyGO T-Display-S3-Long
//
// A 640x180 wall panel showing current conditions and the next ten hours,
// designed to be glanced at from across a trailer and interrogated by touch
// when you want more.
//
// Boot order matters here. The panel and backlight come up first so there is
// something to look at within a few hundred milliseconds; the UI is built and
// drawn before the network is touched at all, so the first thing anyone sees is
// the panel telling them what it is doing rather than a black rectangle.
//
// Threading: LVGL and all rendering live on core 1 (Arduino's loop). The
// weather fetch - DNS, TLS, HTTP, JSON - runs on core 0 and hands over a
// snapshot behind a mutex. Nothing on the render path ever blocks on the
// network.

#include <Arduino.h>
#include <esp_heap_caps.h>
#include <lvgl.h>

#include "config.h"
#include "display/backlight.h"
#include "display/panel.h"
#include "input/buttons.h"
#include "input/touch.h"
#include "net/net_manager.h"
#include "net/weather.h"
#include "ui/overlays.h"
#include "ui/screen_manager.h"
#include "ui/screens/screen_forecast.h"
#include "ui/screens/screen_system.h"
#include "ui/screens/screen_today.h"
#include "ui/startup.h"
#include "ui/theme.h"

namespace {

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t      s_disp_drv;
lv_indev_drv_t     s_indev_drv;

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    // full_refresh is set, so every flush is the whole UI. Logged once so a
    // black screen can be separated into "LVGL never flushed" and "flushes
    // land and the panel stays dark" from the console alone.
    static bool s_logged = false;
    if (!s_logged) {
        s_logged = true;
        Serial.printf("[flush] first frame (%d,%d)-(%d,%d)\n",
                      area->x1, area->y1, area->x2, area->y2);
    }
    panel_push_frame(reinterpret_cast<uint16_t *>(pixels));
    lv_disp_flush_ready(drv);
}

void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    LV_UNUSED(drv);
    const TouchPoint p = touch_read();

    // The digitiser reports in the panel's own 180x640 space; LVGL sees the
    // UI unrotated at 640x180, so the same turn the driver applies to pixels
    // is applied here to the touch, in reverse. Same two mappings as
    // panel_push_frame(), inverted.
    data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    static bool s_was_pressed = false;
    if (p.pressed && !s_was_pressed) ui_note_press_start();
    s_was_pressed = p.pressed;
    if (p.pressed) {
        if (UI_ROTATION == LV_DISP_ROT_270) {
            data->point.x = p.y;
            data->point.y = (PANEL_WIDTH - 1) - p.x;
        } else {
            data->point.x = (PANEL_HEIGHT - 1) - p.y;
            data->point.y = p.x;
        }
        // Presence for the auto-dimmer is taken here rather than from a widget
        // event, because this is the only place that sees every contact -
        // including taps on dead space and drags that never become clicks.
        backlight_note_activity();
    }
}

void init_lvgl_display() {
    lv_init();

    // One full-size, unrotated 640x180 frame in PSRAM, and full_refresh on.
    //
    // LVGL renders the UI the way it is designed - wide and short - and the
    // panel driver turns each finished frame into the glass's 180x640 space
    // on the way out (panel_push_frame). LVGL's own software rotation is
    // deliberately not used: it splits every redraw into narrow column bands
    // with arbitrary partial windows, and on this glass those came out
    // shifted and torn. The driver's path writes one full-screen window per
    // frame, which is exactly what the boot self-test does and the one thing
    // proven to look right. The price is a whole-frame redraw on any change
    // (~25ms to rotate and stream), well inside the 33ms refresh period.
    const size_t px    = size_t(UI_WIDTH) * UI_HEIGHT;
    const size_t bytes = px * sizeof(lv_color_t);
    auto *buf = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    if (buf == nullptr) {
        Serial.println("[fatal] could not allocate the frame buffer in PSRAM");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf, nullptr, px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res      = UI_WIDTH;    // the UI, not the panel
    s_disp_drv.ver_res      = UI_HEIGHT;
    s_disp_drv.flush_cb     = flush_cb;
    s_disp_drv.draw_buf     = &s_draw_buf;
    s_disp_drv.full_refresh = 1;
    s_disp_drv.sw_rotate    = 0;
    s_disp_drv.rotated      = LV_DISP_ROT_NONE;
    lv_disp_drv_register(&s_disp_drv);

    lv_indev_drv_init(&s_indev_drv);
    s_indev_drv.type    = LV_INDEV_TYPE_POINTER;
    s_indev_drv.read_cb = touch_read_cb;

    // LVGL 8.4 hardcodes these in lv_hal_indev.h rather than reading them from
    // lv_conf.h, so they have to be set on the driver. Both defaults are wrong
    // for this panel: 50px of travel is most of a 180px-tall screen, and 400ms
    // fires a long press during an ordinary deliberate tap.
    s_indev_drv.gesture_limit        = GESTURE_MIN_DISTANCE;
    s_indev_drv.gesture_min_velocity = 3;
    s_indev_drv.long_press_time      = 700;

    lv_indev_drv_register(&s_indev_drv);
}

}  // namespace

void setup() {
    Serial.begin(115200);

    // Never let logging stall the panel. With ARDUINO_USB_CDC_ON_BOOT the
    // Serial writes block until a host drains them, up to a 100ms timeout
    // each. On a bulkhead with nothing plugged in that is invisible - until
    // something starts logging every frame, at which point the main loop is
    // starved and the UI, the button and the Wi-Fi portal all go unresponsive
    // while the device looks powered and fine. A zero timeout drops the bytes
    // instead, which is the right trade for a device that spends its life with
    // no USB host attached.
    Serial.setTxTimeoutMs(0);

    panel_init();
    backlight_init();       // comes up dark and fades in with the first frame

#if PANEL_BOOT_PROBE
    backlight_set_immediate(255);
    panel_boot_probe();
#endif

#if PANEL_BOOT_SELF_TEST
    // Drive the panel directly, before LVGL exists. See config.h.
    backlight_set_immediate(255);
    panel_fill_split(panel_rgb565(0x3F, 0xBF, 0xB0),   // COL_TURQUOISE, rows 0-319
                     panel_rgb565(0xE2, 0x70, 0x3A),   // COL_SUNSET,    rows 320-639
                     PANEL_HEIGHT / 2);
    Serial.printf("[panel] self-test: turquoise/orange split, backlight full, "
                  "holding %dms\n", PANEL_BOOT_SELF_TEST_MS);
    delay(PANEL_BOOT_SELF_TEST_MS);
#endif

    touch_init();

    init_lvgl_display();
    theme_init();
    overlays_init();

    // Screen order is swipe order. Adding a third screen is one more line here
    // plus its own file - see docs/ARCHITECTURE.md.
    screens_register(screen_today_def());
    screens_register(screen_forecast_def());
    screens_register(screen_system_def());
    screens_begin();

    // The boot screen goes over the strip until the first forecast lands, or
    // the setup screen takes over. Draw its first frame before going near the
    // network, so the panel is saying what it is doing by the time anyone
    // has looked at it.
    startup_show();
    lv_timer_handler();

    buttons_init();
    buttons_on_short(backlight_cycle_step);
    buttons_on_double(backlight_set_auto);
    buttons_on_long(backlight_toggle_off);

    weather_begin();        // creates the mutex the fetch task needs
    net_begin();            // may raise the setup portal; never blocks

    Serial.printf("[boot] ready - %uKB heap, %uKB PSRAM free\n",
                  unsigned(ESP.getFreeHeap() / 1024),
                  unsigned(ESP.getFreePsram() / 1024));
}

void loop() {
    static uint32_t s_last_second = 0;
    static uint32_t s_last_heartbeat = 0;
    static bool     s_forecast_seen = false;

    net_tick();
    buttons_tick();
    backlight_tick();
    startup_tick();

    // A fresh forecast refreshes every screen, not just the visible one, so a
    // screen swiped to a moment later is already current.
    if (weather_consume_update_flag()) {
        s_forecast_seen = true;
        screens_update_all();
    }

    // Once a second is enough for the clock, the "x min ago" line and the live
    // values in the quick-settings sheet. The screens themselves are static
    // between these ticks, so LVGL has nothing to redraw in between.
    const uint32_t now = millis();
    if (now - s_last_second >= 1000) {
        s_last_second = now;
        screens_update_active();
        overlays_tick();
    }

    // A heartbeat every five seconds. Its presence proves the loop is running
    // - a panel that goes quiet after "[boot] ready" is otherwise
    // indistinguishable from one hung in an SPI transaction - and its fields
    // are the ones that matter for a dark screen: is the backlight up, and is
    // LVGL flushing.
    // Every 5s until the first forecast lands - that is when someone is
    // reading the console - and once a minute after, which is enough to
    // prove the loop is alive without burying everything else.
    if (now - s_last_heartbeat >= (s_forecast_seen ? 60000u : 5000u)) {
        s_last_heartbeat = now;
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        // stack= is the loop task's high-water mark in bytes: the least room
        // it has ever had. WxData is copied onto this stack by every
        // weather_snapshot(), so this is the number to watch when it grows.
        Serial.printf("[loop] up=%lus bl=%u/%u flushes=%lu heap=%uK lvmem=%u%% stack=%u\n",
                      static_cast<unsigned long>(now / 1000),
                      unsigned(backlight_current_level()),
                      unsigned(backlight_target_level()),
                      static_cast<unsigned long>(panel_flush_count()),
                      unsigned(ESP.getFreeHeap() / 1024),
                      unsigned(mon.used_pct),
                      unsigned(uxTaskGetStackHighWaterMark(nullptr)));
    }

    lv_timer_handler();
    delay(2);
}
