#include "panel.h"

#include <Arduino.h>
#include <string.h>
#include "driver/spi_master.h"
#include <esp_heap_caps.h>

#include "pins.h"

namespace {

spi_device_handle_t s_spi = nullptr;
uint32_t s_flush_count = 0;

struct LcdCmd {
    uint8_t cmd;
    uint8_t data[36];
    uint8_t len;   // bit7: delay 200ms after, bit6: delay 20ms after, bits0-5: byte count
};

// The vendor's QSPI init sequence, reproduced verbatim. It is terse because the
// AXS15231B on this board boots with a usable register set already loaded from
// its own OTP; all this does is wake it and turn the display on.
const LcdCmd kInitSequence[] = {
    {0x28, {0x00}, 0x40},   // display off,  +20ms
    {0x10, {0x00}, 0x20},   // sleep in
    {0x11, {0x00}, 0x80},   // sleep out,    +200ms
    {0x29, {0x00}, 0x00},   // display on
};

inline void cs_low()  { digitalWrite(PIN_LCD_CS, LOW); }
inline void cs_high() { digitalWrite(PIN_LCD_CS, HIGH); }

// Single command + parameters, sent on one data line (QSPI cmd 0x02 path).
void send_cmd(uint8_t cmd, const uint8_t *data, uint32_t len) {
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
    spi_device_polling_transmit(s_spi, &t);

    cs_high();
}

void set_address_window(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2) {
    const uint8_t col[4] = {uint8_t(x1 >> 8), uint8_t(x1), uint8_t(x2 >> 8), uint8_t(x2)};
    const uint8_t row[4] = {uint8_t(y1 >> 8), uint8_t(y1), uint8_t(y2 >> 8), uint8_t(y2)};
    send_cmd(0x2A, col, 4);   // column address set
    send_cmd(0x2B, row, 4);   // page address set
}

}  // namespace

void panel_init() {
    pinMode(PIN_LCD_CS, OUTPUT);
    pinMode(PIN_LCD_RST, OUTPUT);
    cs_high();

    // Reset pulse. The generous delays are the vendor's; the panel's internal
    // regulator needs the settling time and shortening them produces an
    // intermittently blank display that looks like a wiring fault.
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(130);
    digitalWrite(PIN_LCD_RST, LOW);
    delay(130);
    digitalWrite(PIN_LCD_RST, HIGH);
    delay(300);

    spi_bus_config_t buscfg = {};
    buscfg.data0_io_num   = PIN_LCD_D0;
    buscfg.data1_io_num   = PIN_LCD_D1;
    buscfg.sclk_io_num    = PIN_LCD_SCK;
    buscfg.data2_io_num   = PIN_LCD_D2;
    buscfg.data3_io_num   = PIN_LCD_D3;
    buscfg.max_transfer_sz = (LCD_SEND_BUF_PIXELS * 16) + 8;
    buscfg.flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS;

    spi_device_interface_config_t devcfg = {};
    devcfg.command_bits    = 8;
    devcfg.address_bits    = 24;
    devcfg.mode            = 0;              // SPI_MODE0
    devcfg.clock_speed_hz  = LCD_SPI_FREQUENCY;
    devcfg.spics_io_num    = -1;             // CS is driven by hand, see below
    devcfg.flags           = SPI_DEVICE_HALFDUPLEX;
    devcfg.queue_size      = 17;

    ESP_ERROR_CHECK(spi_bus_initialize(LCD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &devcfg, &s_spi));

    for (const LcdCmd &c : kInitSequence) {
        send_cmd(c.cmd, c.data, c.len & 0x3F);
        if (c.len & 0x80) delay(200);
        if (c.len & 0x40) delay(20);
    }
}

void panel_push_pixels(uint16_t x, uint16_t y, uint16_t w, uint16_t h,
                       const uint16_t *pixels) {
    if (pixels == nullptr || w == 0 || h == 0) return;
    s_flush_count++;

    set_address_window(x, y, x + w - 1, y + h - 1);

    size_t remaining = static_cast<size_t>(w) * h;
    const uint16_t *p = pixels;
    bool first = true;

    // CS is toggled manually around each chunk rather than delegated to the SPI
    // peripheral. The AXS15231B latches the write-continue on the CS edge, so a
    // hardware-managed CS that stays asserted across the whole frame makes the
    // panel treat every chunk after the first as a fresh 0x2C write and stack
    // them all in the top-left corner.
    while (remaining > 0) {
        size_t chunk = remaining > LCD_SEND_BUF_PIXELS ? LCD_SEND_BUF_PIXELS : remaining;

        spi_transaction_ext_t t;
        memset(&t, 0, sizeof(t));
        t.base.flags     = SPI_TRANS_MODE_QIO;
        t.base.cmd       = 0x32;
        t.base.addr      = first ? 0x002C00   // memory write
                                 : 0x003C00;  // memory write continue
        t.base.tx_buffer = p;
        t.base.length    = chunk * 16;

        if (!first) cs_high();
        cs_low();
        spi_device_polling_transmit(s_spi, reinterpret_cast<spi_transaction_t *>(&t));

        first = false;
        remaining -= chunk;
        p += chunk;
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
    // One chunk's worth of a solid colour, streamed repeatedly. 14400 px is
    // 80 full rows of the 180-wide panel, so rows divide evenly. Heap rather
    // than static: this runs once at boot and 28KB is too much to keep in
    // .bss for the rest of the device's life.
    auto *chunk = static_cast<uint16_t *>(
        heap_caps_malloc(LCD_SEND_BUF_PIXELS * sizeof(uint16_t),
                         MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL));
    if (chunk == nullptr) return;
    const uint16_t rows_per_chunk = LCD_SEND_BUF_PIXELS / PANEL_WIDTH;

    set_address_window(0, 0, PANEL_WIDTH - 1, PANEL_HEIGHT - 1);

    bool first = true;
    for (uint16_t row = 0; row < PANEL_HEIGHT; row += rows_per_chunk) {
        const uint16_t colour = (row < split) ? top : bottom;
        for (size_t i = 0; i < LCD_SEND_BUF_PIXELS; i++) chunk[i] = colour;

        spi_transaction_ext_t t;
        memset(&t, 0, sizeof(t));
        t.base.flags     = SPI_TRANS_MODE_QIO;
        t.base.cmd       = 0x32;
        t.base.addr      = first ? 0x002C00 : 0x003C00;
        t.base.tx_buffer = chunk;
        t.base.length    = size_t(LCD_SEND_BUF_PIXELS) * 16;

        if (!first) cs_high();
        cs_low();
        spi_device_polling_transmit(s_spi, reinterpret_cast<spi_transaction_t *>(&t));
        first = false;
    }
    cs_high();
    heap_caps_free(chunk);
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
