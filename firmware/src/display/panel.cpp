#include "panel.h"

#include <Arduino.h>
#include <string.h>
#include "driver/spi_master.h"
#include <esp_heap_caps.h>

#include <lvgl.h>

#include "config.h"
#include "pins.h"
#include "panel_init_tables.h"

namespace {

spi_device_handle_t s_spi = nullptr;        // writes
spi_device_handle_t s_spi_slow = nullptr;   // 4MHz, register reads only
uint32_t s_flush_count = 0;
bool s_bus_up = false;

// One chunk of pixels in DMA-capable internal SRAM: 14400 px = 80 full rows
// of the 180-wide panel, so a frame is exactly eight of them. The rotation in
// panel_push_frame() gathers into this, and the SPI driver DMAs out of it.
uint16_t *s_chunk = nullptr;

// How pixel data goes over the wire once the address window is set.
enum class WriteMode : uint8_t {
    // What the shipped factory binary does: CS held low for the whole frame,
    // first chunk as QSPI opcode 0x32 + 0x002C00, every later chunk as raw
    // pixel data with no opcode or address at all.
    kQuadHeld,
    // Vendor's alternate (#else) path and Arduino_GFX: CS toggled per chunk,
    // later chunks as 0x32 + 0x003C00 (memory write continue).
    kQuadContinue,
    // Single data line: opcode 0x02, same addresses, data on D0 only.
    kSingle,
};

struct PanelConfig {
    const char    *name;
    uint8_t        spi_mode;
    uint32_t       hz;
    const LcdCmd  *init;
    size_t         init_len;
    WriteMode      write;
};

#define TABLE(t) t, (sizeof(t) / sizeof(t[0]))

// The configuration the panel runs on. Chosen by the boot probe, not by
// reading vendor code: the factory-exact configuration (short init table,
// held-CS QSPI writes) left the glass black in every variant that used that
// init table, and lit it in every variant that did not. The write path was
// never the problem.
const PanelConfig kResting = {"DCS init, held-CS QSPI writes", 0, LCD_SPI_FREQUENCY, TABLE(kInitDcs), WriteMode::kQuadHeld};
PanelConfig s_cfg = kResting;

inline void cs_low()  { digitalWrite(PIN_LCD_CS, LOW); }
inline void cs_high() { digitalWrite(PIN_LCD_CS, HIGH); }

void reset_pulse() {
    // The generous delays are the vendor's; the panel's internal regulator
    // needs the settling time and shortening them produces an intermittently
    // blank display that looks like a wiring fault.
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(130);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(130);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(300);
}

// (Re)creates the two device handles for the given mode and clock. The bus
// itself is initialised once.
void bus_apply(uint8_t mode, uint32_t hz) {
    if (!s_bus_up) {
        spi_bus_config_t buscfg = {};
        buscfg.data0_io_num    = PIN_LCD_D0;
        buscfg.data1_io_num    = PIN_LCD_D1;
        buscfg.sclk_io_num     = PIN_LCD_SCK;
        buscfg.data2_io_num    = PIN_LCD_D2;
        buscfg.data3_io_num    = PIN_LCD_D3;
        buscfg.max_transfer_sz = (LCD_SEND_BUF_PIXELS * 16) + 8;
        buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;
        ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
        s_bus_up = true;
    }
    if (s_spi)      { spi_bus_remove_device(s_spi);      s_spi = nullptr; }
    if (s_spi_slow) { spi_bus_remove_device(s_spi_slow); s_spi_slow = nullptr; }

    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits   = 8;
    devcfg.address_bits   = 24;
    devcfg.mode           = mode;
    devcfg.clock_speed_hz = hz;
    devcfg.spics_io_num   = -1;              // CS is driven by hand
    devcfg.flags          = SPI_DEVICE_HALFDUPLEX;
    devcfg.queue_size     = 17;
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_spi));

    // Slow enough that the driver needs no compensation dummy cycles on
    // reads (NO_DUMMY makes it refuse the device otherwise).
    spi_device_interface_config_t slowcfg = devcfg;
    slowcfg.clock_speed_hz = 4000000;
    slowcfg.flags          = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    slowcfg.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &slowcfg, &s_spi_slow));
}

// Single command + parameters, sent on one data line (QSPI opcode 0x02: the
// controller takes 0x02, then a 24-bit "address" of 00 <cmd> 00, then the
// parameter bytes).
esp_err_t send_cmd(uint8_t cmd, const uint8_t *data, uint32_t len) {
    cs_low();

    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd   = 0x02;
    t.addr  = static_cast<uint32_t>(cmd) << 8;
    if (len != 0) {
        t.tx_buffer = data;
        t.length    = 8 * len;
    }
    const esp_err_t err = spi_device_polling_transmit(s_spi, &t);

    cs_high();
    return err;
}

void run_init(const LcdCmd *table, size_t n) {
    for (size_t i = 0; i < n; i++) {
        const esp_err_t err = send_cmd(table[i].cmd, table[i].data, table[i].len);
        if (err != ESP_OK) Serial.printf("[panel] init cmd 0x%02X refused by SPI driver: %d\n", table[i].cmd, err);
        if (table[i].delay_ms) delay(table[i].delay_ms);
    }
}

// Register read (QSPI opcode 0x03, same 00 <cmd> 00 address, then dummy
// clocks, then the reply on D0). `len` is 1..4.
bool read_reg(uint8_t reg, uint8_t dummy_bits, uint8_t *out, uint8_t len) {
    spi_transaction_ext_t t;
    memset(&t, 0, sizeof(t));
    t.base.flags    = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR |
                      SPI_TRANS_USE_RXDATA | SPI_TRANS_VARIABLE_DUMMY;
    t.base.cmd      = 0x03;
    t.base.addr     = static_cast<uint32_t>(reg) << 8;
    t.base.rxlength = 8 * len;
    t.dummy_bits    = dummy_bits;

    cs_low();
    const esp_err_t err = spi_device_polling_transmit(
        s_spi_slow, reinterpret_cast<spi_transaction_t *>(&t));
    cs_high();

    memcpy(out, t.base.rx_data, len);
    return err == ESP_OK;
}

void set_address_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    const uint8_t col[4] = {uint8_t(x1 >> 8), uint8_t(x1), uint8_t(x2 >> 8), uint8_t(x2)};
    const uint8_t row[4] = {uint8_t(y1 >> 8), uint8_t(y1), uint8_t(y2 >> 8), uint8_t(y2)};
    send_cmd(0x2A, col, 4);   // column address set
    send_cmd(0x2B, row, 4);   // page address set
}

// Streams one chunk of pixels in the active write mode. `first` marks the
// chunk that opens the memory write. Returns the driver's verdict on the
// transaction so a refused one is visible instead of silent.
esp_err_t push_chunk(const uint16_t *p, size_t px, bool first) {
    spi_transaction_ext_t t;
    memset(&t, 0, sizeof(t));
    t.base.tx_buffer = p;
    t.base.length    = px * 16;

    switch (s_cfg.write) {
    case WriteMode::kQuadHeld:
        if (first) {
            cs_low();
            t.base.flags = SPI_TRANS_MODE_QIO;
            t.base.cmd   = 0x32;
            t.base.addr  = 0x002C00;
        } else {
            // CS still low from the previous chunk: no opcode, no address,
            // the panel is still inside the same memory write.
            t.base.flags   = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD |
                             SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t.command_bits = 0;
            t.address_bits = 0;
            t.dummy_bits   = 0;
        }
        return spi_device_polling_transmit(s_spi, reinterpret_cast<spi_transaction_t *>(&t));

    case WriteMode::kQuadContinue:
        t.base.flags = SPI_TRANS_MODE_QIO;
        t.base.cmd   = 0x32;
        t.base.addr  = first ? 0x002C00 : 0x003C00;
        if (!first) cs_high();
        cs_low();
        return spi_device_polling_transmit(s_spi, reinterpret_cast<spi_transaction_t *>(&t));

    case WriteMode::kSingle:
        t.base.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
        t.base.cmd   = 0x02;
        t.base.addr  = first ? 0x002C00 : 0x003C00;
        if (!first) cs_high();
        cs_low();
        return spi_device_polling_transmit(s_spi, reinterpret_cast<spi_transaction_t *>(&t));
    }
    return ESP_FAIL;
}

void fill_split(uint16_t top, uint16_t bottom, uint16_t split) {
    if (s_chunk == nullptr) return;
    uint16_t *chunk = s_chunk;
    const uint16_t rows_per_chunk = LCD_SEND_BUF_PIXELS / PANEL_WIDTH;

    set_address_window(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1);

    bool first = true;
    for (uint16_t row = 0; row < PANEL_HEIGHT; row += rows_per_chunk) {
        const uint16_t colour = (row < split) ? top : bottom;
        for (size_t i = 0; i < LCD_SEND_BUF_PIXELS; i++) chunk[i] = colour;
        const esp_err_t err = push_chunk(chunk, LCD_SEND_BUF_PIXELS, first);
        if (err != ESP_OK) Serial.printf("[panel] fill chunk at row %u refused by SPI driver: %d\n", row, err);
        first = false;
    }
    cs_high();
}

void apply_config(const PanelConfig &cfg) {
    s_cfg = cfg;
    reset_pulse();
    bus_apply(cfg.spi_mode, cfg.hz);
    run_init(cfg.init, cfg.init_len);
}

}  // namespace

void panel_init() {
    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_RST, OUTPUT);
    cs_high();

    s_chunk = static_cast<uint16_t *>(
        heap_caps_malloc(LCD_SEND_BUF_PIXELS * sizeof(uint16_t),
                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (s_chunk == nullptr) {
        Serial.println("[fatal] no DMA-capable SRAM for the panel chunk buffer");
        while (true) delay(1000);
    }

    apply_config(kResting);
    panel_report();
}

void panel_report() {
    // Ask the controller what it thinks its state is. On this glass every
    // register reads 0xFF with either dummy count, so the read opcode format
    // is wrong or unsupported here; the line is kept because it costs nothing
    // and a different panel revision may answer.
    uint8_t id[4] = {0}, pm = 0, colmod = 0;
    read_reg(0x04, 8, id, 4);
    read_reg(0x0A, 8, &pm, 1);
    read_reg(0x0C, 8, &colmod, 1);
    Serial.printf("[panel] readback RDDID=%02X%02X%02X%02X RDDPM=%02X COLMOD=%02X "
                  "(advisory: reads are unverified on this glass)\n",
                  id[0], id[1], id[2], id[3], pm, colmod);
}

void panel_boot_probe() {
    // One variable changes per step relative to step 1, which is the shipped
    // factory binary's configuration. Each step: hardware reset, init, fill
    // the glass turquoise over orange, hold, and report the power-mode read.
    // Round two. Round one showed the factory init table is the fault and the
    // write path is not, so every step here uses the working write path and
    // changes one thing about the factory table. Whichever lights names the
    // exact culprit.
    const PanelConfig steps[] = {
        {"factory table + 120ms after SLPIN (keeps the 32 zero bytes)", 0, LCD_SPI_FREQUENCY, TABLE(kInitFactoryDelayed), WriteMode::kQuadHeld},
        {"factory table without the 32 zero bytes (no delay)",          0, LCD_SPI_FREQUENCY, TABLE(kInitFactoryNoPad),   WriteMode::kQuadHeld},
        {"factory table + COLMOD 16bpp",                                0, LCD_SPI_FREQUENCY, TABLE(kInitFactoryColmod),  WriteMode::kQuadHeld},
        {"factory table + NORON",                                       0, LCD_SPI_FREQUENCY, TABLE(kInitFactoryNoron),   WriteMode::kQuadHeld},
        {"minimal DCS: 28/10/11/29 with delays, no padding, no extras",  0, LCD_SPI_FREQUENCY, TABLE(kInitMinimal),        WriteMode::kQuadHeld},
    };
    const size_t n = sizeof(steps) / sizeof(steps[0]);

    Serial.printf("[probe] %u steps, %dms each. Watch the glass and note EVERY step "
                  "that shows turquoise over orange.\n", (unsigned)n, PANEL_BOOT_PROBE_HOLD_MS);
    for (size_t i = 0; i < n; i++) {
        Serial.printf("[probe] step %u/%u: %s\n", (unsigned)(i + 1), (unsigned)n, steps[i].name);
        apply_config(steps[i]);
        fill_split(panel_rgb565(0x3F, 0xBF, 0xB0), panel_rgb565(0xE2, 0x70, 0x3A), PANEL_HEIGHT / 2);
        uint8_t pm = 0;
        read_reg(0x0A, 8, &pm, 1);
        Serial.printf("[probe]   RDDPM=0x%02X  (holding)\n", pm);
        delay(PANEL_BOOT_PROBE_HOLD_MS);
    }
    Serial.println("[probe] done; back to the resting configuration");
    apply_config(kResting);
}

void panel_push_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *pixels) {
    if (pixels == nullptr || w == 0 || h == 0) return;
    s_flush_count++;

    set_address_window(x, y, x + w - 1, y + h - 1);

    size_t remaining = static_cast<size_t>(w) * h;
    const uint16_t *p = pixels;
    bool first = true;
    while (remaining > 0) {
        const size_t chunk = remaining > LCD_SEND_BUF_PIXELS ? LCD_SEND_BUF_PIXELS : remaining;
        push_chunk(p, chunk, first);
        first = false;
        remaining -= chunk;
        p += chunk;
    }
    cs_high();
}

void panel_push_frame(const uint16_t *frame) {
    if (frame == nullptr || s_chunk == nullptr) return;
    s_flush_count++;

    constexpr uint16_t kRows = LCD_SEND_BUF_PIXELS / PANEL_WIDTH;   // 80
    set_address_window(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1);

    bool first = true;
    for (uint16_t y0 = 0; y0 < PANEL_HEIGHT; y0 += kRows) {
        // Gather one band of panel rows [y0, y0+kRows) from the unrotated
        // frame. The loops are ordered so the frame - which lives in PSRAM -
        // is read along its rows (contiguous, cache-friendly) and the scatter
        // lands in internal SRAM where strided writes are cheap.
        //
        //   ROT_270: panel(x, y) = frame(X = y,       Y = 179 - x)
        //   ROT_90:  panel(x, y) = frame(X = 639 - y, Y = x)
        //
        // These are LVGL's own definitions of the two rotations (lv_refr.c,
        // draw_buf_rotate), kept so UI_ROTATION means the same thing it did
        // when LVGL was doing the turning.
        for (uint16_t x = 0; x < PANEL_WIDTH; x++) {
            uint16_t *dst = s_chunk + x;
            if (UI_ROTATION == LV_DISP_ROT_270) {
                const uint16_t *src = frame + size_t(PANEL_WIDTH - 1 - x) * UI_WIDTH + y0;
                for (uint16_t r = 0; r < kRows; r++) { *dst = src[r]; dst += PANEL_WIDTH; }
            } else {
                const uint16_t *src = frame + size_t(x) * UI_WIDTH + (UI_WIDTH - 1 - y0);
                for (uint16_t r = 0; r < kRows; r++) { *dst = *src--; dst += PANEL_WIDTH; }
            }
        }
        push_chunk(s_chunk, LCD_SEND_BUF_PIXELS, first);
        first = false;
    }
    cs_high();
}

uint32_t panel_flush_count() { return s_flush_count; }

uint16_t panel_rgb565(uint8_t r, uint8_t g, uint8_t b) {
    const uint16_t v = uint16_t((r & 0xF8) << 8) | uint16_t((g & 0xFC) << 3) | uint16_t(b >> 3);
    // The panel takes the high byte first and the ESP32 stores the low byte
    // first, so swap - the same reason LV_COLOR_16_SWAP is 1 in lv_conf.h.
    return uint16_t((v << 8) | (v >> 8));
}

void panel_fill_split(uint16_t top, uint16_t bottom, uint16_t split) {
    fill_split(top, bottom, split);
}

void panel_sleep() {
    send_cmd(0x28, nullptr, 0);   // display off
    send_cmd(0x10, nullptr, 0);   // sleep in
}

void panel_wake() {
    send_cmd(0x11, nullptr, 0);   // sleep out
    delay(120);
    send_cmd(0x29, nullptr, 0);   // display on
}
