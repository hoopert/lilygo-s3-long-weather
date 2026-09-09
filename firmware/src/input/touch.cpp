#include "touch.h"

#include <Arduino.h>
#include <Wire.h>

#include "config.h"

namespace {

TouchChip s_chip = TouchChip::None;

bool probe(uint8_t addr) {
    Wire.beginTransmission(addr);
    return Wire.endTransmission() == 0;
}

// --- AXS15231B ------------------------------------------------------------
// The display controller exposes touch through a fixed 8-byte "read touchpad"
// command; the reply is a gesture byte, a point count, and then six bytes per
// point with the high nibbles of each coordinate packed into the top of the
// low bytes.
const uint8_t kAxsReadCmd[8] = {0xB5, 0xAB, 0xA5, 0x5A, 0x00, 0x00, 0x00, 0x08};

bool read_axs(TouchPoint &out) {
    Wire.beginTransmission(TOUCH_ADDR_AXS15231B);
    Wire.write(kAxsReadCmd, sizeof(kAxsReadCmd));
    if (Wire.endTransmission() != 0) return false;

    uint8_t buf[8] = {0};
    if (Wire.requestFrom(uint8_t(TOUCH_ADDR_AXS15231B), uint8_t(sizeof(buf))) != sizeof(buf)) {
        return false;
    }
    for (uint8_t &b : buf) b = Wire.read();

    const uint8_t points = buf[1];
    if (points == 0 || points > 5) {
        out.pressed = false;
        return true;
    }

    out.x = (uint16_t(buf[2] & 0x0F) << 8) | buf[3];
    out.y = (uint16_t(buf[4] & 0x0F) << 8) | buf[5];
    out.pressed = true;
    return true;
}

// --- CST3530 (Hynitron CST66xx family) ------------------------------------
// Report register is 0xD0070000, written big-endian as a 4-byte address. The
// reply is: u16 checksum, report type, a packed finger/key count, then five
// bytes per point. After each read the controller must be re-armed by writing
// 0xD00002AB or it will not raise another report.
uint16_t sum16(uint16_t seed, const uint8_t *buf, size_t len) {
    uint16_t sum = seed;
    while (len--) sum += *buf++;
    return sum;
}

bool cst_write_reg(uint32_t reg) {
    Wire.beginTransmission(TOUCH_ADDR_CST66XX);
    Wire.write(uint8_t(reg >> 24));
    Wire.write(uint8_t(reg >> 16));
    Wire.write(uint8_t(reg >> 8));
    Wire.write(uint8_t(reg));
    return Wire.endTransmission() == 0;
}

bool read_cst(TouchPoint &out) {
    if (!cst_write_reg(0xD0070000)) return false;

    uint8_t buf[9] = {0};
    if (Wire.requestFrom(uint8_t(TOUCH_ADDR_CST66XX), uint8_t(sizeof(buf))) != sizeof(buf)) {
        return false;
    }
    for (uint8_t &b : buf) b = Wire.read();

    cst_write_reg(0xD00002AB);   // re-arm; a missed write stalls all reporting

    const uint8_t report_type = buf[2];
    const uint8_t fingers     = buf[3] & 0x0F;
    const uint8_t keys        = (buf[3] & 0xF0) >> 4;

    if (report_type != 0xFF || fingers == 0 || fingers + keys > 5) {
        out.pressed = false;
        return true;
    }

    // We only ever use the first contact, so only the single-contact case can
    // be checksummed from this one 9-byte read. Multi-touch reports carry their
    // remaining points in a follow-up read we do not issue.
    if (fingers + keys == 1 && sum16(0x55, &buf[4], 5) != uint16_t(buf[0] | (buf[1] << 8))) {
        return false;
    }

    const size_t idx = size_t(keys) * 5;   // skip any key entries
    if (idx + 8 >= sizeof(buf)) {
        out.pressed = false;
        return true;
    }

    const uint8_t event = buf[idx + 8] >> 4;
    if (event == 0) {          // contact lifted
        out.pressed = false;
        return true;
    }

    out.x = uint16_t(buf[idx + 4]) + (uint16_t(buf[idx + 7] & 0x0F) << 8);
    out.y = uint16_t(buf[idx + 5]) + (uint16_t(buf[idx + 7] & 0xF0) << 4);
    out.pressed = true;
    return true;
}

}  // namespace

bool touch_init() {
    pinMode(PIN_TP_RST, OUTPUT);
    digitalWrite(PIN_TP_RST, LOW);
    delay(20);
    digitalWrite(PIN_TP_RST, HIGH);
    delay(60);   // both controllers need ~50ms before they will ACK

    pinMode(PIN_TP_IRQ, INPUT_PULLUP);

    Wire.begin(PIN_TP_SDA, PIN_TP_SCL, 400000);

    if (probe(TOUCH_ADDR_AXS15231B)) {
        s_chip = TouchChip::AXS15231B;
    } else if (probe(TOUCH_ADDR_CST66XX)) {
        s_chip = TouchChip::CST66XX;
    } else {
        s_chip = TouchChip::None;
    }

    Serial.printf("[touch] controller: %s\n", touch_chip_name());
    return s_chip != TouchChip::None;
}

namespace { TouchPoint s_last = {false, 0, 0}; }

TouchPoint touch_last() { return s_last; }

TouchPoint touch_read() {
    TouchPoint &last = s_last;

    TouchPoint p = last;
    p.pressed = false;

    bool ok = false;
    switch (s_chip) {
        case TouchChip::AXS15231B: ok = read_axs(p); break;
        case TouchChip::CST66XX:   ok = read_cst(p); break;
        case TouchChip::None:      return {false, 0, 0};
    }

    // A failed bus transaction is far more likely to be noise than a genuine
    // release, so hold the previous state rather than injecting a phantom lift
    // that would abort an in-progress drag.
    if (!ok) return last;

    if (p.pressed) {
        if (p.x >= PANEL_WIDTH)  p.x = PANEL_WIDTH - 1;
        if (p.y >= PANEL_HEIGHT) p.y = PANEL_HEIGHT - 1;
        if (TOUCH_INVERT_X) p.x = (PANEL_WIDTH - 1) - p.x;
        if (TOUCH_INVERT_Y) p.y = (PANEL_HEIGHT - 1) - p.y;
    }

    last = p;
    return p;
}

TouchChip touch_chip() { return s_chip; }

const char *touch_chip_name() {
    switch (s_chip) {
        case TouchChip::AXS15231B: return "AXS15231B @ 0x3B";
        case TouchChip::CST66XX:   return "CST3530 @ 0x58";
        default:                   return "none detected";
    }
}
