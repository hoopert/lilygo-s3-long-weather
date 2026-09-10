// Barometric outlook and body-effect rules, from design/logic.json.
//
// Pure functions over plain numbers: no LVGL, no Arduino, no globals. That is
// what lets tools/check_pressure_logic.py compile this file with the host's
// g++ and check it against the JSON on every push - the one part of the
// design that is a specification rather than a picture, tested as one.
//
// Units throughout: pressure in hPa at mean sea level (Open-Meteo's
// `pressure_msl`, never `surface_pressure` - at Denver's altitude the surface
// reading is ~830 hPa and every threshold here would fire at once), humidity
// in percent, temperature in Celsius, and delta_3h = now - three hours ago.
#pragma once

#include <stddef.h>
#include <stdint.h>

enum class PressureBand : uint8_t {
    StormRisk,      // delta <= -3.0
    ClearingSlow,   // -3.0 <  delta <= -1.0
    Clear,          // -1.0 <  delta <   1.0
    Clearing,       //  1.0 <= delta <   3.0
    WindRisk,       //  3.0 <= delta
    Unknown,        // no history yet
};

struct PressureOutlook {
    PressureBand band;
    const char  *word;      // "Storm Risk", "Clear", ... - Body 20 in the UI
    const char  *caption;   // "FALLING FAST", "STEADY", ... - Micro
    bool         risk;      // true for the two outer bands: render in sunset
};

enum class RiskLevel : uint8_t { Low, Medium, High };

struct PressureRisks {
    RiskLevel joint_pain;
    RiskLevel migraine;
    RiskLevel sinus_ears;
    RiskLevel heart_strain;
    bool      any_high;     // bubbles sunset up to the outlook word and glyph
};

// Band edges: the negative limits are inclusive and the positive ones are
// exclusive, so exactly -3.0 is Storm Risk and exactly +3.0 is Wind Risk, as
// the SPEC's "<= -3.0" and ">= +3.0" say. A NaN delta is Unknown.
PressureOutlook pressure_outlook(float delta_3h);

// The four biometric rules. A NaN delta counts as 0 (steady) so the
// pressure- and temperature-only clauses still apply; a NaN pressure,
// humidity or temperature makes its clause false.
PressureRisks pressure_risks(float delta_3h, float pressure_hpa,
                             float humidity_pct, float temp_c);

// now - history[count - 1 - 3], for an hourly history whose last sample is
// the current hour. NaN when there are not four samples to compare.
float pressure_delta_3h(const float *history, size_t count, float now);

const char *risk_word(RiskLevel level);   // "LOW", "MEDIUM", "HIGH"
