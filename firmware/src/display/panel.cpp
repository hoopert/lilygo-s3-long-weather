#include "panel.h"

#include <Arduino.h>
#include <string.h>
#include "driver/spi_master.h"
#include <esp_heap_caps.h>

#include "pins.h"

namespace {

spi_device_handle_t s_spi = nullptr;        // 32MHz, writes
spi_device_handle_t s_spi_slow = nullptr;   // 4MHz, register reads only
uint32_t s_flush_count = 0;

struct LcdCmd {
    uint8_t  cmd;
    uint8_t  data[4];
    uint8_t  len;        // parameter bytes
    uint16_t delay_ms;   // settle time after the command
};

// Init sequence. It is the union of the two sequences known to light this
// exact glass, in the order the MIPI DCS spec wants them:
//
//   - LilyGO's factory driver (examples/factory/AXS15231B.cpp) sends only
//     DISPOFF, SLPIN, SLPOUT, DISPON and relies on the controller's OTP
//     defaults for everything else. It works on their bench, but their own
//     source carries a comment about re-running it "to prevent initialization
//     failure", which is not confidence-inspiring on a panel that shows black.
//   - Arduino_GFX's Arduino_AXS15231 (LilyGO ships an example on it too)
//     states the things the factory driver leaves to chance: normal display
//     mode, inversion off, 16-bit pixel format, brightness-control block.
//
// Nothing here is specific to a panel batch; every register below is a
// standard DCS user command. The vendor's long manufacturer-register table
// (axs15231b_qspi_init_new) is deliberately not used - it retunes gamma and
// power for one particular glass and is unused in their shipped firmware.
const LcdCmd kInitSequence[] = {
    {0x28, {0},    0,  20},   // DISPOFF
    {0x10, {0},    0, 120},   // SLPIN
    {0x11, {0},    0, 200},   // SLPOUT  - the long one, panel regulator settles
    {0x13, {0},    0,   0},   // NORON   - normal (not partial) display mode
    {0x20, {0},    0,   0},   // INVOFF
    {0x3A, {0x05}, 1,   0},   // COLMOD  - 16 bits per pixel, RGB565
    {0x29, {0},    0,  20},   // DISPON
    {0x53, {0x28}, 1,   0},   // WRCTRLD - brightness control + dimming on
    {0x51, {0x00}, 1,   0},   // WRDISBV - panel-side brightness (unused; BL is a GPIO)
    {0x58, {0x00}, 1,  10},   // WRCE    - sunlight-readability enhancement off
};

inline void cs_low()  { digitalWrite(PIN_LCD_CS, LOW); }
inline void cs_high() { digitalWrite(PIN_LCD_CS, HIGH); }

// Single command + parameters, sent on one data line (QSPI opcode 0x02: the
// controller takes 0x02, then a 24-bit "address" of 00 <cmd> 00, then the
// parameter bytes).
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

// Register read (QSPI opcode 0x03, same 00 <cmd> 00 address, then dummy
// clocks, then the reply on D0). Goes over the slow device so the SPI driver
// does not insert its own timing-compensation dummy bits and shift the reply.
// `len` is 1..4. Returns false only if the SPI driver refused the transaction;
// a panel that is not answering returns true with all-0x00 or all-0xFF data.
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

    // A second handle on the same bus for reads. Slow enough that the driver
    // needs no compensation dummy cycles (NO_DUMMY makes it refuse if not).
    spi_device_interface_config_t slowcfg = devcfg;
    slowcfg.clock_speed_hz = 4000000;
    slowcfg.flags          = SPI_DEVICE_HALFDUPLEX | SPI_DEVICE_NO_DUMMY;
    slowcfg.queue_size     = 1;
    ESP_ERROR_CHECK(spi_bus_add_device(LCD_SPI_HOST, &slowcfg, &s_spi_slow));

    for (const LcdCmd &c : kInitSequence) {
        send_cmd(c.cmd, c.data, c.len);
        if (c.delay_ms) delay(c.delay_ms);
    }

    panel_report();
}

void panel_report() {
    // Ask the controller what state it thinks it is in. This is the one line
    // that separates "the panel never heard us" from "the panel is on and
    // showing what we send" without a scope on the bus.
    //
    // The AXS15231B's read opcode wants dummy clocks between address and data;
    // the datasheet is thin on how many, so both plausible counts are shown.
    // Whichever column decodes sensibly is the right one; if both are all
    // 00 or all FF for every register, the panel is not answering at all.
    struct Reg { uint8_t reg; uint8_t len; const char *name; };
    const Reg regs[] = {
        {0x04, 4, "RDDID   "},   // manufacturer / version / driver id
        {0x0A, 1, "RDDPM   "},   // power mode: bit7 booster, bit4 sleep-out, bit3 normal, bit2 display-on
        {0x0C, 1, "RDDCOLMOD"},  // pixel format: 0x05 = 16bpp
        {0x0D, 1, "RDDIM   "},   // image mode: bit5 inversion
    };
    bool any_signal = false;
    for (const Reg &r : regs) {
        uint8_t d8[4] = {0}, d0[4] = {0};
        read_reg(r.reg, 8, d8, r.len);
        read_reg(r.reg, 0, d0, r.len);
        Serial.printf("[panel] %s (%02X)  dummy8:", r.name, r.reg);
        for (uint8_t i = 0; i < r.len; i++) Serial.printf(" %02X", d8[i]);
        Serial.print("  dummy0:");
        for (uint8_t i = 0; i < r.len; i++) Serial.printf(" %02X", d0[i]);
        Serial.println();
        for (uint8_t i = 0; i < r.len; i++) {
            if ((d8[i] != 0x00 && d8[i] != 0xFF) || (d0[i] != 0x00 && d0[i] != 0xFF)) any_signal = true;
        }
    }

    uint8_t pm = 0;
    read_reg(0x0A, 8, &pm, 1);
    if (!any_signal) {
        Serial.println("[panel] no reply on QSPI: every register reads 00/FF. Either the "
                       "controller is not powered/reset, or the read opcode format "
                       "is not what this revision speaks (writes may still work).");
    } else {
        Serial.printf("[panel] power mode 0x%02X: booster %s, sleep %s, %s mode, display %s\n",
                      pm,
                      (pm & 0x80) ? "on" : "OFF",
                      (pm & 0x10) ? "out" : "IN",
                      (pm & 0x08) ? "normal" : "partial",
                      (pm & 0x04) ? "ON" : "OFF");
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
