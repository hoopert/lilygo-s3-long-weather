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

    // The first few flushes are logged so a black screen can be diagnosed
    // from the console alone: if these never appear, LVGL is not flushing;
    // if they appear and the panel stays dark, the fault is below this line.
    static uint8_t s_logged = 0;
    if (s_logged < 3) {
        s_logged++;
        Serial.printf("[flush] #%u area (%d,%d)-(%d,%d) %ux%u px=%u\n",
                      unsigned(s_logged), area->x1, area->y1, area->x2, area->y2,
                      unsigned(w), unsigned(h), unsigned(w * h));
    }

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

    // Partial draw buffers, a tenth of the screen each, in internal SRAM.
    //
    // NOT full-screen, and NOT full_refresh - see the disp_drv setup below for
    // why that combination cannot work. Partial buffers are also the better fit
    // for this UI: most updates are one label (the clock, a temperature), so
    // redrawing a small dirty rectangle beats repainting 640x180 every second.
    //
    // Internal SRAM rather than PSRAM because LVGL renders into these buffers
    // pixel by pixel, and internal memory is roughly an order of magnitude
    // faster for that. At 23KB each they fit comfortably; the full-screen
    // buffers this replaced did not, which is why they were in PSRAM.
    const size_t px = (size_t(PANEL_WIDTH) * PANEL_HEIGHT) / 10;
    const size_t bytes = px * sizeof(lv_color_t);

    // MALLOC_CAP_DMA as well as INTERNAL: the SPI driver DMAs straight out of
    // these, and asking for DMA-capable memory explicitly is cheaper than
    // finding out at runtime that it was not.
    const uint32_t caps = MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL;
    auto *buf_a = static_cast<lv_color_t *>(heap_caps_malloc(bytes, caps));
    auto *buf_b = static_cast<lv_color_t *>(heap_caps_malloc(bytes, caps));
    if (buf_a == nullptr || buf_b == nullptr) {
        // Fall back to PSRAM rather than refusing to boot: slower, but a
        // working panel beats a dead one.
        Serial.println("[warn] draw buffers fell back to PSRAM");
        heap_caps_free(buf_a);
        heap_caps_free(buf_b);
        buf_a = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
        buf_b = static_cast<lv_color_t *>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM));
    }
    if (buf_a == nullptr || buf_b == nullptr) {
        Serial.println("[fatal] could not allocate draw buffers");
        while (true) delay(1000);
    }
    lv_disp_draw_buf_init(&s_draw_buf, buf_a, buf_b, px);

    lv_disp_drv_init(&s_disp_drv);
    s_disp_drv.hor_res  = PANEL_WIDTH;    // the physical panel, not the UI
    s_disp_drv.ver_res  = PANEL_HEIGHT;
    s_disp_drv.flush_cb = flush_cb;
    s_disp_drv.draw_buf = &s_draw_buf;
    s_disp_drv.sw_rotate = 1;
    s_disp_drv.rotated   = UI_ROTATION;

    // full_refresh MUST stay 0 here. draw_buf_rotate() in lv_refr.c opens with
    //
    //     if(disp_refr->driver->full_refresh && drv->sw_rotate) {
    //         LV_LOG_ERROR("cannot rotate a full refreshed display!");
    //         return;
    //     }
    //
    // and that return happens before any flush, so the panel never receives a
    // single pixel - a permanently black screen with one error line on the
    // serial console. LilyGO's factory example does set both, which is where
    // this came from, but it ships a patched LVGL; that is what its "if you
    // turn on software rotation, do not update or replace LVGL" comment means.
    // Against stock LVGL the two are mutually exclusive.
    s_disp_drv.full_refresh = 0;

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
    static uint32_t s_last_heartbeat = 0;

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

    // A heartbeat every five seconds. Its presence proves the loop is running
    // - a panel that goes quiet after "[boot] ready" is otherwise
    // indistinguishable from one hung in an SPI transaction - and its fields
    // are the ones that matter for a dark screen: is the backlight up, and is
    // LVGL flushing.
    if (now - s_last_heartbeat >= 5000) {
        s_last_heartbeat = now;
        lv_mem_monitor_t mon;
        lv_mem_monitor(&mon);
        Serial.printf("[loop] up=%lus bl=%u/%u flushes=%lu heap=%uK lvmem=%u%%\n",
                      static_cast<unsigned long>(now / 1000),
                      unsigned(backlight_current_level()),
                      unsigned(backlight_target_level()),
                      static_cast<unsigned long>(panel_flush_count()),
                      unsigned(ESP.getFreeHeap() / 1024),
                      unsigned(mon.used_pct));
    }

    lv_timer_handler();
    delay(2);
}
