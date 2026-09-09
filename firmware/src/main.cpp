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
#include "ui/screens/screen_system.h"
#include "ui/screens/screen_today.h"
#include "ui/theme.h"

namespace {

lv_disp_draw_buf_t s_draw_buf;
lv_disp_drv_t      s_disp_drv;
lv_indev_drv_t     s_indev_drv;

void flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *pixels) {
    const uint32_t w = area->x2 - area->x1 + 1;
    const uint32_t h = area->y2 - area->y1 + 1;
    panel_push_pixels(area->x1, area->y1, w, h, reinterpret_cast<uint16_t *>(pixels));
    lv_disp_flush_ready(drv);
}

void touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data) {
    LV_UNUSED(drv);
    const TouchPoint p = touch_read();

    // Coordinates are reported in the panel's own 180x640 space. LVGL applies
    // the same rotation to input that it applies to the framebuffer, so they
    // must NOT be pre-rotated here - doing so lands every tap 90 degrees away
    // from where it was made.
    data->state = p.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    if (p.pressed) {
        data->point.x = p.x;
        data->point.y = p.y;
        // Presence for the auto-dimmer is taken here rather than from a widget
        // event, because this is the only place that sees every contact -
        // including taps on dead space and drags that never become clicks.
        backlight_note_activity();
    }
}

void init_lvgl_display() {
    lv_init();

    // Two full-screen buffers in PSRAM. LVGL's software rotation needs either
    // full_refresh or a second full-size buffer; we give it both, which also
    // buys clean double-buffering.
    const size_t px = size_t(PANEL_WIDTH) * PANEL_HEIGHT;
    auto *buf_a = static_cast<lv_color_t *>(heap_caps_malloc(px * sizeof(lv_color_t),
                                                            MALLOC_CAP_SPIRAM));
    auto *buf_b = static_cast<lv_color_t *>(heap_caps_malloc(px * sizeof(lv_color_t),
                                                            MALLOC_CAP_SPIRAM));
    if (buf_a == nullptr || buf_b == nullptr) {
        // Without PSRAM there is nowhere to put 450KB of framebuffer, and
        // continuing would fault somewhere far less obvious than here.
        Serial.println("[fatal] could not allocate framebuffers - is PSRAM enabled?");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf_a, buf_b, px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res      = PANEL_WIDTH;    // the physical panel, not the UI
    s_disp_drv.ver_res      = PANEL_HEIGHT;
    s_disp_drv.flush_cb     = flush_cb;
    s_disp_drv.draw_buf     = &s_draw_buf;
    s_disp_drv.sw_rotate    = 1;
    s_disp_drv.rotated      = UI_ROTATION;
    s_disp_drv.full_refresh = 1;              // required alongside sw_rotate
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

    panel_init();
    backlight_init();       // comes up dark and fades in with the first frame
    touch_init();

    init_lvgl_display();
    theme_init();
    overlays_init();

    // Screen order is swipe order. Adding a third screen is one more line here
    // plus its own file - see docs/ARCHITECTURE.md.
    screens_register(screen_today_def());
    screens_register(screen_system_def());
    screens_begin();

    // Draw the first frame before going near the network, so the panel is
    // showing its setup message by the time anyone has looked at it.
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

    net_tick();
    buttons_tick();
    backlight_tick();

    // A fresh forecast refreshes every screen, not just the visible one, so a
    // screen swiped to a moment later is already current.
    if (weather_consume_update_flag()) screens_update_all();

    // Once a second is enough for the clock, the "x min ago" line and the live
    // values in the quick-settings sheet. The screens themselves are static
    // between these ticks, so LVGL has nothing to redraw in between.
    const uint32_t now = millis();
    if (now - s_last_second >= 1000) {
        s_last_second = now;
        screens_update_active();
        overlays_tick();
    }

    lv_timer_handler();
    delay(2);
}
